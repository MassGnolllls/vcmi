/*
 * CombatSim.h, part of VCMI engine - FairAI
 *
 * A small, fast forward model of a battle used for look-ahead search.
 *
 * Accuracy model:
 *  - damage per creature for every attacker/defender pair (melee, ranged near/far) is taken from
 *    the engine's own damage estimation at the start of each decision, so hero skills, specialties,
 *    creature bonuses and active spell effects are respected
 *  - the dynamics (movement, turn order, retaliation, double attacks, adjacency attacks, ammo,
 *    blocked shooters) are simulated here with expected values (no luck/morale dice)
 *  - movement uses its own breadth-first search on the hex grid (obstacles from the engine)
 *
 * Everything in a battle is visible to both sides in Heroes III, so no fairness filtering is
 * needed here; only information about the enemy hero's spell book is not used.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../../lib/battle/BattleHex.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/battle/BattleSide.h"
#include "../../../lib/GameConstants.h"

VCMI_LIB_NAMESPACE_BEGIN
class CBattleInfoCallback;
namespace battle
{
class Unit;
}
VCMI_LIB_NAMESPACE_END

namespace FAI::Combat
{

constexpr int FIELD_SIZE = GameConstants::BFIELD_SIZE;

struct SimUnit
{
	uint32_t id = 0;
	BattleSide side = BattleSide::NONE;
	CreatureID creature;

	int count = 0;
	int firstHP = 0;
	int maxHP = 1;
	double value = 0; ///< AI value of one creature

	int speed = 0;
	int initiative = 0;
	int shots = 0;
	int retaliationsLeft = 0;
	int retaliationsPerRound = 1;

	bool shooter = false;
	bool flying = false;
	bool doubleWide = false;
	bool canMove = true;
	bool noDistancePenalty = false;
	bool blocksRetaliation = false; ///< enemies do not retaliate against this unit
	bool attacksAllAdjacent = false;
	bool freeShooting = false; ///< can shoot while blocked
	int meleeAttacks = 1;
	int rangedAttacks = 1;

	BattleHex pos;

	bool acted = false;
	bool waited = false;

	bool alive() const { return count > 0; }
	int64_t totalHP() const { return count > 0 ? static_cast<int64_t>(count - 1) * maxHP + firstHP : 0; }
	double remainingValue() const { return count > 0 ? value * (count - 1 + static_cast<double>(firstHP) / maxHP) : 0.0; }
};

/// Expected damage per attacking creature, per unit pair
class DamageTable
{
public:
	void build(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & units);

	double melee(size_t attacker, size_t defender) const { return meleeDmg[attacker * n + defender]; }
	double ranged(size_t attacker, size_t defender, bool far) const { return far ? rangedFar[attacker * n + defender] : rangedNear[attacker * n + defender]; }

private:
	size_t n = 0;
	std::vector<double> meleeDmg;
	std::vector<double> rangedNear;
	std::vector<double> rangedFar;
};

/// How a side behaves in play-outs. Evaluating against several behaviours keeps the search from
/// relying on an opponent that walks into a trap.
enum class Stance : uint8_t
{
	CHARGE, ///< melee units advance towards the enemy
	HOLD    ///< melee units only attack what is already in reach, otherwise stay
};

/// Engine-evaluated result of one attack (including special abilities and retaliation)
struct AttackOutcome
{
	BattleHex from;
	int attackerCount = 0;
	int attackerFirstHP = 0;
	bool shooting = false;
	int target = -1;
	std::vector<std::tuple<int, int, int>> affected; ///< unit index, count, first HP
};

enum class SimActionType : uint8_t
{
	MOVE,
	ATTACK,
	SHOOT,
	WAIT,
	DEFEND
};

struct SimAction
{
	SimActionType type = SimActionType::DEFEND;
	int target = -1;   ///< unit index for ATTACK / SHOOT
	BattleHex hex;     ///< destination for MOVE, attack position for ATTACK
};

class SimState
{
public:
	std::vector<SimUnit> units;
	std::array<int16_t, FIELD_SIZE> occupant{}; ///< -1 free, -2 blocked by terrain, >= 0 unit index
	int round = 0;
	BattleSide lastMoved = BattleSide::NONE;
	const DamageTable * damage = nullptr;
	std::array<Stance, 2> stance = {Stance::CHARGE, Stance::CHARGE}; ///< indexed by side

	/// Build from the real battle. `units` order must match the damage table.
	void init(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & battleUnits, const DamageTable & table);

	int indexOf(uint32_t unitId) const;
	bool battleOver() const;
	double sideValue(BattleSide side) const;
	/// our remaining value minus enemy remaining value
	double evaluate(BattleSide perspective) const;
	/// Next-round potential: for every unit the best trade it could make from where it stands
	/// (shot or reachable melee), ours minus theirs. Damps the horizon at the end of the round.
	double exposure(BattleSide perspective) const;

	/// Pick the next unit to act (engine turn order rules), starting a new round when needed. -1 if battle over.
	int nextActiveUnit();

	// --- geometry and movement
	std::vector<BattleHex> unitHexes(int unit, BattleHex at) const;
	bool canStand(int unit, BattleHex at) const;
	void reachable(int unit, std::array<int8_t, FIELD_SIZE> & dist) const;
	bool adjacent(int a, BattleHex aPos, int b) const;
	bool isBlocked(int unit) const; ///< an enemy stands next to it
	bool canShoot(int unit) const;

	// --- actions
	void apply(int unit, const SimAction & action);
	/// apply an attack whose result was computed by the engine-based evaluator
	void applyOutcome(int unit, const AttackOutcome & outcome);
	/// let the default (greedy) policy act for this unit
	void playDefault(int unit);
	/// play with the default policy until the end of round `lastRound` or the end of the battle
	void playout(int lastRound);

	/// expected value killed/lost by an attack, without applying it
	double meleeTradeValue(int attacker, int target) const;
	double shotValue(int attacker, int target) const;

private:
	void place(int unit, BattleHex at);
	void lift(int unit);
	void dealDamage(int target, double damage);
	void strike(int attacker, int target, bool ranged);
	void finishTurn(int unit, bool waited);
	bool shotIsFar(int attacker, int target) const;
};

}
