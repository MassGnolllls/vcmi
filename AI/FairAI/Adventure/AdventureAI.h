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
 * values depend only on what a human sees: the object type, never hidden contents. What a
 * building can hold comes from the public table of its kind (Perception::rewardTable).
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Params.h"
#include <array>
#include "../../../lib/ResourceSet.h"

#include "../../../lib/int3.h"
#include "../../../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
class CArtifact;
class CCallback;
class ResourceSet;
class CGHeroInstance;
class CGTownInstance;
class CGObjectInstance;
class CGDwelling;
class CArmedInstance;
class IMarket;
struct CPathsInfo;
struct CGPathNode;
struct Component;
class CStackBasicDescriptor;
namespace Rewardable
{
struct Reward;
struct RewardRevealTiles;
}
VCMI_LIB_NAMESPACE_END

namespace FAI
{

namespace Combat
{
struct HeroSpec;
}

class Perception;
class FairAIGateway;
struct ObservedObject;
struct RewardTable;

/// A visible (or recently seen) enemy hero and how far it may move until our next turn
struct Threat
{
	int3 pos;
	double strength = 0;
	double radius = 0;
	int age = 0; ///< days since we saw it
	ObjectInstanceID hero;
};

/// Walking distances of one enemy hero, judged from our map knowledge (see AdventureAI::enemyDistance)
struct ReachMap
{
	int3 origin{-1, -1, -1};
	int day = -1;
	double maxTiles = 0;
	double strength = 0;
	std::vector<float> tiles; ///< tile units (100 movement points) per tile, all levels; < 0: not reached
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
	/// what holding a secondary skill at a level is worth to this hero in its role (parameter skillChoice)
	double skillValue(const CGHeroInstance * hero, SecondarySkill skill, int level) const;
	double skillValueAs(const CGHeroInstance * hero, SecondarySkill skill, int level, bool fighter) const;
	/// 0: a pure warrior ... 1: a hero whose fighting rests on spells
	double magicReliance(const CGHeroInstance * hero) const;
	/// what carrying an artifact is worth to this hero (negative: harmful), as leader of the army or as one of the others
	double artifactValue(const CGHeroInstance * hero, const CArtifact * art, bool fighter) const;
	/// wear the best per slot, take off what harms, assemble sets (parameter artifactCare)
	void arrangeArtifacts(const CGHeroInstance * hero);
	/// two of our heroes meet: artifacts go where they do more good
	void exchangeArtifacts(const CGHeroInstance * leader, const CGHeroInstance * other);
	/// Selection dialog (e.g. treasure chest gold vs experience): 1-based index
	int chooseSelection(const std::vector<Component> & components) const;
	/// Two of our heroes met: move troops to the main hero
	void onHeroExchange(ObjectInstanceID first, ObjectInstanceID second);
	/// A hero visits a dwelling and may recruit
	void recruitAtDwelling(const CGDwelling * dwelling, const CArmedInstance * destination);
	/// The game asks whether to go on - at a guarded building it shows the guards: true = yes (parameter tables)
	bool confirmVisit(const std::vector<Component> & components);
	/// A building hands over creatures and the army has no free slot: the exchange window is open
	void onGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down);
	/// A hero stands in a university: buy the skill that is worth its price (parameter sites)
	void onUniversity(const IMarket * market, const CGHeroInstance * visitor);
	/// A hero stands at a market on the map (a black market sells artifacts): parameter sites
	void onMarket(const IMarket * market, const CGHeroInstance * visitor);

	const Params & parameters() const { return params; }

	/// The game showed us a hero's primary skills (attack, defence, spell power, knowledge): in a
	/// battle against it or in our thieves' guild. Remembered with the day.
	void noteHeroSkills(HeroTypeID hero, const std::array<int, 4> & skills);
	/// (log only) what we took an enemy hero for before a battle shows it: army and hero factor as estimated
	std::string describeEstimate(HeroTypeID hero) const;
	/// a battle against an enemy hero is over: what we know of its army is what the battle left of it
	void noteBattleOutcome(HeroTypeID hero, bool gone, double remainingShare, double ownStrength, double enemyStrength, const int3 & pos,
		int ownHero = -1, bool weAttack = false, bool siege = false, double ownLostShare = 0, int day = 0);
	/// (fortress) the strength of an enemy army seen near us lately that outmatches this hero (0: none)
	double outmatchedBy(const CGHeroInstance * hero) const;
	int fortressLog = -1;
	void noteHeroSpell(HeroTypeID hero, SpellID spell, int affected);
	/// A battle is lost: is buying our army free for `cost` gold the better end? ownValue: AI value we
	/// still have on the field, enemyLoss: what it would still kill (parameter surrender)
	bool surrenderWorth(int cost, double ownValue, double enemyLoss) const;
	/// Our hero surrendered and waits in the tavern with its army: hire it back before the week ends
	void noteSurrender(HeroTypeID hero);

private:
	FairAIGateway & gateway;
	std::shared_ptr<CCallback> cb;
	Perception & perception;
	PlayerColor me;
	Params params;

	ObjectInstanceID mainHero;
	std::atomic<int> activeHero{-1};                         ///< hero currently moving (for dialogs)
	std::atomic<int> exchanges{0};                           ///< meetings of two of our heroes so far (to tell a meeting from standing next to each other)
	std::map<ObjectInstanceID, ObjectInstanceID> reservedBy; ///< target object -> hero (this turn)
	std::map<ObjectInstanceID, int3> currentGoal;            ///< hero -> tile (stickiness)
	std::set<std::pair<int, int3>> failedToday;              ///< hero, tile
	std::map<std::pair<int, ObjectInstanceID>, int> fruitless; ///< hero, object -> day of a visit that changed nothing (not again this week)
	std::map<std::pair<int, int>, std::pair<uint64_t, uint64_t>> fruitlessMeetings; ///< two of our heroes met and nothing changed hands: their army values then
	std::map<ObjectInstanceID, std::set<int3>> trail;        ///< tiles each hero stood on today
	std::map<ObjectInstanceID, std::set<int3>> abandonedToday; ///< goals a hero gave up unreached today (no A-B-A oscillation)
	struct RoundTrip
	{
		ObjectInstanceID town; ///< needy own town the main hero left for a short trip
		int3 goal;             ///< the trip's target: the town does not call the hero back before it is reached
	};
	std::map<ObjectInstanceID, RoundTrip> roundTrip;
	ResourceSet earmark;                                     ///< resources put aside for a planned purchase (a boat)
	int earmarkDay = -100;                                   ///< day the earmark was (re)confirmed; it lapses after a few days
	/// What towns and heroes may spend: our resources minus the earmark
	ResourceSet spendable() const;
	int lastBoatDay = -100;                                  ///< day we last bought a boat
	bool seaWanted = false;                                  ///< a hero ran out of land: a town shipyard is worth building
	/// A hero with nothing left to do on its landmass: buy a boat (own town with a shipyard, or the
	/// shipyard the hero stands at - where a human could do the same) or head for a shipyard.
	/// Returns true if a boat was bought (paths must be recomputed).
	bool seekBoat(const CGHeroInstance * hero, const CPathsInfo & paths, std::vector<Target> & targets);
	std::map<ObjectInstanceID, double> townLeftover;         ///< own town -> troop value the main hero left behind on its last visit (not a reason to come back)
	/// Troops the main hero could pick up in an own town: garrison, a garrison hero's army, creatures for sale
	double townTroops(const CGTownInstance * town, const CGHeroInstance * hero) const;
	int today = 0;

	// ---- turn parts
	void manageTowns(bool endOfTurn);
	void hireHeroes();
	void playHero(ObjectInstanceID heroId);
	void defendTowns();
	void buyTavernArmies();

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
	std::vector<uint16_t> fogDistances(const int3 & from) const;
	bool inPass(const int3 & tile, const CPathsInfo & paths, const int3 * alsoOpen = nullptr) const;
	mutable std::set<int> courierLog; ///< diagnostics: day and hero already reported
	std::set<std::pair<int, int3>> pocketExits; ///< hero, tile: stepped onto to leave a pocket (this turn)
	void dumpKnowledge() const;
	int lastHeroLossDay = -100;
	int idleDays = 0; ///< consecutive days the main hero achieved nothing (no visit, fight or exploration)
	bool mainProductive = false; ///< the main hero visited, fought or explored today
	std::map<int, int> resourceNeed; ///< resource -> amount our best blocked buildings lack
	std::map<int, double> resourceScarcity; ///< resource -> what our towns will need / (stock + four weeks of income), see updateResourcePlan
	void updateResourcePlan();
	double scarcity(GameResID res) const;
	std::set<ObjectInstanceID> heroesLastTurn;
	std::map<ObjectInstanceID, int> townGainedDay;        ///< (townHeroes) town of ours -> the day it became ours (the first towns: long ago)
	std::map<ObjectInstanceID, ObjectInstanceID> postOf;  ///< (townHeroes) hero -> the town of ours it is moving to
	std::map<ObjectInstanceID, int> postSince;            ///< (townHeroes) hero -> the day it reached the town it moved to
	/// (townHeroes) a town of ours that changed hands lately or has enemy heroes about: where a garrison is needed
	bool contested(const CGTownInstance * town) const;
	std::set<ObjectInstanceID> usedTeleporters;
	/// An enemy hero was seen within `radius` tiles of `tile` during the last `days` days
	bool enemySeenNear(const int3 & tile, int days, double radius) const;
	/// How much a town needs the main hero now (0..1): the strongest enemy that can reach it before our
	/// next turn against what the town fields without the main hero (garrison, other heroes, troops we
	/// can still buy, walls)
	double townNeed(const CGTownInstance * town, const CGHeroInstance * mainHero) const;
	/// Our other hero stands in the gate the main hero wants to enter: move it into the garrison
	void clearGate(const CGTownInstance * town, const CGHeroInstance * mainHero);
	bool leaveGate(const CGTownInstance * town, const int3 & keepFree, const std::string & forWhom); ///< our hero in the gate steps onto a free tile next to it; true when the gate is free now
	void arrangeTowns();                ///< (nightWatch) end of a turn: in a threatened town with two heroes of ours the better commander goes inside with the best troops of both
	void restoreNightWatch();           ///< (nightWatch) start of a turn: the troops pooled for the night go back to whom they belong
	double commanderFactor(const CGHeroInstance * hero) const; ///< what the hero adds to an army it leads (field strength per army value)
	double primaryPointValue(const CGHeroInstance * hero, int which, bool leader) const; ///< gold a point of attack (0), defence (1), power (2) or knowledge (3) is worth to this hero
	double ownMagicWorth(const CGHeroInstance * hero) const; ///< (magicWorth 2) what our hero's spells add to its army, by the measured values used for enemy heroes
	double heroEdgeNow() const;         ///< (boldBehind) the edge the main hero asks for before it attacks an enemy hero
	bool wouldLead(const CGHeroInstance * candidate) const; ///< (veterans 2) clearly the more experienced leader than the hero that leads now
	void takeCommand(const CGHeroInstance * veteran, const CGTownInstance * town); ///< (veterans 2) an experienced hero hired back leads our troops again
	/// supply runs (parameter supplyRuns): a hero standing in a town brings what has grown there to the main hero
	struct SupplyRun
	{
		ObjectInstanceID courier;
		ObjectInstanceID town;
		int startDay = 0;
		bool returning = false;
		bool waiting = false; ///< no way to the leader, or the road is not safe: it waits in a town
		bool loading = false; ///< (supplyRuns 2) called to the town: still on its way to the troops
	};
	std::optional<SupplyRun> supply;
	int supplyLog = -1;
	int armyFirstLog = -1;
	bool supplyPossible = false; ///< some town of ours has a hero that could carry (the main hero need not walk home)
	void planSupplyRun();
	double supplyRoadThreat(const CGPathNode * node) const;
	std::vector<Target> supplyTargets(const CGHeroInstance * hero, const CPathsInfo & paths);
	/// Relations to the owner of an object: a valid player we are at war with / a team mate (never ourselves)
	bool isEnemy(PlayerColor owner) const;
	bool isAlly(PlayerColor owner) const;
	/// Multiplier for defenders behind the walls of our own town
	double defenceWalls(const CGTownInstance * town) const;
	/// Threat this hero accepts relative to its defence (main hero / others)
	double tolerance(const CGHeroInstance * hero) const;
	/// Strength the hero would defend with on this tile (own town: garrison and walls help)
	double defenceAt(const CGHeroInstance * hero, const int3 & tile) const;
	/// Walking distance (tiles of normal terrain) an enemy hero at `from` needs to reach `tile`, judged
	/// from what we know of the map: unexplored land counts as open, obstacles and water block, and so
	/// do tiles guarded by known monsters stronger than the hero. Our towns end the walk, roads speed
	/// it up, terrain penalties are ignored (the enemy may be native). Teleporters and subterranean gates
	/// count when we know where they lead (exits we have seen). Buildings that can only be entered
	/// from some sides are entered and left only there.
	/// Infinity if the tile is further than maxTiles. Cached per day.
	double enemyDistance(ObjectInstanceID enemy, const int3 & from, double strength, const int3 & tile, double maxTiles) const;
	/// validation mode (FAI_CHECK_REACH): unexplored land blocks and our towns do not stop the walk,
	/// so that the result can be compared with the game's pathfinder for one of our own heroes
	void computeReach(ReachMap & reach, const int3 & from, double strength, double maxTiles, bool validation = false) const;
	void checkReachAgainstEngine(const CGHeroInstance * hero);
	mutable std::map<std::pair<int, int3>, ReachMap> reachCache;
	/// teleporter -> its channel, remembered while we see it (static, so it stays valid)
	std::map<ObjectInstanceID, TeleportChannelID> teleportChannels;
	/// objects that can be entered (and left) only from some sides, as the game's pathfinder rules
	/// say: object -> mask of allowed directions (bit (dy+1)*3+(dx+1)), remembered while we see it
	std::map<ObjectInstanceID, uint16_t> entryMasks;
	/// fortification level of towns (visible on the map), remembered while we see them
	std::map<ObjectInstanceID, int> townFortLevel;
	// statistics for the test bench (environment variable FAI_STATS = path prefix): what the main hero
	// spends its days on and what lies idle, by four-week phase. Never read by the AI itself.
	const char * statsFile = nullptr;
	std::map<std::string, int> mainStepsToday;
	std::map<int, std::map<std::string, double>> stats;
	static int phaseOf(int day);
	void noteStep(const Target & best);
	void closeStatsDay();
	int breakoutLogDay = -1;
	int lookBehindLogDay = -1;
	std::set<ObjectInstanceID> fledToday;       ///< heroes that fled from a threat today: they do not turn round for home the same day
	int mainSlackDays = 0;                   ///< (breakout 3) days in a row on which the main hero found nothing worth its army to do
	double mainBestValueToday = 0;           ///< (breakout 3) the most valuable target the main hero went for today (ways out not counted)
	int breakoutPauseUntil = 0;              ///< (breakout 2) no ways out are forced before this day: one has just been opened
	std::set<ObjectInstanceID> breakoutExits; ///< (breakout 2) the guards counted as ways out at the last look
	std::map<int, int> seenTilesOn; ///< (breakout) tiles of the map uncovered at the start of each of our days
	int mapTiles = 0;
	bool exploringStalled() const;
	int breakoutDebugDay = -1;
	int trappedLog = -1; ///< day of the last "marching home" line (log only)
	std::set<ObjectInstanceID> scoutsToday; ///< secondary heroes exploring without the territory limit
	/// Strength our hero is expected to lose to moat and arrow towers before the walls fall
	double siegeOverhead(const CGHeroInstance * hero, int fortLevel) const;
	void rememberStaticObjects();

	// ---- what we know about enemy heroes (fair: battles we fought, the thieves' guild, their class)
	struct HeroIntel
	{
		int day = -1;
		std::array<double, 4> skills{}; ///< attack, defence, spell power, knowledge
		double magic = 1.0; ///< what the spells we saw it cast add to its strength (noteHeroSpell)
		std::set<int> damageSpells; ///< the damage spells we saw it cast: a forecast plays them against us (spellForecast)
		// what else we saw it cast (magicWorth): spells on a whole army, blinding, raising the dead
		bool massSpeed = false;
		bool massOther = false;
		bool blinds = false;
		bool raises = false;
	};
	/// (magicWorth) what the magic we saw of an enemy hero adds to its army, by measurement
	double magicWorthOf(const HeroIntel & intel, const std::array<double, 4> & skills) const;
	std::map<int, HeroIntel> heroIntel; ///< hero type -> primary skills the game showed us, and when
	mutable std::mutex intelMutex;
	struct Sighting
	{
		int day = -1;
		double strength = 0; ///< army as shown, times the hero factor
		int3 pos = int3(-1, -1, -1); ///< where
		double army = 0;     ///< (for the log) the army as shown ...
		double factor = 0;   ///< ... and the hero factor we gave it
	};
	int oppositionHero = -1; ///< the hero type behind oppositionHeroes (for the log)
	mutable std::set<int> heavyPartLog; ///< (safeParts) day and hero already logged
	std::map<int, Sighting> enemySightings; ///< hero type -> last time we saw it on the map
	std::set<ObjectInstanceID> secondLeaders; ///< (secondArmy) heroes beside the main hero that lead an army of their own
	bool oppositionKnown = false;             ///< the enemies' best heroes have been seen lately (what the campaign asks for, too)
	std::map<int, int> heroFirstSeen;       ///< hero type -> the day we first saw it on the map (guarded by intelMutex)
	std::map<int, int> playerContact;       ///< player colour -> the day we first saw one of its heroes or towns (guarded by intelMutex)
	/// what a battle against an enemy hero left of it (reported by the battle thread, applied in updateThreats)
	struct BattleOutcome
	{
		int hero = -1;
		bool gone = false;         ///< we won: the hero fell, fled or surrendered
		double remainingShare = 1; ///< else: the share of its army that survived
		double ownStrength = 0;    ///< our army in that battle
		double enemyStrength = 0;  ///< its army in that battle
		int3 pos = int3(-1, -1, -1); ///< where the battle was fought
		int ownHero = -1;          ///< (calibration) our hero in it (object id)
		bool weAttack = false;
		bool siege = false;        ///< fought at the walls of a town
		double ownLostShare = 0;
		int day = 0;
	};
	std::vector<BattleOutcome> pendingOutcomes; ///< guarded by intelMutex
	/// (calibration) What the hero model said about every battle against an enemy hero before it was
	/// fought, and how it ended - for the test bench (statistics file, key "fights") and the log; never
	/// used for a decision. One entry: day, we attack, siege, our army, our hero's factor x 100, their
	/// army, the factor we gave their hero x 100 (-1: never seen before), won, our loss in per cent, theirs.
	/// And for attacks on us: how far away our reach model had that hero when our last turn ended, in
	/// tenths of a tile (-1: not among the threats we knew, 9999: further than 60 tiles), and the age
	/// of the sighting in days - what the model said the night before the enemy proved it could reach us.
	/// Last: the army we had seen that hero with at its last sighting (-1: none) - against the army it
	/// came with: Nullkiller2 hands troops along a chain of heroes before it strikes.
	std::vector<std::array<int64_t, 13>> fightNotes;
	std::map<int, std::map<int, std::pair<int, int>>> threatDistanceSeen; ///< (calibration) own hero (object id) -> enemy hero type -> (model distance x 10 at our last turn end, age of the sighting)
	std::map<int, double> ownFactorSeen; ///< (calibration) own hero (object id) -> its commander factor when last looked at
	std::map<int, int> defeatedDay;             ///< hero type -> day we beat it
	int decisiveVictoryDay = -1;                ///< the day we beat what was the strongest enemy army we knew of
	double oppositionHeroes = 0; ///< strongest enemy hero seen in the last three weeks (0: none)
	double hiddenMain = 0;        ///< strongest of those we cannot see today: it may sit in a town (parameter townAmbush)
	std::vector<int3> ambushTowns; ///< enemy towns we know of
	bool ambushCounts = true;     ///< (townAmbush) false while targets are collected for a hero with next to nothing to lose
	double oppositionGuards = 0; ///< strongest guard we know of on the map
	int guildDay = -1;           ///< day we last looked into the thieves' guild
	int contactDay = -1;         ///< day we first saw an enemy hero or town (-1: not yet)
	std::atomic<int> rehireHero{-1}; ///< hero type that surrendered and waits in our tavern with its army
	std::atomic<int> rehireDay{-100};
	ObjectInstanceID veteranMain;       ///< (veterans 2) the leader that came back from the tavern ...
	int veteranUntil = 0;               ///< ... keeps the lead until this day, whatever army the others hold
	int townDeficit = 0;                ///< towns the enemy holds more than we do (thieves' guild, and what we have seen)
	int nearMissToday = 0;              ///< (statistics) 2: an enemy hero no weaker than our main hero was in reach today and not attacked; 1: one at 0.8 to 1.0
	struct NightLoan
	{
		ObjectInstanceID inside;
		ObjectInstanceID outside;
		std::map<CreatureID, int> insideHad;  ///< what the hero inside owned before the troops were pooled
		std::map<CreatureID, int> outsideHad;
	};
	std::vector<NightLoan> nightLoans;  ///< (nightWatch) pooled last night, to be undone this morning
	std::atomic<int> lastSurrenderDay{-100};
	std::atomic<double> waitingTroopGold{0}; ///< gold the creatures waiting in our dwellings cost (updated every turn; read by the battle thread)
	std::map<PlayerColor, int> guildTownPlace; ///< thieves' guild: place of each player by number of towns (0 = most)
	std::map<PlayerColor, int> guildBestHero;  ///< thieves' guild: each player's best hero (hero type)
	bool huntWanted = false;     ///< some enemy still has towns we have not found (updateThreats)
	bool campaignMode = false;   ///< the main hero is clearly the strongest force we know of (updateThreats; parameter leaderFocus)
	bool refillMode = false; ///< campaign interrupted: the main hero fetches the troops that wait in our towns
	/// this hero is the main hero on campaign: long marches, the enemy's towns first, no errands
	bool campaign(const CGHeroInstance * hero) const;
	/// attack, defence, spell power and knowledge of an enemy hero: as shown to us, else as its class and the day suggest
	std::array<double, 4> heroSkillsEstimate(const ObservedObject & hero) const;
	/// expected loss (gold) of attacking an enemy hero in the open field, by simulating the battle with
	/// the hero's estimated attack and defence on its troops; -1: leave it alone (parameter heroForecast)
	double heroFightLoss(const CGHeroInstance * hero, const ObservedObject & enemy) const;
	/// our hero's strength as it counts against another hero: army times what its attack and defence
	/// add (parameter heroModel; else the game's total strength, which also counts spell power and knowledge)
	double fieldStrength(const CGHeroInstance * hero) const;
	/// What an enemy hero adds to its army (multiplier): attack and defence, spell power and knowledge,
	/// as shown to us or as its class and the day suggest (parameter heroModel; else a flat factor)
	double heroFactor(const ObservedObject & hero) const;
	/// what walking back for troops is worth to the main hero, as a share of the troops' value (parameter leaderFocus)
	double fetchFactor(const CGHeroInstance * hero) const;
	/// the hero's strength over the strongest enemy hero seen lately; 0 if we have seen none
	double superiority(const CGHeroInstance * hero) const;
	/// planning horizon in days: the main hero may march on a far town (parameter leaderFocus)
	int travelHorizon(const CGHeroInstance * hero) const;

	// ---- helpers
	const CGHeroInstance * ownHero(ObjectInstanceID id) const;
	void chooseMainHero();
	bool isMain(const CGHeroInstance * hero) const;
	/// the main hero, or (secondArmy) another hero strong enough to lead an army of its own
	bool leads(const CGHeroInstance * hero) const;
	/// a hero that is no leader and carries a real army (heavyPartValue, heavyPartShare of the main hero's)
	bool heavy(const CGHeroInstance * hero) const;
	/// (secondArmy) the field strength a second leader needs; below zero: no second army now
	double secondArmyBar() const;
	void updateSecondLeaders();
	std::shared_ptr<CPathsInfo> computePaths(const CGHeroInstance * hero, bool ignoreGuards) const;

	std::vector<Target> collectTargets(const CGHeroInstance * hero, const CPathsInfo & paths, const CPathsInfo & freePaths);
	double objectValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const;
	double resourceValue(GameResID resource) const;
	double expValue(const CGHeroInstance * hero) const;

	double observedDanger(const ObservedObject & object) const;
	double guardDanger(const int3 & tile) const;
	/// Expected losses in gold equivalents, or a negative value if the fight is too risky
	double expectedLoss(const CGHeroInstance * hero, double danger, bool desperate = false, bool versusHero = false) const;
	/// A fight against map guards as the battle simulator expects it to go (parameter fightForecast)
	struct FightForecast
	{
		bool win = false;
		double lossShare = 1.0; ///< of our army value
		double lossValue = 0;
		int mana = 0;           ///< spell points our hero uses in the plan
		double manaShare = 0;   ///< ... and what they are worth, as a share of our army value (spellForecast)
	};
	/// our hero's spells for a forecast (nullptr while spellForecast is off)
	const Combat::HeroSpec * ownSpells(const CGHeroInstance * hero, Combat::HeroSpec & spec) const;
	/// what a spell point of this hero is worth right now, as a share of the value of its army
	double manaPointShare(const CGHeroInstance * hero) const;
	/// army and (with spellForecast) spell points and spells of a hero: the same hash, the same forecast
	uint64_t fightSignature(const CGHeroInstance * hero) const;
	FightForecast forecastMonsterFight(const CGHeroInstance * hero, CreatureID creature, int count) const;
	/// expected loss (gold) of attacking this stack of wandering monsters; -1: leave it alone
	double monsterLoss(const CGHeroInstance * hero, const ObservedObject & monster, bool simulate = false) const;
	/// same for whatever guards a tile
	double guardLoss(const CGHeroInstance * hero, const int3 & tile) const;
	mutable std::map<std::tuple<int, uint64_t, int, int>, FightForecast> forecastCache;

	// ---- buildings valued by the public table of their kind (parameter tables)
	/// what one reward of a table is worth to this hero, in gold; what the building takes is subtracted
	double rewardValue(const CGHeroInstance * hero, const Rewardable::Reward & reward, const int3 & where) const;
	/// what luck or morale for the next battle is worth to this hero's army
	double battleEdgeValue(const CGHeroInstance * hero, bool luck, int points) const;
	mutable std::set<std::tuple<int, int, int>> moveLossLog; ///< diagnostics: day, hero, building
	std::set<int3> sanctuaries; ///< where the Sanctuaries we know stand
	/// the share of the movement a hero has left that a visit to such a building takes
	double moveLossShare(const CGHeroInstance * hero, const RewardTable & table) const;
	/// what a visit brings on average over the buildings the table can produce (`only`: these samples)
	double tableValue(const CGHeroInstance * hero, const RewardTable & table, const std::vector<int> * only, const int3 & where) const;
	/// an artifact to pick up: by its class, or by what it does for our heroes (parameter artifactCare)
	double artifactPickupValue(const CGHeroInstance * hero, ArtifactID art) const;
	/// army value the hero really gains from creatures handed to it: a stack of the same kind, a free
	/// slot, or a weaker stack to send away
	double armyGain(const CGHeroInstance * hero, const std::vector<CStackBasicDescriptor> & creatures) const;
	// ---- armies as they fight (parameter armyFit)
	struct FitStack
	{
		CreatureID kind;
		double value = 0;
		int slot = -1;
	};
	struct FitMove
	{
		int take = -1;   ///< index into the offered stacks
		int drop = -1;   ///< index into the army's stacks that leaves for it (-1: a free slot)
		double gain = 0; ///< what the army gains in worth
	};
	/// expected turns of a troop with this morale against one without (the game's dice)
	double turnShare(int morale) const;
	mutable std::array<double, 13> turnTable{};
	mutable bool turnTableReady = false;
	/// morale every troop of this hero has from the hero itself - all but the part that follows from the mix of its army
	int sharedMorale(const CGHeroInstance * hero) const;
	/// an army's worth as it fights: AI value of each kind times the turns its morale gives it; the
	/// mix sets the morale all share (one faction +1, each further one -1, undead among them -1)
	double armyWorth(int shared, const std::vector<std::pair<CreatureID, double>> & kinds) const;
	/// share of its face value that a troop of this kind really adds to the hero's army (1 without armyFit; can be negative)
	double fitShare(const CGHeroInstance * hero, CreatureID kind, double value) const;
	/// the next stack worth taking over, and which one leaves for it when the army is full; false: none
	bool fitMove(int shared, const std::vector<FitStack> & have, const std::vector<FitStack> & offer, bool full, FitMove & move) const;
	/// tiles we have not seen that a building would reveal
	double unexploredAround(const int3 & where, const Rewardable::RewardRevealTiles & reveal) const;
	/// a creature bank or another guarded building (and tables = 2)
	bool guardedTable(const ObservedObject & object) const;
	struct GuardedPlan
	{
		double chance = 0;    ///< share of the states the building can be in whose guards we would fight
		double value = 0;     ///< what the visit brings on average, in gold (a state we turn away from brings nothing)
		double loss = 0;      ///< what those fights cost on average, in gold
		double lossShare = 0; ///< ... as a share of our army (diagnostics)
		bool known = false;   ///< the game lists the guards
	};
	GuardedPlan guardedPlan(const CGHeroInstance * hero, const ObservedObject & object, bool useCache = true) const;
	/// the fight against these guards in this battle layout as the battle simulator expects it to go
	FightForecast forecastGuards(const CGHeroInstance * hero, const std::vector<std::pair<CreatureID, int>> & guards, const std::string & layout, bool useCache) const;
	// ---- buildings with rules of their own (parameter sites)
	/// what a visit to such a building is worth to this hero now; 0: nothing, or not such a building
	double siteValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const;
	/// the fight a visit means (a garrison of somebody else, the troglodytes of an abandoned mine):
	/// expected loss in gold, 0 without a fight, -1 if it is too risky
	double siteLoss(const CGHeroInstance * hero, const ObservedObject & object) const;
	/// the guards of a dwelling that is not ours, by the rule of the game (nothing read from the building itself)
	std::vector<std::pair<CreatureID, int>> dwellingGuards(const ObservedObject & object, const CGObjectInstance * instance) const;
	std::map<ObjectInstanceID, std::vector<CreatureID>> dwellingKinds; ///< what each dwelling we have seen breeds (first kind of every row)
	/// what beating them would cost this hero, in gold; -1: not to be won, or too dear
	double dwellingLoss(const CGHeroInstance * hero, const std::vector<std::pair<CreatureID, int>> & guards) const;
	/// the hero stands on the building it went for: what there is to do there
	void useSite(const CGHeroInstance * hero, ObjectInstanceID site);
	/// what a skill bought at a university is worth to this hero, in gold
	double universitySkillValue(const CGHeroInstance * hero, SecondarySkill skill) const;
	/// A wish from our quest log (seer huts, quest guards) that is still open
	struct KnownQuest
	{
		std::vector<ArtifactID> artifacts; ///< artifacts it asks for
		double cost = 0;                   ///< what granting it takes from us, in gold (artifacts, resources, creatures)
	};
	std::map<ObjectInstanceID, KnownQuest> quests;        ///< hut or guard -> its wish
	std::map<ArtifactID, ObjectInstanceID> questArtifacts; ///< artifact somebody in our quest log asks for -> who
	/// reads our quest log: what a player sees there (the wish - not the reward, which shows only when it is granted)
	void updateQuests();
	/// gold nothing waits for: what is left when our towns could spend two more weeks' worth (0 while gold is short)
	double idleGold() const;
	/// what buying this artifact adds to the hero: its worth there against what it would replace
	double artifactGain(const CGHeroInstance * hero, ArtifactID art) const;
	/// the main hero at an artifact merchant (town building or black market): gold that lies idle
	/// buys what makes it stronger. Returns the number of artifacts bought
	int buyArtifacts(const IMarket * market, const CGHeroInstance * hero);
	std::map<ObjectInstanceID, std::vector<ArtifactID>> marketOffers; ///< black market -> what it had on offer when we stood there (guarded by intelMutex)
	std::map<ObjectInstanceID, int> siteWeek;  ///< building -> week we last used it (a refugee camp has new guests every week)
	std::set<ObjectInstanceID> sitesDone;      ///< buildings that have nothing more for us (a hut of the magi shows the same again)
	std::atomic<int> guardedTarget{-1};   ///< guarded building the moving hero is heading for (for the game's question at its door)
	std::atomic<int> guardAnswer{-1};     ///< what we answered there: 1 fight, 0 turn back, -1 not asked
	std::atomic<int> guardForecast{0};    ///< loss forecast for that fight, in thousandths of our army
	int tableLogDay = -1;                 ///< diagnostics: the main hero's judgement of the guarded buildings in reach, once a day
	mutable std::string * tableDebug = nullptr; ///< diagnostics: objectValue notes the buildings it values by their table here
	int forecastLogDay = -1; ///< diagnostics: the forecasts of the main hero are logged once a day
	int guardLogDay = -1;    ///< diagnostics: the main hero's judgement of the guards in reach, once a day

	/// Moves the hero one step towards `tile`. Returns false if no step could be made.
	bool stepTowards(const CGHeroInstance * hero, const int3 & tile, const CPathsInfo & paths);

	// ---- towns and armies
	void buildInTown(const CGTownInstance * town);
	double buildingValue(const CGTownInstance * town, BuildingID building) const;
	/// Buys the resources missing for `cost` with gold at a marketplace. True if `cost` is affordable now.
	bool buyResourcesFor(const CGTownInstance * town, const ResourceSet & cost, bool forEarmark = false, const ResourceSet * keep = nullptr);
	void recruitInTown(const CGTownInstance * town, const CArmedInstance * destination, int64_t goldReserve);
	int64_t recruitableValue(const CGTownInstance * town, int64_t gold) const;
	void manageTownVisit(const CGHeroInstance * hero, const CGTownInstance * town);
	/// Upgrades the hero's creatures in the town it visits
	void upgradeArmy(const CArmedInstance * army);
	/// (heroChoice) what a hero on offer in the tavern is worth to us, in gold
	double tavernHeroValue(const CGHeroInstance * candidate, bool fighter) const;
	/// (magicFocus) guild levels of this town the hero can learn that were built since its last visit
	int unlearnedGuildLevels(const CGHeroInstance * hero, const CGTownInstance * town) const;
	std::map<std::pair<ObjectInstanceID, ObjectInstanceID>, int> guildSeen; ///< (hero, town) -> guild levels learned there
	/// (garrisonRoom) frees a slot for `creature` in the army if the troops to buy are worth it
	bool makeRoom(const CGTownInstance * town, const CArmedInstance * army, CreatureID creature, double batchValue);
	/// stacks of one kind in one army become one stack
	void mergeDuplicates(const CArmedInstance * army);
	/// single creatures split off the cheapest stacks into free slots: they take retaliations and block
	/// in battle (the combat search knows how to use them); merged back before troops are exchanged
	void arrangeBaits(const CGHeroInstance * hero);
	void mergeBaits(const CGHeroInstance * hero);
	/// Leaves the weakest stacks in the town while enemy heroes are about
	void leaveGarrison(const CGHeroInstance * hero, const CGTownInstance * town);
	/// Moves the strongest stacks from `source` to `destination` (keeps one unit on a source hero)
	/// takeAll: nothing is left behind as a guard (the source is a building that hands troops over)
	void pickBestCreatures(const CArmedInstance * destination, const CArmedInstance * source, bool takeAll = false);
	double transferableValue(const CGHeroInstance * from) const;
	/// army value `destination` would really gain by taking troops from `source` (see pickBestCreatures)
	double transferGain(const CArmedInstance * destination, const CArmedInstance * source) const;
	/// > 1 when troops waiting for the main hero are big compared with its army (parameter couriers)
	double reinforcementBoost(double troops) const;
	/// (gather) what troops count for when they close the gap between the main hero and the edge it asks for against the strongest enemy hero seen lately: 1 to 3, or 0 when that is not the question
	double decisiveBoost(double troops) const;
};

}
