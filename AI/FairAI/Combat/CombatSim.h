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

#include <bitset>
#include "../../../lib/battle/BattleHex.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/battle/BattleSide.h"
#include "../../../lib/GameConstants.h"

VCMI_LIB_NAMESPACE_BEGIN
class CBattleInfoCallback;
class ACreature;
class CCreature;
class CGHeroInstance;
class JsonNode;
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
	int initiativeNext = 0; ///< ... in the next round: differs when haste or slow runs out
	int shots = 0;
	int retaliationsLeft = 0;
	int retaliationsPerRound = 1;

	bool shooter = false;
	bool flying = false;
	bool doubleWide = false;
	bool canMove = true;
	bool active = true; ///< false while blinded, paralysed or petrified: skips its turns until attacked
	bool activeNext = true; ///< ... and in the next round: the effect may run out before
	bool noDistancePenalty = false;
	bool blocksRetaliation = false; ///< enemies do not retaliate against this unit
	bool attacksAllAdjacent = false;
	bool freeShooting = false; ///< can shoot while blocked
	int meleeAttacks = 1;
	int rangedAttacks = 1;
	int minDamage = 1;        ///< damage of one creature as the creature list has it (the model of the guards needs the span)
	int maxDamage = 1;
	int minDamageRanged = 1;

	// --- special abilities, read from the unit's bonuses (so creatures of mods are covered as well)
	int baseCount = 0;               ///< stack size at the start of the battle (limit for life drain)
	bool living = true;              ///< neither undead nor a construct: its life can be drained
	double lifeDrain = 0;            ///< share of melee damage dealt that comes back as health (vampire lords)
	int regeneration = 0;            ///< health the top creature regains when its turn starts (wights, trolls)
	double fireShield = 0;           ///< share of melee damage received that burns the attacker (efreet sultans)
	bool fireImmune = false;
	bool returnsAfterStrike = false; ///< flies back to where it started after a melee attack (harpies)
	// A melee attack that reaches further hexes: breath goes on behind the target (dragons, phoenix),
	// several heads bite sideways (cerberus). Each path is noted the way the game notes it: steps from
	// the attacking hex, one digit per step, 1 = straight on towards the target, 2 and 6 = one turn to
	// either side.
	uint8_t extraCount = 0;
	std::array<int16_t, 6> extraPath{};
	uint8_t extraFriendly = 0;       ///< bit i: path i burns our own units too (breath)
	// The same paths whatever the simulator's switch for such attacks says (setMultiHex): the model of
	// the guards has to count them the way VCMI's BattleAI does - which sees friend and foe alike on them
	uint8_t reachCount = 0;
	std::array<int16_t, 8> reachPath{};
	bool areaShot = false;           ///< its shot bursts on every hex around the target, friend and foe (lich, magog)
	bool areaLivingOnly = false;     ///< ... and harms only the living (death cloud)
	bool areaFire = false;           ///< ... and is fire (fireball): the fire-immune stay unharmed
	bool magicImmune = false;        ///< immune to all spells: spell-like shots do nothing to it
	// --- what an attack does beyond its damage: read from the unit's bonuses and the spells they
	// name, as expected values (every chance is per attack). Index 0 = melee, 1 = shot.
	std::array<double, 3> stare{};   ///< death stare: chance per attacking creature to kill one creature of the target (mighty gorgons; accurate shot: index 2 = shot from afar)
	std::array<double, 2> stun{};    ///< chance to put the target out of action (petrify, paralyse, freeze)
	std::array<double, 2> stunMind{}; ///< the same by a spell on the mind (blind, hypnotise): the mindless are immune
	std::array<double, 2> bolt{};    ///< expected spell damage per attacking creature (thunderbolt)
	std::array<double, 2> ageing{};  ///< chance to cut the health of the target's creatures (ageing, poison)
	double ageDepth = 0;             ///< ... by this share: half when aged, about a quarter over a fight when poisoned
	std::array<double, 2> sapAttack{};  ///< expected attack points taken from the target (weakness, disease)
	std::array<double, 2> sapDefence{}; ///< expected defence points taken (disease, acid breath)
	std::array<double, 2> curse{};   ///< chance to curse the target: it deals its lowest damage
	double acid = 0;                 ///< expected acid damage per attacking creature (rust dragons)
	bool flesh = true;               ///< can be stared to death, aged, poisoned: neither undead nor construct, machine or gargoyle
	bool mindImmune = false;         ///< cannot be blinded
	bool fearless = false;           ///< does not lose turns to fear
	double spreadsFear = 0;          ///< chance that each enemy unit loses its turn (azure dragons)
	double spellResist = 0;          ///< chance to resist a hostile spell (dwarves, a hero's Resistance)
	double resistAura = 0;           ///< ... that it lends to the units of its side next to it (unicorns)
	double lowDamage = 1.0;          ///< its lowest damage as a share of its average damage (what a curse leaves)
	// --- what the unit suffers from such attacks at the moment (expected values)
	double stunned = 0;              ///< share of its next action that is lost
	double afflicted = 0;            ///< probability that its creatures are aged or poisoned
	int fullHP = 1;                  ///< health of one creature before that (maxHP shrinks with it: the creatures stay, each dies sooner)
	double attackLost = 0;
	double defenceLost = 0;
	double cursed = 0;
	double meleeFactor = 1.0;        ///< expected multiplier of melee damage dealt: luck, death blow
	double rangedFactor = 1.0;       ///< expected multiplier of ranged damage dealt: luck
	double turnFactor = 1.0;         ///< expected number of own actions per turn: good morale > 1, bad morale < 1

	BattleHex pos;

	bool acted = false;
	bool waited = false;
	bool hadMorale = false; ///< got an extra turn from good morale in this round (the game queues such a unit ahead of the others that waited)

	bool alive() const { return count > 0; }
	int64_t totalHP() const { return count > 0 ? static_cast<int64_t>(count - 1) * maxHP + firstHP : 0; }
	double remainingValue() const { return count > 0 ? value * (count - 1 + static_cast<double>(firstHP) / maxHP) : 0.0; }
};

/// A hero's spell as a forecast plays it: what it does to every unit of the battle
struct SimSpell
{
	int32_t id = -1;
	int cost = 0;
	bool everyUnit = false;       ///< reaches the whole field (death ripple, armageddon)
	bool sparesOwn = true;        ///< picks its victims: the caster's own units stay unharmed
	std::array<bool, 3> layer{};  ///< the hexes it reaches around its aim: the aim itself, one step away, two steps away
	int chain = 1;                ///< jumps on until it has hit this many units, friend or foe (chain lightning) ...
	double chainFactor = 1.0;     ///< ... each with this share of the damage before
	std::vector<double> damage;   ///< per unit: health it takes off a unit it hits first; 0 = the unit is immune. Without the chance to resist
};

/// The hero of one side in a forecast
struct SimHero
{
	int mana = 0;
	bool casts = false;           ///< casts the most harmful spell every round while the spell points last
	int spent = 0;                ///< spell points used so far
	int castRound = -1;           ///< round of the last spell: one a round
	const std::vector<SimSpell> * spells = nullptr;
};

/// A hero as a forecast is told of it. Ours: everything is read from the hero. An enemy's: what we
/// have seen of it and estimate.
struct HeroSpec
{
	const CGHeroInstance * hero = nullptr;
	int power = 0;
	int mana = 0;
	std::vector<std::pair<SpellID, int>> spells; ///< spell and mastery of its school (0 none to 3 expert)
};

/// One stack described by anything that carries creature bonuses: a unit in a battle, a stack of
/// an army on the adventure map (hero skills included), or a plain creature type (observed armies)
struct StackSpec
{
	const ACreature * bearer = nullptr;
	const CCreature * type = nullptr;
	int count = 0;
	BattleSide side = BattleSide::NONE;
	int attackBonus = 0;  ///< added to the bearer's attack: an enemy hero's skill as we estimate it
	int defenceBonus = 0; ///< same for defence
};

/// Expected damage per attacking creature, per unit pair
class DamageTable
{
public:
	/// Without a battle (forecasts on the adventure map): FairAI's own copy of the game's damage formula
	void build(const std::vector<StackSpec> & stacks);
	/// Expected damage of one attacking creature, without luck and other dice
	static double formula(const ACreature & attacker, const CCreature * attackerType, const ACreature & defender, const CCreature * defenderType, bool shooting, int chargeDistance = 0, int attackBonus = 0, int defenceBonus = 0);
	/// abilities = false: plain damage only (to measure what the knowledge of special abilities is worth)
	void build(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & units, bool abilities = true);

	/// also filled for units of the same side where the attacker can hit its own (breath, bursting shots)
	double melee(size_t attacker, size_t defender) const { return meleeDmg[attacker * n + defender]; }
	/// extra melee damage per creature for every hex walked before the attack (jousting; 0 for everybody else)
	double chargeStep(size_t attacker, size_t defender) const { return chargeDmg.empty() ? 0.0 : chargeDmg[attacker * n + defender]; }
	double ranged(size_t attacker, size_t defender, bool far) const { return far ? rangedFar[attacker * n + defender] : rangedNear[attacker * n + defender]; }
	/// The lowest and the highest melee damage of one creature, before the game rounds the damage of the
	/// whole stack down (the model of the guards needs the span as the game computes it)
	double meleeLow(size_t attacker, size_t defender) const { return meleeLowDmg.empty() ? melee(attacker, defender) : meleeLowDmg[attacker * n + defender]; }
	double meleeHigh(size_t attacker, size_t defender) const { return meleeHighDmg.empty() ? melee(attacker, defender) : meleeHighDmg[attacker * n + defender]; }
	/// ... and what every hex walked before the blow adds to each (jousting; 0 for everybody else)
	double chargeLow(size_t attacker, size_t defender) const { return chargeLowDmg.empty() ? chargeStep(attacker, defender) : chargeLowDmg[attacker * n + defender]; }
	double chargeHigh(size_t attacker, size_t defender) const { return chargeHighDmg.empty() ? chargeStep(attacker, defender) : chargeHighDmg[attacker * n + defender]; }
	/// What a unit would do to one of its own side hand to hand - the game's estimate, which BattleAI
	/// uses when it weighs attacks that reach several hexes (0 where no unit of the battle has such an attack)
	double ownSide(size_t attacker, size_t defender) const { return ownSideDmg.empty() ? 0.0 : ownSideDmg[attacker * n + defender]; }

private:
	size_t n = 0;
	std::vector<double> meleeDmg;
	std::vector<double> rangedNear;
	std::vector<double> rangedFar;
	std::vector<double> chargeDmg;
	std::vector<double> meleeLowDmg;
	std::vector<double> meleeHighDmg;
	std::vector<double> ownSideDmg;
	std::vector<double> chargeLowDmg;
	std::vector<double> chargeHighDmg;
};

/// How a side behaves in play-outs. Evaluating against several behaviours keeps the search from
/// relying on an opponent that walks into a trap.
enum class Stance : uint8_t
{
	CHARGE, ///< melee units advance towards the enemy
	HOLD,   ///< melee units only attack what is already in reach, otherwise stay
	STALK,  ///< like VCMI's BattleAI: with nothing in reach wait for the others to move, then close in on the most rewarding target
	AMBUSH, ///< let them come: with nothing worth hitting in reach wait; after waiting hit what has come into reach, otherwise stay. Faster units strike an enemy that closed in at the end of the round - and again before it at the start of the next
	GUARD,  ///< close ranks: like AMBUSH, but a unit that does not stand next to our most valuable shooter first walks there - the enemy meets our troops together, and the shooter cannot be reached
	KITE    ///< shoot and stay out of reach: a unit an enemy could strike before its next turn steps out of that enemy's reach; shooters shoot while they are safe; the others wait for the enemy to move and then strike only where nothing strikes back. While our side has a shot left - then it advances
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

class SimState;

/// Something that knows what an enemy unit will really do (FairBattleAI: VCMI's BattleAI run on a
/// mirror of the state). Slow and exact, where the default policy is fast and a guess.
class EnemyOracle
{
public:
	virtual ~EnemyOracle() = default;
	virtual bool choose(const SimState & state, int unit, SimAction & out) const = 0;
};

class SimState
{
public:
	std::vector<SimUnit> units;
	std::array<int16_t, FIELD_SIZE> occupant{}; ///< -1 free, -2 blocked by terrain, >= 0 unit index
	/// Tiles that end a walk (Quicksand, a moat) - those our side sees. A unit that steps on one stands
	/// there, and strikes nobody this turn; the hexes it starts its move on do not count.
	std::bitset<FIELD_SIZE> stoppers;
	int round = 0;
	BattleSide lastMoved = BattleSide::NONE;
	const DamageTable * damage = nullptr;
	std::array<Stance, 2> stance = {Stance::CHARGE, Stance::CHARGE}; ///< indexed by side
	std::array<SimHero, 2> heroes; ///< attacker, defender (forecasts; a battle's own search does not play the heroes yet)

	/// Build from the real battle. `units` order must match the damage table.
	void init(const CBattleInfoCallback & cb, const std::vector<const battle::Unit *> & battleUnits, const DamageTable & table, bool abilities = true);
	/// A battle that has not begun: both armies on the game's default starting hexes, no obstacles.
	/// `stacks` order must match the damage table; `layout` is the game setting for battle layouts.
	void initForecast(const std::vector<StackSpec> & stacks, const DamageTable & table, const JsonNode & layout);
	/// everything about a unit that follows from its bonuses (shared by battles and forecasts)
	static void describe(SimUnit & unit, const ACreature & bearer, const CCreature * type);

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
	/// Would the unit, coming to `at`, stand on a tile that ends its walk?
	bool stoppedAt(int unit, BattleHex at) const;
	/// ... within \p range hexes instead of its speed (several moves in a row, taken as one)
	void reachableWithin(int unit, int range, std::array<int8_t, FIELD_SIZE> & dist) const;
	bool adjacent(int a, BattleHex aPos, int b) const;
	bool isBlocked(int unit) const; ///< an enemy stands next to it
	bool canShoot(int unit) const;

	// --- actions
	void apply(int unit, const SimAction & action);
	SimAction lastAction;  ///< the action applied last (what the default policy chose, for the policy check)
	/// Play-outs: the units of oracleSide are played by the oracle up to round oracleLastRound
	const EnemyOracle * oracle = nullptr;
	BattleSide oracleSide = BattleSide::NONE;
	int oracleLastRound = -1;
	int oracleCalls = 0;
	/// apply an attack whose result was computed by the engine-based evaluator
	void applyOutcome(int unit, const AttackOutcome & outcome);
	/// let the default (greedy) policy act for this unit
	void playDefault(int unit);
	/// Can the unit shoot, or reach an enemy for a melee attack, in this turn?
	bool hasAttack(int unit) const;
	/// Stance::KITE: what \p unit does to stay out of reach. ACT: \p out; SHOOT: it is safe and can
	/// shoot (the default policy picks the target); OVER: the side has no shot left or the unit is
	/// cornered - staying out of reach is over for the side, it goes on with stanceAfterKite.
	enum class KiteStep { ACT, SHOOT, OVER };
	KiteStep kiteAction(int unit, SimAction & out) const;
	/// a unit that can strike and be gone: nothing strikes back at it, or it is faster than every enemy
	bool kiteStriker(int unit) const;
	std::array<Stance, 2> stanceAfterKite = {Stance::CHARGE, Stance::CHARGE};
	std::array<bool, 2> kiteOver = {false, false};
	/// what staying out of reach is kept up with: 1 - shots, 2 - troops nothing strikes back at
	/// (vampire lords against walkers slower than they: wait, strike at the end of the round, be
	/// gone at the start of the next - 300 of 300 staged battles without a loss, 24.11.)
	int kiteWith = 3;
	/// the flight of a unit that cannot wait out the enemy's next move anywhere is chosen two moves
	/// ahead (the decision itself only: too slow for play-outs)
	bool kiteDeep = false;
	bool stopAtKiteEnd = false; ///< playout() returns when staying out of reach is over for a side (to try what comes after)
	/// The unit our side closes ranks around: its most valuable stack that can shoot (-1: none)
	int anchorOf(BattleSide side) const;
	/// Where \p unit would stand to close ranks: the hex next to the anchor it reaches in this turn,
	/// on the side the enemy comes from (invalid: none, or it stands there already)
	BattleHex rallyHex(int unit) const;
	/// play with the default policy until the end of round `lastRound` or the end of the battle
	void playout(int lastRound);
	/// The hero of `side` casts if it may this round and a spell of its harms the enemy more than its own
	/// side. Returns whether it cast.
	bool heroCast(BattleSide side);

	/// expected value killed/lost by an attack, without applying it
	double meleeTradeValue(int attacker, int target) const;
	double shotValue(int attacker, int target) const;
	/// How VCMI's BattleAI - the combat AI of the map's guards - counts damage (AttackPossibility::
	/// calculateDamageReduce): what the victim would deal to the attacker, for the part of it that
	/// dies - half for creatures killed, half for health taken. In damage points, not army value.
	double damageReduce(int attacker, int victim, double dealt) const;
	/// A shot and a melee attack in that measure (melee: minus what the retaliation takes from us,
	/// plus what a shooter loses that the attacker blocks by standing on \p from)
	double guardShotValue(int attacker, int target) const;
	double guardMeleeValue(int attacker, int target, BattleHex from) const;
	/// The units of this side choose their targets by that measure (set for map guards when the
	/// enemy model is Stance::STALK)
	std::array<bool, 2> guardValues = {false, false};
	/// The units of this side are played by GuardModel, the copy of BattleAI's decision rules: the
	/// number of turns that AI plays an exchange of blows ahead (it uses 10 from the fourth difficulty
	/// on, 2 below). 0 = off: the stance decides.
	std::array<uint8_t, 2> guardModel = {0, 0};
	int guardModelLastRound = std::numeric_limits<int>::max(); ///< ... up to this round

private:
	void place(int unit, BattleHex at);
	void lift(int unit);
	/// returns the health actually removed
	int64_t dealDamage(int target, double damage);
	/// health taken as it is (dealDamage: after what the target's weakened state adds)
	int64_t removeHealth(int target, double amount);
	/// health back to a stack, fallen creatures return up to the size the stack had at the start
	void heal(int unit, double amount);
	/// chargeHexes: hexes walked before a melee attack; ownTurn: the unit's own action (not a retaliation)
	void strike(int attacker, int target, bool ranged, int chargeHexes = 0, bool ownTurn = false);
	/// what an attack does beyond its damage: death stare, bolts, acid, and what the target suffers from then on
	void afterHit(int attacker, int target, bool ranged, double share, bool far = false);
	/// fear spread by one side's units takes turns from the other side (called once the units are described)
	void applyFear();
	/// What a spell does if it is aimed at unit `aim` or hex `hex`: the units it hits and the health each
	/// loses (resisting counted in as an expected value). Returns the caster's gain in value.
	double spellHits(const SimSpell & spell, BattleSide caster, int aim, BattleHex hex, std::vector<std::pair<int, double>> & hits) const;
	/// units on the further hexes a melee attack of `attacker` on `target` reaches (not the target itself)
	void extraVictims(int attacker, int target, std::array<int, 8> & out, int & count) const;
	/// units next to the target that a bursting shot of `attacker` also hits
	void burstVictims(int attacker, int target, std::array<int, 8> & out, int & count) const;
	void finishTurn(int unit, bool waited);
	bool shotIsFar(int attacker, int target) const;
};

/// The plan "shoot and stay out of reach" (Stance::KITE) played on from \p start for \p ownSide. How
/// long to keep it up? Until somebody is cornered or the last shot is gone, at the latest; from
/// there the battle is fought out by whichever of \p plans does it best. Often it pays to turn and
/// fight earlier - the pursuers arrive one by one -, so the same is tried after each of the next
/// \p turns rounds. Returns the end that is best for us (evaluate()); \p turnedAfter: the round after
/// which we turned in it, \p lasted: the round up to which staying out of reach could be kept up.
SimState playKite(const SimState & start, BattleSide ownSide, const std::vector<Stance> & plans, int turns, int lastRound, int * turnedAfter = nullptr, int * lasted = nullptr,
	std::string * report = nullptr, SimState * turnedFrom = nullptr);

/// How a battle between two armies is expected to end, both sides played by the default policy:
/// plan 0 = our side holds its ground, 1 = it advances. The enemy always advances. With a hero of ours
/// that has spells worth casting there are four plans: 2 and 3 are the same two with the hero casting.
/// An enemy hero casts in every plan.
struct Forecast
{
	double ownBefore = 0;
	double enemyBefore = 0;
	int plans = 2;
	std::array<double, 10> ownAfter{};
	std::array<double, 10> enemyAfter{};
	std::array<int, 10> rounds{};
	std::array<int, 10> manaSpent{}; ///< spell points our hero uses in the plan
	int kitePlan = -1; ///< the plan "shoot and stay out of reach", if it was played (ForecastOptions::kiteTurns)

	/// The plan we would pick. manaPrice: what a spell point is worth, in the value of our army - a
	/// plan with spells must save more army than its spell points are worth.
	int bestPlan(double manaPrice = 0) const;
	bool win(int plan) const { return enemyAfter[plan] <= 0 && ownAfter[plan] > 0; }
	/// share of our army value lost; 1 if the battle is lost (or not won in time)
	double lossShare(int plan) const;
};

/// How a forecast plays: as the combat AI plays the battle afterwards
struct ForecastOptions
{
	int ownStances = 2;   ///< plans for our side: 2 = hold, advance; 3 = and lie in wait; 4 = and close ranks (the plans of the combat AI's battle plan). With spells every plan is played twice, the second time with the hero casting.
	int guardTurns = 0;   ///< > 0: the enemy is played by GuardModel - the copy of VCMI's BattleAI, which plays map guards -, looking this many turns of an exchange ahead (as that AI: 10 on the highest difficulty, 2 below)
	int guardRounds = 6;  ///< ... for this many rounds, by its stance afterwards (the opening decides; the model costs time)
	int kiteTurns = 0;    ///< > 0 (with guardTurns): one more plan, "shoot and stay out of reach" (playKite), turning to fight tried after each of this many rounds; the guards by the model to the end
};

Forecast forecastBattle(const std::vector<StackSpec> & stacks, BattleSide ownSide, const JsonNode & layout, int maxRounds = 30, Stance enemyStance = Stance::CHARGE,
	const HeroSpec * ownHero = nullptr, const HeroSpec * enemyHero = nullptr, const ForecastOptions & options = {});

/// Attacks that reach several hexes (breath, further heads, bursting shots) in the simulator: on or off.
/// One switch for the whole process (parameter simMultiHex; off until the unit lab and a benchmark confirm it).
void setMultiHex(bool enabled);
/// What a spell point is worth to a hero right now, as a factor on the plain price: told by the
/// adventure AI (a town with a mage guild within a day: cheap; on the way to the enemy: dear),
/// read by the battle AI when it weighs a spell. 1 when nothing is known about the hero.
void setManaUrgency(int hero, double urgency);
double manaUrgency(int hero);

/// What attacks do beyond their damage (death stare, petrifying and the like, ageing, poison, curse,
/// weakness, acid, thunderbolt) - off until the unit lab has confirmed it against the game
void setAfterHit(bool enabled);
/// How a won battle is counted: by the creatures that are left - a wounded creature is whole again
/// after the battle -, not by the health left. Wounds then cost only a tenth of their share: a blow
/// that kills nobody is almost free, as it is in the game (damage spread over tough stacks instead of
/// taken by one). Battles that are not over at the horizon are counted by health as before.
void setWholeCreatures(bool enabled);
bool multiHex();

}
