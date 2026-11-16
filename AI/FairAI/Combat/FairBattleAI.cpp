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

// components of VCMI's BattleAI (compiled into FairAI, see CMakeLists.txt)
#include "../../BattleAI/BattleAI.h"
#include "../../BattleAI/BattleEvaluator.h"
#include "../../BattleAI/PotentialTargets.h"
#include "../../BattleAI/StackWithBonuses.h"

#include "../../../lib/CStack.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/IGameInfoCallback.h"

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

bool FairBattleAI::tryHeroSpell(const BattleID & battleID, const CStack * stack)
{
	if(!preferences.enableSpellsUsage)
		return false;

	auto battle = cb->getBattle(battleID);
	const int simulationTurns = env->game()->getStartInfo()->difficulty < 4 ? 2 : 10;
	BattleEvaluator evaluator(env, cb, stack, playerID, battleID, side, strengthRatio(battle, side), simulationTurns);
	return evaluator.canCastSpell() && evaluator.attemptCastingSpell(stack);
}

void FairBattleAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	try
	{
		if(needsFallback(stack))
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
	auto battle = cb->getBattle(battleID);

	std::vector<const battle::Unit *> units;
	for(const auto * u : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(u);

	DamageTable table;
	table.build(*battle, units);
	SimState base;
	base.init(*battle, units, table);

	const int me = base.indexOf(stack->unitId());
	if(me < 0)
		return BattleAction::makeDefend(stack);

	std::vector<Candidate> candidates;

	// legal attacks and shots, as enumerated by the engine-based targeting code
	auto hypothetic = std::make_shared<HypotheticBattle>(env.get(), battle);
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
	const int lastRound = base.round + config.roundsAhead;
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

	logAi->debug("FairBattleAI: %s -> %s (score %.0f, %d candidates)", stack->getDescription(), best->description, best->score, candidates.size());
	return best->real;
}

}
