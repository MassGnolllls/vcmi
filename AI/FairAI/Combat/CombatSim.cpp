/*
 * CombatSim.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "CombatSim.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/BonusEnum.h"

namespace FAI::Combat
{

namespace
{
double average(const DamageRange & range)
{
	return (range.min + range.max) / 2.0;
}

constexpr int MAX_SIM_STEPS = 600;
}

// ------------------------------------------------------------------ DamageTable

void DamageTable::build(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & units)
{
	n = units.size();
	meleeDmg.assign(n * n, 0.0);
	rangedNear.assign(n * n, 0.0);
	rangedFar.assign(n * n, 0.0);

	for(size_t a = 0; a < n; ++a)
	{
		const battle::Unit * attacker = units[a];
		const double count = std::max(1, attacker->getCount());

		for(size_t d = 0; d < n; ++d)
		{
			const battle::Unit * defender = units[d];
			if(a == d || attacker->unitSide() == defender->unitSide())
				continue;

			BattleAttackInfo meleeInfo(attacker, defender, 0, false);
			meleeDmg[a * n + d] = average(cb.battleEstimateDamage(meleeInfo).damage) / count;

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

// ------------------------------------------------------------------ SimState: setup

void SimState::init(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & battleUnits, const DamageTable & table)
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
		s.id = u->unitId();
		s.side = u->unitSide();
		s.creature = u->creatureId();
		s.count = u->getCount();
		s.firstHP = u->getFirstHPleft();
		s.maxHP = std::max(1, static_cast<int>(u->getMaxHealth()));
		s.value = u->creatureId().toCreature() ? std::max(1, u->creatureId().toCreature()->getAIValue()) : 1;
		s.speed = static_cast<int>(u->getMovementRange(0));
		s.initiative = state->getInitiative(0);
		s.shooter = u->isShooter();
		s.shots = s.shooter ? state->shots.available() : 0;
		s.flying = u->hasBonusOfType(BonusType::FLYING);
		s.doubleWide = u->doubleWide();
		s.canMove = !u->hasBonusOfType(BonusType::SIEGE_WEAPON) && s.speed > 0;
		s.noDistancePenalty = u->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY);
		s.blocksRetaliation = u->hasBonusOfType(BonusType::BLOCKS_RETALIATION);
		s.attacksAllAdjacent = u->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT);
		s.freeShooting = u->canShootBlocked();
		s.meleeAttacks = std::max(1, u->getTotalAttacks(false));
		s.rangedAttacks = std::max(1, u->getTotalAttacks(true));
		s.retaliationsPerRound = u->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS) ? 1000 : std::max(0, state->counterAttacks.total());
		s.retaliationsLeft = u->ableToRetaliate() ? (u->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS) ? 1000 : state->counterAttacks.available()) : 0;
		s.pos = u->getPosition();
		s.acted = u->moved() || (!s.canMove && !s.shooter);
		s.waited = u->waited();
		units.push_back(s);
	}

	for(int i = 0; i < static_cast<int>(units.size()); ++i)
		place(i, units[i].pos);

	round = 0;
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
				return best;
		}

		// everyone acted: new round
		round++;
		for(auto & u : units)
		{
			u.acted = !u.alive() || (!u.canMove && !u.shooter);
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

void SimState::dealDamage(int target, double amount)
{
	auto & t = units[target];
	int64_t total = t.totalHP() - static_cast<int64_t>(std::llround(amount));
	if(total <= 0)
	{
		lift(target);
		t.count = 0;
		t.firstHP = 0;
		return;
	}
	t.count = static_cast<int>((total + t.maxHP - 1) / t.maxHP);
	t.firstHP = static_cast<int>(total - static_cast<int64_t>(t.count - 1) * t.maxHP);
}

void SimState::strike(int attacker, int target, bool ranged)
{
	const auto & a = units[attacker];
	if(ranged)
	{
		dealDamage(target, damage->ranged(attacker, target, shotIsFar(attacker, target)) * a.count);
		return;
	}

	if(a.attacksAllAdjacent)
	{
		for(int i = 0; i < static_cast<int>(units.size()); ++i)
		{
			if(i != target && units[i].alive() && units[i].side != a.side && adjacent(attacker, a.pos, i))
				dealDamage(i, damage->melee(attacker, i) * a.count);
		}
	}
	dealDamage(target, damage->melee(attacker, target) * a.count);
}

void SimState::finishTurn(int unit, bool waited)
{
	auto & u = units[unit];
	if(waited && !u.waited)
		u.waited = true;
	else
		u.acted = true;
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
		if(action.hex.isValid() && action.hex != u.pos)
		{
			lift(unit);
			place(unit, action.hex);
		}
		int target = action.target;
		for(int strikeNo = 0; strikeNo < units[unit].meleeAttacks; ++strikeNo)
		{
			if(!units[unit].alive() || !units[target].alive())
				break;
			strike(unit, target, false);
			auto & t = units[target];
			if(t.alive() && t.retaliationsLeft > 0 && !units[unit].blocksRetaliation)
			{
				strike(target, unit, false);
				t.retaliationsLeft--;
			}
		}
		finishTurn(unit, false);
		break;
	}

	case SimActionType::SHOOT:
		for(int shot = 0; shot < units[unit].rangedAttacks; ++shot)
			if(units[action.target].alive())
				strike(unit, action.target, true);
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

	if(!outcome.shooting && outcome.from.isValid() && outcome.from != u.pos)
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
	double dealt = std::min<double>(t.totalHP(), damage->ranged(attacker, target, shotIsFar(attacker, target)) * a.count * a.rangedAttacks);
	return dealt / t.maxHP * t.value;
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

	if(bestTarget >= 0 && (stance[static_cast<int>(u.side)] == Stance::CHARGE || bestValue > 0))
	{
		apply(unit, {SimActionType::ATTACK, bestTarget, bestHex});
		return;
	}

	// nothing in reach: advance towards the closest enemy - unless this side holds its ground
	if(stance[static_cast<int>(u.side)] == Stance::HOLD)
	{
		apply(unit, {SimActionType::DEFEND, -1, BattleHex()});
		return;
	}

	BattleHex bestMove = u.pos;
	int bestRemaining = 1000;
	for(int h = 0; h < FIELD_SIZE; ++h)
	{
		if(dist[h] < 0)
			continue;
		int closest = 1000;
		for(const auto & e : units)
			if(e.alive() && e.side != u.side)
				closest = std::min<int>(closest, BattleHex::getDistance(BattleHex(h), e.pos));
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

}
