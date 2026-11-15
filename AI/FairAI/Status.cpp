/*
 * Status.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Status.h"

#include "../../lib/mapObjects/CGObjectInstance.h"

namespace FAI
{

void Status::setBattle(BattleState state)
{
	std::unique_lock lock(mx);
	battle = state;
	cv.notify_all();
}

BattleState Status::getBattle() const
{
	std::unique_lock lock(mx);
	return battle;
}

void Status::addQuery(QueryID id, const std::string & description)
{
	if(id == QueryID(-1))
	{
		logAi->trace("FairAI: ignoring non-query '%s'", description);
		return;
	}

	std::unique_lock lock(mx);
	remainingQueries[id] = description;
	cv.notify_all();
	logAi->debug("FairAI: query %d added (%s), open queries: %d", id.getNum(), description, remainingQueries.size());
}

void Status::removeQuery(QueryID id)
{
	remainingQueries.erase(id);
	cv.notify_all();
	logAi->debug("FairAI: query %d answered, open queries: %d", id.getNum(), remainingQueries.size());
}

void Status::attemptedAnsweringQuery(QueryID id, int answerRequestID)
{
	std::unique_lock lock(mx);
	requestToQueryID[answerRequestID] = id;
}

void Status::receivedAnswerConfirmation(int answerRequestID, bool success)
{
	std::unique_lock lock(mx);
	auto it = requestToQueryID.find(answerRequestID);
	if(it == requestToQueryID.end())
		return;

	QueryID query = it->second;
	requestToQueryID.erase(it);

	if(success)
		removeQuery(query);
	else
		logAi->error("FairAI: server rejected answer to query %d (%s)", query.getNum(), remainingQueries[query]);
}

int Status::getQueriesCount() const
{
	std::unique_lock lock(mx);
	return static_cast<int>(remainingQueries.size());
}

void Status::heroVisit(const CGObjectInstance * obj, bool started)
{
	std::unique_lock lock(mx);
	if(started)
	{
		objectsBeingVisited.push_back(obj ? obj->id : ObjectInstanceID());
	}
	else if(!objectsBeingVisited.empty())
	{
		// start/end notifications are guaranteed to be stack-ordered
		objectsBeingVisited.pop_back();
	}
	cv.notify_all();
}

void Status::setMove(bool ongoing)
{
	std::unique_lock lock(mx);
	ongoingHeroMovement = ongoing;
	cv.notify_all();
}

void Status::startedTurn()
{
	std::unique_lock lock(mx);
	havingTurn = true;
	cv.notify_all();
}

void Status::madeTurn()
{
	std::unique_lock lock(mx);
	havingTurn = false;
	cv.notify_all();
}

bool Status::haveTurn() const
{
	std::unique_lock lock(mx);
	return havingTurn;
}

void Status::waitTillFree()
{
	std::unique_lock lock(mx);
	while(battle != BattleState::NO_BATTLE || !remainingQueries.empty() || !objectsBeingVisited.empty() || ongoingHeroMovement)
	{
		cv.wait_for(lock, std::chrono::milliseconds(10));
		turnInterruption.interruptionPoint();
	}
}

}
