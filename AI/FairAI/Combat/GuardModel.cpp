/*
 * GuardModel.cpp, part of VCMI engine - FairAI
 *
 * A native copy of the decision rules of VCMI's BattleAI - see GuardModel.h. The functions follow
 * the originals closely, quirks included, because the point is to predict what that AI does, not
 * to do better: each one names the function of AI/BattleAI it copies.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "GuardModel.h"

#include "../../../lib/battle/Unit.h"

namespace FAI::Combat
{

namespace
{

constexpr int MAX_UNITS = 32;
constexpr int32_t INFINITE_DIST = 1000000;          // ReachabilityInfo::INFINITE_DIST
constexpr float INEFFECTIVE_SCORE = -100000000.0f;  // EvaluationResult::INEFFECTIVE_SCORE
constexpr int ORDER_TURNS = 2;                      // BattleExchangeEvaluator::updateReachabilityMap: TURN_DEPTH
constexpr int REACH_ROWS = 6;

using Distances = std::array<int32_t, FIELD_SIZE>;
using Predecessors = std::array<int16_t, FIELD_SIZE>;
using Occupants = std::array<int16_t, FIELD_SIZE>;

inline uint32_t bit(int unit)
{
	return 1u << unit;
}

inline int lowest(uint32_t mask)
{
	return std::countr_zero(mask);
}

float hpFunction(uint64_t healthStart, uint64_t healthEnd, uint64_t maxHealth)
{
	const float ratioStart = static_cast<float>(healthStart) / maxHealth;
	const float ratioEnd = static_cast<float>(healthEnd) / maxHealth;
	const float base = 0.666666f;
	return (base * (4 - ratioStart - ratioEnd)) * (ratioStart - ratioEnd) / 2;
}

bool almostEqual(float left, float right)
{
	const float relative = std::max(std::abs(left), std::abs(right)) * 0.00001f;
	return std::abs(left - right) <= relative;
}

/// A short list of hexes without duplicates, in the order they were added (BattleHexArray)
struct HexList
{
	std::array<BattleHex, 24> hex;
	int size = 0;

	void add(const BattleHex & h)
	{
		if(!h.isValid() || contains(h) || size >= static_cast<int>(hex.size()))
			return;
		hex[size++] = h;
	}
	bool contains(const BattleHex & h) const
	{
		for(int i = 0; i < size; ++i)
			if(hex[i] == h)
				return true;
		return false;
	}
};

/// AttackPossibility: one attack the active unit can make now, and what BattleAI thinks of it
struct Attack
{
	int target = -1;
	BattleHex from;              ///< where the unit stands when it strikes (invalid: a shot)
	bool shooting = false;
	bool real = false;           ///< BattleAI: attackerState is set (false: no hex of the target can be struck from there)
	float defenderReduce = 0;    ///< what the target can do less to us afterwards
	float attackerReduce = 0;    ///< what we can do less after its retaliation
	float collateralReduce = 0;  ///< what our own side loses to the blow or to the retaliation (attacks that reach several hexes)
	int64_t blockedShooters = 0; ///< what shooters next to that hex lose when they cannot shoot any more
	bool defenderDead = false;
	int64_t attackerHealth = 0;  ///< the health of both after the attack
	int64_t defenderHealth = 0;
	bool defenderRetaliates = false; ///< the target can still retaliate afterwards
	// whoever else is hit, and the health left to each
	std::array<int8_t, 10> others{};
	std::array<int64_t, 10> othersHealth{};
	int othersCount = 0;

	float value() const
	{
		return defenderReduce - attackerReduce - collateralReduce + blockedShooters;
	}
};

struct Score
{
	float enemy = 0; ///< BattleScore::enemyDamageReduce
	float our = 0;   ///< BattleScore::ourDamageReduce
};

struct Order
{
	std::array<int8_t, MAX_UNITS> unit{};
	int size = 0;

	void add(int u)
	{
		unit[size++] = static_cast<int8_t>(u);
	}
};

struct MoveTarget
{
	float score = INEFFECTIVE_SCORE;
	BattleHex position;
	int turnsToReach = 1;
};

class Model
{
public:
	Model(const SimState & state, int unit, int simulationTurns);
	SimAction choose();
	void describe(std::string & into);

private:
	const SimState & s;
	const int me;
	const int n;
	const int simulationTurns;
	const SimUnit & self;
	float negativeMultiplier = 1;

	// --- the state the decision starts from
	std::array<int64_t, MAX_UNITS> health{};
	std::array<int, MAX_UNITS> speed{};
	std::array<bool, MAX_UNITS> hasShots{};      ///< Unit::canShoot
	std::array<bool, MAX_UNITS> shootsNow{};     ///< battleCanShoot: has shots and no enemy next to it
	std::array<uint32_t, MAX_UNITS> adjacentUnits{};
	std::array<uint32_t, MAX_UNITS> cluster{};   ///< getAdjacentUnits: the units of its side it stands shoulder to shoulder with, itself included
	std::array<std::array<float, MAX_UNITS>, MAX_UNITS> perCreature{}; ///< DamageCache
	uint32_t alive = 0;
	uint32_t ownMask = 0;
	uint32_t foeMask = 0;

	// --- turn order of this round and the next: [0] as it stands, [1] if the unit waits
	std::array<std::array<Order, ORDER_TURNS>, 2> order;
	std::array<uint32_t, ORDER_TURNS> queueMask{};

	// --- reachability
	std::array<Distances, MAX_UNITS> distances;
	uint32_t haveDistances = 0;
	Predecessors myPredecessors;
	struct ReachRow
	{
		std::bitset<FIELD_SIZE> valid;
		std::array<std::array<uint32_t, FIELD_SIZE>, ORDER_TURNS> queue;
	};
	std::array<ReachRow, REACH_ROWS> reachRows;

	// --- PotentialTargets
	std::vector<Attack> attacks;
	Order unreachable;
	uint32_t unreachableMask = 0;
	MoveTarget lastMove;
	uint32_t lastExchangeUnits = 0; ///< for the trace

	uint32_t sideMask(int unit) const { return s.units[unit].side == self.side ? ownMask : foeMask; }
	int64_t countOf(int unit, int64_t healthLeft) const
	{
		const int64_t maxHP = s.units[unit].maxHP;
		return healthLeft > 0 ? (healthLeft + maxHP - 1) / maxHP : 0;
	}
	bool shotIsFar(int attacker, int target) const;
	int64_t estimate(int attacker, int defender) const;
	bool covers(int unit, const BattleHex & hex) const;
	void buildOrders();
	void take(uint32_t mask, bool slowestFirst, bool nextRound, BattleSide & lastMoved, Order & into) const;
	void walk(const Occupants & occupant, int unit, const BattleHex & start, Distances & dist, Predecessors * pred, const Distances * bypass) const;
	const Distances & distancesOf(int unit);
	void reach(int turn, const BattleHex & hex, uint32_t & first, uint32_t & second);
	float damageReduce(int attacker, int defender, int64_t dealt, int64_t defenderCount, int64_t defenderFirstHP) const;
	/// against a unit as it stands at the start (the game does not tidy up a stack whose creatures lost
	/// part of their health to ageing: the first one may still have more than a creature can have now)
	float reduceBase(int attacker, int defender, int64_t dealt) const
	{
		return damageReduce(attacker, defender, dealt, s.units[defender].count, s.units[defender].firstHP);
	}
	Attack evaluate(int target, const BattleHex & from, bool shooting, int chargeDistance) const;
	uint32_t attackedUnits(int attacker, const BattleHex & attackerPos, int defender, const BattleHex & defenderPos) const;
	void gatherTargets();
	Score exchange(const Attack & ap, int turn, int variant, uint32_t additional);
	float scoreValue(const Score & score) const { return score.enemy - score.our * negativeMultiplier; }
	void findBestTarget(int & best, bool & wait, float & score);
	bool canBeHitThisTurn();
	HexList attackableHexes(int defender, int attacker) const;
	int32_t distToNearestNeighbour(const Distances & dist, bool attackerDoubleWide, int defender) const;
	MoveTarget findMove();
	bool blocksOwn(const BattleHex & position);
	SimAction moveOrAttack(const BattleHex & hex) const;
	SimAction goTowards(const BattleHex & wanted);
};

Model::Model(const SimState & state, int unit, int turns)
	: s(state), me(unit), n(static_cast<int>(state.units.size())), simulationTurns(turns), self(state.units[unit])
{
	// BattleAI.cpp, getStrengthRatio: what we lose counts less the weaker we are
	int64_t ours = 0;
	int64_t theirs = 0;
	for(int i = 0; i < n; ++i)
	{
		const auto & u = s.units[i];
		(u.side == self.side ? ours : theirs) += static_cast<int64_t>(u.count) * static_cast<int64_t>(u.value);
		if(!u.alive())
			continue;
		alive |= bit(i);
		(u.side == self.side ? ownMask : foeMask) |= bit(i);
		health[i] = u.totalHP();
		speed[i] = u.canMove ? u.speed : 0;
		hasShots[i] = u.shooter && u.shots > 0;
		shootsNow[i] = s.canShoot(i);
	}
	const float ratio = theirs == 0 ? 1.0f : static_cast<float>(ours) / theirs;
	negativeMultiplier = ratio >= 1 ? 1 : ratio * ratio;

	for(int i = 0; i < n; ++i)
	{
		const auto & u = s.units[i];
		if(!u.alive())
			continue;
		for(const auto & hex : battle::Unit::getSurroundingHexes(u.pos, u.doubleWide, u.side))
		{
			const int occ = s.occupant[hex.toInt()];
			if(occ >= 0 && occ != i)
				adjacentUnits[i] |= bit(occ);
		}
	}
	for(int i = 0; i < n; ++i)
	{
		if(!(alive >> i & 1))
			continue;
		uint32_t members = bit(i);
		uint32_t frontier = bit(i);
		while(frontier)
		{
			const int u = lowest(frontier);
			frontier &= frontier - 1;
			const uint32_t more = adjacentUnits[u] & sideMask(i) & ~members;
			members |= more;
			frontier |= more;
		}
		cluster[i] = members;
	}

	// DamageCache::buildDamageCache: the damage of every unit against every enemy, per creature -
	// by shot if it can shoot from where it stands now, with the range penalty of this moment
	for(int a = 0; a < n; ++a)
	{
		if(!(alive >> a & 1))
			continue;
		const int count = std::max(1, s.units[a].count);
		// (against its own side as well: blows that reach several hexes; the original fills these in when asked)
		for(uint32_t rest = alive & ~bit(a); rest; rest &= rest - 1)
		{
			const int d = lowest(rest);
			perCreature[a][d] = static_cast<float>(estimate(a, d)) / count;
		}
	}

	buildOrders();
}

bool Model::shotIsFar(int attacker, int target) const
{
	// no penalty if any hex of the target is within range (battleHasDistancePenalty)
	const auto & t = s.units[target];
	for(const auto & hex : battle::Unit::getHexes(t.pos, t.doubleWide, t.side))
		if(BattleHex::getDistance(s.units[attacker].pos, hex) <= GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE)
			return false;
	return true;
}

/// battleEstimateDamage(attacker, defender, 0), averaged the way BattleAI does: by shot if the
/// attacker can shoot at this enemy from where it stands, with the range penalty of this moment
int64_t Model::estimate(int attacker, int defender) const
{
	const auto & a = s.units[attacker];
	double per = 0;
	if(a.side == s.units[defender].side)
		per = s.damage->ownSide(attacker, defender);
	else if(shootsNow[attacker])
		per = s.damage->ranged(attacker, defender, shotIsFar(attacker, defender));
	else
		per = s.damage->melee(attacker, defender);
	return static_cast<int64_t>(per * a.count + 1e-6);
}

bool Model::covers(int unit, const BattleHex & hex) const
{
	const auto & u = s.units[unit];
	return u.pos == hex || (u.doubleWide && battle::Unit::occupiedHex(u.pos, true, u.side) == hex);
}

/// CBattleInfoCallback::battleGetTurnOrder with takeOneUnit: the fastest first, among units that
/// waited the slowest first; equally fast units of both sides take turns, the lower slot first
void Model::take(uint32_t mask, bool slowestFirst, bool nextRound, BattleSide & lastMoved, Order & into) const
{
	auto initiativeOf = [&](int u)
	{
		return nextRound ? s.units[u].initiativeNext : s.units[u].initiative;
	};
	while(mask)
	{
		int bestInitiative = 0;
		bool any = false;
		for(uint32_t rest = mask; rest; rest &= rest - 1)
		{
			const int initiative = initiativeOf(lowest(rest));
			if(!any || (slowestFirst ? initiative < bestInitiative : initiative > bestInitiative))
				bestInitiative = initiative;
			any = true;
		}
		const BattleSide preferred = lastMoved == BattleSide::NONE ? BattleSide::ATTACKER
			: (lastMoved == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER);
		int chosen = -1;
		int fallback = -1;
		for(uint32_t rest = mask; rest; rest &= rest - 1)
		{
			const int u = lowest(rest);
			if(initiativeOf(u) != bestInitiative)
				continue;
			if(s.units[u].side == preferred)
			{
				chosen = u;
				break;
			}
			if(fallback < 0)
				fallback = u;
		}
		if(chosen < 0)
			chosen = fallback;
		into.add(chosen);
		mask &= ~bit(chosen);
		lastMoved = s.units[chosen].side;
	}
}

void Model::buildOrders()
{
	uint32_t normal = 0;
	uint32_t waiting = 0;
	uint32_t waitingMorale = 0; // those that waited on an extra turn from morale come before the others that waited
	uint32_t next = 0;
	for(uint32_t rest = alive; rest; rest &= rest - 1)
	{
		const int i = lowest(rest);
		const auto & u = s.units[i];
		if(u.activeNext)
			next |= bit(i); // (blindness and the like may have run out by then)
		if(!u.active)
			continue;
		// (a unit that never acts in the simulator - a first aid tent - still has its turn in the game)
		if(i != me && (!u.acted || (!u.canMove && !u.shooter)))
			(u.waited ? (u.hadMorale ? waitingMorale : waiting) : normal) |= bit(i);
	}
	next |= bit(me);
	queueMask[0] = normal | waiting | waitingMorale | bit(me);
	queueMask[1] = next;

	for(int variant = 0; variant < 2; ++variant)
	{
		BattleSide lastMoved = BattleSide::NONE;
		Order & now = order[variant][0];
		if(variant == 0)
		{
			// the active unit that has not acted yet leads the queue, whatever its speed
			now.add(me);
			lastMoved = self.side;
			take(normal, false, false, lastMoved, now);
			take(waitingMorale, true, false, lastMoved, now);
			take(waiting, true, false, lastMoved, now);
		}
		else
		{
			// HypotheticBattle::makeWait: nobody is active, the unit queues up with those that waited
			take(normal, false, false, lastMoved, now);
			take(waitingMorale | (self.hadMorale ? bit(me) : 0), true, false, lastMoved, now);
			take(waiting | (self.hadMorale ? 0 : bit(me)), true, false, lastMoved, now);
		}
		if(lastMoved == BattleSide::NONE)
			lastMoved = BattleSide::ATTACKER;
		// the next round: by the speed the units will have then (a spell may run out)
		take(next, false, true, lastMoved, order[variant][1]);
	}
}

/// CBattleInfoCallback::makeBFS / getFlyingReachability: the distance of every hex for the unit -
/// not limited by its speed. bypass: hexes of enemies it could cut its way through, with the price
/// of doing so (getReachabilityWithEnemyBypass).
void Model::walk(const Occupants & occupant, int unit, const BattleHex & start, Distances & dist, Predecessors * pred, const Distances * bypass) const
{
	const auto & u = s.units[unit];
	dist.fill(INFINITE_DIST);
	if(pred)
		pred->fill(-1);
	if(!start.isValid())
		return;

	auto tileFree = [&](int h)
	{
		const int occ = occupant[h];
		return occ == -1 || occ == unit || (bypass && occ >= 0 && (*bypass)[h] >= 0);
	};
	std::array<bool, FIELD_SIZE> accessible;
	for(int h = 0; h < FIELD_SIZE; ++h)
	{
		bool ok = tileFree(h);
		if(ok && u.doubleWide)
		{
			const BattleHex other = battle::Unit::occupiedHex(BattleHex(h), true, u.side);
			ok = other.isValid() && tileFree(other.toInt());
		}
		accessible[h] = ok;
	}

	if(u.flying)
	{
		for(int h = 0; h < FIELD_SIZE; ++h)
		{
			if(!accessible[h])
				continue;
			dist[h] = BattleHex::getDistance(start, BattleHex(h));
			if(pred)
				(*pred)[h] = static_cast<int16_t>(start.toInt());
		}
		return;
	}

	std::array<int16_t, 2048> queue;
	int head = 0;
	int tail = 0;
	queue[tail++] = static_cast<int16_t>(start.toInt());
	dist[start.toInt()] = 0;
	while(head < tail)
	{
		const int cur = queue[head++];
		const int cost = dist[cur] + 1;
		const BattleHex curHex(cur);
		for(const auto & neighbour : curHex.getNeighbouringTiles())
		{
			const int to = neighbour.toInt();
			int extra = 0;
			if(bypass && (*bypass)[to] >= 0)
				extra = (*bypass)[to];
			if(accessible[to] && cost + extra < dist[to])
			{
				if(tail < static_cast<int>(queue.size()))
					queue[tail++] = static_cast<int16_t>(to);
				dist[to] = cost + extra;
				if(pred)
					(*pred)[to] = static_cast<int16_t>(cur);
			}
		}
	}
}

const Distances & Model::distancesOf(int unit)
{
	if(!(haveDistances >> unit & 1))
	{
		walk(s.occupant, unit, s.units[unit].pos, distances[unit], unit == me ? &myPredecessors : nullptr, nullptr);
		haveDistances |= bit(unit);
	}
	return distances[unit];
}

/// ReachabilityMapCache::computeOneTurnReachableUnits: who gets to this hex - of those still to act
/// in this round within `turn + 1` moves (first), of everybody in the next round within `turn + 2`
/// (second). Whoever can shoot reaches everything.
void Model::reach(int turn, const BattleHex & hex, uint32_t & first, uint32_t & second)
{
	const int h = hex.toInt();
	ReachRow * row = turn < REACH_ROWS ? &reachRows[turn] : nullptr;
	if(row && row->valid[h])
	{
		first = row->queue[0][h];
		second = row->queue[1][h];
		return;
	}
	std::array<uint32_t, ORDER_TURNS> result = {0, 0};
	const int occ = s.occupant[h];
	for(int q = 0; q < ORDER_TURNS; ++q)
	{
		for(uint32_t rest = queueMask[q]; rest; rest &= rest - 1)
		{
			const int u = lowest(rest);
			if(shootsNow[u])
			{
				result[q] |= bit(u);
				continue;
			}
			const int radius = speed[u] * (turn + q + 1);
			const Distances & dist = distancesOf(u);
			bool reachable = dist[h] <= radius;
			if(!reachable && occ >= 0 && occ != u && s.units[occ].side != s.units[u].side)
			{
				// an enemy stands there: reaching a hex next to it is enough
				for(const auto & neighbour : hex.getNeighbouringTiles())
				{
					if(dist[neighbour.toInt()] <= radius)
					{
						reachable = true;
						break;
					}
				}
			}
			if(reachable)
				result[q] |= bit(u);
		}
	}
	if(row)
	{
		row->queue[0][h] = result[0];
		row->queue[1][h] = result[1];
		row->valid.set(h);
	}
	first = result[0];
	second = result[1];
}

/// AttackPossibility::calculateDamageReduce: the damage the victim deals to the attacker, for the
/// share of the victim that the blow removes - half counted by creatures killed, half by health taken
float Model::damageReduce(int attacker, int defender, int64_t dealt, int64_t defenderCount, int64_t defenderFirstHP) const
{
	if(defenderCount <= 0 || dealt <= 0)
		return 0;
	const uint64_t maxHealth = static_cast<uint64_t>(std::max(1, s.units[defender].maxHP));
	const uint64_t count = static_cast<uint64_t>(defenderCount);
	const uint64_t firstHP = static_cast<uint64_t>(defenderFirstHP);
	const uint64_t damage = std::min<uint64_t>(dealt, firstHP + (count - 1) * maxHealth);

	const auto before = static_cast<int64_t>(perCreature[defender][attacker] * count);
	const uint64_t killed = damage / maxHealth + (damage % maxHealth >= firstHP ? 1 : 0);
	const double perEnemy = before / static_cast<double>(count);
	const uint64_t exceeding = damage % maxHealth;
	float hpValue = static_cast<float>(damage / maxHealth);
	if(firstHP >= exceeding)
	{
		hpValue += hpFunction(firstHP, firstHP - exceeding, maxHealth);
	}
	else
	{
		hpValue += hpFunction(firstHP, 0, maxHealth);
		hpValue += hpFunction(maxHealth, maxHealth + firstHP - exceeding, maxHealth);
	}
	return static_cast<float>(perEnemy * (killed * 0.5f + hpValue * 0.5f));
}

/// AttackPossibility::evaluate for the main target (attacks that reach several hexes are counted
/// for that target only)
Attack Model::evaluate(int target, const BattleHex & from, bool shooting, int chargeDistance) const
{
	Attack ap;
	ap.target = target;
	ap.from = from;
	ap.shooting = shooting;
	ap.attackerHealth = health[me];
	ap.defenderHealth = health[target];
	if(!shooting && !s.adjacent(me, from, target))
		return ap; // BattleAI: no hex to strike, an attack without states

	ap.real = true;
	const auto & t = s.units[target];
	const double per = shooting ? s.damage->ranged(me, target, shotIsFar(me, target))
		: s.damage->melee(me, target) + s.damage->chargeStep(me, target) * chargeDistance;
	const double exact = per * self.count;
	auto attackDamage = static_cast<int64_t>(exact + 1e-6);
	// the damage of a stack as the game computes it: per creature times the stack, rounded down, at least 1
	auto stackDamage = [](double perCreature, int64_t count)
	{
		return count > 0 ? std::max<int64_t>(1, static_cast<int64_t>(perCreature * count + 0.002)) : 0;
	};
	const double lowBlow = shooting ? 0 : s.damage->meleeLow(me, target) + s.damage->chargeLow(me, target) * chargeDistance;
	const double highBlow = shooting ? 0 : s.damage->meleeHigh(me, target) + s.damage->chargeHigh(me, target) * chargeDistance;
	if(!shooting && chargeDistance > 0 && s.damage->chargeStep(me, target) > 0)
		attackDamage = (stackDamage(lowBlow, self.count) + stackDamage(highBlow, self.count)) / 2; // jousting: exact for the hexes walked

	// battleEstimateDamage: the retaliation is the middle between the weakest answer of what the
	// hardest blow leaves and the strongest answer of what the lightest blow leaves
	int64_t retaliation = 0;
	const bool retaliates = !shooting && t.retaliationsLeft > 0 && !self.blocksRetaliation;
	if(retaliates)
	{
		const auto light = std::min<int64_t>(health[target], stackDamage(lowBlow, self.count));
		const auto hard = std::min<int64_t>(health[target], stackDamage(highBlow, self.count));
		const auto least = stackDamage(s.damage->meleeLow(target, me), countOf(target, health[target] - hard));
		const auto most = stackDamage(s.damage->meleeHigh(target, me), countOf(target, health[target] - light));
		retaliation = (least + most) / 2;
	}

	// Who is hit besides the target, and whom the retaliation hits besides us: as BattleAI sees it,
	// friend and foe alike (getAttackedBattleUnits)
	uint32_t struck = 0;
	uint32_t answered = 0;
	if(!shooting)
	{
		struck = attackedUnits(me, from, target, t.pos) & ~bit(me);
		if(retaliates)
			answered = attackedUnits(target, t.pos, me, from) & ~bit(me);
	}
	Order victims; // in the order of the battle's units; the target last unless the blow sweeps it anyway
	for(uint32_t rest = struck; rest; rest &= rest - 1)
		victims.add(lowest(rest));
	if(!(struck >> target & 1))
		victims.add(target);

	std::array<int64_t, MAX_UNITS> after = health;
	int retaliationsLeft = t.retaliationsLeft;
	const int totalAttacks = shooting ? self.rangedAttacks : self.meleeAttacks;
	for(int i = 0; i < totalAttacks; ++i)
	{
		if(ap.attackerHealth <= 0 || after[target] <= 0)
			break;
		for(int k = 0; k < victims.size; ++k)
		{
			const int u = victims.unit[k];
			// the original deals everybody the damage estimated for the target, and measures every
			// blow against the victim as it stood before the attack
			const int64_t dealt = std::min(attackDamage, after[u]);
			float defenderReduce = reduceBase(me, u, dealt);
			float attackerReduce = 0;
			if(u == target && retaliates && retaliationsLeft > 0)
			{
				const int64_t received = std::min(retaliation, ap.attackerHealth);
				attackerReduce = reduceBase(target, me, received);
				ap.attackerHealth -= received;
				for(uint32_t rest = answered; rest; rest &= rest - 1)
				{
					// the retaliation sweeps others as well
					const int r = lowest(rest);
					const auto blow = std::min(estimate(target, r), health[r]);
					if(s.units[r].side == t.side)
						defenderReduce += reduceBase(target, r, blow);
					else
						ap.collateralReduce += reduceBase(target, r, blow);
					after[r] -= std::min(blow, after[r]);
				}
				--retaliationsLeft;
			}
			if(s.units[u].side != self.side)
				ap.defenderReduce += defenderReduce;
			else
				ap.collateralReduce += defenderReduce;
			if(u == target)
				ap.attackerReduce += attackerReduce;
			after[u] -= dealt;
			if(u == target)
				ap.defenderDead = after[target] <= 0;
		}
	}
	ap.defenderHealth = after[target];
	ap.defenderRetaliates = !ap.defenderDead && retaliationsLeft > 0;
	for(uint32_t rest = (struck | answered) & ~bit(target); rest; rest &= rest - 1)
	{
		const int u = lowest(rest);
		if(after[u] != health[u] && ap.othersCount < static_cast<int>(ap.others.size()))
		{
			ap.others[ap.othersCount] = static_cast<int8_t>(u);
			ap.othersHealth[ap.othersCount++] = after[u];
		}
	}

	// evaluateBlockedShootersDmg: shooters next to that hex have to fight hand to hand from then on
	if(!shooting)
	{
		uint32_t checked = 0;
		for(const auto & tile : battle::Unit::getSurroundingHexes(from, self.doubleWide, self.side))
		{
			const int st = s.occupant[tile.toInt()];
			if(st < 0 || s.units[st].side == self.side || (checked >> st & 1) || !shootsNow[st])
				continue;
			checked |= bit(st);
			const int count = s.units[st].count;
			const auto ranged = static_cast<int64_t>(s.damage->ranged(st, me, false) * count + 1e-6);
			const auto melee = static_cast<int64_t>(s.damage->melee(st, me) * count + 1e-6);
			const auto cached = static_cast<int64_t>(perCreature[st][me] * count);
			// As the original computes it - in unsigned numbers. A shooter that hits harder hand to
			// hand than by shot (a hero with Offence and without Archery, no melee penalty) makes
			// the gain negative, and that turns into a number beyond all others: BattleAI then
			// prefers the hexes next to such a shooter to everything else. The guards really do that.
			const int64_t gain = ranged - melee + 1;
			const uint64_t share = static_cast<uint64_t>(gain * cached) / std::max<uint64_t>(1, static_cast<uint64_t>(ranged));
			ap.blockedShooters = static_cast<int64_t>(static_cast<uint64_t>(ap.blockedShooters) + share);
		}
	}
	return ap;
}

/// CBattleInfoCallback::getAttackedBattleUnits for a melee attack, as BattleAI calls it: every unit
/// that stands on a hex the attack reaches besides the target's - around the attacker (hydras, magic
/// elementals), on the hexes of further heads and of breath. BattleAI makes no difference between
/// friend and foe there, and measures the attacker's own hexes by where it stands now.
uint32_t Model::attackedUnits(int attacker, const BattleHex & attackerPos, int defender, const BattleHex & defenderPos) const
{
	const auto & a = s.units[attacker];
	const auto & d = s.units[defender];
	if(!a.attacksAllAdjacent && a.reachCount == 0)
		return 0;
	uint32_t result = 0;
	auto mark = [&](const BattleHex & hex)
	{
		if(!hex.isValid())
			return;
		const int occ = s.occupant[hex.toInt()];
		if(occ >= 0)
			result |= bit(occ);
	};
	if(a.attacksAllAdjacent)
		for(const auto & hex : battle::Unit::getSurroundingHexes(attackerPos, a.doubleWide, a.side))
			mark(hex);
	if(a.reachCount == 0)
		return result;

	BattleHex origin = attackerPos;
	// a wide attacker whose head does not touch the enemy strikes from its other hex
	if(a.doubleWide && !battle::Unit::getSurroundingHexes(defenderPos, d.doubleWide, d.side).contains(origin))
		origin = battle::Unit::occupiedHex(origin, true, a.side);
	if(!origin.isValid())
		return result;
	auto direction = BattleHex::mutualPosition(origin, defenderPos);
	if(d.doubleWide)
	{
		// a wide defender is hit on its far hex whenever that one is in reach
		const BattleHex tail = battle::Unit::occupiedHex(defenderPos, true, d.side);
		if(tail.isValid() && BattleHex::mutualPosition(origin, tail) != BattleHex::NONE)
			direction = BattleHex::mutualPosition(origin, tail);
	}
	if(direction == BattleHex::NONE)
		return result;
	for(int i = 0; i < a.reachCount; ++i)
	{
		std::array<BattleHex::EDir, 5> path{};
		int steps = 0;
		for(int left = a.reachPath[i]; left != 0 && steps < 5; left /= 10)
			path[steps++] = static_cast<BattleHex::EDir>((direction + left % 10 - 1) % 6);
		if(steps == 0)
			continue;
		BattleHex hex = origin;
		try
		{
			if(a.doubleWide && covers(attacker, hex.cloneInDirection(path[0])))
				hex.moveInDirection(direction);
			for(int k = 0; k < steps; ++k)
				hex.moveInDirection(path[k]);
		}
		catch(const std::out_of_range &)
		{
			continue; // off the field
		}
		if(hex.isValid() && !covers(attacker, hex))
			mark(hex);
	}
	return result;
}

/// PotentialTargets: every attack the unit can make in this turn, the best first
void Model::gatherTargets()
{
	const Distances & dist = distancesOf(me);
	attacks.reserve(48);
	for(uint32_t rest = sideMask(me) ^ (ownMask | foeMask); rest; rest &= rest - 1)
	{
		const int defender = lowest(rest);
		if(shootsNow[me])
		{
			attacks.push_back(evaluate(defender, BattleHex(), true, 0));
			continue;
		}
		bool any = false;
		for(int h = 0; h < FIELD_SIZE; ++h)
		{
			if(dist[h] > speed[me] || !s.adjacent(me, BattleHex(h), defender))
				continue;
			attacks.push_back(evaluate(defender, BattleHex(h), false, dist[h]));
			any = true;
		}
		if(!any)
		{
			unreachable.add(defender);
			unreachableMask |= bit(defender);
		}
	}
	std::sort(attacks.begin(), attacks.end(), [](const Attack & left, const Attack & right)
	{
		return left.value() > right.value();
	});
}

/// BattleExchangeEvaluator::calculateExchange with getExchangeUnits: everybody who gets to the
/// place of the attack trades blows for `simulationTurns` turns; what each side loses in power to
/// do damage is the score. turn: how many turns it takes until the attack happens.
Score Model::exchange(const Attack & ap, int turn, int variant, uint32_t additional)
{
	Score score;
	const auto & defender = s.units[ap.target];
	const uint32_t friends = ownMask; // "our" units in BattleAI's sense: the side of the attacker
	const uint32_t foes = foeMask;

	// --- getExchangeUnits
	uint32_t all = additional;
	uint32_t reachingAttacker = 0;
	uint32_t first = 0;
	uint32_t second = 0;
	for(const auto & hex : battle::Unit::getSurroundingHexes(defender.pos, defender.doubleWide, defender.side))
	{
		reach(turn, hex, first, second);
		all |= first | second;
	}
	if(!ap.shooting && ap.from.isValid())
	{
		reach(turn, ap.from, first, second);
		all |= first | second;
	}
	for(const auto & hex : battle::Unit::getHexes(self.pos, self.doubleWide, self.side))
	{
		reach(turn, hex, first, second);
		const uint32_t enemies = (first | second) & foes;
		all |= enemies;
		reachingAttacker |= enemies;
	}
	// enemies the unit cannot attack now that stand shoulder to shoulder with one of them
	for(uint32_t rest = all; rest; rest &= rest - 1)
		all |= cluster[lowest(rest)] & unreachableMask;
	all |= bit(me);
	lastExchangeUnits = all;
	if(std::popcount(all) < 2)
		return score;

	// shooters that nobody of the exchange can get at: not next to the target
	uint32_t farShooters = 0;
	for(uint32_t rest = all; rest; rest &= rest - 1)
	{
		const int u = lowest(rest);
		if(hasShots[u] && !(additional >> u & 1) && !(adjacentUnits[u] >> ap.target & 1))
			farShooters |= bit(u);
	}

	const auto & turnOrder = order[variant];
	Order ourQueue;
	Order enemyQueue;
	enemyQueue.add(ap.target);
	int leader = -1; // the first to act in the exchange
	for(int t = 0; t < ORDER_TURNS; ++t)
	{
		for(int k = 0; k < turnOrder[t].size; ++k)
		{
			const int u = turnOrder[t].unit[k];
			if(!(all >> u & 1))
				continue;
			if(t == 0 && leader < 0)
				leader = u;
			Order & queue = (friends >> u & 1) ? ourQueue : enemyQueue;
			bool known = false;
			for(int i = 0; i < queue.size; ++i)
				known = known || queue.unit[i] == u;
			if(!known)
				queue.add(u);
		}
	}

	// --- the exchange
	std::array<int64_t, MAX_UNITS> left = health;
	std::array<int, MAX_UNITS> retaliations{};
	std::array<int, MAX_UNITS> shots{};
	for(uint32_t rest = alive; rest; rest &= rest - 1)
	{
		const int u = lowest(rest);
		retaliations[u] = s.units[u].retaliationsLeft;
		shots[u] = s.units[u].shots;
	}
	uint32_t standing = alive;
	uint32_t touched = 0; // whose health has changed in the exchange (the game counts such a stack afresh)
	uint32_t blockedShooters = 0;
	bool canUseAp = true;
	auto countNow = [&](int unit) -> int64_t
	{
		return (touched >> unit & 1) ? countOf(unit, left[unit]) : s.units[unit].count;
	};
	auto reduce = [&](int attacker, int target, int64_t dealt)
	{
		if(!(touched >> target & 1))
			return reduceBase(attacker, target, dealt);
		const int64_t maxHP = std::max(1, s.units[target].maxHP);
		const int64_t count = countOf(target, left[target]);
		return damageReduce(attacker, target, dealt, count, left[target] - (count - 1) * maxHP);
	};

	auto removeDead = [&](Order & queue)
	{
		int kept = 0;
		for(int i = 0; i < queue.size; ++i)
			if(left[queue.unit[i]] > 0)
				queue.unit[kept++] = queue.unit[i];
		queue.size = kept;
	};
	auto hurt = [&](int unit, int64_t amount)
	{
		left[unit] -= std::min(amount, left[unit]);
		touched |= bit(unit);
		if(left[unit] <= 0)
			standing &= ~bit(unit);
	};
	// BattleExchangeVariant::trackAttack for an attack out of the damage cache
	auto strike = [&](int attacker, int target, bool shooting, bool isOur)
	{
		const auto damage = static_cast<int64_t>(perCreature[attacker][target] * countNow(attacker));
		const float gained = reduce(attacker, target, damage);
		(isOur ? score.enemy : score.our) += gained;
		hurt(target, damage);
		if(shooting)
			--shots[attacker];
		if(left[target] > 0 && retaliations[target] > 0 && !s.units[attacker].blocksRetaliation && !shooting)
		{
			const auto answer = static_cast<int64_t>(perCreature[target][attacker] * countNow(target));
			const float lost = reduce(target, attacker, answer);
			(isOur ? score.our : score.enemy) += lost;
			hurt(attacker, answer);
			--retaliations[target];
		}
	};

	const int totalTurns = simulationTurns >= turn + ORDER_TURNS ? simulationTurns : turn + ORDER_TURNS;
	for(int exchangeTurn = 0; exchangeTurn < simulationTurns; ++exchangeTurn)
	{
		const bool movingTurn = exchangeTurn < turn;
		const Order & queue = turnOrder[std::min(exchangeTurn, ORDER_TURNS - 1)];
		for(int k = 0; k < queue.size; ++k)
		{
			const int attacker = queue.unit[k];
			if(!(all >> attacker & 1))
				continue;
			const auto & a = s.units[attacker];
			const bool isOur = friends >> attacker & 1;
			Order & attackerQueue = isOur ? ourQueue : enemyQueue;
			Order & oppositeQueue = isOur ? enemyQueue : ourQueue;
			const bool canShoot = a.shooter && shots[attacker] > 0 && (a.freeShooting || !(adjacentUnits[attacker] & standing & (isOur ? foes : friends)));
			const bool shooting = canShoot && !(blockedShooters >> attacker & 1);

			if(left[attacker] <= 0)
				continue;
			if(movingTurn && !shooting && !(reachingAttacker >> attacker & 1))
				continue; // still on its way

			int target = ap.target;
			if(!isOur || left[target] <= 0)
			{
				// the most rewarding target among those of the exchange; the attacker itself is
				// spared in the first turn by enemies that do not get to it
				const bool spareAttacker = !isOur && exchangeTurn == 0 && leader != me && !(reachingAttacker >> attacker & 1);
				const int64_t count = countNow(attacker);
				auto estimate = [&](int u)
				{
					return reduce(attacker, u, static_cast<int64_t>(perCreature[attacker][u] * count));
				};
				int best = -1;
				float bestValue = 0;
				for(int i = 0; i < oppositeQueue.size; ++i)
				{
					const int u = oppositeQueue.unit[i];
					if((farShooters >> u & 1) || (spareAttacker && u == me))
						continue;
					const float value = estimate(u);
					if(best < 0 || value > bestValue)
					{
						best = u;
						bestValue = value;
					}
				}
				if(best < 0)
				{
					// nobody of the exchange: whoever it gets to within a turn
					for(uint32_t rest = standing & (isOur ? foes : friends); rest; rest &= rest - 1)
					{
						const int u = lowest(rest);
						reach(0, s.units[u].pos, first, second);
						if(!((first | second) >> attacker & 1))
							continue;
						const float value = estimate(u);
						if(best < 0 || value > bestValue)
						{
							best = u;
							bestValue = value;
						}
					}
					if(best < 0)
						continue;
				}
				target = best;
			}

			if(canUseAp && attacker == me && target == ap.target)
			{
				// BattleExchangeVariant::trackAttack for the attack itself
				if(ap.real)
				{
					score.our += ap.attackerReduce + ap.collateralReduce;
					score.enemy += ap.defenderReduce + ap.blockedShooters;
					const bool couldRetaliate = retaliations[target] > 0;
					if(left[target] > ap.defenderHealth)
						hurt(target, left[target] - ap.defenderHealth);
					for(int i = 0; i < ap.othersCount; ++i)
						if(left[ap.others[i]] > ap.othersHealth[i])
							hurt(ap.others[i], left[ap.others[i]] - ap.othersHealth[i]);
					if(left[me] > ap.attackerHealth)
						hurt(me, left[me] - ap.attackerHealth);
					if(ap.shooting)
						--shots[me];
					if(left[target] > 0 && couldRetaliate && !ap.defenderRetaliates)
						--retaliations[target];
				}
			}
			else
			{
				const int totalAttacks = shooting ? a.rangedAttacks : a.meleeAttacks;
				for(int i = 0; i < totalAttacks; ++i)
				{
					strike(attacker, target, shooting, isOur);
					if(left[attacker] <= 0 || left[target] <= 0)
						break;
				}
			}

			if(!shooting)
				blockedShooters |= bit(target);
			canUseAp = false;
			removeDead(attackerQueue);
			removeDead(oppositeQueue);
		}
		// HypotheticBattle::nextRound: everybody can retaliate again
		for(uint32_t rest = standing; rest; rest &= rest - 1)
		{
			const int u = lowest(rest);
			retaliations[u] = s.units[u].retaliationsPerRound;
		}
	}

	if(simulationTurns < totalTurns)
	{
		const float scaling = simulationTurns / static_cast<float>(totalTurns);
		score.enemy *= scaling;
		score.our *= scaling;
	}
	if(turn > 0)
		score.enemy *= static_cast<float>(1 - std::min(0.2, 0.05 * turn));
	return score;
}

/// BattleExchangeEvaluator::canBeHitThisTurn - which in fact asks: is there an enemy shooter, or
/// an enemy that gets next to the unit within two turns
bool Model::canBeHitThisTurn()
{
	uint32_t first = 0;
	uint32_t second = 0;
	for(const auto & hex : battle::Unit::getSurroundingHexes(self.pos, self.doubleWide, self.side))
	{
		reach(0, hex, first, second);
		if((first | second) & foeMask)
			return true;
	}
	return false;
}

/// BattleExchangeEvaluator::findBestTarget: the attack with the best exchange - now, or after
/// waiting for the others to move
void Model::findBestTarget(int & best, bool & wait, float & score)
{
	best = 0;
	wait = false;
	score = INEFFECTIVE_SCORE;

	if(!self.waited && !self.hadMorale)
	{
		for(int i = 0; i < static_cast<int>(attacks.size()); ++i)
		{
			const float value = scoreValue(exchange(attacks[i], 0, 1, 0));
			if(value > score)
			{
				score = value;
				best = i;
				wait = true;
			}
		}
	}

	// a shooter that would shoot with the range penalty and has nothing to fear lets them come closer
	const Attack & chosen = attacks[best];
	if(chosen.shooting && !chosen.defenderDead && !self.waited && !self.noDistancePenalty && shotIsFar(me, chosen.target))
	{
		if(!canBeHitThisTurn())
			return;
	}

	for(int i = 0; i < static_cast<int>(attacks.size()); ++i)
	{
		const float value = scoreValue(exchange(attacks[i], 0, 0, 0));
		const bool sameScoreButWaited = almostEqual(value, score) && wait;
		if(value > score || sameScoreButWaited)
		{
			score = value;
			best = i;
			wait = false;
		}
	}
}

/// battle::Unit::getAttackableHexes: where `attacker` can stand to strike `defender`
HexList Model::attackableHexes(int defender, int attacker) const
{
	HexList result;
	const auto & d = s.units[defender];
	const auto & a = s.units[attacker];
	const auto & around = battle::Unit::getSurroundingHexes(d.pos, d.doubleWide, d.side);
	if(!a.doubleWide)
	{
		for(const auto & hex : around)
			result.add(hex);
		return result;
	}
	for(const auto & origin : around)
	{
		const BattleHex rear = battle::Unit::occupiedHex(origin, true, a.side);
		if(!covers(defender, rear) && rear.isAvailable())
			result.add(origin);
		const BattleHex head(origin.toInt() + (a.side == BattleSide::ATTACKER ? 1 : -1));
		if(head.isValid() && !covers(defender, head) && head.isAvailable())
			result.add(head);
	}
	return result;
}

/// ReachabilityInfo::distToNearestNeighbour
int32_t Model::distToNearestNeighbour(const Distances & dist, bool attackerDoubleWide, int defender) const
{
	const auto & d = s.units[defender];
	HexList hexes;
	for(const auto & hex : battle::Unit::getHexes(d.pos, d.doubleWide, d.side))
		hexes.add(hex);
	if(attackerDoubleWide)
	{
		const BattleHex base = d.doubleWide ? battle::Unit::occupiedHex(d.pos, true, d.side) : d.pos;
		for(const auto & hex : battle::Unit::getHexes(base, true, d.side))
			hexes.add(hex);
	}
	int32_t result = INFINITE_DIST;
	for(int i = 0; i < hexes.size; ++i)
	{
		const BattleHex & hex = hexes.hex[i];
		if(hex.getY() != d.pos.getY() || !hex.isAvailable())
			continue;
		for(const auto & neighbour : hex.getNeighbouringTiles())
			result = std::min(result, dist[neighbour.toInt()]);
	}
	return result;
}

/// BattleExchangeEvaluator::findMoveTowardsUnreachable: the enemy out of reach that is most worth
/// walking towards, and the hex of this turn on the way to it
MoveTarget Model::findMove()
{
	MoveTarget result;
	if(unreachable.size == 0 || speed[me] == 0)
		return result;

	// getReachabilityWithEnemyBypass: a walking unit may plan its way through enemies - at the
	// price of the turns it takes to kill them
	Distances bypassCost;
	bypassCost.fill(-1);
	Distances dist;
	Predecessors pred;
	if(!self.flying)
	{
		for(uint32_t rest = foeMask; rest; rest &= rest - 1)
		{
			const int e = lowest(rest);
			const auto & u = s.units[e];
			const auto damage = static_cast<int64_t>(perCreature[me][e] * self.count);
			const int64_t turnsToKill = std::min<int64_t>(health[e] / std::max<int64_t>(damage, 1), 100);
			for(const auto & hex : battle::Unit::getHexes(u.pos, u.doubleWide, u.side))
				if(hex.isAvailable())
					bypassCost[hex.toInt()] = static_cast<int8_t>(turnsToKill * speed[e]); // (the original keeps it in eight bits: a long fight wraps around, below zero means no way through)
		}
	}
	walk(s.occupant, me, self.pos, dist, &pred, self.flying ? nullptr : &bypassCost);

	// (the original measures the allies' distance on the map of the active unit: only their width counts)
	std::array<bool, 2> allyWidth = {false, false};
	for(uint32_t rest = ownMask & ~bit(me); rest; rest &= rest - 1)
		allyWidth[s.units[lowest(rest)].doubleWide ? 1 : 0] = true;

	for(int k = 0; k < unreachable.size; ++k)
	{
		const int enemy = unreachable.unit[k];
		const int32_t distance = distToNearestNeighbour(dist, self.doubleWide, enemy);
		if(distance >= FIELD_SIZE || distance <= speed[me])
			continue;

		float penaltyMultiplier = 1.0f;
		float closestAlly = std::numeric_limits<float>::max();
		for(int wide = 0; wide < 2; ++wide)
			if(allyWidth[wide])
				closestAlly = std::min(closestAlly, static_cast<float>(distToNearestNeighbour(dist, wide == 1, enemy)));
		if(closestAlly < distance)
			penaltyMultiplier = closestAlly / distance;

		const int turnsToReach = (distance - 1) / speed[me] + 1;
		const HexList hexes = attackableHexes(enemy, me);
		const float speedRatio = speed[enemy] > 0 ? speed[me] / static_cast<float>(speed[enemy]) : 2.0f;
		const float multiplier = (speedRatio > 1 ? 1 : speedRatio) * penaltyMultiplier;

		for(int i = 0; i < hexes.size; ++i)
		{
			const BattleHex hex = hexes.hex[i];
			if(dist[hex.toInt()] >= INFINITE_DIST)
				continue;
			Attack attack = evaluate(enemy, hex, false, 0);
			attack.blockedShooters = 0; // not counted on: it is not for sure
			Score score = exchange(attack, turnsToReach & 0xFF, 0, 0);
			score.enemy *= multiplier;
			const float value = scoreValue(score);

			if(result.score < value || ((result.turnsToReach & 0xFF) > turnsToReach && almostEqual(result.score, value)))
			{
				result.score = value;
				BattleHex enemyHex = hex;
				while(!self.flying && dist[enemyHex.toInt()] > speed[me] && pred[enemyHex.toInt()] >= 0)
				{
					enemyHex = BattleHex(pred[enemyHex.toInt()]);
					const int occ = s.occupant[enemyHex.toInt()];
					if(occ >= 0 && occ != me)
					{
						// the way leads through an enemy: is killing it first the better exchange?
						const int attackHex = pred[enemyHex.toInt()];
						if(occ != enemy && attackHex >= 0 && attackableHexes(occ, me).contains(BattleHex(attackHex)))
						{
							const Attack bypass = evaluate(occ, BattleHex(attackHex), false, 0);
							const Score bypassScore = exchange(bypass, dist[attackHex] & 0xFF, 0, cluster[enemy] | bit(occ));
							if(scoreValue(bypassScore) > result.score)
								result.score = scoreValue(bypassScore);
						}
					}
				}
				result.position = enemyHex;
				result.turnsToReach = turnsToReach;
			}
		}
	}
	return result;
}

/// BattleExchangeEvaluator::checkPositionBlocksOurStacks: standing there would take hexes - worse:
/// targets - from the stronger units of our side
bool Model::blocksOwn(const BattleHex & position)
{
	const float BLOCKING_THRESHOLD = 70;
	const float BLOCKING_OWN_ATTACK_PENALTY = 100;
	const float BLOCKING_OWN_MOVE_PENALTY = 1;

	auto minDamage = [&](int unit)
	{
		const auto & u = s.units[unit];
		return static_cast<int64_t>(shootsNow[unit] ? u.minDamageRanged : u.minDamage) * u.count;
	};
	float blockingScore = 0;
	const int64_t activeDamage = minDamage(me);

	Occupants moved = s.occupant;
	for(const auto & hex : battle::Unit::getHexes(self.pos, self.doubleWide, self.side))
		if(hex.isValid() && moved[hex.toInt()] == me)
			moved[hex.toInt()] = -1;
	for(const auto & hex : battle::Unit::getHexes(position, self.doubleWide, self.side))
		if(hex.isValid())
			moved[hex.toInt()] = static_cast<int16_t>(me);

	Distances dist;
	uint32_t first = 0;
	uint32_t second = 0;
	for(int turn = 0; turn < ORDER_TURNS; ++turn)
	{
		const Order & queue = order[0][turn];
		for(int k = 0; k < queue.size; ++k)
		{
			const int unit = queue.unit[k];
			if(unit == me || s.units[unit].side != self.side)
				continue;
			const int64_t blockedDamage = minDamage(unit);
			const float ratio = blockedDamage / static_cast<float>(blockedDamage + activeDamage + 0.01);
			walk(moved, unit, s.units[unit].pos, dist, nullptr, nullptr);
			const int unitSpeed = speed[unit];
			for(int h = 0; h < FIELD_SIZE; ++h)
			{
				bool enemyUnit = false;
				bool reachable = dist[h] <= unitSpeed;
				const int occ = moved[h];
				if(!reachable && occ >= 0 && occ != unit && s.units[occ].side != s.units[unit].side)
				{
					enemyUnit = true;
					const BattleHex there(h);
					for(const auto & neighbour : there.getNeighbouringTiles())
					{
						if(dist[neighbour.toInt()] <= unitSpeed)
						{
							reachable = true;
							break;
						}
					}
				}
				if(!reachable)
				{
					// it would have got there in this round and in the next
					reach(0, BattleHex(h), first, second);
					if((first & second) >> unit & 1)
						blockingScore += ratio * (enemyUnit ? BLOCKING_OWN_ATTACK_PENALTY : BLOCKING_OWN_MOVE_PENALTY);
				}
			}
		}
	}
	return blockingScore > BLOCKING_THRESHOLD;
}

/// BattleEvaluator::moveOrAttack: an attack that can be made from that hex is taken on the way
SimAction Model::moveOrAttack(const BattleHex & hex) const
{
	int additional = 0;
	const Attack * onTheWay = nullptr;
	for(const auto & ap : attacks)
	{
		if(!ap.shooting && ap.from == hex && ap.value() > additional)
		{
			additional = static_cast<int>(ap.value());
			onTheWay = &ap;
		}
	}
	if(onTheWay)
		return {SimActionType::ATTACK, onTheWay->target, hex};
	if(hex == self.pos)
		return {SimActionType::DEFEND, -1, BattleHex()};
	return {SimActionType::MOVE, -1, hex};
}

/// BattleEvaluator::goTowardsNearest
SimAction Model::goTowards(const BattleHex & wanted)
{
	const SimAction defend = {SimActionType::DEFEND, -1, BattleHex()};
	if(!wanted.isValid())
		return defend;
	const Distances & dist = distancesOf(me);
	if(dist[wanted.toInt()] > FIELD_SIZE)
		return defend; // only through an enemy
	if(dist[wanted.toInt()] <= speed[me])
		return moveOrAttack(wanted);
	if(covers(me, wanted))
		return defend;

	if(self.flying)
	{
		// a flying unit lands as close to the hex as it gets
		BattleHex bestHex;
		int bestDistance = std::numeric_limits<int>::max();
		for(int h = 0; h < FIELD_SIZE; ++h)
		{
			if(dist[h] > speed[me])
				continue;
			int distance = BattleHex::getDistance(wanted, BattleHex(h));
			if(blocksOwn(BattleHex(h)))
				distance += 100;
			if(distance < bestDistance)
			{
				bestDistance = distance;
				bestHex = BattleHex(h);
			}
		}
		return bestHex.isValid() ? moveOrAttack(bestHex) : defend;
	}

	int current = wanted.toInt();
	for(int guard = 0; guard < FIELD_SIZE; ++guard)
	{
		if(current < 0)
			return defend;
		if(dist[current] <= speed[me] && !blocksOwn(BattleHex(current)))
			return moveOrAttack(BattleHex(current));
		current = myPredecessors[current];
	}
	return defend;
}

/// BattleEvaluator::selectStackAction
SimAction Model::choose()
{
	const SimAction wait = {SimActionType::WAIT, -1, BattleHex()};
	const SimAction defend = {SimActionType::DEFEND, -1, BattleHex()};

	gatherTargets();
	const MoveTarget moveTarget = findMove();
	lastMove = moveTarget;
	float score = INEFFECTIVE_SCORE;

	if(!attacks.empty())
	{
		int best = 0;
		bool waitFirst = false;
		float bestScore = INEFFECTIVE_SCORE;
		findBestTarget(best, waitFirst, bestScore);
		if(bestScore > score)
		{
			score = bestScore;
			if(moveTarget.score <= score)
			{
				if(waitFirst)
					return wait;
				const Attack & attack = attacks[best];
				if(attack.shooting)
					return {SimActionType::SHOOT, attack.target, BattleHex()};
				// a blow that costs our own side half of what it costs the enemy, in a losing exchange: rather not
				if(attack.collateralReduce != 0 && attack.collateralReduce >= attack.defenderReduce / 2 && score < 0)
					return defend;
				return {SimActionType::ATTACK, attack.target, attack.from};
			}
		}
	}

	if(moveTarget.score > score)
	{
		if(!self.waited)
			return wait;
		return goTowards(moveTarget.position);
	}
	return self.waited ? defend : wait;
}

/// What the model saw, in the format of FairBattleAI::oracleAction's trace
void Model::describe(std::string & into)
{
	for(int variant = 0; variant < 2; ++variant)
	{
		for(int t = 0; t < ORDER_TURNS; ++t)
		{
			into += boost::str(boost::format("\n  order%s%d:") % (variant ? "W" : "") % t);
			for(int k = 0; k < order[variant][t].size; ++k)
				into += boost::str(boost::format(" %d") % static_cast<int>(order[variant][t].unit[k]));
		}
	}
	into += "\n  unreachable:";
	for(int k = 0; k < unreachable.size; ++k)
		into += boost::str(boost::format(" %d") % static_cast<int>(unreachable.unit[k]));
	for(const auto & ap : attacks)
	{
		const float waited = self.waited ? 0.0f : scoreValue(exchange(ap, 0, 1, 0));
		const float now = scoreValue(exchange(ap, 0, 0, 0));
		into += boost::str(boost::format("\n  ap|%d|%d|%d|%.1f|%.1f|%d|%.1f|%.1f|") % ap.target % (ap.from.isValid() ? ap.from.toInt() : -1) % (ap.shooting ? 1 : 0)
			% ap.defenderReduce % ap.attackerReduce % ap.blockedShooters % now % waited);
		for(uint32_t rest = lastExchangeUnits; rest; rest &= rest - 1)
			into += boost::str(boost::format("%d,") % lowest(rest));
		into += boost::str(boost::format("|%.1f") % ap.collateralReduce);
	}
	into += boost::str(boost::format("\n  move|%.1f|%d|%d") % lastMove.score % (lastMove.position.isValid() ? lastMove.position.toInt() : -1) % lastMove.turnsToReach);
}

}

bool guardAction(const SimState & state, int unit, int simulationTurns, SimAction & out, std::string * trace)
{
	if(unit < 0 || unit >= static_cast<int>(state.units.size()) || state.units.size() > MAX_UNITS || !state.damage || !state.units[unit].alive())
		return false;
	Model model(state, unit, simulationTurns);
	out = model.choose();
	if(trace)
		model.describe(*trace);
	return true;
}

}
