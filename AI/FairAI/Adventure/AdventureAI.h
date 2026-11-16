/*
 * AdventureAI.h, part of VCMI engine - FairAI
 *
 * FairAI's adventure-map logic (Phase 2: economy and exploration).
 *
 * Every hero repeatedly
 *   1. asks the engine's pathfinder (only revealed tiles) where it can go - once normally and
 *      once ignoring guards, to see what a guard is blocking,
 *   2. rates every known target - pickups, mines, dwellings, skill objects, weaker monsters
 *      (plus what they block), enemy towns and heroes, own towns with reinforcements,
 *      unexplored areas - by (value - expected losses) / (travel days + overhead),
 *   3. moves ONE tile towards the best target and starts over, so new information
 *      (revealed tiles, battles, enemy movement) is used immediately.
 * A small bonus for the target of the previous step prevents oscillation.
 *
 * Towns build by expected value per cost (prerequisites resolved automatically), recruit
 * for visiting main heroes, hire additional heroes as collectors that ferry their armies
 * to the main hero.
 *
 * All knowledge about foreign objects comes from Perception (fairness boundary). Object
 * values depend only on what a human sees: the object type, never hidden contents.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Params.h"

#include "../../../lib/int3.h"
#include "../../../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
class CCallback;
class ResourceSet;
class CGHeroInstance;
class CGTownInstance;
class CGObjectInstance;
class CGDwelling;
class CArmedInstance;
struct CPathsInfo;
struct CGPathNode;
struct Component;
VCMI_LIB_NAMESPACE_END

namespace FAI
{

class Perception;
class FairAIGateway;
struct ObservedObject;

/// A visible (or recently seen) enemy hero and how far it may move until our next turn
struct Threat
{
	int3 pos;
	double strength = 0;
	double radius = 0;
	int age = 0; ///< days since we saw it
	ObjectInstanceID hero;
};

/// Where the economy stands: is gold or creature supply the bottleneck?
struct Economy
{
	double weeklyIncome = 0;      ///< gold
	double weeklyRecruitCost = 0; ///< gold to buy one week of growth in all towns
	double rho = 1;               ///< (income + half the stock) / recruit cost; > 1: gold piles up
	double armyPremium = 1;       ///< army value multiplier when creatures, not gold, are scarce
	double goldWeight = 1;        ///< gold value multiplier (1 / armyPremium)
};

struct Target
{
	int3 tile;
	ObjectInstanceID object;  ///< invalid for exploration targets
	double value = 0;          ///< gold equivalent (before losses)
	double loss = 0;           ///< expected losses (gold equivalent)
	double travel = 0;         ///< days
	double score = 0;
	std::string description;
};

class AdventureAI
{
public:
	AdventureAI(FairAIGateway & gateway, std::shared_ptr<CCallback> cb, Perception & perception, PlayerColor me);
	~AdventureAI();

	/// Plays the adventure part of a turn. Called from the turn thread with the gamestate lock held.
	void playTurn();

	// ---- answers to queries (called from worker threads with the gamestate lock held)
	/// Hero level-up: index of the preferred skill in `skills` (0 if none)
	int chooseSkill(const CGHeroInstance * hero, const std::vector<SecondarySkill> & skills) const;
	/// Selection dialog (e.g. treasure chest gold vs experience): 1-based index
	int chooseSelection(const std::vector<Component> & components) const;
	/// Two of our heroes met: move troops to the main hero
	void onHeroExchange(ObjectInstanceID first, ObjectInstanceID second);
	/// A hero visits a dwelling and may recruit
	void recruitAtDwelling(const CGDwelling * dwelling, const CArmedInstance * destination);

	const Params & parameters() const { return params; }

private:
	FairAIGateway & gateway;
	std::shared_ptr<CCallback> cb;
	Perception & perception;
	PlayerColor me;
	Params params;

	ObjectInstanceID mainHero;
	std::atomic<int> activeHero{-1};                         ///< hero currently moving (for dialogs)
	std::map<ObjectInstanceID, ObjectInstanceID> reservedBy; ///< target object -> hero (this turn)
	std::map<ObjectInstanceID, int3> currentGoal;            ///< hero -> tile (stickiness)
	std::set<std::pair<int, int3>> failedToday;              ///< hero, tile
	std::map<ObjectInstanceID, std::set<int3>> trail;        ///< tiles each hero stood on today
	int today = 0;

	// ---- turn parts
	void manageTowns(bool endOfTurn);
	void hireHeroes();
	void playHero(ObjectInstanceID heroId);
	void defendTowns();

	Economy economy;
	void updateEconomy();

	// ---- threats (fair: only enemy heroes we saw today or yesterday)
	std::vector<Threat> threats;
	void updateThreats();
	/// Strongest enemy hero that might attack this tile before our next turn
	/// Cautious: weak heroes also fear enemies seen up to three days ago, in a wider radius
	double threatAt(const int3 & tile, ObjectInstanceID ignoreHero = ObjectInstanceID(), bool cautious = false) const;
	/// Tiles a collector may work on: close to one of our towns
	bool inTerritory(const int3 & tile) const;
	void dumpKnowledge() const;
	int lastHeroLossDay = -100;
	int idleDays = 0; ///< consecutive days the main hero hardly moved
	std::map<int, int> resourceNeed; ///< resource -> amount our best blocked buildings lack
	std::set<ObjectInstanceID> heroesLastTurn;
	std::set<ObjectInstanceID> usedTeleporters;
	/// An enemy hero was seen within `radius` tiles of `tile` during the last `days` days
	bool enemySeenNear(const int3 & tile, int days, double radius) const;
	/// Strength the hero would defend with on this tile (own town: garrison and walls help)
	double defenceAt(const CGHeroInstance * hero, const int3 & tile) const;

	// ---- helpers
	const CGHeroInstance * ownHero(ObjectInstanceID id) const;
	void chooseMainHero();
	bool isMain(const CGHeroInstance * hero) const;
	std::shared_ptr<CPathsInfo> computePaths(const CGHeroInstance * hero, bool ignoreGuards) const;

	std::vector<Target> collectTargets(const CGHeroInstance * hero, const CPathsInfo & paths, const CPathsInfo & freePaths);
	double objectValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const;
	double resourceValue(GameResID resource) const;
	double expValue(const CGHeroInstance * hero) const;

	double observedDanger(const ObservedObject & object) const;
	double guardDanger(const int3 & tile) const;
	/// Expected losses in gold equivalents, or a negative value if the fight is too risky
	double expectedLoss(const CGHeroInstance * hero, double danger, bool desperate = false, bool versusHero = false) const;

	/// Moves the hero one step towards `tile`. Returns false if no step could be made.
	bool stepTowards(const CGHeroInstance * hero, const int3 & tile, const CPathsInfo & paths);

	// ---- towns and armies
	void buildInTown(const CGTownInstance * town);
	double buildingValue(const CGTownInstance * town, BuildingID building) const;
	/// Buys the resources missing for `cost` with gold at a marketplace. True if `cost` is affordable now.
	bool buyResourcesFor(const CGTownInstance * town, const ResourceSet & cost);
	void recruitInTown(const CGTownInstance * town, const CArmedInstance * destination, int64_t goldReserve);
	int64_t recruitableValue(const CGTownInstance * town, int64_t gold) const;
	void manageTownVisit(const CGHeroInstance * hero, const CGTownInstance * town);
	/// Upgrades the hero's creatures in the town it visits
	void upgradeArmy(const CGHeroInstance * hero);
	/// Moves the strongest stacks from `source` to `destination` (keeps one unit on a source hero)
	void pickBestCreatures(const CArmedInstance * destination, const CArmedInstance * source);
	double transferableValue(const CGHeroInstance * from) const;
};

}
