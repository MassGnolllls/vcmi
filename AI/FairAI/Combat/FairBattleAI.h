/*
 * FairBattleAI.h, part of VCMI engine - FairAI
 *
 * FairAI's combat AI.
 *
 * Decision per unit:
 *  1. special units (catapult, first aid tent, berserk, creature spell casters) and the hero's
 *     spells are still handled by components of VCMI's BattleAI (compiled into FairAI)
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

VCMI_LIB_NAMESPACE_BEGIN
class CBattleCallback;
class CStack;
class BattleAction;
class Environment;
VCMI_LIB_NAMESPACE_END

class CBattleAI;

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

private:
	std::shared_ptr<Environment> env;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<CBattleAI> fallback;
	AutocombatPreferences preferences;
	BattleSide side = BattleSide::NONE;
	bool wasWaitingForRealize = false;

	bool needsFallback(const CStack * stack) const;
	bool tryHeroSpell(const BattleID & battleID, const CStack * stack);
	BattleAction searchUnitAction(const BattleID & battleID, const CStack * stack);
};

}
