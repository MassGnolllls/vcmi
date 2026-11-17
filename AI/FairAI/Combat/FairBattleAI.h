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

private:
	struct SimCheck;
	std::unique_ptr<SimCheck> pendingCheck;
	std::map<uint32_t, int64_t> lastHealth; ///< unit -> health after the previous action (changes in between: regeneration, poison...)
	bool lastActionWasSpell = false;
	int quietDecisions = 0; ///< our decisions since anybody was hurt (the attacker must not let a battle stand still)
	bool forecastLogged = false;

	std::shared_ptr<Environment> env;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<CBattleAI> fallback;
	AutocombatPreferences preferences;
	BattleSide side = BattleSide::NONE;
	bool wasWaitingForRealize = false;
	bool spellSearch = false; ///< from the player's FairAI parameters (or FAI_SPELL_SEARCH)
	int spellMode = 0;        ///< 2: a spell is cast when it changes how the fight ends by more than its spell points are worth
	double manaShare = 0.15;  ///< what a full store of spell points is worth, as a share of the army
	bool simAbilities = true; ///< from the player's FairAI parameters: the look-ahead knows special abilities
	bool battlePlan = false;  ///< from the player's FairAI parameters: candidates are judged by playing several rounds ahead, holding or advancing

	bool needsFallback(const CStack * stack) const;
	bool tryHeroSpell(const BattleID & battleID, const CStack * stack);
	/// Siege attack, walkers only: while walls and gate still stand, attack defenders who came out
	/// (never from a moat hex) or wait outside instead of wading through the moat (VCMI's BattleAI
	/// lost whole armies that way). nullopt: there is a way in, let BattleAI lead the assault.
	std::optional<BattleAction> siegeWalkerAction(const BattleID & battleID, const CStack * stack) const;
	std::optional<BattleAction> considerRetreat(const BattleID & battleID);
	void logForecast(const BattleID & battleID);
	BattleAction searchUnitAction(const BattleID & battleID, const CStack * stack, bool forbidPassive = false);

	struct UnitChoice
	{
		BattleAction action;
		double score = 0;
		std::string description;
	};
	/// Best action of `stack` in a (possibly hypothetical) battle state, with its look-ahead score
	/// roundsAhead < 0: the search setting; spells look further ahead (their effect lasts for rounds)
	/// forbidPassive: waiting and defending are not an option (we attack and nothing has happened for rounds)
	UnitChoice bestUnitAction(const std::shared_ptr<HypotheticBattle> & state, const CStack * stack, int roundsAhead = -1, bool forbidPassive = false);
	/// Hero spells judged by the same look-ahead: every castable spell and target is applied to a
	/// hypothetical copy of the battle (the game's own spell mechanics) and the active unit's best
	/// continuation is compared with not casting; mana is priced by how scarce it is
	bool trySpellSearch(const BattleID & battleID, const CStack * stack);
	bool trySpellByOutcome(const BattleID & battleID, const CStack * stack);
	double outcomeScore(const std::shared_ptr<HypotheticBattle> & battle) const;
	bool spellSearchEnabled() const;
};

}
