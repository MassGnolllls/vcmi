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
	double safetyRatio = 1.4;          ///< minimum own strength / enemy strength to start a fight (main hero)
	double safetyRatioSecondary = 2.5; ///< same for scouts and collectors
	double dangerUpperWeight = 0.4;    ///< observed quantity ranges: weight of the upper bound
	double lossFactor = 0.7;           ///< scales the Lanchester estimate of own losses
	double lossAversion = 1.0;         ///< weight of expected losses against gains
	double expValueMain = 0.6;         ///< gold per experience point for the main hero
	double expValueOther = 0.15;
	double unlockFactor = 0.7;         ///< share of the value behind a guard credited to beating it

	// --- enemy heroes
	double threatRadius = 16;          ///< tiles an enemy hero may cover until our next turn
	double enemyHeroFactor = 1.25;     ///< assumed attack/defence bonus of an enemy hero
	double threatTolerance = 0.8;      ///< avoid tiles where threat > tolerance x own strength
	double townDefenceValue = 10000;   ///< value of returning to a threatened own town

	// --- movement and planning
	double travelOffset = 0.15;        ///< days added to every travel time (visit overhead)
	double stickiness = 1.2;           ///< score multiplier for the target chosen last step
	double explorationTileValue = 18;  ///< gold per newly revealed tile (decays over the game)
	double staleTileWeight = 0.3;      ///< tiles hidden again (Cover of Darkness) vs never seen
	int maxTravelTurns = 4;            ///< targets further away are ignored

	// --- economy
	double incomeHorizonDays = 14;     ///< days of income a mine/building is worth
	double dwellingHorizonWeeks = 4; ///< weeks of growth a dwelling is worth
	double primarySkillValueMain = 700;
	double primarySkillValueOther = 150;
	double ferryFactor = 0.5;          ///< value of bringing army to the main hero (x army gold value)
	double reinforceFactor = 0.8;      ///< value of picking up town troops (main hero)
	int heroGoldReserve = 1000;        ///< gold kept after hiring a hero (except on the first days)
	double retreatValue = 0.6;         ///< share of hero + army value saved by leaving a threatened tile

	/// Apply overrides from the arena settings for this player (no-op in normal games)
	void loadOverrides(int playerIndex);
};

}
