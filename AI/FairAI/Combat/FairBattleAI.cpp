/*
 * FairBattleAI.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "FairBattleAI.h"
#include "Puppet.h"
#include "../../../lib/battle/BattleStateInfoForRetreat.h"
#include "CombatSim.h"
#include "GuardModel.h"
#include "SpellSim.h"
#include "../Adventure/Params.h"
#include "../Status.h"

// components of VCMI's BattleAI (compiled into FairAI, see CMakeLists.txt)
#include "../../BattleAI/BattleAI.h"
#include "../../BattleAI/BattleEvaluator.h"
#include "../../BattleAI/BattleExchangeVariant.h"
#include "../../../lib/battle/BattleUnitTurnReason.h"
#include "../../../lib/CConfigHandler.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CPlayerState.h"
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
#include "../../../lib/networkPacks/PacksForClientBattle.h"
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
	int oracleRounds = envInt("FAI_ORACLE_ROUNDS", 0);      ///< against guards: this many rounds (the current one counts) the enemy is played by the oracle - BattleAI itself on a mirror of the state; 0 = the player's parameter guardOracle decides
	int oracleUntilRound = envInt("FAI_ORACLE_UNTIL", 3);   ///< ... in the first rounds of a battle only (the opening decides who gets hit first)
	int ambush = envInt("FAI_AMBUSH", 0);                  ///< battle plan: a third plan for our side besides holding and advancing - lying in wait (Stance::AMBUSH)
	int corner = envInt("FAI_CORNER", 0);                  ///< move candidate: into the nearer corner of our side
	int guardValues = envInt("FAI_GUARD_VALUES", 1);       ///< the stalking enemy of the play-outs chooses its targets by BattleAI's own measure (damage it is spared), not by army value
	int guardModel = envInt("FAI_GUARD_MODEL", -1);        ///< battle plan against guards: the guards are played by GuardModel (1) or by a stance (0); -1 = the player's parameter guardModel decides
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
bool policyCheckEnabled()
{
	static const bool enabled = std::getenv("FAI_POLICYCHECK") != nullptr;
	return enabled;
}

/// 2: where the fast model of the guards and the oracle disagree, both views are logged side by side
int policyCheckLevel()
{
	static const int level = std::getenv("FAI_POLICYCHECK") ? std::atoi(std::getenv("FAI_POLICYCHECK")) : 0;
	return level;
}

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

void FairBattleAI::battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc)
{
	// a hero's spell points run out: a battle in which heroes still cast is not standing still
	if(!sc || !sc->castByHero || seenRound < 0 || !cb)
		return;
	if(auto battle = cb->getBattle(battleID))
	{
		lastChangeRound = std::max(lastChangeRound, battle->battleGetRound());
		lastGainRound = std::max(lastGainRound, battle->battleGetRound());
	}
}

void FairBattleAI::watchDeadlock(const BattleID & battleID)
{
	auto battle = cb->getBattle(battleID);
	const int round = battle->battleGetRound();
	if(round == seenRound)
		return;
	const bool first = seenRound < 0;
	seenRound = round;
	int64_t own = 0;
	int64_t enemy = 0;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(unit->alive())
			(unit->unitSide() == side ? own : enemy) += unit->getAvailableHealth();
	}
	if(battle->battleGetDefendedTown())
	{
		// (a siege moves while walls come down, even when nobody is hurt)
		int64_t walls = 0;
		for(int part = static_cast<int>(EWallPart::KEEP); part < static_cast<int>(EWallPart::PARTS_COUNT); ++part)
			walls += std::max(0, static_cast<int>(battle->battleGetWallState(static_cast<EWallPart>(part))));
		(side == BattleSide::DEFENDER ? own : enemy) += walls;
	}
	if(first || own < leastOwnHealth)
	{
		leastOwnHealth = own;
		lastChangeRound = round;
	}
	if(first || enemy < leastEnemyHealth)
	{
		leastEnemyHealth = enemy;
		lastChangeRound = round;
		lastGainRound = round;
	}
	// (test) FAI_TEST_DEADLOCK=<round>: from that round on the battle counts as standing still for good -
	// a real one cannot be staged (luck and morale end them), and the way out has to be seen working
	static const int staged = envInt("FAI_TEST_DEADLOCK", 0);
	if(staged > 0 && round >= staged)
	{
		deadlockStaged = true;
		return;
	}
	if(deadlockSince >= 0 || round - lastChangeRound < DEADLOCK_ROUNDS)
		return;
	// (a human who attacks us and then waits is not to be rewarded with our troops leaving their place)
	const PlayerColor enemyOwner = battle->sideToPlayer(side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER);
	if(enemyOwner.isValidPlayer())
	{
		const auto & players = env->game()->getStartInfo()->playerInfos;
		const auto owner = players.find(enemyOwner);
		if(owner == players.end() || owner->second.isControlledByHuman())
			return;
	}
	deadlockSince = round;
	logAi->info("FairAI %s battle stands still: round %d, nobody weaker than before since round %d (we %lld, they %lld) - our units must strike now",
		playerID.toString(), round, lastChangeRound, static_cast<long long>(own), static_cast<long long>(enemy));
}

bool FairBattleAI::deadlockLost(const BattleID & battleID) const
{
	return deadlockStaged || (deadlockSince >= 0 && seenRound - deadlockSince >= DEADLOCK_PATIENCE && seenRound - lastGainRound >= DEADLOCK_PATIENCE);
}

bool FairBattleAI::oracleAction(const std::shared_ptr<CBattleInfoCallback> & real, const SimState & sim, int unit, int baseRound, SimAction & out, std::string * trace)
{
	const auto & actor = sim.units[unit];
	using Clock = std::chrono::steady_clock;
	auto lap = [last = Clock::now()](double & into) mutable
	{
		const auto now = Clock::now();
		into += std::chrono::duration<double, std::milli>(now - last).count();
		last = now;
	};
	++oracleProfile[0];
	auto hb = std::make_shared<HypotheticBattle>(env.get(), real);
	// the unit whose turn it is (this resets what the engine resets when a unit gets its turn;
	// the simulator's own flags are written over it below)
	hb->nextTurn(actor.id, BattleUnitTurnReason::TURN_QUEUE);
	for(const auto & u : sim.units)
	{
		auto state = hb->getForUpdate(u.id);
		if(!state)
			return false;
		if(u.pos.isValid() && state->getPosition() != u.pos)
			state->setPosition(u.pos);
		const int64_t health = state->getAvailableHealth();
		if(health > u.totalHP())
		{
			int64_t lost = health - u.totalHP();
			state->damage(lost);
		}
		else if(health < u.totalHP())
		{
			int64_t gained = u.totalHP() - health;
			state->heal(gained, EHealLevel::OVERHEAL, EHealPower::PERMANENT);
		}
		state->waiting = u.waited && !u.acted;
		state->waitedThisTurn = u.waited;
		state->movedThisRound = u.acted;
		if(sim.round != baseRound || !u.acted)
			state->defending = false;
		state->counterAttacks.reset();
		const int have = state->counterAttacks.available();
		if(have > u.retaliationsLeft && u.retaliationsLeft >= 0)
			state->counterAttacks.use(have - u.retaliationsLeft);
	}
	const battle::Unit * active = hb->battleGetUnitByID(actor.id);
	if(!active || !active->alive())
		return false;
	const BattleSide own = actor.side;
	lap(oracleProfile[1]);

	DamageCache damageCache;
	damageCache.buildDamageCache(hb, own);
	lap(oracleProfile[2]);
	PotentialTargets targets(active, damageCache, hb);
	lap(oracleProfile[3]);
	const int turns = env->game()->getStartInfo()->difficulty < 4 ? 2 : 10; // as BattleAI sets itself up
	BattleExchangeEvaluator scoreEvaluator(hb, env, strengthRatio(hb, own), turns);

	auto answer = [&](SimActionType type, int target = -1, BattleHex hex = BattleHex())
	{
		out = SimAction{type, target, hex};
		return true;
	};

	auto moveTarget = scoreEvaluator.findMoveTowardsUnreachable(active, targets, damageCache, hb);
	lap(oracleProfile[4]);
	if(trace)
	{
		// what BattleAI sees, in the format of GuardModel's own trace
		auto hbWaited = std::make_shared<HypotheticBattle>(env.get(), hb);
		hbWaited->makeWait(active);
		for(int variant = 0; variant < 2; ++variant)
		{
			std::vector<battle::Units> turnOrder;
			(variant ? hbWaited : hb)->battleGetTurnOrder(turnOrder, std::numeric_limits<int>::max(), 2);
			for(size_t t = 0; t < turnOrder.size(); ++t)
			{
				*trace += boost::str(boost::format("\n  order%s%d:") % (variant ? "W" : "") % t);
				for(const auto * u : turnOrder[t])
					*trace += boost::str(boost::format(" %d") % sim.indexOf(u->unitId()));
			}
		}
		*trace += "\n  unreachable:";
		for(const auto * u : targets.unreachableEnemies)
			*trace += boost::str(boost::format(" %d") % sim.indexOf(u->unitId()));
		std::vector<float> waitedScores;
		if(!active->waited())
		{
			scoreEvaluator.updateReachabilityMap(hbWaited);
			for(const auto & ap : targets.possibleAttacks)
				waitedScores.push_back(scoreEvaluator.evaluateExchange(ap, 0, targets, damageCache, hbWaited));
		}
		scoreEvaluator.updateReachabilityMap(hb);
		size_t index = 0;
		for(const auto & ap : targets.possibleAttacks)
		{
			const float now = scoreEvaluator.evaluateExchange(ap, 0, targets, damageCache, hb);
			*trace += boost::str(boost::format("\n  ap|%d|%d|%d|%.1f|%.1f|%d|%.1f|%.1f|") % sim.indexOf(ap.attack.defender->unitId()) % (ap.attack.shooting ? -1 : ap.from.toInt()) % (ap.attack.shooting ? 1 : 0)
				% ap.defenderDamageReduce % ap.attackerDamageReduce % ap.shootersBlockedDmg % now % (index < waitedScores.size() ? waitedScores[index] : 0.0f));
			std::set<int> members;
			for(const auto & [turn, units] : scoreEvaluator.getExchangeUnits(ap, 0, targets, hb).units)
				for(const auto * u : units)
					members.insert(sim.indexOf(u->unitId()));
			for(int member : members)
				*trace += boost::str(boost::format("%d,") % member);
			*trace += boost::str(boost::format("|%.1f") % ap.collateralDamageReduce);
			++index;
		}
		*trace += boost::str(boost::format("\n  move|%.1f|%d|%d") % moveTarget.score % (moveTarget.positions.empty() ? -1 : moveTarget.positions.front().toInt()) % static_cast<int>(moveTarget.turnsToReach));
	}
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	if(!targets.possibleAttacks.empty())
	{
		auto result = scoreEvaluator.findBestTarget(active, targets, damageCache, hb, false);
		lap(oracleProfile[5]);
		const auto & bestAttack = result.bestAttack;
		if(result.score > score)
		{
			score = result.score;
			if(moveTarget.score <= score)
			{
				if(result.wait)
					return answer(SimActionType::WAIT);
				const int target = sim.indexOf(bestAttack.attack.defender->unitId());
				if(target < 0)
					return false;
				if(bestAttack.attack.shooting)
					return answer(SimActionType::SHOOT, target);
				if(bestAttack.collateralDamageReduce && bestAttack.collateralDamageReduce >= bestAttack.defenderDamageReduce / 2 && score < 0)
					return answer(SimActionType::DEFEND);
				return answer(SimActionType::ATTACK, target, bestAttack.from);
			}
		}
	}
	if(moveTarget.score > score)
	{
		if(!active->waited())
			return answer(SimActionType::WAIT);
		// BattleEvaluator::goTowardsNearest, on the hypothetical state
		auto reachability = hb->getReachability(active);
		auto available = hb->battleGetAvailableHexes(reachability, active, false);
		if(available.empty() || moveTarget.positions.empty())
			return answer(SimActionType::DEFEND);
		BattleHexArray wanted = moveTarget.positions;
		wanted.sort([&reachability](const BattleHex & a, const BattleHex & b) { return reachability.distances[a.toInt()] < reachability.distances[b.toInt()]; });
		const BattleHex nearest = wanted.front();
		if(reachability.distances[nearest.toInt()] > GameConstants::BFIELD_SIZE)
			return answer(SimActionType::DEFEND);
		auto moveOrAttack = [&](const BattleHex & hex)
		{
			// an attack from that very hex is taken on the way
			float best = 0;
			const AttackPossibility * onTheWay = nullptr;
			for(const auto & ap : targets.possibleAttacks)
			{
				if(!ap.attack.shooting && ap.from == hex && ap.attackValue() > best)
				{
					best = ap.attackValue();
					onTheWay = &ap;
				}
			}
			if(onTheWay)
			{
				const int target = sim.indexOf(onTheWay->attack.defender->unitId());
				if(target >= 0)
					return answer(SimActionType::ATTACK, target, hex);
			}
			return hex == active->getPosition() ? answer(SimActionType::DEFEND) : answer(SimActionType::MOVE, -1, hex);
		};
		for(const auto & hex : wanted)
		{
			if(available.contains(hex))
				return moveOrAttack(hex);
			if(active->coversPos(hex))
				return answer(SimActionType::DEFEND);
		}
		scoreEvaluator.updateReachabilityMap(hb);
		if(active->hasBonusOfType(BonusType::FLYING))
		{
			BattleHex bestHex;
			int bestDistance = std::numeric_limits<int>::max();
			for(const auto & hex : available)
			{
				int distance = BattleHex::getDistance(nearest, hex);
				if(scoreEvaluator.checkPositionBlocksOurStacks(*hb, active, hex))
					distance += 100;
				if(distance < bestDistance)
				{
					bestDistance = distance;
					bestHex = hex;
				}
			}
			return bestHex.isValid() ? moveOrAttack(bestHex) : answer(SimActionType::DEFEND);
		}
		BattleHex step = nearest;
		for(int guard = 0; guard < GameConstants::BFIELD_SIZE; ++guard)
		{
			if(!step.isValid())
				return answer(SimActionType::DEFEND);
			if(available.contains(step) && !scoreEvaluator.checkPositionBlocksOurStacks(*hb, active, step))
				return moveOrAttack(step);
			step = reachability.predecessors[step.toInt()];
		}
		return answer(SimActionType::DEFEND);
	}
	return answer(active->waited() ? SimActionType::DEFEND : SimActionType::WAIT);
}

void FairBattleAI::policyCheck(const BattleID & battleID, const BattleAction & action)
{
	try
	{
		auto battle = cb->getBattle(battleID);
		if(!battle || battle->battleGetDefendedTown())
			return;
		const CStack * actor = battle->battleGetStackByID(action.stackNumber, true);
		if(!actor || actor->unitSide() == side)
			return;
		const auto targets = action.getTarget(battle.get());
		const battle::Unit * target = nullptr;
		BattleHex hex;
		const char * kind = nullptr;
		switch(action.actionType)
		{
		case EActionType::WALK:
			if(targets.empty())
				return;
			hex = targets[0].hexValue;
			kind = "move";
			break;
		case EActionType::WALK_AND_ATTACK:
			if(targets.size() < 2)
				return;
			target = battle->battleGetUnitByPos(targets[1].hexValue, true);
			hex = targets[0].hexValue;
			kind = "attack";
			break;
		case EActionType::SHOOT:
			if(targets.empty())
				return;
			target = targets[0].unitValue ? targets[0].unitValue : battle->battleGetUnitByPos(targets[0].hexValue, true);
			kind = "shoot";
			break;
		case EActionType::WAIT:
			kind = "wait";
			break;
		case EActionType::DEFEND:
			kind = "defend";
			break;
		default:
			return;
		}

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
		const int targetIndex = target ? state.indexOf(target->unitId()) : -1;
		const int enemySide = side == BattleSide::ATTACKER ? 1 : 0;

		auto name = [](SimActionType type)
		{
			switch(type)
			{
			case SimActionType::MOVE: return "move";
			case SimActionType::ATTACK: return "attack";
			case SimActionType::SHOOT: return "shoot";
			case SimActionType::WAIT: return "wait";
			default: return "defend";
			}
		};
		std::string line = boost::str(boost::format("FairAI policycheck|%s|%d|%d|%d|%s|%s|%d") % creatureKey(actor) % actor->getCount() % state.round
			% (actor->waited() ? 1 : 0) % kind % (target ? creatureKey(dynamic_cast<const CStack *>(target)) : std::string("-")) % (hex.isValid() ? hex.toInt() : -1));
		auto add = [&](const SimAction & chosen)
		{
			const bool sameTarget = (chosen.type == SimActionType::ATTACK || chosen.type == SimActionType::SHOOT) && chosen.target == targetIndex && targetIndex >= 0;
			const int apart = chosen.hex.isValid() && hex.isValid() ? static_cast<int>(BattleHex::getDistance(chosen.hex, hex)) : -1;
			line += boost::str(boost::format("|%s|%d|%d") % name(chosen.type) % (sameTarget ? 1 : 0) % apart);
		};
		for(Stance stance : {Stance::CHARGE, Stance::STALK})
		{
			SimState sim = state;
			sim.stance[enemySide] = stance;
			// the guards choose their targets by BattleAI's own measure
			sim.guardValues[enemySide] = stance == Stance::STALK && searchSettings().guardValues != 0;
			sim.playDefault(me);
			add(sim.lastAction);
		}
		// ... and the oracle: BattleAI's own evaluation on a mirror of this state
		SimAction told;
		const auto started = std::chrono::steady_clock::now();
		const bool haveOracle = oracleAction(battle, state, me, state.round, told);
		if(haveOracle)
			add(told);
		else
			line += "|none|0|-1";
		line += boost::str(boost::format("|%.2f") % std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
		// the shortcut of the play-outs: a unit with nothing to attack that has not waited yet waits
		line += !actor->waited() && state.units[me].active && !state.hasAttack(me) ? "|1" : "|0";
		// ... and the fast copy of BattleAI's rules: the same answer as the oracle, in microseconds?
		SimAction native;
		const auto nativeStarted = std::chrono::steady_clock::now();
		const bool haveNative = guardAction(state, me, env->game()->getStartInfo()->difficulty < 4 ? 2 : 10, native);
		const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - nativeStarted).count();
		if(haveNative)
			add(native);
		else
			line += "|none|0|-1";
		auto same = [](const SimAction & a, const SimAction & b)
		{
			if(a.type != b.type)
				return false;
			if((a.type == SimActionType::ATTACK || a.type == SimActionType::SHOOT) && a.target != b.target)
				return false;
			return (a.type != SimActionType::ATTACK && a.type != SimActionType::MOVE) || a.hex == b.hex;
		};
		line += boost::str(boost::format("|%.1f|%d|%d|%d|%d|%d") % micros % (haveOracle && haveNative && same(told, native) ? 1 : 0)
			% told.target % (told.hex.isValid() ? told.hex.toInt() : -1) % native.target % (native.hex.isValid() ? native.hex.toInt() : -1));
		if(policyCheckLevel() >= 2 && haveOracle && haveNative && !same(told, native))
		{
			// both views side by side, to find where the copy differs
			std::string oracleView;
			std::string nativeView;
			SimAction again;
			oracleAction(battle, state, me, state.round, again, &oracleView);
			guardAction(state, me, env->game()->getStartInfo()->difficulty < 4 ? 2 : 10, again, &nativeView);
			std::string unitsView;
			for(size_t i = 0; i < state.units.size(); ++i)
			{
				const auto & u = state.units[i];
				unitsView += boost::str(boost::format("\n  unit %d: %s x%d side %d pos %d speed %d init %d hp %d/%d shots %d ret %d acted %d waited %d%s") % i % creatureKey(battle->battleGetStackByID(u.id, false)) % u.count
					% static_cast<int>(u.side) % u.pos.toInt() % u.speed % u.initiative % u.firstHP % u.maxHP % u.shots % u.retaliationsLeft % (u.acted ? 1 : 0) % (u.waited ? 1 : 0) % (static_cast<int>(i) == me ? " <- active" : ""));
			}
			logAi->info("FairAI guardmodel differs for unit %d%s\n oracle:%s\n native:%s", me, unitsView, oracleView, nativeView);
		}
		logAi->info(line);
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairAI policycheck: failed (%s)", e.what());
	}
}

void FairBattleAI::actionStarted(const BattleID & battleID, const BattleAction & action)
{
	// test bench: every action of the battle is told to whoever plays it from outside (Puppet.h)
	if(Puppet::listening())
	{
		if(auto battle = cb->getBattle(battleID))
			Puppet::report(*battle, action, actionsSeen);
	}
	++actionsSeen;
	pendingCheck.reset();
	if(policyCheckEnabled())
		policyCheck(battleID, action);
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
	spellForecast = params.spellForecast != 0;
	simAbilities = params.simAbilities != 0;
	setMultiHex(params.simMultiHex != 0);
	setAfterHit(params.simAfterHit != 0);
	setWholeCreatures(params.wholeCreatures != 0);
	battlePlan = params.battlePlan != 0;
	areaAim = params.areaAim != 0;
	useTactics = params.useTactics != 0;
	guardOracle = params.guardOracle;
	ambushPlan = params.battlePlan >= 2;
	guardPlan = params.battlePlan >= 3;
	cornerMove = params.battlePlan >= 4;
	guardOracleUntil = params.guardOracleUntil;
	guardModel = params.guardModel;
	ownCasters = params.ownCasters != 0;
	planVsPlayers = params.planVsPlayers != 0;
	modelSearch = params.modelSearch != 0;
	kitePlan = params.kitePlan;
	freeKite = params.freeKite;
	latePlan = params.latePlan;
}

void FairBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences prefs)
{
	preferences = prefs;
	initBattleInterface(ENV, CB);
}

void FairBattleAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide Side, bool replayAllowed)
{
	side = Side;
	actionsSeen = 0;
	Puppet::battleBegins();
	quietDecisions = 0;
	seenRound = -1;
	deadlockStaged = false;
	lastChangeRound = 0;
	lastGainRound = 0;
	deadlockSince = -1;
	setBattleVerdict(playerID, BattleVerdict()); // (BattleAI asks the adventure AI too: never with the verdict of an earlier battle)
	forecastLogged = false;
	spellCheckDone = false;
	lastHealth.clear();
	if(fallback)
		fallback->battleStart(battleID, army1, army2, tile, hero1, hero2, Side, replayAllowed);
}

void FairBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	auto battle = cb->getBattle(battleID);
	if(useTactics && battle)
	{
		// (useTactics) Owner's word of 27.11.2026: the shooters go to the two corners on our side, the
		// single creatures - then the weakest stacks - take the hexes beside them as their shell. The
		// hexes are chosen from the start of the phase at once: none is given twice, none taken now.
		const BattleSide side = battle->battleGetMySide();
		std::vector<const CStack *> shooters;
		std::vector<const CStack *> shields;
		std::set<BattleHex> taken;
		for(const CStack * stack : battle->battleGetAllStacks())
		{
			if(!stack || !stack->alive() || stack->unitSide() != side || stack->isTurret() || stack->hasBonusOfType(BonusType::SIEGE_WEAPON))
				continue;
			for(const BattleHex & h : stack->getHexes())
				taken.insert(h);
			if(stack->isShooter())
				shooters.push_back(stack);
			else
				shields.push_back(stack);
		}
		for(const CStack * stack : battle->battleGetAllStacks())
			if(stack && stack->alive())
				for(const BattleHex & h : stack->getHexes())
					taken.insert(h);
		auto worth = [](const CStack * s) { return static_cast<double>(s->unitType()->getAIValue()) * s->getCount(); };
		std::sort(shooters.begin(), shooters.end(), [&](auto * a, auto * b) { return worth(a) > worth(b); });
		std::sort(shields.begin(), shields.end(), [&](auto * a, auto * b)
		{
			if((a->getCount() == 1) != (b->getCount() == 1))
				return a->getCount() == 1; // singles first
			return worth(a) < worth(b);
		});
		const int backX = side == BattleSide::ATTACKER ? 1 : GameConstants::BFIELD_WIDTH - 2;
		const std::array<int, 2> rows = {0, GameConstants::BFIELD_HEIGHT - 1};
		std::vector<BattleAction> moves;
		std::set<const CStack *> moved;
		for(size_t k = 0; k < shooters.size() && k < 2; ++k)
		{
			const CStack * shooter = shooters[k];
			const BattleHex corner(backX, rows[k]);
			BattleHex best;
			int bestDist = 1000;
			for(const BattleHex & h : battle->battleGetAvailableHexes(shooter, false))
			{
				if(taken.count(h) && !vstd::contains(shooter->getHexes(), h))
					continue;
				const int d = BattleHex::getDistance(h, corner);
				if(d < bestDist)
				{
					bestDist = d;
					best = h;
				}
			}
			if(!best.isValid())
				continue;
			if(best != shooter->getPosition())
				moves.push_back(BattleAction::makeMove(shooter, best));
			moved.insert(shooter);
			taken.insert(best);
			// the shell: the free hexes beside the shooter, singles first, then weak stacks worth less than the shooter
			for(const BattleHex & around : best.getNeighbouringTiles())
			{
				if(!around.isAvailable() || taken.count(around))
					continue;
				const CStack * guard = nullptr;
				for(const CStack * candidate : shields)
				{
					if(moved.count(candidate) || candidate->doubleWide())
						continue;
					if(candidate->getCount() > 1 && worth(candidate) >= worth(shooter))
						continue;
					if(!vstd::contains(battle->battleGetAvailableHexes(candidate, false), around))
						continue;
					guard = candidate;
					break;
				}
				if(!guard)
					continue;
				moves.push_back(BattleAction::makeMove(guard, around));
				moved.insert(guard);
				taken.insert(around);
			}
		}
		for(const auto & move : moves)
			cb->battleMakeTacticAction(battleID, move);
		if(!moves.empty())
			logAi->info("FairAI battle: tactics phase - %d moves (shooters to the corners, their shell beside them)", static_cast<int>(moves.size()));
	}
	cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(battle ? battle->battleGetTacticsSide() : BattleSide::ATTACKER));
}

bool FairBattleAI::needsFallback(const CStack * stack) const
{
	return stack->creatureId() == CreatureID::CATAPULT
		|| stack->hasBonusOfType(BonusType::HEALER)
		|| stack->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE) // berserk: forced action
		|| (stack->canCast() && !ownCasters)
		|| !stack->getPosition().isValid();
}

std::optional<BattleAction> FairBattleAI::creatureSpell(const BattleID & battleID, const CStack * stack, const BattleAction & chosen)
{
	if(chosen.actionType == EActionType::WAIT)
		return std::nullopt; // its turn comes again in this round
	const bool attacking = chosen.actionType == EActionType::WALK_AND_ATTACK || chosen.actionType == EActionType::SHOOT;
	auto battle = cb->getBattle(battleID);

	// Spells that harm the enemy, heal, raise or summon: BattleAI counts the health they are worth.
	// Instead of a blow only if that AI itself would rather cast.
	BattleEvaluator evaluator(env, cb, stack, playerID, battleID, side, strengthRatio(battle, side), 10);
	if(auto cast = evaluator.findBestCreatureSpell(stack))
	{
		if(!attacking || evaluator.selectStackAction(stack).actionType == EActionType::MONSTER_SPELL)
			return BattleAction::makeCreatureSpellcast(stack, cast->dest, cast->spell->id);
		return std::nullopt;
	}
	if(attacking)
		return std::nullopt;

	// Blessings and curses, which BattleAI does not count: a blessing on the most valuable unit of
	// ours that can take it, a curse on the most valuable of theirs
	auto worth = [](const battle::Unit * unit)
	{
		const auto * type = unit->creatureId().toCreature();
		return type && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON) ? static_cast<double>(unit->getCount()) * type->getAIValue() : 0.0;
	};
	double bestWorth = 0;
	std::optional<BattleAction> best;
	if(stack->hasBonusOfType(BonusType::RANDOM_SPELLCASTER))
	{
		// (master genies: the game draws the blessing once the target is named)
		for(const auto * unit : battle->battleGetUnitsIf([this](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid() && u->unitSide() == side; }))
		{
			const SpellID drawn = battle->getRandomBeneficialSpell(CRandomGenerator::getDefault(), stack, unit);
			if(drawn == SpellID::NONE || worth(unit) <= bestWorth)
				continue;
			bestWorth = worth(unit);
			best = BattleAction::makeCreatureSpellcast(stack, battle::Target{battle::Destination(unit)}, drawn);
		}
		return best;
	}
	for(const auto & bonus : *stack->getBonusesOfType(BonusType::SPELLCASTER))
	{
		const SpellID id = bonus->subtype.as<SpellID>();
		if(!id.hasValue())
			continue;
		const CSpell * spell = id.toSpell();
		// (only the spells of the mage guilds: what a creature's own ability does is not ours to guess -
		// the automaton's "ignition" is a blessing by its flag, and blows the stack up after its next blow)
		if(!spell || spell->isCreatureAbility() || spell->isSpecial() || !spell->canBeCast(battle.get(), spells::Mode::CREATURE_ACTIVE, stack))
			continue;
		spells::BattleCast temp(battle.get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
		{
			if(target.empty())
				continue;
			const battle::Unit * unit = target[0].unitValue ? target[0].unitValue : battle->battleGetUnitByPos(target[0].hexValue, true);
			if(!unit || (unit->unitSide() == side) != spell->isPositive())
				continue;
			// (not twice on the same unit)
			if(unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id))))
				continue;
			if(worth(unit) <= bestWorth)
				continue;
			bestWorth = worth(unit);
			best = BattleAction::makeCreatureSpellcast(stack, target, id);
		}
	}
	return best;
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

void FairBattleAI::spellCheck(const BattleID & battleID)
{
	// Forecasts on the adventure map have no battle to ask what a spell does to a unit, so FairAI
	// carries its own copy of the game's rules (SpellSim). Here, in a battle, both can be asked:
	// every combat spell of the game against every unit on the field, and - if we have a hero - the
	// damage of each of its spells on each unit. Differences are logged.
	const auto battle = cb->getBattle(battleID);
	std::vector<const battle::Unit *> units;
	for(const auto * unit : battle->battleGetUnitsIf([](const battle::Unit * u) { return u->alive() && !u->isGhost() && u->getPosition().isValid(); }))
		units.push_back(unit);
	if(units.empty())
		return;
	const battle::Unit * anyCaster = nullptr;
	for(const auto * unit : units)
	{
		if(unit->unitSide() == side)
			anyCaster = unit;
	}
	if(!anyCaster)
		return;

	int pairs = 0;
	int differ = 0;
	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell || !spell->isCombat())
			continue;
		try
		{
			spells::BattleCast event(battle.get(), anyCaster, spells::Mode::HERO, spell.get());
			const auto mechanics = spell->battleMechanics(&event);
			for(const auto * unit : units)
			{
				const IBonusBearer & bearer = *unit->getBonusBearer();
				const bool mine = spellWorksOn(*spell, bearer, unit->creatureId());
				const bool game = mechanics->isReceptive(unit);
				double mineShare = 1.0;
				double gameShare = 1.0;
				if(spell->isDamage() && game)
				{
					mineShare = spellDamageShare(*spell, bearer, unit->creatureId());
					gameShare = spell->adjustRawDamage(anyCaster, unit, 1000000) / 1000000.0;
				}
				++pairs;
				if(mine != game || std::abs(mineShare - gameShare) > 0.001)
				{
					++differ;
					logAi->info("FairAI spellcheck|rule|%s|%s|%d|%d|%.3f|%.3f", spell->getJsonKey(), unit->creatureId().toCreature()->getJsonKey(), mine ? 1 : 0, game ? 1 : 0, mineShare, gameShare);
				}
			}
		}
		catch(const std::exception & e)
		{
			logAi->info("FairAI spellcheck|error|%s|%s", spell->getJsonKey(), e.what());
		}
	}

	// our hero's damage spells: the number a forecast would use against the game's
	int casts = 0;
	int castsDiffer = 0;
	if(const CGHeroInstance * hero = battle->battleGetMyHero())
	{
		std::vector<StackSpec> stacks;
		for(const auto * unit : units)
			stacks.push_back({unit, unit->unitType(), unit->getCount(), unit->unitSide()});
		HeroSpec spec;
		spec.hero = hero;
		int mana = 0;
		for(const SimSpell & entry : describeSpells(spec, side, stacks, mana))
		{
			const CSpell * spell = SpellID(entry.id).toSpell();
			spells::BattleCast event(battle.get(), hero, spells::Mode::HERO, spell);
			const auto mechanics = spell->battleMechanics(&event);
			const int cost = battle->battleGetSpellCost(spell, hero);
			for(size_t i = 0; i < units.size(); ++i)
			{
				const double game = mechanics->isReceptive(units[i]) ? static_cast<double>(mechanics->adjustEffectValue(units[i])) : 0.0;
				++casts;
				if(std::abs(entry.damage[i] - game) > std::max(1.0, 0.005 * game) || entry.cost != cost)
				{
					++castsDiffer;
					logAi->info("FairAI spellcheck|damage|%s|%s|%.0f|%.0f|%d|%d", spell->getJsonKey(), units[i]->creatureId().toCreature()->getJsonKey(), entry.damage[i], game, entry.cost, cost);
				}
			}
		}
	}
	logAi->info("FairAI spellcheck|sum|%d|%d|%d|%d", pairs, differ, casts, castsDiffer);
}

void FairBattleAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	if(!spellCheckDone)
	{
		spellCheckDone = true;
		static const bool wanted = std::getenv("FAI_SPELLCHECK") != nullptr;
		if(wanted)
		{
			try
			{
				spellCheck(battleID);
			}
			catch(const std::exception & e)
			{
				logAi->info("FairAI spellcheck|error|-|%s", e.what());
			}
		}
	}
	// test bench: this side is played from outside the game (Puppet.h); "auto" as an answer falls through
	if(Puppet::controls(side))
	{
		if(auto battle = cb->getBattle(battleID))
		{
			if(auto action = Puppet::ask(*battle, stack, side))
			{
				cb->battleMakeUnitAction(battleID, *action);
				return;
			}
		}
	}
	try
	{
		watchDeadlock(battleID);
		const bool standsStill = deadlockLost(battleID);
		if(standsStill)
		{
			if(auto end = considerRetreat(battleID))
			{
				cb->battleMakeUnitAction(battleID, *end);
				return;
			}
		}
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

		if(!standsStill) // (asked above already)
		{
			if(auto retreat = considerRetreat(battleID))
			{
				cb->battleMakeUnitAction(battleID, *retreat);
				return;
			}
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
		int urgency = deadlockSince >= 0 ? 2 : pressure ? 1 : 0;
		// Troops that cannot answer an enemy that shoots must close in: the look-ahead of the plain search
		// ends with the round, and within a round standing back always costs least - so they stood and
		// waited until the last of them was shot (seen with map guards played by FairAI: 150 gnoll
		// marauders against grand elves, user's test map, 23.11.2026)
		if(urgency < 2 && outranged(battleID))
			urgency = 2;
		BattleAction chosen = searchUnitAction(battleID, stack, urgency);
		if(stack->canCast())
		{
			if(auto cast = creatureSpell(battleID, stack, chosen))
			{
				logAi->debug("FairBattleAI: %s casts instead (spell %d)", stack->getDescription(), cast->spell.getNum());
				chosen = *cast;
			}
		}
		cb->battleMakeUnitAction(battleID, withinReach(battleID, stack, chosen));
	}
	catch(const std::exception & e)
	{
		logAi->error("FairBattleAI: search failed (%s), falling back to BattleAI", e.what());
		fallback->activeStack(battleID, stack);
	}
}

BattleAction FairBattleAI::withinReach(const BattleID & battleID, const CStack * stack, const BattleAction & order) const
{
	// An order the engine turns down costs the unit its turn (and once ended a whole game: the server
	// threw on a destination it had no way to, mirror test 24.11.2026). Plans come out of our own
	// simulator; whatever it does not know about the field must not reach the engine as an order.
	if(order.actionType != EActionType::WALK && order.actionType != EActionType::WALK_AND_ATTACK)
		return order;
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return order;
	const auto target = order.getTarget(battle.get());
	if(target.empty())
		return order;
	const BattleHex wanted = target.at(0).hexValue;
	if(!wanted.isValid() || wanted == stack->getPosition())
		return order;
	const auto reach = battle->getReachability(stack);
	const auto hexes = battle->battleGetAvailableHexes(reach, stack, true);
	if(hexes.contains(wanted))
		return order;

	// as near to where it was sent as it gets
	BattleHex best;
	int bestDistance = BattleHex::getDistance(stack->getPosition(), wanted);
	for(const auto & hex : hexes)
	{
		const int distance = BattleHex::getDistance(hex, wanted);
		if(distance < bestDistance)
		{
			bestDistance = distance;
			best = hex;
		}
	}
	logAi->info("FairAI battle: %s has no way to hex %d, %s", stack->getDescription(), wanted.toInt(),
		best.isValid() ? "goes to hex " + std::to_string(best.toInt()) : std::string("stands"));
	return best.isValid() ? BattleAction::makeMove(stack, best) : BattleAction::makeDefend(stack);
}

bool FairBattleAI::outranged(const BattleID & battleID) const
{
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;
	// where the battle plan decides between holding and advancing by playing both to the end, it knows better
	const PlayerColor enemyOwner = battle->sideToPlayer(battle->otherSide(side));
	if(battlePlan && (planVsPlayers || enemyOwner == PlayerColor::NEUTRAL))
		return false;
	// a hero of ours that still casts may be the answer
	if(const auto * hero = battle->battleGetMyHero(); hero && hero->hasSpellbook() && hero->mana > 0)
		return false;
	int enemyShots = 0;
	for(const auto * unit : battle->battleGetAllStacks(false))
	{
		if(!unit->alive() || unit->isGhost())
			continue;
		const int shots = unit->isShooter() ? unit->acquireState()->shots.available() : 0;
		if(unit->unitSide() == side)
		{
			if(shots > 0 || unit->canCast())
				return false; // we can answer from afar
		}
		else if(!unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			enemyShots += shots;
	}
	return enemyShots >= 2;
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
	verdict.deadlock = deadlockLost(battleID);
	setBattleVerdict(playerID, verdict);
	// the adventure AI decides: it knows what the hero, the army and our gold are worth
	auto decision = cb->makeSurrenderRetreatDecision(battleID, bs);
	if(!decision && verdict.deadlock && bs.canFlee)
	{
		// (an adventure AI other than ours knows nothing of battles that stand still)
		logAi->info("FairAI %s: battle stands still since round %d and our attacks change nothing - we leave it", playerID.toString(), deadlockSince);
		decision = BattleAction::makeRetreat(bs.ourSide);
	}
	return decision;
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
		// (spellForecast) with our hero's damage spells: two more plans, and the spell points each uses
		HeroSpec spec;
		spec.hero = spellForecast ? battle->battleGetMyHero() : nullptr;
		const Forecast map = forecastBattle(stacks, side, layout, 30, searchSettings().enemyModel == 1 ? Stance::STALK : Stance::CHARGE,
			spec.hero && spec.hero->hasSpellbook() ? &spec : nullptr);
		text += boost::str(boost::format("|%.0f|%.0f|%.0f|%.0f|%.0f|%.0f") % map.ownBefore % map.enemyBefore % map.ownAfter[0] % map.enemyAfter[0] % map.ownAfter[1] % map.enemyAfter[1]);
		if(map.plans > 2)
			text += boost::str(boost::format("|%.0f|%.0f|%d|%.0f|%.0f|%d") % map.ownAfter[2] % map.enemyAfter[2] % map.manaSpent[2] % map.ownAfter[3] % map.enemyAfter[3] % map.manaSpent[3]);
		if(battle->sideToPlayer(other) == PlayerColor::NEUTRAL && !battle->battleGetDefendedTown())
		{
			// ... and played the way the combat AI plays guards: four plans for our side, the guards by
			// the model of VCMI's BattleAI. Logged on a line of its own, with the time it took.
			ForecastOptions options;
			options.ownStances = 4;
			options.guardTurns = env->game()->getStartInfo()->difficulty < 4 ? 2 : 10;
			options.kiteTurns = kitePlan > 0 ? KITE_TURNS : 0;
			const auto started = std::chrono::steady_clock::now();
			const Forecast planned = forecastBattle(stacks, side, layout, 30, Stance::STALK, nullptr, nullptr, options);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
			const int plan = planned.bestPlan();
			logAi->info("FairAI planforecast|%.0f|%.0f|%.0f|%.0f|%d|%.2f", planned.ownBefore, planned.enemyBefore, planned.ownAfter[plan], planned.enemyAfter[plan], plan, ms);
		}
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

BattleAction FairBattleAI::searchUnitAction(const BattleID & battleID, const CStack * stack, int urgency)
{
	const auto started = std::chrono::steady_clock::now();
	auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	auto choice = bestUnitAction(state, stack, -1, urgency);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	logAi->debug("FairBattleAI: %s -> %s (score %.0f, %.1f ms)%s", stack->getDescription(), choice.description, choice.score, ms,
		urgency >= 2 ? " [must strike]" : urgency == 1 ? " [must act]" : "");
	return choice.action;
}

FairBattleAI::UnitChoice FairBattleAI::bestUnitAction(const std::shared_ptr<HypotheticBattle> & battle, const CStack * realStack, int roundsAhead, int urgency)
{
	const bool forbidPassive = urgency >= 1;
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

	// (areaAim) An area shot - Magog's fireball, Lich's death cloud - hits every unit the blast covers,
	// ours too. Aimed at each hex from which the blast still reaches an enemy (beside it, where the rules
	// let a shot land on an empty hex), every unit in the blast takes its shot's damage; the look-ahead
	// weighs what the enemy loses against what we lose. (Owner, 27.11.2026: Magogs often hit their own
	// troops - a player aims so that the blast spares them.)
	if(areaAim && battle->battleCanShoot(stack) && stack->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
	{
		const auto bonus = stack->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
		const CSpell * spell = bonus && bonus->subtype.hasValue() ? bonus->subtype.as<SpellID>().toSpell() : nullptr;
		const bool emptyHexes = battle->battleCanTargetEmptyHex(stack);
		std::set<BattleHex> aims;
		for(const auto & e : base.units)
		{
			if(!e.alive() || e.side == base.units[me].side)
				continue;
			const auto * enemy = battle->battleGetUnitByID(e.id);
			if(!enemy)
				continue;
			for(const auto & hex : enemy->getHexes())
			{
				aims.insert(hex);
				if(emptyHexes)
					for(const auto & next : hex.getNeighbouringTiles())
						if(next.isValid())
							aims.insert(next);
			}
		}
		for(const auto & aim : aims)
		{
			if(!spell || !battle->battleCanShoot(stack, aim))
				continue;
			const auto * standing = battle->battleGetUnitByPos(aim, true);
			if(!emptyHexes && (!standing || standing->unitSide() == stack->unitSide()))
				continue;
			spells::BattleCast cast(battle.get(), stack, spells::Mode::SPELL_LIKE_ATTACK, spell);
			cast.setSpellLevel(bonus->val);
			auto mechanics = spell->battleMechanics(&cast);
			if(!mechanics)
				continue;
			spells::Target where;
			where.emplace_back(aim);
			const auto hit = mechanics->getAffectedStacks(where);
			AttackOutcome outcome;
			outcome.shooting = true;
			outcome.attackerCount = stack->getCount();
			outcome.attackerFirstHP = stack->getFirstHPleft();
			int64_t bestDamage = -1;
			bool enemyHit = false;
			for(const battle::Unit * u : hit)
			{
				const int index = base.indexOf(u->unitId());
				if(index < 0 || !u->alive())
					continue;
				BattleAttackInfo info(stack, u, 0, true);
				const auto estimate = battle->battleEstimateDamage(info);
				auto state = u->acquireState();
				int64_t damage = std::min<int64_t>((estimate.damage.min + estimate.damage.max) / 2, state->getAvailableHealth());
				const int64_t dealt = damage;
				state->damage(damage);
				outcome.affected.emplace_back(index, state->getCount(), state->getFirstHPleft());
				if(u->unitSide() != stack->unitSide())
				{
					enemyHit = true;
					if(dealt > bestDamage)
					{
						bestDamage = dealt;
						outcome.target = index;
					}
				}
			}
			if(!enemyHit || outcome.target < 0)
				continue;
			BattleAction shot;
			shot.side = stack->unitSide();
			shot.actionType = EActionType::SHOOT;
			shot.stackNumber = stack->unitId();
			shot.aimToHex(aim);
			candidates.push_back({{SimActionType::SHOOT, outcome.target, BattleHex()}, shot, "aimed shot at hex " + std::to_string(aim.toInt()) + " (" + std::to_string(outcome.affected.size()) + " units in the blast)", outcome});
		}
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
		// (only when the enemy has shooters: without any, every hex scored the same and the unit was sent
		// to the first hex of the field - royal griffins flew into the top corner)
		bool enemyShoots = false;
		for(const auto & e : base.units)
			enemyShoots = enemyShoots || (e.alive() && e.side != base.units[me].side && e.shooter);
		if(enemyShoots)
			addMove(pick([&](const BattleHex & h) { return closestEnemyDistance(h, true, false); }), "advance to shooters");
		if(searchSettings().ambush >= 2 || guardPlan)
			addMove(base.rallyHex(me), "close ranks");
		if(searchSettings().corner || cornerMove)
		{
			// Into the corner of our side of the field that lies nearer to our troops: there an enemy
			// reaches us from fewer hexes, and our troops stand together. (Found by accident: a bug sent
			// units to the first hex of the field, and the battle plan lost less with it than without.)
			double rows = 0;
			int own = 0;
			for(const auto & u : base.units)
			{
				if(u.alive() && u.side == base.units[me].side)
				{
					rows += u.pos.getY();
					++own;
				}
			}
			const int cornerY = own && rows / own > (GameConstants::BFIELD_HEIGHT - 1) / 2.0 ? GameConstants::BFIELD_HEIGHT - 1 : 0;
			const int cornerX = base.units[me].side == BattleSide::ATTACKER ? 1 : GameConstants::BFIELD_WIDTH - 2;
			const BattleHex corner(cornerX, cornerY);
			addMove(pick([&](const BattleHex & h) { return static_cast<int>(BattleHex::getDistance(h, corner)); }), "to the corner");
		}
		if(base.units[me].shooter)
			addMove(pick([&](const BattleHex & h) { return -closestEnemyDistance(h, false, true); }), "keep distance");
	}

	if(urgency >= 2)
	{
		// The battle stands still: only blows count now, and with none in reach the way to the enemy
		const bool blow = std::any_of(candidates.begin(), candidates.end(), [](const Candidate & c) { return c.sim.type != SimActionType::MOVE; });
		std::erase_if(candidates, [blow](const Candidate & c)
		{
			return c.sim.type == SimActionType::MOVE && (blow || c.description.rfind("advance", 0) != 0);
		});
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
	const BattleSide otherSide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	std::vector<Stance> ownPlans = {Stance::HOLD, Stance::CHARGE};
	if(config.ambush || ambushPlan)
		ownPlans.push_back(Stance::AMBUSH);
	if(config.ambush >= 2 || guardPlan)
		ownPlans.push_back(Stance::GUARD);
	// (latePlan) "wait, then advance": nothing in reach - wait, and walk when the others have moved.
	// Level 1: only while the enemy has troops that shoot - there walking at once takes our walkers
	// into the range where a shot counts in full (the plan gained there and nowhere else)
	bool lateNow = latePlan >= 2;
	if(latePlan == 1)
	{
		for(const auto & u : base.units)
			lateNow = lateNow || (u.alive() && u.side != base.units[me].side && u.shooter && u.shots > 0);
	}
	if(lateNow)
		ownPlans.push_back(Stance::STALK);
	// (the environment variables of the test bench win over the player's parameters)
	const int oracleRounds = config.oracleRounds > 0 ? config.oracleRounds : guardOracle;
	const int oracleUntil = config.oracleRounds > 0 ? config.oracleUntilRound : guardOracleUntil;
	if(oracleRounds > 0 && roundsAhead < 0 && battle->sideToPlayer(otherSide) == PlayerColor::NEUTRAL && battle->battleGetRound() <= oracleUntil)
	{
		// Against guards we know the enemy's mind: VCMI's BattleAI plays them, and the oracle runs that
		// very code on a mirror of each state. Every candidate is played on with our troops once
		// holding and once advancing - the plan is ours to choose - and the enemy answering as it
		// really would for oracleRounds rounds; after that the default policies finish the battle.
		// What counts is what we have left at the end.
		struct Adapter : EnemyOracle
		{
			FairBattleAI * ai;
			std::shared_ptr<CBattleInfoCallback> real;
			int baseRound;
			bool choose(const SimState & state, int unit, SimAction & out) const override
			{
				try
				{
					return ai->oracleAction(real, state, unit, baseRound, out);
				}
				catch(const std::exception &)
				{
					return false;
				}
			}
		} adapter;
		adapter.ai = this;
		adapter.real = battle;
		adapter.baseRound = base.round;
		const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
		const int planEnd = base.round + std::max(1, config.planRounds);
		int calls = 0;
		for(auto & c : candidates)
		{
			double bestPlan = -std::numeric_limits<double>::max();
			for(Stance ownStance : ownPlans)
			{
				SimState sim = base;
				sim.stance[ownSide] = ownStance;
				sim.stance[enemySide] = Stance::STALK;
				sim.oracle = &adapter;
				sim.oracleSide = otherSide;
				sim.oracleLastRound = base.round + oracleRounds - 1;
				if(c.outcome && config.engineOutcome)
					sim.applyOutcome(me, *c.outcome);
				else
					sim.apply(me, c.sim);
				sim.playout(planEnd);
				calls += sim.oracleCalls;
				bestPlan = std::max(bestPlan, sim.evaluate(side));
			}
			c.score = bestPlan;
			if(!best || c.score > best->score + 1e-6)
				best = &c;
		}
		oracleCallsMade += calls;
		if(logAi->isDebugEnabled())
			logAi->debug("FairAI oracleprofile|%d candidates|%d calls|mirror %.1f|damage %.1f|targets %.1f|move %.1f|best %.1f ms (sums of this battle AI)", static_cast<int>(candidates.size()),
				static_cast<int>(oracleProfile[0]), oracleProfile[1], oracleProfile[2], oracleProfile[3], oracleProfile[4], oracleProfile[5]);
		return {best->real, best->score, best->description + " [oracle]"};
	}
	// Whom GuardModel can play for us: neutral troops - VCMI's BattleAI plays them unless the game is
	// set up otherwise - and, from level 2 on, the troops of a computer player, which the game's battle
	// AI for computer players plays (taken to be BattleAI; its hero's spells are not foreseen). A human
	// plays as he likes. No guessing between "they advance" and "they lie in wait" then.
	const int modelLevel = config.guardModel >= 0 ? config.guardModel : guardModel;
	const PlayerColor enemyOwner = battle->sideToPlayer(otherSide);
	bool modelGuards = false;
	if(modelLevel > 0 && !battle->battleGetDefendedTown())
	{
		if(enemyOwner == PlayerColor::NEUTRAL)
		{
			{
				const char * arenaNeutral = std::getenv("ARENA_NEUTRAL_AI"); // (the test bench names the guards' combat AI per process)
				modelGuards = (arenaNeutral && *arenaNeutral ? std::string(arenaNeutral) : settings["ai"]["combatNeutralAI"].String()) == "BattleAI";
			}
		}
		else if(modelLevel >= 2 && enemyOwner.isValidPlayer())
		{
			// (who is human is part of the game's set-up, known to everybody)
			const auto & players = env->game()->getStartInfo()->playerInfos;
			const auto owner = players.find(enemyOwner);
			modelGuards = owner != players.end() && !owner->second.isControlledByHuman();
		}
	}
	const auto guardTurns = static_cast<uint8_t>(env->game()->getStartInfo()->difficulty < 4 ? 2 : 10); // as BattleAI sets itself up
	// (the plan is made against neutral troops; against the army of a hero it lost badly as long as
	// the enemy was guessed - 498 battles against BattleAI with heroes: 60 % won instead of 74 % -,
	// so there only on demand: planVsPlayers)
	if(battlePlan && roundsAhead < 0 && (planVsPlayers || enemyOwner == PlayerColor::NEUTRAL))
	{
		// Battle plan: every candidate is played on for several rounds, once with our troops advancing
		// and once with them holding their ground (shooting, hitting only what comes into reach). The
		// better of the two counts - the plan is ours to choose. This is what tells "let them come
		// while our shooters work" from "close in before their shooters wear us down".
		const int ownSide = side == BattleSide::ATTACKER ? 0 : 1;
		const int planEnd = base.round + std::max(1, config.planRounds);
		if(modelGuards)
			enemyStances = {Stance::STALK};
		static const bool showCandidates = std::getenv("FAI_CANDIDATES") != nullptr;
		// (test) FAI_PLAN_TRACE=<part of a unit's description>: for the turns of such a unit every candidate
		// is played on step by step under every plan and written down - who does what, who is left where
		static const char * const tracedUnit = std::getenv("FAI_PLAN_TRACE");
		const bool tracePlans = tracedUnit && stack->getDescription().find(tracedUnit) != std::string::npos;
		const auto tracePlayout = [&](SimState sim, int lastRound)
		{
			static const char * const names[] = {"move", "attack", "shoot", "wait", "defend"};
			std::string trace;
			for(int step = 0; step < 60; ++step)
			{
				const int unit = sim.nextActiveUnit();
				if(unit < 0 || sim.round > lastRound)
					break;
				if(sim.heroCast(sim.units[unit].side) && (sim.battleOver() || !sim.units[unit].alive()))
					continue;
				sim.playDefault(unit);
				const auto & a = sim.lastAction;
				trace += boost::str(boost::format("\n      r%d %s%d(%s) %s") % sim.round % (sim.units[unit].side == side ? "our " : "their ") % unit
					% (sim.units[unit].creature.hasValue() ? sim.units[unit].creature.toCreature()->getJsonKey() : std::string("?")) % names[std::clamp(static_cast<int>(a.type), 0, 4)]);
				if(a.target >= 0)
					trace += " ->" + std::to_string(a.target);
				if(a.hex.isValid())
					trace += " @" + std::to_string(a.hex.toInt());
				trace += " |";
				for(int i = 0; i < static_cast<int>(sim.units.size()); ++i)
					if(sim.units[i].alive())
						trace += boost::str(boost::format(" %d:%dx@%d") % i % sim.units[i].count % sim.units[i].pos.toInt());
			}
			return trace;
		};
		// (Tried and dropped, 19.11.: a bonus for the plan the unit before acted by, so that the units of a
		// round follow one forecast. 579 battles: 0.3 +- 0.3 points of the army lost MORE with a bonus of
		// one per cent of the army; with the fifth plan, 213 battles, no difference at one or three.)
		std::string shown;
		for(auto & c : candidates)
		{
			double bestPlan = -std::numeric_limits<double>::max();
			if(showCandidates)
				shown += "\n  " + c.description + ":";
			for(Stance ownStance : ownPlans)
			{
				double worst = std::numeric_limits<double>::max();
				for(Stance enemyStance : enemyStances)
				{
					SimState sim = base;
					sim.stance[ownSide] = ownStance;
					sim.stance[enemySide] = enemyStance;
					if(modelGuards)
						sim.guardModel[enemySide] = guardTurns;
					if(c.outcome && config.engineOutcome)
						sim.applyOutcome(me, *c.outcome);
					else
						sim.apply(me, c.sim);
					if(tracePlans)
						logAi->info("FairAI plantrace %s, plan %d:%s", c.description, static_cast<int>(ownStance), tracePlayout(sim, planEnd));
					sim.playout(planEnd);
					worst = std::min(worst, sim.evaluate(side));
					if(showCandidates)
						shown += boost::str(boost::format(" %.0f (own %.0f, round %d%s)") % sim.evaluate(side) % sim.sideValue(side) % sim.round % (sim.battleOver() ? ", over" : ""));
				}
				bestPlan = std::max(bestPlan, worst);
			}
			c.score = bestPlan;
			if(!best || c.score > best->score + 1e-6)
				best = &c;
		}
		// The fifth plan, "shoot and stay out of reach" (kitePlan). It pays over many rounds only - a
		// walker that never reaches us is shot down without a scratch -, so it is played to the end of
		// the battle, and judged by what the plan itself does with this unit. It is taken when the
		// battle ends better with it than with the best action of the other plans, played as far.
		// (freeKite) The same with troops nothing strikes back at, shots or none: they wait while the
		// enemy cannot reach them, strike when it has moved, and are gone before it moves again.
		const int kiteWith = (kitePlan > 0 && modelGuards ? 1 : 0) | (freeKite > 0 ? 2 : 0);
		if(kiteWith && best && urgency == 0)
		{
			bool shots = false;
			for(int i = 0; i < static_cast<int>(base.units.size()) && !shots; ++i)
			{
				const auto & own = base.units[i];
				if(!own.alive() || own.side != base.units[me].side)
					continue;
				shots = ((kiteWith & 1) && own.shooter && own.shots > 0) || ((kiteWith & 2) && base.kiteStriker(i));
			}
			if(shots)
			{
				const int kiteEnd = base.round + KITE_ROUNDS;
				SimState sim = base;
				sim.kiteWith = kiteWith;
				sim.stance[ownSide] = Stance::KITE;
				sim.stance[enemySide] = Stance::STALK;
				sim.guardModel[enemySide] = guardTurns;
				sim.stopAtKiteEnd = true;
				sim.kiteDeep = true;
				sim.playDefault(me);
				sim.kiteDeep = false;
				const SimAction first = sim.lastAction;
				// (cornered at once, or no shot left: there is no such plan now)
				const bool noKite = sim.kiteOver[ownSide];
				static const bool traced = std::getenv("FAI_KITE_TRACE") != nullptr;
				if(traced && !noKite)
				{
					// (test) the plan step by step: who does what, and where everybody stands afterwards
					std::string trace;
					const auto note = [&](int unit)
					{
						static const char * const names[] = {"move", "attack", "shoot", "wait", "defend"};
						const auto & a = sim.lastAction;
						trace += boost::str(boost::format("\n  r%d %s%d %s") % sim.round % (sim.units[unit].side == side ? "our " : "their ") % unit % names[std::clamp(static_cast<int>(a.type), 0, 4)]);
						if(a.target >= 0)
							trace += " ->" + std::to_string(a.target);
						if(a.hex.isValid())
							trace += " @" + std::to_string(a.hex.toInt());
						trace += " |";
						for(int i = 0; i < static_cast<int>(sim.units.size()); ++i)
							if(sim.units[i].alive())
								trace += boost::str(boost::format(" %d:%dx@%d") % i % sim.units[i].count % sim.units[i].pos.toInt());
					};
					note(me);
					SimState copy = sim;
					copy.stopAtKiteEnd = false;
					for(int step = 0; step < 400; ++step)
					{
						const int unit = copy.nextActiveUnit();
						if(unit < 0 || copy.round > kiteEnd)
							break;
						copy.playDefault(unit);
						std::swap(copy, sim);
						note(unit);
						std::swap(copy, sim);
					}
					logAi->info("FairAI kitetrace for %s:%s", stack->getDescription(), trace);
				}
				// (how long to keep it up - see playKite)
				double kiteValue = -std::numeric_limits<double>::max();
				bool kiteWhole = false; // nothing of ours is lost under the plan
				int bestTurn = -1;
				int lasted = -1;
				if(!noKite)
				{
					std::string report;
					SimState turned = sim;
					const SimState end = playKite(sim, side, ownPlans, KITE_TURNS, kiteEnd, &bestTurn, &lasted, traced ? &report : nullptr, traced ? &turned : nullptr);
					kiteValue = end.evaluate(side);
					kiteWhole = end.sideValue(side) > base.sideValue(side) - 1e-6;
					if(traced)
					{
						// ... and the best way on from where we turn, step by step
						static const char * const names[] = {"move", "attack", "shoot", "wait", "defend"};
						for(int step = 0; step < 300; ++step)
						{
							const int unit = turned.nextActiveUnit();
							if(unit < 0 || turned.round > kiteEnd)
								break;
							turned.playDefault(unit);
							const auto & a = turned.lastAction;
							report += boost::str(boost::format("\n  r%d %s%d %s") % turned.round % (turned.units[unit].side == side ? "our " : "their ") % unit % names[std::clamp(static_cast<int>(a.type), 0, 4)]);
							if(a.target >= 0)
								report += " ->" + std::to_string(a.target);
							if(a.hex.isValid())
								report += " @" + std::to_string(a.hex.toInt());
							report += " |";
							for(int i = 0; i < static_cast<int>(turned.units.size()); ++i)
								if(turned.units[i].alive())
									report += boost::str(boost::format(" %d:%dx@%d") % i % turned.units[i].count % turned.units[i].pos.toInt());
						}
						logAi->info("FairAI kitevalues for %s:%s", stack->getDescription(), report);
					}
				}
				if(showCandidates && !noKite)
					shown += boost::str(boost::format("\n  shoot and stay out of reach (type %d, target %d, hex %d): %.0f (best kept up to round %d; it lasts to round %d)") % static_cast<int>(first.type) % first.target
						% first.hex.toInt() % kiteValue % bestTurn % lasted);
				// The plan must be better by one per cent of our army (FAI_KITE_MARGIN, for tests): 213
				// battles against walking guards with shooters of our own lost 1.5 +- 0.9 points of the
				// army less with it than without the plan - as with no margin (1.8 +- 1.0) -, but against
				// weak guards, the usual case, only with the margin fewer battles got worse (3 of 68
				// instead of 6; 0.5 +- 0.3 points less instead of none). Three per cent: 0.9 +- 0.8.
				static const double marginShare = envInt("FAI_KITE_MARGIN", 1) / 100.0;
				// A plan under which nothing of ours is ever struck wins a tie: it does not depend on the
				// forecast of the blows we would take otherwise.
				const double margin = kiteWhole ? -1e-6 : marginShare * base.sideValue(side) + 1e-6;
				if(!noKite && kiteValue > best->score + margin)
				{
					// (the same horizon for the best of the others)
					double rival = -std::numeric_limits<double>::max();
					for(Stance ownStance : ownPlans)
					{
						SimState other = base;
						other.stance[ownSide] = ownStance;
						other.stance[enemySide] = Stance::STALK;
						other.guardModel[enemySide] = guardTurns;
						if(best->outcome && config.engineOutcome)
							other.applyOutcome(me, *best->outcome);
						else
							other.apply(me, best->sim);
						other.playout(kiteEnd);
						rival = std::max(rival, other.evaluate(side));
					}
					if(showCandidates)
						shown += boost::str(boost::format(" against %.0f for %s played as far") % rival % best->description);
					std::optional<BattleAction> real;
					std::string what;
					for(const auto & c : candidates)
					{
						if(c.sim.type == first.type && c.sim.target == first.target && (c.sim.hex == first.hex || first.type == SimActionType::SHOOT
							|| first.type == SimActionType::WAIT || first.type == SimActionType::DEFEND))
						{
							real = c.real;
							what = c.description;
						}
					}
					if(!real && first.type == SimActionType::MOVE && moveHexes.contains(first.hex))
					{
						real = BattleAction::makeMove(stack, first.hex);
						what = "step aside to " + std::to_string(first.hex.toInt());
					}
					if(real && kiteValue > rival + margin)
					{
						if(showCandidates)
							logAi->info("FairAI candidates for %s (plans hold, advance%s%s%s; guards by model):%s", stack->getDescription(), ownPlans.size() > 2 ? ", lie in wait" : "",
								ownPlans.size() > 3 ? ", close ranks" : "", lateNow ? ", wait then advance" : "", shown);
						return {*real, kiteValue, what + " [out of reach]"};
					}
				}
			}
		}
		if(showCandidates)
			logAi->info("FairAI candidates for %s (plans hold, advance%s%s%s; guards by %s):%s", stack->getDescription(), ownPlans.size() > 2 ? ", lie in wait" : "", ownPlans.size() > 3 ? ", close ranks" : "", lateNow ? ", wait then advance" : "",
				modelGuards ? "model" : "stance", shown);
		return {best->real, best->score, best->description};
	}
	// the plain search: the rest of this round. With modelSearch the enemy's part of it is played by
	// GuardModel as well, where that applies.
	const bool modelRest = modelGuards && modelSearch && roundsAhead < 0;
	if(modelRest)
		enemyStances = {Stance::STALK};
	for(auto & c : candidates)
	{
		double worst = std::numeric_limits<double>::max();
		for(Stance enemyStance : enemyStances)
		{
			SimState sim = base;
			sim.stance[enemySide] = enemyStance;
			if(modelRest)
				sim.guardModel[enemySide] = guardTurns;
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
