/*
 * Params.h, part of VCMI engine - FairAI
 *
 * Tunable weights of the adventure-map AI. Defaults are hand-set; Phase 4 tunes them by
 * self-play. Arena runs can override single values per player through
 * settings["session"]["arena"]["fairaiParams"][playerIndex] = { "name": value, ... }.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

namespace FAI
{

struct Params
{
	// --- currency: everything is rated in gold equivalents
	double goldPerArmyValue = 0.6;     ///< gold equivalent of one point of creature AI value
	double resourceValueFactor = 0.6;  ///< x the game's market price of a resource

	// --- battles
	double safetyRatio = 1.3;          ///< minimum own strength / enemy strength to start a fight (main hero)
	double resourceNeedFactor = 3.0;   ///< resource piles we need for a blocked building are worth up to 1 + this
	double patienceStep = 0.03;        ///< safety ratio reduction per idle day of the main hero
	double minSafetyRatio = 1.15;
	double safetyRatioHero = 1.05;     ///< same against enemy heroes and towns (exact armies, our combat AI is better)
	double safetyRatioSecondary = 2.5; ///< same for scouts and collectors
	double dangerUpperWeight = 0.15;   ///< observed quantity ranges: weight of the upper bound
	double lossFactor = 0.7;           ///< scales the Lanchester estimate of own losses
	double lossAversion = 1.0;         ///< weight of expected losses against gains
	double expValueMain = 0.6;         ///< gold per experience point for the main hero
	double expValueOther = 0.15;
	double unlockFactor = 0.7;         ///< share of the value behind a guard credited to beating it
	double guardFogFactor = 0.0;       ///< share of the unexplored land around a guard credited to beating it

	// --- enemy heroes
	double threatRadius = 16;          ///< tiles an enemy hero may cover until our next turn
	int pathThreats = 1;               ///< threat zones follow the walkable land we know (0: straight-line radius)
	double enemyHeroFactor = 1.15;     ///< assumed attack/defence bonus of an enemy hero
	double threatGrowth = 0.15;        ///< an enemy hero we have not seen for a day has probably picked up this much more army (per day)
	double threatTolerance = 1.4;      ///< avoid tiles where threat > tolerance x own strength (main hero)
	double threatToleranceSecondary = 1.4; ///< same for the other heroes
	double defenceWallsFort = 1.3;     ///< our own town with a fort: defenders count this much more
	double defenceWallsCitadel = 1.5;  ///< same with a citadel or castle
	double fleeGain = 4;               ///< trapped (no safe tile today): flee only if the threats' path distance grows by this much
	double townDefenceValue = 10000;   ///< value of returning to a threatened own town
	int townGuard = 1;                 ///< 1: town defence weighs the town's own means, the main hero only makes round trips from a town that needs it and clears the gate of our other heroes (0: old rules)
	double wallsFactor = 1.5;          ///< sieges: defenders behind town walls count this much more
	int siegeCost = 0;                 ///< sieges: moat and arrow towers cost troops before the walls fall (0: ignore). Off until a test shows a gain: the first A/B leaned against it (-0.055 +- 0.055)

	// --- movement and planning
	double travelOffset = 0.15;        ///< days added to every travel time (visit overhead)
	double stickiness = 1.2;           ///< score multiplier for the target chosen last step
	int antiOscillation = 1;           ///< goals given up unreached are not taken up again the same day; trips out of a needy town are finished before the town calls the hero back
	double explorationTileValue = 36;  ///< gold per newly revealed tile (decays over the game)
	double staleTileWeight = 0.3;      ///< tiles hidden again (Cover of Darkness) vs never seen
	int maxTravelTurns = 4;            ///< targets further away are ignored
	int exploreMaxTurns = 4;           ///< exploration targets at most this many days away
	int collectorsTakeGarrison = 1;    ///< collectors pick up garrison troops to ferry them
	double huntFactor = 2.0;           ///< exploration bonus for the main hero while the enemy town is unknown

	// --- economy
	double incomeHorizonDays = 28;     ///< days of income a mine/building is worth (as long as a dwelling counts; 14 was measurably worse on medium maps)
	double neutralTownValue = 12000;   ///< capturing a neutral town (income, creatures, a base)
	double enemyTownValue = 20000;     ///< capturing an enemy town (also takes it from the enemy)
	double dwellingHorizonWeeks = 4; ///< weeks of growth a dwelling is worth
	double primarySkillValueMain = 700;
	double primarySkillValueOther = 150;
	double ferryFactor = 0.5;          ///< value of bringing army to the main hero (x army gold value)
	double reinforceFactor = 0.8;      ///< value of picking up town troops (main hero)
	int heroGoldReserve = 1000;        ///< gold kept after hiring a hero (except on the first days)
	double retreatValue = 0.6;         ///< share of hero + army value saved by leaving a threatened tile
	double collectorRadius = 12;       ///< collectors stay within this many tiles of an own town
	int maxHeroes = 8;                 ///< never hire beyond this
	int buyWarMachines = 0;            ///< main hero buys the blacksmith's war machine
	double mageGuildValue = 700;       ///< first mage guild level (spells for the main hero); higher levels less
	double garrisonShare = 0.0;        ///< share of hero + garrison value left in a town while enemies are about
	int tavernArmies = 1;              ///< spend surplus gold on tavern heroes' starting armies
	int collectorsRoam = 1;            ///< how many secondary heroes roam; the others guard towns from the garrison
	int scouts = 0;                    ///< extra secondary heroes that explore beyond our territory (avoiding known threats)
	int moveSpells = 1;                ///< paths may use Fly / Water Walk when the hero can cast them (cast on the way)
	int boats = 1;                     ///< heroes board boats, buy one when they run out of land (town shipyard, or a shipyard they stand at) and sail
	int concentrate = 0;               ///< heroes meeting to hand over troops count each other's army as protection; collectors may leave our territory to reach the main hero. Off until a test shows a gain: the first A/B leaned against it (-0.046 +- 0.051)
	int couriers = 0;                  ///< secondary heroes fetch troops waiting in our towns and bring them to the main hero; a big reinforcement counts more than its size (it makes fights possible). Off until a test shows a gain
	double earlyGoldDays = 21;         ///< until this day gold is scarce whatever the stock says (buildings are waiting): income buildings count in full, fading out over the two weeks after. 0: off. On by default: +0.05 and +0.12 score in two tests on medium maps (town hall and city hall in week one)
	int heroesWeek1 = 2;               ///< heroes wanted in the first week (hiring schedule)
	int heroesWeek3 = 3;               ///< ... until day 21
	int heroesLater = 4;               ///< ... afterwards
	int sellSurplus = 0;               ///< resources piling up are traded for the ones our buildings lack. Off until a test shows a gain
	int skillChoice = 0;               ///< secondary skills are valued by the hero's role and army (what the skill's bonuses do) instead of one fixed ranking for everybody. Off until a test shows a gain
	int resourcePlan = 0;              ///< mines and resource piles are valued by what our towns will need (buildings still missing, weekly creature costs) against stock and income - before a building is blocked. Off until a test shows a gain
	int artifactCare = 0;              ///< artifacts are valued by what they do for the hero that would carry them; heroes wear the best per slot, hand artifacts on when they meet, assemble sets; artifacts on the map are targets by that value. Off until a test shows a gain

	// --- combat (read by the battle AI of the same player)
	int spellSearch = 0;               ///< hero spells chosen by the look-ahead search (0: VCMI's BattleAI evaluator)
	int simAbilities = 1;              ///< battle look-ahead knows special abilities: life drain, regeneration, fire shield, charge bonus, strike and return, luck, morale (0: plain damage only)
	int battlePlan = 0;                ///< battle AI judges every action by playing several rounds ahead with two plans for our side, hold or advance (0: rest of the current round only). Off until a test shows a gain
	int fightForecast = 0;             ///< fights against map guards are judged by simulating the battle (unit against unit, FairAI's battle simulator) instead of comparing army values. Off until a test shows a gain
	double maxLossShare = 0.35;        ///< with fightForecast: the main hero does not start a fight that is forecast to cost more than this share of its army ...
	double maxLossShareIdle = 0.03;    ///< ... raised by this much per idle day (up to 0.6)
	double maxLossShareSecondary = 0.15; ///< same limit for the other heroes
	double guardKillValue = 0;         ///< beating guards is worth this share of their army value by itself (experience and room to move beyond what the map shows)
	int keepGarrison = 1;              ///< heroes leave the weakest stack in a town they take troops from
	int baitStacks = 0;                ///< main hero keeps this many single creatures of its cheapest troops (retaliation bait, blockers)

	/// Apply overrides from the arena settings for this player (no-op in normal games)
	void loadOverrides(int playerIndex);
};

}
