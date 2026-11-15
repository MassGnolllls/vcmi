/*
 * FairAI.cpp, part of VCMI engine - FairAI
 *
 * Turn-thread and query handling patterns follow the Nullkiller AI gateway (GPL v2+).
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FairAI.h"

#include "Perception/Perception.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGObjectInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CTypeList.h"

namespace FAI
{

FairAIGateway::FairAIGateway()
	: asyncTasks(std::make_unique<AsyncRunner>())
{
}

FairAIGateway::~FairAIGateway()
{
	finish();
}

std::string FairAIGateway::getBattleAIName() const
{
	// Arena runs may assign a combat AI per player (harness configuration, not game information)
	const JsonNode & list = settings["session"]["arena"]["battleAI"];
	if(list.isVector() && playerID.isValidPlayer() && playerID.getNum() < static_cast<int>(list.Vector().size()))
	{
		const std::string & name = list.Vector()[playerID.getNum()].String();
		if(!name.empty())
			return name;
	}

	// Phase 0: VCMI's BattleAI fights for us. Phase 1 replaces this with FairAI's own combat module.
	return "BattleAI";
}

void FairAIGateway::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	cb = CB;
	cbc = CB;
	env = ENV;
	human = false;
	playerID = *cb->getPlayerID();
	cb->waitTillRealize = true;

	perception = std::make_unique<Perception>(cb, playerID);
	logAi->info("FairAI initialised for player %s", playerID.toString());
}

// ------------------------------------------------------------------ turn

void FairAIGateway::yourTurn(QueryID queryID)
{
	status.addQuery(queryID, "YourTurn");
	executeActionAsync("yourTurn", [this, queryID]() { answerQuery(queryID, 0); });
	status.startedTurn();
	status.turnInterruption.reset();

	asyncTasks->run([this]()
	{
		ScopedThreadName guard("FairAI::makingTurn");
		try
		{
			status.waitTillFree();
			makeTurn();
		}
		catch(const InterruptionRequestedException &)
		{
			logAi->debug("FairAI: turn thread interrupted");
		}
		catch(const TerminationRequestedException &)
		{
			logAi->debug("FairAI: turn thread terminated");
		}
		catch(const std::exception & e)
		{
			logAi->error("FairAI: turn failed with exception: %s", e.what());
			// Never leave the game hanging: end the turn even after an internal error.
			endTurn();
		}
	});
}

void FairAIGateway::makeTurn()
{
	std::shared_lock gsLock(CGameState::mutex);

	const int day = cb->getDate(Date::DAY);
	perception->refresh();
	logAi->info("FairAI %s day %d: %s", playerID.toString(), day, perception->summary());

	// Phase 0: no adventure-map decisions yet (Phase 2/3 plug in here).

	endTurn();
}

void FairAIGateway::endTurn()
{
	if(cb->getPlayerStatus(playerID) != EPlayerStatus::INGAME)
		return;

	do
	{
		cb->endTurn();
		status.turnInterruption.interruptionPoint();
	}
	while(status.haveTurn()); // a request may fail: keep asking until the server confirms
}

// ------------------------------------------------------------------ queries

void FairAIGateway::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills, QueryID queryID)
{
	status.addQuery(queryID, "HeroGotLevel");
	executeActionAsync("heroGotLevel", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	status.addQuery(queryID, "CommanderGotLevel");
	executeActionAsync("commanderGotLevel", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	status.addQuery(askID, "BlockingDialog");

	// Yes/no questions are answered "yes" (1) - FairAI only walks into objects it wants to visit.
	// Selections pick the last option (components are indexed 1..n).
	int answer = 0;
	if(selection)
		answer = static_cast<int>(components.size());
	else if(cancel)
		answer = 1;

	executeActionAsync("showBlockingDialog", [this, askID, answer]() { answerQuery(askID, answer); });
}

void FairAIGateway::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle)
{
	status.addQuery(queryID, "GarrisonDialog");
	executeActionAsync("showGarrisonDialog", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID)
{
	status.addQuery(askID, "TeleportDialog");
	executeActionAsync("showTeleportDialog", [this, askID]() { answerQuery(askID, -1); });
}

void FairAIGateway::showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	status.addQuery(askID, "MapObjectSelectDialog");
	int choice = objects.empty() ? -1 : objects.front().getNum();
	executeActionAsync("showMapObjectSelectDialog", [this, askID, choice]() { answerQuery(askID, choice); });
}

std::optional<BattleAction> FairAIGateway::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return std::nullopt;
}

void FairAIGateway::showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID)
{
	status.addQuery(queryID, "TavernWindow");
	executeActionAsync("showTavernWindow", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID)
{
	status.addQuery(queryID, "MarketWindow");
	executeActionAsync("showMarketWindow", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID)
{
	status.addQuery(queryID, "UniversityWindow");
	executeActionAsync("showUniversityWindow", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QueryID queryID)
{
	status.addQuery(queryID, "RecruitmentDialog");
	executeActionAsync("showRecruitmentDialog", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID)
{
	status.addQuery(queryID, "HeroExchange");
	executeActionAsync("heroExchangeStarted", [this, queryID]() { answerQuery(queryID, 0); });
}

void FairAIGateway::answerQuery(QueryID queryID, int selection)
{
	if(queryID != QueryID(-1))
		cb->selectionMade(selection, queryID);
}

void FairAIGateway::executeActionAsync(const std::string & description, const std::function<void()> & whatToDo)
{
	if(!asyncTasks)
		throw std::runtime_error("FairAI: attempt to execute task on shut down AI");

	asyncTasks->run([description, whatToDo]() noexcept
	{
		ScopedThreadName guard("FairAI::" + description);
		std::shared_lock gsLock(CGameState::mutex);
		try
		{
			whatToDo();
		}
		catch(const TerminationRequestedException &)
		{
			logAi->debug("FairAI: %s terminated", description);
		}
		catch(const std::exception & e)
		{
			logAi->error("FairAI: %s failed: %s", description, e.what());
		}
	});
}

// ------------------------------------------------------------------ bookkeeping

void FairAIGateway::requestSent(const CPackForServer * pack, int requestID)
{
	if(auto reply = dynamic_cast<const QueryReply *>(pack))
		status.attemptedAnsweringQuery(reply->qid, requestID);
}

void FairAIGateway::requestRealized(PackageApplied * pa)
{
	if(status.haveTurn() && pa->packType == CTypeList::getInstance().getTypeID<EndTurn>(nullptr) && pa->result)
		status.madeTurn();

	if(pa->packType == CTypeList::getInstance().getTypeID<QueryReply>(nullptr))
		status.receivedAnswerConfirmation(pa->requestID, pa->result);
}

void FairAIGateway::heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start)
{
	status.heroVisit(visitedObj, start);
}

void FairAIGateway::tileRevealed(const FowTilesType & pos)
{
	if(perception)
		perception->onTilesRevealed(pos);
}

void FairAIGateway::tileHidden(const FowTilesType & pos)
{
	if(perception)
		perception->onTilesHidden(pos);
}

void FairAIGateway::objectRemoved(const CGObjectInstance * obj, const PlayerColor & initiator)
{
	if(perception && obj)
		perception->onObjectRemoved(obj->id);
}

void FairAIGateway::gameOver(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult)
{
	if(player == playerID)
	{
		logAi->info("FairAI %s: game over, %s", playerID.toString(), victoryLossCheckResult.victory() ? "victory" : "defeat");
		status.turnInterruption.interruptThread();
	}
}

void FairAIGateway::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed)
{
	status.setBattle(BattleState::ONGOING_BATTLE);
	CAdventureAI::battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void FairAIGateway::battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID)
{
	status.setBattle(BattleState::ENDING_BATTLE);
	CAdventureAI::battleEnd(battleID, br, queryID);

	if(queryID != QueryID::NONE && cb->getPlayerState(playerID)->isHuman())
	{
		// only happens when FairAI auto-plays for a human player
		status.addQuery(queryID, "ConfirmBattleResult");
		executeActionAsync("battleEnd", [this, queryID]() { answerQuery(queryID, 0); });
	}
}

void FairAIGateway::battleEnded()
{
	status.setBattle(BattleState::NO_BATTLE);
}

void FairAIGateway::finish()
{
	status.turnInterruption.interruptThread();
	if(asyncTasks)
	{
		asyncTasks->wait();
		asyncTasks.reset();
	}
}

}
