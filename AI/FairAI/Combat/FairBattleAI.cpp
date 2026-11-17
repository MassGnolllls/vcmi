/*
 * FairBattleAI.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "FairBattleAI.h"
#include "../../../lib/battle/BattleStateInfoForRetreat.h"
#include "CombatSim.h"
#include "../Adventure/Params.h"
#include "../Status.h"

// components of VCMI's BattleAI (compiled into FairAI, see CMakeLists.txt)
#include "../../BattleAI/BattleAI.h"
#include "../../BattleAI/BattleEvaluator.h"
#include "../../BattleAI/PotentialTargets.h"
#include "../../BattleAI/StackWithBonuses.h"
#include "../../BattleAI/SpellTargetsEvaluator.h"

#include "../../../lib/CStack.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

#include <vcmi/Environment.h>

namespace FAI::Combat
{

namespace
{
/// Tuning knobs. Defaults are the current best settings; the environment variables exist so
/// that variants can be compared in the arena without rebuilding.
int envInt(const char * name, int defaultValue)
{
	const char * value = std::getenv(name);
	return value ? std::atoi(value) : defaultValue;
}

struct SearchSettings
{
	int roundsAhead = envInt("FAI_ROUNDS", 0);                 ///< extra rounds after the current one (arena: 0 is best so far)
	double nextRoundWeight = envInt("FAI_NEXT_PCT", 0) / 100.0; ///< blend in the outcome after one more round
	bool robustStances = envInt("FAI_STANCES", 0) != 0;   ///< also play out against a waiting opponent
	bool engineOutcome = envInt("FAI_OUTCOME", 1) != 0;   ///< use engine-evaluated results for attacks
	double exposureWeight = envInt("FAI_EXPOSURE_PCT", 0) / 100.0; ///< weight of the next-round potential
	int planRounds = envInt("FAI_PLAN_ROUNDS", 8);          ///< battle plan: rounds played ahead when comparing "hold" and "advance"
	int enemyModel = envInt("FAI_ENEMY_MODEL", 0);          ///< play-outs: 0 the enemy advances at once, 1 it waits and stalks like VCMI's BattleAI, 2 the worse of both for us
	int spellRounds = envInt("FAI_SPELL_ROUNDS", 2);        ///< spell search: rounds after the current one
	int manaPricePermille = envInt("FAI_MANA_PRICE", 4);   ///< spell search: price of a scarce spell point, per mille of our army
};

const SearchSettings & searchSettings()
{
	static const SearchSettings settings;
	return settings;
}

float strengthRatio(const std::shared_ptr<CBattleInfoCallback> & battle, BattleSide side)
{
	int64_t our = 0;
	int64_t enemy = 0;
	for(const auto * stack : battle->battleGetAllStacks())
	{
		const auto * creature = stack->creatureId().toCreature();
		if(!creature)
			continue;
		(stack->unitSide() == side ? our : enemy) += static_cast<int64_t>(stack->getCount()) * creature->getAIValue();
	}
	return enemy == 0 ? 1.0f : static_cast<float>(our) / enemy;
}

struct Candidate
{
	SimAction sim;
	BattleAction real;
	std::string description;
	std::optional<AttackOutcome> outcome; ///< engine-evaluated result for attacks
	double score = 0;
};
}

struct FairBattleAI::SimCheck
{
	struct Row
	{
		uint32_t id = 0;
		int64_t before = 0;
		int64_t predicted = 0;
		bool activeBefore = true;
		std::set<SpellID> spellsBefore;
		BattleHex posPredicted;
	};
	std::vector<Row> rows;
	uint32_t actor = 0;
	int64_t target = -1;
	std::string kind;
};

namespace
{
bool simCheckEnabled()
{
	static const bool enabled = std::getenv("FAI_SIMCHECK") != nullptr;
	return enabled;
}

int64_t healthOf(const CStack * stack)
{
	return stack && stack->alive() ? stack->getAvailableHealth() : 0;
}

std::string creatureKey(const CStack * stack)
{
	const auto * creature = stack ? stack->creatureId().toCreature() : nullptr;
	return creature ? creature->getJsonKey() : std::string("?");
}

std::set<SpellID> spellsOn(const CStack * stack)
{
	std::set<SpellID> result;
	if(stack)
		for(const auto & spell : stack->activeSpells())
			result.insert(spell);
	return result;
}
}

FairBattleAI::FairBattleAI() = default;

void FairBattleAI::battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	if(!bsa.empty())
		quietDecisions = 0;
}

void FairBattleAI::actionStarted(const BattleID & battleID, const BattleAction & action)
{
	pendingCheck.reset();
	if(!simCheckEnabled() || side != BattleSide::ATTACKER)
		return;
	try
	{
		auto battle = cb->getBattle(battleID);
		if(!battle || battle->battleGetDefendedTown())
			return;

		// what changed while nobody acted: regeneration, poison, effects at the start of a turn
		for(const auto * stack : battle->battleGetAllStacks(false))
		{
			auto known = lastHealth.find(stack->unitId());
			const int64_t now = healthOf(stack);
			if(known != lastHealth.end() && known->second != now && !lastActionWasSpell)
				logAi->info("FairAI simidle|%s|%d|%lld|%lld", creatureKey(stack), stack->getCount(), static_cast<long long>(known->second), static_cast<long long>(now));
			lastHealth[stack->unitId()] = now;
		}
		lastActionWasSpell = action.actionType == EActionType::HERO_SPELL || action.actionType == EActionType::MONSTER_SPELL;

		const CStack * actor = battle->battleGetStackByID(action.stackNumber, true);
		if(!actor)
			return;
		const auto targets = action.getTarget(battle.get());
		const battle::Unit * target = nullptr;
		SimAction simAction;
		std::string kind;
		switch(action.actionType)
		{
		case EActionType::WALK:
			if(targets.empty())
				return;
			simAction = {SimActionType::MOVE, -1, targets[0].hexValue};
			kind = "move";
			break;
		case EActionType::WALK_AND_ATTACK:
			if(targets.size() < 2)
				return;
			target = battle->battleGetUnitByPos(targets[1].hexValue, true);
			simAction = {SimActionType::ATTACK, -1, targets[0].hexValue};
			kind = "attack";
			break;
		case EActionType::SHOOT:
			if(targets.empty())
				return;
			target = targets[0].unitValue ? targets[0].unitValue : battle->battleGetUnitByPos(targets[0].hexValue, true);
			simAction = {SimActionType::SHOOT, -1, BattleHex()};
			kind = "shoot";
			break;
		case EActionType::WAIT:
			simAction = {SimActionType::WAIT, -1, BattleHex()};
			kind = "wait";
			break;
		case EActionType::DEFEND:
			simAction = {SimActionType::DEFEND, -1, BattleHex()};
			kind = "defend";
			break;
		default:
			return;
		}
		if((kind == "attack" || kind == "shoot") && !target)
			return;

		std::vector<const battle::Unit *> units;
		for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
			units.push_back(u);
		DamageTable table;
		table.build(*battle, units, simAbilities);
		SimState state;
		state.init(*battle, units, table, simAbilities);
		const int me = state.indexOf(actor->unitId());
		if(me < 0)
			return;
		if(target)
		{
			simAction.target = state.indexOf(target->unitId());
			if(simAction.target < 0)
				return;
		}

		auto check = std::make_unique<SimCheck>();
		check->actor = actor->unitId();
		check->target = target ? static_cast<int64_t>(target->unitId()) : -1;
		check->kind = kind;
		for(const auto & u : state.units)
		{
			SimCheck::Row row;
			row.id = u.id;
			row.before = u.totalHP();
			const CStack * stack = battle->battleGetStackByID(u.id, false);
			row.activeBefore = stack && !stack->hasBonusOfType(BonusType::NOT_ACTIVE);
			row.spellsBefore = spellsOn(stack);
			check->rows.push_back(row);
		}
		state.apply(me, simAction);
		for(size_t i = 0; i < check->rows.size(); ++i)
		{
			check->rows[i].predicted = state.units[i].totalHP();
			check->rows[i].posPredicted = state.units[i].pos;
		}
		pendingCheck = std::move(check);
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairAI simcheck: prediction failed (%s)", e.what());
	}
}

void FairBattleAI::actionFinished(const BattleID & battleID, const BattleAction & action)
{
	if(!simCheckEnabled() || side != BattleSide::ATTACKER)
		return;
	try
	{
		auto battle = cb->getBattle(battleID);
		if(!battle)
			return;
		if(pendingCheck)
		{
			const auto & check = *pendingCheck;
			long long targetPred = 0, targetReal = 0, actorPred = 0, actorReal = 0, othersPred = 0, othersReal = 0;
			std::string targetCreature = "-", actorCreature = "?", targetSpells, actorSpells;
			int targetCount = 0, actorCount = 0, targetInactive = 0, positionOk = 1;
			auto newSpells = [](const std::set<SpellID> & before, const CStack * stack)
			{
				std::string text;
				for(const auto & spell : spellsOn(stack))
					if(!before.count(spell))
						text += (text.empty() ? "" : ",") + spell.toSpell()->getJsonKey();
				return text;
			};
			for(const auto & row : check.rows)
			{
				const CStack * stack = battle->battleGetStackByID(row.id, false);
				const long long pred = row.before - row.predicted;
				const long long real = row.before - healthOf(stack);
				if(row.id == check.actor)
				{
					actorPred = pred;
					actorReal = real;
					actorCreature = creatureKey(stack);
					actorCount = stack ? stack->getCount() : 0;
					actorSpells = newSpells(row.spellsBefore, stack);
					positionOk = stack && stack->alive() ? (stack->getPosition() == row.posPredicted ? 1 : 0) : 1;
				}
				else if(static_cast<int64_t>(row.id) == check.target)
				{
					targetPred = pred;
					targetReal = real;
					targetCreature = creatureKey(stack);
					targetCount = stack ? stack->getCount() : 0;
					targetSpells = newSpells(row.spellsBefore, stack);
					targetInactive = stack && stack->alive() && row.activeBefore && stack->hasBonusOfType(BonusType::NOT_ACTIVE) ? 1 : 0;
				}
				else
				{
					othersPred += std::abs(pred);
					othersReal += std::abs(real);
				}
			}
			const bool interesting = check.kind == "attack" || check.kind == "shoot" || !positionOk || actorPred != actorReal || othersReal != 0;
			if(interesting)
				logAi->info("FairAI simcheck|%s|%s|%d|%s|%d|%lld|%lld|%lld|%lld|%lld|%lld|%s|%s|%d|%d", check.kind, actorCreature, actorCount,
					targetCreature, targetCount, targetPred, targetReal, actorPred, actorReal, othersPred, othersReal,
					targetSpells.empty() ? "-" : targetSpells, actorSpells.empty() ? "-" : actorSpells, targetInactive, positionOk);
			pendingCheck.reset();
		}
		for(const auto * stack : battle->battleGetAllStacks(false))
			lastHealth[stack->unitId()] = healthOf(stack);
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairAI simcheck: comparison failed (%s)", e.what());
	}
}

FairBattleAI::~FairBattleAI()
{
	fallback.reset(); // restores the callback state it saved, then we restore ours
	if(cb)
		cb->waitTillRealize = wasWaitingForRealize;
}

void FairBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	playerID = *CB->getPlayerID();
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false; // actions are sent from the event thread

	fallback = std::make_shared<CBattleAI>();
	fallback->initBattleInterface(ENV, CB, preferences);

	Params params;
	params.loadOverrides(playerID.getNum());
	spellSearch = params.spellSearch != 0;
	spellMode = params.spellSearch;
	manaShare = params.manaShare;
	simAbilities = params.simAbilities != 0;
	setMultiHex(params.simMultiHex != 0);
	setAfterHit(params.simAfterHit != 0);
	battlePlan = params.battlePlan != 0;
}

void FairBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences prefs)
{
	preferences = prefs;
	initBattleInterface(ENV, CB);
}

void FairBattleAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide Side, bool replayAllowed)
{
	side = Side;
	quietDecisions = 0;
	forecastLogged = false;
	lastHealth.clear();
	if(fallback)
		fallback->battleStart(battleID, army1, army2, tile, hero1, hero2, Side, replayAllowed);
}

void FairBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
}

bool FairBattleAI::needsFallback(const CStack * stack) const
{
	return stack->creatureId() == CreatureID::CATAPULT
		|| stack->hasBonusOfType(BonusType::HEALER)
		|| stack->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE) // berserk: forced action
		|| stack->canCast()
		|| !stack->getPosition().isValid();
}

bool FairBattleAI::spellSearchEnabled() const
{
	if(const char * flag = std::getenv("FAI_SPELL_SEARCH"))
		return std::string(flag) != "0";
	return spellSearch;
}

double FairBattleAI::outcomeScore(const std::shared_ptr<HypotheticBattle> & battle) const
{
	// How the fight ends from here if nobody casts anything more: what is left of our army when we
	// win, minus what is left of theirs when we lose. Our plan is ours to choose (advance or hold),
	// the enemy's is not (the worse case counts).
	std::vector<const battle::Unit *> units;
	for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(u);
	DamageTable table;
	table.build(*battle, units, simAbilities);
	SimState base;
	base.init(*battle, units, table, simAbilities);
	const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
	const int enemySide = 1 - ownSide;
	double best = -std::numeric_limits<double>::max();
	for(Stance ownStance : {Stance::CHARGE, Stance::HOLD})
	{
		double worst = std::numeric_limits<double>::max();
		for(Stance enemyStance : {Stance::CHARGE, Stance::STALK})
		{
			SimState sim = base;
			sim.stance[ownSide] = ownStance;
			sim.stance[enemySide] = enemyStance;
			sim.playout(base.round + 40);
			worst = std::min(worst, sim.evaluate(side));
		}
		best = std::max(best, worst);
	}
	return best;
}

bool FairBattleAI::trySpellByOutcome(const BattleID & battleID, const CStack * stack)
{
	// One spell per round, and spell points are not free: a spell is cast when it changes how the
	// fight ENDS - fewer of our troops lost, or a fight won that would be lost - by more than the
	// spell points are worth. A bolt into guards we beat without a scratch changes nothing and is
	// not cast; the same bolt is cast at once when it saves a stack.
	auto battle = cb->getBattle(battleID);
	const CGHeroInstance * hero = battle->battleGetMyHero();
	if(!hero || battle->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
		return false;

	double ourValue = 0;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(unit->alive() && !unit->isTurret() && unit->unitSide() == side)
			ourValue += static_cast<double>(unit->getCount()) * unit->creatureId().toCreature()->getAIValue();
	}
	const double baseline = outcomeScore(std::make_shared<HypotheticBattle>(env.get(), battle));
	// What a spell point is worth: a full store about manaShare of the army it fights for. Nothing
	// when the fight is lost anyway - then every point that hurts the enemy is well spent.
	const double manaPrice = baseline > 0 ? manaShare * manaUrgency(hero->id.getNum()) * ourValue / std::max<double>(30.0, hero->manaLimit()) : 0.0;
	const double threshold = baseline > 0 ? 0.003 * ourValue : 0.0;

	struct Cast
	{
		const CSpell * spell;
		spells::Target target;
	};
	int tried = 0;
	std::optional<Cast> best;
	double bestGain = threshold;
	std::string bestWhat;
	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell->isCombat() || spell->isCreatureAbility() || !spell->canBeCast(battle.get(), spells::Mode::HERO, hero))
			continue;
		const double cost = battle->battleGetSpellCost(spell.get(), hero);
		spells::BattleCast probe(battle.get(), hero, spells::Mode::HERO, spell.get());
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&probe).get()))
		{
			if(++tried > 250)
				break;
			auto state = std::make_shared<HypotheticBattle>(env.get(), battle);
			spells::BattleCast apply(state.get(), hero, spells::Mode::HERO, spell.get());
			apply.castEval(state->getServerCallback(), target);
			const double gain = outcomeScore(state) - baseline - manaPrice * cost;
			if(gain > bestGain)
			{
				bestGain = gain;
				best = Cast{spell.get(), target};
				bestWhat = spell->getNameTranslated();
				if(!target.empty() && target.front().unitValue)
					bestWhat += " on " + target.front().unitValue->getDescription();
			}
		}
	}
	logAi->debug("FairBattleAI: spell by outcome for %s: %d casts tried, fight ends at %.0f without, best %s (gain %.0f beyond the price, a spell point costs %.1f, mana %d)",
		stack->getDescription(), tried, baseline, best ? bestWhat : std::string("none"), best ? bestGain : 0.0, manaPrice, hero->mana);
	if(!best)
		return false;

	BattleAction spellcast;
	spellcast.actionType = EActionType::HERO_SPELL;
	spellcast.spell = best->spell->id;
	spellcast.setTarget(best->target);
	spellcast.side = side;
	spellcast.stackNumber = -1;
	cb->battleMakeSpellAction(battleID, spellcast);
	return true;
}

bool FairBattleAI::trySpellSearch(const BattleID & battleID, const CStack * stack)
{
	auto battle = cb->getBattle(battleID);
	const CGHeroInstance * hero = battle->battleGetMyHero();
	if(!hero || battle->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
		return false;

	struct Cast
	{
		const CSpell * spell;
		spells::Target target;
	};
	std::vector<Cast> casts;
	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell->isCombat() || spell->isCreatureAbility() || !spell->canBeCast(battle.get(), spells::Mode::HERO, hero))
			continue;
		spells::BattleCast probe(battle.get(), hero, spells::Mode::HERO, spell.get());
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&probe).get()))
			casts.push_back({spell.get(), target});
	}
	if(casts.empty())
		return false;

	const int rounds = searchSettings().spellRounds;
	auto baseState = std::make_shared<HypotheticBattle>(env.get(), battle);
	const double baseline = bestUnitAction(baseState, stack, rounds).score;

	// what is at stake decides how much a spell must achieve; scarce mana is worth keeping
	double ourValue = 0;
	double enemyValue = 0;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(!unit->alive() || unit->isTurret())
			continue;
		const double value = static_cast<double>(unit->getCount()) * unit->creatureId().toCreature()->getAIValue();
		(unit->unitSide() == side ? ourValue : enemyValue) += value;
	}
	const double mana = hero->mana;
	const double scarcity = std::clamp(1.0 - mana / 60.0, 0.0, 1.0);
	const double manaPrice = searchSettings().manaPricePermille / 1000.0 * ourValue * scarcity; // per spell point
	const double threshold = 0.005 * (ourValue + enemyValue);

	int best = -1;
	double bestGain = threshold;
	std::string bestWhat;
	for(size_t i = 0; i < casts.size(); ++i)
	{
		const auto & cast = casts[i];
		auto state = std::make_shared<HypotheticBattle>(env.get(), battle);
		spells::BattleCast apply(state.get(), hero, spells::Mode::HERO, cast.spell);
		apply.castEval(state->getServerCallback(), cast.target);
		const double value = bestUnitAction(state, stack, rounds).score;
		const double cost = battle->battleGetSpellCost(cast.spell, hero);
		const double gain = value - baseline - manaPrice * cost;
		if(gain > bestGain)
		{
			bestGain = gain;
			best = static_cast<int>(i);
			bestWhat = cast.spell->getNameTranslated();
			if(!cast.target.empty() && cast.target.front().unitValue)
				bestWhat += " on " + cast.target.front().unitValue->getDescription();
		}
	}
	logAi->debug("FairBattleAI: spell search for %s: %d casts, best %s (gain %.0f, threshold %.0f, mana %.0f)", stack->getDescription(),
		static_cast<int>(casts.size()), best >= 0 ? bestWhat : std::string("none"), best >= 0 ? bestGain : 0.0, threshold, mana);
	if(best < 0)
		return false;

	BattleAction spellcast;
	spellcast.actionType = EActionType::HERO_SPELL;
	spellcast.spell = casts[best].spell->id;
	spellcast.setTarget(casts[best].target);
	spellcast.side = side;
	spellcast.stackNumber = -1;
	cb->battleMakeSpellAction(battleID, spellcast);
	return true;
}

bool FairBattleAI::tryHeroSpell(const BattleID & battleID, const CStack * stack)
{
	if(!preferences.enableSpellsUsage)
		return false;
	if(spellMode >= 2)
		return trySpellByOutcome(battleID, stack);
	if(spellSearchEnabled())
		return trySpellSearch(battleID, stack);

	auto battle = cb->getBattle(battleID);
	// always the deep setting: VCMI's BattleAI plays its spells weaker below the highest difficulty,
	// FairAI's strength must not depend on the difficulty the human picked
	const int simulationTurns = 10;
	BattleEvaluator evaluator(env, cb, stack, playerID, battleID, side, strengthRatio(battle, side), simulationTurns);
	return evaluator.canCastSpell() && evaluator.attemptCastingSpell(stack);
}

std::optional<BattleAction> FairBattleAI::siegeWalkerAction(const BattleID & battleID, const CStack * stack) const
{
	auto battle = cb->getBattle(battleID);
	if(stack->unitSide() != BattleSide::ATTACKER || stack->isShooter() || stack->hasBonusOfType(BonusType::FLYING))
		return std::nullopt;
	// a way in: broken gate or a breach in the wall (a gate a defender opened closes again)
	if(battle->battleGetGateState() == EGateState::DESTROYED)
		return std::nullopt;
	for(EWallPart part : {EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL})
		if(battle->battleGetWallState(part) == EWallState::DESTROYED)
			return std::nullopt;

	BattleHexArray moat;
	for(const auto & obstacle : battle->battleGetAllObstacles())
		if(obstacle->obstacleType == CObstacleInstance::MOAT)
			for(const auto & hex : obstacle->getAffectedTiles())
				moat.insert(hex);

	// a defender came out: hit the most valuable one we reach without standing in the moat
	ReachabilityInfo reach = battle->getReachability(stack);
	const uint32_t range = stack->getMovementRange();
	std::optional<BattleAction> best;
	int64_t bestValue = 0;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(!unit->alive() || unit->unitSide() == stack->unitSide() || unit->isTurret())
			continue;
		for(const auto & hex : unit->getSurroundingHexes())
		{
			if(!hex.isValid() || moat.contains(hex) || !reach.isReachable(hex) || reach.distances[hex.toInt()] > range)
				continue;
			const int64_t value = static_cast<int64_t>(unit->getCount()) * unit->creatureId().toCreature()->getAIValue();
			if(!best || value > bestValue)
			{
				best = BattleAction::makeMeleeAttack(stack, unit->getPosition(), hex);
				bestValue = value;
			}
			break;
		}
	}
	if(best)
		return best;
	return BattleAction::makeDefend(stack);
}

void FairBattleAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	try
	{
		// sieges: our simulator knows no walls, moat or towers - VCMI's BattleAI handles them until FairAI
		// models sieges itself, except that our walkers do not wade into the moat while there is no way in
		if(!needsFallback(stack) && cb->getBattle(battleID)->battleGetDefendedTown())
		{
			if(auto action = siegeWalkerAction(battleID, stack))
			{
				cb->battleMakeUnitAction(battleID, *action);
				return;
			}
		}
		if(needsFallback(stack) || cb->getBattle(battleID)->battleGetDefendedTown())
		{
			fallback->activeStack(battleID, stack);
			return;
		}

		if(!forecastLogged)
		{
			forecastLogged = true;
			logForecast(battleID);
		}

		if(auto retreat = considerRetreat(battleID))
		{
			cb->battleMakeUnitAction(battleID, *retreat);
			return;
		}

		if(tryHeroSpell(battleID, stack))
			return; // the same unit gets activated again after the spell

		// The attacker has to bring a battle to an end: when nobody has been hurt for about two
		// rounds (both sides waiting for the other to come), standing still is no longer an option.
		int ownUnits = 0;
		for(const auto * unit : cb->getBattle(battleID)->battleGetAllStacks(false))
			ownUnits += unit->alive() && unit->unitSide() == side ? 1 : 0;
		const bool pressure = side == BattleSide::ATTACKER && quietDecisions > 4 * ownUnits + 1; // (wait + act) x 2 rounds
		++quietDecisions;
		cb->battleMakeUnitAction(battleID, searchUnitAction(battleID, stack, pressure));
	}
	catch(const std::exception & e)
	{
		logAi->error("FairBattleAI: search failed (%s), falling back to BattleAI", e.what());
		fallback->activeStack(battleID, stack);
	}
}

std::optional<BattleAction> FairBattleAI::considerRetreat(const BattleID & battleID)
{
	auto battle = cb->getBattle(battleID);
	BattleStateInfoForRetreat bs;
	bs.canFlee = battle->battleCanFlee();
	bs.canSurrender = battle->battleCanSurrender(playerID);
	if(!bs.canFlee && !bs.canSurrender)
		return std::nullopt;
	bs.ourSide = battle->battleGetMySide();
	bs.ourHero = battle->battleGetMyHero();
	bs.enemyHero = nullptr;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(!unit->alive())
			continue;
		if(unit->unitSide() == bs.ourSide)
			bs.ourStacks.push_back(unit);
		else
		{
			bs.enemyStacks.push_back(unit);
			bs.enemyHero = battle->battleGetOwnerHero(unit);
		}
	}
	bs.turnsSkippedByDefense = 0;
	// Is the battle lost? Both sides played on by the simulator, our side once holding and once
	// advancing. Lost means: we end with nothing either way and the enemy keeps a good part.
	BattleVerdict verdict;
	try
	{
		if(!battle->battleGetDefendedTown())
		{
			std::vector<const battle::Unit *> units;
			for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
				units.push_back(u);
			DamageTable table;
			table.build(*battle, units, simAbilities);
			SimState base;
			base.init(*battle, units, table, simAbilities);
			const BattleSide other = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
			const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
			const double enemyNow = base.sideValue(other);
			verdict.ownValue = base.sideValue(side);
			verdict.lost = true;
			double enemyLeft = enemyNow;
			for(Stance stance : {Stance::HOLD, Stance::CHARGE})
			{
				SimState sim = base;
				sim.stance[ownSide] = stance;
				sim.playout(base.round + 30);
				if(sim.sideValue(side) > 0 || sim.sideValue(other) < 0.3 * enemyNow)
					verdict.lost = false;
				enemyLeft = std::min(enemyLeft, sim.sideValue(other));
			}
			verdict.enemyLoss = enemyNow - enemyLeft;
		}
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairBattleAI: no verdict on the battle (%s)", e.what());
		verdict = BattleVerdict();
	}
	setBattleVerdict(playerID, verdict);
	// the adventure AI decides: it knows what the hero, the army and our gold are worth
	return cb->makeSurrenderRetreatDecision(battleID, bs);
}

void FairBattleAI::logForecast(const BattleID & battleID)
{
	// How will this battle end? Both sides played by the simulator's default policy, our side once
	// holding and once advancing. Logged once per battle; the arena compares it with the real result
	// (guard_bench.py): a simulator that forecasts battles well can also plan them, and the adventure
	// AI needs exactly this number before it picks a fight.
	auto battle = cb->getBattle(battleID);
	std::vector<const battle::Unit *> units;
	for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(u);
	DamageTable table;
	table.build(*battle, units, simAbilities);
	SimState base;
	base.init(*battle, units, table, simAbilities);
	const BattleSide other = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
	std::string text;
	for(Stance stance : {Stance::HOLD, Stance::CHARGE})
	{
		SimState sim = base;
		sim.stance[ownSide] = stance;
		sim.stance[1 - ownSide] = searchSettings().enemyModel == 1 ? Stance::STALK : Stance::CHARGE;
		sim.playout(base.round + 30);
		text += boost::str(boost::format("|%.0f|%.0f|%d") % sim.sideValue(side) % sim.sideValue(other) % (sim.round - base.round));
	}
	// The same battle as the adventure AI would see it beforehand: armies and creature types only,
	// FairAI's own damage formula, default starting positions.
	try
	{
		std::vector<StackSpec> stacks;
		// our own stacks with everything our hero adds ...
		if(const CArmedInstance * army = battle->battleGetArmyObject(side))
		{
			for(const auto & [slot, stack] : army->Slots())
			{
				if(stack && stack->getCreature() && stack->getCount() > 0)
					stacks.push_back({stack.get(), stack->getCreature(), stack->getCount(), side});
			}
		}
		// ... the enemy as plain creatures: type and number, as seen on the field
		for(const auto * unit : units)
		{
			const CCreature * type = unit->creatureId().toCreature();
			if(unit->unitSide() != side && type && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
				stacks.push_back({type, type, unit->getCount(), unit->unitSide()});
		}
		const JsonNode & layout = LIBRARY->engineSettings()->getValue(EGameSettings::COMBAT_LAYOUTS)["default"];
		const Forecast map = forecastBattle(stacks, side, layout, 30, searchSettings().enemyModel == 1 ? Stance::STALK : Stance::CHARGE);
		text += boost::str(boost::format("|%.0f|%.0f|%.0f|%.0f|%.0f|%.0f") % map.ownBefore % map.enemyBefore % map.ownAfter[0] % map.enemyAfter[0] % map.ownAfter[1] % map.enemyAfter[1]);
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairAI forecast: map-style forecast failed (%s)", e.what());
	}
	logAi->info("FairAI forecast|%.0f|%.0f%s", base.sideValue(side), base.sideValue(other), text);

	if(simCheckEnabled())
	{
		// unit lab: FairAI's damage formula against the game's estimate, for every pair on the field
		for(const auto * attacker : units)
		{
			for(const auto * defender : units)
			{
				if(attacker->unitSide() == defender->unitSide())
					continue;
				for(bool shooting : {false, true})
				{
					if(shooting && !attacker->isShooter())
						continue;
					BattleAttackInfo info(attacker, defender, 0, shooting);
					const auto engine = battle->battleEstimateDamage(info).damage;
					double game = (engine.min + engine.max) / 2.0 / std::max(1, attacker->getCount());
					if(shooting && battle->battleHasDistancePenalty(attacker, attacker->getPosition(), defender->getPosition()))
						game *= 2;
					const double own = DamageTable::formula(*attacker, attacker->creatureId().toCreature(), *defender, defender->creatureId().toCreature(), shooting);
					if(std::abs(own - game) > 0.03 * std::max(1.0, game) + 0.6 / std::max(1, attacker->getCount()))
						logAi->info("FairAI dmgcheck|%s|%s|%s|%.2f|%.2f", creatureKey(dynamic_cast<const CStack *>(attacker)), creatureKey(dynamic_cast<const CStack *>(defender)),
							shooting ? "ranged" : "melee", game, own);
				}
			}
		}
	}
}

BattleAction FairBattleAI::searchUnitAction(const BattleID & battleID, const CStack * stack, bool forbidPassive)
{
	const auto started = std::chrono::steady_clock::now();
	auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	auto choice = bestUnitAction(state, stack, -1, forbidPassive);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	logAi->debug("FairBattleAI: %s -> %s (score %.0f, %.1f ms)%s", stack->getDescription(), choice.description, choice.score, ms, forbidPassive ? " [must act]" : "");
	return choice.action;
}

FairBattleAI::UnitChoice FairBattleAI::bestUnitAction(const std::shared_ptr<HypotheticBattle> & battle, const CStack * realStack, int roundsAhead, bool forbidPassive)
{
	// the unit as it is in this (possibly hypothetical) state: spells change its speed and position
	const battle::Unit * stack = battle->battleGetUnitByID(realStack->unitId());
	if(!stack || !stack->alive())
		return {BattleAction::makeDefend(realStack), -1e18, "gone"};

	std::vector<const battle::Unit *> units;
	for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(u);

	DamageTable table;
	table.build(*battle, units, simAbilities);
	SimState base;
	base.init(*battle, units, table, simAbilities);

	const int me = base.indexOf(stack->unitId());
	if(me < 0)
		return {BattleAction::makeDefend(stack), base.evaluate(side), "defend"};

	std::vector<Candidate> candidates;

	// legal attacks and shots, as enumerated by the engine-based targeting code
	auto hypothetic = battle;
	DamageCache damageCache;
	damageCache.buildDamageCache(hypothetic, side);
	PotentialTargets targets(stack, damageCache, hypothetic);
	for(const auto & ap : targets.possibleAttacks)
	{
		int target = base.indexOf(ap.attack.defender->unitId());
		if(target < 0)
			continue;
		AttackOutcome outcome;
		outcome.from = ap.from;
		outcome.shooting = ap.attack.shooting;
		outcome.target = target;
		outcome.attackerCount = ap.attackerState ? ap.attackerState->getCount() : stack->getCount();
		outcome.attackerFirstHP = ap.attackerState ? ap.attackerState->getFirstHPleft() : stack->getFirstHPleft();
		for(const auto & affected : ap.affectedUnits)
			outcome.affected.emplace_back(base.indexOf(affected->unitId()), affected->getCount(), affected->getFirstHPleft());

		if(ap.attack.shooting)
			candidates.push_back({{SimActionType::SHOOT, target, BattleHex()}, BattleAction::makeShotAttack(stack, ap.attack.defender), "shoot " + ap.attack.defender->getDescription(), outcome});
		else
			candidates.push_back({{SimActionType::ATTACK, target, ap.from}, BattleAction::makeMeleeAttack(stack, ap.attack.defenderPos, ap.from), "attack " + ap.attack.defender->getDescription() + " from " + std::to_string(ap.from.toInt()), outcome});
	}

	// a few movement targets
	auto reachability = battle->getReachability(stack);
	auto moveHexes = battle->battleGetAvailableHexes(reachability, stack, true);
	if(!moveHexes.empty() && base.units[me].canMove)
	{
		auto closestEnemyDistance = [&](const BattleHex & hex, bool onlyShooters, bool onlyMelee) {
			int best = 1000;
			for(const auto & e : base.units)
			{
				if(!e.alive() || e.side == base.units[me].side)
					continue;
				if(onlyShooters && !e.shooter)
					continue;
				if(onlyMelee && e.shooter)
					continue;
				best = std::min<int>(best, BattleHex::getDistance(hex, e.pos));
			}
			return best;
		};

		auto addMove = [&](const BattleHex & hex, const std::string & what) {
			if(!hex.isValid() || hex == stack->getPosition())
				return;
			for(const auto & c : candidates)
				if(c.sim.type == SimActionType::MOVE && c.sim.hex == hex)
					return;
			candidates.push_back({{SimActionType::MOVE, -1, hex}, BattleAction::makeMove(stack, hex), what + " " + std::to_string(hex.toInt())});
		};

		auto pick = [&](auto scoreOf) {
			BattleHex best;
			int bestScore = std::numeric_limits<int>::max();
			for(const auto & hex : moveHexes)
			{
				int score = scoreOf(hex);
				if(score < bestScore)
				{
					bestScore = score;
					best = hex;
				}
			}
			return best;
		};

		addMove(pick([&](const BattleHex & h) { return closestEnemyDistance(h, false, false); }), "advance");
		addMove(pick([&](const BattleHex & h) { return closestEnemyDistance(h, true, false); }), "advance to shooters");
		if(base.units[me].shooter)
			addMove(pick([&](const BattleHex & h) { return -closestEnemyDistance(h, false, true); }), "keep distance");
	}

	if(!forbidPassive || candidates.empty())
	{
		if(!stack->waited())
			candidates.push_back({{SimActionType::WAIT, -1, BattleHex()}, BattleAction::makeWait(stack), "wait"});
		candidates.push_back({{SimActionType::DEFEND, -1, BattleHex()}, BattleAction::makeDefend(stack), "defend"});
	}

	// look ahead: every candidate is played out against an attacking and a waiting opponent,
	// the worse of both counts (robust against opponents that do not walk into our traps)
	const auto & config = searchSettings();
	const int lastRound = base.round + (roundsAhead >= 0 ? roundsAhead : config.roundsAhead);
	const int enemySide = side == BattleSide::ATTACKER ? 1 : 0;
	std::vector<Stance> enemyStances = {Stance::CHARGE};
	if(config.enemyModel == 1)
		enemyStances = {Stance::STALK};
	else if(config.enemyModel == 2)
		enemyStances = {Stance::CHARGE, Stance::STALK};
	if(config.robustStances)
		enemyStances.push_back(Stance::HOLD);
	Candidate * best = nullptr;
	if(battlePlan && roundsAhead < 0)
	{
		// Battle plan: every candidate is played on for several rounds, once with our troops advancing
		// and once with them holding their ground (shooting, hitting only what comes into reach). The
		// better of the two counts - the plan is ours to choose. This is what tells "let them come
		// while our shooters work" from "close in before their shooters wear us down".
		const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
		const int planEnd = base.round + std::max(1, config.planRounds);
		for(auto & c : candidates)
		{
			double bestPlan = -std::numeric_limits<double>::max();
			for(Stance ownStance : {Stance::HOLD, Stance::CHARGE})
			{
				double worst = std::numeric_limits<double>::max();
				for(Stance enemyStance : enemyStances)
				{
					SimState sim = base;
					sim.stance[ownSide] = ownStance;
					sim.stance[enemySide] = enemyStance;
					if(c.outcome && config.engineOutcome)
						sim.applyOutcome(me, *c.outcome);
					else
						sim.apply(me, c.sim);
					sim.playout(planEnd);
					worst = std::min(worst, sim.evaluate(side));
				}
				bestPlan = std::max(bestPlan, worst);
			}
			c.score = bestPlan;
			if(!best || c.score > best->score + 1e-6)
				best = &c;
		}
		return {best->real, best->score, best->description};
	}
	for(auto & c : candidates)
	{
		double worst = std::numeric_limits<double>::max();
		for(Stance enemyStance : enemyStances)
		{
			SimState sim = base;
			sim.stance[enemySide] = enemyStance;
			if(c.outcome && config.engineOutcome)
				sim.applyOutcome(me, *c.outcome);
			else
				sim.apply(me, c.sim);
			sim.playout(lastRound);
			double value = sim.evaluate(side);
			if(config.exposureWeight > 0 && !sim.battleOver())
				value += config.exposureWeight * sim.exposure(side);
			if(config.nextRoundWeight > 0 && !sim.battleOver())
			{
				sim.playout(lastRound + 1);
				value += config.nextRoundWeight * (sim.evaluate(side) - value);
			}
			worst = std::min(worst, value);
		}
		c.score = worst;
		if(!best || c.score > best->score + 1e-6)
			best = &c;
	}

	return {best->real, best->score, best->description};
}

}
