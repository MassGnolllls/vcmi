/*
 * Status.h, part of VCMI engine - FairAI
 *
 * Tracks everything the turn thread has to wait for before it may issue the next
 * command: open queries (dialogs), ongoing battles, ongoing visits and movement.
 * Adapted from the AIStatus class of the Nullkiller AI (GPL v2+).
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/ConditionalWait.h"
#include "../../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGObjectInstance;
VCMI_LIB_NAMESPACE_END

namespace FAI
{

enum class BattleState
{
	NO_BATTLE,
	UPCOMING_BATTLE,
	ONGOING_BATTLE,
	ENDING_BATTLE
};

/// What our battle AI's simulator says about the battle in progress (for the retreat/surrender decision)
struct BattleVerdict
{
	bool lost = false;      ///< played on by the simulator - holding or advancing - we lose all, the enemy keeps a good part
	double ownValue = 0;    ///< AI value of what we still have on the field
	double enemyLoss = 0;   ///< AI value the enemy is expected to lose if we fight on
};
/// Battle AI and gateway of one player are separate objects created by the engine: they meet here
void setBattleVerdict(PlayerColor player, const BattleVerdict & verdict);
BattleVerdict battleVerdict(PlayerColor player);

class Status
{
	mutable std::mutex mx;
	std::condition_variable cv;

	BattleState battle = BattleState::NO_BATTLE;
	std::map<QueryID, std::string> remainingQueries;
	std::map<int, QueryID> requestToQueryID;
	std::vector<ObjectInstanceID> objectsBeingVisited;
	bool ongoingHeroMovement = false;
	bool havingTurn = false;

public:
	/// Set by the gateway: interrupting the turn thread (game over, shutdown)
	ThreadInterruption turnInterruption;

	void setBattle(BattleState state);
	BattleState getBattle() const;

	void addQuery(QueryID id, const std::string & description);
	void attemptedAnsweringQuery(QueryID id, int answerRequestID);
	void receivedAnswerConfirmation(int answerRequestID, bool success);
	int getQueriesCount() const;

	void heroVisit(const CGObjectInstance * obj, bool started);
	void setMove(bool ongoing);

	void startedTurn();
	void madeTurn();
	bool haveTurn() const;

	/// Blocks the turn thread until no dialog, battle, visit or movement is pending.
	/// Throws InterruptionRequestedException if the turn was interrupted meanwhile.
	void waitTillFree();

private:
	void removeQuery(QueryID id); // mx must be held
};

}
