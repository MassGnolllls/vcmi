/*
 * FairBattleAI.h, part of VCMI engine - FairAI
 *
 * FairAI's combat AI.
 *
 * Decision per unit:
 *  1. special units (catapult, first aid tent, berserk, creature spell casters), the hero's
 *     spells and all of a siege are still handled by components of VCMI's BattleAI (compiled into FairAI)
 *  2. all regular unit actions are chosen by look-ahead search: every legal attack, shot, a few
 *     move targets, wait and defend are played forward in CombatSim (both sides with a greedy
 *     policy) for the rest of this round and two more; the action with the best material balance
 *     afterwards is taken
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../../lib/callback/CBattleGameInterface.h"
#include "../../../lib/battle/AutocombatPreferences.h"
#include "../../../lib/battle/BattleSide.h"
#include "../../../lib/battle/BattleAction.h"

VCMI_LIB_NAMESPACE_BEGIN
class CBattleCallback;
class CBattleInfoCallback;
class CStack;
class BattleAction;
class Environment;
VCMI_LIB_NAMESPACE_END

VCMI_LIB_NAMESPACE_BEGIN
struct BattleStackAttacked;
VCMI_LIB_NAMESPACE_END

class CBattleAI;
class HypotheticBattle;

namespace FAI::Combat
{

class SimState;
struct SimAction;

class FairBattleAI : public CBattleGameInterface
{
public:
	FairBattleAI();
	~FairBattleAI() override;

	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB) override;
	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences preferences) override;

	void activeStack(const BattleID & battleID, const CStack * stack) override;
	void yourTacticPhase(const BattleID & battleID, int distance) override;
	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	/// Unit lab (FAI_SIMCHECK=1): before every action of any unit the simulator predicts its result,
	/// afterwards the prediction is compared with what the game did. One log line per action;
	/// arena/unit_lab.py turns them into a list of what the simulator gets wrong, per creature.
	void actionStarted(const BattleID & battleID, const BattleAction & action) override;
	void actionFinished(const BattleID & battleID, const BattleAction & action) override;
	void battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged) override;
	void battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc) override;

private:
	struct SimCheck;
	std::unique_ptr<SimCheck> pendingCheck;
	/// Policy check (FAI_POLICYCHECK=1): before every action of an enemy unit, what would the
	/// simulator's default policy have done in its place? One log line per action; arena/policy_check.py
	/// counts how often the simulator's enemy does what the real one does.
	void policyCheck(const BattleID & battleID, const BattleAction & action);
	/// The enemy's oracle: what VCMI's BattleAI - the combat AI of the map's guards - does with unit
	/// \p unit in the simulator's state \p sim. The state is mirrored into a hypothetical battle of
	/// the engine and BattleAI's own evaluation is run on it. Slow (about a millisecond), exact.
	/// \p baseRound: the simulator's round in which the real battle stands. False: no answer.
	long oracleCallsMade = 0; ///< oracle calls of this battle AI (statistics)
	std::array<double, 6> oracleProfile{}; ///< calls, and milliseconds spent on: the mirror, the damage cache, the targets, the move target, the best target
	bool oracleAction(const std::shared_ptr<CBattleInfoCallback> & real, const SimState & sim, int unit, int baseRound, SimAction & out, std::string * trace = nullptr);
	std::map<uint32_t, int64_t> lastHealth; ///< unit -> health after the previous action (changes in between: regeneration, poison...)
	bool lastActionWasSpell = false;
	int actionsSeen = 0; ///< actions of this battle so far, whoever made them (numbers them for the test bench's listener)
	int quietDecisions = 0; ///< our decisions since anybody was hurt (the attacker must not let a battle stand still)
	/// A battle that stands still. Troops that regenerate or drain life can trade blows for ever
	/// without anybody getting weaker, and the game knows no draw: the battle - and with it the whole
	/// game - would never end. So we watch for it: no round has begun with either side weaker than
	/// ever before, and no hero has cast, for DEADLOCK_ROUNDS rounds. From then on our units must
	/// attack (the search may no longer wait, defend or step aside). If that does not wear the enemy
	/// down within DEADLOCK_PATIENCE rounds either, the battle is given up: see considerRetreat().
	/// Not against a human: whoever attacks us and then waits must not be rewarded for it.
	static constexpr int DEADLOCK_ROUNDS = 10;
	static constexpr int DEADLOCK_PATIENCE = 10;
	int seenRound = -1;           ///< the round in which our units were last asked for an action
	int64_t leastOwnHealth = 0;   ///< the lowest total health of our troops at the start of our part of a round
	int64_t leastEnemyHealth = 0; ///< ... and of the enemy's
	int lastChangeRound = 0;      ///< the last round in which one of the two was lower than ever before, or a hero cast
	int lastGainRound = 0;        ///< ... in which the enemy's was, or a hero cast
	int deadlockSince = -1;       ///< the round in which the battle was found to stand still (-1: it moves)
	bool deadlockStaged = false;  ///< (test, FAI_TEST_DEADLOCK) the battle counts as standing still for good
	void watchDeadlock(const BattleID & battleID);
	bool deadlockLost(const BattleID & battleID) const; ///< standing still, and attacking has not helped: give the battle up
	bool forecastLogged = false;
	bool spellCheckDone = false; ///< FAI_SPELLCHECK: the spell rules were compared with the game's in this battle
	/// (test) FairAI's own rules of what a spell does to a unit - used by forecasts outside a battle - against the game's
	void spellCheck(const BattleID & battleID);

	std::shared_ptr<Environment> env;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<CBattleAI> fallback;
	AutocombatPreferences preferences;
	BattleSide side = BattleSide::NONE;
	bool wasWaitingForRealize = false;
	bool spellSearch = false; ///< from the player's FairAI parameters (or FAI_SPELL_SEARCH)
	int spellMode = 0;        ///< 2: a spell is cast when it changes how the fight ends by more than its spell points are worth
	double manaShare = 0.15;  ///< what a full store of spell points is worth, as a share of the army
	bool spellForecast = false; ///< the forecast logged at the start of a battle plays our hero's damage spells (as the adventure AI's does)
	bool simAbilities = true; ///< from the player's FairAI parameters: the look-ahead knows special abilities
	bool battlePlan = false;  ///< from the player's FairAI parameters: candidates are judged by playing several rounds ahead, holding or advancing
	int guardOracle = 0;      ///< from the player's FairAI parameters: rounds the guards are played by the oracle (0 = off)
	bool ambushPlan = false;  ///< battlePlan 2: lying in wait is a third plan besides holding and advancing
	bool guardPlan = false;   ///< battlePlan 3: closing ranks around our shooter is a fourth
	bool cornerMove = false;  ///< battlePlan 4: a move into the nearer corner of our side is among the candidates
	int guardOracleUntil = 3; ///< ... in the first rounds of a battle only
	int guardModel = 0;       ///< from the player's FairAI parameters: the play-outs of the battle plan play guards by GuardModel
	bool ownCasters = false;  ///< from the player's FairAI parameters: our units that can cast are played by our own search
	bool modelSearch = false; ///< from the player's FairAI parameters: the plain search plays the enemy by GuardModel too
	int latePlan = 0;         ///< from the player's FairAI parameters: "wait, then advance" is one more plan (1: against troops that shoot, 2: always)
	int kitePlan = 0;         ///< from the player's FairAI parameters: "shoot and stay out of reach" is a fifth plan against guards
	static constexpr int KITE_ROUNDS = 40; ///< ... played this many rounds ahead (to the end of the battle, as a rule)
	static constexpr int KITE_TURNS = 6;   ///< ... and turning to fight is tried after each of this many rounds
	bool planVsPlayers = false; ///< from the player's FairAI parameters: the battle plan is also used against the army of a player

	bool needsFallback(const CStack * stack) const;
	bool tryHeroSpell(const BattleID & battleID, const CStack * stack);
	/// Siege attack, walkers only: while walls and gate still stand, attack defenders who came out
	/// (never from a moat hex) or wait outside instead of wading through the moat (VCMI's BattleAI
	/// lost whole armies that way). nullopt: there is a way in, let BattleAI lead the assault.
	std::optional<BattleAction> siegeWalkerAction(const BattleID & battleID, const CStack * stack) const;
	std::optional<BattleAction> considerRetreat(const BattleID & battleID);
	/// The enemy shoots and we have nothing that reaches it from afar - no shot, no caster, no hero with
	/// spell points: standing back only costs troops. (Not where the battle plan weighs holding against
	/// advancing by playing both out.)
	bool outranged(const BattleID & battleID) const;
	void logForecast(const BattleID & battleID);
	/// urgency: 0 = free choice, 1 = waiting and defending are no option, 2 = the unit must strike (or, with nobody in reach, advance)
	BattleAction searchUnitAction(const BattleID & battleID, const CStack * stack, int urgency = 0);

	struct UnitChoice
	{
		BattleAction action;
		double score = 0;
		std::string description;
	};
	/// Best action of `stack` in a (possibly hypothetical) battle state, with its look-ahead score
	/// roundsAhead < 0: the search setting; spells look further ahead (their effect lasts for rounds)
	/// urgency 1: waiting and defending are not an option (we attack and nothing has happened for rounds);
	/// 2: the battle stands still - only blows are, and where none is in reach the way to the enemy
	UnitChoice bestUnitAction(const std::shared_ptr<HypotheticBattle> & state, const CStack * stack, int roundsAhead = -1, int urgency = 0);
	/// Hero spells judged by the same look-ahead: every castable spell and target is applied to a
	/// hypothetical copy of the battle (the game's own spell mechanics) and the active unit's best
	/// continuation is compared with not casting; mana is priced by how scarce it is
	bool trySpellSearch(const BattleID & battleID, const CStack * stack);
	bool trySpellByOutcome(const BattleID & battleID, const CStack * stack);
	double outcomeScore(const std::shared_ptr<HypotheticBattle> & battle) const;
	bool spellSearchEnabled() const;
	/// A unit of ours that can cast: the cast it makes instead of what the search chose, if any
	std::optional<BattleAction> creatureSpell(const BattleID & battleID, const CStack * stack, const BattleAction & chosen);
};

}
