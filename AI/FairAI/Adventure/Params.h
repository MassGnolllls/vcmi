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
	int unlockDirect = 0;              ///< what a guard blocks counts from the guard's place (where the hero stands after the fight), not from where the hero is now. Off until a test shows a gain
	double regionValue = 0;            ///< main hero: unexplored land behind a guard is a new region (early: mines, treasures, perhaps a town; late: where the enemy lives): beating the guard is worth this much more. 0: only the strip the first step reveals counts

	// --- enemy heroes
	double threatRadius = 16;          ///< tiles an enemy hero may cover until our next turn
	int pathThreats = 1;               ///< threat zones follow the walkable land we know (0: straight-line radius)
	double enemyHeroFactor = 1.15;     ///< assumed attack/defence bonus of an enemy hero
	int heroModel = 1;                 ///< 1: hero against hero is judged in one measure - army times what the hero's attack and defence add. Ours as it is; the enemy's as the game showed it to us (battles, thieves' guild) or as its class and the day suggest. Attack from heroEdge on, keep away from heroTolerance on. Replaces the flat enemyHeroFactor and the game's total strength (which counts our own spell power in full). On since 17.11.2026 (192 long games: with the campaign 40 % won, without it none of 96)
	double heroEdge = 1.3;             ///< with heroModel: attack an enemy hero in the open from this ratio on (logged battles: all won from 1.3, coin flip at 1.0)
	double heroTolerance = 1.0;        ///< with heroModel: tiles an enemy hero of this many times our strength can reach are unsafe
	int surrender = 0;                 ///< 1: in a battle our simulator sees as lost the hero buys its army free (surrender) when we have the gold and the army is worth more to us than the gold; hired back from the tavern at once. Off until a test shows a gain
	int veterans = 0;                  ///< 1: an experienced hero in the tavern (level 6 or more: ours that fled, or anybody's fallen hero) is hired at once, beyond the usual number of heroes. Off until a test shows a gain
	int heroForecast = 0;              ///< 1: attacking an enemy hero in the open field is judged by simulating the battle with the hero's estimated attack and defence (as for guards), not by army values alone. Off until a test shows a gain
	int leaderFocus = 1;               ///< 1: campaign mode - once the main hero is campaignEdge times as strong as any enemy hero seen in three weeks it plans up to 30 days ahead, looks for the enemy's towns and takes them, and leaves the growth at home as garrison. On since 17.11.2026 (192 long games: with the campaign 40 % won, without it none of 96); an enemy army we have beaten no longer counts, and for three weeks after beating the strongest one we knew of the campaign runs without further conditions (pursuit)
	double campaignEdge = 3.0;         ///< see leaderFocus (3.0 since 17.11.2026)
	double refillShare = 0;            ///< campaign: troops waiting in our towns worth this share of the main hero's army interrupt the campaign - the hero fetches them first (0: never). Off: the first test (0.4, with other changes) won no game where the campaign without it had won three
	int fetchByShare = 0;              ///< 1: troops waiting for the main hero (in towns, with our other heroes) count by how much they would add to its army - full value from fetchShare of the army on, a tenth for a trickle. Off until a test shows a gain
	double fetchShare = 0.5;           ///< see fetchByShare
	int noChase = 2;                   ///< 1: enemy heroes are targets only when in reach today or standing in a town (no chasing across the map); 2: the same, but only while on campaign. 2 since 17.11.2026 (part of the campaign setting; never chasing, 1, made no difference in 191 games)
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
	int seekTowns = 0;                 ///< 1: towns we know of but have no way to (land between unseen, or hidden again by a Cover of Darkness) draw the main hero and scouts: the way is guessed through the fog, and the stretch to explore or the guard on that guess is credited with the town. Enemy towns only for a main hero stronger than the enemy heroes it has seen (or on campaign). Off until a test shows a gain
	int exploreRegion = 0;             ///< 1 (with regionValue): a place to explore from which a whole unexplored region shows (not a pocket of fog) is worth regionValue more, for the main hero. Off until a test shows a gain
	int fetchRoundTrip = 0;            ///< 1: the main hero counts the way home for troops twice (it has to come back to where its work is). Off until a test shows a gain
	int armyFirst = 0;                 ///< 1: a building that would leave troops unbought in our dwellings is built only if it is worth more per gold piece than those troops (until now buildings always came first, and the main hero led a few thousand in weeks two and three). Off until a test shows a gain
	int townAmbush = 0;                ///< 1: within a day's march of an enemy town, the strongest enemy hero seen in the last three weeks but not visible today counts as a threat (it may sit in the town). Off until a test shows a gain
	int fortress = 0;                  ///< 1: a main hero that is outmatched - an enemy army stronger than fortressRatio times its own was seen within fortressDays near our towns or near the hero (or beat one of our armies there) - ends every day in a town of ours: outside it heads home, from a town it only makes trips that end back there the same day. Measured: whenever the enemy attacked our biggest army it won (55 of 56), and the armies lost that way in the open were mostly led by heroes hired after the leader had fallen (level 1 to 4, 30k to 100k and more). Off until a test shows a gain
	double fortressRatio = 1.2;        ///< see fortress (the armies that attacked our main army were 1.5 times its size in the median; at 1.0 a hero three per cent behind sat in its town for eleven days)
	int fortressDays = 10;             ///< see fortress
	double fortressRadius = 36;        ///< see fortress: tiles from the place of the sighting to one of our towns or to the hero
	int heroChoice = 0;                ///< 1: the hero hired from the tavern is the one worth most to us - its troops, its secondary skills for the role it will play (collector, or leader if we have none), and what its specialty brings in day by day (350 gold, a rare resource). Until now the one with the strongest starting army was taken. Off until a test shows a gain
	int magicFocus = 0;                ///< 1: mage guilds are built for what their spells do for the main hero - all five levels, valued by whether the hero can learn them (spellbook, Wisdom) -, and the main hero visits a town of ours whose guild holds spells it can learn but has not got. Until now levels 2 and 3 were worth next to nothing and 4 and 5 were never built; the enemy's heroes cast Implosion and Chain Lightning, ours Stone Skin. Off until a test shows a gain
	int garrisonRoom = 1;              ///< 1: a town's garrison (or the hero in it) whose seven slots are taken by leftovers makes room for what the town grows: stacks are upgraded where the town can, stacks of one kind merged, and the weakest stack is dismissed when the troops to buy are worth roomFactor times as much. Until now nothing was bought in such a town (in logged games a hundred thousand gold of troops waited in one town's dwellings while 200k gold lay unused). On since 17.11.2026, together with collectorsTakeGarrison 0 (191 long games: score +0.135 +- 0.077, town share +0.137 +- 0.083, 44 against 33 wins, 17 against 27 losses)
	double roomFactor = 2.0;           ///< garrisonRoom: the troops to buy must be worth this many times the stack that goes
	int campaignPrior = 0;             ///< 1: the campaign measures the main hero not only against the strongest enemy hero seen but also against what the enemy is likely to have - half of an army like ours, scaled by its number of towns against ours (thieves' guild, towns seen). Off until a test shows a gain
	int mainHorizon = 0;               ///< main hero: targets and unexplored land up to this many days away count (0: maxTravelTurns / exploreMaxTurns). Off until a test shows a gain
	int exploreMaxTurns = 4;           ///< exploration targets at most this many days away
	int collectorsTakeGarrison = 0;    ///< collectors pick up garrison troops to ferry them 0 since 17.11.2026: garrison troops stay in the towns until the main hero comes - weak heroes carrying them lost 230k of army per game in the field, without 110k
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
	int couriers = 0;                  ///< secondary heroes fetch troops waiting in our towns and bring them to the main hero (trips of up to 8 days, also beyond our territory); the main hero keeps its role unless another army is three times as strong, and walks home for troops only at 35 % of their worth. Off until a test shows a gain
	int supplyRuns = 0;                ///< 1: one hero at a time, standing in a town of ours, brings what has grown there to the main hero as soon as it is worth supplyShare of the leader's army and the leader is within supplyDays; other heroes leave garrison troops alone; while a run is on its way the main hero does not walk home for troops. Off until a test shows a gain
	double supplyShare = 0.2;          ///< supply runs: smallest load worth a run, as a share of the main hero's army
	int supplyDays = 4;                ///< supply runs: the leader must be within this many days' march
	double earlyGoldDays = 21;         ///< until this day gold is scarce whatever the stock says (buildings are waiting): income buildings count in full, fading out over the two weeks after. 0: off. On by default: +0.05 and +0.12 score in two tests on medium maps (town hall and city hall in week one)
	int earlyGoldContact = 0;          ///< 1: "gold first" ends when the first enemy hero or town comes into view (then troops count, fading within a week) - on small maps that is early, on large ones late. Off until a test shows a gain
	int heroesWeek1 = 2;               ///< heroes wanted in the first week (hiring schedule)
	int heroesWeek3 = 3;               ///< ... until day 21
	int heroesLater = 4;               ///< ... afterwards
	int sellSurplus = 0;               ///< resources piling up are traded for the ones our buildings lack. Off until a test shows a gain
	int skillChoice = 0;               ///< secondary skills are valued by the hero's role and army (what the skill's bonuses do) instead of one fixed ranking for everybody. Off until a test shows a gain
	int resourcePlan = 0;              ///< mines and resource piles are valued by what our towns will need (buildings still missing, weekly creature costs) against stock and income - before a building is blocked. Off until a test shows a gain
	int artifactCare = 0;              ///< artifacts are valued by what they do for the hero that would carry them; heroes wear the best per slot, hand artifacts on when they meet, assemble sets; artifacts on the map are targets by that value. Off until a test shows a gain
	int tables = 0;                    ///< buildings without a rule of their own are valued by the public table of their kind: what a visit can bring this hero now - resources by need, creatures by what the army gains, skills, spells, spell points, movement, artifacts, land revealed. 1: unguarded buildings only; 2: creature banks and other guarded buildings too - the fight against every set of guards the table allows is simulated in the bank's battle layout, and at the door, where the game shows the guards, the hero turns back if these are too strong. Off until a test shows a gain
	int sites = 0;                     ///< buildings with rules of their own that FairAI walked past so far: Hill Fort (the main hero upgrades its army there when the gain is worth the price), University (skills for the main hero), Refugee Camp and War Machine Factory, Obelisk, Keymaster's Tent and Border Guard, garrisons of others in the way, Abandoned Mine (100-199 troglodytes, a mine afterwards), Hut of the Magi, lighthouses and other buildings one flags. Off until a test shows a gain
	int armyFit = 0;                   ///< troops are judged by what they do to the army as it fights, not by their face value: the game takes morale from mixed armies (one faction +1, each further one a point less, undead among the living one less again), so a handful of skeletons can cost a living army more than they add. Applies to which stacks a hero takes over (towns, other heroes, creatures offered), what it buys at dwellings and camps on the map, what goes into the garrison instead of the army in a town of another faction, and what such places are worth as targets. Off until a test shows a gain
	int fastKeeper = 0;                ///< a hero that hands over its army keeps one creature of its fastest kind (a hero marches as fast as its slowest troop; until now it kept one of whichever stack came last, often the slowest). Off until a test shows a gain
	double idleRho = 2.0;              ///< sites: gold counts as lying idle (free for artifacts) from this ratio of income to what our towns can spend on troops
	double idleReserve = 10000;        ///< sites: ... and only what exceeds this much (or two weeks of troops, if that is more)
	double manaValueMain = 4;          ///< tables: gold per spell point a building restores to the main hero (a quarter of it for the others)
	double moveValueMain = 0.4;        ///< tables: gold per movement point a building gives the main hero today (a third of it for the others)

	// --- combat (read by the battle AI of the same player)
	int spellSearch = 0;               ///< hero spells chosen by the look-ahead search (0: VCMI's BattleAI evaluator)
	int simAbilities = 1;              ///< battle look-ahead knows special abilities: life drain, regeneration, fire shield, charge bonus, strike and return, luck, morale (0: plain damage only)
	int simMultiHex = 0;               ///< battle look-ahead knows attacks that reach several hexes: breath behind the target (dragons, phoenix), further heads (cerberus), bursting shots (lich, magog) - our own troops in the way included. Off until the unit lab and the guard benchmark confirm it
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
