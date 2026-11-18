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
	int primaryByArmy = 0;             ///< 1: a point of attack or defence counts for a leader what it adds to the army it leads (about 1.7 per cent of its field strength at ten points, on twice the army it leads now), never less than the flat primarySkillValueMain: stat buildings, artifacts and rewards. Off until a test shows a gain
	int boldBehind = 0;                ///< 1: behind in towns, the main hero takes the battle with an enemy hero at a smaller edge - heroEdge less a tenth for every town the enemy holds more than we do (thieves' guild and towns seen), down to boldEdge. Level or ahead nothing changes. In lost games our main army was the bigger one until day 84 (268,000 against 209,000) while the enemy held twice our towns; we beat big enemy armies only at 3.5 times their size, the enemy beat ours at 1.6 times. Off until a test shows a gain
	double boldEdge = 1.05;            ///< see boldBehind
	int nightWatch = 0;                ///< 1: who sleeps inside the walls. The game lets an attacker meet a hero that stands in the gate of a town in the open field, alone, as long as another hero holds the garrison - walls and garrison do nothing for it (logged games: armies of 23,000 to 104,000 lost in the gate of towns we held). With this, at the end of a turn in which an enemy could reach such a town with more than the hero in the gate can beat, the better commander of the two goes inside and takes the best troops of both; the other waits in the gate with what is left over. Next morning everybody has its own troops back. Off until a test shows a gain
	int surrender = 0;                 ///< 1: in a battle our simulator sees as lost the hero buys its army free (surrender) when we have the gold and the army is worth more to us than the gold; hired back from the tavern at once. 2: only an army of surrenderMinArmy or more, paid from gold that is left once all troops waiting in our dwellings are paid for, not twice within three days, and hired back in the town with a tavern that is least threatened (version 1 bought armies of a few thousand free day after day next to the enemy that had beaten them: towns on day 84 -0.42 +- 0.30). Off until a test shows a gain
	double surrenderMinArmy = 20000;   ///< (surrender 2) smaller armies are not bought free
	int veterans = 2;                  ///< 1: an experienced hero in the tavern (level 6 or more: ours that fled, or anybody's fallen hero) is hired at once, beyond the usual number of heroes (never tested on its own). 2 (standard since 18.11.2026 - a rare situation, checked in staged games with FAI_TEST_FLEE): such a hero is hired at once only if it is clearly the better leader - nobody leads, or it is five levels or more above the hero that leads now and worth a tenth more to an army. Then it leads again: it is hired where most of our troops at home stand, takes them over, and keeps the lead for a week while the rest is brought to it. 3: both (our main hero flees from a lost battle with its levels, spells and artifacts and waits in the tavern until the week ends - until now nobody hired it, and a hero of level 1 led what was left).
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
	int idleBuild = 0;                 ///< 1: gold that lies idle is built with - when all troops waiting in our dwellings are paid for and idleGold more remains, a town builds its best option even below the usual score (0.25), as long as it has a value and needs no trading. In long games 100,000 to 250,000 gold lay unused from day 80 on while mage guilds above level 1 and other buildings of small value were missing. Off until a test shows a gain
	double idleGold = 20000;           ///< (idleBuild) gold that stays in the chest after such a building
	int armyFirst = 0;                 ///< 1: a building that would leave troops unbought in our dwellings is built only if it is worth more per gold piece than those troops (until now buildings always came first, and the main hero led a few thousand in weeks two and three). Off until a test shows a gain
	int townAmbush = 0;                ///< 1: within a day's march of an enemy town, the strongest enemy hero seen in the last three weeks but not visible today counts as a threat (it may sit in the town). Off until a test shows a gain
	int fortress = 0;                  ///< 1: a main hero that is outmatched - an enemy army stronger than fortressRatio times its own was seen within fortressDays near our towns or near the hero (or beat one of our armies there) - ends every day in a town of ours: outside it heads home, from a town it only makes trips that end back there the same day. Measured: whenever the enemy attacked our biggest army it won (55 of 56), and the armies lost that way in the open were mostly led by heroes hired after the leader had fallen (level 1 to 4, 30k to 100k and more). Off until a test shows a gain
	double fortressRatio = 1.2;        ///< see fortress (the armies that attacked our main army were 1.5 times its size in the median; at 1.0 a hero three per cent behind sat in its town for eleven days)
	int fortressDays = 10;             ///< see fortress
	double fortressRadius = 36;        ///< see fortress: tiles from the place of the sighting to one of our towns or to the hero
	int heroChoice = 0;                ///< 1: the hero hired from the tavern is the one worth most to us - its troops, its secondary skills for the role it will play (collector, or leader if we have none), and what its specialty brings in day by day (350 gold, a rare resource). Until now the one with the strongest starting army was taken. Off until a test shows a gain
	int magicFocus = 0;                ///< 1: mage guilds are built for what their spells do for the main hero - all five levels, valued by whether the hero can learn them (spellbook, Wisdom) -, and the main hero visits a town of ours whose guild holds spells it can learn but has not got. Until now levels 2 and 3 were worth next to nothing and 4 and 5 were never built; the enemy's heroes cast Implosion and Chain Lightning, ours Stone Skin. Off: with skillChoice it cost 0.18 (the army fell behind from day 57 on - guilds before dwellings). 2: situational - guild levels are worth 15 per cent of that, too little to be built before dwellings but enough for gold that lies idle (idleBuild), and the main hero still visits a town whose guild holds spells it can learn. Off until a test shows a gain
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
	int tables = 2;                    ///< buildings without a rule of their own are valued by the public table of their kind: what a visit can bring this hero now - resources by need, creatures by what the army gains, skills, spells, spell points, movement, artifacts, land revealed. 1: unguarded buildings only; 2: creature banks and other guarded buildings too - the fight against every set of guards the table allows is simulated in the bank's battle layout, and at the door, where the game shows the guards, the hero turns back if these are too strong. Standard since 17.11.2026: 95 long games with all twelve factions, score +0.157 ± 0.125, 24 wins against 11
	int sites = 0;                     ///< buildings with rules of their own that FairAI walked past so far: Hill Fort (the main hero upgrades its army there when the gain is worth the price), University (skills for the main hero), Refugee Camp and War Machine Factory, Obelisk, Keymaster's Tent and Border Guard, garrisons of others in the way, Abandoned Mine (100-199 troglodytes, a mine afterwards), Hut of the Magi, lighthouses and other buildings one flags. Off until a test shows a gain
	int armyFit = 0;                   ///< troops are judged by what they do to the army as it fights, not by their face value: the game takes morale from mixed armies (one faction +1, each further one a point less, undead among the living one less again), so a handful of skeletons can cost a living army more than they add. Applies to which stacks a hero takes over (towns, other heroes, creatures offered), what it buys at dwellings and camps on the map, what goes into the garrison instead of the army in a town of another faction, and what such places are worth as targets. Off until a test shows a gain
	int fastKeeper = 0;                ///< a hero that hands over its army keeps one creature of its fastest kind (a hero marches as fast as its slowest troop; until now it kept one of whichever stack came last, often the slowest). Off until a test shows a gain
	int breakout = 0;                  ///< 1: a hero that leads an army and is shut in - two weeks in which less than a fiftieth of the map was uncovered (a tenth or more still unseen), and no foreign town among its targets - values a guard with unseen land behind it like an enemy town (enemyTownValue, growing with the days as towns do), by how much of the map around the land behind the guard is unseen. In logged games armies of 200,000 to 700,000 stayed in their corner of the map for a hundred days because every guard at the border cost more than the few hundred gold it "opened". Off until a test shows a gain 2: the same, but a way out that has just been opened is walked through first - no further ways out for three days after one fell, and none while new land keeps showing (half a per cent of the map in three days).
	int passage = 0;                   ///< 1: the tiles a guard watches on its far side count as land behind it (on generated maps the guard between two regions stands in a gap one tile wide and the view ends right behind it: such a guard "opened" nothing and was worth its experience); with regionValue a region counts where the land we know behind a guard runs on into land we have not seen, by the share of the map around that is unseen - no longer for every guard with unseen tiles around it (unseen rock around a treasure pocket). Off until a test shows a gain
	double idleRho = 2.0;              ///< sites: gold counts as lying idle (free for artifacts) from this ratio of income to what our towns can spend on troops
	double idleReserve = 10000;        ///< sites: ... and only what exceeds this much (or two weeks of troops, if that is more)
	double manaValueMain = 4;          ///< tables: gold per spell point a building restores to the main hero (a quarter of it for the others)
	double moveValueMain = 0.4;        ///< tables: gold per movement point a building gives the main hero today (a third of it for the others)

	// --- combat (read by the battle AI of the same player)
	int spellSearch = 0;               ///< hero spells in battle. 0: VCMI's BattleAI evaluator (casts whenever a spell gains anything). 1: chosen by two rounds of look-ahead. 2: a spell is cast when it changes how the fight ENDS (troops saved, or a lost fight won) by more than its spell points are worth - no bolts into guards we beat without a loss
	double manaShare = 0.15;           ///< spellSearch 2: what a full store of spell points is worth, as a share of the army it fights for
	int spellForecast = 0;             ///< fight forecasts on the map play our hero's damage spells: what each does to every enemy stack (immunities, protection, weakness to the spell, the chance to resist), one spell a round while the spell points last, and the spell points a plan uses count against it at the price of manaShare. A hero without spell points forecasts as what it is: weaker. Off until a test shows a gain
	int magicWorth = 0;                ///< 1: what an enemy hero's magic adds to its army is taken from measurement, not from a guess. 1,344 staged battles against BattleAI (arena/magic_calib.py; heroes alike, armies of 110,000 at five size ratios): mass Slow stands for 1.05 times the army (assumed until now: 1.6), mass Haste and Prayer the same, Berserk for nothing, Blind for 1.2, Resurrection for 1.15, damage spells for 1 + 0.7 x (the damage the hero can cast in a battle / the health of our army) - 1.11 at spell power 5, 1.52 at 12 against those armies. With the old values a main hero twice the size of the enemy's kept away from it for sixty days. Off until a test with whole games confirms it
	int safeParts = 0;                 ///< 1: a hero that is not the main hero and carries a real army (heavyPartValue and heavyPartShare of the main army) ends every day in a town of ours or with the main hero: from a town only trips that end back there today, outside a town the way to one; a supply courier leaves only when it reaches the leader the same day. In 93 logged games heroes carrying 10,000 to 100,000 were caught in the field 1.5 to 3.3 times a game and lost 42,000 to 84,000 for a quarter of that in enemy losses - after a supply run that was given up the courier went on collecting mines with 64,000. Off until a test shows a gain
	int trappedHome = 1;               ///< 1: a main hero that is threatened where it stands, finds nothing safe in reach and cannot get further away marches to a town of ours however far it is (before: only when a town was less than two days away - otherwise it stood still with all its movement left). In a logged game it stood 28 days in the open facing an enemy hero of its own strength (105,000 against 107,000), three days from home, where 187,000 and later 370,000 of troops were waiting; then it was caught. A defect put right on 17.11.2026; 0 restores the old behaviour for comparison
	int townHop = 0;                   ///< 1: a foreign town the hero takes today is ours tonight - for the question whether the hero is safe there its walls count for us (defenceWallsFort / defenceWallsCitadel), as they do in a town we own already. Without it an enemy army of our own strength within reach kept every enemy town "unsafe" although behind its walls we would stand half as strong again. Off until a test shows a gain
	int freshHeroes = 0;               ///< 1: an enemy hero we have never fought is taken for one hired about two weeks before we first saw it - when that was more than two weeks after we first met its owner and the thieves' guild does not name it as the owner's best. Until now every unknown hero leading a big army counted as grown since day 1. In 96 first meetings of seven logged games 78 heroes had 14 skill points or fewer; those leading 150,000 and more showed attack 1.6 and defence 3.8 where we had assumed 8.7 and 8.9 - their armies counted a third too strong (1.44 against 1.13). Off until a test shows a gain
	int townHeroes = 0;                ///< 1: garrisons with legs. A hero that holds a town's troops (heavyPartValue / heavyPartShare) is that town's hero: it takes no collector's or scout's place, is played every day on the safeParts leash (trips that end back in the town today, the raider that comes within reach, a town it can take today), spends the night in the garrison, and leaves for a town where it can stand when the town cannot hold against what reaches it by tomorrow. A town with troops and no hero of ours hires one (gold lies idle). 549 long games: the enemy takes 4.7 of our towns a game without a battle and 5.3 with one; attacked garrisons hold in 2 % of 2,895 cases and lose 162,000 a game for 56,000; won games differ from undecided ones by towns not lost (4.6 against 8.7), not by towns taken (10.5 against 11.0). Off until a test shows a gain
	double evacuateRatio = 1.25;       ///< townHeroes: the army leaves a town when what can reach it by tomorrow counts this many times what stands behind the walls. The enemy attacks towns it clearly outweighs (defenders at 0.13 to 0.68 of the attackers' army value in 2,895 logged defences), not at parity
	int townKeep = 0;                  ///< 1: keeping a town of ours that cannot hold on its own counts what taking an enemy town counts under townWorth (twice enemyTownValue, growing with the day), by the share of the threat the town cannot meet - instead of townDefenceValue (10,000). A town lost is a town the enemy gains; won games differ from undecided ones by towns not lost (4.6 against 8.7 a game), not by towns taken. Off until a test shows a gain
	double lastTowns = 0;              ///< townWorth: an enemy town counts (1 + lastTowns / the towns we know that enemy to hold) times as much - the fewer towns an enemy has left, the more each of them is the game itself (with 2: its last town counts three times, one of four one and a half times). 0: every town the same. Off until a test shows a gain
	int heroLossPause = 1;             ///< 1: after losing a hero only the minimum (two heroes) is replaced for a week - every lost hero feeds the enemy experience. 0: no pause while more than 15,000 gold lie idle (the enemy keeps eight heroes moving at all times; ours fell to two or three, with 65,000 to 150,000 gold unspent). 0 is off until a test shows a gain
	int spareStock = 0;                ///< 1: creatures a town cannot buy because its garrison has no free slot are bought by a hero of ours standing in the gate (not the main hero, not a courier); when nobody stands there and the stock is a real army (heavyPartValue), a hero is hired for it. In logged games 12,000 to 35,000 gold worth of creatures per town stayed unbought late in the game for want of a slot, with 100,000 and more gold in the bank - and the enemy buys them when it takes the town. Off until a test shows a gain
	int secondArmy = 0;                ///< 1: troops in a town that would let a hero beat secondArmyEdge times the strongest enemy hero seen in the last three weeks are taken by a hero standing there (one is hired if nobody is), and a hero that strong is a second leader beside the main hero: it is played like one (not kept to our territory, not on the safeParts leash, fights judged by heroEdge, experience and skill points count as for the leader, fetches more troops in our towns) and hands its troops to the main hero only when that would add half the main hero's army. Only while the enemies' best heroes have been seen lately. 549 long games: on day 84 the main hero leads 249,000 and 313,000 lie in garrisons; attacked garrisons hold in 2 % of 2,895 cases and lose 162,000 a game for 56,000; in a logged game with safeParts a hero sat in the home town for 70 days with up to 363,000. Off until a test shows a gain
	double secondArmyEdge = 1.3;       ///< secondArmy: field strength needed, in multiples of the strongest enemy hero seen lately (heroEdge: from 1.3 on every logged battle was won); a second leader keeps its role down to three quarters of that
	int townWorth = 0;                 ///< 1: a foreign town is worth what it yields - twice enemyTownValue (neutralTownValue for a neutral one), growing with the day - to every hero on every day, not only to the main hero on campaign. Off campaign an enemy town counted 20,000: less than a gold mine in enemy hands (42,000) and no more than chasing one of the enemy's lesser heroes. In a logged game the main hero, 1.7 times the strongest enemy hero and so short of the campaign's edge, spent 65 days flagging mines and hunting heroes while the enemy held four towns to our one; in 200 undecided games of 518 we stood at three towns against four from day 56 to day 150 with armies level. Off until a test shows a gain
	double heavyPartValue = 15000;     ///< safeParts: an army from this value on ...
	double heavyPartShare = 0.05;      ///< ... and from this share of the main hero's army on is not left in the open
	int simAbilities = 1;              ///< battle look-ahead knows special abilities: life drain, regeneration, fire shield, charge bonus, strike and return, luck, morale (0: plain damage only)
	int simMultiHex = 0;               ///< battle look-ahead knows attacks that reach several hexes: breath behind the target (dragons, phoenix), further heads (cerberus), bursting shots (lich, magog) - our own troops in the way included. Off until the unit lab and the guard benchmark confirm it
	int simAfterHit = 0;               ///< battle look-ahead and forecasts know what attacks do beyond their damage: death stare (mighty gorgons, accurate shot), petrifying, paralysing, blinding and freezing, ageing, poison, curse, weakness, disease, acid breath, thunderbolt - as expected values, read from the units' bonuses and the spells they name. Off until the unit lab confirms it
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
