/*
 * Perception.h, part of VCMI engine - FairAI
 *
 * The fairness boundary of FairAI.
 *
 * Rule: FairAI may only know what a human player in the same seat could see in the UI.
 *  - Own heroes, towns and armies: full information (the human sees them in their own windows).
 *  - Everything else is handed out as an *observation*, never as a raw game object pointer:
 *      * enemy heroes / towns: what the UI shows (cb->getHeroInfo / cb->getTownInfo),
 *        i.e. exact numbers only where the game itself grants details (e.g. Thieves Guild)
 *      * wandering monsters / garrisons: quantity categories ("Pack" = 10-19, ...)
 *      * other guarded objects: unknown until the game reveals the guards
 *      * what a building gives: the public table of its kind (RewardTable) - what it can hold and
 *        how often - never what this particular building on the map holds; the guards of a creature
 *        bank only once one of our heroes has stood before it and the game lists them
 *  - Tiles: only revealed tiles. When tiles get hidden again (Cover of Darkness), the
 *    memory keeps the last observation with its timestamp and flags the area as stale.
 *
 * All adventure-map and strategic code must read foreign state through this class.
 * The arena's fairness check enforces that no FairAI code outside Perception/ touches
 * the full game state (see arena/check_fairness.py).
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../../lib/int3.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/rewardable/Configuration.h"

VCMI_LIB_NAMESPACE_BEGIN
class CCallback;
class CArmedInstance;
class CGHeroInstance;
class CGTownInstance;
class CGObjectInstance;
struct ArmyDescriptor;
VCMI_LIB_NAMESPACE_END

namespace FAI
{

/// One stack as a human would perceive it. For exact knowledge minCount == maxCount.
struct ObservedStack
{
	CreatureID creature;
	int minCount = 0;
	int maxCount = 0;
	bool exact = false;
};

/// Army as a human would perceive it.
struct ObservedArmy
{
	std::vector<ObservedStack> units;
	bool known = false; ///< false: the composition could not be observed at all

	uint64_t minStrength() const;
	uint64_t maxStrength() const;
	uint64_t expectedStrength() const;

	/// Build from what the game's info callbacks report (exact if detailed, quantity ranges otherwise)
	static ObservedArmy fromDescriptor(const ArmyDescriptor & descriptor);
	/// Exact copy - only legal for our own armies
	static ObservedArmy exactOwn(const CArmedInstance * army);
	/// Quantity category (1 = Few ... 9 = Legion) to count range, as shown by the game
	static std::pair<int, int> quantityRange(int quantityId);
};

/// One state a guarded building - a creature bank - can be in: who guards it, and how often a
/// building of this kind is found in that state
struct GuardState
{
	double chance = 0;
	std::vector<std::pair<CreatureID, int>> guards; ///< stack by stack, in the order the battle places them
	std::vector<int> samples;                       ///< RewardTable::samples that are in this state
	uint64_t strength = 0;                          ///< AI value of the guards
};

/// What a visit can bring in one of the buildings the table can produce: the rewards a first visit
/// offers, each with its conditions, as the engine reads the table
struct RewardSample
{
	std::vector<Rewardable::VisitInfo> visits;
	int weight = 1; ///< how many of the rolled buildings look exactly like this
};

/// The public table of one kind of building that gives something - a creature bank, a well, an
/// observatory - turned into the buildings it can produce: a hundred of them, one for every face of
/// the game's hundred-sided die, rolled with dice of our own. This is what a player knows from the
/// rules and from experience. It says nothing about what a particular building on the map holds.
struct RewardTable
{
	std::vector<RewardSample> samples;
	int total = 0;                  ///< sum of the weights
	bool guarded = false;           ///< some of them are guarded
	std::vector<GuardState> states; ///< guarded kinds only, weakest first; the chances sum to 1
	bool forceCombat = false;       ///< the fight begins without the game asking
	std::string layout;             ///< battle layout of the fight against the guards
	Rewardable::EVisitMode visitMode = Rewardable::VISIT_UNLIMITED;
	Rewardable::ESelectMode selectMode = Rewardable::SELECT_FIRST;
};

/// Last known state of a map object.
struct ObservedObject
{
	ObjectInstanceID id;
	MapObjectID type;
	MapObjectSubID subtype;
	PlayerColor owner;
	int3 visitablePos;
	int lastSeenDay = -1;
	bool visibleNow = false;
	std::optional<ObservedArmy> army;
	/// heroes: attack, defence, spell power, knowledge - only if the game shows them to us in the
	/// hero's info window (it does not for an ordinary enemy hero)
	std::optional<std::array<int, 4>> heroSkills;
	/// buildings that give something: the public table of their kind (nothing about this one)
	std::shared_ptr<const RewardTable> table;
	/// guarded buildings one of our heroes has stood before: the game lists the guards on a right
	/// click from then on (game setting; on by default). Listed and none shown: nobody guards it any more
	bool guardsShown = false;
	std::vector<std::pair<CreatureID, int>> shownGuards; ///< creature and number, as listed
	/// universities one of our heroes has been to: the skills taught there (a right click lists them)
	std::vector<SecondarySkill> shownSkills;
};

/// The best hero of another player as our thieves' guild shows it (tavern window)
struct GuildHero
{
	HeroTypeID hero;                            ///< whose portrait is shown
	std::optional<std::array<int, 4>> skills;   ///< shown from two taverns on
};

/// What the thieves' guild window shows (as far as FairAI uses it)
struct GuildInfo
{
	bool available = false;                     ///< we own a tavern
	std::map<PlayerColor, GuildHero> bestHero;
	std::map<PlayerColor, int> townPlace;       ///< place in the ranking by number of towns (0 = most; ties share a place)
};

class Perception
{
public:
	Perception(std::shared_ptr<CCallback> cb, PlayerColor me);

	/// ---- own side (full knowledge) ----
	std::vector<const CGHeroInstance *> ownHeroes() const;
	std::vector<const CGTownInstance *> ownTowns() const;
	bool isOwn(const CGObjectInstance * obj) const;

	/// ---- world memory (only what has been seen) ----
	/// Rescan everything currently visible and update the memory. Call with the gamestate lock held.
	void refresh();
	/// Tiles that became hidden again, e.g. by a Cover of Darkness
	void onTilesHidden(const std::set<int3> & tiles);
	void onTilesRevealed(const std::set<int3> & tiles);
	void onObjectRemoved(ObjectInstanceID id);

	std::map<ObjectInstanceID, ObservedObject> knownObjects() const;
	/// The live object if we can see it right now (or own it), else nullptr
	const CGObjectInstance * visibleObject(ObjectInstanceID id) const;

	/// Tiles that we saw once but are hidden from us now
	std::set<int3> staleTiles() const;
	bool isVisibleNow(const int3 & tile) const;

	/// Perceive an army the way the UI would show it to us
	ObservedArmy observeArmy(const CArmedInstance * army) const;

	/// The public table of this kind of building (by its type alone); nullptr if it has none
	std::shared_ptr<const RewardTable> rewardTable(const CGObjectInstance * obj) const;

	/// What the thieves' guild in one of our taverns tells about the other players' best heroes.
	/// Only what the window shows: the portrait and, with enough taverns, the primary skills.
	GuildInfo thievesGuild() const;

	/// Short status line for logs
	std::string summary() const;

private:
	std::shared_ptr<CCallback> cb;
	PlayerColor me;

	mutable std::mutex mx;
	std::map<ObjectInstanceID, ObservedObject> objects;
	std::set<int3> hiddenAgain;

	mutable std::mutex tableMx;
	mutable std::map<std::pair<int, int>, std::shared_ptr<const RewardTable>> tables; ///< object type, subtype -> table (nullptr: none)
};

}
