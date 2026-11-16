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
	double threatTolerance = 1.4;      ///< avoid tiles where threat > tolerance x own strength
	double townDefenceValue = 10000;   ///< value of returning to a threatened own town
	int townGuard = 1;                 ///< 1: town defence weighs the town's own means, the main hero only makes round trips from a town that needs it and clears the gate of our other heroes (0: old rules)
	double wallsFactor = 1.5;          ///< sieges: defenders behind town walls count this much more
	int siegeCost = 1;                 ///< sieges: moat and arrow towers cost troops before the walls fall (0: ignore)

	// --- movement and planning
	double travelOffset = 0.15;        ///< days added to every travel time (visit overhead)
	double stickiness = 1.2;           ///< score multiplier for the target chosen last step
	double explorationTileValue = 36;  ///< gold per newly revealed tile (decays over the game)
	double staleTileWeight = 0.3;      ///< tiles hidden again (Cover of Darkness) vs never seen
	int maxTravelTurns = 4;            ///< targets further away are ignored
	int exploreMaxTurns = 4;           ///< exploration targets at most this many days away
	int collectorsTakeGarrison = 1;    ///< collectors pick up garrison troops to ferry them
	double huntFactor = 2.0;           ///< exploration bonus for the main hero while the enemy town is unknown

	// --- economy
	double incomeHorizonDays = 14;     ///< days of income a mine/building is worth
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
	int keepGarrison = 1;              ///< heroes leave the weakest stack in a town they take troops from

	/// Apply overrides from the arena settings for this player (no-op in normal games)
	void loadOverrides(int playerIndex);
};

}
