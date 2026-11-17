/*
 * CombatSim.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "CombatSim.h"

#include "../../../lib/CBonusTypeHandler.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/IBonusBearer.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/constants/EntityIdentifiers.h"

namespace FAI::Combat
{

namespace
{
double average(const DamageRange & range)
{
	return (range.min + range.max) / 2.0;
}

constexpr int MAX_SIM_STEPS = 600;

/// Death stare, the game's rule: every creature of the stack kills one creature of the target with
/// that chance, but never more than the stack's size times the chance, rounded up. The expected
/// number of kills.
double stareKills(int count, double chance)
{
	if(count <= 0 || chance <= 0)
		return 0;
	const int cap = static_cast<int>(std::ceil(count * std::min(1.0, chance) - 1e-9));
	if(chance >= 1.0)
		return cap;
	if(count <= 40)
	{
		double expected = 0;
		double p = std::pow(1.0 - chance, count); // exactly no kill
		for(int k = 0; k <= count; ++k)
		{
			expected += std::min(k, cap) * p;
			p *= static_cast<double>(count - k) / (k + 1) * chance / (1.0 - chance);
		}
		return expected;
	}
	// large stacks: the bell curve, cut off at the cap
	const double mean = count * chance;
	const double sigma = std::sqrt(mean * (1.0 - chance));
	const double z = (cap - mean) / sigma;
	const double density = std::exp(-0.5 * z * z) / std::sqrt(2.0 * 3.14159265358979323846);
	const double tail = 0.5 * std::erfc(z / std::sqrt(2.0));
	return mean - sigma * (density - z * tail);
}

/// how much a point of attack or defence changes damage, between the game's 5 % (attack above
/// defence) and 2.5 % (below)
constexpr double SKILL_POINT = 0.035;
}

// ------------------------------------------------------------------ DamageTable

namespace
{
std::atomic<bool> multiHexEnabled{false};
std::atomic<bool> abilitiesBeyondDamage{false};

/// a kind of bonus that the game's files define (not one of the built-in ones), by its name
BonusType bonusNamed(const std::string & name)
{
	for(BonusType type : LIBRARY->bth->getAllObjets())
	{
		if(LIBRARY->bth->bonusToString(type) == name)
			return type;
	}
	return BonusType::NONE;
}

/// the lasting bonuses a spell gives, by the few kinds the simulator knows
std::string effectKind(BonusType type)
{
	switch(type)
	{
	case BonusType::NOT_ACTIVE: return "NOT_ACTIVE";
	case BonusType::HYPNOTIZED: return "HYPNOTIZED";
	case BonusType::ALWAYS_MINIMUM_DAMAGE: return "ALWAYS_MINIMUM_DAMAGE";
	case BonusType::POISON: return "POISON";
	case BonusType::STACK_HEALTH: return "STACK_HEALTH";
	case BonusType::PRIMARY_SKILL: return "PRIMARY_SKILL";
	default: return std::string();
	}
}

/// attacks that reach beyond the target do not spare the attacker's own side
bool canHitOwn(const IBonusBearer & unit)
{
	if(!multiHexEnabled)
		return false;
	return unit.hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH) || unit.hasBonusOfType(BonusType::WIDE_BREATH)
		|| unit.hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH) || unit.hasBonusOfType(BonusType::MULTIHEX_UNIT_ATTACK)
		|| unit.hasBonusOfType(BonusType::SPELL_LIKE_ATTACK);
}
}

void DamageTable::build(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & units, bool abilities)
{
	n = units.size();
	chargeDmg.clear();
	meleeDmg.assign(n * n, 0.0);
	rangedNear.assign(n * n, 0.0);
	rangedFar.assign(n * n, 0.0);

	for(size_t a = 0; a < n; ++a)
	{
		const battle::Unit * attacker = units[a];
		const double count = std::max(1, attacker->getCount());
		const bool hitsOwn = canHitOwn(*attacker);

		for(size_t d = 0; d < n; ++d)
		{
			const battle::Unit * defender = units[d];
			if(a == d)
				continue;
			if(attacker->unitSide() == defender->unitSide())
			{
				// breath and bursting shots do not tell friend from foe
				if(hitsOwn)
				{
					const CCreature * attackerType = attacker->creatureId().toCreature();
					const CCreature * defenderType = defender->creatureId().toCreature();
					meleeDmg[a * n + d] = formula(*attacker, attackerType, *defender, defenderType, false);
					if(attacker->isShooter())
						rangedNear[a * n + d] = rangedFar[a * n + d] = formula(*attacker, attackerType, *defender, defenderType, true);
				}
				continue;
			}

			BattleAttackInfo meleeInfo(attacker, defender, 0, false);
			meleeDmg[a * n + d] = average(cb.battleEstimateDamage(meleeInfo).damage) / count;
			if(abilities && attacker->hasBonusOfType(BonusType::JOUSTING))
			{
				if(chargeDmg.empty())
					chargeDmg.assign(n * n, 0.0);
				BattleAttackInfo chargeInfo(attacker, defender, 1, false);
				chargeDmg[a * n + d] = std::max(0.0, average(cb.battleEstimateDamage(chargeInfo).damage) / count - meleeDmg[a * n + d]);
			}

			if(attacker->isShooter())
			{
				BattleAttackInfo rangedInfo(attacker, defender, 0, true);
				double perCreature = average(cb.battleEstimateDamage(rangedInfo).damage) / count;
				bool penaltyNow = cb.battleHasDistancePenalty(attacker, attacker->getPosition(), defender->getPosition());
				double nearValue = penaltyNow ? perCreature * 2 : perCreature;
				rangedNear[a * n + d] = nearValue;
				rangedFar[a * n + d] = attacker->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY) ? nearValue : nearValue / 2;
			}
		}
	}
}

double DamageTable::formula(const ACreature & attacker, const CCreature * attackerType, const ACreature & defender, const CCreature * defenderType, bool shooting, int chargeDistance, int attackBonus, int defenceBonus)
{
	// Mirrors lib/battle/DamageCalculator.cpp for everything that does not depend on dice, positions
	// or spells in effect. unit_lab.py checks it against the game for every pair it sees in a battle.
	const IBonusBearer & a = *attacker.getBonusBearer();
	const IBonusBearer & d = *defender.getBonusBearer();
	static const auto noLimit = Selector::effectRange()(BonusLimitEffect::NO_LIMIT);
	const auto inRange = noLimit.Or(Selector::effectRange()(shooting ? BonusLimitEffect::ONLY_DISTANCE_FIGHT : BonusLimitEffect::ONLY_MELEE_FIGHT));
	auto ranged = [&inRange](const IBonusBearer & bearer, BonusType type)
	{
		return bearer.getBonusesOfType(type)->valOfBonuses(inRange);
	};

	const double base = (attacker.getMinDamage(shooting) + attacker.getMaxDamage(shooting)) / 2.0;

	int attack = a.getBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)))->valOfBonuses(inRange);
	int defence = d.getBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)))->valOfBonuses(inRange);
	attack = std::max(0, attack + attackBonus);
	defence = std::max(0, defence + defenceBonus);
	if(const int percent = ranged(d, BonusType::ENEMY_ATTACK_REDUCTION); percent > 0)
		attack -= std::min(attack, static_cast<int>(std::lround(attack * percent / 100.0)));
	if(const double share = ranged(a, BonusType::ENEMY_DEFENCE_REDUCTION) / 100.0; share > 0)
		defence -= std::min(defence, static_cast<int>(std::floor(share * defence)) + 1);

	const auto & settings = *LIBRARY->engineSettings();
	double attackFactor = 1.0;
	double defenceFactor = 1.0;
	if(attack > defence)
		attackFactor += std::min(settings.getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR) * (attack - defence),
			settings.getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR_CAP));
	else if(defence > attack)
		defenceFactor *= 1.0 - std::min(settings.getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR) * (defence - attack),
			settings.getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR_CAP));

	// offence / archery, bless-like bonuses, hate, charge
	attackFactor += a.valOfBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST, shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee)) / 100.0;
	attackFactor += a.valOfBonuses(BonusType::GENERAL_DAMAGE_PREMY) / 100.0;
	if(defenderType)
		attackFactor += a.getBonusesOfType(BonusType::HATE)->valOfBonuses(Selector::subtype()(BonusSubtypeID(defenderType->getId()))) / 100.0;
	for(const auto & hate : *a.getBonusesOfType(BonusType::HATES_TRAIT))
	{
		if(d.hasBonusOfType(hate->subtype.as<BonusTypeID>().toEnum()))
			attackFactor += hate->val / 100.0;
	}
	if(!shooting && chargeDistance > 0 && a.hasBonusOfType(BonusType::JOUSTING) && !d.hasBonusOfType(BonusType::CHARGE_IMMUNITY))
		attackFactor += chargeDistance * a.valOfBonuses(BonusType::JOUSTING) / 100.0;

	// armourer and shields
	auto reduce = [&defenceFactor](double share)
	{
		defenceFactor *= 1.0 - std::clamp(share, 0.0, 1.0);
	};
	reduce(d.valOfBonuses(Selector::typeSubtype(BonusType::GENERAL_DAMAGE_REDUCTION, BonusCustomSubtype::damageTypeAll)) / 100.0);
	reduce(d.valOfBonuses(Selector::typeSubtype(BonusType::GENERAL_DAMAGE_REDUCTION, shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee)) / 100.0);
	reduce(ranged(a, BonusType::GENERAL_ATTACK_REDUCTION) / 100.0);
	// shooters fight badly hand to hand
	if(!shooting && a.valOfBonuses(BonusType::SHOTS) > 0 && !a.hasBonusOfType(BonusType::NO_MELEE_PENALTY))
		reduce(0.5);
	// the two elementals with their own rules
	if(attackerType && attackerType->getId() == CreatureID::MAGIC_ELEMENTAL && d.valOfBonuses(BonusType::LEVEL_SPELL_IMMUNITY) >= 5)
		reduce(0.5);
	if(attackerType && attackerType->getId() == CreatureID::PSYCHIC_ELEMENTAL && d.hasBonusOfType(BonusType::MIND_IMMUNITY))
		reduce(0.5);

	double damage = std::max(1.0, base * attackFactor * defenceFactor);
	if(const int cap = d.valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP); cap > 0)
		damage = std::min(damage, defender.getMaxHealth() * cap / 100.0);
	return damage;
}

void DamageTable::build(const std::vector<StackSpec> & stacks)
{
	n = stacks.size();
	chargeDmg.clear();
	meleeDmg.assign(n * n, 0.0);
	rangedNear.assign(n * n, 0.0);
	rangedFar.assign(n * n, 0.0);
	for(size_t a = 0; a < n; ++a)
	{
		const StackSpec & attacker = stacks[a];
		const IBonusBearer & bearer = *attacker.bearer->getBonusBearer();
		const bool shooter = bearer.valOfBonuses(BonusType::SHOTS) > 0;
		const bool hitsOwn = canHitOwn(bearer);
		for(size_t d = 0; d < n; ++d)
		{
			const StackSpec & defender = stacks[d];
			if(a == d || (attacker.side == defender.side && !hitsOwn))
				continue;
			meleeDmg[a * n + d] = formula(*attacker.bearer, attacker.type, *defender.bearer, defender.type, false, 0, attacker.attackBonus, defender.defenceBonus);
			if(bearer.hasBonusOfType(BonusType::JOUSTING))
			{
				if(chargeDmg.empty())
					chargeDmg.assign(n * n, 0.0);
				chargeDmg[a * n + d] = std::max(0.0, formula(*attacker.bearer, attacker.type, *defender.bearer, defender.type, false, 1, attacker.attackBonus, defender.defenceBonus) - meleeDmg[a * n + d]);
			}
			if(shooter)
			{
				const double nearValue = formula(*attacker.bearer, attacker.type, *defender.bearer, defender.type, true, 0, attacker.attackBonus, defender.defenceBonus);
				rangedNear[a * n + d] = nearValue;
				rangedFar[a * n + d] = bearer.hasBonusOfType(BonusType::NO_DISTANCE_PENALTY) ? nearValue : nearValue / 2;
			}
		}
	}
}

// ------------------------------------------------------------------ SimState: setup

void SimState::init(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & battleUnits, const DamageTable & table, bool abilities)
{
	damage = &table;
	units.clear();
	units.reserve(battleUnits.size());

	AccessibilityInfo access = cb.getAccessibility();
	for(int h = 0; h < FIELD_SIZE; ++h)
	{
		auto a = access[h];
		occupant[h] = (a == EAccessibility::ACCESSIBLE || a == EAccessibility::ALIVE_STACK) ? -1 : -2;
	}

	for(const battle::Unit * u : battleUnits)
	{
		auto state = u->acquireState();
		SimUnit s;
		describe(s, *u, u->creatureId().toCreature());
		// what only a running battle knows
		s.id = u->unitId();
		s.side = u->unitSide();
		s.creature = u->creatureId();
		s.count = u->getCount();
		s.firstHP = u->getFirstHPleft();
		s.speed = static_cast<int>(u->getMovementRange(0));
		s.initiative = state->getInitiative(0);
		s.shooter = u->isShooter();
		s.shots = s.shooter ? state->shots.available() : 0;
		s.doubleWide = u->doubleWide();
		s.canMove = !u->hasBonusOfType(BonusType::SIEGE_WEAPON) && s.speed > 0;
		s.freeShooting = u->canShootBlocked();
		s.meleeAttacks = std::max(1, u->getTotalAttacks(false));
		s.rangedAttacks = std::max(1, u->getTotalAttacks(true));
		s.retaliationsPerRound = u->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS) ? 1000 : std::max(0, state->counterAttacks.total());
		s.retaliationsLeft = u->ableToRetaliate() ? (u->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS) ? 1000 : state->counterAttacks.available()) : 0;
		s.baseCount = std::max(s.count, static_cast<int>(u->unitBaseAmount()));
		if(!abilities)
		{
			s.lifeDrain = 0;
			s.regeneration = 0;
			s.fireShield = 0;
			s.returnsAfterStrike = false;
			s.meleeFactor = s.rangedFactor = s.turnFactor = 1.0;
		}
		s.pos = u->getPosition();
		s.active = !u->hasBonusOfType(BonusType::NOT_ACTIVE);
		s.acted = u->moved() || !s.active || (!s.canMove && !s.shooter);
		s.waited = u->waited();
		units.push_back(s);
	}

	for(int i = 0; i < static_cast<int>(units.size()); ++i)
		place(i, units[i].pos);
	if(abilities)
		applyFear();

	round = 0;
}

void SimState::describe(SimUnit & s, const ACreature & creature, const CCreature * type)
{
	const IBonusBearer & u = *creature.getBonusBearer();
	static const auto noLimit = Selector::effectRange()(BonusLimitEffect::NO_LIMIT);
	static const auto meleeOnly = noLimit.Or(Selector::effectRange()(BonusLimitEffect::ONLY_MELEE_FIGHT));
	static const auto rangedOnly = noLimit.Or(Selector::effectRange()(BonusLimitEffect::ONLY_DISTANCE_FIGHT));

	s.creature = type ? type->getId() : CreatureID();
	s.maxHP = std::max(1, static_cast<int>(creature.getMaxHealth()));
	s.fullHP = s.maxHP;
	s.firstHP = s.maxHP;
	s.value = type ? std::max(1, type->getAIValue()) : 1;
	s.speed = static_cast<int>(creature.getMovementRange());
	s.initiative = creature.getInitiative();
	s.shots = std::max(0, u.valOfBonuses(BonusType::SHOTS));
	s.shooter = s.shots > 0;
	s.flying = u.hasBonusOfType(BonusType::FLYING);
	s.doubleWide = type && type->isDoubleWide();
	s.canMove = !u.hasBonusOfType(BonusType::SIEGE_WEAPON) && s.speed > 0;
	s.noDistancePenalty = u.hasBonusOfType(BonusType::NO_DISTANCE_PENALTY);
	s.blocksRetaliation = u.hasBonusOfType(BonusType::BLOCKS_RETALIATION);
	s.attacksAllAdjacent = u.hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT);
	s.freeShooting = u.hasBonusOfType(BonusType::FREE_SHOOTING) || u.hasBonusOfType(BonusType::SIEGE_WEAPON);
	s.meleeAttacks = std::max(1, 1 + u.getBonusesOfType(BonusType::ADDITIONAL_ATTACK)->valOfBonuses(meleeOnly));
	s.rangedAttacks = std::max(1, 1 + u.getBonusesOfType(BonusType::ADDITIONAL_ATTACK)->valOfBonuses(rangedOnly));
	if(u.hasBonusOfType(BonusType::NO_RETALIATION))
		s.retaliationsPerRound = 0;
	else if(u.hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS))
		s.retaliationsPerRound = 1000;
	else
		s.retaliationsPerRound = std::max(0, 1 + u.valOfBonuses(BonusType::ADDITIONAL_RETALIATION));
	s.retaliationsLeft = s.retaliationsPerRound;

	// --- special abilities
	// The game's own rule (ACreature::isLiving): living is what is neither undead nor a construct,
	// machine, war machine or gargoyle. Life drain, death stare, ageing and poison work on the living
	// only. (Until 17.11.2026 gargoyles counted as living here: vampire lords "healed" on them.)
	s.living = u.hasBonusOfType(BonusType::LIVING);
	s.lifeDrain = u.valOfBonuses(BonusType::LIFE_DRAIN) / 100.0;
	s.regeneration = u.valOfBonuses(BonusType::HP_REGENERATION);
	s.fireShield = u.valOfBonuses(BonusType::FIRE_SHIELD) / 100.0;
	s.fireImmune = u.hasBonusOfType(BonusType::SPELL_SCHOOL_IMMUNITY, BonusSubtypeID(SpellSchool::FIRE));
	s.returnsAfterStrike = u.hasBonusOfType(BonusType::RETURN_AFTER_STRIKE);

	// attacks that reach further hexes (the game's own notation of the paths, see CBattleInfoCallback)
	s.extraCount = 0;
	s.extraFriendly = 0;
	auto addPath = [&s](int path, bool friendly)
	{
		if(path <= 0 || path > 30000 || s.extraCount >= s.extraPath.size())
			return;
		for(int i = 0; i < s.extraCount; ++i)
		{
			if(s.extraPath[i] == path)
			{
				if(friendly)
					s.extraFriendly |= static_cast<uint8_t>(1u << i);
				return;
			}
		}
		if(friendly)
			s.extraFriendly |= static_cast<uint8_t>(1u << s.extraCount);
		s.extraPath[s.extraCount++] = static_cast<int16_t>(path);
	};
	if(multiHexEnabled)
	{
	for(const auto & bonus : *u.getBonusesOfType(BonusType::MULTIHEX_UNIT_ATTACK))
		if(bonus->parameters)
			for(int path : bonus->parameters->toVector())
				addPath(path, true);
	for(const auto & bonus : *u.getBonusesOfType(BonusType::MULTIHEX_ENEMY_ATTACK))
		if(bonus->parameters)
			for(int path : bonus->parameters->toVector())
				addPath(path, false);
	if(u.hasBonusOfType(BonusType::THREE_HEADED_ATTACK))
		for(int path : {2, 6})
			addPath(path, false);
	if(u.hasBonusOfType(BonusType::WIDE_BREATH))
		for(int path : {11, 111, 2, 12, 6, 16})
			addPath(path, true);
	if(u.hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH))
		addPath(11, true);
	if(u.hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH))
		for(int path : {11, 12, 16})
			addPath(path, true);

	// shots that burst like a spell (the lich's death cloud, the magog's fireball)
	s.areaShot = false;
	s.areaLivingOnly = false;
	s.areaFire = false;
	if(const auto burst = u.getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK)))
	{
		s.areaShot = true;
		const SpellID spell = burst->subtype.as<SpellID>();
		s.areaLivingOnly = spell == SpellID::DEATH_CLOUD;
		if(const auto * info = spell.hasValue() ? spell.toSpell() : nullptr)
			s.areaFire = info->hasSchool(SpellSchool::FIRE);
	}
	}
	s.magicImmune = u.valOfBonuses(BonusType::LEVEL_SPELL_IMMUNITY) >= 5;

	// --- what its attacks do beyond their damage. Nothing here names a creature: death stare and
	// acid are bonuses of their own, everything else is a spell the unit casts after an attack, read
	// by what that spell does to its target (so creatures of mods are covered as far as their spells
	// are built from the same parts).
	s.flesh = s.living;
	s.mindImmune = !s.living || u.hasBonusOfType(BonusType::MIND_IMMUNITY);
	s.lowDamage = 1.0;
	if(type && type->getBaseDamageMin() + type->getBaseDamageMax() > 0)
		s.lowDamage = 2.0 * type->getBaseDamageMin() / (type->getBaseDamageMin() + type->getBaseDamageMax());
	s.stare = {};
	s.stun = {};
	s.stunMind = {};
	s.bolt = {};
	s.ageing = {};
	s.ageDepth = 0;
	s.sapAttack = {};
	s.sapDefence = {};
	s.curse = {};
	s.acid = 0;
	if(abilitiesBeyondDamage)
	{
		s.stare[0] = std::clamp(u.valOfBonuses(BonusType::DEATH_STARE, BonusCustomSubtype::deathStareGorgon) / 100.0, 0.0, 1.0);
		s.stare[1] = std::clamp(u.valOfBonuses(BonusType::DEATH_STARE, BonusCustomSubtype::deathStareNoRangePenalty) / 100.0, 0.0, 1.0);
		s.stare[2] = std::clamp(u.valOfBonuses(BonusType::DEATH_STARE, BonusCustomSubtype::deathStareRangePenalty) / 100.0, 0.0, 1.0);
		for(const auto & breath : *u.getBonusesOfType(BonusType::ACID_BREATH))
			s.acid += breath->val * (breath->parameters ? std::clamp(breath->parameters->toNumber() / 100.0, 0.0, 1.0) : 1.0);

		std::set<SpellID> seen;
		for(const auto & bonus : *u.getBonusesOfType(BonusType::SPELL_AFTER_ATTACK))
		{
			const SpellID id = bonus->subtype.as<SpellID>();
			const CSpell * spell = id.hasValue() ? id.toSpell() : nullptr;
			if(!spell || !seen.insert(id).second)
				continue;
			const double chance = std::clamp(u.valOfBonuses(Selector::typeSubtype(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(id))) / 100.0, 0.0, 1.0);
			int level = 0;
			int mode = 0; // 0 every attack, 1 shots only, 2 melee only
			if(bonus->parameters)
			{
				const auto & numbers = bonus->parameters->toVector();
				if(!numbers.empty())
					level = numbers[0];
				if(numbers.size() > 1)
					mode = numbers[1];
			}
			bool disables = false;
			bool lowest = false;
			bool poison = false;
			double health = 0;
			double attack = 0;
			double defence = 0;
			auto note = [&](const std::string & what, bool onAttack, bool onDefence, double val)
			{
				if(what == "NOT_ACTIVE" || what == "HYPNOTIZED")
					disables = true;
				else if(what == "ALWAYS_MINIMUM_DAMAGE")
					lowest = true;
				else if(what == "POISON")
					poison = true;
				else if(what == "STACK_HEALTH")
					health += val;
				else if(what == "PRIMARY_SKILL" && onAttack)
					attack -= val;
				else if(what == "PRIMARY_SKILL" && onDefence)
					defence -= val;
			};
			const auto & info = spell->getLevelInfo(std::clamp(level, 0, 3));
			for(const auto & effect : info.effects)
			{
				const bool skill = effect->type == BonusType::PRIMARY_SKILL;
				note(effectKind(effect->type), skill && effect->subtype.as<PrimarySkill>() == PrimarySkill::ATTACK,
					skill && effect->subtype.as<PrimarySkill>() == PrimarySkill::DEFENSE, effect->val);
			}
			// spells built the newer way list their lasting bonuses as text
			for(const auto & [name, effect] : info.battleEffects.Struct())
			{
				if(effect["type"].String() != "core:timed")
					continue;
				for(const auto & [bonusName, node] : effect["bonus"].Struct())
				{
					const std::string & subtype = node["subtype"].String();
					note(node["type"].String(), subtype.find("attack") != std::string::npos, subtype.find("defence") != std::string::npos, node["val"].Float());
				}
			}
			const double power = spell->isDamage() ? u.valOfBonuses(BonusType::SPECIFIC_SPELL_POWER, BonusSubtypeID(id)) : 0.0;
			const std::string key = spell->getJsonKey();
			const bool mind = key.find("blind") != std::string::npos || key.find("hypno") != std::string::npos;
			for(int kind = 0; kind < 2; ++kind)
			{
				if((kind == 0 && mode == 1) || (kind == 1 && mode == 2))
					continue;
				if(disables)
					(mind ? s.stunMind : s.stun)[kind] = 1.0 - (1.0 - (mind ? s.stunMind : s.stun)[kind]) * (1.0 - chance);
				if(lowest)
					s.curse[kind] = 1.0 - (1.0 - s.curse[kind]) * (1.0 - chance);
				// ageing halves the health of every creature; poison takes a tenth more every round, half at most - about a quarter over a fight
				if(health < 0)
				{
					s.ageing[kind] = 1.0 - (1.0 - s.ageing[kind]) * (1.0 - chance);
					s.ageDepth = std::max(s.ageDepth, std::min(0.5, poison ? 0.25 : -health / 100.0));
				}
				s.sapAttack[kind] += chance * std::max(0.0, attack);
				s.sapDefence[kind] += chance * std::max(0.0, defence);
				s.bolt[kind] += chance * power;
			}
		}
	}

	// expected values instead of dice: luck doubles a hit, a death blow too; morale gives or takes turns
	const auto & settings = *LIBRARY->engineSettings();
	auto chance = [&settings](EGameSettings table, EGameSettings dice, int level)
	{
		const auto & steps = settings.getVector(table);
		const double size = std::max<int64_t>(1, settings.getInteger(dice));
		if(level <= 0 || steps.empty())
			return 0.0;
		return std::clamp(steps[std::min<size_t>(level, steps.size()) - 1] / size, 0.0, 1.0);
	};
	const int luck = creature.luckVal();
	const double lucky = luck > 0 ? chance(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, EGameSettings::COMBAT_LUCK_DICE_SIZE, luck) : 0.0;
	const double unlucky = luck < 0 ? chance(EGameSettings::COMBAT_BAD_LUCK_CHANCE, EGameSettings::COMBAT_LUCK_DICE_SIZE, -luck) : 0.0;
	const double deathBlow = std::clamp(u.valOfBonuses(BonusType::DOUBLE_DAMAGE_CHANCE) / 100.0, 0.0, 1.0);
	s.meleeFactor = (1.0 + lucky - 0.5 * unlucky) * (1.0 + deathBlow);
	s.rangedFactor = 1.0 + lucky - 0.5 * unlucky;
	const int morale = creature.moraleVal();
	s.fearless = false;
	for(const auto & fear : *u.getBonusesOfType(BonusType::FEARFUL))
		s.fearless = s.fearless || fear->valType == BonusValueType::INDEPENDENT_MIN;
	s.spreadsFear = 0;
	if(type)
	{
		// what its own kind lays on the other side (azure dragons: every enemy may lose its turn)
		for(const auto & fear : type->getExportedBonusList())
		{
			if(fear && fear->type == BonusType::FEARFUL && fear->propagator && fear->val > 0)
				s.spreadsFear = std::max(s.spreadsFear, std::clamp(fear->val / 100.0, 0.0, 1.0));
		}
	}
	s.turnFactor = 1.0;
	if(morale > 0)
		s.turnFactor = 1.0 + chance(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, EGameSettings::COMBAT_MORALE_DICE_SIZE, morale);
	else if(morale < 0)
		s.turnFactor = 1.0 - chance(EGameSettings::COMBAT_BAD_MORALE_CHANCE, EGameSettings::COMBAT_MORALE_DICE_SIZE, -morale);
}

void SimState::applyFear()
{
	// Fear: every unit facing azure dragons may lose its turn, unless it is fearless. As an expected
	// value on the turns it gets. (In a battle the game holds this as a bonus on the frightened; the
	// simulator reads it from the dragons, so forecasts without a battle know it too.)
	if(!abilitiesBeyondDamage)
		return;
	std::array<double, 2> fear{};
	for(const auto & u : units)
	{
		if(u.alive() && (u.side == BattleSide::ATTACKER || u.side == BattleSide::DEFENDER))
			fear[u.side == BattleSide::ATTACKER ? 1 : 0] = std::max(fear[u.side == BattleSide::ATTACKER ? 1 : 0], u.spreadsFear);
	}
	for(auto & u : units)
	{
		const double chance = u.side == BattleSide::ATTACKER ? fear[0] : fear[1];
		if(chance > 0 && !u.fearless)
			u.turnFactor *= 1.0 - chance;
	}
}

void SimState::initForecast(const std::vector<StackSpec> & stacks, const DamageTable & table, const JsonNode & layout)
{
	damage = &table;
	units.clear();
	units.reserve(stacks.size());
	for(int h = 0; h < FIELD_SIZE; ++h)
		occupant[h] = BattleHex(h).isAvailable() ? -1 : -2;

	std::array<int, 2> stackCount = {0, 0};
	for(const auto & spec : stacks)
		stackCount[spec.side == BattleSide::ATTACKER ? 0 : 1]++;
	std::array<int, 2> placed = {0, 0};
	uint32_t nextId = 0;
	for(const auto & spec : stacks)
	{
		const int sideIndex = spec.side == BattleSide::ATTACKER ? 0 : 1;
		SimUnit s;
		describe(s, *spec.bearer, spec.type);
		s.id = nextId++;
		s.side = spec.side;
		s.count = spec.count;
		s.baseCount = spec.count;

		// the game's loose formation; a layout without formations (creature banks: we stand in the
		// middle, the guards around us) lists one hex per stack instead
		const JsonNode & rows = layout[sideIndex == 0 ? "attackerUnitsLoose" : "defenderUnitsLoose"];
		const int formation = std::clamp(stackCount[sideIndex], 1, 7) - 1;
		const int serial = std::min(placed[sideIndex]++, 6);
		const JsonNode & listed = rows.isNull() ? layout[sideIndex == 0 ? "attackerUnits" : "defenderUnits"][serial] : rows[formation][std::min(serial, formation)];
		BattleHex pos = listed.isNull() ? BattleHex() : BattleHex(static_cast<int>(listed.Integer()));
		if(!pos.isValid())
			pos = BattleHex(sideIndex == 0 ? 1 : 15, 5);
		s.pos = pos;
		s.acted = !s.canMove && !s.shooter;
		units.push_back(s);
	}
	for(int i = 0; i < static_cast<int>(units.size()); ++i)
	{
		// Like the game: a unit that cannot stand on its hex - a wide one at the edge of the field,
		// or the hex is taken - stands on the nearest hex where it can, towards the middle first.
		BattleHex pos = units[i].pos;
		if(!canStand(i, pos))
		{
			const int inwards = units[i].side == BattleSide::ATTACKER ? 1 : -1;
			for(int shift : {inwards, -inwards, 2 * inwards, -2 * inwards})
			{
				const int x = pos.getX() + shift;
				if(x < 1 || x > GameConstants::BFIELD_WIDTH - 2)
					continue;
				const BattleHex moved(x, pos.getY());
				if(canStand(i, moved))
				{
					pos = moved;
					break;
				}
			}
		}
		place(i, pos);
	}
	applyFear();
	round = 0;
	lastMoved = BattleSide::NONE;
}

int SimState::indexOf(uint32_t unitId) const
{
	for(int i = 0; i < static_cast<int>(units.size()); ++i)
		if(units[i].id == unitId)
			return i;
	return -1;
}

bool SimState::battleOver() const
{
	bool attackerAlive = false;
	bool defenderAlive = false;
	for(const auto & u : units)
	{
		if(!u.alive())
			continue;
		if(u.side == BattleSide::ATTACKER)
			attackerAlive = true;
		else
			defenderAlive = true;
	}
	return !attackerAlive || !defenderAlive;
}

double SimState::sideValue(BattleSide side) const
{
	double total = 0;
	for(const auto & u : units)
		if(u.side == side)
			total += u.remainingValue();
	return total;
}

double SimState::evaluate(BattleSide perspective) const
{
	BattleSide other = perspective == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	return sideValue(perspective) - sideValue(other);
}

double SimState::exposure(BattleSide perspective) const
{
	double ours = 0;
	double theirs = 0;
	std::array<int8_t, FIELD_SIZE> dist;
	for(int i = 0; i < static_cast<int>(units.size()); ++i)
	{
		const auto & u = units[i];
		if(!u.alive())
			continue;
		double best = 0;
		if(canShoot(i))
		{
			for(int e = 0; e < static_cast<int>(units.size()); ++e)
				if(units[e].alive() && units[e].side != u.side)
					best = std::max(best, shotValue(i, e));
		}
		else if(u.canMove)
		{
			reachable(i, dist);
			for(int e = 0; e < static_cast<int>(units.size()); ++e)
			{
				const auto & target = units[e];
				if(!target.alive() || target.side == u.side)
					continue;
				bool inReach = false;
				for(const auto & around : battle::Unit::getSurroundingHexes(target.pos, target.doubleWide, target.side))
				{
					if(around.isValid() && dist[around.toInt()] >= 0 && adjacent(i, around, e))
					{
						inReach = true;
						break;
					}
				}
				if(inReach)
					best = std::max(best, meleeTradeValue(i, e));
			}
		}
		if(u.side == perspective)
			ours += best;
		else
			theirs += best;
	}
	return ours - theirs;
}

// ------------------------------------------------------------------ turn order

int SimState::nextActiveUnit()
{
	for(int guard = 0; guard < 3; ++guard)
	{
		if(battleOver())
			return -1;

		for(bool waitPhase : {false, true})
		{
			int best = -1;
			for(int i = 0; i < static_cast<int>(units.size()); ++i)
			{
				const auto & u = units[i];
				if(!u.alive() || u.acted || u.waited != waitPhase)
					continue;
				if(best < 0)
				{
					best = i;
					continue;
				}
				const auto & b = units[best];
				if(u.initiative != b.initiative)
				{
					if(u.initiative > b.initiative)
						best = i;
				}
				else if(u.side != b.side && u.side != lastMoved)
				{
					best = i; // engine rule: on ties the side that did not move last goes first
				}
			}
			if(best >= 0)
			{
				auto & chosen = units[best];
				if(!waitPhase && chosen.regeneration > 0)
					chosen.firstHP = std::min(chosen.maxHP, chosen.firstHP + chosen.regeneration);
				return best;
			}
		}

		// everyone acted: new round
		round++;
		for(auto & u : units)
		{
			u.acted = !u.alive() || !u.active || (!u.canMove && !u.shooter);
			u.waited = false;
			u.retaliationsLeft = u.retaliationsPerRound;
		}
	}
	return -1;
}

// ------------------------------------------------------------------ geometry

std::vector<BattleHex> SimState::unitHexes(int unit, BattleHex at) const
{
	const auto & u = units[unit];
	std::vector<BattleHex> result;
	for(const auto & h : battle::Unit::getHexes(at, u.doubleWide, u.side))
		result.push_back(h);
	return result;
}

bool SimState::canStand(int unit, BattleHex at) const
{
	if(!at.isAvailable())
		return false;
	for(const auto & h : battle::Unit::getHexes(at, units[unit].doubleWide, units[unit].side))
	{
		if(!h.isAvailable())
			return false;
		int occ = occupant[h.toInt()];
		if(occ != -1 && occ != unit)
			return false;
	}
	return true;
}

void SimState::reachable(int unit, std::array<int8_t, FIELD_SIZE> & dist) const
{
	dist.fill(-1);
	const auto & u = units[unit];
	dist[u.pos.toInt()] = 0;
	if(!u.canMove)
		return;

	if(u.flying)
	{
		for(int h = 0; h < FIELD_SIZE; ++h)
		{
			BattleHex hex(h);
			int d = BattleHex::getDistance(u.pos, hex);
			if(d <= u.speed && canStand(unit, hex))
				dist[h] = static_cast<int8_t>(d);
		}
		return;
	}

	std::array<int16_t, FIELD_SIZE> queue;
	int head = 0;
	int tail = 0;
	queue[tail++] = u.pos.toInt();
	while(head < tail)
	{
		int cur = queue[head++];
		int d = dist[cur];
		if(d >= u.speed)
			continue;
		for(const auto & next : BattleHex(cur).getNeighbouringTiles())
		{
			if(!next.isValid() || dist[next.toInt()] >= 0)
				continue;
			if(!canStand(unit, next))
				continue;
			dist[next.toInt()] = static_cast<int8_t>(d + 1);
			queue[tail++] = next.toInt();
		}
	}
}

bool SimState::adjacent(int a, BattleHex aPos, int b) const
{
	const auto & ub = units[b];
	const auto & around = battle::Unit::getSurroundingHexes(ub.pos, ub.doubleWide, ub.side);
	for(const auto & h : battle::Unit::getHexes(aPos, units[a].doubleWide, units[a].side))
		if(around.contains(h))
			return true;
	return false;
}

bool SimState::isBlocked(int unit) const
{
	const auto & u = units[unit];
	for(int i = 0; i < static_cast<int>(units.size()); ++i)
	{
		const auto & e = units[i];
		if(e.alive() && e.side != u.side && adjacent(unit, u.pos, i))
			return true;
	}
	return false;
}

bool SimState::canShoot(int unit) const
{
	const auto & u = units[unit];
	return u.shooter && u.shots > 0 && (u.freeShooting || !isBlocked(unit));
}

bool SimState::shotIsFar(int attacker, int target) const
{
	return BattleHex::getDistance(units[attacker].pos, units[target].pos) > 10;
}

// ------------------------------------------------------------------ actions

void SimState::place(int unit, BattleHex at)
{
	units[unit].pos = at;
	if(!units[unit].alive())
		return;
	for(const auto & h : battle::Unit::getHexes(at, units[unit].doubleWide, units[unit].side))
		if(h.isValid())
			occupant[h.toInt()] = static_cast<int16_t>(unit);
}

void SimState::lift(int unit)
{
	for(const auto & h : battle::Unit::getHexes(units[unit].pos, units[unit].doubleWide, units[unit].side))
		if(h.isValid() && occupant[h.toInt()] == unit)
			occupant[h.toInt()] = -1;
}

int64_t SimState::dealDamage(int target, double amount)
{
	// a target that has lost defence (acid, disease) suffers more
	return removeHealth(target, amount * (1.0 + SKILL_POINT * units[target].defenceLost));
}

int64_t SimState::removeHealth(int target, double amount)
{
	auto & t = units[target];
	const int64_t before = t.totalHP();
	int64_t total = before - static_cast<int64_t>(std::llround(amount));
	if(total <= 0)
	{
		lift(target);
		t.count = 0;
		t.firstHP = 0;
		return before;
	}
	t.count = static_cast<int>((total + t.maxHP - 1) / t.maxHP);
	t.firstHP = static_cast<int>(total - static_cast<int64_t>(t.count - 1) * t.maxHP);
	return before - total;
}

void SimState::heal(int unit, double amount)
{
	auto & u = units[unit];
	if(!u.alive() || amount <= 0)
		return;
	const int64_t limit = static_cast<int64_t>(std::max(u.baseCount, u.count)) * u.maxHP;
	const int64_t total = std::min(limit, u.totalHP() + static_cast<int64_t>(std::llround(amount)));
	u.count = static_cast<int>((total + u.maxHP - 1) / u.maxHP);
	u.firstHP = static_cast<int>(total - static_cast<int64_t>(u.count - 1) * u.maxHP);
}

void SimState::strike(int attacker, int target, bool ranged, int chargeHexes, bool ownTurn)
{
	const SimUnit a = units[attacker]; // copy: fire shield may hurt the attacker while we still need its numbers
	units[target].active = true; // blindness, paralysis and petrification end when the unit is attacked
	units[target].stunned = 0;
	// what the attacker itself suffers: put out of action in part (it strikes less, also back), cursed, weakened
	const double able = 1.0 - std::min(1.0, a.stunned);
	const double sapped = std::max(0.3, 1.0 - SKILL_POINT * a.attackLost) * (1.0 - a.cursed * (1.0 - a.lowDamage));
	const double turns = (ownTurn ? a.turnFactor : 1.0) * able * sapped;
	if(ranged)
	{
		const bool far = shotIsFar(attacker, target);
		if(a.areaShot)
		{
			// the burst hits everything around the target as well - our own troops included
			std::array<int, 8> around{};
			int aroundCount = 0;
			burstVictims(attacker, target, around, aroundCount);
			for(int i = 0; i < aroundCount; ++i)
			{
				units[around[i]].active = true;
				dealDamage(around[i], damage->ranged(attacker, around[i], far) * a.count * a.rangedFactor * turns);
			}
		}
		dealDamage(target, damage->ranged(attacker, target, far) * a.count * a.rangedFactor * turns);
		afterHit(attacker, target, true, able, far);
		return;
	}

	const double factor = a.meleeFactor * turns;
	int64_t dealtToLiving = 0;
	double burn = 0;
	auto hit = [&](int victim, double amount)
	{
		const bool wasLiving = units[victim].living;
		const double shield = units[victim].fireShield;
		const int64_t dealt = dealDamage(victim, amount);
		if(wasLiving)
			dealtToLiving += dealt;
		burn += shield * dealt;
	};
	if(a.attacksAllAdjacent)
	{
		for(int i = 0; i < static_cast<int>(units.size()); ++i)
		{
			if(i != target && units[i].alive() && units[i].side != a.side && adjacent(attacker, a.pos, i))
				hit(i, damage->melee(attacker, i) * a.count * factor);
		}
	}
	if(a.extraCount > 0)
	{
		// breath and further heads: whoever stands on the hexes they reach
		std::array<int, 8> further{};
		int furtherCount = 0;
		extraVictims(attacker, target, further, furtherCount);
		for(int i = 0; i < furtherCount; ++i)
		{
			units[further[i]].active = true;
			hit(further[i], damage->melee(attacker, further[i]) * a.count * factor);
		}
	}
	hit(target, (damage->melee(attacker, target) + damage->chargeStep(attacker, target) * chargeHexes) * a.count * factor);

	if(a.lifeDrain > 0 && dealtToLiving > 0)
		heal(attacker, a.lifeDrain * dealtToLiving);
	if(burn > 0 && !a.fireImmune && units[attacker].alive())
		removeHealth(attacker, burn);
	afterHit(attacker, target, false, able);
}

void SimState::afterHit(int attacker, int target, bool ranged, double share, bool far)
{
	const SimUnit a = units[attacker];
	if(!a.alive() || !units[target].alive() || share <= 0)
		return;
	const int kind = ranged ? 1 : 0;
	// death stare: whole creatures, whatever they are worth - the reason not to walk an archangel into gorgons
	const double stare = a.stare[ranged && far ? 2 : kind];
	// (the stare of the gorgons spares what is not of flesh; an accurate shot spares nothing)
	if(stare > 0 && (ranged || units[target].flesh))
		removeHealth(target, share * stareKills(a.count, stare) * units[target].maxHP);
	if(units[target].alive() && a.bolt[kind] > 0 && !units[target].magicImmune)
		removeHealth(target, share * a.bolt[kind] * a.count);
	if(units[target].alive() && !ranged && a.acid > 0)
		removeHealth(target, share * a.acid * a.count);
	auto & t = units[target];
	if(!t.alive())
		return;
	// what the target suffers from now on
	if(!t.magicImmune)
	{
		const double stun = 1.0 - (1.0 - a.stun[kind]) * (1.0 - (t.mindImmune ? 0.0 : a.stunMind[kind]));
		t.stunned = std::min(1.0, t.stunned + share * stun);
		t.cursed = t.living ? 1.0 - (1.0 - t.cursed) * (1.0 - share * a.curse[kind]) : t.cursed;
		t.attackLost = std::min(12.0, t.attackLost + share * a.sapAttack[kind]);
		if(t.flesh && a.ageing[kind] > 0)
		{
			// aged or poisoned: the creatures stay, each has less health - the stack dies sooner, its worth is unchanged
			t.afflicted = 1.0 - (1.0 - t.afflicted) * (1.0 - share * a.ageing[kind]);
			const int left = std::max(1, static_cast<int>(std::lround(t.fullHP * (1.0 - a.ageDepth * t.afflicted))));
			if(left < t.maxHP)
			{
				t.maxHP = left;
				t.firstHP = std::min(t.firstHP, left);
			}
		}
	}
	t.defenceLost = std::min(15.0, t.defenceLost + share * a.sapDefence[kind]);
}

void SimState::extraVictims(int attacker, int target, std::array<int, 8> & out, int & count) const
{
	// The same geometry as the game's (CBattleInfoCallback::getPotentiallyAttackableHexes).
	count = 0;
	const SimUnit & a = units[attacker];
	const SimUnit & d = units[target];
	if(a.extraCount == 0 || !a.pos.isValid() || !d.pos.isValid())
		return;
	BattleHex origin = a.pos;
	// a wide attacker whose head does not touch the enemy strikes from its other hex
	if(a.doubleWide && !battle::Unit::getSurroundingHexes(d.pos, d.doubleWide, d.side).contains(origin))
		origin = battle::Unit::occupiedHex(a.pos, true, a.side);
	if(!origin.isValid())
		return;
	auto direction = BattleHex::mutualPosition(origin, d.pos);
	if(d.doubleWide)
	{
		// a wide defender is hit on its far hex whenever that one is in reach
		const BattleHex tail = battle::Unit::occupiedHex(d.pos, true, d.side);
		if(tail.isValid() && BattleHex::mutualPosition(origin, tail) != BattleHex::NONE)
			direction = BattleHex::mutualPosition(origin, tail);
	}
	if(direction == BattleHex::NONE)
		return;
	const auto & own = battle::Unit::getHexes(a.pos, a.doubleWide, a.side);
	for(int i = 0; i < a.extraCount; ++i)
	{
		std::array<BattleHex::EDir, 5> path{};
		int steps = 0;
		for(int left = a.extraPath[i]; left != 0 && steps < 5; left /= 10)
			path[steps++] = static_cast<BattleHex::EDir>((direction + left % 10 - 1) % 6);
		if(steps == 0)
			continue;
		BattleHex hex = origin;
		try
		{
			if(a.doubleWide && own.contains(hex.cloneInDirection(path[0])))
				hex.moveInDirection(direction);
			for(int k = 0; k < steps; ++k)
				hex.moveInDirection(path[k]);
		}
		catch(const std::out_of_range &)
		{
			continue; // off the field
		}
		if(!hex.isValid() || own.contains(hex))
			continue;
		const int victim = occupant[hex.toInt()];
		if(victim < 0 || victim == target || victim == attacker || !units[victim].alive())
			continue;
		const bool friendly = (a.extraFriendly >> i) & 1u;
		if(!friendly && units[victim].side == a.side)
			continue;
		bool known = false;
		for(int k = 0; k < count; ++k)
			known = known || out[k] == victim;
		if(!known && count < static_cast<int>(out.size()))
			out[count++] = victim;
	}
}

void SimState::burstVictims(int attacker, int target, std::array<int, 8> & out, int & count) const
{
	count = 0;
	const SimUnit & a = units[attacker];
	const SimUnit & d = units[target];
	if(!a.areaShot || !d.pos.isValid())
		return;
	// the burst is centred on the hex of the target the shooter aims at: its position
	for(const auto & hex : d.pos.getNeighbouringTiles())
	{
		if(!hex.isValid())
			continue;
		const int victim = occupant[hex.toInt()];
		if(victim < 0 || victim == target || victim == attacker || !units[victim].alive())
			continue;
		const SimUnit & v = units[victim];
		if(v.magicImmune || (a.areaLivingOnly && !v.living) || (a.areaFire && v.fireImmune))
			continue;
		bool known = false;
		for(int k = 0; k < count; ++k)
			known = known || out[k] == victim;
		if(!known && count < static_cast<int>(out.size()))
			out[count++] = victim;
	}
}

void SimState::finishTurn(int unit, bool waited)
{
	auto & u = units[unit];
	if(waited && !u.waited)
		u.waited = true;
	else
	{
		u.acted = true;
		u.stunned = 0; // the action it lost (in part) is this one
	}
	lastMoved = u.side;
}

void SimState::apply(int unit, const SimAction & action)
{
	auto & u = units[unit];
	switch(action.type)
	{
	case SimActionType::MOVE:
		if(action.hex.isValid() && action.hex != u.pos)
		{
			lift(unit);
			place(unit, action.hex);
		}
		finishTurn(unit, false);
		break;

	case SimActionType::ATTACK:
	{
		const BattleHex origin = u.pos;
		int chargeHexes = 0;
		if(action.hex.isValid() && action.hex != u.pos)
		{
			if(damage->chargeStep(unit, action.target) > 0)
			{
				// jousting: the bonus grows with every hex walked
				std::array<int8_t, FIELD_SIZE> dist;
				reachable(unit, dist);
				chargeHexes = std::max<int>(0, dist[action.hex.toInt()]);
			}
			lift(unit);
			place(unit, action.hex);
		}
		int target = action.target;
		for(int strikeNo = 0; strikeNo < units[unit].meleeAttacks; ++strikeNo)
		{
			if(!units[unit].alive() || !units[target].alive())
				break;
			strike(unit, target, false, strikeNo == 0 ? chargeHexes : 0, true);
			auto & t = units[target];
			if(t.alive() && units[unit].alive() && t.retaliationsLeft > 0 && !units[unit].blocksRetaliation)
			{
				strike(target, unit, false);
				t.retaliationsLeft--;
			}
		}
		if(units[unit].alive() && units[unit].returnsAfterStrike && origin.isValid() && origin != units[unit].pos)
		{
			lift(unit);
			place(unit, origin);
		}
		finishTurn(unit, false);
		break;
	}

	case SimActionType::SHOOT:
		for(int shot = 0; shot < units[unit].rangedAttacks; ++shot)
			if(units[action.target].alive())
				strike(unit, action.target, true, 0, true);
		units[unit].shots = std::max(0, units[unit].shots - 1);
		finishTurn(unit, false);
		break;

	case SimActionType::WAIT:
		finishTurn(unit, !u.waited);
		break;

	case SimActionType::DEFEND:
		finishTurn(unit, false);
		break;
	}
}

void SimState::applyOutcome(int unit, const AttackOutcome & outcome)
{
	auto & u = units[unit];
	const int64_t hpBefore = u.totalHP();
	const int64_t targetBefore = outcome.target >= 0 ? units[outcome.target].totalHP() : 0;

	if(!outcome.shooting && !u.returnsAfterStrike && outcome.from.isValid() && outcome.from != u.pos)
	{
		lift(unit);
		place(unit, outcome.from);
	}

	for(const auto & [index, count, firstHP] : outcome.affected)
	{
		if(index < 0 || index == unit)
			continue;
		auto & t = units[index];
		if(count <= 0)
		{
			lift(index);
			t.count = 0;
			t.firstHP = 0;
		}
		else
		{
			t.count = count;
			t.firstHP = firstHP;
		}
	}

	u.count = outcome.attackerCount;
	u.firstHP = outcome.attackerFirstHP;
	if(u.count <= 0)
	{
		lift(unit);
		u.count = 0;
	}
	if(!outcome.shooting && outcome.target >= 0 && u.alive())
	{
		// abilities the engine-based evaluation leaves out
		const auto & t = units[outcome.target];
		const int64_t dealt = std::max<int64_t>(0, targetBefore - t.totalHP());
		if(u.lifeDrain > 0 && t.living)
			heal(unit, u.lifeDrain * dealt);
		if(t.fireShield > 0 && !u.fireImmune)
			removeHealth(unit, t.fireShield * dealt);
	}
	if(outcome.target >= 0 && u.alive() && units[outcome.target].alive())
	{
		// what the hit does beyond its damage - and what the blow struck back does to us
		const bool struckBack = !outcome.shooting && u.totalHP() < hpBefore;
		afterHit(unit, outcome.target, outcome.shooting, 1.0, outcome.shooting && shotIsFar(unit, outcome.target));
		if(struckBack && units[unit].alive() && units[outcome.target].alive())
			afterHit(outcome.target, unit, false, 1.0);
	}

	if(outcome.shooting)
		u.shots = std::max(0, u.shots - 1);
	else if(outcome.target >= 0 && units[outcome.target].alive() && u.totalHP() < hpBefore && units[outcome.target].retaliationsLeft > 0)
		units[outcome.target].retaliationsLeft--;

	finishTurn(unit, false);
}

// ------------------------------------------------------------------ default policy

double SimState::meleeTradeValue(int attacker, int target) const
{
	const auto & a = units[attacker];
	const auto & t = units[target];
	double dealt = std::min<double>(t.totalHP(), damage->melee(attacker, target) * a.count * a.meleeAttacks);
	double gain = dealt / t.maxHP * t.value;

	double remaining = t.totalHP() - dealt;
	double loss = 0;
	if(remaining > 0 && t.retaliationsLeft > 0 && !a.blocksRetaliation)
	{
		double targetCount = std::ceil(remaining / t.maxHP);
		double ret = std::min<double>(a.totalHP(), damage->melee(target, attacker) * targetCount);
		loss = ret / a.maxHP * a.value;
	}
	return gain - loss;
}

double SimState::shotValue(int attacker, int target) const
{
	const auto & a = units[attacker];
	const auto & t = units[target];
	const bool far = shotIsFar(attacker, target);
	double dealt = std::min<double>(t.totalHP(), damage->ranged(attacker, target, far) * a.count * a.rangedAttacks);
	double value = dealt / t.maxHP * t.value;
	if(a.areaShot)
	{
		// a bursting shot: what else it hits counts too - for us when it is theirs, against us when it is ours
		std::array<int, 8> around{};
		int aroundCount = 0;
		burstVictims(attacker, target, around, aroundCount);
		for(int i = 0; i < aroundCount; ++i)
		{
			const auto & v = units[around[i]];
			const double burst = std::min<double>(v.totalHP(), damage->ranged(attacker, around[i], far) * a.count * a.rangedAttacks) / v.maxHP * v.value;
			value += v.side == a.side ? -burst : burst;
		}
	}
	return value;
}

void SimState::playDefault(int unit)
{
	const auto & u = units[unit];

	if(canShoot(unit))
	{
		int best = -1;
		double bestValue = -1;
		for(int i = 0; i < static_cast<int>(units.size()); ++i)
		{
			if(!units[i].alive() || units[i].side == u.side)
				continue;
			double v = shotValue(unit, i);
			if(v > bestValue)
			{
				bestValue = v;
				best = i;
			}
		}
		if(best >= 0)
		{
			apply(unit, {SimActionType::SHOOT, best, BattleHex()});
			return;
		}
	}

	if(!u.canMove)
	{
		apply(unit, {SimActionType::DEFEND, -1, BattleHex()});
		return;
	}

	std::array<int8_t, FIELD_SIZE> dist;
	reachable(unit, dist);

	int bestTarget = -1;
	BattleHex bestHex;
	double bestValue = -1e18;
	int bestDist = 1000;

	for(int i = 0; i < static_cast<int>(units.size()); ++i)
	{
		const auto & e = units[i];
		if(!e.alive() || e.side == u.side)
			continue;

		BattleHex attackFrom;
		int attackDist = 1000;
		for(const auto & around : battle::Unit::getSurroundingHexes(e.pos, e.doubleWide, e.side))
		{
			// candidate head positions whose occupied hexes include `around`
			std::array<BattleHex, 3> heads = {around, BattleHex(), BattleHex()};
			if(u.doubleWide)
			{
				if(around.getX() > 0)
					heads[1] = BattleHex(around.getX() - 1, around.getY());
				if(around.getX() < GameConstants::BFIELD_WIDTH - 1)
					heads[2] = BattleHex(around.getX() + 1, around.getY());
			}
			for(const auto & head : heads)
			{
				if(!head.isValid() || dist[head.toInt()] < 0)
					continue;
				if(!adjacent(unit, head, i))
					continue;
				if(dist[head.toInt()] < attackDist)
				{
					attackDist = dist[head.toInt()];
					attackFrom = head;
				}
			}
		}
		if(!attackFrom.isValid())
			continue;

		double v = meleeTradeValue(unit, i);
		if(v > bestValue || (v == bestValue && attackDist < bestDist))
		{
			bestValue = v;
			bestTarget = i;
			bestHex = attackFrom;
			bestDist = attackDist;
		}
	}

	const Stance own = stance[static_cast<int>(u.side)];
	if(bestTarget >= 0 && (own != Stance::HOLD || bestValue > 0))
	{
		apply(unit, {SimActionType::ATTACK, bestTarget, bestHex});
		return;
	}

	// nothing in reach: advance towards the closest enemy - unless this side holds its ground
	if(own == Stance::HOLD)
	{
		apply(unit, {SimActionType::DEFEND, -1, BattleHex()});
		return;
	}
	if(own == Stance::STALK && !u.waited)
	{
		apply(unit, {SimActionType::WAIT, -1, BattleHex()});
		return;
	}

	// which enemy to walk towards: the closest, or (stalking) the one that promises the best trade soon
	int prey = -1;
	if(own == Stance::STALK)
	{
		double bestPrey = -1e18;
		for(int i = 0; i < static_cast<int>(units.size()); ++i)
		{
			const auto & e = units[i];
			if(!e.alive() || e.side == u.side)
				continue;
			const int turns = (BattleHex::getDistance(u.pos, e.pos) - 1) / std::max(1, u.speed) + 1;
			const double v = meleeTradeValue(unit, i) / turns;
			if(v > bestPrey)
			{
				bestPrey = v;
				prey = i;
			}
		}
	}

	BattleHex bestMove = u.pos;
	int bestRemaining = 1000;
	for(int h = 0; h < FIELD_SIZE; ++h)
	{
		if(dist[h] < 0)
			continue;
		int closest = 1000;
		if(prey >= 0)
			closest = BattleHex::getDistance(BattleHex(h), units[prey].pos);
		else
		{
			for(const auto & e : units)
				if(e.alive() && e.side != u.side)
					closest = std::min<int>(closest, BattleHex::getDistance(BattleHex(h), e.pos));
		}
		if(closest < bestRemaining)
		{
			bestRemaining = closest;
			bestMove = BattleHex(h);
		}
	}
	if(bestMove != u.pos)
		apply(unit, {SimActionType::MOVE, -1, bestMove});
	else
		apply(unit, {SimActionType::DEFEND, -1, BattleHex()});
}

void SimState::playout(int lastRound)
{
	for(int step = 0; step < MAX_SIM_STEPS; ++step)
	{
		int unit = nextActiveUnit();
		if(unit < 0 || round > lastRound)
			return;
		playDefault(unit);
	}
}

// ------------------------------------------------------------------ forecast

int Forecast::bestPlan() const
{
	if(win(0) != win(1))
		return win(0) ? 0 : 1;
	return ownAfter[0] - enemyAfter[0] >= ownAfter[1] - enemyAfter[1] ? 0 : 1;
}

double Forecast::lossShare(int plan) const
{
	if(!win(plan) || ownBefore <= 0)
		return 1.0;
	return std::clamp(1.0 - ownAfter[plan] / ownBefore, 0.0, 1.0);
}

Forecast forecastBattle(const std::vector<StackSpec> & stacks, BattleSide ownSide, const JsonNode & layout, int maxRounds, Stance enemyStance)
{
	DamageTable table;
	table.build(stacks);
	SimState base;
	base.initForecast(stacks, table, layout);
	const BattleSide other = ownSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const int ownIndex = ownSide == BattleSide::ATTACKER ? 0 : 1;

	Forecast result;
	result.ownBefore = base.sideValue(ownSide);
	result.enemyBefore = base.sideValue(other);
	int plan = 0;
	for(Stance stance : {Stance::HOLD, Stance::CHARGE})
	{
		SimState sim = base;
		sim.stance[ownIndex] = stance;
		sim.stance[1 - ownIndex] = enemyStance;
		sim.playout(maxRounds);
		result.ownAfter[plan] = sim.sideValue(ownSide);
		result.enemyAfter[plan] = sim.sideValue(other);
		result.rounds[plan] = sim.round;
		++plan;
	}
	return result;
}

namespace
{
std::mutex urgencyMutex;
std::map<int, double> urgencyByHero;
}

void setManaUrgency(int hero, double urgency)
{
	std::lock_guard lock(urgencyMutex);
	urgencyByHero[hero] = urgency;
}

double manaUrgency(int hero)
{
	std::lock_guard lock(urgencyMutex);
	auto known = urgencyByHero.find(hero);
	return known != urgencyByHero.end() ? known->second : 1.0;
}

void setAfterHit(bool enabled)
{
	abilitiesBeyondDamage = enabled;
}

void setMultiHex(bool enabled)
{
	multiHexEnabled = enabled;
}

bool multiHex()
{
	return multiHexEnabled;
}

}
