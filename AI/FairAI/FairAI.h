/*
 * FairAI.h, part of VCMI engine - FairAI
 *
 * FairAI: an adventure + combat AI for VCMI that plays without cheats.
 * No map reveal, no resource bonuses beyond what the difficulty gives humans,
 * no knowledge a human player in the same seat would not have (see Perception/).
 *
 * Phase 0 skeleton: correct event/query handling and turn thread; the turn itself
 * only refreshes the perception memory and ends the turn. Combat is delegated to
 * VCMI's BattleAI until FairAI's own combat module lands (Phase 1).
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/callback/CAdventureAI.h"
#include "Status.h"

VCMI_LIB_NAMESPACE_BEGIN
class AsyncRunner;
VCMI_LIB_NAMESPACE_END

namespace FAI
{

class Perception;

class DLL_EXPORT FairAIGateway : public CAdventureAI
{
public:
	FairAIGateway();
	~FairAIGateway() override;

	// ---- CAdventureAI / CGameInterface ----
	std::string getBattleAIName() const override;
	void initGameInterface(std::shared_ptr<Environment> env, std::shared_ptr<CCallback> CB) override;
	void yourTurn(QueryID queryID) override;
	void finish() override;

	void heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills, QueryID queryID) override;
	void commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID) override;
	void showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept) override;
	void showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle) override;
	void showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID) override;
	void showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects) override;
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState) override;

	// ---- events that carry a query and must be answered ----
	void showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID) override;
	void showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID) override;
	void showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID) override;
	void showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QueryID queryID) override;
	void heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID) override;

	// ---- bookkeeping events ----
	void requestSent(const CPackForServer * pack, int requestID) override;
	void requestRealized(PackageApplied * pa) override;
	void heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start) override;
	void tileRevealed(const FowTilesType & pos) override;
	void tileHidden(const FowTilesType & pos) override;
	void objectRemoved(const CGObjectInstance * obj, const PlayerColor & initiator) override;
	void gameOver(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult) override;

	// ---- battle bookkeeping (combat itself is forwarded by CAdventureAI) ----
	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	void battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID) override;
	void battleEnded() override;

private:
	std::shared_ptr<CCallback> cb;
	std::unique_ptr<AsyncRunner> asyncTasks;
	std::unique_ptr<Perception> perception;
	Status status;

	void makeTurn();
	void endTurn();
	void answerQuery(QueryID queryID, int selection);
	/// Runs on a worker thread with a shared gamestate lock. Needed because requests
	/// may not be sent from the thread that delivers game events.
	void executeActionAsync(const std::string & description, const std::function<void()> & whatToDo);
};

}
