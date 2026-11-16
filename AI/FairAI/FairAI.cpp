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
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/mapObjects/CGTownInstance.h"

#include "Perception/Perception.h"
#include "Adventure/AdventureAI.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/spells/CSpellHandler.h"
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

	// FairAI's own combat module (same library)
	return "FairAI";
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
	adventure = std::make_unique<AdventureAI>(*this, cb, *perception, playerID);
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

	adventure->playTurn();

	endTurn();
}

void FairAIGateway::waitTillFree()
{
	auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
	status.waitTillFree();
}

void FairAIGateway::checkInterruption()
{
	status.turnInterruption.interruptionPoint();
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
	int choice = adventure ? adventure->chooseSkill(hero, skills) : 0;
	executeActionAsync("heroGotLevel", [this, queryID, choice]() { answerQuery(queryID, choice); });
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
		answer = adventure ? adventure->chooseSelection(components) : static_cast<int>(components.size());
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
	// A lost battle costs the army either way; fleeing keeps the hero (level, skills, artifacts),
	// who can be hired again. Without a town there is nothing to flee to.
	if(!battleState.canFlee || !battleState.ourHero || cb->getTownsInfo().empty())
		return std::nullopt;
	// A battle our simulator sees as lost: buying the army free keeps all of it - if we have the gold
	// and the army is worth more to us than that gold (and than what it would still kill).
	const BattleVerdict verdict = battleVerdict(playerID);
	if(verdict.lost && battleState.canSurrender && adventure)
	{
		auto battle = cb->getBattle(battleID);
		const int cost = battle ? battle->battleGetSurrenderCost() : -1;
		if(cost >= 0 && adventure->surrenderWorth(cost, verdict.ownValue, verdict.enemyLoss))
		{
			logAi->info("FairAI %s: %s surrenders a lost battle for %d gold (army value %.0f, would still kill %.0f)", playerID.toString(),
				battleState.ourHero->getNameTranslated(), cost, verdict.ownValue, verdict.enemyLoss);
			adventure->noteSurrender(battleState.ourHero->getHeroTypeID());
			return BattleAction::makeSurrender(battleState.ourSide);
		}
	}
	const double ours = static_cast<double>(battleState.getOurStrength());
	const double theirs = std::max<double>(1.0, static_cast<double>(battleState.getEnemyStrength()));
	if(ours / theirs < 0.25 && battleState.ourHero->level >= 4)
	{
		logAi->info("FairAI %s: %s retreats from a lost battle (%.0f vs %.0f)", playerID.toString(), battleState.ourHero->getNameTranslated(), ours, theirs);
		return BattleAction::makeRetreat(battleState.ourSide);
	}
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
	executeActionAsync("showRecruitmentDialog", [this, queryID, dwelling, dst]()
	{
		if(adventure)
			adventure->recruitAtDwelling(dwelling, dst);
		answerQuery(queryID, 0);
	});
}

void FairAIGateway::heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID)
{
	status.addQuery(queryID, "HeroExchange");
	executeActionAsync("heroExchangeStarted", [this, queryID, hero1, hero2]()
	{
		if(adventure)
			adventure->onHeroExchange(hero1, hero2);
		answerQuery(queryID, 0);
	});
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
	// diagnostics: who fights whom, with which (own, exact) strength
	{
		auto describe = [](const CCreatureSet * army, const CGHeroInstance * hero)
		{
			std::string who = hero ? hero->getNameTranslated() : std::string("army");
			return who + " (" + std::to_string(army ? army->getArmyStrength() : 0) + ")";
		};
		const bool weAttack = side == BattleSide::ATTACKER;
		logAi->info("FairAI %s battle at %s: %s %s %s", playerID.toString(), tile.toString(),
			describe(weAttack ? army1 : army2, weAttack ? hero1 : hero2), weAttack ? "attacks" : "is attacked by",
			weAttack ? (hero2 ? hero2->getNameTranslated() : std::string("guards")) : (hero1 ? hero1->getNameTranslated() : std::string("monsters")));
		battleSide = side;
		battleOwnStrength = weAttack ? (army1 ? army1->getArmyStrength() : 0) : (army2 ? army2->getArmyStrength() : 0);
		battleEnemyStrength = weAttack ? (army2 ? army2->getArmyStrength() : 0) : (army1 ? army1->getArmyStrength() : 0);

		// calibration (log only, never used for decisions): what the UI showed vs. the real enemy
		const CCreatureSet * enemyArmy = weAttack ? army2 : army1;
		const CGHeroInstance * enemyHero = weAttack ? hero2 : hero1;
		const auto * enemyArmed = dynamic_cast<const CArmedInstance *>(enemyArmy);
		if(enemyArmed && perception)
		{
			ObservedArmy seen = perception->observeArmy(enemyArmed);
			logAi->info("FairAI %s calibration: enemy %s observed %llu..%llu (expected %llu), actual %llu, hero factor %.2f",
				playerID.toString(), enemyHero ? "hero" : "army", (unsigned long long)seen.minStrength(), (unsigned long long)seen.maxStrength(),
				(unsigned long long)seen.expectedStrength(), (unsigned long long)enemyArmy->getArmyStrength(), enemyHero ? enemyHero->getFightingStrength() : 1.0);
		}
	}
	CAdventureAI::battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
	if(auto battle = cb->getBattle(battleID))
	{
		// In battle the game shows the enemy hero's primary skills (its window on the battlefield):
		// remembered for the next time we judge a fight against that hero.
		const CGHeroInstance * enemyHero = side == BattleSide::ATTACKER ? hero2 : hero1;
		battleEnemyHero = enemyHero ? enemyHero->getHeroTypeID().getNum() : -1;
		if(enemyHero && adventure)
		{
			const InfoAboutHero shown = battle->battleGetHeroInfo(side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER);
			if(shown.details && shown.details->primskills.size() >= 4)
				adventure->noteHeroSkills(enemyHero->getHeroTypeID(), {shown.details->primskills[0], shown.details->primskills[1], shown.details->primskills[2], shown.details->primskills[3]});
		}
		if(const auto * town = battle->battleGetDefendedTown(); town && town->fortLevel() > CGTownInstance::NONE)
			logAi->info("FairAI %s battle is a siege of %s (fort level %d)", playerID.toString(), town->getNameTranslated(), static_cast<int>(town->fortLevel()));
	}
}

void FairAIGateway::actionStarted(const BattleID & battleID, const BattleAction & action)
{
	if(logAi->isDebugEnabled())
	{
		if(auto battle = cb->getBattle(battleID))
		{
			const auto * unit = battle->battleGetStackByID(action.stackNumber, false);
			logAi->debug("FairAI %s battle action: %s %s", playerID.toString(),
				unit ? unit->getDescription() : std::string("hero/other"), action.toString());
		}
	}
	CAdventureAI::actionStarted(battleID, action);
}

void FairAIGateway::battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	if(logAi->isDebugEnabled())
	{
		if(auto battle = cb->getBattle(battleID))
		{
			for(const auto & hit : bsa)
			{
				const auto * target = battle->battleGetStackByID(hit.stackAttacked, false);
				const auto * attacker = battle->battleGetStackByID(hit.attackerID, false);
				logAi->debug("FairAI %s battle hit: %s -> %s, damage %lld, killed %u%s", playerID.toString(),
					attacker ? attacker->getDescription() : std::string("?"), target ? target->getDescription() : std::string("?"),
					static_cast<long long>(hit.damageAmount), hit.killedAmount, ranged ? " (ranged)" : "");
			}
		}
	}
	CAdventureAI::battleStacksAttacked(battleID, bsa, ranged);
}

void FairAIGateway::battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc)
{
	if(sc)
	{
		logAi->info("FairAI %s battle spell: %s %s casts %s on %d units", playerID.toString(), sc->side == battleSide ? "our" : "enemy",
			sc->castByHero ? "hero" : "creature", sc->spellID.toSpell()->getNameTranslated(), static_cast<int>(sc->affectedCres.size()));
		// what an enemy hero casts in front of us is remembered: a hero that slows a whole army is
		// worth far more than its army and its attack and defence say
		if(sc->castByHero && sc->side != battleSide && battleEnemyHero >= 0 && adventure)
			adventure->noteHeroSpell(HeroTypeID(battleEnemyHero), sc->spellID, static_cast<int>(sc->affectedCres.size()));
	}
	CAdventureAI::battleSpellCast(battleID, sc);
}

void FairAIGateway::battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID)
{
	status.setBattle(BattleState::ENDING_BATTLE);
	if(br)
	{
		int lost = 0;
		uint64_t lostValue = 0;
		for(const auto & entry : br->casualties[battleSide])
		{
			lost += entry.second;
			lostValue += static_cast<uint64_t>(entry.second) * entry.first.toCreature()->getAIValue();
		}
		logAi->info("FairAI %s battle result: %s, %d of our creatures died (value %llu of %llu, enemy %llu)", playerID.toString(),
			br->winner == battleSide ? "won" : "lost", lost, (unsigned long long)lostValue, (unsigned long long)battleOwnStrength,
			(unsigned long long)battleEnemyStrength);
		if(battleEnemyHero >= 0 && adventure)
		{
			// the result window shows both sides' casualties: what is left of that hero's army is known
			const BattleSide other = battleSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
			uint64_t enemyLost = 0;
			for(const auto & entry : br->casualties[other])
				enemyLost += static_cast<uint64_t>(entry.second) * entry.first.toCreature()->getAIValue();
			const double remaining = battleEnemyStrength > 0 ? 1.0 - std::min(1.0, static_cast<double>(enemyLost) / static_cast<double>(battleEnemyStrength)) : 0.0;
			adventure->noteBattleOutcome(HeroTypeID(battleEnemyHero), br->winner == battleSide, remaining,
				static_cast<double>(battleOwnStrength), static_cast<double>(battleEnemyStrength));
		}
	}
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
