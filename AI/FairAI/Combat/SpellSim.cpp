/*
 * SpellSim.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "SpellSim.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/IBonusBearer.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModUtility.h"
#include "../../../lib/spells/CSpellHandler.h"

namespace FAI::Combat
{

namespace
{
/// One condition a spell sets for its targets ("noneOf": {"bonus.UNDEAD": "absolute"}), as the game reads it
struct Condition
{
	enum class Kind : uint8_t
	{
		BONUS,       ///< the target has a bonus of a kind
		CREATURE,    ///< the target is a certain creature
		SPELL_EFFECT ///< the target is under a certain spell
	};
	Kind kind = Kind::BONUS;
	bool absolute = false;  ///< nothing lifts it
	bool exclusive = false; ///< must hold (allOf, noneOf); otherwise one of several (anyOf)
	bool inverted = false;  ///< noneOf
	CSelector selector;
	int32_t id = -1;
};

std::mutex conditionMutex;
std::map<int32_t, std::vector<Condition>> conditionCache;

const std::vector<Condition> & conditionsOf(const CSpell & spell)
{
	std::unique_lock lock(conditionMutex);
	if(auto found = conditionCache.find(spell.getIndex()); found != conditionCache.end())
		return found->second;

	std::vector<Condition> list;
	auto load = [&](const JsonNode & source, bool exclusive, bool inverted)
	{
		for(const auto & [key, value] : source.Struct())
		{
			// the game reads the short form only: "bonus.X": "absolute" or "normal"
			if(!value.isString() || (value.String() != "absolute" && value.String() != "normal"))
				continue;
			Condition condition;
			condition.absolute = value.String() == "absolute";
			condition.exclusive = exclusive;
			condition.inverted = inverted;

			std::string scope;
			std::string type;
			std::string identifier;
			ModUtility::parseIdentifier(key, scope, type, identifier);
			if(type == "healthValueSpecial")
				continue; // hypnotise: by the health of the target - no damage spell has it
			if(type != "bonus" && type != "creature" && type != "spell")
				continue;
			const auto id = LIBRARY->identifiers()->getIdentifier(value.getModScope(), type, identifier, true);
			if(!id)
				continue;
			if(type == "bonus")
			{
				condition.kind = Condition::Kind::BONUS;
				condition.selector = Selector::type()(static_cast<BonusType>(*id));
			}
			else if(type == "creature")
			{
				condition.kind = Condition::Kind::CREATURE;
				condition.id = *id;
			}
			else
			{
				condition.kind = Condition::Kind::SPELL_EFFECT;
				condition.selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(*id)));
			}
			list.push_back(condition);
		}
	};
	load(spell.targetCondition["anyOf"], false, false);
	load(spell.targetCondition["allOf"], true, false);
	load(spell.targetCondition["noneOf"], true, true);
	return conditionCache.emplace(spell.getIndex(), std::move(list)).first->second;
}

bool holds(const Condition & condition, const IBonusBearer & target, CreatureID creature)
{
	bool result = false;
	switch(condition.kind)
	{
	case Condition::Kind::BONUS:
	case Condition::Kind::SPELL_EFFECT:
		result = target.hasBonus(condition.selector);
		break;
	case Condition::Kind::CREATURE:
		result = creature.getNum() == condition.id;
		break;
	}
	return condition.inverted != result;
}

/// The game's way of joining conditions: every exclusive one must hold, and of the others at least
/// one if there are any. `other`: a built-in condition of the second kind and whether it holds.
bool allHold(const std::vector<Condition> & list, bool absolute, const IBonusBearer & target, CreatureID creature, bool otherExists, bool otherHolds)
{
	bool exists = otherExists;
	bool any = otherExists && otherHolds;
	for(const auto & condition : list)
	{
		if(condition.absolute != absolute)
			continue;
		const bool ok = holds(condition, target, creature);
		if(condition.exclusive)
		{
			if(!ok)
				return false;
		}
		else
		{
			exists = true;
			any = any || ok;
		}
	}
	return !exists || any;
}

bool markedAbsolute(const std::shared_ptr<const Bonus> & bonus)
{
	return bonus->parameters && bonus->parameters->toNumber() == 1;
}
}

double spellResistance(const IBonusBearer & target)
{
	return std::clamp(target.valOfBonuses(BonusType::MAGIC_RESISTANCE), 0, 100) / 100.0;
}

bool spellWorksOn(const CSpell & spell, const IBonusBearer & target, CreatureID creature)
{
	const bool positive = spell.isPositive();
	const bool magical = spell.isMagical();
	const int level = spell.getLevel();
	const auto & conditions = conditionsOf(spell);
	const BonusSubtypeID thisSpell(spell.getId());

	// --- what nothing lifts: immunity to this spell or to its level where the game marks it so, and
	// the spell's own conditions of that rank
	const auto spellImmunities = target.getBonusesOfType(BonusType::SPELL_IMMUNITY, thisSpell);
	for(const auto & bonus : *spellImmunities)
	{
		if(markedAbsolute(bonus))
			return false;
	}
	const auto levelImmunities = target.getBonusesOfType(BonusType::LEVEL_SPELL_IMMUNITY);
	if(magical && level > 0)
	{
		bool absolute = false;
		for(const auto & bonus : *levelImmunities)
			absolute = absolute || markedAbsolute(bonus);
		if(absolute && levelImmunities->totalValue() >= level)
			return false;
	}
	if(!allHold(conditions, true, target, creature, false, false))
		return false;

	// --- what lifts the rest: a unit that takes every friendly spell. (The Orb of Vulnerability does
	// too; a forecast does not know of it.)
	if(positive && target.hasBonusOfType(BonusType::RECEPTIVE))
		return true;

	// --- the ordinary immunities: to the school, to hostile magic of the school, to the level, to the spell
	bool schoolImmune = !positive && target.hasBonusOfType(BonusType::NEGATIVE_EFFECTS_IMMUNITY, BonusSubtypeID(SpellSchool::ANY));
	if(!schoolImmune)
	{
		spell.forEachSchool([&](const SpellSchool & school, bool & stop)
		{
			if(target.hasBonusOfType(BonusType::SPELL_SCHOOL_IMMUNITY, BonusSubtypeID(school))
				|| (!positive && target.hasBonusOfType(BonusType::NEGATIVE_EFFECTS_IMMUNITY, BonusSubtypeID(school))))
			{
				schoolImmune = true;
				stop = true;
			}
		});
	}
	if(schoolImmune)
		return false;
	if(magical && level > 0 && !levelImmunities->empty() && levelImmunities->totalValue() >= level)
		return false;
	if(!spellImmunities->empty())
		return false;
	// a unit that always resists (the game counts this among the "one of several" conditions)
	const bool canBeHit = positive || target.valOfBonuses(BonusType::MAGIC_RESISTANCE) < 100;
	return allHold(conditions, false, target, creature, true, canBeHit);
}

double spellDamageShare(const CSpell & spell, const IBonusBearer & target, CreatureID creature)
{
	if(!spellWorksOn(spell, target, creature))
		return 0;

	const bool magical = spell.isMagical();
	const BonusSubtypeID anySchool(SpellSchool::ANY);
	// full protection counts as immunity
	bool immune = magical && target.valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, anySchool) >= 100;
	spell.forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		immune = immune || target.valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(school)) >= 100;
	});
	if(immune)
		return 0;

	double share = 1.0;
	// protection from one of the spell's schools (the game uses the first it finds), then from all magic
	spell.forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		if(target.hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(school)))
		{
			share *= (100 - target.valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(school))) / 100.0;
			stop = true;
		}
	});
	if(magical && target.hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION, anySchool))
		share *= (100 - target.valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, anySchool)) / 100.0;
	// elementals and their opposite element
	if(target.hasBonusOfType(BonusType::MORE_DAMAGE_FROM_SPELL, BonusSubtypeID(spell.getId())))
		share *= (100 + target.valOfBonuses(BonusType::MORE_DAMAGE_FROM_SPELL, BonusSubtypeID(spell.getId()))) / 100.0;
	return std::max(0.0, share);
}

std::vector<SimSpell> describeSpells(const HeroSpec & spec, BattleSide side, const std::vector<StackSpec> & stacks, int & mana)
{
	std::vector<SimSpell> result;
	const CGHeroInstance * hero = spec.hero;
	mana = hero ? hero->mana : spec.mana;
	if(mana <= 0)
		return result;

	int lowest = 0;
	int highest = GameConstants::SPELL_LEVELS;
	if(hero)
	{
		// what keeps a hero from casting: no book, the Orb of Inhibition, a cloak or cursed ground that
		// allows low spells only
		if(!hero->hasSpellbook() || hero->hasBonusOfType(BonusType::BLOCK_ALL_MAGIC))
			return result;
		if(const auto above = hero->getBonusesOfType(BonusType::BLOCK_MAGIC_ABOVE); !above->empty())
			highest = above->totalValue();
		if(const auto below = hero->getBonusesOfType(BonusType::BLOCK_MAGIC_BELOW); !below->empty())
			lowest = below->totalValue();
	}

	// creatures that make spells cheaper for their own hero (mages) or dearer for the enemy's (pegasi)
	int cheaper = 0;
	int dearer = 0;
	for(const auto & stack : stacks)
	{
		const IBonusBearer & bearer = *stack.bearer->getBonusBearer();
		if(stack.side == side)
			cheaper = std::max(cheaper, bearer.valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ALLY));
		else
			dearer = std::max(dearer, bearer.valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ENEMY));
	}

	auto consider = [&](const CSpell & spell, int mastery, int power)
	{
		if(!spell.isCombat() || !spell.isDamage() || spell.getLevel() > highest || spell.getLevel() < lowest)
			return;
		mastery = std::clamp(mastery, 0, 3);
		const auto & info = spell.getLevelInfo(mastery);
		// the spell must do damage by the caster's power and nothing else that matters
		const JsonNode * effect = nullptr;
		for(const auto & [name, node] : info.battleEffects.Struct())
		{
			if(node["type"].String() == "core:damage" && !node["indirect"].Bool())
				effect = &node;
		}
		if(!effect || (*effect)["killByPercentage"].Bool() || (*effect)["killByCount"].Bool())
			return;

		SimSpell entry;
		entry.id = spell.getIndex();
		entry.cost = std::max(0, (hero ? hero->getSpellCost(&spell) : spell.getCost(mastery)) - cheaper + dearer);
		if(entry.cost > mana)
			return;
		entry.everyUnit = info.range.empty();
		entry.sparesOwn = info.smartTarget;
		for(int distance : info.range)
		{
			if(distance >= 0 && distance < static_cast<int>(entry.layer.size()))
				entry.layer[distance] = true;
		}
		if((*effect)["chainLength"].Integer() > 1)
		{
			entry.chain = static_cast<int>((*effect)["chainLength"].Integer());
			entry.chainFactor = (*effect)["chainFactor"].Float();
		}

		const int64_t raw = spell.calculateRawEffectValue(mastery, power, 1);
		entry.damage.reserve(stacks.size());
		bool harmsEnemy = false;
		for(const auto & stack : stacks)
		{
			const IBonusBearer & bearer = *stack.bearer->getBonusBearer();
			double damage = raw * spellDamageShare(spell, bearer, stack.type ? stack.type->getId() : CreatureID());
			if(damage > 0 && hero)
			{
				// Sorcery, the orbs, a specialty in the spell - the hero's own rule; a specialty that
				// grows weaker with the level of the target (Solmyr, Deemer) as the game counts it
				damage = static_cast<double>(hero->getSpellBonus(&spell, static_cast<int64_t>(damage), nullptr));
				const int creatureLevel = stack.type ? stack.type->getLevel() : 0;
				if(creatureLevel > 0)
					damage *= (100 + hero->valOfBonuses(BonusType::SPECIAL_SPELL_LEV, BonusSubtypeID(spell.getId())) / creatureLevel) / 100.0;
			}
			entry.damage.push_back(damage);
			harmsEnemy = harmsEnemy || (damage > 0 && stack.side != side);
		}
		if(harmsEnemy)
			result.push_back(std::move(entry));
	};

	if(hero)
	{
		for(const auto & spell : LIBRARY->spellh->objects)
		{
			if(spell && spell->isCombat() && spell->isDamage() && hero->canCastThisSpell(spell.get()))
				consider(*spell, hero->getEffectLevel(spell.get()), hero->getEffectPower(spell.get()));
		}
	}
	else
	{
		for(const auto & [id, mastery] : spec.spells)
		{
			if(const CSpell * spell = id.toSpell())
				consider(*spell, mastery, spec.power);
		}
	}
	return result;
}

}
