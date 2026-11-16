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

FairBattleAI::FairBattleAI() = default;

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
}

void FairBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences prefs)
{
	preferences = prefs;
	initBattleInterface(ENV, CB);
}

void FairBattleAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide Side, bool replayAllowed)
{
	side = Side;
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
	if(spellSearchEnabled())
		return trySpellSearch(battleID, stack);

	auto battle = cb->getBattle(battleID);
	const int simulationTurns = env->game()->getStartInfo()->difficulty < 4 ? 2 : 10;
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

		if(auto retreat = considerRetreat(battleID))
		{
			cb->battleMakeUnitAction(battleID, *retreat);
			return;
		}

		if(tryHeroSpell(battleID, stack))
			return; // the same unit gets activated again after the spell

		cb->battleMakeUnitAction(battleID, searchUnitAction(battleID, stack));
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
	// the adventure AI decides: it knows what the hero is worth
	return cb->makeSurrenderRetreatDecision(battleID, bs);
}

BattleAction FairBattleAI::searchUnitAction(const BattleID & battleID, const CStack * stack)
{
	auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	auto choice = bestUnitAction(state, stack);
	logAi->debug("FairBattleAI: %s -> %s (score %.0f)", stack->getDescription(), choice.description, choice.score);
	return choice.action;
}

FairBattleAI::UnitChoice FairBattleAI::bestUnitAction(const std::shared_ptr<HypotheticBattle> & battle, const CStack * realStack, int roundsAhead)
{
	// the unit as it is in this (possibly hypothetical) state: spells change its speed and position
	const battle::Unit * stack = battle->battleGetUnitByID(realStack->unitId());
	if(!stack || !stack->alive())
		return {BattleAction::makeDefend(realStack), -1e18, "gone"};

	std::vector<const battle::Unit *> units;
	for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(u);

	DamageTable table;
	table.build(*battle, units);
	SimState base;
	base.init(*battle, units, table);

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

	if(!stack->waited())
		candidates.push_back({{SimActionType::WAIT, -1, BattleHex()}, BattleAction::makeWait(stack), "wait"});
	candidates.push_back({{SimActionType::DEFEND, -1, BattleHex()}, BattleAction::makeDefend(stack), "defend"});

	// look ahead: every candidate is played out against an attacking and a waiting opponent,
	// the worse of both counts (robust against opponents that do not walk into our traps)
	const auto & config = searchSettings();
	const int lastRound = base.round + (roundsAhead >= 0 ? roundsAhead : config.roundsAhead);
	const int enemySide = side == BattleSide::ATTACKER ? 1 : 0;
	std::vector<Stance> enemyStances = {Stance::CHARGE};
	if(config.robustStances)
		enemyStances.push_back(Stance::HOLD);
	Candidate * best = nullptr;
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
