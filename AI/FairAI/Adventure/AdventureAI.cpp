/*
 * AdventureAI.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AdventureAI.h"
#include <cstdio>
#include <fstream>
#include "../Combat/CombatSim.h"

#include "../FairAI.h"
#include "../Perception/Perception.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include <vcmi/Faction.h>
#include "../../../lib/CConfigHandler.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/entities/ResourceTypeHandler.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/artifact/ArtBearer.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/entities/artifact/ArtifactUtils.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGMarket.h"
#include "../../../lib/mapObjects/CQuest.h"
#include "../../../lib/mapObjects/FlaggableMapObject.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/IMarket.h"
#include "../../../lib/mapObjects/IOwnableObject.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/ResourceSet.h"
#include "../../../lib/gameState/UpgradeInfo.h"
#include "../../../lib/gameState/QuestInfo.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/PathfindingRules.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/TerrainHandler.h"
#include "../../../lib/RoadHandler.h"

#include <queue>

namespace FAI
{

namespace
{
constexpr int MAX_STEPS_PER_HERO = 300;
constexpr int MAX_FAILURES_PER_HERO = 8;
constexpr double TURN_TIME_BUDGET_SEC = 30.0;

/// Secondary skill preference, indexed by the standard HoMM3 skill order
/// (pathfinding, archery, logistics, scouting, diplomacy, navigation, leadership, wisdom,
///  mysticism, luck, ballistics, eagle eye, necromancy, estates, fire, air, water, earth,
///  scholar, tactics, artillery, learning, offence, armorer, intelligence, sorcery,
///  resistance, first aid)
constexpr std::array<double, 28> SKILL_PRIORITY = {
	3, 6, 10, 2, 4, 0, 6, 6,
	2, 4, 3, 0, 9, 4, 5, 7, 5, 8,
	1, 5, 3, 2, 8, 8, 3, 3,
	5, 1
};
constexpr int FIRST_MAGIC_SCHOOL = 14;
constexpr int LAST_MAGIC_SCHOOL = 17;

double stackValue(const CStackInstance * stack)
{
	if(!stack || !stack->getCreature())
		return 0;
	return static_cast<double>(stack->getCreature()->getAIValue()) * stack->getCount();
}

/// Weighted count of unexplored tiles a hero would reveal standing on a tile
/// (same circle as CGameInfoCallback::getTilesInRange: floor(distance) <= radius)
class RevealGrid
{
public:
	RevealGrid(const CCallback & cb, const std::set<int3> & stale, double staleWeight)
	{
		size = cb.getMapSize();
		weights.assign(static_cast<size_t>(size.x) * size.y * size.z, 0.0f);
		for(int z = 0; z < size.z; ++z)
			for(int y = 0; y < size.y; ++y)
				for(int x = 0; x < size.x; ++x)
				{
					int3 tile(x, y, z);
					if(!cb.isVisible(tile))
						weights[index(x, y, z)] = stale.count(tile) ? static_cast<float>(staleWeight) : 1.0f;
				}
	}

	double count(const int3 & c, int r)
	{
		if(r != cachedRadius)
		{
			offsets.clear();
			for(int dx = -r; dx <= r; ++dx)
				for(int dy = -r; dy <= r; ++dy)
					if(dx * dx + dy * dy < (r + 1) * (r + 1))
						offsets.emplace_back(dx, dy);
			cachedRadius = r;
		}
		double total = 0;
		for(const auto & [dx, dy] : offsets)
		{
			int x = c.x + dx;
			int y = c.y + dy;
			if(x >= 0 && y >= 0 && x < size.x && y < size.y)
				total += weights[index(x, y, c.z)];
		}
		return total;
	}

	/// unexplored tiles (weighted) in the square of side 2r+1 around a tile: is there a whole region out there?
	double countBox(const int3 & c, int r)
	{
		if(sums.empty())
		{
			// summed-area table per level, with a border of zeros
			const size_t w = size.x + 1;
			sums.assign(w * (size.y + 1) * size.z, 0.0);
			for(int z = 0; z < size.z; ++z)
				for(int y = 0; y < size.y; ++y)
					for(int x = 0; x < size.x; ++x)
						sumAt(x + 1, y + 1, z) = weights[index(x, y, z)] + sumAt(x, y + 1, z) + sumAt(x + 1, y, z) - sumAt(x, y, z);
		}
		const int x0 = std::max(0, c.x - r);
		const int y0 = std::max(0, c.y - r);
		const int x1 = std::min(size.x, c.x + r + 1);
		const int y1 = std::min(size.y, c.y + r + 1);
		return sumAt(x1, y1, c.z) - sumAt(x0, y1, c.z) - sumAt(x1, y0, c.z) + sumAt(x0, y0, c.z);
	}

	/// share of unexplored tiles in that square (the part of it that lies on the map)
	double boxShare(const int3 & c, int r)
	{
		const int x0 = std::max(0, c.x - r);
		const int y0 = std::max(0, c.y - r);
		const int x1 = std::min(size.x, c.x + r + 1);
		const int y1 = std::min(size.y, c.y + r + 1);
		return countBox(c, r) / std::max(1, (x1 - x0) * (y1 - y0));
	}

	/// does the land we know end here - is a tile next to this one unexplored?
	bool edge(const int3 & c) const
	{
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const int x = c.x + dx;
				const int y = c.y + dy;
				if((dx || dy) && x >= 0 && y >= 0 && x < size.x && y < size.y && weights[index(x, y, c.z)] > 0)
					return true;
			}
		return false;
	}

private:
	int3 size;
	std::vector<float> weights;
	std::vector<double> sums;
	std::vector<std::pair<int, int>> offsets;
	int cachedRadius = -1;
	size_t index(int x, int y, int z) const { return (static_cast<size_t>(z) * size.y + y) * size.x + x; }
	double & sumAt(int x, int y, int z) { return sums[(static_cast<size_t>(z) * (size.y + 1) + y) * (size.x + 1) + x]; }
};

}

// ======================================================================= params

void Params::loadOverrides(int playerIndex)
{
	// 1. arena settings, per player; 2. FAI_PARAMS environment variable (JSON object, all FairAI players)
	JsonNode values;
	const JsonNode & list = settings["session"]["arena"]["fairaiParams"];
	if(list.isVector() && playerIndex >= 0 && playerIndex < static_cast<int>(list.Vector().size()))
		values = list.Vector()[playerIndex];
	if(!values.isStruct())
	{
		const char * env = std::getenv("FAI_PARAMS");
		if(!env || !*env)
			return;
		try
		{
			values = JsonNode(reinterpret_cast<const std::byte *>(env), std::strlen(env), "FAI_PARAMS");
		}
		catch(const std::exception & e)
		{
			logAi->error("FairAI: cannot parse FAI_PARAMS: %s", e.what());
			return;
		}
	}
	if(!values.isStruct())
		return;
	logAi->info("FairAI: parameter overrides %s", values.toCompactString());

	auto read = [&values](const char * name, auto & field)
	{
		const JsonNode & v = values[name];
		if(v.isNumber())
			field = static_cast<std::remove_reference_t<decltype(field)>>(v.Float());
	};
	read("goldPerArmyValue", goldPerArmyValue);
	read("resourceValueFactor", resourceValueFactor);
	read("safetyRatio", safetyRatio);
	read("dangerUpperWeight", dangerUpperWeight);
	read("safetyRatioHero", safetyRatioHero);
	read("safetyRatioSecondary", safetyRatioSecondary);
	read("lossFactor", lossFactor);
	read("lossAversion", lossAversion);
	read("expValueMain", expValueMain);
	read("expValueOther", expValueOther);
	read("unlockFactor", unlockFactor);
	read("travelOffset", travelOffset);
	read("stickiness", stickiness);
	read("explorationTileValue", explorationTileValue);
	read("staleTileWeight", staleTileWeight);
	read("maxTravelTurns", maxTravelTurns);
	read("incomeHorizonDays", incomeHorizonDays);
	read("dwellingHorizonWeeks", dwellingHorizonWeeks);
	read("primarySkillValueMain", primarySkillValueMain);
	read("primarySkillValueOther", primarySkillValueOther);
	read("ferryFactor", ferryFactor);
	read("reinforceFactor", reinforceFactor);
	read("heroGoldReserve", heroGoldReserve);
	read("threatRadius", threatRadius);
	read("enemyHeroFactor", enemyHeroFactor);
	read("threatTolerance", threatTolerance);
	read("threatToleranceSecondary", threatToleranceSecondary);
	read("defenceWallsFort", defenceWallsFort);
	read("defenceWallsCitadel", defenceWallsCitadel);
	read("fleeGain", fleeGain);
	read("standFast", standFast);
	read("antiOscillation", antiOscillation);
	read("boats", boats);
	read("townDefenceValue", townDefenceValue);
	read("retreatValue", retreatValue);
	read("collectorRadius", collectorRadius);
	read("wallsFactor", wallsFactor);
	read("maxHeroes", maxHeroes);
	read("keepGarrison", keepGarrison);
	read("collectorsRoam", collectorsRoam);
	read("tavernArmies", tavernArmies);
	read("garrisonShare", garrisonShare);
	read("mageGuildValue", mageGuildValue);
	read("buyWarMachines", buyWarMachines);
	read("pathThreats", pathThreats);
	read("townGuard", townGuard);
	read("siegeCost", siegeCost);
	read("baitStacks", baitStacks);
	read("scouts", scouts);
	read("moveSpells", moveSpells);
	read("concentrate", concentrate);
	read("threatGrowth", threatGrowth);
	read("neutralTownValue", neutralTownValue);
	read("enemyTownValue", enemyTownValue);
	read("spellSearch", spellSearch);
	read("simAbilities", simAbilities);
	read("simMultiHex", simMultiHex);
	read("simAfterHit", simAfterHit);
	read("manaShare", manaShare);
	read("spellForecast", spellForecast);
	read("magicWorth", magicWorth);
	read("safeParts", safeParts);
	read("townWorth", townWorth);
	read("secondArmy", secondArmy);
	read("townHeroes", townHeroes);
	read("spareStock", spareStock);
	read("townKeep", townKeep);
	read("lastTowns", lastTowns);
	read("heroLossPause", heroLossPause);
	read("evacuateRatio", evacuateRatio);
	read("secondArmyEdge", secondArmyEdge);
	read("freshHeroes", freshHeroes);
	read("trappedHome", trappedHome);
	read("townHop", townHop);
	read("heavyPartValue", heavyPartValue);
	read("heavyPartShare", heavyPartShare);
	read("battlePlan", battlePlan);
	read("guardOracle", guardOracle);
	read("guardOracleUntil", guardOracleUntil);
	read("guardModel", guardModel);
	read("ownCasters", ownCasters);
	read("planVsPlayers", planVsPlayers);
	read("modelSearch", modelSearch);
	read("detour", detour);
	read("kitePlan", kitePlan);
	read("wholeCreatures", wholeCreatures);
	read("forecastPlans", forecastPlans);
	read("fightForecast", fightForecast);
	read("maxLossShare", maxLossShare);
	read("maxLossShareIdle", maxLossShareIdle);
	read("maxLossShareSecondary", maxLossShareSecondary);
	read("forecastCalib", forecastCalib);
	read("forecastCalibWeight", forecastCalibWeight);
	read("forecastCalibMemory", forecastCalibMemory);
	read("forecastSafeShare", forecastSafeShare);
	read("guardKillValue", guardKillValue);
	read("couriers", couriers);
	read("earlyGoldDays", earlyGoldDays);
	read("heroesWeek1", heroesWeek1);
	read("heroesWeek3", heroesWeek3);
	read("heroesLater", heroesLater);
	read("sellSurplus", sellSurplus);
	read("skillChoice", skillChoice);
	read("resourcePlan", resourcePlan);
	read("artifactCare", artifactCare);
	read("tables", tables);
	read("sites", sites);
	read("armyFit", armyFit);
	read("fastKeeper", fastKeeper);
	read("passage", passage);
	read("breakout", breakout);
	read("breakoutSlack", breakoutSlack);
	read("breakoutSlackDays", breakoutSlackDays);
	read("breakoutReady", breakoutReady);
	read("worstCaseRatio", worstCaseRatio);
	read("boatGuards", boatGuards);
	read("breakoutLoss", breakoutLoss);
	read("idleBuild", idleBuild);
	read("saveFor", saveFor);
	read("saveDays", saveDays);
	read("saveMinScore", saveMinScore);
	read("towerGuard", towerGuard);
	read("threatSpend", threatSpend);
	read("idleGold", idleGold);
	read("idleRho", idleRho);
	read("idleReserve", idleReserve);
	read("manaValueMain", manaValueMain);
	read("moveValueMain", moveValueMain);
	read("patienceStep", patienceStep);
	read("resourceNeedFactor", resourceNeedFactor);
	read("guardFogFactor", guardFogFactor);
	read("unlockDirect", unlockDirect);
	read("heroModel", heroModel);
	read("heroEdge", heroEdge);
	read("heroTolerance", heroTolerance);
	read("leaderFocus", leaderFocus);
	read("campaignEdge", campaignEdge);
	read("noChase", noChase);
	read("fetchByShare", fetchByShare);
	read("fetchShare", fetchShare);
	read("heroForecast", heroForecast);
	read("surrender", surrender);
	read("surrenderMinArmy", surrenderMinArmy);
	read("nightWatch", nightWatch);
	read("boldBehind", boldBehind);
	read("gather", gather);
	read("gatherDays", gatherDays);
	read("primaryByArmy", primaryByArmy);
	read("boldEdge", boldEdge);
	read("veterans", veterans);
	read("earlyGoldContact", earlyGoldContact);
	read("regionValue", regionValue);
	read("mainHorizon", mainHorizon);
	read("refillShare", refillShare);
	read("passOwn", passOwn);
	read("muster", muster);
	read("musterShare", musterShare);
	read("musterBoost", musterBoost);
	read("supplyRuns", supplyRuns);
	read("armyFirst", armyFirst);
	read("townAmbush", townAmbush);
	read("campaignPrior", campaignPrior);
	read("garrisonRoom", garrisonRoom);
	read("magicFocus", magicFocus);
	read("heroChoice", heroChoice);
	read("fortress", fortress);
	read("fortressRatio", fortressRatio);
	read("fortressDays", fortressDays);
	read("fortressRadius", fortressRadius);
	read("roomFactor", roomFactor);
	read("exploreRegion", exploreRegion);
	read("fetchRoundTrip", fetchRoundTrip);
	read("supplyShare", supplyShare);
	read("supplyDays", supplyDays);
	read("seekTowns", seekTowns);
	read("minSafetyRatio", minSafetyRatio);
	read("exploreMaxTurns", exploreMaxTurns);
	read("huntFactor", huntFactor);
	read("collectorsTakeGarrison", collectorsTakeGarrison);
}

// ======================================================================= setup

AdventureAI::AdventureAI(FairAIGateway & gateway, std::shared_ptr<CCallback> cb, Perception & perception, PlayerColor me)
	: gateway(gateway)
	, cb(std::move(cb))
	, perception(perception)
	, me(me)
{
	params.loadOverrides(me.getNum());
	statsFile = std::getenv("FAI_STATS"); // the test bench asks for statistics of this game
	if(statsFile && !*statsFile)
		statsFile = nullptr;
	Combat::setMultiHex(params.simMultiHex != 0); // the forecasts of the adventure AI use the same simulator
	Combat::setAfterHit(params.simAfterHit != 0);
	Combat::setWholeCreatures(params.wholeCreatures != 0);
}

AdventureAI::~AdventureAI() = default;

ResourceSet AdventureAI::spendable() const
{
	ResourceSet stock = cb->getResourceAmount();
	if(today - earmarkDay <= 3)
	{
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			stock[res] = std::max<TResource>(0, stock[res] - earmark[res]);
	}
	if(params.saveFor && today - savingsDay <= 1)
	{
		// (saveFor) what a town saves for its best building is not there for anything else
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			stock[res] = std::max<TResource>(0, stock[res] - savings[res]);
	}
	return stock;
}

bool AdventureAI::isEnemy(PlayerColor owner) const
{
	return owner.isValidPlayer() && owner != me && cb->getPlayerRelations(me, owner) == PlayerRelations::ENEMIES;
}

bool AdventureAI::isAlly(PlayerColor owner) const
{
	return owner.isValidPlayer() && owner != me && cb->getPlayerRelations(me, owner) != PlayerRelations::ENEMIES;
}

const CGHeroInstance * AdventureAI::ownHero(ObjectInstanceID id) const
{
	if(!id.hasValue())
		return nullptr;
	const CGHeroInstance * hero = cb->getHero(id);
	if(!hero || hero->tempOwner != me)
		return nullptr;
	return hero;
}

void AdventureAI::chooseMainHero()
{
	const CGHeroInstance * current = ownHero(mainHero);
	const CGHeroInstance * strongest = nullptr;
	for(const auto * hero : perception.ownHeroes())
	{
		if(hero->isGarrisoned() && hero->getVisitedTown() == nullptr)
			continue;
		if(supply && supply->courier == hero->id && current)
			continue; // carries the leader's troops: no candidate for the lead
		if(!strongest || hero->getTotalStrength() > strongest->getTotalStrength())
			strongest = hero;
	}
	if(!strongest)
	{
		mainHero = ObjectInstanceID();
		return;
	}
	// The army leader keeps its role (and its experience): couriers bring it the troops. Only an army
	// far bigger than what is left of the leader's takes over (couriers; else at 1.5 times).
	// - as far as the leader is the more experienced hero: between beginners the bigger army leads.
	if(current && current->id == veteranMain && today < veteranUntil)
		return; // (veterans 2) the leader that came back: the troops are on their way to it
	double takeover = 1.5;
	if(params.couriers && current)
		takeover = std::clamp(1.5 + 0.25 * (static_cast<double>(current->level) - static_cast<double>(strongest->level)), 1.5, 3.0);
	if(!current || strongest->getTotalStrength() > takeover * current->getTotalStrength())
		mainHero = strongest->id;
	// The leader fell while a courier was on its way: if the courier leads now, its run is over.
	// (It used to go on "delivering" to itself - and asking the server to merge a stack with itself
	// kills the game: the engine does not check for that.)
	if(supply && supply->courier == mainHero)
	{
		logAi->debug("FairAI %s day %d: supply run ended (the courier leads now)", me.toString(), today);
		supply.reset();
	}
}

bool AdventureAI::isMain(const CGHeroInstance * hero) const
{
	return hero && hero->id == mainHero;
}

bool AdventureAI::leads(const CGHeroInstance * hero) const
{
	return hero && (hero->id == mainHero || secondLeaders.count(hero->id) > 0);
}

bool AdventureAI::heavy(const CGHeroInstance * hero) const
{
	if(!hero || leads(hero))
		return false;
	const CGHeroInstance * leader = ownHero(mainHero);
	return static_cast<double>(hero->getArmyStrength()) >= std::max(params.heavyPartValue, params.heavyPartShare * (leader ? static_cast<double>(leader->getArmyStrength()) : 0.0));
}

bool AdventureAI::contested(const CGTownInstance * town) const
{
	if(!town)
		return false;
	auto gained = townGainedDay.find(town->id);
	if(gained != townGainedDay.end() && today - gained->second <= 14)
		return true;
	return enemySeenNear(town->visitablePos(), 7, 24);
}

bool AdventureAI::exploringStalled() const
{
	// (breakout) Two weeks that uncovered less than a fiftieth of the map, with a tenth of it or more
	// still unseen: whatever there is left to explore without a fight is not worth the way.
	if(mapTiles <= 0)
		return false;
	auto now = seenTilesOn.find(today);
	if(now == seenTilesOn.end() || now->second >= 0.9 * mapTiles)
		return false;
	// tiles uncovered since the last count taken so many days ago or earlier (-1: no such count)
	auto uncoveredIn = [&](int days) -> double
	{
		auto before = seenTilesOn.upper_bound(today - days);
		if(before == seenTilesOn.begin())
			return -1;
		--before;
		return now->second - before->second;
	};
	bool stalled = false;
	if(today >= 21)
	{
		const double twoWeeks = uncoveredIn(14);
		stalled = twoWeeks >= 0 && twoWeeks < 0.02 * mapTiles;
	}
	if(!stalled && params.breakout == 3 && today >= 14 && mainSlackDays >= params.breakoutSlackDays)
	{
		// (breakout 3) Nothing left to do here: the main hero has spent its last days on things not
		// worth its army (and, by the rule of level 2 below, the last three days showed next to no
		// new land). Waiting for two full weeks of standstill is waiting while the enemy takes the
		// towns between us.
		stalled = true;
	}
	if(!stalled)
		return false;
	if(params.breakout >= 2)
	{
		// (breakout 2) A way out that has just been opened is walked through first: for three days
		// after a guard on the way out fell, and for as long as new land keeps showing (half a per
		// cent of the map in three days), nobody is shut in. Version 1 went from one way out to the
		// next - eight guards in sixteen days in a probe game, across the whole map, before the
		// hero walked through any of the gaps.
		if(today < breakoutPauseUntil)
			return false;
		const double lately = uncoveredIn(3);
		if(lately >= 0.005 * mapTiles)
			return false;
	}
	return true;
}

double AdventureAI::secondArmyBar() const
{
	// What a second army must be able to beat on its own: the strongest enemy hero seen in the last
	// three weeks, in the measure of the hero model. Only while we know the enemies' best heroes -
	// an army that beats the scouts we happened to see is no match for the one we did not.
	if(!params.secondArmy || !params.heroModel || !oppositionKnown || oppositionHeroes <= 0)
		return -1;
	return std::max(params.secondArmyEdge * oppositionHeroes, params.heavyPartValue);
}

void AdventureAI::updateSecondLeaders()
{
	const double bar = secondArmyBar();
	std::set<ObjectInstanceID> now;
	if(bar > 0)
	{
		for(const auto * hero : perception.ownHeroes())
		{
			if(hero->id == mainHero || (supply && supply->courier == hero->id))
				continue;
			const double strength = fieldStrength(hero);
			// (a leader keeps its role down to three quarters of the bar: no change of rules after every fight)
			if(strength >= bar || (secondLeaders.count(hero->id) && strength >= 0.75 * bar))
				now.insert(hero->id);
		}
	}
	for(const auto & id : now)
	{
		if(!secondLeaders.count(id))
			if(const auto * hero = ownHero(id))
				logAi->info("FairAI %s day %d: %s leads a second army (in the field %.0f; needed %.0f, the strongest enemy hero seen lately %.0f)", me.toString(), today,
					hero->getNameTranslated(), fieldStrength(hero), bar, oppositionHeroes);
	}
	for(const auto & id : secondLeaders)
	{
		if(!now.count(id))
			if(const auto * hero = ownHero(id); hero && hero->id != mainHero)
				logAi->info("FairAI %s day %d: %s no longer leads a second army (in the field %.0f; needed %.0f)", me.toString(), today,
					hero->getNameTranslated(), fieldStrength(hero), bar);
	}
	secondLeaders = now;
}

namespace
{
/// (passOwn) A hero of ours on the road is not the end of the road: it moves on, or steps aside when
/// another hero of ours has to pass (AdventureAI::makeWay). The game's rule ends every way at a hero,
/// whoever it belongs to. Left as it is: a hero in a town gate (the gate is the end of a way anyway),
/// one on a tile a monster guards, and one that cannot move any more today on a tile reached today.
class OwnHeroesGiveWay : public MovementAfterDestinationRule
{
	PlayerColor owner;
	const std::set<int3> & wedged; ///< heroes that could not step aside today

public:
	OwnHeroesGiveWay(PlayerColor owner, const std::set<int3> & wedged)
		: owner(owner), wedged(wedged)
	{
	}

protected:
	BlockingReason getBlockingReason(const PathNodeInfo & source, const CDestinationNodeInfo & destination, const PathfinderConfig * config,
		const CPathfinderHelper * helper) const override
	{
		if(destination.action == EPathNodeAction::BLOCKING_VISIT && destination.nodeHero && destination.nodeObject == destination.nodeHero
			&& destination.nodeHero->tempOwner == owner && destination.node->layer == EPathfindingLayer::LAND && !destination.guarded
			&& ((destination.nodeHero->movementPointsRemaining() > 0 && !wedged.count(destination.node->coord)) || destination.turn > 0))
		{
			return BlockingReason::NONE;
		}
		return MovementAfterDestinationRule::getBlockingReason(source, destination, config, helper);
	}
};
}

std::shared_ptr<CPathsInfo> AdventureAI::computePaths(const CGHeroInstance * hero, bool ignoreGuards, int horizon) const
{
	auto result = std::make_shared<CPathsInfo>(cb->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(*result, *cb, hero);
	config->options.ignoreGuards = ignoreGuards;
	// boats we can see and board; monoliths and gates whose exits we know
	config->options.useEmbarkAndDisembark = params.boats != 0;
	config->options.useTeleportTwoWay = true;
	config->options.useTeleportOneWay = true;
	config->options.useTeleportOneWayRandom = false;
	config->options.useTeleportWhirlpool = false;
	config->options.canUseCast = params.moveSpells != 0;
	if(params.passOwn && !config->rules.empty())
		config->rules.back() = std::make_shared<OwnHeroesGiveWay>(me, wedgedToday); // in place of the game's rule for moving on from a tile
	// (horizon: days of march the caller asks about, where that is more than the hero looks ahead by itself)
	// (muster) the main hero's ways reach the town whose troops it is to fetch; other targets count as far as before (collectTargets)
	if(musterTown.hasValue() && hero->id == mainHero)
		horizon = std::max(horizon, 30);
	config->options.turnLimit = static_cast<uint8_t>(std::clamp(std::max(travelHorizon(hero), horizon) + 1, 1, 250));
	cb->calculatePaths(config);
	return result;
}

std::string AdventureAI::stuckAt(const CPathsInfo & paths, const int3 & to, const CPathsInfo * freeWay) const
{
	const int3 size = cb->getMapSize();
	int3 best(-1, -1, -1);
	int bestDistance = std::numeric_limits<int>::max();
	// with a way that leaves the guards aside: the first tile of it that the ways of `paths` do not reach
	if(freeWay)
	{
		const CGPathNode * first = nullptr;
		const CGPathNode * before = nullptr;
		for(const CGPathNode * n = freeWay->getPathInfo(to); n; n = n->theNodeBefore)
		{
			const CGPathNode * mine = paths.getPathInfo(n->coord);
			if(mine && mine->reachable())
			{
				before = n;
				break;
			}
			first = n;
		}
		if(first)
		{
			std::string around;
			for(const auto & [id, object] : perception.knownObjects())
			{
				const int3 & at = object.visitablePos;
				if(at.z != first->coord.z || std::abs(at.x - first->coord.x) > 1 || std::abs(at.y - first->coord.y) > 1)
					continue;
				around += boost::str(boost::format(" [object %d%s at %s%s]") % object.type.getNum()
					% (object.owner == me ? " own" : object.owner.isValidPlayer() ? " foreign" : "") % at.toString() % (object.visibleNow ? "" : ", not in sight"));
			}
			const CGPathNode * mine = paths.getPathInfo(first->coord);
			return boost::str(boost::format("the way without guards leaves what is within reach at %s (day %d of it) for %s (there: accessible %d, on the free way action %d); next to that tile:%s")
				% (before ? before->coord.toString() : std::string("?")) % (before ? static_cast<int>(before->turns) : -1) % first->coord.toString()
				% (mine ? static_cast<int>(mine->accessible) : -1) % static_cast<int>(first->action) % (around.empty() ? std::string(" nothing known") : around));
		}
	}
	for(int x = 0; x < size.x; ++x)
	{
		for(int y = 0; y < size.y; ++y)
		{
			const int3 tile(x, y, to.z);
			const CGPathNode * node = paths.getPathInfo(tile);
			if(!node || !node->reachable())
				continue;
			const int distance = std::max(std::abs(x - to.x), std::abs(y - to.y));
			if(distance < bestDistance)
			{
				bestDistance = distance;
				best = tile;
			}
		}
	}
	if(best.x < 0)
		return "no tile on that level within reach";
	std::string around;
	int unseen = 0;
	for(int dx = -2; dx <= 2; ++dx)
	{
		for(int dy = -2; dy <= 2; ++dy)
		{
			const int3 tile(best.x + dx, best.y + dy, best.z);
			if(!cb->isInTheMap(tile))
				continue;
			if(!cb->isVisible(tile))
				++unseen;
		}
	}
	for(const auto & [id, object] : perception.knownObjects())
	{
		const int3 & at = object.visitablePos;
		if(at.z != best.z || std::abs(at.x - best.x) > 2 || std::abs(at.y - best.y) > 2)
			continue;
		around += boost::str(boost::format(" [object %d%s at %s%s]") % object.type.getNum()
			% (object.owner == me ? " own" : object.owner.isValidPlayer() ? " foreign" : "") % at.toString() % (object.visibleNow ? "" : ", not in sight"));
	}
	return boost::str(boost::format("nearest tile within reach %s, %d from the goal; within two tiles of it:%s; unseen tiles there %d") % best.toString() % bestDistance
		% (around.empty() ? std::string(" nothing known") : around) % unseen);
}

int AdventureAI::travelHorizon(const CGHeroInstance * hero) const
{
	// on campaign the main hero may march on a far town; couriers may follow it for a week to hand over troops
	if(campaign(hero))
		return std::max(params.maxTravelTurns, 30);
	// the main hero looks further than a few days (mainHorizon): a guard that bars the way out of our
	// corner of the map may stand a week's march from where the hero happens to be
	// (townWorth) the main hero goes for foreign towns on any day: as on campaign they may lie weeks
	// away (everything else counts only as far as before - see collectTargets)
	if(isMain(hero) && params.townWorth)
		return std::max(params.maxTravelTurns, 30);
	if(isMain(hero) && params.mainHorizon > params.maxTravelTurns)
		return params.mainHorizon;
	if(supply && supply->courier == hero->id)
		return std::max(params.maxTravelTurns, params.supplyDays + 1);
	return params.couriers && !isMain(hero) ? std::max(params.maxTravelTurns, 8) : params.maxTravelTurns;
}

bool AdventureAI::campaign(const CGHeroInstance * hero) const
{
	return campaignMode && !refillMode && hero && isMain(hero);
}

// ======================================================================= turn

void AdventureAI::playTurn()
{
	const auto turnStart = std::chrono::steady_clock::now();
	today = cb->getDate(Date::DAY);
	failedToday.clear();
	wedgedToday.clear();
	fledToday.clear();
	reservedBy.clear();
	pocketExits.clear();
	trail.clear();
	abandonedToday.clear();
	roundTrip.clear();
	forecastCache.clear();
	perception.refresh();
	updateQuests();
	// sanctuaries we know: a hero standing in one cannot be attacked
	sanctuaries.clear();
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type == Obj::SANCTUARY)
			sanctuaries.insert(object.visitablePos);
	}
	{
		// heroes killed during the enemy's turn
		std::set<ObjectInstanceID> current;
		for(const auto * hero : perception.ownHeroes())
			current.insert(hero->id);
		for(auto id : heroesLastTurn)
		{
			if(!current.count(id))
			{
				lastHeroLossDay = today;
				logAi->info("FairAI %s: a hero was killed during the enemy turn before day %d", me.toString(), today);
			}
		}
		heroesLastTurn = current;
	}
	{
		// (townHeroes) since when each town is ours: one taken lately is one the enemy comes back for
		std::map<ObjectInstanceID, int> now;
		for(const auto * town : perception.ownTowns())
		{
			auto known = townGainedDay.find(town->id);
			now[town->id] = known != townGainedDay.end() ? known->second : (today <= 1 ? -1000 : today);
		}
		townGainedDay = std::move(now);
	}
	restoreNightWatch();
	for(const auto * hero : perception.ownHeroes())
		ownFactorSeen[hero->id.getNum()] = commanderFactor(hero);
	if(params.breakout || statsFile)
	{
		// (breakout) how much of the map we have uncovered, day by day
		const int3 size = cb->getMapSize();
		int seen = 0;
		for(int z = 0; z < size.z; ++z)
			for(int x = 0; x < size.x; ++x)
				for(int y = 0; y < size.y; ++y)
					seen += cb->isVisible(int3(x, y, z)) ? 1 : 0;
		seenTilesOn[today] = seen;
		mapTiles = size.x * size.y * size.z;
	}
	chooseMainHero();
	if(std::getenv("FAI_CHECK_REACH"))
		if(const auto * main = ownHero(mainHero))
			checkReachAgainstEngine(main);
	updateEconomy();
	updateResourcePlan();
	logAi->info("FairAI %s day %d economy: income %.0f/week, growth costs %.0f/week, rho %.2f", me.toString(), today,
		economy.weeklyIncome, economy.weeklyRecruitCost, economy.rho);

	resourceNeed.clear();
	manageTowns(false);
	hireHeroes();
	chooseMainHero();
	updateSecondLeaders();
	for(const auto * hero : perception.ownHeroes())
	{
		if(const auto * town = hero->getVisitedTown())
			manageTownVisit(hero, town);
	}
	updateSecondLeaders();

	auto timeLeft = [&turnStart]()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - turnStart).count() < TURN_TIME_BUDGET_SEC;
	};

	if(const auto * main = ownHero(mainHero))
	{
		const int3 start = main->visitablePos();
		const ObjectInstanceID id = mainHero;
		mainProductive = false;
		mainBestValueToday = 0;
		const double armyGold = static_cast<double>(main->getArmyStrength()) * params.goldPerArmyValue;
		playHero(mainHero);
		const auto * after = ownHero(id);
		// (breakout 3) a slack day: nothing the hero went for was worth a tenth of its army
		if(mainBestValueToday < std::clamp(params.breakoutSlack * armyGold, 3000.0, 8000.0))
			++mainSlackDays;
		else
			mainSlackDays = 0;
		// walking about without visiting, fighting or exploring is idle too (patience lowers the safety ratio)
		if(after && (params.concentrate ? !mainProductive : after->visitablePos().dist2d(start) < 3))
			++idleDays;
		else
			idleDays = 0;
	}

	hireHeroes();
	chooseMainHero();
	updateSecondLeaders();

	scoutsToday.clear();
	std::vector<ObjectInstanceID> others;
	for(const auto * hero : perception.ownHeroes())
	{
		if(hero->id != mainHero)
			others.push_back(hero->id);
	}
	// strongest collectors first
	std::sort(others.begin(), others.end(), [this](ObjectInstanceID a, ObjectInstanceID b)
	{
		const auto * ha = ownHero(a);
		const auto * hb = ownHero(b);
		return (ha ? ha->getTotalStrength() : 0) > (hb ? hb->getTotalStrength() : 0);
	});
	planSupplyRun();
	int roaming = 0;
	for(auto id : others)
	{
		if(!timeLeft())
		{
			logAi->warn("FairAI: turn time budget exhausted on day %d", today);
			break;
		}
		if(supply && supply->courier == id)
		{
			playHero(id);
			continue;
		}
		// (secondArmy) a second leader is no collector, scout or town guard: it takes none of their places
		if(leads(ownHero(id)))
		{
			playHero(id);
			continue;
		}
		// (townHeroes) A hero that holds a town's troops is that town's hero. It takes no collector's
		// or scout's place, makes its sorties on the leash, and spends the night in the garrison: the
		// gate stays free for the main hero, which takes what it needs from it there.
		if(params.townHeroes && heavy(ownHero(id)))
		{
			// (in the garrison with another hero of ours in the gate - the main hero as a rule: it stays inside)
			if(const auto * inside = ownHero(id); inside->isGarrisoned() && inside->getVisitedTown() && inside->getVisitedTown()->getVisitingHero())
				continue;
			playHero(id);
			if(const auto * now = ownHero(id))
			{
				const auto * town = now->getVisitedTown();
				if(town && town->tempOwner == me && !now->isGarrisoned() && !town->getGarrisonHero() && now->canBeMergedWith(*town))
				{
					cb->swapGarrisonHero(town);
					gateway.waitTillFree();
				}
			}
			continue;
		}
		const int role = roaming++;
		if(role >= params.collectorsRoam && role < params.collectorsRoam + params.scouts)
			scoutsToday.insert(id); // weakest roamers scout: they explore beyond our territory
		else if(role >= params.collectorsRoam)
		{
			// secondary heroes guard towns from the garrison instead of roaming
			const auto * hero = ownHero(id);
			const auto * town = hero ? hero->getVisitedTown() : nullptr;
			const auto * leader = ownHero(mainHero);
			const bool courier = params.couriers && hero && leader
				&& transferGain(leader, hero) >= std::max(800.0, 0.1 * static_cast<double>(leader->getArmyStrength()));
			if(town && !courier && !hero->isGarrisoned() && !town->getGarrisonHero() && hero->canBeMergedWith(*town))
			{
				cb->swapGarrisonHero(town);
				gateway.waitTillFree();
			}
			// A second hero in the gate guards nothing (it would fight outside the walls) and locks
			// everybody else out of the town: it goes out and collects instead.
			const bool gateOnly = town && !hero->isGarrisoned() && town->getGarrisonHero() && town->getGarrisonHero() != hero;
			if(town && !courier && !gateOnly)
				continue;
		}
		playHero(id);
	}

	hireHeroes();
	manageTowns(true);
	buyTavernArmies();
	defendTowns();
	arrangeTowns();

	heroesLastTurn.clear();
	for(const auto * hero : perception.ownHeroes())
		heroesLastTurn.insert(hero->id);
	for(const auto * hero : perception.ownHeroes())
		ownFactorSeen[hero->id.getNum()] = commanderFactor(hero); // (calibration) as the enemy will find it tonight
	{
		// (calibration) how far away the reach model has every enemy hero we know of from each of our
		// heroes as the turn ends - against which the attacks of the coming night are measured
		threatDistanceSeen.clear();
		const auto knownNow = perception.knownObjects();
		for(const auto & t : threats)
		{
			auto object = knownNow.find(t.hero);
			if(object == knownNow.end() || object->second.type != Obj::HERO)
				continue;
			const int type = object->second.subtype.getNum();
			for(const auto * hero : perception.ownHeroes())
			{
				const double distance = enemyDistance(t.hero, t.pos, t.strength, hero->visitablePos(), 60.0);
				const int tenths = std::isfinite(distance) ? static_cast<int>(std::lround(distance * 10)) : 9999;
				auto & slot = threatDistanceSeen[hero->id.getNum()];
				auto had = slot.find(type);
				if(had == slot.end() || tenths < had->second.first)
					slot[type] = {tenths, t.age};
			}
		}
	}

	if(const char * dump = std::getenv("FAI_DUMP_MAP"))
	{
		// FAI_DUMP_MAP=1: days 7, 14 and 28; FAI_DUMP_MAP=40,80: those days
		const std::string days = std::string(",") + dump + ",";
		if(days.find("," + std::to_string(today) + ",") != std::string::npos || (days == ",1," && (today == 7 || today == 14 || today == 28)))
			dumpKnowledge();
	}

	const auto * main = ownHero(mainHero);
	int mines = 0;
	for(const auto * object : cb->getMyObjects())
		mines += object->ID == Obj::MINE ? 1 : 0;
	if(logAi->isDebugEnabled())
	{
		// what we know of the map's towns, and how we stand against the opposition we have seen
		std::string towns;
		for(const auto & [id, object] : perception.knownObjects())
		{
			if(object.type == Obj::TOWN && object.owner != me)
				towns += boost::str(boost::format(" %s%s%s") % (object.owner.isValidPlayer() ? object.owner.toString() : std::string("neutral"))
					% object.visitablePos.toString() % (object.visibleNow ? "" : boost::str(boost::format("(seen day %d)") % object.lastSeenDay)));
		}
		// what the strongest enemy hero is made of: the army as shown, the factor we give its hero, the days since
		std::string strongest;
		if(auto seen = enemySightings.find(oppositionHero); seen != enemySightings.end())
		{
			const HeroTypeID type(oppositionHero);
			strongest = boost::str(boost::format(" (%s, seen %d days ago: army %.0f x hero %.2f x %.2f for the days since)") % (type.toHeroType() ? type.toHeroType()->getNameTranslated() : std::string("?"))
				% (today - seen->second.day) % seen->second.army % seen->second.factor % (1.0 + 0.03 * (today - seen->second.day)));
		}
		logAi->debug("FairAI %s day %d: foreign towns known:%s; towns unfound %s; opposition heroes %.0f%s, guards %.0f; main hero strength %.0f (army %.0f, in the field %.0f), campaign %s",
			me.toString(), today, towns.empty() ? " none" : towns, huntWanted ? "yes" : "no", oppositionHeroes, strongest, oppositionGuards,
			main ? static_cast<double>(main->getTotalStrength()) : 0.0, main ? static_cast<double>(main->getArmyStrength()) : 0.0, main ? fieldStrength(main) : 0.0, campaignMode ? "on" : "off");
	}
	if(main && logAi->isDebugEnabled() && today % 7 == 0)
	{
		// once a week: what the main hero knows of magic (diagnostics)
		std::array<int, 6> perLevel{};
		std::string travel;
		for(const SpellID & spell : main->getSpellsInSpellbook())
		{
			const auto * known = spell.toSpell();
			if(!known)
				continue;
			perLevel[std::clamp(known->getLevel(), 0, 5)]++;
			if(spell == SpellID::TOWN_PORTAL || spell == SpellID::DIMENSION_DOOR || spell == SpellID::FLY || spell == SpellID::WATER_WALK
				|| spell == SpellID::VIEW_AIR || spell == SpellID::VIEW_EARTH || spell == SpellID::SUMMON_BOAT)
				travel += " " + known->getJsonKey();
		}
		logAi->debug("FairAI %s day %d: main hero %s level %d, spells by level %d/%d/%d/%d/%d, wisdom %d, mana %d of %d, travel spells:%s", me.toString(), today,
			main->getNameTranslated(), main->level, perLevel[1], perLevel[2], perLevel[3], perLevel[4], perLevel[5],
			static_cast<int>(main->getSecSkillLevel(SecondarySkill::WISDOM)), main->mana, main->manaLimit(), travel.empty() ? " none" : travel);
	}
	logAi->info("FairAI %s day %d end: gold %d, heroes %d, towns %d, main hero %s army %d, mines %d, wood %d, ore %d", me.toString(), today,
		cb->getResourceAmount(GameResID::GOLD), static_cast<int>(perception.ownHeroes().size()), static_cast<int>(perception.ownTowns().size()),
		main ? main->getNameTranslated() : "-", main ? static_cast<int>(main->getArmyStrength()) : 0, mines,
		cb->getResourceAmount(GameResID::WOOD), cb->getResourceAmount(GameResID::ORE));
	if(statsFile)
		closeStatsDay();
}

int AdventureAI::phaseOf(int day)
{
	// four-week phases: 0 = days 1 to 28, 1 = 29 to 56, ...; everything from day 197 on is phase 7
	return std::clamp((day - 1) / 28, 0, 7);
}

void AdventureAI::noteStep(const Target & best)
{
	// what the main hero is walking to, in a few kinds (statistics for the test bench only)
	const std::string & d = best.description;
	std::string kind = "object";
	if(d == "explore" || d == "approach")
		kind = "explore";
	else if(d == "monster")
		kind = "monster";
	else if(d.rfind("bank", 0) == 0)
		kind = "bank";
	else if(d == "retreat" || d == "flee" || d == "home" || d == "aside" || d == "way out" || d == "fortress" || d == "evacuate" || d == "relocate")
		kind = "safety";
	else if(best.object.hasValue())
	{
		const auto known = perception.knownObjects();
		auto it = known.find(best.object);
		if(it != known.end() && it->second.type == Obj::TOWN)
			kind = it->second.owner == me ? "ownTown" : "foreignTown";
		else if(it != known.end() && it->second.type == Obj::HERO)
			kind = it->second.owner == me ? "ownHero" : "enemyHero";
	}
	++mainStepsToday[kind];
}

void AdventureAI::closeStatsDay()
{
	// One more day of the game, booked by phase: what the main hero spent it on (the kind most of
	// its steps went to), whether exploring has stalled, how many heroes lead or hold a town's
	// troops, what lies idle. Written out after every turn - the test bench reads the last state.
	auto & phase = stats[phaseOf(today)];
	phase["days"] += 1;
	std::string most = "none";
	int steps = 0;
	for(const auto & [kind, count] : mainStepsToday)
	{
		if(count > steps)
		{
			steps = count;
			most = kind;
		}
	}
	phase["main." + most] += 1;
	mainStepsToday.clear();
	if(nearMissToday == 2)
		phase["edge10"] += 1;
	else if(nearMissToday == 1)
		phase["edge08"] += 1;
	nearMissToday = 0;
	phase["townDeficit"] += townDeficit;
	const CGHeroInstance * main = ownHero(mainHero);
	if(main && main->getVisitedTown() && main->getVisitedTown()->tempOwner == me)
		phase["mainEndsInOwnTown"] += 1;
	if(exploringStalled())
		phase["stalled"] += 1;
	if(mainSlackDays > 0)
		phase["slack"] += 1;
	int heavyHeroes = 0;
	for(const auto * hero : perception.ownHeroes())
		heavyHeroes += heavy(hero) ? 1 : 0;
	phase["leaders"] += static_cast<double>(secondLeaders.size());
	phase["heavy"] += heavyHeroes;
	phase["campaign"] += campaignMode ? 1 : 0;
	phase["gold"] += cb->getResourceAmount(GameResID::GOLD);
	{
		// how we stand against the strongest enemy hero seen lately, and what we know to strike at
		int enemyTowns = 0;
		for(const auto & [id, object] : perception.knownObjects())
			enemyTowns += object.type == Obj::TOWN && isEnemy(object.owner) ? 1 : 0;
		phase["enemyTowns"] += enemyTowns;
		if(oppositionHeroes <= 0)
			phase["noOpposition"] += 1;
		else if(main)
		{
			const double ratio = fieldStrength(main) / oppositionHeroes;
			if(ratio >= 2.0)
				phase["twice"] += 1;       // the main hero counts twice the strongest enemy hero seen in three weeks
			else if(ratio >= 1.3)
				phase["ahead"] += 1;       // ... or at least the edge it asks for
			else if(ratio < 0.8)
				phase["behind"] += 1;
		}
	}

	std::string text = "{";
	bool firstPhase = true;
	for(const auto & [index, figures] : stats)
	{
		text += std::string(firstPhase ? "" : ",") + "\"" + std::to_string(index) + "\":{";
		firstPhase = false;
		bool first = true;
		for(const auto & [name, value] : figures)
		{
			text += std::string(first ? "" : ",") + "\"" + name + "\":" + boost::str(boost::format("%.0f") % value);
			first = false;
		}
		text += "}";
	}
	// (calibration) the battles against enemy heroes as the model saw them
	text += std::string(firstPhase ? "" : ",") + "\"fights\":[";
	for(size_t i = 0; i < fightNotes.size(); ++i)
	{
		text += i ? ",[" : "[";
		for(size_t k = 0; k < fightNotes[i].size(); ++k)
			text += (k ? "," : "") + std::to_string(fightNotes[i][k]);
		text += "]";
	}
	text += "]";
	text += "}";
	const std::string path = std::string(statsFile) + "-" + me.toString() + ".json";
	const std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		if(!out)
			return;
		out << text;
	}
	std::rename(tmp.c_str(), path.c_str()); // the game may end at any moment: never a half-written file
}

void AdventureAI::playHero(ObjectInstanceID heroId)
{
	int failures = 0;
	int boatAttempts = 0;
	if(params.artifactCare)
		arrangeArtifacts(ownHero(heroId));
	struct EndOfDayLog
	{
		AdventureAI & ai;
		ObjectInstanceID id;
		~EndOfDayLog()
		{
			const auto * hero = ai.ownHero(id);
			if(hero)
				logAi->info("FairAI %s: %s%s ends at %s, strength %d, known threat %.0f (cautious %.0f), defence %.0f, mp %d%s", ai.me.toString(), hero->getNameTranslated(),
					ai.isMain(hero) ? " (main)" : "", hero->visitablePos().toString(), static_cast<int>(hero->getTotalStrength()),
					ai.threatAt(hero->visitablePos()), ai.threatAt(hero->visitablePos(), ObjectInstanceID(), true),
					ai.defenceAt(hero, hero->visitablePos()), hero->movementPointsRemaining(), hero->getVisitedTown() ? ", in town" : "");
		}
	} endOfDayLog{*this, heroId};
	for(int step = 0; step < MAX_STEPS_PER_HERO; ++step)
	{
		gateway.checkInterruption();
		const CGHeroInstance * hero = ownHero(heroId);
		if(!hero || hero->movementPointsRemaining() <= 0)
			return;
		if(hero->isGarrisoned())
		{
			const CGTownInstance * town = hero->getVisitedTown();
			if(!town)
				return;
			const CGHeroInstance * visitor = town->getVisitingHero();
			if(visitor && visitor->tempOwner != me)
				return; // the gate is taken
			// our own hero in the gate simply swaps places with us
			cb->swapGarrisonHero(town);
			gateway.waitTillFree();
			hero = ownHero(heroId);
			if(!hero || hero->isGarrisoned())
				return;
			// (secondArmy) A second leader that walks out of the garrison takes all of it along: its
			// weakest stack stays behind as a token guard - raiders take empty towns for free.
			if(params.secondArmy && params.keepGarrison && leads(hero) && !isMain(hero) && town->stacksCount() == 0 && hero->stacksCount() > 1)
			{
				SlotID weakest;
				double weakestValue = std::numeric_limits<double>::max();
				for(const auto & [slot, stack] : hero->Slots())
				{
					const double v = stackValue(stack.get());
					if(v < weakestValue)
					{
						weakestValue = v;
						weakest = slot;
					}
				}
				const CStackInstance * stack = weakest.validSlot() ? hero->getStackPtr(weakest) : nullptr;
				const SlotID target = stack ? town->getSlotFor(stack->getCreatureID()) : SlotID();
				if(stack && target.validSlot())
				{
					cb->mergeOrSwapStacks(hero, town, weakest, target);
					gateway.waitTillFree();
					hero = ownHero(heroId);
					if(!hero)
						return;
				}
			}
		}

		if(isMain(hero) && !hero->getVisitedTown())
		{
			arrangeBaits(hero);
			hero = ownHero(heroId);
			if(!hero)
				return;
		}

		perception.refresh();
		updateThreats();
		{
			// What a spell point is worth to this hero where it stands (for the battle AI): next to a
			// town of ours with a mage guild the next ones cost a visit, on the way to the enemy's
			// towns the big fight is still to come.
			double urgency = 1.0;
			for(const auto * town : perception.ownTowns())
			{
				if(town->hasBuilt(BuildingID::MAGES_GUILD_1) && town->visitablePos().z == hero->visitablePos().z
					&& town->visitablePos().dist2d(hero->visitablePos()) <= 14)
					urgency = 0.3;
			}
			if(campaign(hero))
				urgency *= 2.0;
			else if(threatAt(hero->visitablePos(), ObjectInstanceID(), true) > 0)
				urgency *= 1.5;
			Combat::setManaUrgency(hero->id.getNum(), urgency);
		}
		auto paths = computePaths(hero, false);
		auto freePaths = computePaths(hero, true);
		auto targets = supply && supply->courier == heroId ? supplyTargets(hero, *paths) : collectTargets(hero, *paths, *freePaths);
		// (muster) More waits in the town than the main hero leads: while that town is among its
		// targets, nothing else is worth more than half a day - a town next door that changed hands
		// every other day kept a leader of 130,000 from the 380,000 waiting four days away.
		// Retreat and flight as ever.
		if(musterUrgent && musterTown.hasValue() && heroId == mainHero)
		{
			const bool onTheList = std::any_of(targets.begin(), targets.end(), [this](const Target & t) { return t.object == musterTown; });
			if(onTheList)
			{
				vstd::erase_if(targets, [this](const Target & t)
				{
					const std::string & d = t.description;
					const bool safety = d == "retreat" || d == "flee" || d == "home" || d == "aside" || d == "way out" || d == "fortress" || d == "evacuate" || d == "relocate";
					return t.object != musterTown && t.travel > 0.5 && !safety;
				});
			}
		}
		if(supply && supply->courier == heroId && targets.empty())
			return;
		{
			// nothing worth doing on this landmass: the sea is the way on
			bool somethingToDo = false;
			for(const auto & t : targets)
				somethingToDo = somethingToDo || (t.description != "retreat" && t.description != "flee");
			if(!somethingToDo && boatAttempts < 2 && seekBoat(hero, *paths, targets, freePaths.get()))
			{
				++boatAttempts;
				continue; // a boat is waiting: plan again
			}
		}
		if(targets.empty() && !hero->getVisitedTown() && !(supply && supply->courier == heroId))
		{
			// In a pocket: a tile whose only way out leads over a place one visits (a dwelling, a
			// mine...). A path never runs across such a place, so from here nothing else is in reach
			// - the hero has to step onto it first. (A main hero stood nine days behind a dwelling.)
			const int3 size = cb->getMapSize();
			const int3 here = hero->visitablePos();
			int open = 0;
			int3 exit(-1);
			float exitCost = std::numeric_limits<float>::max();
			for(int x = std::max(0, here.x - 6); x < std::min(size.x, here.x + 7); ++x)
			{
				for(int y = std::max(0, here.y - 6); y < std::min(size.y, here.y + 7); ++y)
				{
					const int3 tile(x, y, here.z);
					const CGPathNode * node = tile == here ? nullptr : paths->getPathInfo(tile);
					if(!node || !node->reachable())
						continue;
					if(node->action == EPathNodeAction::NORMAL)
						++open;
					else if(node->action == EPathNodeAction::VISIT && node->turns == 0 && node->cost < exitCost
						&& !failedToday.count({heroId.getNum(), tile}) && !pocketExits.count({heroId.getNum(), tile}))
					{
						exit = tile;
						exitCost = node->cost;
					}
				}
			}
			if(open < 6 && exit.isValid())
			{
				pocketExits.insert({heroId.getNum(), exit}); // once a day: no stepping to and fro
				logAi->debug("FairAI %s: %s is in a pocket (%d tiles), the way out leads over %s", me.toString(), hero->getNameTranslated(), open, exit.toString());
				Target t;
				t.tile = exit;
				t.value = 1;
				t.travel = exitCost;
				t.score = 1;
				t.description = "way out";
				targets.push_back(t);
			}
		}
		if(targets.empty() && isMain(hero) && !hero->getVisitedTown() && !fledToday.count(heroId))
		{
			// (a hero that fled today stays where the flight ended: turning round for home with what
			// movement is left ends in the open, nearer to the threat - a main hero of 350,000 was
			// beaten that way two tiles from the gate it had fled from in the morning)
			// nothing to do: wait in the nearest own town, where garrison and walls help
			// (trappedHome) Threatened where it stands, with nothing safe in reach and nowhere to flee
			// to: standing still helps nothing - the nights on the way home are no worse than this one,
			// and at home wait the walls and the troops that have grown there. However far it is.
			const int3 here = hero->visitablePos();
			const bool trapped = params.trappedHome && threatAt(here) > tolerance(hero) * defenceAt(hero, here);
			const CGTownInstance * home = nullptr;
			double homeCost = 1e9;
			const CGTownInstance * strongest = nullptr;
			double strongestMargin = -1e18;
			double strongestCost = 1e9;
			for(const auto * town : perception.ownTowns())
			{
				const CGPathNode * node = paths->getPathInfo(town->visitablePos());
				if(!node || !node->reachable() || failedToday.count({heroId.getNum(), town->visitablePos()}))
					continue;
				const double margin = tolerance(hero) * defenceAt(hero, town->visitablePos()) - threatAt(town->visitablePos());
				if(margin >= 0 && node->cost < homeCost)
				{
					home = town;
					homeCost = node->cost;
				}
				if(margin > strongestMargin)
				{
					strongest = town;
					strongestMargin = margin;
					strongestCost = node->cost;
				}
			}
			if(trapped && !home && strongest)
			{
				// no town of ours holds against that army either: the one where we stand strongest
				home = strongest;
				homeCost = strongestCost;
			}
			if(trapped && home && homeCost >= 2.0 && trappedLog != today)
			{
				trappedLog = today;
				logAi->info("FairAI %s day %d: %s is threatened where it stands (%.0f against %.0f) and nothing in reach is safe - marching home to %s, %.1f days away",
					me.toString(), today, hero->getNameTranslated(), threatAt(here), defenceAt(hero, here), home->getNameTranslated(), homeCost);
			}
			if(home && (homeCost < 2.0 || trapped))
			{
				Target t;
				t.tile = home->visitablePos();
				t.object = home->id;
				t.value = 1;
				t.travel = homeCost;
				t.score = 1;
				t.description = "home";
				targets.push_back(t);
			}
		}
		if(targets.empty() && !isMain(hero) && !hero->getVisitedTown())
		{
			// An idle hero does not stay out in the open: a hero cannot walk through another, so one
			// standing in a pass locks our own heroes in (a whole game was lost with the main hero
			// shut in its corner by a scout parked in the only way out). It goes to the nearest own
			// town with a free gate; failing that it at least leaves the pass.
			const CGTownInstance * home = nullptr;
			double homeCost = 1e9;
			for(const auto * town : perception.ownTowns())
			{
				if(town->getVisitingHero())
					continue;
				const CGPathNode * node = paths->getPathInfo(town->visitablePos());
				const bool safe = threatAt(town->visitablePos(), ObjectInstanceID(), true) <= tolerance(hero) * defenceAt(hero, town->visitablePos());
				if(safe && node && node->reachable() && node->cost < homeCost && !failedToday.count({heroId.getNum(), town->visitablePos()}))
				{
					home = town;
					homeCost = node->cost;
				}
			}
			Target t;
			t.value = 1;
			t.score = 1;
			if(home)
			{
				t.tile = home->visitablePos();
				t.object = home->id;
				t.travel = homeCost;
				t.description = "home";
				targets.push_back(t);
			}
			else if(inPass(hero->visitablePos(), *paths))
			{
				const int3 size = cb->getMapSize();
				const int3 here = hero->visitablePos();
				float bestCost = std::numeric_limits<float>::max();
				for(int x = std::max(0, here.x - 12); x < std::min(size.x, here.x + 13); ++x)
				{
					for(int y = std::max(0, here.y - 12); y < std::min(size.y, here.y + 13); ++y)
					{
						const int3 tile(x, y, here.z);
						const CGPathNode * node = paths->getPathInfo(tile);
						if(!node || !node->reachable() || node->turns > 0 || node->action != EPathNodeAction::NORMAL
							|| node->accessible != EPathAccessibility::ACCESSIBLE || node->cost >= bestCost
							|| tile == here || failedToday.count({heroId.getNum(), tile}) || inPass(tile, *paths, &here))
							continue;
						bestCost = node->cost;
						t.tile = tile;
					}
				}
				if(bestCost < std::numeric_limits<float>::max())
				{
					t.travel = bestCost;
					t.description = "aside";
					targets.push_back(t);
				}
			}
		}
		if(targets.empty())
			return;

		std::sort(targets.begin(), targets.end(), [](const Target & a, const Target & b) { return a.score > b.score; });
		// (detour) What lies on the way is taken on the way. Judged by itself, a small target beside the
		// road loses against a big one days away at every step - and when the hero comes back from the
		// big one, the small one is days away and loses again. (In a logged game four packs of 20 to 49
		// pixies with mines and resource piles behind them stood untouched on day 26 beside the road a
		// main hero of 5,000 to 50,000 had walked three times between its town and its work.) Going
		// there first costs only the detour: it pays when it brings more per day of detour than the
		// big target brings per day of its way.
		const Target * pick = &targets.front();
		if(params.detour && targets.size() > 1 && targets.front().travel >= 0.5 && targets.front().score > 0)
		{
			const Target & far = targets.front();
			const int3 here = hero->visitablePos();
			const auto tiles = [](const int3 & a, const int3 & b) { return static_cast<double>(std::max(std::abs(a.x - b.x), std::abs(a.y - b.y))); };
			const double direct = tiles(here, far.tile);
			const auto known = perception.knownObjects();
			double bestRate = far.score;
			for(size_t i = 1; i < targets.size() && direct > 0 && here.z == far.tile.z; ++i)
			{
				const Target & near = targets[i];
				if(near.travel >= far.travel || near.value <= near.loss || near.tile.z != here.z || !near.object.hasValue())
					continue;
				// (things to pick up or beat only: a town, a hero, a way to flee is a plan of its own)
				const auto it = known.find(near.object);
				if(it == known.end() || it->second.type == Obj::TOWN || it->second.type == Obj::HERO)
					continue;
				if(near.description.rfind("monster", 0) != 0 && near.description.rfind("object", 0) != 0 && near.description.rfind("bank", 0) != 0)
					continue;
				// the way on from there, taken at the pace of the way to the big target
				const double onward = far.travel * tiles(near.tile, far.tile) / direct;
				const double extra = std::max(0.1, near.travel + onward - far.travel);
				const double rate = (near.value - near.loss) / (extra + params.travelOffset);
				if(rate > bestRate)
				{
					bestRate = rate;
					pick = &near;
				}
			}
			if(pick != &targets.front() && step == 0)
				logAi->debug("FairAI %s: %s takes %s %s on the way to %s %s (%.0f a day of detour against %.0f a day of the way)", me.toString(), hero->getNameTranslated(),
					pick->description, pick->tile.toString(), far.description, far.tile.toString(), bestRate, far.score);
		}
		const Target & best = *pick;
		if(targets.size() > 1 && step == 0)
			logAi->debug("FairAI %s: %s alternatives: %s %.0f, %s %.0f", me.toString(), hero->getNameTranslated(),
				targets[1].description, targets[1].score, targets.size() > 2 ? targets[2].description : "-", targets.size() > 2 ? targets[2].score : 0.0);
		if(best.score <= 0)
			return;

		logAi->debug("FairAI %s: %s -> %s %s value %.0f loss %.0f travel %.2f score %.0f", me.toString(), hero->getNameTranslated(),
			best.description, best.tile.toString(), best.value, best.loss, best.travel, best.score);
		if(statsFile && isMain(hero))
			noteStep(best);
		// (breakout 3) what the main hero found worth doing today; a way out it is forcing does not
		// count - or the day it sets out for one would end the standstill that sent it
		if(isMain(hero) && !(best.object.hasValue() && breakoutExits.count(best.object)))
		{
			// ... and neither does fetching troops from our own towns and heroes: an army that only
			// grows is not being used (a main hero went from 64,000 to 300,000 in four weeks of
			// walking between its town and the troops brought after it, next to the weakly guarded
			// monolith that was the way out)
			bool fetching = false;
			if(best.object.hasValue())
			{
				const auto & all = perception.knownObjects();
				auto it = all.find(best.object);
				fetching = it != all.end() && it->second.owner == me && (it->second.type == Obj::TOWN || it->second.type == Obj::HERO);
			}
			if(!fetching)
				mainBestValueToday = std::max(mainBestValueToday, best.value);
		}

		if(best.description == "flee")
			fledToday.insert(heroId);
		const bool safetyMove = best.description == "retreat" || best.description == "flee" || best.description == "home" || best.description == "aside" || best.description == "way out"
			|| best.description == "fortress";
		if(params.antiOscillation)
		{
			// giving up an unreached goal: do not come back to it today (A-B-A wastes the day's movement)
			auto previous = currentGoal.find(heroId);
			if(previous != currentGoal.end() && previous->second != best.tile && previous->second != hero->visitablePos())
				abandonedToday[heroId].insert(previous->second);
			// a short trip out of a town that needs its defender: the town calls the hero back only once the trip's goal is reached
			auto trip = roundTrip.find(heroId);
			if(trip != roundTrip.end() && trip->second.goal != best.tile)
				roundTrip.erase(trip);
			if(isMain(hero) && !safetyMove && params.townGuard)
			{
				if(const auto * home = hero->getVisitedTown(); home && home->visitablePos() == hero->visitablePos() && best.object != home->id
					&& townNeed(home, hero) > 0.2)
					roundTrip[heroId] = RoundTrip{home->id, best.tile};
			}
		}
		currentGoal[heroId] = best.tile;
		reservedBy.erase(heroId);
		if(best.object.hasValue())
			reservedBy[heroId] = best.object;

		if(params.townGuard && isMain(hero) && best.object.hasValue())
		{
			for(const auto * town : perception.ownTowns())
			{
				if(town->id == best.object)
				{
					clearGate(town, hero);
					break;
				}
			}
			hero = ownHero(heroId);
			if(!hero)
				return;
		}

		const int3 posBefore = hero->visitablePos();
		const int mpBefore = hero->movementPointsRemaining();
		const auto objectBefore = perception.knownObjects();
		const bool meetingOwnHero = best.object.hasValue() && ownHero(best.object) != nullptr;
		const int exchangesBefore = exchanges.load();
		const uint64_t ownArmyBefore = hero->getArmyStrength();
		// heading for a guarded building: at its door the game shows the guards and asks (confirmVisit)
		const bool towardsBank = best.description.rfind("bank ", 0) == 0 && best.object.hasValue();
		guardAnswer = -1;
		guardedTarget = towardsBank ? best.object.getNum() : -1;
		activeHero = heroId.getNum();
		bool sent = stepTowards(hero, best.tile, *paths);
		if(sent)
			gateway.waitTillFree();
		activeHero = -1;
		guardedTarget = -1;
		if(towardsBank && guardAnswer.load() == 1)
		{
			// how the fight went against what the simulator expected (to keep the forecast honest)
			const CGHeroInstance * after = ownHero(heroId);
			const double left = after ? static_cast<double>(after->getArmyStrength()) : 0.0;
			logAi->info("FairAI %s: fight at the guarded building %s: army %llu -> %.0f, forecast loss %.1f%% of the army%s", me.toString(), best.tile.toString(),
				static_cast<unsigned long long>(ownArmyBefore), left, guardForecast.load() / 10.0, after ? "" : " - the hero is lost");
		}

		hero = ownHero(heroId);
		if(hero)
			trail[heroId].insert(hero->visitablePos());
		if(!hero)
		{
			logAi->info("FairAI %s: lost a hero on day %d", me.toString(), today);
			lastHeroLossDay = today;
			currentGoal.erase(heroId);
			reservedBy.erase(heroId);
			chooseMainHero();
			return;
		}

		// Progress: the hero moved, or (below) the target changed, or troops changed hands. Movement
		// points spent without any of these are no progress: a hero outside a gate that our other hero
		// blocks would otherwise "meet" that hero again and again until the day is over.
		bool progress = sent && (hero->visitablePos() != posBefore || hero->getArmyStrength() != ownArmyBefore);
		if(sent && madeWay)
			progress = true; // (passOwn) a hero of ours stepped off the way: the next step is the hero's own
		if(sent && !progress && hero->movementPointsRemaining() != mpBefore && !best.object.hasValue())
			progress = true; // no object to check: trust the movement
		if(sent && hero->visitablePos().dist2d(posBefore) > 2.5)
		{
			// teleported: both ends of the channel are explored now
			if(statsFile && best.object.hasValue() && breakoutExits.count(best.object) && !usedTeleporters.count(best.object))
				stats[phaseOf(today)]["door"] += 1; // (breakout 3) a door taken as the way out
			if(params.breakout >= 4 && best.object.hasValue() && breakoutExits.count(best.object) && !usedTeleporters.count(best.object))
				breakoutPauseUntil = today + 3; // (breakout 4) through a door: look at what lies behind it
			if(best.object.hasValue())
				usedTeleporters.insert(best.object);
			for(const auto * obj : cb->getVisitableObjs(hero->visitablePos(), false))
				usedTeleporters.insert(obj->id);
			for(const auto * obj : cb->getVisitableObjs(posBefore, false))
				usedTeleporters.insert(obj->id);
		}
		if(!progress && sent && best.object.hasValue())
		{
			// visiting an adjacent object does not always move the hero: check the object instead
			perception.refresh();
			auto now = perception.knownObjects();
			auto before = objectBefore.find(best.object);
			auto after = now.find(best.object);
			if(after == now.end() || (before != objectBefore.end() && before->second.owner != after->second.owner))
				progress = true;
		}

		if(!progress)
		{
			if(logAi->isDebugEnabled())
			{
				CGPath path;
				std::string next = "no path";
				if(paths->getPath(path, best.tile) && path.nodes.size() >= 2)
				{
					const CGPathNode & n = path.nodes[path.nodes.size() - 2];
					next = boost::str(boost::format("next %s action %d turns %d cost %.2f") % n.coord.toString() % static_cast<int>(n.action) % static_cast<int>(n.turns) % n.cost);
				}
				logAi->debug("FairAI %s: %s made no progress towards %s %s (at %s, mp %d, %s, order %s)", me.toString(), hero->getNameTranslated(),
					best.description, best.tile.toString(), hero->visitablePos().toString(), hero->movementPointsRemaining(), next, sent ? "sent" : "not sent");
			}
			failedToday.insert({heroId.getNum(), best.tile});
			// Met our other hero and nothing changed hands: once is enough - for as long as neither
			// army changes. (Only when the two really met. Judged by the distance alone, every
			// approach ended one tile short, written off as fruitless, and troops were never handed
			// over unless the heroes started side by side.)
			if(meetingOwnHero && exchanges.load() != exchangesBefore)
			{
				if(const auto * other = ownHero(best.object))
					fruitlessMeetings[{std::min(heroId.getNum(), best.object.getNum()), std::max(heroId.getNum(), best.object.getNum())}]
						= {hero->getArmyStrength(), other->getArmyStrength()};
			}
			if(++failures >= MAX_FAILURES_PER_HERO)
				return;
			continue;
		}
		failures = 0;
		// arrived at a building with rules of its own: do what there is to do there (a building one
		// cannot enter is visited from the tile next to it: the hero stays where it was and the visit costs movement)
		const bool atSite = hero->visitablePos() == best.tile
			|| (sent && hero->visitablePos() == posBefore && hero->movementPointsRemaining() != mpBefore && posBefore.z == best.tile.z && posBefore.dist2d(best.tile) < 1.6);
		if(params.sites && best.object.hasValue() && !meetingOwnHero && atSite && !hero->getVisitedTown())
		{
			const bool asked = quests.count(best.object) > 0;
			updateQuests();
			// a seer that has nothing to say to us (its wish was granted by somebody else, or its time is over)
			if(!asked && !quests.count(best.object) && dynamic_cast<const CGSeerHut *>(perception.visibleObject(best.object)))
				sitesDone.insert(best.object);
			useSite(hero, best.object);
			hero = ownHero(heroId);
			if(!hero)
				return;
		}
		if(best.object.hasValue() && !meetingOwnHero && hero->visitablePos() == best.tile)
		{
			// Visited, and the object offers the same as before (an emptied lean-to, a hut that teaches
			// what the hero cannot learn, a school we cannot pay): nothing to come back for this week.
			perception.refresh();
			auto now = perception.knownObjects();
			auto after = now.find(best.object);
			auto before = objectBefore.find(best.object);
			if(after != now.end() && before != objectBefore.end() && after->second.owner == before->second.owner
				&& after->second.type != Obj::TOWN && after->second.type != Obj::HERO
				&& objectValue(hero, after->second, perception.visibleObject(best.object)) > 0)
			{
				fruitless[{heroId.getNum(), best.object}] = today;
				logAi->debug("FairAI %s: %s gained nothing at object %d %s", me.toString(), hero->getNameTranslated(),
					after->second.type.getNum(), best.tile.toString());
			}
		}
		if(isMain(hero) && (best.object.hasValue() || best.description == "explore") && best.description != "home")
			mainProductive = true;
		if(auto trip = roundTrip.find(heroId); trip != roundTrip.end())
		{
			bool reached = hero->visitablePos() == trip->second.goal;
			if(!reached && best.object.hasValue())
			{
				// visiting an adjacent object or winning a fight does not always move the hero onto the tile
				auto now = perception.knownObjects();
				auto after = now.find(best.object);
				auto before = objectBefore.find(best.object);
				reached = after == now.end() || (before != objectBefore.end() && before->second.owner != after->second.owner);
			}
			if(reached)
				roundTrip.erase(trip);
		}

		if(const auto * town = hero->getVisitedTown())
			manageTownVisit(hero, town);
	}
}

bool AdventureAI::seekBoat(const CGHeroInstance * hero, const CPathsInfo & paths, std::vector<Target> & targets, const CPathsInfo * freePaths)
{
	if(!params.boats || hero->inBoat())
		return false;
	// the hero that carries the game (and scouts) goes to sea; collectors stay with their towns
	if(!isMain(hero) && !scoutsToday.count(hero->id))
		return false;
	{
		// only while a good part of the map is still unknown
		const int3 size = cb->getMapSize();
		int hidden = 0;
		for(int z = 0; z < size.z; ++z)
			for(int x = 0; x < size.x; ++x)
				for(int y = 0; y < size.y; ++y)
					hidden += cb->isVisible(int3(x, y, z)) ? 0 : 1;
		if(hidden * 20 < size.x * size.y * size.z)
			return false;
	}
	seaWanted = true;
	auto affordable = [this](const IShipyard * yard)
	{
		ResourceSet cost;
		yard->getBoatCost(cost);
		if(cb->getResourceAmount().canAfford(cost))
			return true;
		// put the price aside from now on (towns stop spending it) and trade for missing wood
		earmark = cost;
		earmarkDay = today;
		return buyResourcesFor(nullptr, cost, true) && cb->getResourceAmount().canAfford(cost);
	};
	auto buy = [&](const IShipyard * yard, const std::string & where)
	{
		logAi->info("FairAI %s day %d: %s has run out of land, buying a boat at %s", me.toString(), today, hero->getNameTranslated(), where);
		cb->buildBoat(yard);
		lastBoatDay = today;
		earmarkDay = -100;
		return true;
	};
	if(today - lastBoatDay >= 2)
	{
		// a town's shipyard sells from the town screen, wherever our heroes are; the boat must be in this hero's reach
		for(const auto * town : perception.ownTowns())
		{
			if(!town->hasBuilt(BuildingID::SHIPYARD) || town->shipyardStatus() != IBoatGenerator::GOOD || !affordable(town))
				continue;
			const CGPathNode * node = paths.getPathInfo(town->visitablePos());
			if(hero->getVisitedTown() == town || (node && node->reachable() && node->turns <= params.maxTravelTurns))
				return buy(town, town->getNameTranslated());
		}
		// a shipyard on the map sells to the hero standing at it
		for(const auto * object : cb->getVisitableObjs(hero->visitablePos(), false))
		{
			const auto * yard = object->ID == Obj::SHIPYARD ? dynamic_cast<const IShipyard *>(object) : nullptr;
			if(yard && yard->shipyardStatus() == IBoatGenerator::GOOD && affordable(yard))
				return buy(yard, "the shipyard at " + hero->visitablePos().toString());
		}
	}
	// otherwise walk to the nearest shipyard we know
	const CGPathNode * bestNode = nullptr;
	ObjectInstanceID bestId;
	int3 bestTile;
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type != Obj::SHIPYARD || object.visitablePos == hero->visitablePos() || failedToday.count({hero->id.getNum(), object.visitablePos}))
			continue;
		const CGPathNode * node = paths.getPathInfo(object.visitablePos);
		if(!node || !node->reachable() || node->turns > params.maxTravelTurns)
			continue;
		if(!bestNode || node->cost < bestNode->cost)
		{
			bestNode = node;
			bestId = id;
			bestTile = object.visitablePos;
		}
	}
	if(bestNode)
	{
		Target t;
		t.tile = bestTile;
		t.object = bestId;
		t.value = 1500;
		t.travel = bestNode->cost;
		t.score = t.value / (t.travel + params.travelOffset);
		t.description = "shipyard";
		targets.push_back(t);
		if(const auto * yard = dynamic_cast<const IShipyard *>(perception.visibleObject(bestId)))
		{
			yard->getBoatCost(earmark);
			earmarkDay = today;
		}
	}
	else if(freePaths && params.boatGuards)
	{
		// (boatGuards) No shipyard we can walk to - but one behind a guard: that guard stands between us and the
		// sea, and beating it is the way on. (On an island of 123 tiles the main hero stood in its
		// town from day 6 to day 14 with nothing to do: 50 to 99 troglodytes watched the shipyard,
		// "opened" nothing and were "not worth it" until the fight cost less than its experience
		// gave. The enemy held four towns by day 28.) Judged by the simulated battle.
		const auto known = perception.knownObjects();
		Target fight;
		fight.travel = 1e9;
		for(const auto & [id, object] : known)
		{
			if(object.type != Obj::SHIPYARD)
				continue;
			const CGPathNode * free = freePaths->getPathInfo(object.visitablePos);
			if(!free || !free->reachable() || free->turns > params.maxTravelTurns)
				continue;
			// the guard nearest to us on that way
			const CGObjectInstance * first = nullptr;
			for(const CGPathNode * n = free; n; n = n->theNodeBefore)
			{
				if(!cb->isVisible(n->coord))
					continue;
				for(const CGObjectInstance * guard : cb->getGuardingCreatures(n->coord))
					first = guard;
			}
			auto it = first ? known.find(first->id) : known.end();
			if(it == known.end() || it->second.type != Obj::MONSTER)
				continue;
			const CGPathNode * node = paths.getPathInfo(it->second.visitablePos);
			if(!node || !node->reachable() || node->turns > params.maxTravelTurns || node->cost >= fight.travel
				|| failedToday.count({hero->id.getNum(), it->second.visitablePos}))
				continue;
			const double loss = monsterLoss(hero, it->second, true);
			if(loss < 0)
				continue;
			fight.tile = it->second.visitablePos;
			fight.object = it->first;
			fight.value = std::max(3000.0, 2.0 * loss);
			fight.loss = loss;
			fight.travel = node->cost;
			fight.score = (fight.value - fight.loss) / (fight.travel + params.travelOffset);
			fight.description = "monster";
		}
		if(fight.object.hasValue())
		{
			if(boatGuardLog != today)
				logAi->info("FairAI %s day %d: %s has run out of land - the guard at %s stands between it and the shipyard (forecast loss %.0f)", me.toString(), today,
					hero->getNameTranslated(), fight.tile.toString(), fight.loss);
			boatGuardLog = today;
			targets.push_back(fight);
		}
	}
	return false;
}

bool AdventureAI::makeWay(const CGHeroInstance * blocker, const CGHeroInstance * mover, const CGPath & way)
{
	const ObjectInstanceID blockerId = blocker->id;
	const int3 here = blocker->visitablePos();
	const int3 moverPos = mover->visitablePos();
	std::set<int3> onWay;
	for(const auto & node : way.nodes)
		onWay.insert(node.coord);
	// the nearest tile off that way which the blocker reaches today without passing the hero it makes
	// way for - open ground rather than another narrow place
	auto paths = computePaths(blocker, false);
	const int3 size = cb->getMapSize();
	int3 best(-1, -1, -1);
	float bestCost = std::numeric_limits<float>::max();
	bool bestOpen = false;
	for(int x = std::max(0, here.x - 9); x < std::min(size.x, here.x + 10); ++x)
	{
		for(int y = std::max(0, here.y - 9); y < std::min(size.y, here.y + 10); ++y)
		{
			const int3 tile(x, y, here.z);
			if(tile == here || onWay.count(tile))
				continue;
			const CGPathNode * node = paths->getPathInfo(tile);
			if(!node || !node->reachable() || node->turns > 0 || node->action != EPathNodeAction::NORMAL || node->accessible != EPathAccessibility::ACCESSIBLE)
				continue;
			const bool open = !inPass(tile, *paths, &here);
			if(!(open && !bestOpen) && !(open == bestOpen && node->cost < bestCost))
				continue;
			bool pastMover = false;
			bool plain = true;
			for(const CGPathNode * n = node->theNodeBefore; n && n->theNodeBefore; n = n->theNodeBefore)
			{
				pastMover = pastMover || n->coord == moverPos;
				plain = plain && n->action == EPathNodeAction::NORMAL;
			}
			if(pastMover || !plain)
				continue;
			best = tile;
			bestCost = node->cost;
			bestOpen = open;
		}
	}
	if(best.x < 0)
	{
		logAi->debug("FairAI %s: %s at %s stands in the way of %s and has nowhere to step aside today (mp %d)", me.toString(), blocker->getNameTranslated(),
			here.toString(), mover->getNameTranslated(), blocker->movementPointsRemaining());
		wedgedToday.insert(here);
		return false;
	}
	logAi->debug("FairAI %s: %s at %s makes way for %s: to %s", me.toString(), blocker->getNameTranslated(), here.toString(), mover->getNameTranslated(), best.toString());
	if(statsFile)
		stats[phaseOf(today)]["madeWay"] += 1;
	makingWay = true;
	const int activeBefore = activeHero.load();
	for(int i = 0; i < 24; ++i)
	{
		const CGHeroInstance * now = ownHero(blockerId);
		if(!now || now->visitablePos() == best)
			break;
		const int3 before = now->visitablePos();
		auto stepPaths = computePaths(now, false);
		activeHero = blockerId.getNum();
		const bool sent = stepTowards(now, best, *stepPaths);
		if(sent)
			gateway.waitTillFree();
		activeHero = activeBefore;
		perception.refresh();
		now = ownHero(blockerId);
		if(!sent || !now || now->visitablePos() == before)
			break;
	}
	makingWay = false;
	const CGHeroInstance * after = ownHero(blockerId);
	if(after && after->visitablePos() == here)
		wedgedToday.insert(here);
	return after && after->visitablePos() != here;
}

bool AdventureAI::stepTowards(const CGHeroInstance * hero, const int3 & tile, const CPathsInfo & paths)
{
	CGPath path;
	if(!paths.getPath(path, tile) || path.nodes.size() < 2)
		return false;

	// nodes[0] is the destination, nodes.back() the hero's own tile
	const CGPathNode & next = path.nodes[path.nodes.size() - 2];
	if(next.turns > 0)
		return false;

	// (passOwn) a hero of ours stands on the next tile and the way leads on past it: it makes way first
	madeWay = false;
	if(params.passOwn && !makingWay && path.nodes.size() > 2)
	{
		for(const auto * other : perception.ownHeroes())
		{
			if(other->id == hero->id || other->isGarrisoned() || other->getVisitedTown() || other->visitablePos() != next.coord)
				continue;
			madeWay = makeWay(other, hero, path);
			return madeWay;
		}
	}

	if(next.action == EPathNodeAction::TELEPORT_NORMAL || next.action == EPathNodeAction::TELEPORT_BLOCKING_VISIT
		|| next.action == EPathNodeAction::TELEPORT_BATTLE)
	{
		// standing on a teleporter: visiting it again sends us through
		cb->moveHero(hero, hero->pos, false);
		return true;
	}
	// the path may rely on Fly or Water Walk the hero has not cast yet today
	auto castMoveSpell = [&](BonusType needed)
	{
		if(hero->hasBonusOfType(needed))
			return;
		for(const auto & spell : LIBRARY->spellh->objects)
		{
			if(!spell || !spell->isAdventure() || !spell->getAdventureMechanics().givesBonus(hero, needed))
				continue;
			if(!hero->canCastThisSpell(spell.get()) || hero->mana < hero->getSpellCost(spell.get()))
				continue;
			logAi->info("FairAI %s: %s casts %s to go on", me.toString(), hero->getNameTranslated(), spell->getNameTranslated());
			cb->castSpell(hero, spell->id);
			gateway.waitTillFree();
			return;
		}
	};
	if(next.layer == EPathfindingLayer::AIR)
		castMoveSpell(BonusType::FLYING_MOVEMENT);
	else if(next.layer == EPathfindingLayer::WATER)
		castMoveSpell(BonusType::WATER_WALKING);

	const bool transit = next.layer == EPathfindingLayer::AIR;
	cb->moveHero(hero, hero->convertFromVisitablePos(next.coord), transit);
	return true;
}

// ======================================================================= targets

double AdventureAI::resourceValue(GameResID resource) const
{
	if(resource == GameResID::GOLD)
		return 1.0;
	return resource.toResource()->getPrice() * params.resourceValueFactor;
}

double AdventureAI::expValue(const CGHeroInstance * hero) const
{
	return leads(hero) ? params.expValueMain : params.expValueOther;
}

double AdventureAI::observedDanger(const ObservedObject & object) const
{
	if(!object.army)
		return 0;
	// ranges are wide ("pack" = 10-19): lean towards the upper end
	const double w = params.dangerUpperWeight;
	return (1 - w) * object.army->expectedStrength() + w * object.army->maxStrength();
}

double AdventureAI::guardDanger(const int3 & tile) const
{
	if(!cb->isVisible(tile))
		return 0;
	double danger = 0;
	auto known = perception.knownObjects();
	for(const CGObjectInstance * guard : cb->getGuardingCreatures(tile))
	{
		auto it = known.find(guard->id);
		if(it != known.end())
			danger += observedDanger(it->second);
		else if(const auto * armed = dynamic_cast<const CArmedInstance *>(guard))
		{
			ObservedArmy army = perception.observeArmy(armed);
			danger += (1 - params.dangerUpperWeight) * army.expectedStrength() + params.dangerUpperWeight * army.maxStrength();
		}
	}
	return danger;
}

double AdventureAI::primaryPointValue(const CGHeroInstance * hero, int which, bool leader) const
{
	// (primaryByArmy) A point of attack or defence is worth what it adds to the army the hero leads:
	// field strength goes with sqrt((1 + 0.05 attack)(1 + 0.05 defence)), so one point adds about
	// 1.7 per cent at ten points - 3,400 army value to an army of 200,000, for every battle to come.
	// The flat 700 gold was a fair price for a hero with 30,000; the enemy's main hero counts 1.9
	// times its army, ours 1.5 times.
	const double flat = leader ? params.primarySkillValueMain : params.primarySkillValueOther;
	if(!params.primaryByArmy || !leader || !hero || which > 1)
		return flat;
	const int now = hero->getPrimSkillLevel(which == 0 ? PrimarySkill::ATTACK : PrimarySkill::DEFENSE);
	const double step = std::sqrt((1.0 + 0.05 * (now + 1)) / (1.0 + 0.05 * std::max(0, now))) - 1.0;
	// the army it leads now and in the weeks to come (a leader's army doubles within a month)
	const double army = std::max(60000.0, 2.0 * static_cast<double>(hero->getArmyStrength()));
	return std::max(flat, step * army * params.goldPerArmyValue);
}

double AdventureAI::heroEdgeNow() const
{
	// (boldBehind) Time works for whoever holds more towns. Level with the enemy or ahead, the main
	// hero fights an enemy hero only when the battle is as good as won (heroEdge: every logged
	// battle from 1.3 on was won, at 1.0 it was a coin flip). Behind, waiting loses: in the games we
	// lost our main army was the bigger one until day 84 and never used it, and the enemy attacked
	// us with armies no bigger than ours and won nine times out of ten. So for every town the enemy
	// holds more than we do the edge we ask for drops by a tenth, down to boldEdge.
	if(!params.boldBehind || townDeficit <= 0)
		return params.heroEdge;
	return std::max(params.boldEdge, params.heroEdge - 0.1 * townDeficit);
}

double AdventureAI::expectedLoss(const CGHeroInstance * hero, double danger, bool desperate, bool versusHero) const
{
	if(danger <= 0)
		return 0;
	// Against heroes (heroModel): both sides count as army times what attack and defence add - the
	// measure that told winner from loser in the logged battles; `danger` is given in the same measure.
	const bool model = params.heroModel && versusHero;
	const double strength = model ? fieldStrength(hero) : static_cast<double>(hero->getTotalStrength());
	double ratio = leads(hero) ? (versusHero ? (model ? (isMain(hero) ? heroEdgeNow() : params.heroEdge) : params.safetyRatioHero) : params.safetyRatio) : params.safetyRatioSecondary;
	// a main hero that has been stuck for days accepts riskier fights against guards
	if(isMain(hero) && !versusHero)
		ratio = std::max(params.minSafetyRatio, ratio - params.patienceStep * idleDays);
	if(desperate)
		ratio = 1.0; // no town left: a coin flip is better than certain defeat
	if(strength < danger * ratio)
		return -1;
	// Lanchester square law: survivors = sqrt(S^2 - D^2)
	const double loss = strength - std::sqrt(std::max(0.0, strength * strength - danger * danger));
	if(model)
	{
		// in this measure the square law needs no correction (losses in 144 won battles: 0.45 / ratio^2); back to army value
		const double army = static_cast<double>(hero->getArmyStrength());
		return loss / std::max(1.0, strength) * army * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
	}
	return loss * params.lossFactor * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
}

double AdventureAI::fieldStrength(const CGHeroInstance * hero) const
{
	if(!hero)
		return 0;
	if(!params.heroModel)
		return static_cast<double>(hero->getTotalStrength());
	// spell power and knowledge are left out on both sides (see heroFactor); offence and armourer
	// count as they do for the enemy's hero
	const double melee = hero->valOfBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSubtypeID(BonusCustomSubtype::damageTypeMelee))) / 100.0;
	const double armour = std::clamp(hero->valOfBonuses(BonusType::GENERAL_DAMAGE_REDUCTION) / 100.0, 0.0, 0.7);
	return static_cast<double>(hero->getArmyStrength()) * hero->getFightingStrength() * std::sqrt((1.0 + std::max(0.0, melee)) / (1.0 - armour))
		* (params.magicWorth >= 2 ? ownMagicWorth(hero) : 1.0);
}

double AdventureAI::ownMagicWorth(const CGHeroInstance * hero) const
{
	// (magicWorth 2) The enemy hero's magic counts in its strength; ours counted for nothing - with
	// equal armies and equal spells the enemy was "stronger". The same measured values, for the
	// spells our hero has and the spell points it holds: spells on a whole army 1.05 (Slow, Haste,
	// Prayer from advanced mastery on), raising the dead 1.15, Blind 1.2, damage by what five rounds
	// of the best spell take off an army as big as ours (measured: 1.11 at power 5, 1.61 at 12).
	if(!hero || !hero->hasSpellbook() || hero->mana < 6)
		return 1.0;
	double worth = 1.0;
	double damage = 0;
	const int power = std::max(1, hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
	for(const SpellID & id : hero->getSpellsInSpellbook())
	{
		const CSpell * spell = id.toSpell();
		if(!spell || !spell->isCombat() || !hero->canCastThisSpell(spell))
			continue;
		const int cost = std::max(1, static_cast<int>(hero->getSpellCost(spell)));
		if(cost > hero->mana)
			continue;
		const int mastery = std::clamp(static_cast<int>(hero->getSpellSchoolLevel(spell)), 0, 3);
		if((id == SpellID::SLOW || id == SpellID::HASTE || id == SpellID::PRAYER) && mastery >= 2)
			worth = std::max(worth, 1.05);
		else if(id == SpellID::RESURRECTION || id == SpellID::ANIMATE_DEAD)
			worth = std::max(worth, 1.15);
		else if(id == SpellID::BLIND)
			worth = std::max(worth, 1.2);
		else if(spell->isDamage())
		{
			const auto & info = spell->getLevelInfo(mastery);
			double perCast = static_cast<double>(spell->calculateRawEffectValue(mastery, power, 1));
			for(const auto & [name, node] : info.battleEffects.Struct())
			{
				if(node["chainLength"].Integer() > 1)
					perCast *= 1.9;
			}
			if(info.range.empty() || info.range.size() > 1)
				perCast *= 1.5;
			damage = std::max(damage, perCast * std::min(5.0, std::floor(static_cast<double>(hero->mana) / cost)));
		}
	}
	if(damage > 0)
	{
		double health = 0;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(stack && stack->getCreature())
				health += static_cast<double>(stack->getCount()) * stack->getCreature()->getMaxHealth();
		}
		if(health > 0)
			worth = std::max(worth, 1.0 + 0.7 * std::min(1.0, damage / health));
	}
	return worth;
}

const Combat::HeroSpec * AdventureAI::ownSpells(const CGHeroInstance * hero, Combat::HeroSpec & spec) const
{
	if(!params.spellForecast || !hero || !hero->hasSpellbook() || hero->mana <= 0)
		return nullptr;
	spec.hero = hero;
	return &spec;
}

double AdventureAI::manaPointShare(const CGHeroInstance * hero) const
{
	// the battle AI asks the same price when it weighs a spell (FairBattleAI::trySpellByOutcome)
	return params.manaShare * Combat::manaUrgency(hero->id.getNum()) / std::max(30, hero->manaLimit());
}

uint64_t AdventureAI::fightSignature(const CGHeroInstance * hero) const
{
	uint64_t signature = 1469598103934665603ULL;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(!stack || !stack->getCreature())
			continue;
		signature = (signature ^ static_cast<uint64_t>(stack->getCreatureID().getNum() * 100003 + slot.getNum())) * 1099511628211ULL;
		signature = (signature ^ static_cast<uint64_t>(stack->getCount())) * 1099511628211ULL;
	}
	if(params.spellForecast && hero->hasSpellbook())
	{
		// a battle that cost spell points and no troops leaves another hero behind
		signature = (signature ^ static_cast<uint64_t>(hero->mana + 1)) * 1099511628211ULL;
		signature = (signature ^ static_cast<uint64_t>(hero->getSpellsInSpellbook().size())) * 1099511628211ULL;
	}
	return signature;
}

AdventureAI::FightForecast AdventureAI::forecastMonsterFight(const CGHeroInstance * hero, CreatureID creature, int count) const
{
	FightForecast result;
	const CCreature * type = creature.toCreature();
	if(!hero || !type || count <= 0 || hero->stacksCount() == 0)
		return result;

	// same question, same answer: the hero's army and the guards do not change between planning steps
	const uint64_t signature = fightSignature(hero);
	const auto key = std::make_tuple(hero->id.getNum(), signature, creature.getNum(), count);
	if(auto cached = forecastCache.find(key); cached != forecastCache.end())
		return cached->second;
	result = simulateMonsterFight(hero, creature, count);
	forecastCache[key] = result;
	return result;
}

AdventureAI::FightForecast AdventureAI::simulateMonsterFight(const CGHeroInstance * hero, CreatureID creature, int count) const
{
	FightForecast result;
	const CCreature * type = creature.toCreature();
	if(!hero || !type || count <= 0 || hero->stacksCount() == 0)
		return result;

	std::vector<Combat::StackSpec> stacks;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(stack && stack->getCreature() && stack->getCount() > 0)
			stacks.push_back({stack.get(), stack->getCreature(), stack->getCount(), BattleSide::ATTACKER});
	}
	// The game splits guards by how strong the visitor is (public rule; the last step depends on the
	// guards' position, which we do not use). One of the stacks may be upgraded: assume it is.
	const double ratio = static_cast<double>(hero->getArmyStrength()) / std::max<double>(1.0, static_cast<double>(count) * type->getAIValue());
	int split = ratio < 0.5 ? 7 : (ratio < 0.67 ? 6 : (ratio < 1.0 ? 5 : (ratio < 1.5 ? 4 : (ratio < 2.0 ? 3 : 2))));
	split = std::clamp(split, 1, std::min(7, count));
	const int each = count / split;
	const int larger = count - each * split; // this many stacks get one creature more
	const CCreature * upgraded = type;
	if(split > 1 && !type->upgrades.empty())
		upgraded = type->upgrades.begin()->toCreature();
	for(int i = 0; i < split; ++i)
	{
		const CCreature * stackType = i == split / 2 && upgraded ? upgraded : type;
		stacks.push_back({stackType, stackType, each + (i < larger ? 1 : 0), BattleSide::DEFENDER});
	}

	try
	{
		const JsonNode & layout = cb->getSettings().getValue(EGameSettings::COMBAT_LAYOUTS)["default"];
		Combat::HeroSpec spec;
		const Combat::Forecast forecast = Combat::forecastBattle(stacks, BattleSide::ATTACKER, layout, 30, Combat::Stance::CHARGE, ownSpells(hero, spec), nullptr, guardForecastOptions());
		const double pointShare = manaPointShare(hero);
		const int plan = forecast.bestPlan(pointShare * forecast.ownBefore);
		result.win = forecast.win(plan);
		result.lossShare = forecast.lossShare(plan);
		result.lossValue = result.lossShare * forecast.ownBefore;
		result.mana = forecast.manaSpent[plan];
		result.manaShare = pointShare * result.mana;
	}
	catch(const std::exception & e)
	{
		logAi->warn("FairAI %s: battle forecast failed (%s)", me.toString(), e.what());
	}
	return result;
}

double AdventureAI::forecastCalibration(const CGHeroInstance * hero) const
{
	// The simulator plays our side by one plan from the first move to the last; in the game the combat
	// AI searches every move, and our hero casts. In the first week of a game the forecast is about
	// right all the same (real loss 0.95 to 1.16 of it), from the third week on real fights cost 0.5
	// to 0.7 of it (three sets of 13 logged games, 689 fights). What it is for this player and this
	// hero right now shows as the game goes: real loss over forecast loss of the last fights (the
	// forecast for the number that really stood there), starting from forecastCalib. A hero's own
	// record counts on top of the record of all our heroes.
	std::lock_guard<std::mutex> lock(calibMutex);
	const double weight = std::max(0.01, params.forecastCalibWeight);
	double scale = (params.forecastCalib * weight + calibAll.real) / (weight + calibAll.forecast);
	if(hero)
	{
		if(auto own = calibHero.find(hero->id.getNum()); own != calibHero.end())
			scale = (scale * weight + own->second.real) / (weight + own->second.forecast);
	}
	return std::clamp(scale, 0.4, 1.3);
}

void AdventureAI::noteGuardFightStart(const CGHeroInstance * hero, CreatureID creature, int count, const CArmedInstance * guards)
{
	PendingFight pending;
	const bool diagnostics = logAi->isDebugEnabled();
	if((params.fightForecast >= 2 || diagnostics) && hero && creature.hasValue() && count > 0)
	{
		const FightForecast forecast = simulateMonsterFight(hero, creature, count);
		pending.hero = hero->id.getNum();
		pending.forecast = forecast.lossShare;
		pending.win = forecast.win;
		pending.set = true;
		pending.count = params.fightForecast >= 2;
		if(diagnostics && guards)
		{
			// what either rule made of this stack when only the range on the map was known (for the
			// battle the game has split the stack: what we saw of it before is in what we remember)
			ObservedObject seen;
			const auto known = perception.knownObjects();
			if(auto remembered = known.find(guards->id); remembered != known.end())
				seen = remembered->second;
			const double army = static_cast<double>(hero->getArmyStrength());
			const double price = army * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
			if(seen.army && seen.army->units.size() == 1 && seen.army->units.front().creature.hasValue() && price > 0)
			{
				const bool below = params.worstCaseRatio > 0 && army < params.worstCaseRatio * static_cast<double>(seen.army->maxStrength());
				const double old = below ? -1 : expectedLoss(hero, observedDanger(seen));
				pending.old = old < 0 ? -1 : old / price;
				const MonsterJudgement judged = judgeMonster(hero, seen.army->units.front(), below, false);
				pending.judged = judged.cost < 0 ? -1 : judged.share;
			}
		}
	}
	std::lock_guard<std::mutex> lock(calibMutex);
	pendingFight = pending;
}

void AdventureAI::noteGuardFightEnd(bool won, double lostShare)
{
	std::lock_guard<std::mutex> lock(calibMutex);
	if(!pendingFight.set)
		return;
	const PendingFight fight = pendingFight;
	pendingFight = PendingFight();
	const double real = won ? std::clamp(lostShare, 0.0, 1.0) : 1.0;
	// a fight the simulator gave up for lost has no forecast loss to scale: logged, not counted
	if(fight.win && fight.count)
	{
		const double keep = std::clamp(params.forecastCalibMemory, 0.0, 1.0);
		calibAll.forecast = calibAll.forecast * keep + fight.forecast;
		calibAll.real = calibAll.real * keep + real;
		++calibAll.fights;
		ForecastRecord & own = calibHero[fight.hero];
		own.forecast = own.forecast * keep + fight.forecast;
		own.real = own.real * keep + real;
		++own.fights;
	}
	const double weight = std::max(0.01, params.forecastCalibWeight);
	const double scale = std::clamp((params.forecastCalib * weight + calibAll.real) / (weight + calibAll.forecast), 0.4, 1.3);
	std::string before;
	if(fight.judged > -2)
		before = boost::str(boost::format(" (from the range on the map: forecast rule %.3f, old rule %.3f)") % fight.judged % fight.old);
	if(fight.count)
		logAi->info("FairAI %s forecast check: %s %.3f of the army, real %s%.3f%s; after %d fights the forecast is scaled by %.2f", me.toString(),
			fight.win ? "forecast" : "forecast LOST,", fight.forecast, won ? "" : "LOST, ", real, before, calibAll.fights, scale);
	else
		logAi->debug("FairAI %s forecast check: %s %.3f of the army, real %s%.3f%s", me.toString(),
			fight.win ? "forecast" : "forecast LOST,", fight.forecast, won ? "" : "LOST, ", real, before);
}

AdventureAI::MonsterJudgement AdventureAI::judgeMonster(const CGHeroInstance * hero, const ObservedStack & seen, bool belowWorstCase, bool useCache) const
{
	// (fightForecast 2) The map shows a range ("a pack": 20 to 49). In 554 logged fights the real number
	// lay evenly over it (mean position 0.45). The expected loss over an even spread is taken from two
	// simulated fights, at 21 % and at 79 % of the range (Gauss' rule with two points: exact for every
	// cost that bends no more than a cubic) - and the fight at 79 % has to be won.
	MonsterJudgement result;
	// (the battle callbacks ask from another thread: no cache there)
	const auto fight = [&](int count) { return useCache ? forecastMonsterFight(hero, seen.creature, count) : simulateMonsterFight(hero, seen.creature, count); };
	const int least = std::max(1, seen.minCount);
	const int most = std::max(least, seen.maxCount);
	const double middle = 0.5 * (least + most);
	const double reach = 0.5 * (most - least) / std::sqrt(3.0);
	result.lowCount = std::clamp(static_cast<int>(std::lround(middle - reach)), least, most);
	result.highCount = std::clamp(static_cast<int>(std::lround(middle + reach)), least, most);
	result.calibration = forecastCalibration(hero);
	result.high = fight(result.highCount);
	if(!result.high.win)
	{
		result.verdict = "lost in the upper part of the range";
		return result;
	}
	result.low = result.lowCount < result.highCount ? fight(result.lowCount) : result.high;
	if(!result.low.win)
	{
		result.verdict = "lost in the lower part of the range";
		return result;
	}
	if(belowWorstCase)
	{
		// The army is worth less than worstCaseRatio times the largest stack the range allows. Judged
		// by army values such fights were lost outright too often; the simulator may overrule that -
		// when the fight against the largest number is a clear win as simulated (not scaled down).
		const FightForecast top = most > result.highCount ? fight(most) : result.high;
		if(!top.win || top.lossShare > params.forecastSafeShare)
		{
			result.verdict = "may be as strong as we are";
			return result;
		}
	}
	const double scale = result.calibration;
	result.share = 0.5 * (std::min(1.0, scale * result.low.lossShare) + std::min(1.0, scale * result.high.lossShare));
	const double mana = 0.5 * (result.low.manaShare + result.high.manaShare); // the spell points the fight takes, priced like troops
	const double limit = isMain(hero) ? std::min(0.6, params.maxLossShare + params.maxLossShareIdle * idleDays) : (leads(hero) ? params.maxLossShare : params.maxLossShareSecondary);
	if(result.share > limit)
	{
		result.verdict = "costs too much";
		return result;
	}
	result.cost = (result.share + mana) * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
	result.verdict = "taken";
	return result;
}

Combat::ForecastOptions AdventureAI::guardForecastOptions() const
{
	Combat::ForecastOptions options;
	if(!params.forecastPlans)
		return options;
	options.ownStances = 4;
	// guards are played by the game's neutral battle AI - VCMI's BattleAI unless the game is set up otherwise
	if(settings["ai"]["combatNeutralAI"].String() == "BattleAI")
	{
		options.guardTurns = cb->getStartInfo()->difficulty < 4 ? 2 : 10;
		options.kiteTurns = params.kitePlan ? 6 : 0; // (as the combat AI: FairBattleAI::KITE_TURNS)
	}
	return options;
}

double AdventureAI::monsterLoss(const CGHeroInstance * hero, const ObservedObject & monster, bool simulate) const
{
	if(!monster.army)
		return 0;
	// Never against a stack that may be as strong as we are. The map shows a range ("20 to 49"), the
	// rules below judge by its middle - and the top of the range is what may stand there (in 5,204
	// logged fights a tenth of the stacks were a third stronger than assumed, the strongest 1.8
	// times). With an army worth less than 1.1 times the largest stack the range allows we lost 8 of
	// 367 fights outright and a quarter of the army on average; above that, 2 of 4,837.
	const bool belowWorstCase = params.worstCaseRatio > 0 && static_cast<double>(hero->getArmyStrength()) < params.worstCaseRatio * static_cast<double>(monster.army->maxStrength());
	// (fightForecast 2) the simulator decides, scaled by what our fights have really cost
	if(params.fightForecast >= 2 && monster.army->units.size() == 1 && monster.army->units.front().creature.hasValue())
		return judgeMonster(hero, monster.army->units.front(), belowWorstCase).cost;
	if(belowWorstCase)
		return -1;
	if((!params.fightForecast && !simulate) || monster.army->units.size() != 1 || !monster.army->units.front().creature.hasValue())
		return expectedLoss(hero, observedDanger(monster));

	// the map shows a range ("a pack": 10-19): judge the likely size, and survive the largest
	const ObservedStack & seen = monster.army->units.front();
	const double middle = 0.5 * (seen.minCount + seen.maxCount);
	const int likelyCount = std::max(1, static_cast<int>(std::lround((1 - params.dangerUpperWeight) * middle + params.dangerUpperWeight * seen.maxCount)));
	const FightForecast likely = forecastMonsterFight(hero, seen.creature, likelyCount);
	if(!likely.win)
		return -1;
	double share = likely.lossShare;
	double mana = likely.manaShare; // the spell points the fight takes, priced like troops
	if(seen.maxCount > likelyCount)
	{
		const FightForecast worst = forecastMonsterFight(hero, seen.creature, seen.maxCount);
		if(!worst.win)
			return -1;
		share = 0.7 * likely.lossShare + 0.3 * worst.lossShare;
		mana = 0.7 * likely.manaShare + 0.3 * worst.manaShare;
	}
	const double limit = isMain(hero) ? std::min(0.6, params.maxLossShare + params.maxLossShareIdle * idleDays) : (leads(hero) ? params.maxLossShare : params.maxLossShareSecondary);
	if(share > limit)
		return -1;
	return (share + mana) * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
}

double AdventureAI::guardLoss(const CGHeroInstance * hero, const int3 & tile) const
{
	if(!cb->isVisible(tile))
		return 0;
	double total = 0;
	auto known = perception.knownObjects();
	for(const CGObjectInstance * guard : cb->getGuardingCreatures(tile))
	{
		double loss = 0;
		auto it = known.find(guard->id);
		if(it != known.end())
			loss = monsterLoss(hero, it->second);
		else if(const auto * armed = dynamic_cast<const CArmedInstance *>(guard))
		{
			ObservedObject seen;
			seen.id = guard->id;
			seen.type = guard->ID;
			seen.army = perception.observeArmy(armed);
			loss = monsterLoss(hero, seen);
		}
		if(loss < 0)
			return -1;
		total += loss;
	}
	return total;
}

// ======================================================================= buildings with rules of their own

double AdventureAI::idleGold() const
{
	if(economy.rho < params.idleRho)
		return 0;
	return std::max(0.0, static_cast<double>(spendable()[GameResID::GOLD]) - std::max(params.idleReserve, 2.0 * economy.weeklyRecruitCost));
}

double AdventureAI::artifactGain(const CGHeroInstance * hero, ArtifactID art) const
{
	if(!hero || !art.hasValue())
		return 0;
	const CArtifact * type = art.toArtifact();
	const double worth = artifactValue(hero, type, true);
	if(worth <= 0)
		return 0;
	const auto & possible = type->getPossibleSlots();
	const auto slots = possible.find(ArtBearer::HERO);
	if(slots == possible.end())
		return 0;
	double replaced = std::numeric_limits<double>::max();
	for(const ArtifactPosition & slot : slots->second)
	{
		if(!type->canBePutAt(hero, slot, true))
			continue;
		const CArtifactInstance * worn = hero->getArt(slot);
		if(!worn)
			return worth; // a free place
		replaced = std::min(replaced, artifactValue(hero, worn->getType(), true));
	}
	// only a hero that sorts its artifacts (artifactCare) puts the better one on
	return params.artifactCare && replaced < worth ? worth - replaced : 0.0;
}

int AdventureAI::buyArtifacts(const IMarket * market, const CGHeroInstance * visitor)
{
	if(!params.sites || !market || !visitor || visitor->tempOwner != me || !isMain(visitor) || !market->allowsTrade(EMarketMode::RESOURCE_ARTIFACT))
		return 0;
	int bought = 0;
	for(int round = 0; round < 4; ++round)
	{
		gateway.checkInterruption();
		const CGHeroInstance * hero = ownHero(visitor->id);
		const double idle = idleGold();
		if(!hero || idle <= 0)
			break;
		// The more gold lies about, the less an artifact has to give for its price: gold nothing
		// waits for wins no battle, attack and defence on the main hero do.
		const double wanted = idle > 50000 ? 0.04 : (idle > 20000 ? 0.12 : 0.3);
		ArtifactID best;
		double bestRatio = wanted;
		int bestPrice = 0;
		double bestGain = 0;
		for(const auto & item : market->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT))
		{
			const ArtifactID art = item.as<ArtifactID>();
			if(!art.hasValue())
				continue;
			int give = 0;
			int get = 0;
			if(!market->getOffer(GameResID(GameResID::GOLD).getNum(), art.getNum(), give, get, EMarketMode::RESOURCE_ARTIFACT) || give <= 0 || give > idle)
				continue;
			const double gain = artifactGain(hero, art);
			if(gain / give > bestRatio)
			{
				bestRatio = gain / give;
				best = art;
				bestPrice = give;
				bestGain = gain;
			}
		}
		if(!best.hasValue())
			break;
		logAi->info("FairAI %s: %s buys %s for %d gold (worth %.0f on this hero; %.0f gold lay idle)", me.toString(), hero->getNameTranslated(),
			best.toArtifact()->getJsonKey(), bestPrice, bestGain, idle);
		const int64_t goldBefore = spendable()[GameResID::GOLD];
		cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_ARTIFACT, GameResID(GameResID::GOLD), best, 1, hero);
		if(spendable()[GameResID::GOLD] >= goldBefore)
			break; // refused: do not ask again
		++bought;
		arrangeArtifacts(ownHero(visitor->id));
	}
	return bought;
}

void AdventureAI::onMarket(const IMarket * market, const CGHeroInstance * visitor)
{
	if(!params.sites || !market || !visitor || visitor->tempOwner != me || !market->allowsTrade(EMarketMode::RESOURCE_ARTIFACT))
		return;
	buyArtifacts(market, visitor);
	// what is on the counter now: what we know of this market from here on
	std::vector<ArtifactID> offer;
	for(const auto & item : market->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT))
	{
		if(item.as<ArtifactID>().hasValue())
			offer.push_back(item.as<ArtifactID>());
	}
	std::unique_lock lock(intelMutex);
	marketOffers[market->getObjInstanceID()] = offer;
}

double AdventureAI::universitySkillValue(const CGHeroInstance * hero, SecondarySkill skill) const
{
	if(!hero->canLearnSkill(skill))
		return 0; // it knows the skill, cannot have it, or has no room
	if(params.skillChoice)
		return skillValue(hero, skill, 1);
	// only what we would pick first on a level-up is worth a slot
	const int index = skill.getNum();
	const double priority = index >= 0 && index < static_cast<int>(SKILL_PRIORITY.size()) ? SKILL_PRIORITY[index] : 3.0;
	return priority >= 6 ? 300.0 * priority : 0.0;
}

void AdventureAI::updateQuests()
{
	quests.clear();
	questArtifacts.clear();
	if(!params.sites)
		return;
	// The quest log: every seer and quest guard one of our heroes has spoken to, with its wish.
	for(const QuestInfo & entry : cb->getMyQuests())
	{
		const CGObjectInstance * object = entry.getObject(cb.get());
		const auto * asker = dynamic_cast<const IQuestObject *>(object);
		if(!object || !asker || !dynamic_cast<const CGSeerHut *>(object))
			continue;
		const CQuest & quest = asker->getQuest();
		if(quest.isCompleted || (quest.lastDay >= 0 && today - 1 > quest.lastDay))
		{
			sitesDone.insert(object->id);
			continue;
		}
		KnownQuest known;
		known.artifacts = quest.mission.artifacts;
		for(const ArtifactID & art : quest.mission.artifacts)
		{
			double worth = 1000;
			if(art.hasValue())
			{
				switch(art.toArtifact()->aClass)
				{
				case EArtifactClass::ART_TREASURE: worth = 1200; break;
				case EArtifactClass::ART_MINOR: worth = 2500; break;
				case EArtifactClass::ART_MAJOR: worth = 5000; break;
				case EArtifactClass::ART_RELIC: worth = 9000; break;
				default: break;
				}
			}
			known.cost += worth;
		}
		for(const auto & res : LIBRARY->resourceTypeHandler->getAllObjects())
			known.cost += quest.mission.resources[res] * resourceValue(res);
		for(const auto & wanted : quest.mission.creatures)
		{
			if(wanted.getCreature())
				known.cost += static_cast<double>(wanted.getCount()) * wanted.getCreature()->getAIValue() * params.goldPerArmyValue;
		}
		quests[object->id] = known;
		for(const ArtifactID & art : quest.mission.artifacts)
			questArtifacts[art] = object->id;
	}
}

double AdventureAI::siteValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const
{
	if(!instance)
		return 0; // judged by what we see of the building now
	const bool main = isMain(hero);
	const double goldFactor = 0.4 + 0.6 * economy.goldWeight;
	const double armyFactor = std::sqrt(economy.armyPremium);
	const int64_t reserve = economy.rho > 1.5 ? 0 : 2000; // while gold is the bottleneck the towns come first
	const int64_t gold = spendable()[GameResID::GOLD] - reserve;

	// Hill Fort and its relatives: upgrades for the army the main hero carries - as far as we can
	// pay them and the troops gained are worth the price
	if(const auto * fort = dynamic_cast<const HillFort *>(instance))
	{
		if(!main)
			return 0;
		const ICreatureUpgrader * upgrader = fort;
		ResourceSet have = spendable();
		have[GameResID::GOLD] -= reserve;
		double value = 0;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(!stack || !stack->getCreature())
				continue;
			UpgradeInfo info(stack->getCreatureID());
			upgrader->fillUpgradeInfo(info, *stack);
			if(!info.canUpgrade())
				continue;
			const ResourceSet cost = info.getUpgradeCosts() * stack->getCount();
			if(!have.canAfford(cost))
				continue;
			double price = 0;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				price += cost[res] * resourceValue(res) * (res == GameResID::GOLD ? goldFactor : 1.0);
			const double gain = static_cast<double>(info.getUpgrade().toCreature()->getAIValue() - stack->getCreature()->getAIValue()) * stack->getCount()
				* params.goldPerArmyValue * armyFactor;
			if(gain > price)
			{
				value += gain - price;
				have -= cost;
			}
		}
		return value;
	}

	// Seer huts and quest guards. The wish is in our quest log once a hero has asked; what the seer
	// gives shows only when the wish is granted - on random maps experience, gold or creatures
	// worth several thousand gold; the guard opens its passage.
	if(const auto * seer = dynamic_cast<const CGSeerHut *>(instance))
	{
		if(sitesDone.count(object.id))
			return 0;
		const auto known = quests.find(object.id);
		if(known == quests.end())
			return 300; // not asked yet: worth the question
		if(known->second.cost > 4000 || !seer->checkQuest(hero))
			return 0;
		return (dynamic_cast<const CGQuestGuard *>(instance) ? 3000.0 : 6000.0) - known->second.cost;
	}

	// Black market (any market on the map that sells artifacts): for gold that lies idle. What it
	// sells shows at the counter; once we have stood there we know.
	if(const auto * market = dynamic_cast<const CGMarket *>(instance); market && market->allowsTrade(EMarketMode::RESOURCE_ARTIFACT))
	{
		const double idle = idleGold();
		if(!main || idle < 15000)
			return 0;
		std::vector<ArtifactID> offer;
		{
			std::unique_lock lock(intelMutex);
			const auto seen = marketOffers.find(object.id);
			if(seen == marketOffers.end())
				return 1500;
			offer = seen->second;
		}
		double best = 0;
		for(const ArtifactID & art : offer)
		{
			int give = 0;
			int get = 0;
			if(market->getOffer(GameResID(GameResID::GOLD).getNum(), art.getNum(), give, get, EMarketMode::RESOURCE_ARTIFACT) && give > 0 && give <= idle)
				best = std::max(best, artifactGain(hero, art) - 0.05 * give);
		}
		return best > 300 ? best : 0.0;
	}

	// University: a skill for the main hero, for gold
	if(dynamic_cast<const CGUniversity *>(instance))
	{
		if(!main)
			return 0;
		const double price = cb->getSettings().getInteger(EGameSettings::MARKETS_UNIVERSITY_GOLD_COST);
		if(gold < price)
			return 0;
		if(object.shownSkills.empty())
			return hero->canLearnSkill() ? 600 : 0; // we do not know yet what it teaches: worth a look
		double best = 0;
		for(const SecondarySkill & skill : object.shownSkills)
			best = std::max(best, universitySkillValue(hero, skill));
		return std::max(0.0, best - price * goldFactor);
	}

	switch(object.type.toEnum())
	{
	case Obj::REFUGEE_CAMP:
	{
		// new guests every week, which ones shows only at the door
		auto used = siteWeek.find(object.id);
		if(gold < 3000 || (used != siteWeek.end() && used->second == (today - 1) / 7))
			return 0;
		return (main ? 900.0 : 400.0) * armyFactor;
	}
	case Obj::WAR_MACHINE_FACTORY:
	{
		// war machines for the main hero, once gold lies idle
		if(!main || economy.rho < 1.5 || gold < 6000)
			return 0;
		auto used = siteWeek.find(object.id);
		if(used != siteWeek.end() && used->second == (today - 1) / 7)
			return 0;
		int missing = 0;
		for(ArtifactPosition slot : {ArtifactPosition::MACH1, ArtifactPosition::MACH2, ArtifactPosition::MACH3})
			missing += hero->getArt(slot) ? 0 : 1;
		return 250.0 * missing;
	}
	case Obj::OBELISK:
		// a piece of the puzzle that shows where the grail lies
		return instance->wasVisited(me) ? 0 : 200;
	case Obj::KEYMASTER:
		// the password opens every border guard and gate of this colour
		return instance->wasVisited(me) ? 0 : 2000;
	case Obj::BORDERGUARD:
	{
		// with the password the guard steps aside for good: the way behind it is open
		const auto * guard = dynamic_cast<const CGKeys *>(instance);
		return guard && guard->wasMyColorVisited(me) ? 2500 : 0;
	}
	case Obj::HUT_OF_MAGI:
		// shows the land around every eye of the magi
		return sitesDone.count(object.id) ? 0 : 400;
	case Obj::GARRISON:
	case Obj::GARRISON2:
	{
		// somebody else's gate in a passage: beyond it lies land we cannot reach otherwise
		if(object.owner == me)
			return 0;
		double value = isEnemy(object.owner) ? 1500 : 800;
		if(object.army)
		{
			for(const auto & unit : object.army->units)
			{
				if(unit.creature.hasValue())
					value += unit.creature.toCreature()->getMaxHealth() * 0.5 * (unit.minCount + unit.maxCount) * expValue(hero);
			}
		}
		Rewardable::RewardRevealTiles around{};
		around.radius = 9;
		around.scoreSurface = around.scoreSubterra = around.scoreWater = around.scoreRock = 1;
		around.hide = false;
		return value + 0.3 * params.explorationTileValue * unexploredAround(object.visitablePos, around);
	}
	case Obj::ABANDONED_MINE:
	{
		// behind the troglodytes: a mine - which resource shows when it is ours (never wood)
		if(object.owner == me)
			return 0;
		double perDay = 1000 * goldFactor + 2 * resourceValue(GameResID::ORE);
		for(GameResID res : {GameResID::MERCURY, GameResID::SULFUR, GameResID::CRYSTAL, GameResID::GEMS})
			perDay += resourceValue(res);
		return perDay / 6.0 * params.incomeHorizonDays;
	}
	default:
		break;
	}

	// lighthouses and other buildings one flags for a standing bonus
	if(dynamic_cast<const FlaggableMapObject *>(instance))
		return object.owner == me ? 0 : (isEnemy(object.owner) ? 600 : 300);
	return 0;
}

double AdventureAI::siteLoss(const CGHeroInstance * hero, const ObservedObject & object) const
{
	if(object.owner == me || isAlly(object.owner))
		return 0;
	if(object.type == Obj::GARRISON || object.type == Obj::GARRISON2)
		return object.army && !object.army->units.empty() ? monsterLoss(hero, object) : 0.0;
	if(object.type == Obj::ABANDONED_MINE)
	{
		// the rule every player knows: 100 to 199 troglodytes live in an abandoned mine
		ObservedObject dwellers;
		dwellers.id = object.id;
		dwellers.type = Obj::MONSTER;
		ObservedArmy army;
		army.known = true;
		ObservedStack stack;
		stack.creature = CreatureID(CreatureID::TROGLODYTES);
		stack.minCount = 100;
		stack.maxCount = 199;
		army.units.push_back(stack);
		dwellers.army = army;
		return monsterLoss(hero, dwellers);
	}
	return 0;
}

std::vector<std::pair<CreatureID, int>> AdventureAI::dwellingGuards(const ObservedObject & object, const CGObjectInstance * instance) const
{
	// The rule of the game every player knows: a dwelling that breeds creatures of level 5 or higher
	// is guarded by three weeks' growth of each kind it breeds - renewed every week, whoever owns it;
	// the Elemental Conflux by twelve earth elementals, the Golem Factory by nine gold and six
	// diamond golems. The game asks at the door ("... is guarded by a pack of ...; do you wish to
	// fight?"), and FairAI answered yes whatever stood there: in 284 logged games 132 heroes walked
	// into such guards and died - a courier with 250 into six efreet, the next hero into the same
	// six a day later.
	std::vector<std::pair<CreatureID, int>> guards;
	if(object.type != Obj::CREATURE_GENERATOR1 && object.type != Obj::CREATURE_GENERATOR4)
		return guards;
	if(object.owner == me || isAlly(object.owner))
		return guards;
	// what it breeds: the first kind of every row - shown on the map; out of sight, as we saw it
	std::vector<const CCreature *> kinds;
	if(const auto * dwelling = dynamic_cast<const CGDwelling *>(instance))
	{
		for(const auto & level : dwelling->creatures)
			if(!level.second.empty())
				kinds.push_back(level.second.front().toCreature());
	}
	else if(auto seen = dwellingKinds.find(object.id); seen != dwellingKinds.end())
	{
		for(const CreatureID & kind : seen->second)
			kinds.push_back(kind.toCreature());
	}
	const CCreature * earth = nullptr;
	const CCreature * air = nullptr;
	const CCreature * gold = nullptr;
	const CCreature * diamond = nullptr;
	bool high = false;
	for(const CCreature * kind : kinds)
	{
		if(!kind)
			continue;
		const std::string & key = kind->getJsonKey();
		if(key == "core:earthElemental")
			earth = kind;
		else if(key == "core:airElemental")
			air = kind;
		else if(key == "core:goldGolem")
			gold = kind;
		else if(key == "core:diamondGolem")
			diamond = kind;
		high = high || kind->getLevel() >= 5;
	}
	if(object.type == Obj::CREATURE_GENERATOR4 && earth && air)
		guards.emplace_back(earth->getId(), 12);
	else if(object.type == Obj::CREATURE_GENERATOR4 && gold && diamond)
	{
		guards.emplace_back(gold->getId(), 9);
		guards.emplace_back(diamond->getId(), 6);
	}
	else if(high)
	{
		std::set<CreatureID> seen;
		for(const CCreature * kind : kinds)
		{
			if(kind && kind->getGrowth() > 0 && seen.insert(kind->getId()).second)
				guards.emplace_back(kind->getId(), 3 * kind->getGrowth());
		}
	}
	return guards;
}

double AdventureAI::dwellingLoss(const CGHeroInstance * hero, const std::vector<std::pair<CreatureID, int>> & guards) const
{
	if(guards.empty())
		return 0;
	double strength = 0;
	for(const auto & [creature, count] : guards)
		strength += static_cast<double>(creature.toCreature()->getAIValue()) * count;
	const double army = static_cast<double>(hero->getArmyStrength());
	// (an army worth a third of the guards has no chance: no need to play the battle through)
	if(army < 0.33 * strength)
		return -1;
	const FightForecast forecast = forecastGuards(hero, guards, "default", true);
	const double limit = isMain(hero) ? std::min(0.6, params.maxLossShare + params.maxLossShareIdle * idleDays) : (leads(hero) ? params.maxLossShare : params.maxLossShareSecondary);
	if(!forecast.win || forecast.lossShare > limit)
		return -1;
	return (forecast.lossShare + forecast.manaShare) * army * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
}

void AdventureAI::useSite(const CGHeroInstance * hero, ObjectInstanceID site)
{
	const CGObjectInstance * instance = perception.visibleObject(site);
	if(!instance || !hero || !params.sites)
		return;
	if(dynamic_cast<const HillFort *>(instance))
	{
		const uint64_t before = hero->getArmyStrength();
		upgradeArmy(hero);
		if(const CGHeroInstance * now = ownHero(hero->id); now && now->getArmyStrength() != before)
			logAi->info("FairAI %s: %s upgrades its army at a hill fort (army %llu -> %llu)", me.toString(), now->getNameTranslated(),
				static_cast<unsigned long long>(before), static_cast<unsigned long long>(now->getArmyStrength()));
	}
	if(instance->ID == Obj::REFUGEE_CAMP || instance->ID == Obj::WAR_MACHINE_FACTORY)
		siteWeek[site] = (today - 1) / 7;
	if(instance->ID == Obj::HUT_OF_MAGI)
		sitesDone.insert(site);
}


void AdventureAI::onUniversity(const IMarket * market, const CGHeroInstance * visitor)
{
	if(!params.sites || !market || !visitor || visitor->tempOwner != me || !isMain(visitor))
		return;
	// what it teaches is on the board now: the best skill, if it is worth the fee - and the next one
	const double price = cb->getSettings().getInteger(EGameSettings::MARKETS_UNIVERSITY_GOLD_COST);
	const double goldFactor = 0.4 + 0.6 * economy.goldWeight;
	for(int round = 0; round < 4; ++round)
	{
		const CGHeroInstance * hero = ownHero(visitor->id);
		if(!hero)
			return;
		const int64_t gold = spendable()[GameResID::GOLD] - (economy.rho > 1.5 ? 0 : 2000);
		if(gold < price)
			return;
		SecondarySkill best(SecondarySkill::NONE);
		double bestValue = price * goldFactor;
		for(const auto & item : market->availableItemsIds(EMarketMode::RESOURCE_SKILL))
		{
			const SecondarySkill skill = item.as<SecondarySkill>();
			const double value = universitySkillValue(hero, skill);
			if(value > bestValue)
			{
				bestValue = value;
				best = skill;
			}
		}
		if(best == SecondarySkill::NONE)
			return;
		logAi->info("FairAI %s: %s learns %s at a university", me.toString(), hero->getNameTranslated(), best.toEntity(LIBRARY)->getJsonKey());
		cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_SKILL, GameResID(GameResID::GOLD), best, 1, hero);
	}
}

// ======================================================================= buildings by their public table

bool AdventureAI::guardedTable(const ObservedObject & object) const
{
	return params.tables >= 2 && object.table && object.table->guarded;
}

double AdventureAI::artifactPickupValue(const CGHeroInstance * hero, ArtifactID art) const
{
	if(art == ArtifactID::SPELL_SCROLL)
		return hero->hasSpellbook() ? 800 : 300;
	if(!art.hasValue())
		return 1000;
	const bool main = isMain(hero);
	if(params.artifactCare)
	{
		// what it would do for the hero leading our army; the others fetch it for that hero
		const CGHeroInstance * leader = ownHero(mainHero);
		const double forLeader = artifactValue(leader ? leader : hero, art.toArtifact(), true);
		const double forMe = main ? forLeader : std::max(artifactValue(hero, art.toArtifact(), false), 0.8 * forLeader);
		return std::clamp(std::max(forMe, 0.1 * art.toArtifact()->getPrice()), 300.0, 20000.0);
	}
	double value = 1000;
	switch(art.toArtifact()->aClass)
	{
	case EArtifactClass::ART_TREASURE: value = 1200; break;
	case EArtifactClass::ART_MINOR: value = 2500; break;
	case EArtifactClass::ART_MAJOR: value = 5000; break;
	case EArtifactClass::ART_RELIC: value = 9000; break;
	default: break;
	}
	return main ? value : 0.8 * value;
}

// ======================================================================= armies as they fight

double AdventureAI::turnShare(int morale) const
{
	if(!turnTableReady)
	{
		// the game's dice: good morale gives a second action, bad morale takes the action away
		const auto & rules = cb->getSettings();
		const double dice = std::max<int64_t>(1, rules.getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE));
		const std::vector<int> good = rules.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE);
		const std::vector<int> bad = rules.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE);
		for(int m = -6; m <= 6; ++m)
		{
			double share = 1.0;
			if(m > 0 && !good.empty())
				share += good[std::min<size_t>(m, good.size()) - 1] / dice;
			if(m < 0 && !bad.empty())
				share -= bad[std::min<size_t>(-m, bad.size()) - 1] / dice;
			turnTable[m + 6] = share;
		}
		turnTableReady = true;
	}
	return turnTable[std::clamp(morale, -6, 6) + 6];
}

int AdventureAI::sharedMorale(const CGHeroInstance * hero) const
{
	if(!hero)
		return 0;
	// leadership, artifacts, places visited: everything but what the mix of the army adds or takes
	const int all = hero->valOfBonuses(Selector::type()(BonusType::MORALE));
	const int mix = hero->valOfBonuses(Selector::type()(BonusType::MORALE).And(Selector::sourceType()(BonusSource::ARMY)));
	return all - mix;
}

double AdventureAI::armyWorth(int shared, const std::vector<std::pair<CreatureID, double>> & kinds) const
{
	std::set<FactionID> factions;
	bool undead = false;
	for(const auto & [kind, value] : kinds)
	{
		const CCreature * type = kind.toCreature();
		if(!type || value <= 0)
			continue;
		factions.insert(type->getFactionID());
		undead = undead || type->hasBonusOfType(BonusType::UNDEAD);
	}
	if(factions.empty())
		return 0;
	const int mix = (factions.size() == 1 ? 1 : 2 - static_cast<int>(factions.size())) - (undead ? 1 : 0);
	double worth = 0;
	for(const auto & [kind, value] : kinds)
	{
		const CCreature * type = kind.toCreature();
		if(!type || value <= 0)
			continue;
		// the undead, constructs and machines know no morale
		const bool unmoved = type->hasBonusOfType(BonusType::UNDEAD) || type->hasBonusOfType(BonusType::NON_LIVING) || type->hasBonusOfType(BonusType::MECHANICAL)
			|| type->hasBonusOfType(BonusType::SIEGE_WEAPON) || type->hasBonusOfType(BonusType::NO_MORALE);
		worth += value * (unmoved ? 1.0 : turnShare(shared + mix + type->valOfBonuses(BonusType::MORALE)));
	}
	return worth;
}

double AdventureAI::fitShare(const CGHeroInstance * hero, CreatureID kind, double value) const
{
	if(!params.armyFit || !hero || value <= 0 || !kind.hasValue())
		return 1.0;
	std::map<CreatureID, double> army;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(stack && stack->getCreature())
			army[stack->getCreatureID()] += stackValue(stack.get());
	}
	if(army.empty())
		return 1.0;
	const int shared = sharedMorale(hero);
	const double before = armyWorth(shared, {army.begin(), army.end()});
	army[kind] += value;
	return (armyWorth(shared, {army.begin(), army.end()}) - before) / value;
}

bool AdventureAI::fitMove(int shared, const std::vector<FitStack> & have, const std::vector<FitStack> & offer, bool full, FitMove & move) const
{
	// worth of the army with one of its stacks left out and one of the offered ones added
	auto worth = [&](int without, const FitStack * with)
	{
		std::map<CreatureID, double> kinds;
		for(int i = 0; i < static_cast<int>(have.size()); ++i)
		{
			if(i != without)
				kinds[have[i].kind] += have[i].value;
		}
		if(with)
			kinds[with->kind] += with->value;
		return armyWorth(shared, {kinds.begin(), kinds.end()});
	};
	const double now = worth(-1, nullptr);
	move = FitMove{};
	for(int i = 0; i < static_cast<int>(offer.size()); ++i)
	{
		if(offer[i].value <= 0)
			continue;
		if(!full)
		{
			const double gain = worth(-1, &offer[i]) - now;
			if(gain > 0.02 * offer[i].value && gain > move.gain)
				move = FitMove{i, -1, gain};
			continue;
		}
		for(int j = 0; j < static_cast<int>(have.size()); ++j)
		{
			// clearly better than what leaves for it
			const double gain = worth(j, &offer[i]) - now;
			if(gain > 0.1 * have[j].value && gain > move.gain)
				move = FitMove{i, j, gain};
		}
	}
	return move.take >= 0;
}

double AdventureAI::armyGain(const CGHeroInstance * hero, const std::vector<CStackBasicDescriptor> & creatures) const
{
	// the kinds in the army (single creatures split off as bait count with their kind: the game merges them)
	std::map<CreatureID, double> army;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(stack && stack->getCreature())
			army[stack->getCreatureID()] += stackValue(stack.get());
	}
	// (armyFit) the army as it fights: newcomers of another faction, or undead among the living, cost morale
	const bool fit = params.armyFit;
	const int shared = fit ? sharedMorale(hero) : 0;
	auto fights = [&](const std::map<CreatureID, double> & kinds) { return armyWorth(shared, {kinds.begin(), kinds.end()}); };
	const double before = fit ? fights(army) : 0.0;
	double gain = 0;
	for(const auto & offered : creatures)
	{
		if(!offered.getCreature() || offered.getCount() <= 0)
			continue;
		const double worth = static_cast<double>(offered.getCreature()->getAIValue()) * offered.getCount();
		if(auto same = army.find(offered.getId()); same != army.end())
		{
			same->second += worth;
			gain += worth;
		}
		else if(army.size() < GameConstants::ARMY_SIZE)
		{
			if(fit)
			{
				// troops that would cost the army more than they add are sent away again
				auto with = army;
				with[offered.getId()] = worth;
				if(fights(with) <= fights(army))
					continue;
			}
			army[offered.getId()] = worth;
			gain += worth;
		}
		else if(fit)
		{
			// no room: they take the place of the kind whose leaving helps the army most
			double best = fights(army);
			std::map<CreatureID, double> chosen;
			for(const auto & [kind, value] : army)
			{
				auto with = army;
				with.erase(kind);
				with[offered.getId()] = worth;
				const double total = fights(with);
				if(total > best + 0.1 * value)
				{
					best = total;
					chosen = with;
				}
			}
			if(!chosen.empty())
				army = chosen;
		}
		else
		{
			// no room: they take the place of our weakest stack if they are clearly better
			auto weakest = std::min_element(army.begin(), army.end(), [](const auto & a, const auto & b) { return a.second < b.second; });
			if(worth > 1.1 * weakest->second)
			{
				gain += worth - weakest->second;
				army.erase(weakest);
				army[offered.getId()] = worth;
			}
		}
	}
	return fit ? std::max(0.0, fights(army) - before) : gain;
}

double AdventureAI::unexploredAround(const int3 & where, const Rewardable::RewardRevealTiles & reveal) const
{
	// Which terrain an unseen tile has we cannot know; the level is all we go by.
	const int3 size = cb->getMapSize();
	const bool everywhere = reveal.radius <= 0;
	const int stride = everywhere ? 3 : 1;
	double count = 0;
	for(int z = 0; z < size.z; ++z)
	{
		const bool wanted = z == 0 ? (reveal.scoreSurface > 0 || reveal.scoreWater > 0 || reveal.scoreRock > 0) : (reveal.scoreSubterra > 0 || reveal.scoreRock > 0);
		if(!wanted || (!everywhere && z != where.z))
			continue;
		const int x0 = everywhere ? 0 : std::max(0, where.x - reveal.radius);
		const int x1 = everywhere ? size.x - 1 : std::min(size.x - 1, where.x + reveal.radius);
		const int y0 = everywhere ? 0 : std::max(0, where.y - reveal.radius);
		const int y1 = everywhere ? size.y - 1 : std::min(size.y - 1, where.y + reveal.radius);
		for(int x = x0; x <= x1; x += stride)
		{
			for(int y = y0; y <= y1; y += stride)
			{
				if(!everywhere && (x - where.x) * (x - where.x) + (y - where.y) * (y - where.y) > reveal.radius * reveal.radius)
					continue;
				if(!cb->isVisible(int3(x, y, z)))
					count += stride * stride;
			}
		}
	}
	// a chart of the seas only: most of what we have not seen is land
	if(reveal.scoreSurface <= 0 && reveal.scoreSubterra <= 0 && reveal.scoreRock <= 0)
		count *= 0.3;
	return count;
}

double AdventureAI::battleEdgeValue(const CGHeroInstance * hero, bool luck, int points) const
{
	// Luck or morale for the next battle is worth what it adds to the army that fights it: nothing to
	// a hero that carries a handful of troops, nothing beyond +3, nothing to troops that know no
	// morale - and much to a large army. Luck: the chance of a double blow; morale: turns gained or
	// lost, by the game's own dice.
	if(!hero || points == 0)
		return 0;
	const double army = static_cast<double>(hero->getArmyStrength());
	double edge = 0; // the army's strength in that battle, as a share
	if(luck)
	{
		const auto & rules = cb->getSettings();
		const double dice = std::max<int64_t>(1, rules.getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE));
		const std::vector<int> good = rules.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
		const std::vector<int> bad = rules.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
		auto damage = [&](int l)
		{
			if(l > 0 && !good.empty())
				return 1.0 + good[std::min<size_t>(l, good.size()) - 1] / dice;
			if(l < 0 && !bad.empty())
				return 1.0 - 0.5 * bad[std::min<size_t>(-l, bad.size()) - 1] / dice;
			return 1.0;
		};
		const int now = hero->valOfBonuses(Selector::type()(BonusType::LUCK));
		edge = damage(now + points) - damage(now);
	}
	else
	{
		std::vector<std::pair<CreatureID, double>> kinds;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(stack && stack->getCreatureID().hasValue())
				kinds.emplace_back(stack->getCreatureID(), stackValue(stack.get()));
		}
		const int shared = sharedMorale(hero);
		edge = army > 0 ? (armyWorth(shared + points, kinds) - armyWorth(shared, kinds)) / army : 0.0;
	}
	// one battle, not the war: about the tenth of the army that a fight worth the name costs
	return edge * army * params.goldPerArmyValue * 0.1 * (isMain(hero) ? 1.0 : 0.3);
}

double AdventureAI::moveLossShare(const CGHeroInstance * hero, const RewardTable & table) const
{
	// The share of the movement a hero has left that a visit takes. A Swan Pond takes all of it for
	// two points of luck in the next battle: with a day's march left that is a day lost, with two
	// steps left it costs nothing.
	double total = 0;
	int weight = 0;
	for(const RewardSample & sample : table.samples)
	{
		double worst = 0;
		for(const auto & visit : sample.visits)
		{
			if(!visit.limiter.heroAllowed(hero))
				continue;
			if(visit.reward.movePercentage >= 0 && visit.reward.movePercentage < 100)
				worst = std::max(worst, 1.0 - visit.reward.movePercentage / 100.0);
			if(table.selectMode == Rewardable::SELECT_FIRST)
				break;
		}
		total += worst * sample.weight;
		weight += sample.weight;
	}
	return weight > 0 ? total / weight : 0.0;
}

double AdventureAI::rewardValue(const CGHeroInstance * hero, const Rewardable::Reward & reward, const int3 & where) const
{
	const bool main = isMain(hero);
	const double goldFactor = 0.4 + 0.6 * economy.goldWeight; // gold matters less when it piles up
	const double armyFactor = std::sqrt(economy.armyPremium); // troops matter more when they are scarce
	double value = 0;

	// resources, by what our towns lack; what the building asks for is counted at the plain rate
	for(const auto & res : LIBRARY->resourceTypeHandler->getAllObjects())
	{
		const double amount = reward.resources[res];
		if(amount == 0)
			continue;
		if(res == GameResID::GOLD)
		{
			value += amount * goldFactor;
			continue;
		}
		double need = 1.0;
		if(amount > 0)
		{
			if(auto wanted = resourceNeed.find(res.getNum()); wanted != resourceNeed.end())
				need += params.resourceNeedFactor * std::min(1.0, wanted->second / amount);
			if(params.resourcePlan)
				need = std::max(need, std::clamp(scarcity(res), 0.7, 3.0));
		}
		value += amount * resourceValue(res) * need;
	}

	// experience
	double experience = reward.heroExperience;
	if(reward.heroLevel > 0)
		experience += static_cast<double>(LIBRARY->heroh->reqExp(hero->level + reward.heroLevel)) - static_cast<double>(LIBRARY->heroh->reqExp(hero->level));
	value += experience * expValue(hero);

	// spell points and movement: what the hero would have afterwards against what it has now
	if(hero->hasSpellbook() && (reward.manaDiff != 0 || reward.manaPercentage >= 0))
		value += (reward.calculateManaPoints(hero) - hero->mana) * (main ? params.manaValueMain : 0.25 * params.manaValueMain);
	// (movement a visit takes is not a sum of gold: it is the rest of the day, and counted as time
	// on the way there - see moveLossShare)
	if(reward.movePoints != 0 || reward.movePercentage >= 0)
		value += std::max(0, reward.calculateMovePoints(hero) - hero->movementPointsRemaining()) * (main ? params.moveValueMain : 0.33 * params.moveValueMain);

	// skills
	for(size_t which = 0; which < reward.primary.size(); ++which)
		value += reward.primary[which] * primaryPointValue(hero, static_cast<int>(which), main);
	for(const auto & [skill, level] : reward.secondary)
	{
		const int has = hero->getSecSkillLevel(skill);
		if(has >= level || (has == 0 && !hero->canLearnSkill(skill)))
			continue;
		if(params.skillChoice)
			value += std::max(0.0, skillValue(hero, skill, level) - (has > 0 ? skillValue(hero, skill, has) : 0.0));
		else
			value += (main ? 1000.0 : 300.0) * (level - has);
	}

	// artifacts, scrolls, spells the hero can learn and does not know
	for(const ArtifactID & art : reward.grantedArtifacts)
		value += artifactPickupValue(hero, art);
	value += (hero->hasSpellbook() ? 800.0 : 300.0) * reward.grantedScrolls.size();
	static const std::array<double, 6> spellWorth = {0, 400, 700, 900, 1500, 2500};
	for(const SpellID & spell : reward.spells)
	{
		const auto * taught = spell.toSpell();
		if(!taught || !hero->hasSpellbook() || hero->spellbookContainsSpell(spell) || !hero->canLearnSpell(taught))
			continue;
		value += spellWorth[std::clamp(taught->getLevel(), 1, 5)] * (main ? 1.0 : 0.4);
	}

	// creatures: what the army really gains - and what the building takes or turns into something else
	if(!reward.creatures.empty())
	{
		const double gain = armyGain(hero, reward.creatures);
		value += gain * params.goldPerArmyValue * armyFactor * (main ? reinforcementBoost(gain) : params.ferryFactor);
	}
	for(const auto & taken : reward.takenCreatures)
	{
		if(taken.getCreature())
			value -= static_cast<double>(taken.getCount()) * taken.getCreature()->getAIValue() * params.goldPerArmyValue;
	}
	for(const auto & [from, to] : reward.creaturesChange)
	{
		if(!from.hasValue() || !to.hasValue())
			continue;
		int count = 0;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(stack && stack->getCreatureID() == from)
				count += stack->getCount();
		}
		value += count * (static_cast<double>(to.toCreature()->getAIValue()) - from.toCreature()->getAIValue()) * params.goldPerArmyValue * armyFactor;
	}
	value -= 1000.0 * reward.takenArtifacts.size() + 500.0 * reward.takenScrolls.size();

	// morale, luck, movement for the days to come and the like
	for(const auto & bonus : reward.heroBonuses)
	{
		if(!bonus)
			continue;
		switch(bonus->type)
		{
		case BonusType::MORALE:
		case BonusType::LUCK:
			value += battleEdgeValue(hero, bonus->type == BonusType::LUCK, bonus->val);
			break;
		case BonusType::MOVEMENT:
			value += bonus->val * (main ? 1.0 : 0.3);
			break;
		default:
			value += (main ? 100.0 : 30.0) * (bonus->val < 0 ? -1 : 1);
			break;
		}
	}

	// land revealed (a Cover of Darkness hides land from the others: nothing for the visitor)
	if(reward.revealTiles && !reward.revealTiles->hide)
		value += std::min(6000.0, 0.5 * params.explorationTileValue * unexploredAround(where, *reward.revealTiles)) * std::max(0.4, 1.0 - today / 84.0);
	return value;
}

double AdventureAI::tableValue(const CGHeroInstance * hero, const RewardTable & table, const std::vector<int> * only, const int3 & where) const
{
	double total = 0;
	int weight = 0;
	std::vector<double> options;
	auto add = [&](const RewardSample & sample)
	{
		// the rewards this hero qualifies for, as the game would offer them
		options.clear();
		for(const auto & visit : sample.visits)
		{
			if(!visit.limiter.heroAllowed(hero))
				continue;
			options.push_back(rewardValue(hero, visit.reward, where));
			if(table.selectMode == Rewardable::SELECT_FIRST)
				break;
		}
		double worth = 0;
		if(!options.empty())
		{
			switch(table.selectMode)
			{
			case Rewardable::SELECT_ALL:
				worth = std::accumulate(options.begin(), options.end(), 0.0);
				break;
			case Rewardable::SELECT_RANDOM:
				worth = std::accumulate(options.begin(), options.end(), 0.0) / options.size();
				break;
			case Rewardable::SELECT_PLAYER:
				worth = options.back(); // the choice we make when asked (chooseSelection)
				break;
			default:
				worth = options.front();
				break;
			}
		}
		total += worth * sample.weight;
		weight += sample.weight;
	};
	if(only)
	{
		for(int index : *only)
			add(table.samples[index]);
	}
	else
	{
		for(const auto & sample : table.samples)
			add(sample);
	}
	return weight > 0 ? total / weight : 0.0;
}

AdventureAI::FightForecast AdventureAI::forecastGuards(const CGHeroInstance * hero, const std::vector<std::pair<CreatureID, int>> & guards, const std::string & layoutName, bool useCache) const
{
	FightForecast result;
	if(!hero || guards.empty() || hero->stacksCount() == 0)
		return result;

	// same question, same answer: the hero's army does not change between planning steps
	const uint64_t signature = fightSignature(hero);
	uint64_t setup = 1469598103934665603ULL;
	for(const auto & [creature, count] : guards)
		setup = ((setup ^ static_cast<uint64_t>(creature.getNum())) * 1099511628211ULL ^ static_cast<uint64_t>(count)) * 1099511628211ULL;
	for(char c : layoutName)
		setup = (setup ^ static_cast<uint64_t>(static_cast<unsigned char>(c))) * 1099511628211ULL;
	const auto key = std::make_tuple(hero->id.getNum(), signature ^ (setup * 0x9E3779B97F4A7C15ULL), -1, static_cast<int>(setup & 0x7fffffff));
	if(useCache)
	{
		if(auto cached = forecastCache.find(key); cached != forecastCache.end())
			return cached->second;
	}

	std::vector<Combat::StackSpec> stacks;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(stack && stack->getCreature() && stack->getCount() > 0)
			stacks.push_back({stack.get(), stack->getCreature(), stack->getCount(), BattleSide::ATTACKER});
	}
	for(const auto & [creature, count] : guards)
	{
		const CCreature * type = creature.toCreature();
		if(type && count > 0)
			stacks.push_back({type, type, count, BattleSide::DEFENDER});
	}
	try
	{
		const JsonNode & layouts = cb->getSettings().getValue(EGameSettings::COMBAT_LAYOUTS);
		const JsonNode & layout = layouts[layoutName].isNull() ? layouts["default"] : layouts[layoutName];
		Combat::HeroSpec spec;
		const Combat::Forecast forecast = Combat::forecastBattle(stacks, BattleSide::ATTACKER, layout, 30, Combat::Stance::CHARGE, ownSpells(hero, spec), nullptr, guardForecastOptions());
		const double pointShare = manaPointShare(hero);
		const int plan = forecast.bestPlan(pointShare * forecast.ownBefore);
		result.win = forecast.win(plan);
		result.lossShare = forecast.lossShare(plan);
		result.lossValue = result.lossShare * forecast.ownBefore;
		result.mana = forecast.manaSpent[plan];
		result.manaShare = pointShare * result.mana;
	}
	catch(const std::exception & e)
	{
		logAi->warn("FairAI %s: battle forecast failed (%s)", me.toString(), e.what());
	}
	if(useCache)
		forecastCache[key] = result;
	return result;
}

AdventureAI::GuardedPlan AdventureAI::guardedPlan(const CGHeroInstance * hero, const ObservedObject & object, bool useCache) const
{
	GuardedPlan plan;
	plan.known = object.guardsShown;
	if(!hero || !object.table)
		return plan;
	const RewardTable & table = *object.table;
	const double army = static_cast<double>(hero->getArmyStrength());
	const double limit = isMain(hero) ? std::min(0.6, params.maxLossShare + params.maxLossShareIdle * idleDays) : (leads(hero) ? params.maxLossShare : params.maxLossShareSecondary);

	// the states the building can be in, as far as we know
	struct Possible
	{
		double chance;
		std::vector<std::pair<CreatureID, int>> guards;
		const std::vector<int> * samples;
		double strength;
	};
	std::vector<Possible> possible;
	if(object.guardsShown)
	{
		if(object.shownGuards.empty())
			return plan; // nobody guards it any more: somebody has been here before us
		// the game lists the guards kind by kind: which state of the table is that?
		std::map<CreatureID, int> listed;
		for(const auto & [creature, count] : object.shownGuards)
			listed[creature] += count;
		const GuardState * match = nullptr;
		for(const auto & state : table.states)
		{
			std::map<CreatureID, int> counted;
			for(const auto & [creature, count] : state.guards)
				counted[creature] += count;
			if(counted == listed)
			{
				match = &state;
				break;
			}
		}
		if(match)
			possible.push_back({1.0, match->guards, &match->samples, static_cast<double>(match->strength)});
		else
		{
			// guards the table does not know - what a lost battle left of them, say: we fight what
			// is listed, for what a building of this kind holds on average
			double strength = 0;
			for(const auto & [creature, count] : object.shownGuards)
				strength += static_cast<double>(creature.toCreature()->getAIValue()) * count;
			possible.push_back({1.0, object.shownGuards, nullptr, strength});
		}
	}
	else
	{
		for(const auto & state : table.states)
			possible.push_back({state.chance, state.guards, &state.samples, static_cast<double>(state.strength)});
	}

	// Does the game show us the guards before the fight begins? Then the hero may still turn back
	// at the door, and it is enough that some of the states are worth the fight.
	const bool blind = !object.guardsShown && (table.forceCombat || !cb->getSettings().getBoolean(EGameSettings::BANKS_SHOW_GUARDS_COMPOSITION));

	for(const auto & state : possible)
	{
		double worth = tableValue(hero, table, state.samples, object.visitablePos);
		if(state.guards.empty())
		{
			if(worth > 0)
			{
				plan.chance += state.chance;
				plan.value += state.chance * worth;
			}
			continue;
		}
		bool take = false;
		double cost = 0;
		double share = 0;
		// (an army worth a third of the guards has no chance: no need to play the battle through)
		if(army >= 0.33 * state.strength)
		{
			const FightForecast forecast = forecastGuards(hero, state.guards, table.layout, useCache);
			if(forecast.win && forecast.lossShare <= limit)
			{
				double health = 0;
				for(const auto & [creature, count] : state.guards)
					health += static_cast<double>(creature.toCreature()->getMaxHealth()) * count;
				worth += health * expValue(hero);
				share = forecast.lossShare;
				cost = (share + forecast.manaShare) * army * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
				take = worth > cost;
			}
		}
		if(take)
		{
			plan.chance += state.chance;
			plan.value += state.chance * worth;
			plan.loss += state.chance * cost;
			plan.lossShare += state.chance * share;
		}
		else if(blind)
		{
			// a fight we cannot turn away from must be worth it whatever waits inside
			GuardedPlan none;
			none.known = plan.known;
			return none;
		}
	}
	if(plan.chance > 0)
		plan.lossShare /= plan.chance;
	return plan;
}

bool AdventureAI::confirmVisit(const std::vector<Component> & components)
{
	// Yes/no questions are answered "yes": FairAI only walks into what it wants to visit. The one
	// exception is the door of a guarded building, where the game shows who guards it - what the
	// hero came for was a guess at that.
	const int heroNum = activeHero.load();
	const int objectNum = guardedTarget.load();
	if(params.tables < 2 || heroNum < 0 || objectNum < 0)
		return true;
	std::vector<std::pair<CreatureID, int>> shown;
	for(const Component & c : components)
	{
		if(c.type == ComponentType::CREATURE && c.value && *c.value > 0)
			shown.emplace_back(c.subType.as<CreatureID>(), *c.value);
	}
	if(shown.empty())
		return true; // the game does not tell: the decision was made before the hero came

	const CGHeroInstance * hero = ownHero(ObjectInstanceID(heroNum));
	const auto known = perception.knownObjects();
	const auto it = known.find(ObjectInstanceID(objectNum));
	if(!hero || it == known.end() || !it->second.table || !it->second.table->guarded)
		return true;
	// is this the question of that building? Its guards are creatures its table knows
	std::set<CreatureID> kinds;
	for(const auto & state : it->second.table->states)
		for(const auto & [creature, count] : state.guards)
			kinds.insert(creature);
	for(const auto & [creature, count] : shown)
	{
		if(!kinds.count(creature))
			return true;
	}

	ObservedObject seen = it->second;
	seen.guardsShown = true;
	seen.shownGuards = shown;
	const GuardedPlan plan = guardedPlan(hero, seen, false);
	const bool fight = plan.chance > 0;
	guardAnswer = fight ? 1 : 0;
	guardForecast = static_cast<int>(std::lround(1000 * plan.lossShare));
	std::string list;
	for(const auto & [creature, count] : shown)
		list += boost::str(boost::format(" %d %s") % count % creature.toCreature()->getJsonKey());
	logAi->info("FairAI %s: %s at the door of building %d/%d %s, guards:%s -> %s (worth %.0f, forecast loss %.0f = %.0f%% of the army)", me.toString(),
		hero->getNameTranslated(), it->second.type.getNum(), it->second.subtype.getNum(), it->second.visitablePos.toString(), list,
		fight ? "fights" : "turns back", plan.value, plan.loss, 100 * plan.lossShare);
	return fight;
}

void AdventureAI::onGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down)
{
	// Creatures are handed to a hero whose army has no free slot - by a building, or by monsters
	// that join: the game opens the exchange window with them waiting on the other side. Whatever
	// stays there is lost. (FairAI used to close the window at once: joiners worth a third of the
	// army were sent away while a handful of gremlins kept their slot.)
	if(!up || !down || down->tempOwner != me || up->tempOwner.isValidPlayer())
		return; // towns and garrisons of ours are handled where the hero visits them
	if(up->ID == Obj::HERO || up->ID == Obj::TOWN || up->ID == Obj::GARRISON || up->ID == Obj::GARRISON2)
		return;
	const uint64_t before = down->getArmyStrength();
	// (with armyFit only what the army gains by: pickBestCreatures judges each stack)
	pickBestCreatures(down, up, true);
	if(down->getArmyStrength() != before)
		logAi->info("FairAI %s: %s takes creatures offered at object %d/%d (army %llu -> %llu)", me.toString(), down->getNameTranslated(), up->ID.getNum(),
			up->subID.getNum(), static_cast<unsigned long long>(before), static_cast<unsigned long long>(down->getArmyStrength()));
}

void AdventureAI::updateThreats()
{
	rememberStaticObjects();
	threats.clear();
	const auto known = perception.knownObjects();
	if((params.heroModel || params.leaderFocus) && guildDay != today)
	{
		// the thieves' guild in our tavern: the other players' best heroes, with their skills from two taverns on
		guildDay = today;
		const GuildInfo guild = perception.thievesGuild();
		for(const auto & [color, shown] : guild.bestHero)
		{
			if(shown.skills && isEnemy(color))
				noteHeroSkills(shown.hero, *shown.skills);
		}
		guildTownPlace = guild.townPlace;
		guildBestHero.clear();
		for(const auto & [color, shown] : guild.bestHero)
			guildBestHero[color] = shown.hero.getNum();
	}
	{
		// Battles against enemy heroes since the last look: an army we destroyed is no opposition any
		// more. (Its last sighting used to count for three weeks, growing by 3 % a day - after the
		// decisive battle the enemy looked stronger than before it, and the campaign never began.)
		std::vector<BattleOutcome> outcomes;
		{
			std::unique_lock lock(intelMutex);
			outcomes.swap(pendingOutcomes);
		}
		for(const auto & outcome : outcomes)
		{
			auto seen = enemySightings.find(outcome.hero);
			if(outcome.ownHero >= 0 && outcome.ownStrength > 0 && outcome.enemyStrength > 0)
			{
				// (calibration) the battle as the hero model saw it before it was fought - our hero's
				// factor as last looked at, theirs as we gave it at the last sighting - and how it ended
				auto mine = ownFactorSeen.find(outcome.ownHero);
				const double ownFactor = mine != ownFactorSeen.end() ? mine->second : -1;
				const double theirFactor = seen != enemySightings.end() && seen->second.factor > 0 ? seen->second.factor : -1;
				const double theirLost = 1.0 - std::clamp(outcome.remainingShare, 0.0, 1.0);
				int64_t modelDistance = -1;
				int64_t sightingAge = -1;
				if(!outcome.weAttack)
				{
					if(auto mineSeen = threatDistanceSeen.find(outcome.ownHero); mineSeen != threatDistanceSeen.end())
						if(auto it = mineSeen->second.find(outcome.hero); it != mineSeen->second.end())
						{
							modelDistance = it->second.first;
							sightingAge = it->second.second;
						}
				}
				fightNotes.push_back({static_cast<int64_t>(outcome.day), outcome.weAttack ? 1 : 0, outcome.siege ? 1 : 0,
					static_cast<int64_t>(outcome.ownStrength), static_cast<int64_t>(std::lround(ownFactor * 100)),
					static_cast<int64_t>(outcome.enemyStrength), static_cast<int64_t>(std::lround(theirFactor * 100)),
					outcome.gone ? 1 : 0, static_cast<int64_t>(std::lround(outcome.ownLostShare * 100)), static_cast<int64_t>(std::lround(theirLost * 100)),
					modelDistance, sightingAge,
					seen != enemySightings.end() && seen->second.army > 0 ? static_cast<int64_t>(seen->second.army) : -1});
				const HeroTypeID type(outcome.hero);
				const std::string reach = outcome.weAttack ? std::string() : (modelDistance < 0 ? std::string("; it was not among the threats we knew")
					: boost::str(boost::format("; our reach model had it %.1f tiles away (sighting %d day(s) old)") % (modelDistance / 10.0) % sightingAge));
				logAi->info("FairAI %s day %d: hero battle on day %d (%s%s) against %s: our %.0f x %.2f, their %.0f x %.2f - model ratio %.2f - %s, we lost %.0f %%, they %.0f %%%s",
					me.toString(), today, outcome.day, outcome.weAttack ? "we attack" : "we are attacked", outcome.siege ? ", at the walls" : "",
					type.toHeroType() ? type.toHeroType()->getNameTranslated() : std::string("?"),
					outcome.ownStrength, ownFactor, outcome.enemyStrength, theirFactor,
					ownFactor > 0 && theirFactor > 0 ? outcome.ownStrength * ownFactor / (outcome.enemyStrength * theirFactor) : -1.0,
					outcome.gone ? "won" : "lost", outcome.ownLostShare * 100, theirLost * 100, reach);
			}
			if(outcome.gone)
			{
				double strongest = 0;
				for(const auto & [type, sighting] : enemySightings)
					if(today - sighting.day <= 21)
						strongest = std::max(strongest, sighting.strength);
				// the strongest enemy army we knew of (or close to it), and no skirmish: the way is open
				// (a hero that came out of the fog has no sighting: its army as counted in the battle, without the hero's worth)
				const bool wasMain = seen != enemySightings.end() ? seen->second.strength >= 0.7 * strongest : outcome.enemyStrength >= 0.5 * strongest;
				if(wasMain && outcome.enemyStrength >= 0.2 * outcome.ownStrength && outcome.enemyStrength >= 20000)
				{
					decisiveVictoryDay = today;
					logAi->info("FairAI %s day %d: decisive victory - the strongest enemy army we knew of (%.0f) is beaten", me.toString(), today, outcome.enemyStrength);
				}
				defeatedDay[outcome.hero] = today;
				if(seen != enemySightings.end())
					enemySightings.erase(seen);
			}
			else if(seen != enemySightings.end())
			{
				seen->second.day = today;
				seen->second.strength *= std::clamp(outcome.remainingShare, 0.0, 1.0);
				if(outcome.pos.x >= 0)
					seen->second.pos = outcome.pos;
			}
			else
			{
				// it came out of the fog and beat one of our armies: now we know it (its hero's worth unknown: a usual one)
				enemySightings[outcome.hero] = Sighting{today, outcome.enemyStrength * std::clamp(outcome.remainingShare, 0.0, 1.0) * 1.3, outcome.pos};
			}
		}
	}
	for(const auto & [id, object] : known)
	{
		if(object.type != Obj::HERO || !isEnemy(object.owner) || !object.army)
			continue;
		const int age = today - object.lastSeenDay;
		if(age > 3)
			continue;
		// a memory of a hero we have beaten since (its tile is hidden, so the record stayed)
		if(auto beaten = defeatedDay.find(object.subtype.getNum()); beaten != defeatedDay.end() && beaten->second >= object.lastSeenDay && !object.visibleNow)
			continue;
		Threat t;
		t.pos = object.visitablePos;
		const double factor = heroFactor(object);
		t.strength = observedDanger(object) * factor * (1.0 + params.threatGrowth * age);
		if(age == 0)
		{
			enemySightings[object.subtype.getNum()] = Sighting{today, observedDanger(object) * factor, object.visitablePos, observedDanger(object), factor};
			std::unique_lock lock(intelMutex);
			heroFirstSeen.emplace(object.subtype.getNum(), today);
		}
		t.radius = params.threatRadius * (1 + 0.5 * age);
		t.age = age;
		t.hero = id;
		threats.push_back(t);
	}
	// the opposition we know of: enemy heroes seen in the last three weeks (they grow meanwhile), guards on the map
	oppositionHeroes = 0;
	oppositionHero = -1;
	for(const auto & [type, seen] : enemySightings)
	{
		if(today - seen.day <= 21 && seen.strength * (1.0 + 0.03 * (today - seen.day)) > oppositionHeroes)
		{
			oppositionHeroes = seen.strength * (1.0 + 0.03 * (today - seen.day));
			oppositionHero = type;
		}
	}
	hiddenMain = 0;
	for(const auto & [type, seen] : enemySightings)
	{
		if(seen.day != today && today - seen.day <= 21)
			hiddenMain = std::max(hiddenMain, seen.strength * (1.0 + 0.03 * (today - seen.day)));
	}
	ambushTowns.clear();
	oppositionGuards = 0;
	std::map<PlayerColor, int> townsKnown;
	for(const auto & [id, object] : known)
	{
		if(object.type == Obj::TOWN && isEnemy(object.owner))
			ambushTowns.push_back(object.visitablePos);
		if(object.type == Obj::MONSTER && object.army)
			oppositionGuards = std::max(oppositionGuards, observedDanger(object));
		if(object.type == Obj::TOWN && object.owner.isValidPlayer())
			++townsKnown[object.owner];
		if(contactDay < 0 && (object.type == Obj::TOWN || object.type == Obj::HERO) && isEnemy(object.owner))
			contactDay = today; // the first enemy hero or town we get to see
		if((object.type == Obj::TOWN || object.type == Obj::HERO) && isEnemy(object.owner))
		{
			std::unique_lock lock(intelMutex);
			playerContact.emplace(object.owner.getNum(), today);
		}
	}
	// Campaign: our main hero is clearly the strongest force we know of - time to end the game. On at
	// campaignEdge times the strongest enemy hero seen in the last three weeks, off again well below
	// that. "Know of" must include each enemy's best hero (the thieves' guild shows who that is):
	// while we have not seen it for four weeks we do not know their main army, and marching out
	// against scouts only is how a main hero gets caught in the open.
	{
		const CGHeroInstance * leader = ownHero(mainHero);
		const bool before = campaignMode;
		bool mainsSeen = oppositionHeroes > 0;
		bool guildKnown = false;
		for(const auto & [color, type] : guildBestHero)
		{
			if(color == me || !isEnemy(color) || cb->getPlayerStatus(color, false) != EPlayerStatus::INGAME)
				continue;
			guildKnown = true;
			auto seen = enemySightings.find(type);
			auto beaten = defeatedDay.find(type);
			if(beaten != defeatedDay.end() && today - beaten->second <= 28)
				continue; // we know where that army is: we destroyed it
			if(seen == enemySightings.end() || today - seen->second.day > 28)
				mainsSeen = false;
		}
		// pursuit: for three weeks after beating the enemy's main army nothing more needs to be seen
		const bool pursuit = decisiveVictoryDay > 0 && today - decisiveVictoryDay <= 21;
		// no tavern, no guild: at least something of weight must have shown itself
		if(!guildKnown && leader && oppositionHeroes < 0.25 * fieldStrength(leader))
			mainsSeen = false;
		oppositionKnown = mainsSeen || pursuit; // (secondArmy asks the same question)
		// (campaignPrior) The strongest enemy hero we happened to see is a poor measure of what the
		// enemy has: its main army may never have shown itself (a campaign begun at 3.6 times the
		// strongest hero seen ran into an army twice that size). Until proven otherwise the enemy is
		// taken to be as rich as we are, town for town, with half of its army in one hand.
		double reference = oppositionHeroes;
		if(params.campaignPrior && leader)
		{
			double ownTotal = 0;
			for(const auto * hero : perception.ownHeroes())
				ownTotal += static_cast<double>(hero->getArmyStrength());
			for(const auto * town : perception.ownTowns())
				for(const auto & slot : town->Slots())
					ownTotal += stackValue(slot.second.get());
			const int ownTowns = std::max<int>(1, static_cast<int>(perception.ownTowns().size()));
			auto ownPlace = guildTownPlace.find(me);
			double worst = 0;
			for(int c = 0; c < PlayerColor::PLAYER_LIMIT_I; ++c)
			{
				const PlayerColor color(c);
				if(color == me || cb->getPlayerStatus(color, false) != EPlayerStatus::INGAME || !isEnemy(color))
					continue;
				int theirTowns = std::max(1, townsKnown[color]);
				auto place = guildTownPlace.find(color);
				if(place != guildTownPlace.end() && ownPlace != guildTownPlace.end())
				{
					if(place->second < ownPlace->second)
						theirTowns = std::max(theirTowns, ownTowns + 1);
					else if(place->second == ownPlace->second)
						theirTowns = std::max(theirTowns, ownTowns);
				}
				else
					theirTowns = std::max(theirTowns, ownTowns); // no guild: no reason to think them poorer
				worst = std::max(worst, std::clamp(static_cast<double>(theirTowns) / ownTowns, 0.25, 2.0));
			}
			const double leaderFactor = fieldStrength(leader) / std::max(1.0, static_cast<double>(leader->getArmyStrength()));
			reference = std::max(reference, 0.5 * ownTotal * leaderFactor * worst);
		}
		if(!params.leaderFocus || !leader || today < 21 || (oppositionHeroes <= 0 && !pursuit))
			campaignMode = false;
		else
		{
			const double edge = reference > 0 ? fieldStrength(leader) / reference : 1e9;
			if(edge >= params.campaignEdge && (mainsSeen || pursuit))
				campaignMode = true;
			else if(edge < 0.65 * params.campaignEdge)
				campaignMode = false;
		}
		if(before != campaignMode)
			logAi->info("FairAI %s day %d: campaign %s (main hero %.0f, strongest enemy hero seen lately %.0f, measured against %.0f, their best heroes seen: %s%s)", me.toString(), today,
				campaignMode ? "on" : "off", fieldStrength(leader), oppositionHeroes, reference, mainsSeen ? "yes" : "no", pursuit ? ", in pursuit after a decisive victory" : "");
		// On campaign the hero does not walk home for a trickle. But what has grown at home to a good
		// part of its own army is what decides the war (a campaign fought with half of our troops,
		// the other half waiting in the towns, was how games were lost): fetch it, then march again.
		const bool refillBefore = refillMode;
		// (townWorth: the main hero goes for towns on any day, so the troops at home interrupt it on any day)
		if((campaignMode || (params.townWorth && today >= 21)) && leader && params.refillShare > 0)
		{
			double stock = 0;
			for(const auto * town : perception.ownTowns())
				stock += townTroops(town, leader);
			const double army = std::max(1.0, static_cast<double>(leader->getArmyStrength()));
			if(stock >= params.refillShare * army)
				refillMode = true;
			else if(stock < 0.4 * params.refillShare * army)
				refillMode = false;
			if(refillBefore != refillMode)
				logAi->info("FairAI %s day %d: campaign %s (troops waiting in our towns %.0f, main hero's army %.0f)", me.toString(), today,
					refillMode ? "interrupted to fetch troops" : "resumed", stock, army);
		}
		else
			refillMode = false;
		// (muster) What waits in one town and is worth a good part of the leader's army is fetched by
		// the leader, however far that town is. The leader looks four days ahead and the capital is
		// further from the middle of the game on; in games played to the end 60 to 80 % of our army
		// stood in garrisons from then on, and games were lost with more troops than the enemy had.
		const ObjectInstanceID musterBefore = musterTown;
		musterTown = ObjectInstanceID();
		musterUrgent = false;
		if(params.muster && leader && today >= 14)
		{
			const double army = std::max(1.0, static_cast<double>(leader->getArmyStrength()));
			const double share = (campaignMode ? 2.0 : 1.0) * params.musterShare;
			auto stockOf = [&](const CGTownInstance * town)
			{
				double stock = townTroops(town, leader);
				if(auto left = townLeftover.find(town->id); left != townLeftover.end() && params.antiOscillation)
					stock = std::max(0.0, stock - left->second);
				return stock;
			};
			// (what a courier is bringing is not fetched; the town the leader stands in needs no march)
			auto open = [&](const CGTownInstance * town)
			{
				return leader->getVisitedTown() != town && !(supply && !supply->returning && !supply->waiting && supply->town == town->id);
			};
			const CGTownInstance * chosen = nullptr;
			double chosenStock = 0;
			// the town the leader is already marching to stays its goal down to half the bar - whatever
			// has grown elsewhere meanwhile (two towns whose stocks overtook each other had the leader
			// turn round every other day)
			for(const auto * town : perception.ownTowns())
			{
				if(town->id != musterBefore || !open(town))
					continue;
				const double stock = stockOf(town);
				if(stock >= 0.5 * share * army)
				{
					chosen = town;
					chosenStock = stock;
				}
			}
			if(!chosen)
			{
				// else the largest stock that lies beyond the days the leader looks ahead anyway (a town
				// within them is judged as ever) and that it has a way to
				std::vector<std::pair<double, const CGTownInstance *>> candidates;
				for(const auto * town : perception.ownTowns())
				{
					if(!open(town))
						continue;
					const double stock = stockOf(town);
					if(stock >= share * army)
						candidates.emplace_back(stock, town);
				}
				if(!candidates.empty())
				{
					std::sort(candidates.begin(), candidates.end(), [](const auto & a, const auto & b) { return a.first > b.first; });
					auto way = computePaths(leader, false, 30);
					for(const auto & [stock, town] : candidates)
					{
						const CGPathNode * node = way->getPathInfo(town->visitablePos());
						if(!node || !node->reachable() || node->turns <= std::max(params.maxTravelTurns, params.mainHorizon))
							continue;
						chosen = town;
						chosenStock = stock;
						break;
					}
				}
			}
			if(chosen)
			{
				musterTown = chosen->id;
				musterUrgent = chosenStock >= 2.0 * share * army;
			}
			if(musterTown != musterBefore)
			{
				if(chosen)
				{
					logAi->info("FairAI %s day %d: muster - %.0f wait in %s, the main hero leads %.0f: it goes there for them%s", me.toString(), today, chosenStock,
						chosen->getNameTranslated(), army, musterUrgent ? " and leaves everything else" : "");
					if(statsFile)
						stats[phaseOf(today)]["fetchFar"] += 1;
				}
				else
					logAi->info("FairAI %s day %d: muster over", me.toString(), today);
			}
		}
	}
	// Does an enemy still have towns we have not found? Every enemy in the game has at least one
	// (or is about to drop out); the thieves' guild ranks the players by their number of towns.
	huntWanted = false;
	int deficit = 0;
	const int myTowns = static_cast<int>(perception.ownTowns().size());
	auto myPlace = guildTownPlace.find(me);
	for(int c = 0; c < PlayerColor::PLAYER_LIMIT_I; ++c)
	{
		const PlayerColor color(c);
		if(color == me || cb->getPlayerStatus(color, false) != EPlayerStatus::INGAME || !isEnemy(color))
			continue;
		int expected = 1;
		auto place = guildTownPlace.find(color);
		if(place != guildTownPlace.end() && myPlace != guildTownPlace.end())
		{
			if(place->second < myPlace->second)
				expected = myTowns + 1;
			else if(place->second == myPlace->second)
				expected = std::max(1, myTowns);
		}
		if(townsKnown[color] < expected)
			huntWanted = true;
		deficit = std::max(deficit, std::max(townsKnown[color], place != guildTownPlace.end() && myPlace != guildTownPlace.end() && place->second < myPlace->second ? myTowns + 1 : 0) - myTowns);
	}
	townDeficit = deficit;
	// Test hook for the arena (FAI_TEST_PURSUER=<day>): from that day on, whenever a Sanctuary lies
	// within today's march of the main hero, an army five times its strength stands next to it. Rare
	// situations cannot be checked by counting wins; this one is staged and read in the log.
	if(const char * test = std::getenv("FAI_TEST_PURSUER"))
	{
		const CGHeroInstance * leader = ownHero(mainHero);
		if(leader && today >= std::atoi(test) && !sanctuaries.empty())
		{
			auto paths = computePaths(leader, false);
			bool townToday = false; // a town of ours within today's march is the better shelter: not the case to stage
			for(const auto * town : perception.ownTowns())
			{
				const CGPathNode * home = paths->getPathInfo(town->visitablePos());
				townToday = townToday || (home && home->reachable() && home->turns == 0);
			}
			for(const int3 & tile : sanctuaries)
			{
				const CGPathNode * node = paths->getPathInfo(tile);
				if(townToday || !node || !node->reachable() || node->turns > 0)
					continue;
				Threat staged;
				staged.pos = leader->visitablePos();
				staged.strength = 5.0 * fieldStrength(leader) + 1000;
				staged.radius = 40;
				staged.hero = ObjectInstanceID(1000000); // nobody's: a threat without a hero is skipped
				threats.push_back(staged);
				logAi->info("FairAI %s day %d: TEST a pursuer of %.0f stands next to %s at %s; Sanctuary at %s is %.2f days away", me.toString(), today,
					staged.strength, leader->getNameTranslated(), staged.pos.toString(), tile.toString(), node->cost);
				break;
			}
		}
	}
}

void AdventureAI::noteHeroSkills(HeroTypeID hero, const std::array<int, 4> & skills)
{
	if(!hero.hasValue())
		return;
	std::unique_lock lock(intelMutex);
	HeroIntel & intel = heroIntel[hero.getNum()];
	intel.day = cb->getDate(Date::DAY);
	for(size_t i = 0; i < 4; ++i)
		intel.skills[i] = skills[i];
}

std::array<double, 4> AdventureAI::heroSkillsEstimate(const ObservedObject & hero) const
{
	const HeroTypeID type(hero.subtype.getNum());
	const CHero * heroType = type.hasValue() ? type.toHeroType() : nullptr;
	const CHeroClass * heroClass = heroType ? heroType->heroClass : nullptr;

	// How much of a main hero is it? Judged by the army it leads against our biggest.
	double ownBest = 1;
	for(const auto * own : perception.ownHeroes())
		ownBest = std::max(ownBest, static_cast<double>(own->getArmyStrength()));
	const double army = hero.army ? static_cast<double>(hero.army->expectedStrength()) : 0.0;
	const double weight = std::clamp(army / (0.5 * ownBest), 0.25, 1.0);

	// What `levels` level-ups add: the class decides where the points go; a hero that fights its way
	// across the map also collects skill points at map objects and wears artifacts.
	auto grow = [&](std::array<double, 4> & skills, double fromLevel, double levels)
	{
		for(size_t i = 0; i < 4; ++i)
		{
			double perLevel = 0.25;
			if(heroClass && heroClass->primarySkillLowLevel.size() >= 4 && heroClass->primarySkillHighLevel.size() >= 4)
			{
				const double low = std::clamp(10.0 - fromLevel, 0.0, levels); // levels gained below level 10
				perLevel = levels > 0 ? (low * heroClass->primarySkillLowLevel[i] + (levels - low) * heroClass->primarySkillHighLevel[i]) / (100.0 * levels) : 0.0;
			}
			skills[i] += levels * (perLevel + 0.2 * weight);
		}
	};

	std::array<double, 4> skills{};
	if(hero.heroSkills)
	{
		for(size_t i = 0; i < 4; ++i)
			skills[i] = (*hero.heroSkills)[i];
		return skills;
	}
	{
		std::unique_lock lock(intelMutex);
		auto intel = heroIntel.find(hero.subtype.getNum());
		if(intel != heroIntel.end() && intel->second.day >= 0)
		{
			// shown to us before: it has grown since (main heroes gain a level every four to five days)
			skills = intel->second.skills;
			const double level = 1.0 + weight * intel->second.day / 5.5;
			grow(skills, level, weight * std::max(0, today - intel->second.day) / 5.5);
			return skills;
		}
	}
	if(heroClass && heroClass->primarySkillInitial.size() >= 4)
		for(size_t i = 0; i < 4; ++i)
			skills[i] = heroClass->primarySkillInitial[i];
	// (freshHeroes) Since when has it been growing? A hero that shows up long after we met its owner
	// is one hired lately - the enemy hands the armies of its fallen leaders to new heroes. Not so the
	// one the thieves' guild names as the owner's best, and not the heroes of the first two weeks of
	// contact: those may have been at it since day 1.
	int since = 1;
	if(params.freshHeroes)
	{
		std::unique_lock lock(intelMutex);
		auto first = heroFirstSeen.find(hero.subtype.getNum());
		auto contact = playerContact.find(hero.owner.getNum());
		const int firstDay = first != heroFirstSeen.end() ? first->second : today;
		const int contactDay = contact != playerContact.end() ? contact->second : today;
		auto best = guildBestHero.find(hero.owner);
		const bool named = best != guildBestHero.end() && best->second == hero.subtype.getNum();
		if(firstDay - contactDay > 14 && !named)
			since = std::max(1, firstDay - 14);
	}
	grow(skills, 1.0, std::min(25.0, weight * (today - since + 1) / 5.5));
	return skills;
}

double AdventureAI::heroFactor(const ObservedObject & hero) const
{
	if(!params.heroModel)
		return params.enemyHeroFactor;
	// Attack and defence only. In 266 logged open-field battles against Nullkiller2 the ratio
	// own army / (enemy army x this factor of the enemy hero) told the outcome: from 1.3 on every
	// battle was won, around 1.0 it was a coin flip; spell power and knowledge added nothing to that.
	const auto skills = heroSkillsEstimate(hero);
	double factor = std::sqrt((1.0 + 0.05 * skills[0]) * (1.0 + 0.05 * skills[1]));

	// What every player knows of a hero by its face: the skills it starts with and its specialty.
	// A hero keeps raising the skills it starts with; an offence or armourer specialist (Crag Hack,
	// Tazar ...) gains a twentieth of the skill's effect with every level - the outliers of the log.
	const HeroTypeID type(hero.subtype.getNum());
	const CHero * heroType = type.hasValue() ? type.toHeroType() : nullptr;
	if(heroType)
	{
		double initial = 0;
		if(heroType->heroClass && heroType->heroClass->primarySkillInitial.size() >= 4)
			for(int v : heroType->heroClass->primarySkillInitial)
				initial += v;
		// level: every level-up gives one skill point; about a fifth more comes from the map
		const double level = std::clamp(1.0 + (skills[0] + skills[1] + skills[2] + skills[3] - initial) / 1.2, 1.0, 40.0);
		double melee = 0;
		double armour = 0;
		for(const auto & [skill, mastery] : heroType->secSkillsInit)
		{
			const CSkill * info = skill.toSkill();
			if(!info)
				continue;
			const int reached = level >= 6 ? 3 : std::clamp<int>(mastery, 1, 3);
			for(const auto & effect : info->at(reached).effects)
			{
				if(effect->type == BonusType::PERCENTAGE_DAMAGE_BOOST && effect->subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeMelee))
					melee += effect->val / 100.0;
				else if(effect->type == BonusType::GENERAL_DAMAGE_REDUCTION)
					armour += effect->val / 100.0;
			}
		}
		for(const auto & bonus : heroType->specialty)
		{
			if(bonus->type == BonusType::PERCENTAGE_DAMAGE_BOOST && bonus->subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeMelee))
				melee *= 1.0 + bonus->val / 100.0 * level;
			else if(bonus->type == BonusType::GENERAL_DAMAGE_REDUCTION)
				armour *= 1.0 + bonus->val / 100.0 * level;
		}
		factor *= std::sqrt((1.0 + melee) / std::max(0.3, 1.0 - armour));
	}
	// Magic we have seen this hero cast. In the early battles of the log spells decided nothing; late
	// in a game a hero that slows or hastens a whole army at once (a master of the school) wins
	// battles its numbers would lose: 346k of ours were destroyed by 205k under a mass Slow.
	{
		std::unique_lock lock(intelMutex);
		auto known = heroIntel.find(hero.subtype.getNum());
		if(known != heroIntel.end())
			factor *= params.magicWorth ? magicWorthOf(known->second, skills) : known->second.magic;
	}
	return factor;
}

std::string AdventureAI::describeEstimate(HeroTypeID hero) const
{
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type != Obj::HERO || object.subtype.getNum() != hero.getNum() || !object.army)
			continue;
		const auto skills = heroSkillsEstimate(object);
		return boost::str(boost::format("estimated army %.0f (seen day %d), attack %.1f defence %.1f power %.1f knowledge %.1f, hero factor %.2f")
			% observedDanger(object) % object.lastSeenDay % skills[0] % skills[1] % skills[2] % skills[3] % heroFactor(object));
	}
	return "never seen on the map";
}

double AdventureAI::magicWorthOf(const HeroIntel & intel, const std::array<double, 4> & skills) const
{
	// Measured in staged battles against BattleAI, the combat AI of Nullkiller2 (arena/magic_calib.py):
	// by how much our army must grow to keep winning half of the battles when the enemy hero has the
	// spell. Spells on a whole army count for little, damage for what it takes off our army.
	double worth = 1.0;
	if(intel.massSpeed || intel.massOther)
		worth = 1.05;
	if(intel.raises)
		worth = std::max(worth, 1.15);
	if(intel.blinds)
		worth = std::max(worth, 1.2);
	if(!intel.damageSpells.empty())
	{
		// the damage it can cast in a battle - its best spell we know of, at the power and with the
		// spell points we estimate, five rounds at most - against the health of our main army
		const int power = std::max(1, static_cast<int>(std::lround(skills[2])));
		const double mana = 10.0 * std::max(1.0, skills[3]);
		double damage = 0;
		for(int id : intel.damageSpells)
		{
			const CSpell * spell = SpellID(id).toSpell();
			if(!spell)
				continue;
			const auto & info = spell->getLevelInfo(1);
			double perCast = static_cast<double>(spell->calculateRawEffectValue(1, power, 1));
			// a chain goes on at half strength each time, a blast or a whole-field spell hits about two stacks
			for(const auto & [name, node] : info.battleEffects.Struct())
			{
				if(node["chainLength"].Integer() > 1)
					perCast *= 1.9;
			}
			if(info.range.empty() || info.range.size() > 1)
				perCast *= 1.5;
			const int cost = std::max(1, spell->getCost(1));
			damage = std::max(damage, perCast * std::min(5.0, std::floor(mana / cost)));
		}
		double health = 0;
		if(const CGHeroInstance * leader = ownHero(mainHero))
		{
			for(const auto & [slot, stack] : leader->Slots())
			{
				if(stack && stack->getCreature())
					health += static_cast<double>(stack->getCount()) * stack->getCreature()->getMaxHealth();
			}
		}
		if(health > 0)
			worth = std::max(worth, 1.0 + 0.7 * std::min(1.0, damage / health));
	}
	return worth;
}

void AdventureAI::noteBattleOutcome(HeroTypeID hero, bool gone, double remainingShare, double ownStrength, double enemyStrength, const int3 & pos,
	int ownHero, bool weAttack, bool siege, double ownLostShare, int day)
{
	if(!hero.hasValue())
		return;
	std::unique_lock lock(intelMutex);
	pendingOutcomes.push_back(BattleOutcome{hero.getNum(), gone, remainingShare, ownStrength, enemyStrength, pos, ownHero, weAttack, siege, ownLostShare, day});
}

double AdventureAI::outmatchedBy(const CGHeroInstance * hero) const
{
	// Whenever the enemy attacked our biggest army it won: it comes only when it is the stronger, and
	// it comes unseen (out of unexplored land, through a portal). What we can know: an army stronger
	// than ours has shown itself on our side of the map in the last days, or has beaten one of ours
	// there. Until it is seen no more, or we have outgrown it, the main hero sleeps behind walls.
	if(!params.fortress || !hero)
		return 0;
	const double own = fieldStrength(hero);
	const int3 here = hero->visitablePos();
	double worst = 0;
	for(const auto & [type, seen] : enemySightings)
	{
		const int age = today - seen.day;
		if(age > params.fortressDays || seen.pos.x < 0)
			continue;
		const double strength = seen.strength * (1.0 + 0.03 * age);
		if(strength <= params.fortressRatio * own || strength <= worst)
			continue;
		bool near = seen.pos.dist2d(here) <= params.fortressRadius;
		for(const auto * town : perception.ownTowns())
			near = near || town->visitablePos().dist2d(seen.pos) <= params.fortressRadius;
		if(near)
			worst = strength;
	}
	return worst;
}

void AdventureAI::noteHeroSpell(HeroTypeID hero, SpellID spell, int affected)
{
	if(!hero.hasValue() || !spell.hasValue())
		return;
	{
		std::unique_lock lock(intelMutex);
		HeroIntel & seen = heroIntel[hero.getNum()];
		const auto * info = spell.toSpell();
		if(info && info->isDamage())
			seen.damageSpells.insert(spell.getNum());
		else if(spell == SpellID::BLIND)
			seen.blinds = true;
		else if(spell == SpellID::RESURRECTION || spell == SpellID::ANIMATE_DEAD)
			seen.raises = true;
		else if(affected >= 4 && (spell == SpellID::SLOW || spell == SpellID::HASTE))
			seen.massSpeed = true;
		else if(affected >= 4)
			seen.massOther = true;
	}
	double magic = 1.0;
	if(spell == SpellID::SLOW || spell == SpellID::HASTE)
		magic = affected >= 4 ? (spell == SpellID::SLOW ? 1.6 : 1.4) : 1.1;
	else if(spell == SpellID::BLIND || spell == SpellID::BERSERK || spell == SpellID::HYPNOTIZE || spell == SpellID::FORGETFULNESS)
		magic = affected >= 4 ? 1.6 : 1.15;
	else if(spell == SpellID::IMPLOSION || spell == SpellID::CHAIN_LIGHTNING || spell == SpellID::METEOR_SHOWER || spell == SpellID::ARMAGEDDON
		|| spell == SpellID::RESURRECTION || spell == SpellID::ANIMATE_DEAD || spell == SpellID::CLONE)
		magic = 1.4;
	else if(affected >= 4)
		magic = 1.15; // a mass blessing or curse
	if(magic <= 1.0)
		return;
	std::unique_lock lock(intelMutex);
	HeroIntel & intel = heroIntel[hero.getNum()];
	if(magic > intel.magic)
	{
		logAi->info("FairAI %s: enemy hero %s casts %s on %d units - counted as %.2f times its strength from now on", me.toString(),
			hero.toHeroType() ? hero.toHeroType()->getNameTranslated() : std::string("?"), spell.toSpell()->getNameTranslated(), affected, magic);
		intel.magic = magic;
	}
}

double AdventureAI::heroFightLoss(const CGHeroInstance * hero, const ObservedObject & enemy) const
{
	if(!hero || !enemy.army || enemy.army->units.empty() || hero->stacksCount() == 0)
		return -1;
	const auto skills = heroSkillsEstimate(enemy);
	const int attack = static_cast<int>(std::lround(skills[0]));
	const int defence = static_cast<int>(std::lround(skills[1]));
	// the enemy casts every round and ours does too: what counts is who is the better mage
	const double magic = std::clamp(std::sqrt((1.0 + 0.05 * skills[2]) * (1.0 + 0.05 * skills[3])) / std::max(1.0, hero->getMagicStrength()), 0.8, 1.6);

	// (spellForecast) our damage spells are played, and the enemy's as far as we saw it cast them:
	// with the power we estimate and a full store of spell points
	Combat::HeroSpec ownSpec;
	Combat::HeroSpec enemySpec;
	const Combat::HeroSpec * enemySpells = nullptr;
	if(params.spellForecast)
	{
		std::unique_lock lock(intelMutex);
		auto intel = heroIntel.find(enemy.subtype.getNum());
		if(intel != heroIntel.end() && !intel->second.damageSpells.empty())
		{
			enemySpec.power = std::max(1, static_cast<int>(std::lround(skills[2])));
			enemySpec.mana = 10 * std::max(1, static_cast<int>(std::lround(skills[3])));
			for(int spell : intel->second.damageSpells)
				enemySpec.spells.emplace_back(SpellID(spell), 1);
			enemySpells = &enemySpec;
		}
	}
	const double pointShare = manaPointShare(hero);

	auto simulate = [&](bool upper) -> FightForecast
	{
		FightForecast result;
		std::vector<Combat::StackSpec> stacks;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(stack && stack->getCreature() && stack->getCount() > 0)
				stacks.push_back({stack.get(), stack->getCreature(), stack->getCount(), BattleSide::ATTACKER});
		}
		for(const auto & unit : enemy.army->units)
		{
			if(!unit.creature.hasValue())
				continue;
			const CCreature * type = unit.creature.toCreature();
			const int count = upper ? unit.maxCount : std::max(1, (unit.minCount + unit.maxCount + 1) / 2);
			if(type && count > 0)
				stacks.push_back({type, type, count, BattleSide::DEFENDER, attack, defence});
		}
		try
		{
			const JsonNode & layout = cb->getSettings().getValue(EGameSettings::COMBAT_LAYOUTS)["default"];
			const Combat::Forecast forecast = Combat::forecastBattle(stacks, BattleSide::ATTACKER, layout, 30, Combat::Stance::CHARGE, ownSpells(hero, ownSpec), enemySpells);
			// against a hero the spell points are well spent: the plan that keeps most of the army
			const int plan = forecast.bestPlan(0);
			result.win = forecast.win(plan);
			result.lossShare = forecast.lossShare(plan);
			result.mana = forecast.manaSpent[plan];
			result.manaShare = pointShare * result.mana;
		}
		catch(const std::exception & e)
		{
			logAi->warn("FairAI %s: battle forecast failed (%s)", me.toString(), e.what());
		}
		return result;
	};
	const FightForecast likely = simulate(false);
	const FightForecast worst = simulate(true);
	if(!likely.win || !worst.win)
		return -1;
	const double share = std::min(1.0, (0.7 * likely.lossShare + 0.3 * worst.lossShare) * magic);
	logAi->debug("FairAI %s: %s against hero %d: forecast loss %.2f (at most %.2f), enemy attack %d defence %d, magic x%.2f, our spells %d points, its spells known %d", me.toString(),
		hero->getNameTranslated(), enemy.subtype.getNum(), likely.lossShare, worst.lossShare, attack, defence, magic, likely.mana, static_cast<int>(enemySpec.spells.size()));
	if(share > (isMain(hero) ? 0.6 : 0.3))
		return -1;
	return share * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
}

bool AdventureAI::surrenderWorth(int cost, double ownValue, double enemyLoss) const
{
	if(!params.surrender)
		return false;
	// the hero must be hired back from the tavern this week
	bool tavern = false;
	for(const auto * town : perception.ownTowns())
		tavern = tavern || town->hasBuilt(BuildingID::TAVERN);
	const double gold = cb->getResourceAmount(GameResID::GOLD);
	if(!tavern || cost + GameConstants::HERO_GOLD_COST > gold || static_cast<int>(perception.ownHeroes().size()) > params.maxHeroes)
		return false;
	if(params.surrender >= 2)
	{
		// (surrender 2) Buying an army free costs the gold its creatures cost to recruit. That is a
		// good use of gold that lies idle - troops beyond what our dwellings grow - and a bad one of
		// gold the troops waiting in our dwellings need (the first version bought handfuls of
		// creatures free again and again while towns went unbuilt). So: a real army only, paid from
		// what is left once everything that waits at home is paid for, and not twice within three days.
		if(ownValue < params.surrenderMinArmy || cb->getDate(Date::DAY) - lastSurrenderDay.load() < 3)
			return false;
		// The tavern is dealt anew when the week ends, and the army with it: on a week's last day the
		// hero can only come back if our own turn of today is still to come (players move in the
		// order of their colours).
		if((cb->getDate(Date::DAY) - 1) % 7 == 6)
		{
			bool turnToCome = cb->isPlayerMakingTurn(me);
			for(int colour = 0; colour < me.getNum() && !turnToCome; ++colour)
				turnToCome = cb->isPlayerMakingTurn(PlayerColor(colour));
			if(!turnToCome)
				return false;
		}
		if(gold - cost - GameConstants::HERO_GOLD_COST < waitingTroopGold.load())
			return false;
		// the army stays ours, the enemy's hero gets no experience for it, and ours keeps its levels
		return (ownValue - 0.5 * enemyLoss) * params.goldPerArmyValue * economy.armyPremium > (cost + GameConstants::HERO_GOLD_COST) * economy.goldWeight * 0.8;
	}
	// Fighting on costs the army but hurts the enemy; surrendering costs gold. Army counts for more
	// and gold for less when gold piles up (creatures, not gold, are what we lack then).
	const double fightOn = (ownValue - enemyLoss) * params.goldPerArmyValue * economy.armyPremium;
	const double price = (cost + GameConstants::HERO_GOLD_COST) * economy.goldWeight;
	return fightOn > price;
}

void AdventureAI::noteSurrender(HeroTypeID hero)
{
	rehireHero = hero.getNum();
	rehireDay = cb->getDate(Date::DAY);
	lastSurrenderDay = cb->getDate(Date::DAY);
}

double AdventureAI::fetchFactor(const CGHeroInstance * hero) const
{
	// On campaign the main hero is strong enough: what grows at home stays there and holds the towns;
	// walking back for it pays only when nothing else is left to do (any positive value wins then).
	if(campaign(hero))
		return 0.2;
	// With couriers the troops come to the hero: its own days are for fighting, not for the road home
	if(params.couriers && perception.ownHeroes().size() > 1)
		return 0.35;
	// - while a supply run is really on its way; troops that no courier can bring are the leader's to fetch
	if(params.supplyRuns && supply && !supply->returning && !supply->waiting)
		return 0.35;
	return 1.0;
}

double AdventureAI::superiority(const CGHeroInstance * hero) const
{
	if(!hero || oppositionHeroes <= 0)
		return 0;
	return fieldStrength(hero) / oppositionHeroes;
}

double AdventureAI::threatAt(const int3 & tile, ObjectInstanceID ignoreHero, bool cautious) const
{
	double worst = 0;
	for(const auto & t : threats)
	{
		if(t.hero == ignoreHero || t.strength <= worst)
			continue;
		if(!cautious && t.age > 2)
			continue;
		double radius = cautious ? params.threatRadius + 8.0 * t.age : t.radius;
		if(enemyDistance(t.hero, t.pos, t.strength, tile, radius) <= radius)
			worst = t.strength;
	}
	// (townAmbush) An enemy hero we have seen lately but cannot see now may well sit in one of its
	// towns - a hero inside a town does not show on the map. Within a day's march of an enemy town
	// that army has to be reckoned with. (A main hero went collecting mines beside the enemy's town
	// with 29k; the enemy's main army came out of the gate.)
	if(params.townAmbush && ambushCounts && hiddenMain > worst)
	{
		for(const auto & pos : ambushTowns)
		{
			if(pos.z == tile.z && pos.dist2d(tile) <= params.threatRadius)
			{
				worst = hiddenMain;
				break;
			}
		}
	}
	return worst;
}

double AdventureAI::enemyDistance(ObjectInstanceID enemy, const int3 & from, double strength, const int3 & tile, double maxTiles) const
{
	if(!cb->isInTheMap(tile) || !cb->isInTheMap(from))
		return std::numeric_limits<double>::infinity();
	if(!params.pathThreats)
	{
		const double straight = from.dist2d(tile);
		return from.z == tile.z && straight <= maxTiles ? straight : std::numeric_limits<double>::infinity();
	}
	ReachMap & reach = reachCache[{enemy.getNum(), from}];
	if(reach.day != today || reach.strength != strength || reach.maxTiles < maxTiles)
		computeReach(reach, from, strength, std::max(maxTiles, reach.day == today ? reach.maxTiles : 0.0));
	const int3 size = cb->getMapSize();
	const float d = reach.tiles[(static_cast<size_t>(tile.z) * size.y + tile.y) * size.x + tile.x];
	return d < 0 || d > maxTiles ? std::numeric_limits<double>::infinity() : d;
}

void AdventureAI::checkReachAgainstEngine(const CGHeroInstance * hero)
{
	// our own hero, guards ignored on both sides: where the game's pathfinder can go, our model must go too
	rememberStaticObjects();
	auto engine = computePaths(hero, true);
	ReachMap model;
	computeReach(model, hero->visitablePos(), 1e18, 1e9, true);
	const int3 size = cb->getMapSize();
	int both = 0, onlyEngine = 0, onlyModel = 0;
	std::string examples, modelExamples;
	for(int z = 0; z < size.z; ++z)
		for(int y = 0; y < size.y; ++y)
			for(int x = 0; x < size.x; ++x)
			{
				const int3 tile(x, y, z);
				const CGPathNode * node = engine->getPathInfo(tile);
				const bool e = node && node->reachable();
				const bool m = model.tiles[(static_cast<size_t>(z) * size.y + y) * size.x + x] >= 0;
				if(e && m)
					++both;
				else if(e)
				{
					if(++onlyEngine <= 5)
						examples += " " + tile.toString();
				}
				else if(m)
				{
					if(++onlyModel <= 4)
					{
						const TerrainTile * t = cb->getTile(tile, false);
						modelExamples += " " + tile.toString() + (node ? "[acc " + std::to_string(static_cast<int>(node->accessible)) + "]" : "[-]");
						if(t)
							for(const auto & id : t->visitableObjects)
								if(const auto * o = cb->getObj(id, true))
									modelExamples += o->getObjectName() + ";";
					}
				}
			}
	logAi->info("FairAI %s reach check day %d (%s at %s%s): both %d, only engine %d%s, only model %d%s", me.toString(), today,
		hero->getNameTranslated(), hero->visitablePos().toString(), hero->isGarrisoned() ? " garrisoned" : "", both, onlyEngine, examples, onlyModel, modelExamples);
}

void AdventureAI::rememberStaticObjects()
{
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type == Obj::TOWN)
		{
			// walls and towers show on the map; they can grow, so look again whenever the town is in sight
			if(const auto * town = dynamic_cast<const CGTownInstance *>(perception.visibleObject(id)))
				townFortLevel[id] = town->fortLevel();
		}
		if((object.type == Obj::CREATURE_GENERATOR1 || object.type == Obj::CREATURE_GENERATOR4) && !dwellingKinds.count(id))
		{
			// what a dwelling breeds shows on the map: remembered for when it is out of sight (its guards follow from it)
			if(const auto * dwelling = dynamic_cast<const CGDwelling *>(perception.visibleObject(id)))
			{
				auto & kinds = dwellingKinds[id];
				for(const auto & level : dwelling->creatures)
					if(!level.second.empty())
						kinds.push_back(level.second.front());
			}
		}
		if(object.type == Obj::HERO || (entryMasks.count(id) && teleportChannels.count(id)))
			continue;
		const CGObjectInstance * instance = perception.visibleObject(id);
		if(!instance)
			continue;
		if(!entryMasks.count(id))
		{
			// only objects that block their entrance restrict the direction (game rule, see CMap::checkForVisitableDir)
			uint16_t mask = 0x1FF;
			if(instance->blockingAt(instance->visitablePos()))
			{
				mask = 0;
				for(int dx = -1; dx <= 1; ++dx)
					for(int dy = -1; dy <= 1; ++dy)
						if((dx || dy) && instance->appearance->isVisitableFrom(dx, dy))
							mask |= 1 << ((dy + 1) * 3 + dx + 1);
			}
			entryMasks[id] = mask;
		}
		if(object.type == Obj::MONOLITH_ONE_WAY_ENTRANCE || object.type == Obj::MONOLITH_TWO_WAY
			|| object.type == Obj::SUBTERRANEAN_GATE || object.type == Obj::WHIRLPOOL)
		{
			if(const auto * teleport = dynamic_cast<const CGTeleport *>(instance))
				teleportChannels[id] = teleport->channel;
		}
	}
}

bool AdventureAI::inPass(const int3 & tile, const CPathsInfo & paths, const int3 * alsoOpen) const
{
	// A tile whose open neighbours fall into two or more groups that do not touch each other: a
	// hero standing there cuts the way for everybody else. (Moving diagonally past a corner is
	// allowed, so neighbours that touch at a corner count as connected.)
	static const int ring[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
	bool open[8];
	int count = 0;
	for(int i = 0; i < 8; ++i)
	{
		const int3 n(tile.x + ring[i][0], tile.y + ring[i][1], tile.z);
		open[i] = false;
		if(!cb->isInTheMap(n))
			continue;
		if(alsoOpen && n == *alsoOpen)
			open[i] = true; // the tile the hero is leaving
		else if(const CGPathNode * node = paths.getPathInfo(n))
			open[i] = node->accessible == EPathAccessibility::ACCESSIBLE || node->accessible == EPathAccessibility::GUARDED;
		count += open[i] ? 1 : 0;
	}
	if(count < 2)
		return count == 1 ? false : true; // a dead end blocks nobody; a tile with no way on is no place to stand
	int group[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int groups = 0;
	for(int i = 0; i < 8; ++i)
	{
		if(!open[i] || group[i] >= 0)
			continue;
		group[i] = groups;
		int stack[8];
		int top = 0;
		stack[top++] = i;
		while(top > 0)
		{
			const int a = stack[--top];
			for(int b = 0; b < 8; ++b)
			{
				if(!open[b] || group[b] >= 0)
					continue;
				if(std::abs(ring[a][0] - ring[b][0]) <= 1 && std::abs(ring[a][1] - ring[b][1]) <= 1)
				{
					group[b] = groups;
					stack[top++] = b;
				}
			}
		}
		++groups;
	}
	return groups >= 2;
}

std::vector<uint16_t> AdventureAI::fogDistances(const int3 & from) const
{
	// Steps from `from` to every tile of its level through land we know to be open and land we have
	// not seen (taken for open): how a player guesses the way to a town that shows beyond the fog.
	const int3 size = cb->getMapSize();
	const size_t width = size.x;
	std::vector<uint16_t> dist(width * size.y, UINT16_MAX);
	if(!cb->isInTheMap(from))
		return dist;
	std::vector<uint8_t> blocked(dist.size(), 0);
	for(int y = 0; y < size.y; ++y)
		for(int x = 0; x < size.x; ++x)
		{
			const TerrainTile * t = cb->getTile(int3(x, y, from.z), false);
			if(t && (!t->getTerrain()->isPassable() || t->isWater() || (t->blocked() && !t->visitable())))
				blocked[y * width + x] = 1;
		}
	std::vector<uint32_t> queue;
	queue.reserve(dist.size());
	const size_t start = from.y * width + from.x;
	dist[start] = 0;
	queue.push_back(static_cast<uint32_t>(start));
	for(size_t head = 0; head < queue.size(); ++head)
	{
		const uint32_t i = queue[head];
		const int x = static_cast<int>(i % width);
		const int y = static_cast<int>(i / width);
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const int nx = x + dx;
				const int ny = y + dy;
				if((!dx && !dy) || nx < 0 || ny < 0 || nx >= size.x || ny >= size.y)
					continue;
				const size_t n = ny * width + nx;
				if(blocked[n] || dist[n] != UINT16_MAX)
					continue;
				dist[n] = static_cast<uint16_t>(dist[i] + 1);
				queue.push_back(static_cast<uint32_t>(n));
			}
	}
	return dist;
}

void AdventureAI::computeReach(ReachMap & reach, const int3 & from, double strength, double maxTiles, bool validation) const
{
	const int3 size = cb->getMapSize();
	const size_t width = size.x;
	const size_t height = size.y;
	auto indexOf = [&](const int3 & pos) { return (static_cast<size_t>(pos.z) * height + pos.y) * width + pos.x; };
	reach.origin = from;
	reach.day = today;
	reach.strength = strength;
	reach.maxTiles = maxTiles;
	reach.tiles.assign(width * height * size.z, -1.f);

	// what we know of the land: 0 open, 1 blocked, 2 ends the walk (our towns: attacking them)
	std::vector<uint8_t> kind(reach.tiles.size(), 0);
	std::vector<float> stepCost(reach.tiles.size(), 1.f);
	const TerrainTile * start = cb->getTile(from, false);
	const bool onWater = start && start->isWater();
	for(int z = 0; z < size.z; ++z)
		for(int y = 0; y < size.y; ++y)
			for(int x = 0; x < size.x; ++x)
			{
				const int3 pos(x, y, z);
				const TerrainTile * t = cb->getTile(pos, false);
				const size_t i = indexOf(pos);
				if(!t)
				{
					if(validation)
						kind[i] = 1;
					continue; // never seen: assume open land
				}
				// resources, artifacts and other things to pick up do not block: the hero takes them and walks on
				if(!t->getTerrain()->isPassable() || (t->isWater() != onWater) || (t->blocked() && !t->visitable()))
					kind[i] = 1;
				else if(t->hasRoad())
					stepCost[i] = std::max(0.5f, t->getRoad()->movementCost / 100.f);
			}
	// known monsters this hero would not beat block the tiles they guard
	const auto known = perception.knownObjects();
	for(const auto & [id, object] : known)
	{
		if(object.type != Obj::MONSTER || observedDanger(object) <= strength)
			continue;
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const int3 pos = object.visitablePos + int3(dx, dy, 0);
				if(cb->isInTheMap(pos))
					kind[indexOf(pos)] = 1;
			}
	}
	for(const auto * town : perception.ownTowns())
		if(!validation)
			kind[indexOf(town->visitablePos())] = 2;
	// entrances that only open to some sides (towns, mines, dwellings...): entered and left only there
	std::vector<uint16_t> entry(reach.tiles.size(), 0x1FF);
	for(const auto & [id, mask] : entryMasks)
	{
		auto object = known.find(id);
		if(object != known.end() && mask != 0x1FF)
			entry[indexOf(object->second.visitablePos)] &= mask;
	}
	auto dirBit = [](int dx, int dy) { return static_cast<uint16_t>(1 << ((dy + 1) * 3 + dx + 1)); };

	// teleporters and subterranean gates: entrance tile -> exit tiles we have seen
	std::map<size_t, std::vector<size_t>> jumps;
	for(const auto & [id, channel] : teleportChannels)
	{
		auto entrance = known.find(id);
		if(entrance == known.end())
			continue;
		for(ObjectInstanceID exitId : cb->getTeleportChannelExits(channel, me))
		{
			auto exit = known.find(exitId);
			if(exitId != id && exit != known.end())
				jumps[indexOf(entrance->second.visitablePos)].push_back(indexOf(exit->second.visitablePos));
		}
	}

	using Entry = std::pair<float, size_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
	const size_t startIndex = indexOf(from);
	reach.tiles[startIndex] = 0;
	open.emplace(0.f, startIndex);
	auto relax = [&](size_t next, float total)
	{
		if(kind[next] == 1 || total > maxTiles || (reach.tiles[next] >= 0 && reach.tiles[next] <= total))
			return;
		reach.tiles[next] = total;
		open.emplace(total, next);
	};
	while(!open.empty())
	{
		auto [cost, index] = open.top();
		open.pop();
		if(cost > reach.tiles[index])
			continue;
		if(index != startIndex && kind[index] == 2)
			continue; // attacking our town ends the move
		const int x = static_cast<int>(index % width);
		const int y = static_cast<int>((index / width) % height);
		const int z = static_cast<int>(index / (width * height));
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const int3 next(x + dx, y + dy, z);
				if((dx == 0 && dy == 0) || !cb->isInTheMap(next))
					continue;
				const size_t n = indexOf(next);
				// leaving an entrance and entering one: only through the sides it opens to
				if(!(entry[index] & dirBit(dx, dy)) || !(entry[n] & dirBit(-dx, -dy)))
					continue;
				relax(n, cost + stepCost[n] * ((dx != 0 && dy != 0) ? 1.41421356f : 1.f));
			}
		if(auto jump = jumps.find(index); jump != jumps.end())
			for(size_t exit : jump->second)
				relax(exit, cost + 1.f);
	}
}

bool AdventureAI::inTerritory(const int3 & tile) const
{
	auto towns = perception.ownTowns();
	if(towns.empty())
		return true;
	for(const auto * town : towns)
	{
		if(town->visitablePos().z == tile.z && town->visitablePos().dist2d(tile) <= params.collectorRadius)
			return true;
	}
	return false;
}

bool AdventureAI::enemySeenNear(const int3 & tile, int days, double radius) const
{
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type != Obj::HERO || !object.owner.isValidPlayer() || object.owner == me)
			continue;
		if(today - object.lastSeenDay > days)
			continue;
		if(cb->getPlayerRelations(me, object.owner) == PlayerRelations::ENEMIES
			&& enemyDistance(id, object.visitablePos, observedDanger(object) * heroFactor(object), tile, radius) <= radius)
			return true;
	}
	return false;
}

void AdventureAI::dumpKnowledge() const
{
	// what we know, as ASCII: ' ' unknown, '.' seen, objects by letter (debugging aid)
	const int3 size = cb->getMapSize();
	std::vector<std::string> rows(size.y, std::string(size.x, ' '));
	for(int y = 0; y < size.y; ++y)
		for(int x = 0; x < size.x; ++x)
			if(cb->isVisible(int3(x, y, 0)))
				rows[y][x] = '.';
	for(const auto & [id, o] : perception.knownObjects())
	{
		if(o.visitablePos.z != 0)
			continue;
		char c = 'o';
		switch(o.type.toEnum())
		{
		case Obj::MONSTER: c = 'M'; break;
		case Obj::TOWN: c = o.owner == me ? 'T' : (o.owner.isValidPlayer() ? 'E' : 't'); break;
		case Obj::HERO: c = o.owner == me ? 'H' : 'X'; break;
		case Obj::MINE: c = o.owner == me ? 'm' : 'n'; break;
		case Obj::MONOLITH_TWO_WAY:
		case Obj::MONOLITH_ONE_WAY_ENTRANCE:
		case Obj::MONOLITH_ONE_WAY_EXIT:
		case Obj::SUBTERRANEAN_GATE: c = usedTeleporters.count(id) ? 'u' : 'O'; break;
		case Obj::RESOURCE: c = 'r'; break;
		default: break;
		}
		rows[o.visitablePos.y][o.visitablePos.x] = c;
	}
	std::string out;
	for(const auto & r : rows)
		out += "|" + r + "|\n";
	logAi->info("FairAI %s knowledge on day %d (M monster, T/E/t town own/enemy/neutral, H/X hero, m/n mine, O/u monolith new/used):\n%s",
		me.toString(), today, out);
}

double AdventureAI::defenceWalls(const CGTownInstance * town) const
{
	if(town->fortLevel() >= CGTownInstance::CITADEL)
		return params.defenceWallsCitadel;
	return town->fortLevel() >= CGTownInstance::FORT ? params.defenceWallsFort : 1.0;
}

double AdventureAI::tolerance(const CGHeroInstance * hero) const
{
	// with the hero model strengths are comparable: an enemy as strong as we are is one to avoid
	if(params.heroModel)
		return leads(hero) ? params.heroTolerance : 0.9 * params.heroTolerance;
	return isMain(hero) ? params.threatTolerance : params.threatToleranceSecondary;
}

double AdventureAI::defenceAt(const CGHeroInstance * hero, const int3 & tile) const
{
	// in a Sanctuary nobody can attack: worth nothing while no enemy is near, the place to be when a
	// stronger hero would catch us tomorrow
	if(sanctuaries.count(tile))
		return 1e12;
	double strength = fieldStrength(hero);
	for(const auto * town : perception.ownTowns())
	{
		if(town->visitablePos() != tile)
			continue;
		double garrison = 0;
		for(const auto & slot : town->Slots())
			garrison += stackValue(slot.second.get());
		if(const auto * guard = town->getGarrisonHero(); guard && guard != hero)
		{
			if(params.nightWatch && guard->tempOwner == me && !hero->isGarrisoned())
			{
				// (nightWatch) Two heroes in one town: the one in the gate meets the attacker in the
				// open, alone. By nightfall the better commander is inside with the best troops of
				// both (arrangeTowns); the other one's troops are safe there, the hero itself is not.
				if(commanderFactor(hero) <= commanderFactor(guard) * 1.02)
					return strength;
				garrison += static_cast<double>(guard->getArmyStrength()) * commanderFactor(hero);
			}
			else
				garrison += static_cast<double>(guard->getArmyStrength());
		}
		strength = (strength + garrison) * defenceWalls(town);
	}
	return strength;
}

double AdventureAI::townNeed(const CGTownInstance * town, const CGHeroInstance * mainHero) const
{
	const double threat = threatAt(town->visitablePos());
	if(threat <= 0)
		return 0;
	double local = 0;
	for(const auto & slot : town->Slots())
		local += stackValue(slot.second.get());
	if(const auto * guard = town->getGarrisonHero(); guard && guard != mainHero)
		local += static_cast<double>(guard->getArmyStrength());
	if(const auto * visitor = town->getVisitingHero(); visitor && visitor != mainHero && visitor->tempOwner == me)
		local += static_cast<double>(visitor->getArmyStrength());
	// troops still for sale are bought when the enemy shows up (defendTowns)
	local += static_cast<double>(recruitableValue(town, spendable()[GameResID::GOLD]));
	local *= defenceWalls(town);
	if(threat <= local)
		return 0;
	return std::min(1.0, (threat - local) / threat);
}

double AdventureAI::siegeOverhead(const CGHeroInstance * hero, int fortLevel) const
{
	if(!params.siegeCost || fortLevel < CGTownInstance::CITADEL)
		return 0; // a fort has walls only: no moat, no arrow tower
	// moat: up to 70 damage to every stack that crosses (game config: moatDamage); arrow towers ignore
	// defence and shoot every round: keep 10+2L..2x, side towers 6+L..2x (L = buildings in town,
	// not visible on the map: assumed 10 for a citadel, 15 for a castle); about four rounds until the
	// walls fall and the fight is decided
	constexpr double moatDamage = 70;
	constexpr double rounds = 4;
	const double level = fortLevel >= CGTownInstance::CASTLE ? 15 : 10;
	double hpLoss = 1.5 * (10 + 2 * level) * rounds;
	if(fortLevel >= CGTownInstance::CASTLE)
		hpLoss += 2 * 1.5 * (6 + level) * rounds;
	double totalHp = 0;
	for(const auto & slot : hero->Slots())
	{
		const CStackInstance * stack = slot.second.get();
		if(!stack || !stack->getCreature())
			continue;
		const double stackHp = static_cast<double>(stack->getCount()) * stack->getCreature()->getMaxHealth();
		totalHp += stackHp;
		hpLoss += std::min(moatDamage, stackHp);
	}
	if(totalHp <= 0)
		return 0;
	return std::min(1.0, hpLoss / totalHp) * static_cast<double>(hero->getArmyStrength());
}

void AdventureAI::planSupplyRun()
{
	// Supply runs: what has grown in a town is brought to the main hero by a hero that stands in
	// that town, as soon as it is worth a share of the leader's army. The leader fights on instead
	// of walking home every week - in the logged games that walk took four days out of ten, and
	// weeks of growth waited in the towns meanwhile.
	supplyPossible = false;
	if(!params.supplyRuns)
		return;
	const CGHeroInstance * leader = ownHero(mainHero);
	if(supply)
	{
		const CGHeroInstance * courier = ownHero(supply->courier);
		if(!courier || !leader || courier->id == mainHero || today - supply->startDay > 2 * params.supplyDays + 3)
		{
			logAi->debug("FairAI %s day %d: supply run ended (%s)", me.toString(), today, !courier ? "courier lost" : "given up");
			supply.reset();
		}
		else if(const auto * town = courier->getVisitedTown(); town && !supply->returning && !courier->isGarrisoned())
		{
			// waiting in a town for a way to the leader: what grows meanwhile comes along
			const ObjectInstanceID courierId = courier->id;
			recruitInTown(town, courier, economy.rho > 1.5 ? 0 : 1000);
			if(const auto * now = ownHero(courierId))
				pickBestCreatures(now, town);
		}
	}
	if(supply || !leader)
		return;
	const double army = std::max(1.0, static_cast<double>(leader->getArmyStrength()));
	const CGTownInstance * bestTown = nullptr;
	const CGHeroInstance * bestCarrier = nullptr;
	double bestStock = 0;
	double allStock = 0;
	for(const auto * town : perception.ownTowns())
	{
		// a hero of ours in the gate carries; one in the garrison comes out when the gate is free
		const CGHeroInstance * carrier = town->getVisitingHero();
		if(carrier && (carrier->tempOwner != me || carrier->id == mainHero))
			continue; // the leader itself is here (or a guest): it takes what it needs
		if(!carrier)
			carrier = town->getGarrisonHero();
		if(carrier && (carrier->tempOwner != me || carrier->id == mainHero))
			continue;
		// (supplyRuns 2) nobody stands there: a hero of ours is called to the town, or one is hired
		if(!carrier && params.supplyRuns < 2)
			continue;
		if(carrier)
			supplyPossible = true;
		const double stock = townTroops(town, leader);
		allStock += stock;
		if(stock < std::max(carrier ? 1500.0 : 3000.0, params.supplyShare * army) || stock <= bestStock)
			continue;
		bestTown = town;
		bestCarrier = carrier;
		bestStock = stock;
	}
	if(!bestTown && allStock >= params.supplyShare * army && supplyLog != today)
	{
		logAi->debug("FairAI %s day %d: troops worth %.0f wait in our towns, in none of them enough for a run (the leader's army %.0f)", me.toString(), today, allStock, army);
		supplyLog = today;
	}
	if(!bestTown || leader->getVisitedTown() == bestTown)
		return;
	if(!bestCarrier)
	{
		// (supplyRuns 2) No hero of ours stands in the town. In probe games the troops of a week
		// waited there - the others were out collecting -, so no run started (three in 45 days) and
		// the leader walked home itself: nine days out of thirty, and the town it had just taken
		// fell behind its back. Only when the leader is near enough for a run and not next door (by
		// its own way to the town; the courier's way is checked when it has loaded).
		auto leaderPaths = computePaths(leader, false, params.supplyDays);
		const CGPathNode * back = leaderPaths->getPathInfo(bestTown->visitablePos());
		if(!back || !back->reachable() || back->turns < 1 || back->turns > params.supplyDays)
		{
			if(supplyLog != today && back && back->reachable() && back->turns >= 1)
				logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s, no hero of ours stands there and the leader is %d days away", me.toString(), today, bestStock,
					bestTown->getNameTranslated(), static_cast<int>(back->turns));
			else if(supplyLog != today && !(back && back->reachable()) && logAi->isDebugEnabled())
			{
				// (what bars the way? the same question with the guards left aside, and without a limit of days)
				auto freeWay = computePaths(leader, true, 60);
				const CGPathNode * open = freeWay->getPathInfo(bestTown->visitablePos());
				logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s (%s), no hero of ours stands there and the leader at %s has no way there (leaving guards aside: %s; %s)", me.toString(), today, bestStock,
					bestTown->getNameTranslated(), bestTown->visitablePos().toString(), leader->visitablePos().toString(),
					open && open->reachable() ? (boost::format("%d days") % static_cast<int>(open->turns)).str() : "no way either",
					open && open->reachable() ? stuckAt(*leaderPaths, bestTown->visitablePos(), freeWay.get()) : stuckAt(*freeWay, bestTown->visitablePos()));
			}
			supplyLog = today;
			return;
		}
		// (muster) A load that is a good part of what the leader leads, and that an enemy hero seen
		// lately could beat, does not spend a night on the road with a courier: the leader comes for
		// it. (Couriers were lost with 542,000 and 610,000 on two days of one game, and with 114,000
		// to 309,000 in three others - one in twelve runs is caught.)
		if(params.muster && bestStock >= params.musterShare * army && bestStock < oppositionHeroes)
		{
			if(supplyLog != today)
				logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s - too much to send by courier over %d night(s) (the leader leads %.0f, the strongest enemy hero seen lately counts %.0f)",
					me.toString(), today, bestStock, bestTown->getNameTranslated(), static_cast<int>(back->turns), army, oppositionHeroes);
			supplyLog = today;
			return;
		}
		// A hero of ours that reaches the town today or tomorrow is called there (a light one: not a
		// leader, not one that holds an army of its own) ...
		{
			const CGHeroInstance * runner = nullptr;
			double runnerCost = 1e9;
			for(const auto * candidate : perception.ownHeroes())
			{
				if(candidate->id == mainHero || leads(candidate) || candidate->isGarrisoned() || candidate->getVisitedTown() == bestTown
					|| static_cast<double>(candidate->getArmyStrength()) >= params.heavyPartValue)
					continue;
				auto way = computePaths(candidate, false);
				const CGPathNode * node = way->getPathInfo(bestTown->visitablePos());
				if(!node || !node->reachable() || node->turns > 1 || node->cost >= runnerCost)
					continue;
				runner = candidate;
				runnerCost = node->cost;
			}
			if(runner)
			{
				supply = SupplyRun{runner->id, bestTown->id, today, false, false, true};
				supplyPossible = true;
				if(statsFile)
					stats[phaseOf(today)]["courierCalled"] += 1;
				logAi->info("FairAI %s day %d: supply run - %s is called to %s (%.1f days) for the %.0f troops waiting there; the leader is %d day(s) away", me.toString(), today,
					runner->getNameTranslated(), bestTown->getNameTranslated(), runnerCost, bestStock, static_cast<int>(back->turns));
				return;
			}
		}
		// ... else a courier is hired in the tavern, for a load worth the 2,500 gold
		if(bestStock < 5000 || !bestTown->hasBuilt(BuildingID::TAVERN) || static_cast<int>(perception.ownHeroes().size()) >= params.maxHeroes
			|| spendable()[GameResID::GOLD] < GameConstants::HERO_GOLD_COST + params.heroGoldReserve)
		{
			if(supplyLog != today)
				logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s, nobody to carry them (%s)", me.toString(), today, bestStock, bestTown->getNameTranslated(),
					bestStock < 5000 ? "not worth a hired hero" : !bestTown->hasBuilt(BuildingID::TAVERN) ? "no tavern" :
					static_cast<int>(perception.ownHeroes().size()) >= params.maxHeroes ? "we have as many heroes as we may" : "no gold for a hero");
			supplyLog = today;
			return;
		}
		const CGHeroInstance * best = nullptr;
		for(const auto * candidate : cb->getAvailableHeroes(bestTown))
		{
			if(!best || candidate->getArmyStrength() > best->getArmyStrength())
				best = candidate;
		}
		if(!best)
			return;
		logAi->info("FairAI %s day %d: hiring %s (level %d) in %s as courier for the %.0f troops waiting there (the leader is %d day(s) away)", me.toString(), today,
			best->getNameTranslated(), static_cast<int>(best->level), bestTown->getNameTranslated(), bestStock, static_cast<int>(back->turns));
		cb->recruitHero(bestTown, best);
		gateway.waitTillFree();
		perception.refresh();
		leader = ownHero(mainHero);
		bestCarrier = bestTown->getVisitingHero();
		if(!leader || !bestCarrier || bestCarrier->tempOwner != me || bestCarrier->id == mainHero)
			return;
		if(statsFile)
			stats[phaseOf(today)]["courierHired"] += 1;
		supplyPossible = true;
	}
	const ObjectInstanceID carrierId = bestCarrier->id;
	if(bestCarrier->isGarrisoned())
	{
		if(bestTown->getVisitingHero())
			return;
		cb->swapGarrisonHero(bestTown);
		gateway.waitTillFree();
	}
	const CGHeroInstance * carrier = ownHero(carrierId);
	if(!carrier || carrier->isGarrisoned())
		return;
	// is the leader within reach, and the road safe for what the courier will carry?
	auto paths = computePaths(carrier, false, params.supplyDays);
	const CGPathNode * node = paths->getPathInfo(leader->visitablePos());
	// (safeParts) the courier must arrive the same day: nothing of weight spends a night on the road
	if(!node || !node->reachable() || node->turns > (params.safeParts ? 0 : params.supplyDays))
	{
		std::string why = node && node->reachable() ? (boost::format("%d days") % static_cast<int>(node->turns)).str() : "no way";
		if(!(node && node->reachable()) && logAi->isDebugEnabled())
		{
			// (what bars the way? the same question with the guards left aside, and without a limit of days)
			auto freeWay = computePaths(carrier, true, 60);
			const CGPathNode * open = freeWay->getPathInfo(leader->visitablePos());
			why += boost::str(boost::format("; from %s to %s; leaving guards aside: %s; %s") % carrier->visitablePos().toString() % leader->visitablePos().toString()
				% (open && open->reachable() ? (boost::format("%d days") % static_cast<int>(open->turns)).str() : "no way either")
				% (open && open->reachable() ? stuckAt(*paths, leader->visitablePos(), freeWay.get()) : stuckAt(*freeWay, leader->visitablePos())));
		}
		logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s, the leader is out of reach (%s)", me.toString(), today, bestStock,
			bestTown->getNameTranslated(), why);
		return;
	}
	const double carried = static_cast<double>(carrier->getArmyStrength()) + bestStock;
	// (muster) see above: such a load travels only when it arrives today
	if(params.muster && node->turns > 0 && bestStock >= params.musterShare * army && carried < oppositionHeroes)
	{
		logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s - too much to send by courier over %d night(s) (the leader leads %.0f, the strongest enemy hero seen lately counts %.0f)",
			me.toString(), today, bestStock, bestTown->getNameTranslated(), static_cast<int>(node->turns), army, oppositionHeroes);
		return;
	}
	if(threatAt(leader->visitablePos(), ObjectInstanceID(), true) > fieldStrength(leader) + carried
		|| supplyRoadThreat(node) > carried)
	{
		logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s, the road to the leader is not safe", me.toString(), today, bestStock,
			bestTown->getNameTranslated());
		return;
	}
	recruitInTown(bestTown, carrier, economy.rho > 1.5 ? 0 : 1000);
	carrier = ownHero(carrierId);
	if(!carrier)
		return;
	pickBestCreatures(carrier, bestTown);
	carrier = ownHero(carrierId);
	if(!carrier)
		return;
	supply = SupplyRun{carrierId, bestTown->id, today, false};
	if(statsFile)
		stats[phaseOf(today)]["supplyRun"] += 1;
	logAi->info("FairAI %s day %d: supply run - %s takes %llu from %s to %s (army %llu, %.1f days away)", me.toString(), today,
		carrier->getNameTranslated(), static_cast<unsigned long long>(carrier->getArmyStrength()), bestTown->getNameTranslated(),
		leader->getNameTranslated(), static_cast<unsigned long long>(leader->getArmyStrength()), node->cost);
}

double AdventureAI::supplyRoadThreat(const CGPathNode * node) const
{
	// the strongest known enemy that can reach a place where the courier ends a day
	double worst = 0;
	int turns = -1;
	for(const CGPathNode * n = node; n; n = n->theNodeBefore)
	{
		if(n->turns != turns)
		{
			turns = n->turns;
			worst = std::max(worst, threatAt(n->coord, ObjectInstanceID(), true));
		}
	}
	return worst;
}

std::vector<Target> AdventureAI::supplyTargets(const CGHeroInstance * hero, const CPathsInfo & paths)
{
	std::vector<Target> targets;
	if(!supply)
		return targets;
	const CGHeroInstance * leader = ownHero(mainHero);
	if(leader == hero)
		leader = nullptr; // no run to oneself (chooseMainHero ends such a run; this is the second lock)
	// can the hero still take a step towards `tile` today? (a path exists, and the next tile is within today's movement)
	auto stepToday = [&](const int3 & tile) -> int // 1 yes, 0 not today, -1 no way
	{
		CGPath path;
		if(!paths.getPath(path, tile) || path.nodes.size() < 2 || failedToday.count({hero->id.getNum(), tile}))
			return -1;
		return path.nodes[path.nodes.size() - 2].turns > 0 ? 0 : 1;
	};
	if(supply->loading)
	{
		// (supplyRuns 2) called to the town: walk there and load
		const CGTownInstance * town = nullptr;
		for(const auto * t : perception.ownTowns())
			if(t->id == supply->town)
				town = t;
		if(!town)
		{
			supply.reset(); // the town is lost
			return targets;
		}
		if(hero->getVisitedTown() != town)
		{
			const int step = stepToday(town->visitablePos());
			const CGPathNode * node = paths.getPathInfo(town->visitablePos());
			if(step < 0 || !node || (town->getVisitingHero() && town->getVisitingHero() != hero))
			{
				logAi->debug("FairAI %s day %d: supply run of %s given up (no way to %s, or another hero stands in its gate)", me.toString(), today,
					hero->getNameTranslated(), town->getNameTranslated());
				supply.reset();
				return targets;
			}
			if(step == 1)
			{
				Target t;
				t.tile = town->visitablePos();
				t.object = town->id;
				t.value = 100000;
				t.travel = node->cost;
				t.score = 100000;
				t.description = "load";
				targets.push_back(t);
			}
			return targets;
		}
		const ObjectInstanceID heroId = hero->id;
		recruitInTown(town, hero, economy.rho > 1.5 ? 0 : 1000);
		if(const auto * now = ownHero(heroId))
			pickBestCreatures(now, town);
		hero = ownHero(heroId);
		leader = ownHero(mainHero);
		if(!hero)
			return targets;
		if(leader == hero)
			leader = nullptr;
		supply->loading = false;
		logAi->info("FairAI %s day %d: supply run - %s takes %llu from %s to %s", me.toString(), today, hero->getNameTranslated(),
			static_cast<unsigned long long>(hero->getArmyStrength()), town->getNameTranslated(), leader ? leader->getNameTranslated() : std::string("?"));
	}
	if(leader && !supply->returning && leader->getVisitedTown() && leader->getVisitedTown() == hero->getVisitedTown())
	{
		// both in the same town, one in the gate and one in the garrison: hand over on the spot
		const ObjectInstanceID heroId = hero->id;
		pickBestCreatures(leader, hero);
		hero = ownHero(heroId);
		leader = ownHero(mainHero);
		if(!hero)
			return targets;
		supply->returning = true;
		logAi->debug("FairAI %s day %d: supply run of %s: handed over in town", me.toString(), today, hero->getNameTranslated());
	}
	if(!leader || transferGain(leader, hero) < 1)
	{
		if(!supply->returning)
			logAi->debug("FairAI %s day %d: supply run of %s: delivered", me.toString(), today, hero->getNameTranslated());
		supply->returning = true;
	}
	if(!supply->returning)
	{
		const CGPathNode * node = paths.getPathInfo(leader->visitablePos());
		int step = stepToday(leader->visitablePos());
		// (safeParts) the load does not spend a night on the road: the courier sets out only when it
		// arrives today, and waits in a town of ours otherwise
		if(params.safeParts && step >= 0 && node && node->turns > 0)
			step = -1;
		const bool unsafe = step >= 0 && node && supplyRoadThreat(node) > static_cast<double>(hero->getArmyStrength());
		if(step == 1 && !unsafe)
		{
			Target t;
			t.tile = leader->visitablePos();
			t.object = leader->id;
			t.value = 100000;
			t.travel = node->cost;
			t.score = 100000;
			t.description = "supply";
			targets.push_back(t);
			supply->waiting = false;
			return targets;
		}
		if(step == 0 && !unsafe)
			return targets; // on the way: tomorrow
		supply->waiting = true;
		// no way, or enemies about: wait in a town of ours (below) and try again tomorrow
		if(supplyLog != today)
			logAi->debug("FairAI %s day %d: supply run of %s at %s waits (%s; leader at %s%s%s, node %s accessible %d action %d turns %d)", me.toString(), today,
				hero->getNameTranslated(), hero->visitablePos().toString(), unsafe ? "the road is not safe" : "no way to the leader",
				leader->visitablePos().toString(), leader->getVisitedTown() ? " in town" : "", leader->isGarrisoned() ? " garrisoned" : "",
				node && node->reachable() ? "reachable" : "unreachable", node ? static_cast<int>(node->accessible) : -1,
				node ? static_cast<int>(node->action) : -1, node ? static_cast<int>(node->turns) : -1);
		supplyLog = today;
	}
	// to a town of ours: the one it came from, else the nearest
	if(hero->getVisitedTown())
	{
		if(supply->returning)
			supply.reset(); // done; with troops still to deliver it waits here for a way
		return targets;
	}
	const CGTownInstance * home = nullptr;
	double homeCost = 1e9;
	double realCost = 0;
	for(const auto * town : perception.ownTowns())
	{
		if(town->getVisitingHero())
			continue;
		const CGPathNode * node = paths.getPathInfo(town->visitablePos());
		if(!node || !node->reachable() || stepToday(town->visitablePos()) < 0)
			continue;
		const double cost = node->cost - (town->id == supply->town ? 0.5 : 0.0);
		if(cost < homeCost)
		{
			home = town;
			homeCost = cost;
			realCost = node->cost;
		}
	}
	if(!home)
	{
		if(supply->returning)
			supply.reset(); // nowhere to go: it plays as any other hero from tomorrow
		return targets;
	}
	if(stepToday(home->visitablePos()) != 1)
		return targets;
	Target t;
	t.tile = home->visitablePos();
	t.object = home->id;
	t.value = 1;
	t.travel = realCost;
	t.score = 1;
	t.description = "home";
	targets.push_back(t);
	return targets;
}

void AdventureAI::clearGate(const CGTownInstance * town, const CGHeroInstance * mainHero)
{
	const CGHeroInstance * visitor = town->getVisitingHero();
	if(!visitor || visitor == mainHero || visitor->tempOwner != me)
		return;
	if(!town->getGarrisonHero() && visitor->canBeMergedWith(*town))
	{
		logAi->debug("FairAI %s: %s steps into the garrison of %s to let %s in", me.toString(), visitor->getNameTranslated(),
			town->getNameTranslated(), mainHero->getNameTranslated());
		cb->swapGarrisonHero(town);
		gateway.waitTillFree();
		return;
	}
	// The garrison is taken by another hero of ours, or its troops and the visitor's do not fit into
	// one army: the visitor leaves the gate by the nearest free tile instead - once the main hero is
	// about to arrive. (With both places taken the main hero could never enter: it kept walking up
	// to the gate for the troops inside, day after day.)
	const int3 gate = town->visitablePos();
	if(mainHero->visitablePos().z != gate.z || mainHero->visitablePos().dist2d(gate) > 10)
		return;
	leaveGate(town, mainHero->visitablePos(), mainHero->getNameTranslated());
}

bool AdventureAI::leaveGate(const CGTownInstance * town, const int3 & keepFree, const std::string & forWhom)
{
	// our hero in the gate steps onto the nearest free tile next to it (it needs movement points for that)
	const CGHeroInstance * visitor = town->getVisitingHero();
	if(!visitor || visitor->tempOwner != me)
		return false;
	const int3 gate = town->visitablePos();
	auto paths = computePaths(visitor, false);
	int3 best(-1);
	float bestCost = std::numeric_limits<float>::max();
	for(int dx = -1; dx <= 1; ++dx)
	{
		for(int dy = -1; dy <= 1; ++dy)
		{
			const int3 tile(gate.x + dx, gate.y + dy, gate.z);
			if(tile == gate || tile == keepFree || !cb->isInTheMap(tile))
				continue;
			const CGPathNode * node = paths->getPathInfo(tile);
			if(node && node->reachable() && node->turns == 0 && node->action == EPathNodeAction::NORMAL && node->cost < bestCost)
			{
				best = tile;
				bestCost = node->cost;
			}
		}
	}
	if(!best.isValid())
		return false;
	logAi->debug("FairAI %s: %s leaves the gate of %s to let %s in", me.toString(), visitor->getNameTranslated(),
		town->getNameTranslated(), forWhom);
	cb->moveHero(visitor, visitor->convertFromVisitablePos(best), false);
	gateway.waitTillFree();
	return !town->getVisitingHero();
}

bool AdventureAI::wouldLead(const CGHeroInstance * candidate) const
{
	// clearly the more experienced leader: five levels or more above the hero that leads now, and
	// worth a tenth more to an army (a hero of level 20 is not replaced by one of level 24: the
	// walk to hand over the army costs more than the difference brings)
	const CGHeroInstance * current = ownHero(mainHero);
	if(!current)
		return true;
	if(current == candidate)
		return false;
	return candidate->level >= current->level + 5 && commanderFactor(candidate) >= 1.1 * commanderFactor(current);
}

void AdventureAI::takeCommand(const CGHeroInstance * veteran, const CGTownInstance * town)
{
	// (veterans 2) An experienced hero is back - as a rule our own main hero, which fled from a lost
	// battle and waits in the tavern with its levels, skills, spells and artifacts until the week
	// ends. Until now nobody hired it: the troops at home went to whoever held most of them, a hero
	// of level 1 (in one game such a hero lost 173,000 to an army of 222,000 that lost 4,000; its
	// level 12 predecessor sat in the tavern). The veteran leads again if it is clearly the more
	// experienced: it takes the troops that stand in this town and those of the hero that led until
	// now if that one is here, and for a week the lead is its own - whoever holds troops elsewhere
	// brings them, as heroes do for the main hero.
	if(!wouldLead(veteran))
		return;
	const CGHeroInstance * old = ownHero(mainHero);
	const ObjectInstanceID id = veteran->id;
	const ObjectInstanceID oldId = old ? old->id : ObjectInstanceID();
	const std::string oldName = old ? old->getNameTranslated() + " (level " + std::to_string(old->level) + ")" : std::string("nobody");
	const double before = static_cast<double>(veteran->getArmyStrength());
	mainHero = id;
	veteranMain = id;
	veteranUntil = today + 7;
	const CGHeroInstance * guard = town->getGarrisonHero();
	if(guard && guard != veteran && guard->tempOwner == me && (guard == old || !params.townHeroes))
	{
		// (a town's hero keeps the town's troops - townHeroes -, the leader it replaces hands over)
		pickBestCreatures(veteran, guard);
		if(const auto * now = ownHero(id))
			exchangeArtifacts(now, town->getGarrisonHero());
	}
	else if(!guard)
		pickBestCreatures(veteran, town);
	// the hero that led until now has just stepped out of the gate: it walks back in and hands over
	old = ownHero(oldId);
	if(old && !old->isGarrisoned() && old->visitablePos() != town->visitablePos() && old->visitablePos().z == town->visitablePos().z
		&& old->visitablePos().dist2d(town->visitablePos()) < 2 && old->movementPointsRemaining() > 0)
	{
		cb->moveHero(old, old->convertFromVisitablePos(town->visitablePos()), false);
		gateway.waitTillFree();
	}
	const CGHeroInstance * now = ownHero(id);
	if(!now)
	{
		mainHero = oldId;
		return;
	}
	if(statsFile)
		stats[phaseOf(today)]["veteranBack"] += 1;
	logAi->info("FairAI %s day %d: %s (level %d) is back and takes command in %s: army %.0f -> %.0f; the lead was with %s", me.toString(), today, now->getNameTranslated(),
		static_cast<int>(now->level), town->getNameTranslated(), before, static_cast<double>(now->getArmyStrength()), oldName);
}

double AdventureAI::commanderFactor(const CGHeroInstance * hero) const
{
	if(!hero)
		return 1.0;
	return fieldStrength(hero) / std::max(1.0, static_cast<double>(hero->getArmyStrength()));
}

namespace
{
std::map<CreatureID, int> armyKinds(const CArmedInstance * army)
{
	std::map<CreatureID, int> kinds;
	for(const auto & [slot, stack] : army->Slots())
	{
		if(stack && stack->getCreature())
			kinds[stack->getCreatureID()] += stack->getCount();
	}
	return kinds;
}
}

void AdventureAI::arrangeTowns()
{
	if(!params.nightWatch)
		return;
	for(const auto * town : perception.ownTowns())
	{
		const CGHeroInstance * visitor = town->getVisitingHero();
		const CGHeroInstance * guard = town->getGarrisonHero();
		if(!visitor || !guard || visitor->tempOwner != me || guard->tempOwner != me)
			continue;
		// Whoever stands in the gate meets the attacker first - in the open field and alone (the
		// game's rule for a town that holds a hero in the garrison and one in the gate). A hero that
		// beats whatever can come is the best gate there is; anything less belongs behind the walls.
		const double threat = threatAt(town->visitablePos());
		if(threat <= 0 || fieldStrength(visitor) >= params.heroEdge * threat)
			continue;
		const bool visitorCommands = commanderFactor(visitor) > commanderFactor(guard) * 1.02;
		const ObjectInstanceID insideId = visitorCommands ? visitor->id : guard->id;
		const ObjectInstanceID outsideId = visitorCommands ? guard->id : visitor->id;
		NightLoan loan;
		loan.inside = insideId;
		loan.outside = outsideId;
		loan.insideHad = armyKinds(visitorCommands ? visitor : guard);
		loan.outsideHad = armyKinds(visitorCommands ? guard : visitor);
		const double before = static_cast<double>((visitorCommands ? visitor : guard)->getArmyStrength());
		const double gateAlone = fieldStrength(visitor);
		if(visitorCommands)
		{
			cb->swapGarrisonHero(town);
			gateway.waitTillFree();
		}
		const CGHeroInstance * inside = ownHero(insideId);
		const CGHeroInstance * outside = ownHero(outsideId);
		if(!inside || !outside || town->getGarrisonHero() != inside || town->getVisitingHero() != outside)
			continue; // the swap was refused: nothing pooled, nothing to undo
		pickBestCreatures(inside, outside);
		inside = ownHero(insideId);
		outside = ownHero(outsideId);
		if(!inside || !outside)
			continue;
		nightLoans.push_back(loan);
		if(statsFile)
			stats[phaseOf(today)]["nightWatch"] += 1;
		logAi->info("FairAI %s day %d: night watch in %s (up to %.0f could come, the gate alone held %.0f) - %s commands inside the walls with %.0f (had %.0f), %s waits in the gate with %.0f",
			me.toString(), today, town->getNameTranslated(), threat, gateAlone, inside->getNameTranslated(),
			static_cast<double>(inside->getArmyStrength()), before, outside->getNameTranslated(), static_cast<double>(outside->getArmyStrength()));
	}
}

void AdventureAI::restoreNightWatch()
{
	// What was pooled for the night goes back: every hero gets the kinds and numbers it had (less
	// what fell, should the town have been attacked). A hero that fell leaves its share with the other.
	for(const auto & loan : nightLoans)
	{
		const CGHeroInstance * inside = ownHero(loan.inside);
		const CGHeroInstance * outside = ownHero(loan.outside);
		if(!inside || !outside || !inside->getVisitedTown() || inside->getVisitedTown() != outside->getVisitedTown())
			continue;
		struct Party
		{
			const CGHeroInstance * hero;
			const std::map<CreatureID, int> * had;
		};
		const std::array<Party, 2> parties = {Party{inside, &loan.insideHad}, Party{outside, &loan.outsideHad}};
		auto surplus = [](const Party & party, const CreatureID & kind)
		{
			int have = 0;
			for(const auto & [slot, stack] : party.hero->Slots())
				have += stack && stack->getCreature() && stack->getCreatureID() == kind ? stack->getCount() : 0;
			auto it = party.had->find(kind);
			return have - (it != party.had->end() ? it->second : 0);
		};
		for(int guard = 0; guard < 40; ++guard)
		{
			gateway.checkInterruption();
			bool acted = false;
			// a kind one hero holds more of than it owned goes to the other: into its stack of that kind or a free slot
			for(int side = 0; side < 2 && !acted; ++side)
			{
				const Party & from = parties[side];
				const Party & to = parties[1 - side];
				for(const auto & [slot, stack] : from.hero->Slots())
				{
					if(!stack || !stack->getCreature())
						continue;
					int move = std::min<int>(surplus(from, stack->getCreatureID()), stack->getCount());
					if(from.hero->stacksCount() == 1 && move >= stack->getCount())
						move = stack->getCount() - 1; // a hero keeps one creature
					if(move <= 0)
						continue;
					const SlotID target = to.hero->getSlotFor(stack->getCreatureID());
					if(!target.validSlot())
						continue;
					if(move >= stack->getCount())
						cb->mergeOrSwapStacks(from.hero, to.hero, slot, target);
					else
						cb->splitStack(from.hero, to.hero, slot, target, to.hero->getStackCount(target) + move);
					acted = true;
					break;
				}
			}
			if(acted)
				continue;
			// both armies full of the other's kinds: two whole stacks in the wrong army change places
			SlotID wrong[2];
			for(int side = 0; side < 2; ++side)
			{
				for(const auto & [slot, stack] : parties[side].hero->Slots())
				{
					if(stack && stack->getCreature() && surplus(parties[side], stack->getCreatureID()) >= stack->getCount())
					{
						wrong[side] = slot;
						break;
					}
				}
			}
			if(!wrong[0].validSlot() || !wrong[1].validSlot())
				break;
			cb->swapCreatures(inside, outside, wrong[0], wrong[1]);
		}
		logAi->debug("FairAI %s day %d: after the night watch %s has %.0f again, %s %.0f", me.toString(), today, inside->getNameTranslated(),
			static_cast<double>(inside->getArmyStrength()), outside->getNameTranslated(), static_cast<double>(outside->getArmyStrength()));
	}
	nightLoans.clear();
}

double AdventureAI::townTroops(const CGTownInstance * town, const CGHeroInstance * hero) const
{
	// what the hero's army can really take (free slots, same kinds, clearly better stacks)
	double troops = 0;
	const auto * guard = town->getGarrisonHero();
	if(!guard || guard == hero)
		troops += hero ? transferGain(hero, town) : 0.0;
	// troops recruited while a hero guards the garrison sit in that hero's army
	if(guard && guard != hero && guard->tempOwner == me)
		troops += hero ? transferGain(hero, guard) : transferableValue(guard);
	troops += recruitableValue(town, spendable()[GameResID::GOLD]);
	return troops;
}

double AdventureAI::transferableValue(const CGHeroInstance * from) const
{
	double total = 0;
	double weakest = std::numeric_limits<double>::max();
	for(const auto & slot : from->Slots())
	{
		double v = stackValue(slot.second.get());
		total += v;
		if(slot.second && slot.second->getCreature())
			weakest = std::min(weakest, static_cast<double>(slot.second->getCreature()->getAIValue()));
	}
	if(weakest == std::numeric_limits<double>::max())
		return 0;
	return std::max(0.0, total - weakest);
}

double AdventureAI::transferGain(const CArmedInstance * destination, const CArmedInstance * source) const
{
	// What pickBestCreatures(destination, source) would really move, in army value: stacks that join
	// one of the same kind, fill a free slot, or are clearly better than the destination's weakest
	// stack. Troops that cannot be taken are no reason to walk anywhere - valuing them kept two
	// heroes bumping into each other for weeks.
	struct Stack
	{
		CreatureID type;
		double value;
		int count;
		double unit;
	};
	auto list = [](const CArmedInstance * army)
	{
		std::vector<Stack> stacks;
		for(const auto & [slot, stack] : army->Slots())
		{
			if(stack && stack->getCreature() && stack->getCount() > 0)
				stacks.push_back({stack->getCreatureID(), stackValue(stack.get()), static_cast<int>(stack->getCount()), static_cast<double>(stack->getCreature()->getAIValue())});
		}
		return stacks;
	};
	if(!destination || !source || destination == source)
		return 0;
	std::vector<Stack> dest = list(destination);
	std::vector<Stack> src = list(source);
	const bool sourceIsHero = source->ID == Obj::HERO;
	if(!sourceIsHero && params.keepGarrison && destination->ID == Obj::HERO && !src.empty())
	{
		// a token guard stays in the town: a third of a single stack, else the weakest stack
		if(src.size() == 1)
		{
			const int keep = std::max(1, src.front().count / 3);
			const int move = src.front().count - keep;
			const bool fits = dest.size() < GameConstants::ARMY_SIZE
				|| std::any_of(dest.begin(), dest.end(), [&](const Stack & d) { return d.type == src.front().type; });
			return fits ? move * src.front().unit : 0.0;
		}
		src.erase(std::min_element(src.begin(), src.end(), [](const Stack & a, const Stack & b) { return a.value < b.value; }));
	}

	// (armyFit) which stacks, by what they do to the army as it fights - exactly as pickBestCreatures decides
	const bool fit = params.armyFit && destination->ID == Obj::HERO;
	const int shared = fit ? sharedMorale(dynamic_cast<const CGHeroInstance *>(destination)) : 0;
	double gained = 0;
	for(int guard = 0; guard < 32 && !src.empty(); ++guard)
	{
		const bool lastOfHero = sourceIsHero && src.size() == 1; // a hero keeps one creature
		// 1. join a stack of the same kind
		bool joined = false;
		for(size_t i = 0; i < src.size(); ++i)
		{
			auto same = std::find_if(dest.begin(), dest.end(), [&](const Stack & d) { return d.type == src[i].type; });
			if(same == dest.end())
				continue;
			if(lastOfHero)
				return gained + std::max(0, src[i].count - 1) * src[i].unit;
			same->value += src[i].value;
			gained += src[i].value;
			src.erase(src.begin() + i);
			joined = true;
			break;
		}
		if(joined)
			continue;
		if(fit)
		{
			std::vector<FitStack> have;
			std::vector<FitStack> offer;
			for(size_t i = 0; i < dest.size(); ++i)
				have.push_back({dest[i].type, dest[i].value, static_cast<int>(i)});
			for(size_t i = 0; i < src.size(); ++i)
				offer.push_back({src[i].type, src[i].value, static_cast<int>(i)});
			const bool full = dest.size() >= GameConstants::ARMY_SIZE;
			FitMove move;
			if(!fitMove(shared, have, offer, full, move))
				return gained;
			if(!full)
			{
				if(lastOfHero)
					return gained + move.gain * std::max(0, src[move.take].count - 1) / std::max(1, src[move.take].count);
				gained += move.gain;
				dest.push_back(src[move.take]);
				src.erase(src.begin() + move.take);
				continue;
			}
			if(lastOfHero)
				return gained;
			gained += move.gain;
			std::swap(src[move.take], dest[move.drop]);
			continue;
		}
		// 2. the strongest remaining stack into a free slot
		auto best = std::max_element(src.begin(), src.end(), [](const Stack & a, const Stack & b) { return a.value < b.value; });
		if(dest.size() < GameConstants::ARMY_SIZE)
		{
			if(lastOfHero)
				return gained + std::max(0, best->count - 1) * best->unit;
			gained += best->value;
			dest.push_back(*best);
			src.erase(best);
			continue;
		}
		// 3. full: swap with the weakest stack if clearly better
		auto weak = std::min_element(dest.begin(), dest.end(), [](const Stack & a, const Stack & b) { return a.value < b.value; });
		if(lastOfHero || best->value < 1.1 * weak->value)
			return gained;
		gained += best->value - weak->value;
		std::swap(*best, *weak);
	}
	return gained;
}

double AdventureAI::decisiveBoost(double troops) const
{
	const CGHeroInstance * leader = ownHero(mainHero);
	if(!params.gather || !leader || oppositionHeroes <= 0 || troops <= 0)
		return 0;
	const double own = fieldStrength(leader);
	const double now = own / oppositionHeroes;
	const double then = (own + troops * commanderFactor(leader)) / oppositionHeroes;
	const double edge = heroEdgeNow();
	if(now >= 0.5 && now < edge)
		return 1.0 + 2.0 * std::clamp((then - now) / (edge - now), 0.0, 1.0);
	return 0;
}

double AdventureAI::reinforcementBoost(double troops) const
{
	// Troops in a garrison win nothing. For the main hero a reinforcement that is big compared with
	// its army is worth more than its size: guards it had to leave alone come into reach.
	const CGHeroInstance * leader = ownHero(mainHero);
	if(!leader)
		return 1.0;
	const double army = std::max(1.0, static_cast<double>(leader->getArmyStrength()));
	// (gather) Troops that turn a battle we would not take into one we would - against the strongest
	// enemy hero seen in the last three weeks - are worth more than their size: they are what the
	// decisive battle waits for. Counted while our leader is within reach of the edge it asks for
	// (from half the enemy's strength on): three times their value when they carry it over the edge,
	// in between by the share of the gap they close.
	const double decisive = decisiveBoost(troops);
	if(params.fetchByShare)
	{
		// Troops waiting in a town do not run away: fetching them only brings their use forward. A
		// trickle (a few per cent of the army) is not worth a trip - it would keep the hero circling
		// its town; a week's growth that makes the army half as big again is.
		return std::max(decisive, std::clamp(troops / (params.fetchShare * army), 0.1, 1.5));
	}
	if(!params.couriers)
		return std::max(decisive, 1.0);
	return std::max(decisive, 1.0 + std::min(2.0, troops / army));
}

double AdventureAI::objectValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const
{
	// (a second leader counts skill points, artifacts and the like as the main hero does; towns and
	// heroes of ours have rules for it below)
	const bool main = leads(hero);
	const bool second = main && !isMain(hero);
	const bool own = object.owner == me;
	const bool enemy = isEnemy(object.owner);
	// towns, heroes, mines and dwellings of team mates are neither targets nor ours to take
	if(isAlly(object.owner))
		return 0;
	auto visitedByHero = [&]() { return instance && instance->wasVisited(hero); };
	auto visitedByUs = [&]() { return instance && instance->wasVisited(me); };
	const double primary = main ? params.primarySkillValueMain : params.primarySkillValueOther;
	const double goldFactor = 0.4 + 0.6 * economy.goldWeight;   // gold matters less when it piles up
	const double armyFactor = std::sqrt(economy.armyPremium);   // troops matter more when they are scarce

	switch(object.type.toEnum())
	{
	case Obj::RESOURCE:
	{
		GameResID res(object.subtype.getNum());
		if(!res.hasValue() || res.getNum() >= GameResID::COUNT)
			return 0;
		double amount = res == GameResID::GOLD ? 750 : ((res == GameResID::WOOD || res == GameResID::ORE) ? 7.5 : 4.5);
		double need = 1.0;
		auto wanted = resourceNeed.find(res.getNum());
		if(wanted != resourceNeed.end())
			need += params.resourceNeedFactor * std::min(1.0, wanted->second / amount);
		if(params.resourcePlan && res != GameResID::GOLD)
			need = std::max(need, std::clamp(scarcity(res), 0.7, 3.0));
		return amount * resourceValue(res) * (res == GameResID::GOLD ? goldFactor : need);
	}
	case Obj::CAMPFIRE:
		return 500 + 5 * resourceValue(GameResID::MERCURY);
	case Obj::FLOTSAM:
		return 400;
	case Obj::SEA_CHEST:
		return 900;
	case Obj::SHIPWRECK_SURVIVOR:
		return 1500;
	case Obj::TREASURE_CHEST:
		return main ? 1500 : 1300;
	case Obj::SCHOLAR:
		return main ? 1000 : 400;
	case Obj::PRISON:
		return 5000;
	case Obj::PANDORAS_BOX:
		return 2500;
	case Obj::LEAN_TO:
	case Obj::WAGON:
		return visitedByUs() ? 0 : 600; // they stay on the map when emptied
	case Obj::CORPSE:
		return visitedByUs() ? 0 : 400;
	case Obj::WARRIORS_TOMB:
		return visitedByUs() ? 0 : 900;
	case Obj::SPELL_SCROLL:
		return hero->hasSpellbook() ? 800 : 300;
	case Obj::ARTIFACT:
	{
		ArtifactID art(object.subtype.getNum());
		if(art == ArtifactID::SPELL_SCROLL)
			return hero->hasSpellbook() ? 800 : 300;
		if(!art.hasValue())
			return 1000;
		// (sites) somebody in our quest log asks for exactly this artifact
		const double wished = questArtifacts.count(art) ? 3000.0 : 0.0;
		if(params.artifactCare)
		{
			// what it would do for the hero leading our army; the others fetch it for that hero.
			// Even an artifact that only does harm is worth picking up: the enemy shall not have it.
			const CGHeroInstance * leader = ownHero(mainHero);
			const double forLeader = artifactValue(leader ? leader : hero, art.toArtifact(), true);
			const double forMe = main ? forLeader : std::max(artifactValue(hero, art.toArtifact(), false), 0.8 * forLeader);
			return wished + std::clamp(std::max(forMe, 0.1 * art.toArtifact()->getPrice()), 300.0, 20000.0);
		}
		double value = 1000;
		switch(art.toArtifact()->aClass)
		{
		case EArtifactClass::ART_TREASURE: value = 1200; break;
		case EArtifactClass::ART_MINOR: value = 2500; break;
		case EArtifactClass::ART_MAJOR: value = 5000; break;
		case EArtifactClass::ART_RELIC: value = 9000; break;
		default: break;
		}
		return wished + (main ? value : 0.8 * value);
	}
	case Obj::MINE:
	{
		if(own)
			return 0;
		GameResID res(object.subtype.getNum());
		if(!res.hasValue() || res.getNum() >= GameResID::COUNT)
			return 0;
		double perDay = res == GameResID::GOLD ? 1000 : ((res == GameResID::WOOD || res == GameResID::ORE) ? 2 : 1);
		double value = perDay * resourceValue(res) * params.incomeHorizonDays * (res == GameResID::GOLD ? goldFactor : 1.0);
		if(params.resourcePlan && res != GameResID::GOLD)
			value *= std::clamp(scarcity(res), 0.6, 3.5); // what our towns will need, not only what blocks us today
		else if(resourceNeed.count(res.getNum()))
			value *= 1.0 + 0.5 * params.resourceNeedFactor;
		return enemy ? 1.5 * value : value;
	}
	case Obj::CREATURE_GENERATOR1:
	case Obj::CREATURE_GENERATOR2:
	case Obj::CREATURE_GENERATOR3:
	case Obj::CREATURE_GENERATOR4:
	{
		if(own)
			return 0;
		double value = 800;
		if(const auto * dwelling = dynamic_cast<const CGDwelling *>(instance))
		{
			// the creature type is visible on the map
			for(const auto & level : dwelling->creatures)
			{
				if(level.second.empty())
					continue;
				const CCreature * creature = level.second.back().toCreature();
				// (armyFit) what its creatures would really add to the army they are meant for
				const CGHeroInstance * leader = ownHero(mainHero);
				const double share = params.armyFit
					? std::clamp(fitShare(leader ? leader : hero, creature->getId(), 4.0 * creature->getGrowth() * creature->getAIValue()), 0.0, 1.0) : 1.0;
				value += share * creature->getGrowth() * creature->getAIValue() * params.goldPerArmyValue * 0.5 * params.dwellingHorizonWeeks * armyFactor;
			}
		}
		return enemy ? 1.3 * value : value;
	}
	case Obj::WINDMILL:
		return visitedByUs() ? 0 : 4.5 * resourceValue(GameResID::MERCURY);
	case Obj::WATER_WHEEL:
		return visitedByUs() ? 0 : 750;
	case Obj::MYSTICAL_GARDEN:
		return visitedByUs() ? 0 : 500;
	case Obj::LEARNING_STONE:
		return visitedByHero() ? 0 : 1000 * expValue(hero);
	case Obj::TREE_OF_KNOWLEDGE:
		return visitedByHero() ? 0 : (main ? 1500 : 300);
	case Obj::MARLETTO_TOWER:
		return visitedByHero() ? 0 : primaryPointValue(hero, 1, main);
	case Obj::MERCENARY_CAMP:
		return visitedByHero() ? 0 : primaryPointValue(hero, 0, main);
	case Obj::GARDEN_OF_REVELATION:
	case Obj::STAR_AXIS:
		return visitedByHero() ? 0 : primary;
	case Obj::ARENA:
		return visitedByHero() ? 0 : 2 * primaryPointValue(hero, 0, main);
	case Obj::SCHOOL_OF_MAGIC:
	case Obj::SCHOOL_OF_WAR:
		if(visitedByHero() || !main || spendable()[GameResID::GOLD] < 3000)
			return 0;
		return (object.type == Obj::SCHOOL_OF_WAR ? primaryPointValue(hero, 0, main) + primary : 2 * primary) - 1000;
	case Obj::WITCH_HUT:
		return visitedByHero() ? 0 : (main ? 1000 : 300);
	case Obj::SHRINE_OF_MAGIC_INCANTATION:
		return (!hero->hasSpellbook() || visitedByHero()) ? 0 : 400;
	case Obj::SHRINE_OF_MAGIC_GESTURE:
		return (!hero->hasSpellbook() || visitedByHero()) ? 0 : 700;
	case Obj::SHRINE_OF_MAGIC_THOUGHT:
		return (!hero->hasSpellbook() || visitedByHero()) ? 0 : 900;
	case Obj::STABLES:
		return visitedByHero() ? 0 : 400;
	case Obj::SWAN_POND:
		// two points of luck for the next battle - by what they add to this army (nothing if luck
		// stands at +3 already); the movement the visit takes is counted as time on the way
		if(!main || visitedByHero())
			return 0;
		return object.table ? std::max(0.0, tableValue(hero, *object.table, nullptr, object.visitablePos)) : 150.0;
	case Obj::FOUNTAIN_OF_FORTUNE:
	case Obj::FAERIE_RING:
	case Obj::IDOL_OF_FORTUNE:
	case Obj::TEMPLE:
	case Obj::RALLY_FLAG:
	case Obj::OASIS:
	case Obj::WATERING_HOLE:
	case Obj::FOUNTAIN_OF_YOUTH:
		if(visitedByHero())
			return 0;
		// (tables) by what the building gives this hero now: luck and morale by its army, movement by what is missing
		if(params.tables && object.table)
			return std::max(0.0, tableValue(hero, *object.table, nullptr, object.visitablePos));
		return main ? 150 : 0;
	case Obj::MONOLITH_TWO_WAY:
	case Obj::MONOLITH_ONE_WAY_ENTRANCE:
	case Obj::SUBTERRANEAN_GATE:
		// stepping in reveals a new region; afterwards the pathfinder knows the way
		if(usedTeleporters.count(object.id))
			return 0;
		return 40 * params.explorationTileValue * std::max(0.5, 1.0 - today / 56.0);
	case Obj::TOWN:
	{
		if(own)
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(instance);
			if(!town)
				return 0;
			if(second)
			{
				// (secondArmy) more troops for the second army - what the main hero leaves in our towns
				// and what has grown since; a trickle is not worth the way
				const CGHeroInstance * leader = ownHero(mainHero);
				if(leader && leader->getVisitedTown() == town)
					return 0;
				const double troops = townTroops(town, hero);
				if(troops < std::max(800.0, 0.1 * static_cast<double>(hero->getArmyStrength())))
					return 0;
				return troops * params.goldPerArmyValue * params.reinforceFactor * armyFactor;
			}
			if(!main)
			{
				// couriers: troops waiting in a town are worth bringing to the main hero
				const CGHeroInstance * leader = ownHero(mainHero);
				if(!params.couriers || !leader || leader->getVisitedTown() == town)
					return 0;
				double waiting = 0;
				double weakest = std::numeric_limits<double>::max();
				for(const auto & slot : town->Slots())
				{
					const double v = stackValue(slot.second.get());
					waiting += v;
					weakest = std::min(weakest, v);
				}
				if(params.keepGarrison && weakest < std::numeric_limits<double>::max())
					waiting -= weakest; // stays behind as a token guard
				waiting += static_cast<double>(recruitableValue(town, spendable()[GameResID::GOLD]));
				const double leaderArmy = static_cast<double>(leader->getArmyStrength());
				if(waiting < std::max(800.0, 0.1 * leaderArmy))
					return 0;
				return waiting * params.goldPerArmyValue * params.ferryFactor * armyFactor * reinforcementBoost(waiting);
			}
			double troops = townTroops(town, hero);
			// what the hero left behind on its last visit (no free slot, garrison share) is no reason to return
			if(auto left = townLeftover.find(town->id); left != townLeftover.end() && params.antiOscillation)
				troops = std::max(0.0, troops - left->second);
			double value = troops * params.goldPerArmyValue * params.reinforceFactor * armyFactor * reinforcementBoost(troops) * fetchFactor(hero);
			// (muster) the stock that calls the main hero home, however far: it counts in full and several times over
			if(town->id == musterTown)
				value = troops * params.goldPerArmyValue * params.reinforceFactor * armyFactor * std::max(reinforcementBoost(troops), params.musterBoost);
			if(!hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1))
				value += 300;
			// (magicFocus) spells waiting in the guild for a hero that can learn them
			if(params.magicFocus)
				value += 600.0 * unlearnedGuildLevels(hero, town);
			auto trip = roundTrip.find(hero->id);
			const bool onTrip = trip != roundTrip.end() && trip->second.town == town->id;
			if(params.townGuard && !onTrip)
			{
				// the town cannot hold on its own against what can reach it: the main hero should stand guard
				value += (params.townKeep ? 2.0 * params.enemyTownValue * (1.0 + std::min(today, 140) / 28.0) : params.townDefenceValue) * townNeed(town, hero);
			}
			else if(enemySeenNear(town->visitablePos(), 3, 20))
				value += 2500; // enemies seen near the town: the main hero should stand guard
			return value > 300 ? value : 0;
		}
		// without a town the game is lost after seven days: any town is worth everything
		if(perception.ownTowns().empty())
			return 100000;
		// (while the main hero fetches the troops waiting at home - refillShare - towns count little to it)
		if(!campaign(hero) && (!params.townWorth || (refillMode && isMain(hero))))
			return enemy ? params.enemyTownValue : params.neutralTownValue;
		// On campaign: the enemy's towns are how the game is won, and a town taken late is a built-up one
		// (townWorth: that is what a town is worth on any day and to any hero - gold and growth for
		// four weeks, taken from the enemy; a town is fully built after some twenty weeks)
		double worth = (enemy ? 2.0 * params.enemyTownValue : params.neutralTownValue) * (1.0 + (params.townWorth ? std::min(today, 140) : today) / 28.0);
		if(enemy && params.townWorth && params.lastTowns > 0)
		{
			// (lastTowns) the fewer towns that enemy has left, the more each of them is the game itself
			int held = 0;
			for(const auto & [otherId, other] : perception.knownObjects())
				held += other.type == Obj::TOWN && other.owner == object.owner ? 1 : 0;
			worth *= 1.0 + params.lastTowns / std::max(1, held);
		}
		return worth;
	}
	case Obj::HERO:
	{
		if(own)
		{
			const CGHeroInstance * other = ownHero(object.id);
			if(!other || other == hero)
				return 0;
			// the two met already and had nothing for each other; nothing has changed since
			if(auto met = fruitlessMeetings.find({std::min(hero->id.getNum(), other->id.getNum()), std::max(hero->id.getNum(), other->id.getNum())});
				met != fruitlessMeetings.end())
			{
				const uint64_t mine = hero->getArmyStrength();
				const uint64_t theirs = other->getArmyStrength();
				if((met->second.first == mine && met->second.second == theirs) || (met->second.first == theirs && met->second.second == mine))
					return 0;
			}
			// meeting the main hero hands over troops - as far as its army can take them
			double gain = 0;
			if(second)
			{
				// (secondArmy) a second leader keeps its army: it goes to the main hero only when that
				// would add half the main hero's army (the leader's army is spent), and takes over what
				// the heroes without an army of their own carry
				if(other->id == mainHero)
				{
					gain = transferGain(other, hero);
					// (gather 2) ... or when its troops are what the decisive battle waits for
					if(gain < 0.5 * static_cast<double>(other->getArmyStrength()) && !(params.gather >= 2 && decisiveBoost(gain) >= 2.0))
						gain = 0;
				}
				else if(!leads(other))
					gain = transferGain(hero, other);
			}
			else
				gain = main ? transferGain(hero, other) : (other->id == mainHero ? transferGain(other, hero) : 0.0);
			if(gain < 1)
				return 0;
			// the main hero walks back for troops only as far as it needs them; the others bring them anyway
			// - but a courier on its supply run is met halfway: both walking, the gap closes twice as fast
			const bool courier = supply && supply->courier == other->id && !supply->returning;
			return gain * params.goldPerArmyValue * params.ferryFactor * armyFactor * reinforcementBoost(gain) * (main && !second && !courier ? fetchFactor(hero) : 1.0);
		}
		if(!enemy || !object.visibleNow || !object.army)
			return 0;
		return 3000 + object.army->expectedStrength() * params.goldPerArmyValue;
	}
	default:
		break;
	}

	// buildings with rules of their own (parameter sites)
	if(params.sites)
	{
		const double site = siteValue(hero, object, instance);
		if(tableDebug && (site > 0 || dynamic_cast<const HillFort *>(instance) || dynamic_cast<const CGUniversity *>(instance)
			|| object.type == Obj::REFUGEE_CAMP || object.type == Obj::WAR_MACHINE_FACTORY || object.type == Obj::ABANDONED_MINE))
			*tableDebug += boost::str(boost::format(" [site %d/%d at %s: %.0f]") % object.type.getNum() % object.subtype.getNum() % object.visitablePos.toString() % site);
		if(site > 0)
			return site;
	}

	// Every other building: the public table of its kind tells what a visit can bring (parameter
	// tables). No building is worth nothing as such - it depends on the hero and the day.
	if(!params.tables || !object.table)
		return 0;
	if(visitedByHero())
		return 0; // the game marks it as visited: nothing more for this hero
	if(object.table->guarded)
	{
		if(params.tables < 2)
			return 0;
		if(object.guardsShown && object.shownGuards.empty())
			return 0; // somebody has been here and beaten the guards
		// before the fight is judged (collectTargets does that for the buildings in reach)
	}
	const double worth = std::max(0.0, tableValue(hero, *object.table, nullptr, object.visitablePos));
	if(tableDebug)
		*tableDebug += boost::str(boost::format(" [%d/%d at %s: %.0f]") % object.type.getNum() % object.subtype.getNum() % object.visitablePos.toString() % worth);
	return worth;
}

std::vector<Target> AdventureAI::collectTargets(const CGHeroInstance * hero, const CPathsInfo & paths, const CPathsInfo & freePaths)
{
	std::vector<Target> out;
	// (townAmbush) An army of weight reckons with the enemy's hidden main army around its towns. A
	// hero with next to nothing does not: it risks itself (2,500 gold) for a town - the enemy takes
	// four of ours a game that way, without a battle.
	struct AmbushScope
	{
		bool & flag;
		bool before;
		~AmbushScope() { flag = before; }
	} ambushScope{ambushCounts, ambushCounts};
	ambushCounts = leads(hero) || static_cast<double>(hero->getArmyStrength()) >= params.heavyPartValue;
	bool foreignTownTargeted = false; // (breakout) a town not ours is among this hero's targets
	const auto known = perception.knownObjects();
	const int3 heroPos = hero->visitablePos();
	const int heroNum = hero->id.getNum();
	const int horizon = travelHorizon(hero);
	const int exploreHorizon = campaign(hero) || (isMain(hero) && params.mainHorizon > params.exploreMaxTurns) ? horizon : params.exploreMaxTurns;

	// (muster) the town whose troops the main hero is to fetch counts however far it is
	int3 musterTile(-1, -1, -1);
	if(musterTown.hasValue() && isMain(hero))
	{
		if(auto it = known.find(musterTown); it != known.end())
			musterTile = it->second.visitablePos;
	}
	auto reachable = [&](const CPathsInfo & p, const int3 & tile) -> const CGPathNode *
	{
		const CGPathNode * node = p.getPathInfo(tile);
		if(!node || !node->reachable() || (node->turns > horizon && tile != musterTile))
			return nullptr;
		return node;
	};

	// guards on the guard-free path to a tile: the first one (closest to the hero), where the path
	// meets it (days) and how many different guards the path crosses in all
	struct GuardOnPath
	{
		ObjectInstanceID guard;
		ObjectInstanceID last;
		double cost = 0;
		int count = 0;
	};
	std::map<const CGPathNode *, GuardOnPath> guardMemo;
	std::function<GuardOnPath(const CGPathNode *)> firstGuard = [&](const CGPathNode * node) -> GuardOnPath
	{
		if(!node)
			return GuardOnPath();
		auto memo = guardMemo.find(node);
		if(memo != guardMemo.end())
			return memo->second;
		GuardOnPath result = node->theNodeBefore ? firstGuard(node->theNodeBefore) : GuardOnPath();
		if(node->coord != heroPos && cb->isVisible(node->coord))
		{
			auto guards = cb->getGuardingCreatures(node->coord);
			if(!guards.empty() && guards.front()->id != result.last)
			{
				if(!result.guard.hasValue())
				{
					result.guard = guards.front()->id;
					result.cost = node->cost;
				}
				result.last = guards.front()->id;
				++result.count;
			}
		}
		guardMemo[node] = result;
		return result;
	};
	// what lies behind a guard, credited to beating it. unlockDirect: counted from the guard (the
	// hero stands there after the fight), further guards on the way halve it each; old rule: counted
	// from where the hero stands now
	auto unlockShare = [&](const GuardOnPath & g, const CGPathNode * free) -> double
	{
		if(!params.unlockDirect)
			return 1.0 / (1.0 + free->cost);
		return std::pow(0.5, std::max(0, g.count - 1)) / (1.0 + std::max(0.0, static_cast<double>(free->cost) - g.cost));
	};

	auto reservedByOther = [&](ObjectInstanceID object)
	{
		for(const auto & entry : reservedBy)
		{
			if(entry.first != hero->id && entry.second == object && ownHero(entry.first))
				return true;
		}
		return false;
	};

	const double ownStrength = fieldStrength(hero);
	const double heroWorth = 2000 + ownStrength * params.goldPerArmyValue;

	// where the hero ends the day when heading for `node`
	auto stopTile = [](const CGPathNode * node) -> int3
	{
		while(node && node->turns > 0 && node->theNodeBefore)
			node = node->theNodeBefore;
		return node ? node->coord : int3(-1);
	};
	const bool cautious = !leads(hero);
	const bool scout = scoutsToday.count(hero->id) > 0;
	// meeting another of our heroes: at the meeting point both armies stand together
	auto ownHeroAt = [&](ObjectInstanceID object) -> const CGHeroInstance *
	{
		return params.concentrate && object.hasValue() ? ownHero(object) : nullptr;
	};
	auto unsafe = [&](const CGPathNode * node, ObjectInstanceID object) -> bool
	{
		int3 tile = node->coord;
		int3 stop = stopTile(node);
		const CGHeroInstance * partner = ownHeroAt(object);
		// collectors are weak: they work close to home (scouts roam, but still avoid known enemies);
		// bringing troops to the main hero is worth the trip
		const bool toMain = (partner && partner->id == mainHero)
			|| (params.couriers && object.hasValue() && object == mainHero);
		if(cautious && !scout && !toMain && (!inTerritory(tile) || (stop.isValid() && !inTerritory(stop))))
			return true;
		if(threats.empty())
			return false;
		// attacking the threat itself is judged by the battle estimate
		const double together = partner ? fieldStrength(partner) : 0.0;
		// (townHop) a foreign town taken today is ours tonight: behind its walls we stand as we do in our own
		double walls = 1.0;
		if(params.townHop && object.hasValue() && node->turns == 0)
		{
			auto town = known.find(object);
			auto fort = townFortLevel.find(object);
			if(town != known.end() && town->second.type == Obj::TOWN && town->second.owner != me && town->second.visitablePos == tile && fort != townFortLevel.end())
				walls = fort->second >= CGTownInstance::CITADEL ? params.defenceWallsCitadel : (fort->second >= CGTownInstance::FORT ? params.defenceWallsFort : 1.0);
		}
		if(threatAt(tile, partner ? ObjectInstanceID() : object, cautious) > tolerance(hero) * (defenceAt(hero, tile) * walls + together))
			return true;
		return stop.isValid() && stop != tile && threatAt(stop, partner ? ObjectInstanceID() : object, cautious) > tolerance(hero) * defenceAt(hero, stop);
	};

	// standing in a town that cannot hold on its own: only trips that end back home today,
	// except to attack an enemy hero (the threat itself)
	double roundTripBudget = -1;
	if(params.townGuard && isMain(hero))
	{
		if(const auto * home = hero->getVisitedTown(); home && home->visitablePos() == heroPos && townNeed(home, hero) > 0.2)
			roundTripBudget = static_cast<double>(hero->movementPointsRemaining()) / std::max(1, hero->movementPointsLimit(true));
	}
	// (fortress) outmatched by an army seen near us: from a town of ours only trips that end back
	// there today; outside a town the way home, and what lies on it
	const double outmatched = isMain(hero) ? outmatchedBy(hero) : 0.0;
	bool fortressOutside = false;
	if(outmatched > 0)
	{
		const auto * home = hero->getVisitedTown();
		if(home && home->tempOwner == me && home->visitablePos() == heroPos)
			roundTripBudget = static_cast<double>(hero->movementPointsRemaining()) / std::max(1, hero->movementPointsLimit(true));
		else
			fortressOutside = !perception.ownTowns().empty();
		if(fortressLog != today)
			logAi->debug("FairAI %s day %d: %s is outmatched (own %.0f, an enemy army of %.0f seen near us lately): %s", me.toString(), today,
				hero->getNameTranslated(), fieldStrength(hero), outmatched, fortressOutside ? "heading for a town of ours" : "day trips from the town only");
		fortressLog = today;
	}
	// (safeParts) A real army in the hands of a hero that is not our main hero does not spend the
	// night in the open: whatever attacks it there wins, and it is attacked only by what wins. From
	// a town of ours it makes trips that end back there today; outside it heads for one. Handing the
	// troops to the main hero is the one errand that counts as arriving.
	bool heavyPart = false;
	bool evacuating = false; // (townHeroes) the town cannot hold: the army leaves for one where it can stand
	if((params.safeParts || params.townHeroes) && !leads(hero) && !perception.ownTowns().empty())
	{
		const double carried = static_cast<double>(hero->getArmyStrength());
		heavyPart = heavy(hero);
		if(heavyPart)
		{
			const auto * home = hero->getVisitedTown();
			if(home && home->tempOwner == me && home->visitablePos() == heroPos)
				roundTripBudget = static_cast<double>(hero->movementPointsRemaining()) / std::max(1, hero->movementPointsLimit(true));
			else
				fortressOutside = true;
			if(heavyPartLog.insert(today * 1000 + heroNum % 1000).second)
				logAi->debug("FairAI %s day %d: %s carries %.0f - %s", me.toString(), today, hero->getNameTranslated(), carried,
					fortressOutside ? "heading for a town of ours or the main hero" : "day trips from the town only");
		}
	}
	// (townHeroes) The town cannot hold against what can reach it by tomorrow, not even with this army
	// behind its walls: the army leaves for a town of ours where it can stand. A garrison that waits is
	// destroyed for a third of its worth in enemy losses (2,895 defences, 2 % held); the town falls
	// either way.
	const CGTownInstance * refuge = nullptr;
	double refugeCost = 1e9;
	if(params.townHeroes && heavyPart && roundTripBudget >= 0 && threatAt(heroPos) > params.evacuateRatio * defenceAt(hero, heroPos))
	{
		for(const auto * town : perception.ownTowns())
		{
			if(town->visitablePos() == heroPos)
				continue;
			const CGPathNode * node = paths.getPathInfo(town->visitablePos());
			if(!node || !node->reachable() || node->cost >= refugeCost || failedToday.count({heroNum, town->visitablePos()}))
				continue;
			if(const auto * visitor = town->getVisitingHero(); visitor && visitor != hero)
				continue; // the gate is taken
			// safe there, and safe on the nights on the way (as a supply courier judges its road)
			if(threatAt(town->visitablePos(), ObjectInstanceID(), true) > tolerance(hero) * defenceAt(hero, town->visitablePos()))
				continue;
			if(node->turns > 0 && supplyRoadThreat(node) > tolerance(hero) * fieldStrength(hero))
				continue;
			refuge = town;
			refugeCost = node->cost;
		}
		evacuating = refuge != nullptr;
		if(refuge)
			postOf[hero->id] = refuge->id;
		if(heavyPartLog.insert(today * 1000 + heroNum % 1000 + 500000000).second)
			logAi->info("FairAI %s day %d: %s cannot hold %s (an army of %.0f in reach, we stand at %.0f behind the walls): %s", me.toString(), today,
				hero->getNameTranslated(), hero->getVisitedTown() ? hero->getVisitedTown()->getNameTranslated() : std::string("the town"),
				threatAt(heroPos), defenceAt(hero, heroPos),
				refuge ? (boost::format("leaving for %s, %.1f days away") % refuge->getNameTranslated() % refugeCost).str() : std::string("no town to go to - it stays"));
	}
	// (townHeroes) A town's hero whose town is calm moves to a town of ours that is not: one taken
	// lately, or with enemy heroes about, and no hero of ours in it. Towns change sides about ten
	// times a game each way, and the enemy takes back what the main hero leaves behind it. The road
	// is judged as a supply courier judges its road; the army must be able to stand there.
	const CGTownInstance * post = nullptr;
	double postCost = 1e9;
	if(params.townHeroes && heavyPart && !evacuating)
	{
		const CGTownInstance * home = hero->getVisitedTown();
		if(auto goal = postOf.find(hero->id); goal != postOf.end() && home && home->id == goal->second)
		{
			postOf.erase(goal); // arrived
			postSince[hero->id] = today;
		}
		// (a week at least where it went: what makes a town contested comes and goes within days)
		auto since = postSince.find(hero->id);
		if(roundTripBudget >= 0 && home && !contested(home) && (since == postSince.end() || today - since->second >= 7))
		{
			for(const auto * town : perception.ownTowns())
			{
				if(town == home || town->getVisitingHero() || town->getGarrisonHero() || !contested(town))
					continue;
				bool taken = false;
				for(const auto & [other, goal] : postOf)
					taken = taken || (other != hero->id && goal == town->id && ownHero(other));
				const CGPathNode * node = paths.getPathInfo(town->visitablePos());
				if(taken || !node || !node->reachable() || node->turns > 6 || node->cost >= postCost || failedToday.count({heroNum, town->visitablePos()}))
					continue;
				if(threatAt(town->visitablePos(), ObjectInstanceID(), true) > tolerance(hero) * defenceAt(hero, town->visitablePos()))
					continue;
				if(node->turns > 0 && supplyRoadThreat(node) > tolerance(hero) * fieldStrength(hero))
					continue;
				post = town;
				postCost = node->cost;
			}
		}
	}
	// (gather 2) The muster. A town's hero brings its troops to the main hero, up to gatherDays away,
	// when they are what the decisive battle waits for (they close half the gap to the edge or more),
	// or when its town cannot hold and there is no town to go to: troops that wait there are beaten
	// one town after the other by the one army that is stronger than each of them and weaker than
	// all of them. (Logged: on day 84 we had 527,000 against 207,000, the main hero 207,000 of them;
	// the enemy's main hero took two towns with 63,000 and 41,000 in them within three days while
	// ours stood a day's march away, short of the edge by the troops that fell there.)
	bool muster = false;
	if(params.gather >= 2 && heavyPart && !leads(hero))
	{
		const CGHeroInstance * leader = ownHero(mainHero);
		if(leader && leader != hero)
		{
			const double gain = transferGain(leader, hero);
			const bool lastResort = params.townHeroes && roundTripBudget >= 0 && !refuge && threatAt(heroPos) > params.evacuateRatio * defenceAt(hero, heroPos);
			muster = gain > 0 && (decisiveBoost(gain) >= 2.0 || lastResort);
			if(muster && heavyPartLog.insert(today * 1000 + heroNum % 1000 + 700000000).second)
			{
				if(statsFile)
					stats[phaseOf(today)]["muster"] += 1;
				logAi->info("FairAI %s day %d: %s musters - its army (%llu, %.0f of it for %s) %s", me.toString(), today, hero->getNameTranslated(),
					static_cast<unsigned long long>(hero->getArmyStrength()), gain, leader->getNameTranslated(),
					lastResort ? "cannot hold its town and has no town to go to" : "is what the decisive battle waits for");
			}
		}
	}
	auto addTarget = [&](const int3 & tile, ObjectInstanceID object, double value, double loss, double travel, const std::string & description)
	{
		if(evacuating && description != "evacuate")
			return;
		// (safeParts) the main hero within today's march: delivering the troops is arriving
		const bool delivery = (heavyPart && object.hasValue() && object == mainHero && (travel < 1.0 || (muster && travel <= params.gatherDays))) || description == "evacuate" || description == "relocate";
		if(fortressOutside && description != "fortress" && description != "retreat" && description != "flee" && travel > 0.25 && !delivery)
		{
			auto it = object.hasValue() ? known.find(object) : known.end();
			const bool ownTown = it != known.end() && it->second.type == Obj::TOWN && it->second.owner == me;
			const bool enemyHeroToday = it != known.end() && it->second.type == Obj::HERO && isEnemy(it->second.owner) && travel < 1.0;
			const bool townToday = heavyPart && it != known.end() && it->second.type == Obj::TOWN && travel < 1.0; // (safeParts) taken today, it is ours tonight
			if(!ownTown && !enemyHeroToday && !townToday)
				return;
		}
		if(roundTripBudget >= 0 && travel * 2 > roundTripBudget && !delivery)
		{
			auto it = object.hasValue() ? known.find(object) : known.end();
			const bool enemyHero = it != known.end() && it->second.type == Obj::HERO && isEnemy(it->second.owner);
			// (safeParts) another town within today's march is as good a place to end the day - one of
			// ours, or one we take today (the fight is judged like any other): what waits in a town
			// with a real army wins back the towns around it and meets the raiders that come in reach
			const bool otherTownToday = heavyPart && it != known.end() && it->second.type == Obj::TOWN && travel < 1.0;
			// the threat itself may be attacked, but only today: walking off towards it leaves the town open
			if(!otherTownToday && (!enemyHero || (params.antiOscillation && travel > roundTripBudget)))
				return;
		}
		if(params.antiOscillation && description != "retreat" && description != "flee")
		{
			auto gaveUp = abandonedToday.find(hero->id);
			if(gaveUp != abandonedToday.end() && gaveUp->second.count(tile))
				return;
		}
		Target t;
		t.tile = tile;
		t.object = object;
		t.value = value;
		t.loss = loss;
		t.travel = travel;
		t.description = description;
		t.score = (value - loss) / (travel + params.travelOffset);
		auto goal = currentGoal.find(hero->id);
		if(goal != currentGoal.end() && goal->second == tile)
			t.score *= params.stickiness;
		out.push_back(t);
	};
	if(evacuating)
		addTarget(refuge->visitablePos(), refuge->id, 2000 + 0.5 * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue, 0, refugeCost, "evacuate");
	if(post)
	{
		postOf[hero->id] = post->id;
		if(heavyPartLog.insert(today * 1000 + heroNum % 1000 + 600000000).second)
			logAi->info("FairAI %s day %d: %s moves its army (%llu) from %s to %s, %.1f days away - taken lately or enemy heroes about, and no hero of ours there", me.toString(), today,
				hero->getNameTranslated(), static_cast<unsigned long long>(hero->getArmyStrength()),
				hero->getVisitedTown() ? hero->getVisitedTown()->getNameTranslated() : std::string("?"), post->getNameTranslated(), postCost);
		addTarget(post->visitablePos(), post->id, 3000 + static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue, 0, postCost, "relocate");
	}

	// value hidden behind guards, credited to the guard that blocks it
	std::map<ObjectInstanceID, double> unlockValue;
	std::map<ObjectInstanceID, double> strategicUnlock; ///< the part of it that a campaign is about: foreign towns, land not yet seen
	struct Approach
	{
		int3 town;
		double value = 0;
	};
	std::vector<Approach> approach; ///< enemy towns we know of but cannot find a way to (campaign, seekTowns)
	// Outside a campaign too, a main hero that need not fear the enemy's heroes looks for the way to
	// a town it knows of: the land around a Cover of Darkness is hidden again every day, and without
	// this the hero turned round a day's march from the town to fetch a few troops at home.
	const bool seeking = params.seekTowns && isMain(hero) && !campaign(hero) && today >= 14
		&& (oppositionHeroes <= 0 || fieldStrength(hero) >= params.heroEdge * oppositionHeroes);
	// (seekTowns) Any town not ours that we know of but cannot find a way to - the land between is
	// unseen, or hidden again: the way there is guessed through the fog, and what stands on that
	// guess (a stretch to explore, a guard) is credited with the town.
	struct Seek
	{
		int z = 0;
		double value = 0;
		std::vector<uint16_t> steps;
	};
	std::vector<Seek> seek;
	std::map<std::string, int> rejected; // diagnostics
	const bool bankLog = (params.tables || params.sites) && isMain(hero) && logAi->isDebugEnabled() && tableLogDay != today;
	std::string bankLines;
	std::string tableLines;
	tableDebug = bankLog ? &tableLines : nullptr;

	// (breakout 3) Doors: two-way monoliths and subterranean gates we have never gone through. On
	// generated maps some regions are joined by such a pair instead of a pass - the way out of our
	// corner is then a door, guarded or not, and "land behind a guard" finds nothing. (A main hero
	// of 385,000 stood for thirty days before twenty to forty-nine beholders that watched a
	// monolith: they "opened" 179 gold, and no way out was known with two thirds of the map unseen.)
	std::map<ObjectInstanceID, double> doorBehind;               // guard -> a door behind it
	std::vector<std::pair<ObjectInstanceID, double>> openDoors;  // doors the hero can walk to: id, days
	double unseenShare = 0;
	if(auto seen = seenTilesOn.find(today); seen != seenTilesOn.end() && mapTiles > 0)
		unseenShare = 1.0 - static_cast<double>(seen->second) / mapTiles;

	// ---- objects other than monsters
	for(const auto & [id, object] : known)
	{
		if(object.type == Obj::MONSTER || object.visitablePos == heroPos)
			continue;
		if(failedToday.count({heroNum, object.visitablePos}))
			continue;
		if(auto f = fruitless.find({heroNum, id}); f != fruitless.end() && today < f->second + 7)
			continue;
		const CGObjectInstance * instance = perception.visibleObject(id);
		double value = objectValue(hero, object, instance);
		if(params.couriers && id == mainHero && !isMain(hero) && logAi->isDebugEnabled() && courierLog.insert(today * 1000 + heroNum % 1000).second)
		{
			// once a day per hero: why a hero does or does not bring its troops to the leader (diagnostics)
			const CGHeroInstance * leader = ownHero(mainHero);
			const CGPathNode * way = reachable(paths, object.visitablePos);
			const CGPathNode * freeWay = way ? way : reachable(freePaths, object.visitablePos);
			logAi->debug("FairAI %s: %s (army %llu) to the leader %s (army %llu): gain %.0f, value %.0f%s, way %s%s", me.toString(),
				hero->getNameTranslated(), static_cast<unsigned long long>(hero->getArmyStrength()),
				leader ? leader->getNameTranslated() : "?", static_cast<unsigned long long>(leader ? leader->getArmyStrength() : 0),
				leader ? transferGain(leader, hero) : 0.0, value, reservedByOther(id) ? " (another hero is on its way)" : "",
				way ? (boost::format("%.2f days") % way->cost).str() : (freeWay ? "behind a guard" : "none"),
				way && unsafe(way, id) ? ", unsafe" : "");
		}
		if(value <= 0 || reservedByOther(id))
			continue;
		const bool door = params.breakout >= 3 && leads(hero) && !usedTeleporters.count(id)
			&& (object.type == Obj::MONOLITH_TWO_WAY || object.type == Obj::SUBTERRANEAN_GATE);

		if(const CGPathNode * node = reachable(paths, object.visitablePos))
		{
			const bool foreignHero = object.type == Obj::HERO && object.owner != me;
			const bool foreignTown = object.type == Obj::TOWN && object.owner != me;
			// (townWorth) off campaign the main hero's long view is for foreign towns: a mine, a hero
			// or a town of ours counts as far away as it did before
			if(params.townWorth && isMain(hero) && !campaign(hero) && !foreignTown && id != musterTown
				&& node->turns > std::max(params.maxTravelTurns, params.mainHorizon))
			{
				++rejected["far"];
				continue;
			}
			// An enemy hero further off than today's march will not be there when we arrive: no chase
			// (one standing in a town is that town's defender - the town is the target)
			if((params.noChase == 1 || (params.noChase == 2 && campaign(hero))) && foreignHero && node->turns > 0)
			{
				bool inTown = false;
				for(const auto & [otherId, other] : known)
					inTown = inTown || (other.type == Obj::TOWN && other.visitablePos == object.visitablePos);
				if(!inTown)
				{
					++rejected["no chase"];
					continue;
				}
			}
			// On campaign the main hero has one job - the enemy's towns. Errands are done only on the
			// way; our own towns call it back only when they cannot hold.
			if(campaign(hero) && !foreignTown && !foreignHero && id != musterTown)
			{
				bool needed = false;
				if(object.type == Obj::TOWN)
				{
					const auto * town = dynamic_cast<const CGTownInstance *>(instance);
					needed = town && townNeed(town, hero) > 0.2;
				}
				if(!needed && node->cost > (object.type == Obj::TOWN || object.type == Obj::HERO ? 0.3 : 0.5))
				{
					++rejected["errand"];
					continue;
				}
			}
			double danger = guardDanger(object.visitablePos);
			ObjectInstanceID siegedTown; // attacking this tile means a siege (a hero in a town gate, too)
			double defenders = 0; // nobody at home: the town is simply taken, no battle
			if((object.type == Obj::HERO || object.type == Obj::TOWN) && object.owner != me && !isAlly(object.owner))
			{
				// everything defending this tile: heroes (with their skills) and a town behind walls
				bool town = false;
				for(const auto & [otherId, other] : known)
				{
					if(other.visitablePos != object.visitablePos || other.owner == me)
						continue;
					if(other.type == Obj::HERO)
						defenders += observedDanger(other) * heroFactor(other);
					else if(other.type == Obj::TOWN)
					{
						defenders += observedDanger(other);
						town = true;
						siegedTown = otherId;
					}
				}
				danger += town ? defenders * params.wallsFactor : defenders;
			}
			const bool desperate = object.type == Obj::TOWN && perception.ownTowns().empty();
			const bool versusHero = (object.type == Obj::HERO || object.type == Obj::TOWN) && isEnemy(object.owner);
			// guards only: the simulated battle decides; heroes and towns: army values (their skills and spells are unknown)
			double loss = params.fightForecast && !versusHero && defenders <= 0 && !desperate
				? guardLoss(hero, object.visitablePos)
				: expectedLoss(hero, danger, desperate, versusHero);
			// an enemy hero in the open: the simulated battle, with what we assume about the hero, has the last word
			if(params.heroForecast && loss >= 0 && object.type == Obj::HERO && versusHero && !siegedTown.hasValue() && !desperate
				&& guardDanger(object.visitablePos) <= 0)
			{
				const double simulated = heroFightLoss(hero, object);
				loss = simulated < 0 ? -1 : std::max(loss, simulated);
			}
			if(loss < 0)
			{
				// (statistics) an enemy hero within today's march that the main hero leaves alone
				// although it is no weaker than ours: how often is there a decisive battle to be had?
				if(statsFile && isMain(hero) && object.type == Obj::HERO && versusHero && node->turns == 0 && danger > 0)
				{
					const double edge = fieldStrength(hero) / danger;
					nearMissToday = std::max(nearMissToday, edge >= 1.0 ? 2 : (edge >= 0.8 ? 1 : 0));
				}
				++rejected["too strong"];
				continue;
			}
			// a creature bank or another guarded building: the fight against what may wait inside
			const bool bank = guardedTable(object);
			if(bank)
			{
				const GuardedPlan plan = guardedPlan(hero, object);
				if(bankLog)
					bankLines += boost::str(boost::format(" [%d/%d at %s, %.1f days: %s, we would fight %.0f%% of what may wait there, worth %.0f, loss %.0f (%.0f%% of the army)]")
						% object.type.getNum() % object.subtype.getNum() % object.visitablePos.toString() % node->cost
						% (plan.known ? "guards listed" : "guards unknown") % (100 * plan.chance) % plan.value % plan.loss % (100 * plan.lossShare));
				if(plan.chance <= 0 || plan.value <= plan.loss)
				{
					++rejected["bank too strong"];
					continue;
				}
				value = plan.value;
				loss += plan.loss;
			}
			// a dwelling of high creatures: the fight against its guards (always on - without it heroes
			// walk into certain death, see dwellingGuards)
			if(object.type == Obj::CREATURE_GENERATOR1 || object.type == Obj::CREATURE_GENERATOR4)
			{
				const auto guards = dwellingGuards(object, instance);
				if(!guards.empty())
				{
					const double fight = dwellingLoss(hero, guards);
					if(fight < 0 || fight >= value)
					{
						++rejected["dwelling guards"];
						continue;
					}
					double health = 0;
					for(const auto & [creature, count] : guards)
						health += static_cast<double>(creature.toCreature()->getMaxHealth()) * count;
					value += health * expValue(hero);
					loss += fight;
				}
			}
			if(params.sites)
			{
				// the fight a visit means: somebody's garrison, the troglodytes of an abandoned mine
				const double fight = siteLoss(hero, object);
				if(fight < 0)
				{
					++rejected["site too strong"];
					continue;
				}
				loss += fight;
			}
			if(siegedTown.hasValue() && !desperate && defenders > 0)
			{
				// moat and arrow towers take their share before the walls fall
				auto fort = townFortLevel.find(siegedTown);
				const double overhead = fort != townFortLevel.end() ? siegeOverhead(hero, fort->second) : 0;
				if(overhead > 0)
				{
					const double left = fieldStrength(hero) - overhead;
					if(overhead > 0.5 * fieldStrength(hero) || left < danger * (params.heroModel ? params.heroEdge : params.safetyRatioHero))
					{
						++rejected["walls"];
						continue;
					}
					loss += overhead * params.lossFactor * params.goldPerArmyValue * params.lossAversion;
				}
			}
			if(!desperate && unsafe(node, object.type == Obj::HERO ? id : ObjectInstanceID()))
			{
				++rejected["unsafe"];
				continue;
			}
			// a threatened own town needs its defender
			if(!params.townGuard && object.type == Obj::TOWN && object.owner == me && isMain(hero))
			{
				double garrison = 0;
				if(const auto * town = dynamic_cast<const CGTownInstance *>(instance))
					for(const auto & slot : town->Slots())
						garrison += stackValue(slot.second.get());
				double threat = threatAt(object.visitablePos);
				if(threat > garrison)
					value += params.townDefenceValue * std::min(1.0, (threat - garrison) / std::max(1.0, threat));
			}
			// Fetching troops at home is a round trip: afterwards the hero stands at home, not where
			// its work is. Counted as twice the way - unless the town calls for its defender.
			if(params.fetchRoundTrip && isMain(hero) && object.type == Obj::TOWN && object.owner == me)
			{
				const auto * town = dynamic_cast<const CGTownInstance *>(instance);
				if(town && townNeed(town, hero) <= 0.2)
					value *= (node->cost + params.travelOffset) / (2.0 * node->cost + params.travelOffset);
			}
			// A visit that takes the movement the hero has left (Swan Pond; Watering Place and Grave
			// of Horn of the Abyss): the rest of that day is its price, as time on top of the way.
			double travel = node->cost;
			if(object.table)
			{
				const double share = moveLossShare(hero, *object.table);
				if(share > 0)
				{
					travel += share * std::max(0, node->moveRemains) / std::max(1, hero->movementPointsLimit(true));
					if(logAi->isDebugEnabled() && moveLossLog.insert({today, heroNum, id.getNum()}).second)
						logAi->debug("FairAI %s day %d: %s and the building %d/%d at %s that takes the movement left: worth %.0f, %.2f days away, %d movement points left on arrival -> counted as %.2f days",
							me.toString(), today, hero->getNameTranslated(), object.type.getNum(), object.subtype.getNum(), object.visitablePos.toString(),
							value, node->cost, node->moveRemains, travel);
				}
			}
			// (heroes: whose - walking up to our own hero hands over troops, an enemy's is a hunt)
			if(object.type == Obj::TOWN && object.owner != me && !isAlly(object.owner))
				foreignTownTargeted = true;
			if(door)
				openDoors.emplace_back(id, travel);
			addTarget(object.visitablePos, id, value, loss, travel, (bank ? "bank " : "object ") + std::to_string(object.type.getNum())
				+ (object.type == Obj::HERO ? (object.owner == me ? " own" : " enemy") : ""));
		}
		else if(const CGPathNode * free = reachable(freePaths, object.visitablePos))
		{
			++rejected["behind guard"];
			if(guardedTable(object))
			{
				// what it is worth once the way is free: after the fight against its own guards
				const GuardedPlan plan = guardedPlan(hero, object);
				value = plan.chance > 0 ? std::max(0.0, plan.value - plan.loss) : 0.0;
			}
			const GuardOnPath g = firstGuard(free);
			if(g.guard.hasValue())
			{
				unlockValue[g.guard] += value * unlockShare(g, free);
				if(object.type == Obj::TOWN && object.owner != me)
					strategicUnlock[g.guard] += value * unlockShare(g, free);
				if(door)
					doorBehind[g.guard] = unseenShare;
			}
		}
		else if(params.seekTowns && object.type == Obj::TOWN && object.owner != me && !isAlly(object.owner)
			&& (isMain(hero) || scoutsToday.count(hero->id)) && (!isEnemy(object.owner) || seeking || campaign(hero) || !isMain(hero)))
		{
			seek.push_back({object.visitablePos.z, value, fogDistances(object.visitablePos)});
		}
		else if((campaign(hero) || (params.townWorth && isMain(hero))) && object.type == Obj::TOWN && isEnemy(object.owner) && object.visitablePos.z == heroPos.z)
		{
			// an enemy town we know of but cannot find a way to (the land around it is hidden, or the
			// way leads around something): exploring is drawn towards it
			approach.push_back({object.visitablePos, value});
		}
	}

	tableDebug = nullptr;
	if(bankLog && (!bankLines.empty() || !tableLines.empty()))
	{
		// once a day: what the buildings valued by their table are worth to the main hero, and how it
		// judges the guarded ones in reach (diagnostics)
		logAi->debug("FairAI %s day %d: %s (army %d) buildings by table:%s; guarded ones in reach:%s", me.toString(), today, hero->getNameTranslated(),
			static_cast<int>(hero->getArmyStrength()), tableLines.empty() ? " none" : tableLines, bankLines.empty() ? " none" : bankLines);
		tableLogDay = today;
	}

	// ---- exploration
	// on campaign land that was hidden again counts like land never seen: that is where a town with a Cover of Darkness lies
	RevealGrid revealGrid(*cb, perception.staleTiles(), campaign(hero) || !seek.empty() ? 1.0 : params.staleTileWeight);
	double tileValue = params.explorationTileValue;
	{
		RevealGrid & grid = revealGrid;
		const int radius = hero->getSightRadius();
		// exploring stays important until we know where the enemy lives; a strong main hero hunts
		bool enemyTownKnown = false;
		for(const auto & [id, object] : known)
		{
			if(object.type == Obj::TOWN && object.owner.isValidPlayer() && object.owner != me
				&& cb->getPlayerRelations(me, object.owner) == PlayerRelations::ENEMIES)
				enemyTownKnown = true;
		}
		const std::set<int3> * gaveUp = nullptr;
		if(auto it = abandonedToday.find(hero->id); it != abandonedToday.end() && params.antiOscillation)
			gaveUp = &it->second;
		double decay = enemyTownKnown ? std::max(0.25, 1.0 - today / 56.0) : std::max(0.6, 1.0 - today / 56.0);
		if(campaign(hero))
			decay = std::max(0.6, 1.0 - today / 56.0) * params.huntFactor * (huntWanted ? 3.0 : 1.5); // looking for the enemy's towns
		else if(!enemyTownKnown && isMain(hero) && today > 14)
			decay *= params.huntFactor;
		tileValue = params.explorationTileValue * decay;
		const int3 size = cb->getMapSize();
		Target bestExplore;
		bestExplore.score = 0;
		// (breakout 4) The leader's standing task while no foreign town is among its targets: find the
		// next one. Levels 1 to 3 wait for a standstill - two weeks without new land, or three days
		// without a target worth the army - and the standstill ends with the first step that shows new
		// land or the first decent pile: in probe games the hero set out for a way out, turned to a
		// mine, was "shut in" again three days later. From the third week on the search simply is
		// on: ways out (guards with unseen land behind, doors) count what a town is worth, by how
		// much of the map around the land behind them is unseen.
		const double searchWorth = params.enemyTownValue * (1.0 + std::min(today, 140) / 28.0);
		// On campaign - the leader far stronger than the strongest enemy hero seen lately - the search
		// is on whatever towns are among the targets: the towns within reach are then mostly our own,
		// lost behind our back, and the enemy's base lies in land we never looked at. (A leader of
		// 430,000 to 1,400,000, three to five times the enemy's main hero for six weeks, took back the
		// same three towns of ours again and again; the enemy's capital stood ten tiles beyond the
		// open pass south of the centre, in the third of the map we had not seen by day 200. The
		// enemy's main hero grew from 80,000 to 550,000 meanwhile, and the game was lost.)
		const bool onCampaign = campaign(hero);
		bool searching = params.breakout >= 4 && leads(hero) && (!foreignTownTargeted || onCampaign) && today >= 14 && unseenShare >= 0.1;
		// a way out just opened (its guard beaten, or a door gone through): look at what lies behind
		// it; on campaign open land that runs on into unseen regions counts on any day
		bool lookingBehind = params.breakout >= 4 && leads(hero) && ((!foreignTownTargeted && today < breakoutPauseUntil) || (onCampaign && unseenShare >= 0.1));
		double waitingAtHome = 0;
		if(searching || lookingBehind)
		{
			// ... but not with a part of our army: what waits in our towns and what a courier is
			// bringing must be less than breakoutReady of what the hero leads - else that comes first
			// (fetched, or the courier met). The first version of this level went on from way out to
			// way out with 14,000 for four weeks, across the whole map, while ten times that stood at
			// home; the second walked on into unseen land with 38,000 and met the enemy's main hero
			// with 93,000, its courier two days behind with 46,000.
			for(const auto * town : perception.ownTowns())
			{
				double troops = townTroops(town, hero);
				if(auto left = townLeftover.find(town->id); left != townLeftover.end() && params.antiOscillation)
					troops = std::max(0.0, troops - left->second);
				waitingAtHome += troops;
			}
			if(supply && !supply->returning && supply->courier != hero->id)
			{
				if(const auto * courier = ownHero(supply->courier))
					waitingAtHome += transferGain(hero, courier);
			}
			if(waitingAtHome >= params.breakoutReady * static_cast<double>(hero->getArmyStrength()))
			{
				if(breakoutLogDay != today)
				{
					logAi->info("FairAI %s day %d: %s does not go on looking for the next town - %.0f wait at home or are on their way, it leads %llu", me.toString(), today,
						hero->getNameTranslated(), waitingAtHome, static_cast<unsigned long long>(hero->getArmyStrength()));
					breakoutLogDay = today;
				}
				searching = false;
				lookingBehind = false;
			}
		}
		double bestOpenEnd = 0;
		std::map<ObjectInstanceID, double> behindGuard;
		std::map<ObjectInstanceID, double> fogBehind; // most tiles one step behind the guard would reveal
		std::map<ObjectInstanceID, double> openEnd; // (passage) the land we know behind the guard ends in land we have not seen: how much of the map around there is unseen
		std::map<ObjectInstanceID, double> wayOut; // (breakout) unseen land behind this guard: the largest share of the map around a place there that is unseen
		for(int z = 0; z < size.z; ++z)
		{
			for(int x = 0; x < size.x; ++x)
			{
				for(int y = 0; y < size.y; ++y)
				{
					int3 tile(x, y, z);
					if(tile == heroPos || failedToday.count({heroNum, tile}) || trail[hero->id].count(tile))
						continue;
					if(gaveUp && gaveUp->count(tile))
						continue;
					const CGPathNode * node = reachable(paths, tile);
					const bool normal = node && node->turns <= exploreHorizon && node->action == EPathNodeAction::NORMAL;
					const CGPathNode * free = normal ? nullptr : reachable(freePaths, tile);
					// (passage) The tiles a guard watches on its far side are land behind it as well. On
					// generated maps the guard between two regions stands in a gap one tile wide, and
					// the view often ends right behind it: no open tile lies beyond, so the guard
					// "opened" nothing. (A main hero of 13,000 left 35 troglodytes of 3,000 standing
					// from day 10 to day 28 - they were the only way out of its corner of the map.)
					// (breakout 3) Without passage such a tile counts for one thing only: the guard is a way out.
					bool watched = (params.passage || (params.breakout >= 3 && leads(hero))) && free && free->action == EPathNodeAction::BATTLE;
					if(watched)
					{
						// the far side: no tile we can simply walk to within two steps
						for(int dx = -2; dx <= 2 && watched; ++dx)
							for(int dy = -2; dy <= 2 && watched; ++dy)
							{
								const int3 near(x + dx, y + dy, z);
								if(!cb->isInTheMap(near))
									continue;
								const CGPathNode * walk = reachable(paths, near);
								watched = !(walk && walk->action == EPathNodeAction::NORMAL);
							}
					}
					if(!normal && !(free && free->turns <= exploreHorizon && (free->action == EPathNodeAction::NORMAL || watched)))
						continue;
					double reveal = grid.count(tile, radius);
					if(reveal < 4)
						continue;
					double value = reveal * tileValue;
					// an enemy town we know of but have no way to: uncovering the land around it is how the way is found
					for(const auto & a : approach)
					{
						if(tile.z == a.town.z)
							value *= 1.0 + 4.0 * std::max(0.0, 1.0 - tile.dist2d(a.town) / 24.0);
					}
					// (regionValue) Not a pocket of fog but land for a whole region beyond this tile: it holds
					// mines, treasure and most likely a town - worth more than the strip the first step shows.
					// (A hero that had just broken through a border guard walked home for three days
					// instead of looking around; the town of that region stood eight tiles further on.)
					if(params.regionValue > 0 && isMain(hero) && params.exploreRegion)
						value += params.regionValue * std::clamp((grid.countBox(tile, 12) - 150.0) / 250.0, 0.0, 1.0);
					for(const auto & s : seek)
					{
						// the town's worth, less for every day's march still to go beyond this tile
						const uint16_t steps = z == s.z ? s.steps[static_cast<size_t>(y) * size.x + x] : UINT16_MAX;
						if(steps != UINT16_MAX)
							value += s.value / (1.0 + steps / 15.0);
					}
					if(normal)
					{
						if(unsafe(node, ObjectInstanceID()))
							continue;
						// (breakout 4) For three days after a way out was opened, the land behind it counts
						// what the way out counted: open land that runs on into a region we have not seen.
						// (A hero of 77,000 beat the guard of a gap, turned round and walked home for three
						// days to fetch troops; the town of that region stood eight tiles beyond the gap.)
						// Three days, not for good: with this bonus on every day the main hero became a
						// scout that never came home for its army.
						if(lookingBehind)
						{
							const double part = std::clamp(grid.boxShare(tile, 12) / 0.3, 0.0, 1.0);
							if(part >= 0.2 && reveal >= 10)
							{
								const double open = searchWorth * part * std::clamp(reveal / 40.0, 0.25, 1.0);
								value += open;
								bestOpenEnd = std::max(bestOpenEnd, open);
							}
						}
						double score = value / (node->cost + params.travelOffset);
						if(score > bestExplore.score)
						{
							bestExplore.tile = tile;
							bestExplore.value = value;
							bestExplore.travel = node->cost;
							bestExplore.score = score;
						}
					}
					else
					{
						const GuardOnPath g = firstGuard(free);
						if(g.guard.hasValue())
						{
							if(params.breakout)
								wayOut[g.guard] = std::max(wayOut[g.guard], grid.boxShare(tile, 12));
							if(watched && !params.passage)
								continue; // (breakout 3) a way out, nothing else
							behindGuard[g.guard] = std::max(behindGuard[g.guard], value * unlockShare(g, free));
							if(g.count == 1)
							{
								fogBehind[g.guard] = std::max(fogBehind[g.guard], reveal);
								if(params.passage && grid.edge(tile))
									openEnd[g.guard] = std::max(openEnd[g.guard], grid.boxShare(tile, 12));
							}
						}
					}
				}
			}
		}
		if(bestExplore.score > 0)
			addTarget(bestExplore.tile, ObjectInstanceID(), bestExplore.value, 0, bestExplore.travel, "explore");
		if(lookingBehind && bestOpenEnd > 0 && lookBehindLogDay != today)
		{
			logAi->info("FairAI %s day %d: %s looks at what lies behind the way out it opened (until day %d): open land towards unseen regions worth up to %.0f",
				me.toString(), today, hero->getNameTranslated(), breakoutPauseUntil, bestOpenEnd);
			lookBehindLogDay = today;
		}
		for(const auto & [guard, value] : behindGuard)
		{
			unlockValue[guard] += value;
			strategicUnlock[guard] += value;
		}
		// Land we have not seen behind a guard is more than the strip the first step reveals: on a
		// map of zones it is a new region with its mines, treasures and perhaps a town.
		if(params.regionValue > 0 && isMain(hero) && params.passage)
		{
			// (passage) A region lies behind a guard when the land we know beyond it runs on into
			// land we have not seen - not when unseen rock surrounds the pocket it watches. How much
			// of a region: the share of the map around that end that is unseen. A guard whose far
			// side we have not seen yet (seen from afar) is taken for such a one until we know better;
			// the hero looks again at every step, long before it stands next to the guard.
			for(const auto & [id, object] : known)
			{
				if(object.type == Obj::MONSTER && reachable(paths, object.visitablePos) && grid.countBox(object.visitablePos, 2) > 0)
					openEnd[id] = std::max(openEnd[id], grid.boxShare(object.visitablePos, 12));
			}
			for(const auto & [guard, share] : openEnd)
			{
				const double region = params.regionValue * std::clamp(share / 0.3, 0.0, 1.0);
				unlockValue[guard] += region;
				strategicUnlock[guard] += region;
			}
		}
		// (breakout) Shut in: two weeks in which next to nothing of the map was uncovered, and no
		// foreign town the hero could go for. Then the guards with unseen land behind them are the way to the
		// rest of the map - to the towns the game is decided by -, and opening one is worth what
		// such a town is worth, the more the later it is. Counted by how much of the map around the
		// land behind the guard is unseen: a pocket of fog is not a region. (Without this an army of
		// 200,000 to 700,000 stayed in its corner for a hundred days: 20 to 49 pit lords "opened"
		// 358 gold and would have cost 1,700 of the army, so they were "not worth it", and so was
		// every other way out.)
		for(const auto & [guard, share] : doorBehind)
			wayOut[guard] = std::max(wayOut[guard], share);
		if(params.breakout && leads(hero) && breakoutDebugDay != today && logAi->isDebugEnabled())
		{
			std::string ways;
			for(const auto & [guard, share] : wayOut)
			{
				auto it = known.find(guard);
				ways += boost::str(boost::format(" [%s: %.2f, fog behind %.0f]") % (it != known.end() ? it->second.visitablePos.toString() : std::string("?")) % share % fogBehind[guard]);
			}
			logAi->debug("FairAI %s day %d: breakout check for %s: exploring stalled %s, slack days %d, a foreign town among the targets %s, ways out:%s", me.toString(), today,
				hero->getNameTranslated(), exploringStalled() ? "yes" : "no", mainSlackDays, foreignTownTargeted ? "yes" : "no", ways.empty() ? " none" : ways);
			breakoutDebugDay = today;
		}
		if(params.breakout >= 2)
		{
			// a guard we counted as a way out is gone: the gap is open, walk through it first
			for(auto id : breakoutExits)
			{
				if(!known.count(id))
				{
					breakoutPauseUntil = today + 3;
					logAi->info("FairAI %s day %d: a way out is open - no further ways out are forced before day %d", me.toString(), today, breakoutPauseUntil);
					if(statsFile)
						stats[phaseOf(today)]["wayOut"] += 1;
					breakoutExits.clear();
					break;
				}
			}
		}
		if(params.breakout && leads(hero) && ((exploringStalled() && !foreignTownTargeted) || (searching && today >= breakoutPauseUntil)))
		{
			const double worth = searchWorth;
			int exits = 0;
			breakoutExits.clear();
			int dear = 0;
			for(const auto & [guard, share] : wayOut)
			{
				const double part = std::clamp(share / 0.3, 0.0, 1.0);
				if(part < 0.2)
					continue;
				if(params.breakout >= 3)
				{
					// (breakout 3) not at any price: a way out that would cost more than a fifth of the
					// army waits until the army has grown (the worth of a town would pay for half of it)
					auto it = known.find(guard);
					// (breakout 4) the guards of a way out are the strong ones, and the square law is no
					// judge of a close fight: 30 ogres and 15 ogre magi cost 48 % of an army of 35,000
					// where it said 5 % - the simulated battle said 58 to 70 %. Level 4 asks the simulator.
					const double cost = it != known.end() ? monsterLoss(hero, it->second, params.breakout >= 4) : -1;
					if(cost < 0 || cost > params.breakoutLoss * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue)
					{
						++dear;
						continue;
					}
				}
				unlockValue[guard] += worth * part;
				strategicUnlock[guard] += worth * part;
				breakoutExits.insert(guard);
				++exits;
			}
			// (breakout 3) a door we can simply walk through is a way out as well
			const double doorPart = std::clamp(unseenShare / 0.3, 0.0, 1.0);
			for(const auto & [door, days] : openDoors)
			{
				auto it = known.find(door);
				if(it == known.end() || doorPart < 0.2)
					continue;
				addTarget(it->second.visitablePos, door, worth * doorPart, 0, days, "door");
				breakoutExits.insert(door);
				++exits;
			}
			if(breakoutLogDay != today)
			{
				if(searching)
					logAi->info("FairAI %s day %d: %s looks for the next town (%s, %.0f%% of the map unseen, %.0f waiting at home): %d way(s) out worth up to %.0f%s",
						me.toString(), today, hero->getNameTranslated(), foreignTownTargeted ? "on campaign" : "none among its targets", 100.0 * unseenShare, waitingAtHome, exits, worth,
						dear > 0 ? boost::str(boost::format(" (%d more would cost too much yet)") % dear) : std::string());
				else
					logAi->info("FairAI %s day %d: %s is shut in (next to nothing uncovered lately, no foreign town to go for, %d slack day(s)): %d guard(s) with unseen land behind count as ways out worth up to %.0f%s",
						me.toString(), today, hero->getNameTranslated(), mainSlackDays, exits, worth, dear > 0 ? boost::str(boost::format(", %d more would cost too much yet") % dear) : std::string());
				breakoutLogDay = today;
			}
		}
		if(params.regionValue > 0 && isMain(hero) && !params.passage)
		{
			// A guard seen only from afar has no known tile behind it to stand on: count what shows
			// from the guard's own place once it is gone. (Without this the guards on the way out
			// of our corner opened "nothing", and the hero stayed in two regions for four weeks.)
			for(const auto & [id, object] : known)
			{
				if(object.type == Obj::MONSTER && reachable(paths, object.visitablePos))
					fogBehind[id] = std::max(fogBehind[id], grid.count(object.visitablePos, radius));
			}
			for(const auto & [guard, tiles] : fogBehind)
			{
				if(tiles >= 30)
				{
					// no fading with the days: early it is mines and treasure, late it is where the enemy lives
					unlockValue[guard] += params.regionValue * std::min(1.0, tiles / 60.0);
					strategicUnlock[guard] += params.regionValue * std::min(1.0, tiles / 60.0);
				}
			}
		}
	}

	// ---- monsters: experience plus whatever they block
	double weakestRejected = 0;
	std::string forecastLines;
	std::string guardLines;
	const bool guardLog = isMain(hero) && logAi->isDebugEnabled() && guardLogDay != today;
	for(const auto & [id, object] : known)
	{
		if(object.type != Obj::MONSTER || !object.army || failedToday.count({heroNum, object.visitablePos}))
			continue;
		const CGPathNode * node = reachable(paths, object.visitablePos);
		if(!node)
			continue;
		double danger = observedDanger(object);
		// (breakout 4) a guard that counts as a way out is fought on the simulator's word (see there)
		double loss = monsterLoss(hero, object, params.breakout >= 4 && breakoutExits.count(id) > 0);
		if(params.fightForecast && isMain(hero) && forecastLogDay != today * 100 + heroNum % 100 && logAi->isDebugEnabled() && object.army->units.size() == 1)
		{
			// once a day: how the main hero judges every guard in reach (diagnostics)
			const ObservedStack & seen = object.army->units.front();
			if(params.fightForecast >= 2 && seen.creature.hasValue())
			{
				const bool below = params.worstCaseRatio > 0 && static_cast<double>(hero->getArmyStrength()) < params.worstCaseRatio * static_cast<double>(object.army->maxStrength());
				const MonsterJudgement judged = judgeMonster(hero, seen, below);
				const double old = below ? -1 : expectedLoss(hero, danger);
				forecastLines += boost::str(boost::format(" [%s %d-%d: against %d %s %.2f, against %d %s %.2f, scaled by %.2f: %.3f of the army, %s; old rule %s, loss %.0f against %.0f]")
					% seen.creature.toCreature()->getJsonKey() % seen.minCount % seen.maxCount
					% judged.lowCount % (judged.low.win ? "win" : "LOSE") % judged.low.lossShare
					% judged.highCount % (judged.high.win ? "win" : "LOSE") % judged.high.lossShare
					% judged.calibration % judged.share % judged.verdict % (old < 0 ? "no" : "yes") % loss % old);
			}
			else
			{
				const double middle = 0.5 * (seen.minCount + seen.maxCount);
				const int likelyCount = std::max(1, static_cast<int>(std::lround((1 - params.dangerUpperWeight) * middle + params.dangerUpperWeight * seen.maxCount)));
				const FightForecast likely = forecastMonsterFight(hero, seen.creature, likelyCount);
				const FightForecast worst = forecastMonsterFight(hero, seen.creature, seen.maxCount);
				forecastLines += boost::str(boost::format(" [%s %d-%d: %s %.2f, at most %s %.2f, old rule %s, loss %.0f%s]")
					% (seen.creature.hasValue() ? seen.creature.toCreature()->getJsonKey() : std::string("?")) % seen.minCount % seen.maxCount
					% (likely.win ? "win" : "LOSE") % likely.lossShare % (worst.win ? "win" : "LOSE") % worst.lossShare
					% (expectedLoss(hero, danger) < 0 ? "no" : "yes") % loss
					% (likely.mana + worst.mana > 0 ? boost::str(boost::format(", spells for %d to %d points") % likely.mana % worst.mana) : std::string()));
			}
		}
		if(loss < 0 && (weakestRejected == 0 || danger < weakestRejected))
			weakestRejected = danger;
		if(loss < 0)
		{
			++rejected["monster too strong"];
			if(guardLog && object.army->units.size() == 1 && object.army->units.front().creature.hasValue())
			{
				const ObservedStack & seen = object.army->units.front();
				guardLines += boost::str(boost::format(" [%s %d-%d at %s: danger %.0f -> too strong]")
					% seen.creature.toCreature()->getJsonKey() % seen.minCount % seen.maxCount % object.visitablePos.toString() % danger);
			}
			continue;
		}
		if(unsafe(node, ObjectInstanceID()))
		{
			++rejected["monster unsafe"];
			continue;
		}
		double hp = 0;
		for(const auto & unit : object.army->units)
		{
			if(unit.creature.hasValue())
				hp += unit.creature.toCreature()->getMaxHealth() * 0.5 * (unit.minCount + unit.maxCount);
		}
		double value = hp * expValue(hero);
		if(leads(hero))
			value += params.guardKillValue * danger * params.goldPerArmyValue;
		// a guard next to unexplored land probably blocks a way into it
		value += params.guardFogFactor * tileValue * revealGrid.count(object.visitablePos, hero->getSightRadius());
		// on campaign a guard is worth a detour only for what the campaign is about: towns and the way on
		const auto & opened = campaign(hero) && node->cost > 0.5 ? strategicUnlock : unlockValue;
		const auto unlock = opened.find(id);
		const double opens = unlock != opened.end() ? params.unlockFactor * unlock->second : 0.0;
		value += opens;
		if(guardLog && object.army->units.size() == 1 && object.army->units.front().creature.hasValue())
		{
			// once a day: how the main hero judges every guard in reach (diagnostics)
			const ObservedStack & seen = object.army->units.front();
			guardLines += boost::str(boost::format(" [%s %d-%d at %s, %.1f days: danger %.0f, loss %.0f, experience %.0f, opens %.0f -> %s]")
				% seen.creature.toCreature()->getJsonKey() % seen.minCount % seen.maxCount % object.visitablePos.toString() % node->cost
				% danger % loss % (hp * expValue(hero)) % opens
				% (value <= loss ? "not worth it" : "target"));
		}
		if(value <= loss)
			continue;
		addTarget(object.visitablePos, id, value, loss, node->cost, "monster");
	}

	if(guardLog)
	{
		std::string reasons;
		for(const auto & [why, count] : rejected)
			reasons += " " + why + "=" + std::to_string(count);
		logAi->debug("FairAI %s day %d: %s (strength %.0f) guards in reach:%s; left aside:%s", me.toString(), today, hero->getNameTranslated(), ownStrength,
			guardLines.empty() ? " none" : guardLines, reasons.empty() ? " nothing" : reasons);
		guardLogDay = today;
	}
	if(!forecastLines.empty())
	{
		logAi->debug("FairAI %s day %d: %s (army %d) forecasts:%s", me.toString(), today, hero->getNameTranslated(), static_cast<int>(hero->getArmyStrength()), forecastLines);
		forecastLogDay = today * 100 + heroNum % 100;
	}
	if(out.empty())
	{
		std::string reasons;
		for(const auto & [why, count] : rejected)
			reasons += " " + why + "=" + std::to_string(count);
		int reachableTiles = 0, freeTiles = 0;
		const int3 size = cb->getMapSize();
		for(int z = 0; z < size.z; ++z)
			for(int x = 0; x < size.x; ++x)
				for(int y = 0; y < size.y; ++y)
				{
					if(reachable(paths, int3(x, y, z)))
						++reachableTiles;
					if(reachable(freePaths, int3(x, y, z)))
						++freeTiles;
				}
		logAi->debug("FairAI %s: %s (strength %.0f) has no target; weakest guard in reach %.0f; reachable tiles %d (ignoring guards %d); rejected:%s", me.toString(),
			hero->getNameTranslated(), ownStrength, weakestRejected, reachableTiles, freeTiles, reasons);
	}

	// ---- standing where a stronger enemy hero can reach us: get away (own towns preferred).
	// Every target above ends the day on a tile that is not threatened, so with the anti-oscillation
	// rules the retreat is the fallback, not a competitor that calls the hero back after every step.
	if(fortressOutside)
	{
		// the town of ours where the hero stands strongest against that army, by the days it takes to get there
		// (safeParts: a heavy part outside a town - any town of ours, the nearest first)
		// (townHeroes) An army on its way to another town of ours - its new post, or the refuge from a
		// town it could not hold - goes on: the town it left a step ago is the nearest, and without
		// this it walked straight back in.
		const CGTownInstance * goalTown = nullptr;
		if(auto goal = postOf.find(hero->id); params.townHeroes && heavyPart && goal != postOf.end())
		{
			for(const auto * town : perception.ownTowns())
			{
				const CGPathNode * node = town->id == goal->second ? paths.getPathInfo(town->visitablePos()) : nullptr;
				if(node && node->reachable() && !failedToday.count({heroNum, town->visitablePos()}))
					goalTown = town;
			}
		}
		for(const auto * town : perception.ownTowns())
		{
			const CGPathNode * node = paths.getPathInfo(town->visitablePos());
			if(!node || !node->reachable() || failedToday.count({heroNum, town->visitablePos()}))
				continue;
			if(goalTown && town != goalTown)
				continue;
			double holds = defenceAt(hero, town->visitablePos()) * tolerance(hero) >= outmatched ? 2.0 : 1.0;
			// (townHeroes) an army that left a town it could not hold does not walk back into it
			if(params.townHeroes && heavyPart && !goalTown && threatAt(town->visitablePos(), ObjectInstanceID(), true) > tolerance(hero) * defenceAt(hero, town->visitablePos()))
				holds = 0.25;
			addTarget(town->visitablePos(), town->id, holds * 0.5 * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue, 0, node->cost, "fortress");
		}
	}
	if((out.empty() || !params.antiOscillation) && threatAt(heroPos, ObjectInstanceID(), cautious) > tolerance(hero) * defenceAt(hero, heroPos))
	{
		const int3 size = cb->getMapSize();
		Target bestRetreat;
		bestRetreat.score = 0;
		for(int z = 0; z < size.z; ++z)
			for(int x = 0; x < size.x; ++x)
				for(int y = 0; y < size.y; ++y)
				{
					int3 tile(x, y, z);
					const CGPathNode * node = paths.getPathInfo(tile);
					if(!node || !node->reachable() || node->turns > 0)
						continue;
					bool ownTown = false;
					for(const auto * town : perception.ownTowns())
						ownTown = ownTown || town->visitablePos() == tile;
					const bool haven = sanctuaries.count(tile) > 0 && (node->action == EPathNodeAction::VISIT || node->action == EPathNodeAction::NORMAL);
					if(node->action != EPathNodeAction::NORMAL && !ownTown && !haven)
						continue;
					double margin = defenceAt(hero, tile) * tolerance(hero) - threatAt(tile, ObjectInstanceID(), cautious);
					if(margin <= 0)
						continue;
					double value = heroWorth * params.retreatValue * (ownTown ? 1.2 : 1.0);
					double score = value / (node->cost + params.travelOffset);
					if(score > bestRetreat.score)
					{
						bestRetreat.tile = tile;
						bestRetreat.value = value;
						bestRetreat.travel = node->cost;
						bestRetreat.score = score;
					}
				}
		if(bestRetreat.score > 0)
		{
			if(sanctuaries.count(bestRetreat.tile))
				logAi->info("FairAI %s day %d: %s (strength %.0f) takes shelter in the Sanctuary at %s from an army of %.0f", me.toString(), today,
					hero->getNameTranslated(), ownStrength, bestRetreat.tile.toString(), threatAt(heroPos, ObjectInstanceID(), cautious));
			addTarget(bestRetreat.tile, ObjectInstanceID(), bestRetreat.value, 0, bestRetreat.travel, "retreat");
		}
		else
		{
			// nowhere is safe: get as far away from the threats as we can today
			auto threatDistance = [&](const int3 & tile)
			{
				double nearest = 1e9;
				for(const auto & t : threats)
				{
					if((!cautious ? t.age <= 2 : true) && t.strength > tolerance(hero) * ownStrength)
						nearest = std::min(nearest, enemyDistance(t.hero, t.pos, t.strength, tile, 60));
				}
				return nearest;
			};
			const double here = threatDistance(heroPos);
			Target flee;
			double bestDistance = here + params.fleeGain;
			for(int z = 0; z < size.z; ++z)
				for(int x = 0; x < size.x; ++x)
					for(int y = 0; y < size.y; ++y)
					{
						int3 tile(x, y, z);
						const CGPathNode * node = paths.getPathInfo(tile);
						if(!node || !node->reachable() || node->turns > 0 || node->action != EPathNodeAction::NORMAL)
							continue;
						double d = threatDistance(tile);
						if(d > bestDistance)
						{
							bestDistance = d;
							flee.tile = tile;
							flee.travel = node->cost;
						}
					}
			// (standFast) Behind the walls of a town of ours, or with such a town within today's march,
			// the open field is no refuge: every tile we could flee to is in the enemy's reach as well.
			bool sheltered = false;
			if(params.standFast)
			{
				sheltered = defenceAt(hero, heroPos) > 1.05 * ownStrength;
				if(!sheltered)
				{
					const CGTownInstance * shelter = nullptr;
					double shelterDefence = 1.05 * ownStrength;
					float shelterCost = 0;
					for(const auto * town : perception.ownTowns())
					{
						const CGPathNode * node = paths.getPathInfo(town->visitablePos());
						if(!node || !node->reachable() || node->turns > 0 || failedToday.count({heroNum, town->visitablePos()}))
							continue;
						if(const auto * visitor = town->getVisitingHero(); visitor && visitor != hero)
							continue; // the gate is taken
						const double defence = defenceAt(hero, town->visitablePos());
						if(defence > shelterDefence)
						{
							shelter = town;
							shelterDefence = defence;
							shelterCost = node->cost;
						}
					}
					if(shelter)
					{
						sheltered = true;
						addTarget(shelter->visitablePos(), shelter->id, heroWorth * params.retreatValue * 0.6, 0, shelterCost, "home");
					}
				}
			}
			if(bestDistance > here + params.fleeGain && !sheltered)
				addTarget(flee.tile, ObjectInstanceID(), heroWorth * params.retreatValue * 0.5, 0, flee.travel, "flee");
			logAi->debug("FairAI %s: %s threatened (%.0f vs defence %.0f) and no safe tile today; threat distance here %.1f, best reachable %.1f",
				me.toString(), hero->getNameTranslated(), threatAt(heroPos, ObjectInstanceID(), cautious), defenceAt(hero, heroPos), here,
				bestDistance > here + params.fleeGain ? bestDistance : here);
		}
	}

	return out;
}

// ======================================================================= queries

double AdventureAI::skillValue(const CGHeroInstance * hero, SecondarySkill skill, int level) const
{
	const bool leads = isMain(hero) || !ownHero(mainHero);
	if(leads)
		return skillValueAs(hero, skill, level, true);
	// the others collect, ferry and scout - but one of them may lead the army one day
	return skillValueAs(hero, skill, level, false) + 0.2 * skillValueAs(hero, skill, level, true);
}

double AdventureAI::skillValueAs(const CGHeroInstance * hero, SecondarySkill skill, int level, bool fighter) const
{
	// What holding `skill` at `level` is worth to this hero, in gold. Read from the bonuses the game
	// attaches to the skill, so changed values and new skills of mods are covered. A hero who leads
	// the army needs other things than one who collects, ferries troops or scouts.
	const CSkill * info = skill.toSkill();
	if(!hero || !info || level < 1 || level > 3)
		return 0;
	const bool caster = hero->hasSpellbook();
	double shooters = 0;
	double living = 0;
	double army = 0;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(!stack || !stack->getCreature())
			continue;
		const double v = stackValue(stack.get());
		army += v;
		if(stack->getCreature()->hasBonusOfType(BonusType::SHOOTER))
			shooters += v;
		if(!stack->getCreature()->hasBonusOfType(BonusType::UNDEAD) && !stack->getCreature()->hasBonusOfType(BonusType::NON_LIVING))
			living += v;
	}
	// today's army is a sample of the hero's career: never judge a skill by an extreme mix
	const double rangedShare = army > 0 ? std::clamp(shooters / army, 0.15, 0.6) : 0.3;
	const double livingShare = army > 0 ? std::clamp(living / army, 0.2, 1.0) : 0.8;
	// A skill is chosen for the hero's whole career: the leader's skills are valued for the army it
	// will lead (150k and more from the second month on - one percent of that is 900 in gold), not
	// for today's. With the first figures (140 a percent, the worth of a 23k army) a leader took
	// Estates before Offence.
	const double perPercent = fighter ? 900.0 : 15.0; // one percent more fighting strength
	const double perMove = fighter ? 600.0 : 120.0;   // one percent more movement
	const double magicScale = fighter ? 3.0 : 1.0;    // spells work on the army's scale, too
	const bool necromancer = hero->getFactionID().hasValue() && hero->getFactionID().toEntity(LIBRARY)->getJsonKey() == "necropolis";

	double value = 0;
	for(const auto & bonus : info->at(level).effects)
	{
		const double v = bonus->val;
		switch(bonus->type)
		{
		case BonusType::MOVEMENT:
			if(bonus->subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementLand))
				value += v * perMove;
			else
				value += std::min(v, 150.0) * (seaWanted ? 25.0 : 4.0);
			break;
		case BonusType::ROUGH_TERRAIN_DISCOUNT:
			value += v * perMove * 0.15;
			break;
		case BonusType::SIGHT_RADIUS:
			value += v * (fighter ? 250.0 : 700.0);
			break;
		case BonusType::GENERATE_RESOURCE:
			// income is the same whoever holds it - but the main hero's eight places are worth more
			value += v * 20.0 * (fighter ? 0.5 : 1.0);
			break;
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
			value += v * perPercent * (bonus->subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeRanged) ? rangedShare : 1.0 - rangedShare);
			break;
		case BonusType::GENERAL_DAMAGE_REDUCTION:
			value += v * 1.15 * perPercent;
			break;
		case BonusType::MORALE:
			value += v * 4.2 * livingShare * perPercent;
			break;
		case BonusType::LUCK:
			value += v * 4.2 * perPercent;
			break;
		case BonusType::MAGIC_RESISTANCE:
			value += v * 0.25 * perPercent;
			break;
		case BonusType::UNDEAD_RAISE_PERCENTAGE:
			value += v * (fighter ? (necromancer ? 800.0 : 240.0) : 20.0);
			break;
		case BonusType::MAX_LEARNABLE_SPELL_LEVEL:
		{
			// third level spells matter most; the fifth guild level is rarely built
			const double steps = v >= 3 ? 3600 : (v >= 2 ? 3000 : (v >= 1 ? 2000 : 0));
			value += fighter ? magicScale * steps * (caster ? 1.0 : 0.5) : 150.0 * v;
			break;
		}
		case BonusType::MAGIC_SCHOOL_SKILL:
		{
			double school = 2000;
			if(bonus->subtype == BonusSubtypeID(SpellSchool::EARTH))
				school = 3000;
			else if(bonus->subtype == BonusSubtypeID(SpellSchool::AIR))
				school = 2600;
			else if(bonus->subtype == BonusSubtypeID(SpellSchool::WATER))
				school = 1500;
			else if(bonus->subtype == BonusSubtypeID(SpellSchool::FIRE))
				school = 1400;
			value += v * (fighter ? magicScale * school * (caster ? 1.0 : 0.4) : 150.0);
			break;
		}
		case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
			value += v * 20.0 * (fighter ? (caster ? 1.0 : 0.2) : 0.05);
			break;
		case BonusType::MANA_REGENERATION:
			value += v * (fighter && caster ? 150.0 : 20.0);
			break;
		case BonusType::SPELL_DAMAGE:
			value += v * (fighter && caster ? 40.0 : 2.0);
			break;
		case BonusType::WANDERING_CREATURES_JOIN_BONUS:
			value += v * (fighter ? 500.0 : 100.0);
			break;
		case BonusType::SURRENDER_DISCOUNT:
			value += v * 2.0;
			break;
		case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
			value += v * (fighter ? 60.0 : 5.0);
			break;
		case BonusType::BEFORE_BATTLE_REPOSITION:
			value += v * (fighter ? 80.0 : 5.0); // FairAI does not arrange its troops before a battle yet
			break;
		case BonusType::BEFORE_BATTLE_REPOSITION_BLOCK:
			value += v * (fighter ? 40.0 : 2.0);
			break;
		case BonusType::CATAPULT_EXTRA_SHOTS:
			value += v * (fighter ? 300.0 : 10.0);
			break;
		case BonusType::BONUS_DAMAGE_CHANCE:
			value += v * (fighter ? 6.0 : 0.5);
			break;
		case BonusType::SPECIFIC_SPELL_POWER:
			value += v * (fighter ? 3.0 : 0.3);
			break;
		case BonusType::LEARN_BATTLE_SPELL_CHANCE:
			value += v * (fighter && caster ? 3.0 : 0.3);
			break;
		case BonusType::LEARN_MEETING_SPELL_LIMIT:
			value += v * 60.0;
			break;
		case BonusType::MANUAL_CONTROL:
		case BonusType::IMPROVED_NECROMANCY:
		case BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT:
		case BonusType::HERO_GRANTS_ATTACKS:
		case BonusType::BONUS_DAMAGE_PERCENTAGE:
			break; // side effects of a skill valued by its main bonus
		default:
			value += fighter ? 150.0 : 50.0; // a skill FairAI does not know: assume it does something
			break;
		}
	}
	return value;
}

int AdventureAI::chooseSkill(const CGHeroInstance * hero, const std::vector<SecondarySkill> & skills) const
{
	if(skills.size() < 2)
		return 0;
	if(params.skillChoice && hero)
	{
		const int limit = static_cast<int>(cb->getSettings().getInteger(EGameSettings::HEROES_SKILL_PER_HERO));
		const double used = static_cast<double>(hero->secSkills.size()) / std::max(1, limit);
		int best = 0;
		double bestGain = -1e18;
		std::string text;
		for(size_t i = 0; i < skills.size(); ++i)
		{
			const int have = hero->getSecSkillLevel(skills[i]);
			double gain = skillValue(hero, skills[i], std::min(3, have + 1)) - skillValue(hero, skills[i], have);
			if(have == 0)
			{
				// a new skill takes one of the few places for good: count what it can grow into,
				// and the better skill it may keep out later
				gain += 0.35 * (skillValue(hero, skills[i], 3) - skillValue(hero, skills[i], 1));
				gain -= used * used * 1500.0;
			}
			text += boost::str(boost::format(" %s %d->%d: %.0f") % skills[i].toSkill()->getJsonKey() % have % std::min(3, have + 1) % gain);
			if(gain > bestGain)
			{
				bestGain = gain;
				best = static_cast<int>(i);
			}
		}
		logAi->info("FairAI %s: %s%s learns%s -> %s", me.toString(), hero->getNameTranslated(), isMain(hero) ? " (main)" : "", text, skills[best].toSkill()->getJsonKey());
		return best;
	}
	int best = 0;
	double bestScore = -1e9;
	for(size_t i = 0; i < skills.size(); ++i)
	{
		int s = skills[i].getNum();
		double score = (s >= 0 && s < static_cast<int>(SKILL_PRIORITY.size())) ? SKILL_PRIORITY[s] : 3;
		if(hero)
		{
			if(hero->getSecSkillLevel(skills[i]) > 0)
				score += 1.5; // upgrading a skill we chose before
			if(s >= FIRST_MAGIC_SCHOOL && s <= LAST_MAGIC_SCHOOL && !hero->hasSpellbook())
				score -= 2;
		}
		if(score > bestScore)
		{
			bestScore = score;
			best = static_cast<int>(i);
		}
	}
	return best;
}

int AdventureAI::chooseSelection(const std::vector<Component> & components) const
{
	if(components.empty())
		return 0;
	int goldIndex = -1;
	int expIndex = -1;
	for(size_t i = 0; i < components.size(); ++i)
	{
		if(components[i].type == ComponentType::EXPERIENCE)
			expIndex = static_cast<int>(i);
		if(components[i].type == ComponentType::RESOURCE && components[i].subType.as<GameResID>() == GameResID::GOLD)
			goldIndex = static_cast<int>(i);
	}
	if(goldIndex >= 0 && expIndex >= 0)
	{
		// main hero takes experience, collectors take gold
		bool main = activeHero.load() >= 0 && ObjectInstanceID(activeHero.load()) == mainHero;
		return 1 + (main ? expIndex : goldIndex);
	}
	return static_cast<int>(components.size());
}

// ======================================================================= artifacts

double AdventureAI::magicReliance(const CGHeroInstance * hero) const
{
	// how much of this hero's fighting rests on spells: 0 a pure warrior, 1 a pure spell caster
	if(!hero || !hero->hasSpellbook())
		return 0.05;
	double reliance = 0.15 + 0.03 * std::min(10, hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)) + 0.02 * std::min(10, hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE));
	for(const auto & [skill, level] : hero->secSkills)
	{
		const int s = skill.getNum();
		if(s >= FIRST_MAGIC_SCHOOL && s <= LAST_MAGIC_SCHOOL)
			reliance += 0.07 * level;
		if(skill == SecondarySkill::WISDOM)
			reliance += 0.05 * level;
	}
	return std::clamp(reliance, 0.05, 1.0);
}

double AdventureAI::artifactValue(const CGHeroInstance * hero, const CArtifact * art, bool fighter) const
{
	// What carrying this artifact is worth to this hero, in gold; negative: it does harm (a hero
	// living on spells with the Orb of Inhibition). Read from the artifact's bonuses, so the same
	// rule covers every artifact of the game and of mods.
	if(!hero || !art)
		return 0;
	const bool caster = hero->hasSpellbook();
	const double reliance = magicReliance(hero);
	double shooters = 0;
	double living = 0;
	double army = 0;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(!stack || !stack->getCreature())
			continue;
		const double v = stackValue(stack.get());
		army += v;
		if(stack->getCreature()->hasBonusOfType(BonusType::SHOOTER))
			shooters += v;
		if(!stack->getCreature()->hasBonusOfType(BonusType::UNDEAD) && !stack->getCreature()->hasBonusOfType(BonusType::NON_LIVING))
			living += v;
	}
	const double rangedShare = army > 0 ? std::clamp(shooters / army, 0.15, 0.6) : 0.3;
	const double livingShare = army > 0 ? std::clamp(living / army, 0.2, 1.0) : 0.8;
	const double perPercent = fighter ? 140.0 : 15.0;
	const double perMove = fighter ? 200.0 : 120.0;
	const double skillPoint = fighter ? params.primarySkillValueMain : params.primarySkillValueOther;

	double value = 0;
	int known = 0;
	auto add = [&](const Bonus & bonus)
	{
		const double v = bonus.val;
		const double scope = bonus.limiter ? 0.3 : 1.0; // only some troops profit (dragons, one creature kind...)
		++known;
		switch(bonus.type)
		{
		case BonusType::PRIMARY_SKILL:
			if(bonus.subtype == BonusSubtypeID(PrimarySkill::ATTACK) || bonus.subtype == BonusSubtypeID(PrimarySkill::DEFENSE))
				value += v * (params.primaryByArmy ? primaryPointValue(hero, bonus.subtype == BonusSubtypeID(PrimarySkill::ATTACK) ? 0 : 1, fighter) : skillPoint) * scope;
			else if(bonus.subtype == BonusSubtypeID(PrimarySkill::SPELL_POWER))
				value += v * skillPoint * (0.25 + 0.75 * reliance);
			else
				value += v * skillPoint * (0.2 + 0.6 * reliance);
			break;
		case BonusType::MOVEMENT:
		{
			const double percent = bonus.valType == BonusValueType::ADDITIVE_VALUE ? v / 17.0 : v;
			if(bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementLand))
				value += percent * perMove;
			else
				value += std::min(percent, 150.0) * (seaWanted ? 25.0 : 4.0);
			break;
		}
		case BonusType::FLYING_MOVEMENT:
			value += fighter ? 6000 : 4000;
			break;
		case BonusType::WATER_WALKING:
			value += seaWanted ? 4000 : 1200;
			break;
		case BonusType::FREE_SHIP_BOARDING:
		case BonusType::WHIRLPOOL_PROTECTION:
			value += seaWanted ? 1500 : 100;
			break;
		case BonusType::SIGHT_RADIUS:
			value += v * (fighter ? 250.0 : 700.0);
			break;
		case BonusType::GENERATE_RESOURCE:
		{
			GameResID res = bonus.subtype.as<GameResID>();
			value += v * (res.hasValue() ? resourceValue(res) : 1.0) * 20.0; // the same for every bearer
			break;
		}
		case BonusType::MORALE:
			if(bonus.valType == BonusValueType::INDEPENDENT_MIN)
				break; // nobody has morale: cuts both ways
			value += v * 4.2 * livingShare * perPercent;
			break;
		case BonusType::LUCK:
			if(bonus.valType == BonusValueType::INDEPENDENT_MIN)
				break;
			value += v * 4.2 * perPercent;
			break;
		case BonusType::STACKS_SPEED:
			value += v * 8.0 * perPercent * scope;
			break;
		case BonusType::STACK_HEALTH:
			value += (bonus.valType == BonusValueType::PERCENT_TO_BASE ? v : v * 3.5) * perPercent * scope;
			break;
		case BonusType::HP_REGENERATION:
			value += 3.0 * perPercent;
			break;
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
			value += v * perPercent * (bonus.subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeRanged) ? rangedShare : 1.0 - rangedShare);
			break;
		case BonusType::GENERAL_DAMAGE_REDUCTION:
			value += v * 1.15 * perPercent;
			break;
		case BonusType::NO_DISTANCE_PENALTY:
			value += 25.0 * rangedShare * perPercent;
			break;
		case BonusType::NO_WALL_PENALTY:
		case BonusType::FREE_SHOOTING:
			value += 8.0 * rangedShare * perPercent;
			break;
		case BonusType::MAGIC_RESISTANCE:
			if(bonus.valType != BonusValueType::INDEPENDENT_MIN)
				value += v * 0.25 * perPercent;
			break;
		case BonusType::SPELL_DAMAGE:
			value += v * (fighter ? 60.0 * reliance : 2.0);
			break;
		case BonusType::SPELL_DURATION:
			value += v * (fighter ? 300.0 * reliance : 10.0);
			break;
		case BonusType::MANA_REGENERATION:
			value += v * (fighter ? 300.0 * reliance : 20.0);
			break;
		case BonusType::MANA_PERCENTAGE_REGENERATION:
			value += fighter ? 3000.0 * reliance : 100.0;
			break;
		case BonusType::SPELLS_OF_SCHOOL:
			value += fighter && caster ? 2500.0 + 4000.0 * reliance : 300.0;
			break;
		case BonusType::SPELLS_OF_LEVEL:
			value += fighter && caster ? 2000.0 + 3000.0 * reliance : 300.0;
			break;
		case BonusType::SPELL:
			value += fighter && caster ? 500.0 : 100.0;
			break;
		case BonusType::OPENING_BATTLE_SPELL:
			value += fighter ? 600.0 : 50.0;
			break;
		case BonusType::BLOCK_ALL_MAGIC:
			// nobody casts in battle: good for a warrior facing wizards, ruin for a wizard
			value += fighter ? 6000.0 * (0.5 - reliance) : 0.0;
			break;
		case BonusType::BLOCK_MAGIC_ABOVE:
			value += fighter ? 3000.0 * (0.5 - reliance) : 0.0;
			break;
		case BonusType::SPELL_IMMUNITY:
		case BonusType::LEVEL_SPELL_IMMUNITY:
		case BonusType::MIND_IMMUNITY:
		case BonusType::SPELL_RESISTANCE_AURA:
			value += fighter ? 300.0 : 30.0;
			break;
		case BonusType::NEGATE_ALL_NATURAL_IMMUNITIES:
			value += fighter ? 1500.0 * reliance : 0.0;
			break;
		case BonusType::UNDEAD_RAISE_PERCENTAGE:
			value += v * (fighter ? 150.0 : 10.0);
			break;
		case BonusType::CREATURE_GROWTH_PERCENT:
			value += 1500;
			break;
		case BonusType::SURRENDER_DISCOUNT:
			value += v * 2.0;
			break;
		case BonusType::LEARN_BATTLE_SPELL_CHANCE:
			value += v * (fighter && caster ? 3.0 : 0.3);
			break;
		case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
			value += v * (fighter ? 60.0 : 5.0);
			break;
		case BonusType::WANDERING_CREATURES_JOIN_BONUS:
			value += v * (fighter ? 500.0 : 100.0);
			break;
		case BonusType::BATTLE_NO_FLEEING:
		case BonusType::IMPROVED_NECROMANCY:
		case BonusType::NONEVIL_ALIGNMENT_MIX:
			break; // side effects, or cutting both ways
		default:
			--known; // unknown to FairAI: the price decides below
			break;
		}
	};
	for(const auto & bonus : art->getExportedBonusList())
		add(*bonus);
	for(const auto & bonus : art->instanceBonuses)
		add(*bonus);
	// what the bonuses do not tell: fall back on the price the game gives the artifact
	if(known == 0)
		return art->getPrice() * (fighter ? 0.25 : 0.05);
	return value;
}

void AdventureAI::arrangeArtifacts(const CGHeroInstance * hero)
{
	// Requests are carried out before the call returns (waitTillRealize). No waitTillFree here: this
	// also runs while two heroes meet, when the meeting itself is the open question we would wait for.
	if(!params.artifactCare || !hero)
		return;
	const ObjectInstanceID id = hero->id;
	const bool fighter = isMain(hero) || !ownHero(mainHero);
	auto usable = [](ArtifactPosition slot)
	{
		return slot != ArtifactPosition::SPELLBOOK && !(slot >= ArtifactPosition::MACH1 && slot <= ArtifactPosition::MACH4);
	};
	auto worth = [&](const CGHeroInstance * who, const CArtifactInstance * art)
	{
		// the others carry what helps their errands - and a little of what a leader would want
		const double own = artifactValue(who, art->getType(), fighter);
		return fighter ? own : own + 0.2 * std::max(0.0, artifactValue(who, art->getType(), true));
	};
	for(int round = 0; round < 24; ++round)
	{
		gateway.checkInterruption();
		hero = ownHero(id);
		if(!hero)
			return;
		bool changed = false;

		// 1. take off what does harm
		for(const auto & [slot, info] : hero->artifactsWorn)
		{
			const CArtifactInstance * art = info.getArt();
			if(!art || info.locked || !usable(slot) || art->getType()->isBig())
				continue;
			if(worth(hero, art) < -1 && ArtifactUtils::isBackpackFreeSlots(hero))
			{
				logAi->info("FairAI %s: %s takes off %s", me.toString(), hero->getNameTranslated(), art->getType()->getJsonKey());
				cb->swapArtifacts(ArtifactLocation(id, slot), ArtifactLocation(id, ArtifactPosition(ArtifactPosition::BACKPACK_START + static_cast<int>(hero->artifactsInBackpack.size()))));
				changed = true;
				break;
			}
		}
		if(changed)
			continue;

		// 2. wear the best of what is in the backpack
		for(size_t i = 0; i < hero->artifactsInBackpack.size() && !changed; ++i)
		{
			const CArtifactInstance * art = hero->artifactsInBackpack[i].getArt();
			if(!art)
				continue;
			const double mine = worth(hero, art);
			if(mine <= 1)
				continue;
			const auto & possible = art->getType()->getPossibleSlots();
			auto slots = possible.find(ArtBearer::HERO);
			if(slots == possible.end())
				continue;
			ArtifactPosition bestSlot = ArtifactPosition::PRE_FIRST;
			double bestGain = 50; // not worth shuffling for less
			for(const ArtifactPosition & slot : slots->second)
			{
				if(!usable(slot) || !art->getType()->canBePutAt(hero, slot, true))
					continue;
				const CArtifactInstance * worn = hero->getArt(slot, false);
				double gain = mine;
				if(worn)
				{
					auto wornInfo = hero->artifactsWorn.find(slot);
					if(wornInfo != hero->artifactsWorn.end() && wornInfo->second.locked)
						continue; // part of an assembled set
					if(worn->getType()->isBig())
						continue;
					gain = mine - worth(hero, worn);
				}
				if(gain > bestGain)
				{
					bestGain = gain;
					bestSlot = slot;
				}
			}
			if(bestSlot != ArtifactPosition::PRE_FIRST)
			{
				logAi->info("FairAI %s: %s puts on %s (gain %.0f)", me.toString(), hero->getNameTranslated(), art->getType()->getJsonKey(), bestGain);
				cb->swapArtifacts(ArtifactLocation(id, ArtifactPosition(ArtifactPosition::BACKPACK_START + static_cast<int>(i))), ArtifactLocation(id, bestSlot));
				changed = true;
			}
		}
		if(changed)
			continue;

		// 3. sets: assemble when the whole is worth more than its parts
		for(const auto & [slot, info] : hero->artifactsWorn)
		{
			const CArtifactInstance * art = info.getArt();
			if(!art || info.locked || !usable(slot))
				continue;
			for(const CArtifact * combined : ArtifactUtils::assemblyPossibilities(hero, art->getTypeId(), true))
			{
				double parts = 0;
				for(const CArtifact * part : combined->getConstituents())
					parts += std::max(0.0, artifactValue(hero, part, fighter));
				if(artifactValue(hero, combined, fighter) + 0.25 * combined->getPrice() < parts)
					continue;
				logAi->info("FairAI %s: %s assembles %s", me.toString(), hero->getNameTranslated(), combined->getJsonKey());
				cb->assembleArtifacts(id, slot, true, combined->getId());
				changed = true;
				break;
			}
			if(changed)
				break;
		}
		if(!changed)
			return;
	}
}

void AdventureAI::exchangeArtifacts(const CGHeroInstance * leader, const CGHeroInstance * other)
{
	// Two of our heroes meet: every artifact goes where it does more good. The leader of the army
	// gets what makes it fight, move and cast better; the other keeps what serves its errands.
	if(!params.artifactCare || !leader || !other || leader == other)
		return;
	const ObjectInstanceID leaderId = leader->id;
	const ObjectInstanceID otherId = other->id;
	auto usable = [](ArtifactPosition slot)
	{
		return slot != ArtifactPosition::SPELLBOOK && !(slot >= ArtifactPosition::MACH1 && slot <= ArtifactPosition::MACH4);
	};
	// what `receiver` gains by getting `art` (it may replace something it wears), given the role it plays
	auto gainFor = [&](const CGHeroInstance * receiver, const CArtifactInstance * art, bool asFighter)
	{
		const double mine = artifactValue(receiver, art->getType(), asFighter);
		if(mine <= 0)
			return 0.0;
		const auto & possible = art->getType()->getPossibleSlots();
		auto slots = possible.find(ArtBearer::HERO);
		if(slots == possible.end())
			return 0.0;
		double best = 0;
		for(const ArtifactPosition & slot : slots->second)
		{
			if(!usable(slot))
				continue;
			const CArtifactInstance * worn = receiver->getArt(slot, false);
			auto wornInfo = receiver->artifactsWorn.find(slot);
			if(wornInfo != receiver->artifactsWorn.end() && wornInfo->second.locked)
				continue;
			best = std::max(best, worn ? mine - artifactValue(receiver, worn->getType(), asFighter) : mine);
		}
		return best;
	};
	for(int round = 0; round < 40; ++round)
	{
		gateway.checkInterruption();
		leader = ownHero(leaderId);
		other = ownHero(otherId);
		if(!leader || !other)
			return;
		bool moved = false;
		for(int direction = 0; direction < 2 && !moved; ++direction)
		{
			const CGHeroInstance * from = direction == 0 ? other : leader;
			const CGHeroInstance * to = direction == 0 ? leader : other;
			const bool toFighter = direction == 0;
			if(!ArtifactUtils::isBackpackFreeSlots(to))
				continue;
			std::vector<std::pair<ArtifactPosition, const CArtifactInstance *>> carried;
			for(const auto & [slot, info] : from->artifactsWorn)
			{
				if(info.getArt() && !info.locked && usable(slot))
					carried.emplace_back(slot, info.getArt());
			}
			for(size_t i = 0; i < from->artifactsInBackpack.size(); ++i)
			{
				if(const auto * art = from->artifactsInBackpack[i].getArt())
					carried.emplace_back(ArtifactPosition(ArtifactPosition::BACKPACK_START + static_cast<int>(i)), art);
			}
			for(const auto & [slot, art] : carried)
			{
				if(art->getType()->isBig() || art->getTypeId() == ArtifactID::SPELLBOOK)
					continue;
				const bool worn = ArtifactUtils::isSlotEquipment(slot);
				const double keep = worn ? std::max(0.0, artifactValue(from, art->getType(), !toFighter)) : 0.0;
				const double gain = gainFor(to, art, toFighter);
				if(gain <= keep + 100)
					continue;
				logAi->info("FairAI %s: %s hands %s to %s (worth %.0f there, %.0f here)", me.toString(), from->getNameTranslated(),
					art->getType()->getJsonKey(), to->getNameTranslated(), gain, keep);
				cb->swapArtifacts(ArtifactLocation(from->id, slot), ArtifactLocation(to->id, ArtifactPosition(ArtifactPosition::BACKPACK_START + static_cast<int>(to->artifactsInBackpack.size()))));
				moved = true;
				break;
			}
		}
		if(!moved)
			break;
		// wear it right away: the next comparison must see what is really worn
		arrangeArtifacts(ownHero(leaderId));
		arrangeArtifacts(ownHero(otherId));
	}
}

void AdventureAI::onHeroExchange(ObjectInstanceID first, ObjectInstanceID second)
{
	++exchanges;
	const CGHeroInstance * a = ownHero(first);
	const CGHeroInstance * b = ownHero(second);
	if(!a || !b)
		return;
	if(isMain(b) || (!isMain(a) && b->getTotalStrength() > a->getTotalStrength()))
		std::swap(a, b);
	const ObjectInstanceID receiver = a->id;
	const ObjectInstanceID giver = b->id;
	const double expected = transferGain(a, b);
	const uint64_t before = a->getArmyStrength();
	auto describe = [](const CGHeroInstance * hero)
	{
		std::string text = hero->getNameTranslated() + ":";
		for(const auto & [slot, stack] : hero->Slots())
			if(stack && stack->getCreature())
				text += boost::str(boost::format(" %d %s") % stack->getCount() % stack->getCreature()->getJsonKey());
		return text;
	};
	const std::string armies = logAi->isDebugEnabled() ? describe(a) + " | " + describe(b) : std::string();
	pickBestCreatures(a, b);
	if(const auto * now = ownHero(receiver); now && logAi->isDebugEnabled())
	{
		logAi->debug("FairAI %s: hero meeting, expected gain %.0f, army value %llu -> %llu%s", me.toString(), expected,
			static_cast<unsigned long long>(before), static_cast<unsigned long long>(now->getArmyStrength()),
			expected > 0 && now->getArmyStrength() == before ? " NOTHING MOVED [" + armies + "]" : "");
	}
	// the hero that got the troops leads: it gets the artifacts that make an army fight better, too
	exchangeArtifacts(ownHero(receiver), ownHero(giver));
}

void AdventureAI::recruitAtDwelling(const CGDwelling * dwelling, const CArmedInstance * destination)
{
	if(!dwelling || !destination)
		return;
	for(int level = static_cast<int>(dwelling->creatures.size()) - 1; level >= 0; --level)
	{
		const auto & entry = dwelling->creatures[level];
		if(entry.first == 0 || entry.second.empty())
			continue;
		CreatureID creature = entry.second.back();
		const CCreature * type = creature.toCreature();
		ResourceSet available = spendable();
		int amount = static_cast<int>(entry.first);
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			int cost = type->getRecruitCost(res);
			if(cost > 0)
				amount = std::min(amount, static_cast<int>(available[res] / cost));
		}
		if(amount <= 0 || !destination->getSlotFor(creature).validSlot())
			continue;
		if(params.armyFit && dwelling->ID != Obj::WAR_MACHINE_FACTORY)
		{
			// Do they fit the army they are meant for - the leader's, wherever the buyer carries them?
			// Skeletons for a tower army cost its living troops more turns than the skeletons add.
			const CGHeroInstance * leader = ownHero(mainHero);
			const CGHeroInstance * judge = leader ? leader : dynamic_cast<const CGHeroInstance *>(destination);
			const double share = fitShare(judge, creature, static_cast<double>(amount) * type->getAIValue());
			if(share < 0.5)
			{
				logAi->info("FairAI %s: %d %s at %s not bought: they would add only %.0f%% of their worth to the army of %s", me.toString(), amount,
					type->getJsonKey(), dwelling->visitablePos().toString(), 100 * share, judge ? judge->getNameTranslated() : std::string("?"));
				continue;
			}
		}
		if(dwelling->ID == Obj::REFUGEE_CAMP || dwelling->ID == Obj::WAR_MACHINE_FACTORY)
			logAi->info("FairAI %s: %d %s bought at building %d %s", me.toString(), amount, type->getJsonKey(), dwelling->ID.getNum(), dwelling->visitablePos().toString());
		cb->recruitCreatures(dwelling, destination, creature, amount, level);
	}
}

// ======================================================================= armies

void AdventureAI::pickBestCreatures(const CArmedInstance * destination, const CArmedInstance * source, bool takeAll)
{
	// never an army with itself: a stack merged into its own slot crashes the server
	if(!destination || !source || destination == source)
		return;
	const bool sourceIsHero = source->ID == Obj::HERO;

	// An empty town is taken by any scout walking in: leave the weakest stack behind
	SlotID reserved;
	if(!sourceIsHero && params.keepGarrison && destination->ID == Obj::HERO && !takeAll)
	{
		if(source->stacksCount() == 1)
		{
			const auto & [slot, stack] = *source->Slots().begin();
			const int keep = std::max(1, static_cast<int>(stack->getCount()) / 3);
			const int move = static_cast<int>(stack->getCount()) - keep;
			SlotID target = destination->getSlotFor(stack->getCreatureID());
			if(move > 0 && target.validSlot())
				cb->splitStack(source, destination, slot, target, destination->getStackCount(target) + move);
			return;
		}
		double weakest = std::numeric_limits<double>::max();
		for(const auto & [slot, stack] : source->Slots())
		{
			double v = stackValue(stack.get());
			if(v < weakest)
			{
				weakest = v;
				reserved = slot;
			}
		}
	}

	// (fastKeeper) A hero that hands over its army keeps one creature - and marches as fast as that
	// creature. One of its fastest kind gets a slot of its own and stays; everything else may go.
	if(sourceIsHero && params.fastKeeper && source->id != mainHero && source->stacksCount() >= 1)
	{
		SlotID fastSlot;
		int fastSpeed = -1;
		double cheapest = std::numeric_limits<double>::max();
		for(const auto & [slot, stack] : source->Slots())
		{
			if(!stack || !stack->getCreature())
				continue;
			const int speed = stack->getInitiative();
			const double unit = stack->getCreature()->getAIValue();
			if(speed > fastSpeed || (speed == fastSpeed && unit < cheapest))
			{
				fastSpeed = speed;
				cheapest = unit;
				fastSlot = slot;
			}
		}
		if(fastSlot.validSlot())
		{
			if(source->getStackCount(fastSlot) == 1)
				reserved = fastSlot;
			else if(const SlotID spare = source->getFreeSlot(); spare.validSlot())
			{
				cb->splitStack(source, source, fastSlot, spare, 1);
				if(source->hasStackAtSlot(spare))
					reserved = spare;
			}
		}
	}

	for(int guard = 0; guard < 24; ++guard)
	{
		gateway.checkInterruption();
		bool acted = false;

		// 1. join stacks of the same type
		for(const auto & [slot, stack] : source->Slots())
		{
			if(!stack || !stack->getCreature() || slot == reserved)
				continue;
			SlotID target = destination->getSlotFor(stack->getCreatureID());
			if(!target.validSlot() || !destination->hasStackAtSlot(target))
				continue;
			if(sourceIsHero && source->stacksCount() == 1)
			{
				if(stack->getCount() <= 1)
					break;
				cb->splitStack(source, destination, slot, target, destination->getStackCount(target) + stack->getCount() - 1);
			}
			else
				cb->mergeStacks(source, destination, slot, target);
			acted = true;
			break; // armies changed: iterate again
		}
		if(acted)
			continue;

		if(params.armyFit && destination->ID == Obj::HERO)
		{
			// (armyFit) the next stack by what it does to the army as it fights - the morale of the mix
			// included -, not by its face value; transferGain counts the same way
			std::vector<FitStack> have;
			std::vector<FitStack> offer;
			for(const auto & [slot, stack] : destination->Slots())
			{
				if(stack && stack->getCreature())
					have.push_back({stack->getCreatureID(), stackValue(stack.get()), slot.getNum()});
			}
			for(const auto & [slot, stack] : source->Slots())
			{
				if(stack && stack->getCreature() && slot != reserved)
					offer.push_back({stack->getCreatureID(), stackValue(stack.get()), slot.getNum()});
			}
			const SlotID room = destination->getFreeSlot();
			FitMove move;
			if(!fitMove(sharedMorale(dynamic_cast<const CGHeroInstance *>(destination)), have, offer, !room.validSlot(), move))
				return;
			const SlotID from(offer[move.take].slot);
			const CStackInstance * taken = source->getStackPtr(from);
			const bool last = sourceIsHero && source->stacksCount() == 1;
			if(room.validSlot())
			{
				if(last)
				{
					if(!taken || taken->getCount() <= 1)
						return;
					cb->splitStack(source, destination, from, room, taken->getCount() - 1);
				}
				else
					cb->mergeOrSwapStacks(source, destination, from, room);
				continue;
			}
			if(last || move.drop < 0)
				return;
			cb->swapCreatures(source, destination, from, SlotID(have[move.drop].slot));
			continue;
		}

		// 2. strongest remaining stack of the source
		SlotID bestSlot;
		double bestValue = 0;
		for(const auto & [slot, stack] : source->Slots())
		{
			if(slot == reserved)
				continue;
			double v = stackValue(stack.get());
			if(v > bestValue)
			{
				bestValue = v;
				bestSlot = slot;
			}
		}
		if(!bestSlot.validSlot())
			return;
		const CStackInstance * best = source->getStackPtr(bestSlot);
		const bool lastOfHero = sourceIsHero && source->stacksCount() == 1;

		SlotID freeSlot = destination->getFreeSlot();
		if(freeSlot.validSlot())
		{
			if(lastOfHero)
			{
				if(best->getCount() <= 1)
					return;
				cb->splitStack(source, destination, bestSlot, freeSlot, best->getCount() - 1);
			}
			else
				cb->mergeOrSwapStacks(source, destination, bestSlot, freeSlot);
			continue;
		}

		// 3. destination full: swap with its weakest stack if clearly better
		SlotID weakSlot;
		double weakValue = std::numeric_limits<double>::max();
		for(const auto & [slot, stack] : destination->Slots())
		{
			double v = stackValue(stack.get());
			if(v < weakValue)
			{
				weakValue = v;
				weakSlot = slot;
			}
		}
		if(!weakSlot.validSlot() || bestValue < 1.1 * weakValue || lastOfHero)
			return;
		cb->swapCreatures(source, destination, bestSlot, weakSlot);
	}
}

// ======================================================================= towns

int64_t AdventureAI::recruitableValue(const CGTownInstance * town, int64_t gold) const
{
	double value = 0;
	for(int level = static_cast<int>(town->creatures.size()) - 1; level >= 0 && gold > 0; --level)
	{
		const auto & entry = town->creatures[level];
		if(entry.first == 0 || entry.second.empty())
			continue;
		const CCreature * type = entry.second.back().toCreature();
		int cost = std::max(1, type->getRecruitCost(GameResID::GOLD));
		int amount = std::min<int64_t>(entry.first, gold / cost);
		gold -= static_cast<int64_t>(amount) * cost;
		value += static_cast<double>(amount) * type->getAIValue();
	}
	return static_cast<int64_t>(value);
}

void AdventureAI::recruitInTown(const CGTownInstance * town, const CArmedInstance * destination, int64_t goldReserve)
{
	for(int level = static_cast<int>(town->creatures.size()) - 1; level >= 0; --level)
	{
		gateway.checkInterruption();
		const auto & entry = town->creatures[level];
		if(entry.first == 0 || entry.second.empty())
			continue;
		CreatureID creature = entry.second.back();
		const CCreature * type = creature.toCreature();
		auto affordable = [&]()
		{
			ResourceSet available = spendable();
			available[GameResID::GOLD] = std::max<int64_t>(0, available[GameResID::GOLD] - goldReserve);
			int count = static_cast<int>(entry.first);
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			{
				int cost = type->getRecruitCost(res);
				if(cost > 0)
					count = std::min(count, static_cast<int>(available[res] / cost));
			}
			return count;
		};
		int amount = affordable();
		if(amount <= 0)
			continue;
		const CArmedInstance * target = destination;
		// (armyFit) troops that do not fit the hero's army stay in the garrison: they hold the town
		if(params.armyFit && target->ID == Obj::HERO && target != town->getUpperArmy()
			&& fitShare(dynamic_cast<const CGHeroInstance *>(target), creature, static_cast<double>(amount) * type->getAIValue()) < 0.5)
			target = town->getUpperArmy();
		if(!target->getSlotFor(creature).validSlot())
			target = town->getUpperArmy();
		if(!target->getSlotFor(creature).validSlot())
		{
			if(!makeRoom(town, target, creature, static_cast<double>(amount) * type->getAIValue()))
				continue;
			// making room upgrades the army first, and that costs: what can we still pay for?
			// (the server refused such purchases: "lack of resources")
			amount = affordable();
			if(amount <= 0 || !target->getSlotFor(creature).validSlot())
				continue;
		}
		cb->recruitCreatures(town, target, creature, amount, level);
	}
}

int AdventureAI::unlearnedGuildLevels(const CGHeroInstance * hero, const CGTownInstance * town) const
{
	// without a book the first visit buys one (manageTownVisit) and the spells come with it
	if(!hero || !town || town->tempOwner != me || !hero->hasSpellbook())
		return 0;
	// levels the hero can learn (Wisdom) that were built - or came into its reach - since its last visit
	const int limit = std::min(hero->maxSpellLevel(), town->mageGuildLevel());
	auto seen = guildSeen.find({hero->id, town->id});
	return std::max(0, limit - (seen == guildSeen.end() ? 0 : seen->second));
}

void AdventureAI::mergeDuplicates(const CArmedInstance * army)
{
	for(int guard = 0; guard < GameConstants::ARMY_SIZE && army; ++guard)
	{
		SlotID from;
		SlotID into;
		for(const auto & [slot, stack] : army->Slots())
		{
			if(!stack || !stack->getCreature())
				continue;
			for(const auto & [other, otherStack] : army->Slots())
			{
				if(other != slot && otherStack && otherStack->getCreature() == stack->getCreature())
				{
					from = other;
					into = slot;
					break;
				}
			}
			if(from.validSlot())
				break;
		}
		if(!from.validSlot() || from == into)
			return;
		cb->mergeStacks(army, army, from, into);
	}
}

bool AdventureAI::makeRoom(const CGTownInstance * town, const CArmedInstance * army, CreatureID creature, double batchValue)
{
	// Seven slots fill up with leftovers - the weakest stacks a hero left behind, what a hired hero
	// brought along, troops bought before their dwelling was upgraded. From then on nothing the
	// town grows found a slot, and nothing was bought: in a logged game 42 power liches, 56 vampire
	// lords and more waited in the dwellings of a town whose garrison held four harpies and six gogs.
	if(!params.garrisonRoom || !army || batchValue <= 0)
		return false;
	// 1. what the town can upgrade becomes the kind it sells; two stacks of one kind become one
	upgradeArmy(army);
	mergeDuplicates(army);
	if(army->getSlotFor(creature).validSlot())
		return true;
	// 2. the weakest stack goes, if what comes instead is worth clearly more - a kind this town
	//    does not sell first: its own kinds grow every week and would only be bought again
	auto soldHere = [town](const CStackInstance * stack)
	{
		if(!town || !stack)
			return false;
		for(const auto & entry : town->creatures)
			for(const auto & id : entry.second)
				if(id == stack->getCreatureID())
					return true;
		return false;
	};
	SlotID weakest;
	double weakestValue = std::numeric_limits<double>::max();
	bool weakestForeign = false;
	for(const auto & [slot, stack] : army->Slots())
	{
		const double v = stackValue(stack.get());
		const bool foreign = !soldHere(stack.get());
		if(foreign && params.roomFactor * v > batchValue)
			continue; // worth too much to go: no candidate before one of the town's own kinds
		if((foreign && !weakestForeign) || (foreign == weakestForeign && v < weakestValue))
		{
			weakestValue = v;
			weakest = slot;
			weakestForeign = foreign;
		}
	}
	if(!weakest.validSlot() || army->stacksCount() <= 1 || params.roomFactor * weakestValue > batchValue)
		return false;
	const CStackInstance * stack = army->getStackPtr(weakest);
	if(!stack || !stack->getCreature())
		return false;
	logAi->debug("FairAI %s day %d: %d %s are dismissed (worth %.0f) to make room for %s worth %.0f", me.toString(), today,
		static_cast<int>(stack->getCount()), stack->getCreature()->getJsonKey(), weakestValue, creature.toCreature()->getJsonKey(), batchValue);
	cb->dismissCreature(army, weakest);
	return army->getSlotFor(creature).validSlot();
}

void AdventureAI::leaveGarrison(const CGHeroInstance * hero, const CGTownInstance * town)
{
	// (towerGuard) A town with arrow towers never stands empty: one creature makes the attacker fight,
	// and the towers shoot while he does. The hero leaves one of its cheapest kind.
	if(params.towerGuard && town->fortLevel() >= CGTownInstance::CITADEL && !town->getGarrisonHero() && town->stacksCount() == 0)
	{
		SlotID cheapest;
		double cheapestValue = std::numeric_limits<double>::max();
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(!stack || !stack->getCreature() || (stack->getCount() < 2 && hero->stacksCount() < 2))
				continue;
			const double v = stack->getCreature()->getAIValue();
			if(v < cheapestValue)
			{
				cheapestValue = v;
				cheapest = slot;
			}
		}
		const CStackInstance * stack = cheapest.validSlot() ? hero->getStackPtr(cheapest) : nullptr;
		const SlotID target = stack ? town->getSlotFor(stack->getCreatureID()) : SlotID();
		if(stack && target.validSlot())
		{
			logAi->info("FairAI %s day %d: %s leaves one %s in %s for the towers", me.toString(), today, hero->getNameTranslated(),
				stack->getCreature()->getJsonKey(), town->getNameTranslated());
			if(stack->getCount() >= 2)
				cb->splitStack(hero, town, cheapest, target, 1);
			else
				cb->mergeOrSwapStacks(hero, town, cheapest, target);
			gateway.waitTillFree();
			if(statsFile)
				stats[phaseOf(today)]["towerGuard"] += 1;
		}
	}
	// Raiders take empty towns for free. While enemy heroes are about, leave the weakest stacks
	// behind until the garrison is worth a share of the hero's army.
	if(params.garrisonShare <= 0 || town->getGarrisonHero())
		return;
	bool enemiesAbout = false;
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type == Obj::HERO && isEnemy(object.owner) && today - object.lastSeenDay <= 7)
			enemiesAbout = true;
	}
	if(!enemiesAbout)
		return;
	const double heroArmy = static_cast<double>(hero->getArmyStrength());
	for(int guard = 0; guard < GameConstants::ARMY_SIZE; ++guard)
	{
		double garrison = 0;
		for(const auto & slot : town->Slots())
			garrison += stackValue(slot.second.get());
		if(garrison >= params.garrisonShare * (heroArmy + garrison) || hero->stacksCount() <= 1)
			return;
		SlotID weakest;
		double weakestValue = std::numeric_limits<double>::max();
		for(const auto & [slot, stack] : hero->Slots())
		{
			double v = stackValue(stack.get());
			if(v < weakestValue)
			{
				weakestValue = v;
				weakest = slot;
			}
		}
		const CStackInstance * stack = hero->getStackPtr(weakest);
		if(!stack)
			return;
		SlotID target = town->getSlotFor(stack->getCreatureID());
		if(!target.validSlot())
			return;
		if(town->hasStackAtSlot(target))
			cb->mergeStacks(hero, town, weakest, target);
		else
			cb->mergeOrSwapStacks(hero, town, weakest, target);
	}
}

void AdventureAI::mergeBaits(const CGHeroInstance * hero)
{
	if(params.baitStacks <= 0)
		return;
	for(int guard = 0; guard < GameConstants::ARMY_SIZE && hero; ++guard)
	{
		SlotID single;
		SlotID target;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(!stack || stack->getCount() != 1)
				continue;
			for(const auto & [other, otherStack] : hero->Slots())
			{
				if(other != slot && otherStack && otherStack->getCreature() == stack->getCreature())
				{
					single = slot;
					target = other;
					break;
				}
			}
			if(single.validSlot())
				break;
		}
		if(!single.validSlot())
			return;
		cb->mergeStacks(hero, hero, single, target);
		gateway.waitTillFree();
		hero = ownHero(hero->id);
	}
}

void AdventureAI::arrangeBaits(const CGHeroInstance * hero)
{
	if(params.baitStacks <= 0 || !hero)
		return;
	int singles = 0;
	const CStackInstance * cheapest = nullptr;
	SlotID cheapestSlot;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(!stack || !stack->getCreature())
			continue;
		if(stack->getCount() == 1)
			++singles;
		else if(stack->getCount() >= 3 + params.baitStacks)
		{
			const auto * type = stack->getCreature();
			if(!cheapest || type->getAIValue() < cheapest->getCreature()->getAIValue()
				|| (type->getAIValue() == cheapest->getCreature()->getAIValue() && type->getMovementRange() > cheapest->getCreature()->getMovementRange()))
			{
				cheapest = stack.get();
				cheapestSlot = slot;
			}
		}
	}
	for(int wanted = params.baitStacks - singles; wanted > 0 && cheapest; --wanted)
	{
		const SlotID free = hero->getFreeSlot();
		if(!free.validSlot())
			return;
		logAi->debug("FairAI %s: %s splits off one %s as bait", me.toString(), hero->getNameTranslated(), cheapest->getCreature()->getNameSingularTranslated());
		cb->splitStack(hero, hero, cheapestSlot, free, 1);
		gateway.waitTillFree();
		hero = ownHero(hero->id);
		if(!hero)
			return;
		cheapest = hero->getStackPtr(cheapestSlot);
		if(!cheapest || cheapest->getCount() < 3)
			return;
	}
}

void AdventureAI::manageTownVisit(const CGHeroInstance * hero, const CGTownInstance * town)
{
	if(!hero || !town || town->tempOwner != me)
		return;
	const bool main = isMain(hero);
	// whoever leaves the function: what the guild had to teach this hero, it has taught
	struct NoteGuild
	{
		AdventureAI & ai;
		ObjectInstanceID heroId;
		const CGTownInstance * town;
		~NoteGuild()
		{
			if(const auto * now = ai.ownHero(heroId); now && now->hasSpellbook())
				ai.guildSeen[{heroId, town->id}] = std::min(now->maxSpellLevel(), town->mageGuildLevel());
		}
	} noteGuild{*this, hero->id, town};
	if(main)
	{
		mergeBaits(hero);
		hero = ownHero(hero->id);
		if(!hero)
			return;
		recruitInTown(town, hero, 0);
		pickBestCreatures(hero, town);
		const CGHeroInstance * garrisoned = town->getGarrisonHero();
		if(garrisoned && garrisoned != hero && garrisoned->tempOwner == me)
			pickBestCreatures(hero, garrisoned);
		leaveGarrison(hero, town);
		if(!hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1)
			&& spendable()[GameResID::GOLD] >= GameConstants::SPELLBOOK_GOLD_COST + 500)
		{
			cb->buyArtifact(hero, ArtifactID::SPELLBOOK);
		}
		upgradeArmy(hero);
		// (sites) artifact merchants in this town: gold that lies idle buys what makes the leader stronger
		if(params.sites && buyArtifacts(town, hero) > 0)
		{
			hero = ownHero(hero->id);
			if(!hero)
				return;
		}
		if(params.buyWarMachines)
		{
			// a ballista, tent or ammo cart from the blacksmith helps in every battle
			const ArtifactID machine = town->getWarMachineInBuilding(BuildingID::BLACKSMITH);
			if(machine.hasValue() && !hero->hasArt(machine))
			{
				const int price = static_cast<int>(machine.toArtifact()->getPrice());
				const int64_t reserve = economy.rho > 1.5 ? 500 : 3000;
				if(spendable()[GameResID::GOLD] >= price + reserve)
					cb->buyArtifact(hero, machine);
			}
		}
		if(const auto * now = ownHero(hero->id))
			townLeftover[town->id] = townTroops(town, now);
	}
	else if(ownHero(mainHero) && params.collectorsTakeGarrison && !params.supplyRuns)
	{
		// collectors ferry the garrison to the main hero
		if(params.couriers)
			recruitInTown(town, hero, economy.rho > 1.5 ? 0 : 1000);
		pickBestCreatures(hero, town);
	}
	else if(params.secondArmy && !(supply && supply->courier == hero->id))
	{
		// (secondArmy) What waits in this town makes this hero the leader of a second army, or keeps
		// it one: it takes the best of it. Not while the main hero stands here - that one chooses first.
		const double bar = secondArmyBar();
		const CGHeroInstance * leader = ownHero(mainHero);
		if(bar > 0 && !(leader && leader->getVisitedTown() == town))
		{
			const ObjectInstanceID heroId = hero->id;
			const double army = std::max(1.0, static_cast<double>(hero->getArmyStrength()));
			const double potential = fieldStrength(hero) * (army + townTroops(town, hero)) / army;
			if(leads(hero) || potential >= bar)
			{
				recruitInTown(town, hero, economy.rho > 1.5 ? 0 : 1000);
				if(const auto * now = ownHero(heroId))
					pickBestCreatures(now, town);
				if(const auto * now = ownHero(heroId); now && !now->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1)
					&& spendable()[GameResID::GOLD] >= GameConstants::SPELLBOOK_GOLD_COST + 500)
				{
					cb->buyArtifact(now, ArtifactID::SPELLBOOK);
				}
				if(const auto * now = ownHero(heroId))
					upgradeArmy(now);
				updateSecondLeaders();
			}
		}
	}
}

void AdventureAI::updateResourcePlan()
{
	// What will our towns need, and what comes in? Costs of every building still missing and of four
	// weeks of creatures (the faction's whole line-up: the angels' gems are wanted before the portal
	// stands) against the stock and four weeks of income. All from the game's data, for any faction.
	resourceScarcity.clear();
	if(!params.resourcePlan)
		return;
	ResourceSet demand;
	for(const auto * town : perception.ownTowns())
	{
		const CTown * info = town->getTown();
		for(const auto & [id, building] : info->buildings)
		{
			if(town->hasBuilt(id) || id == BuildingID::GRAIL)
				continue;
			const EBuildingState state = cb->canBuildStructure(town, id);
			if(state == EBuildingState::ALLOWED || state == EBuildingState::NO_RESOURCES || state == EBuildingState::PREREQUIRES)
				demand += building->resources;
		}
		for(const auto & tier : info->creatures)
		{
			if(tier.empty())
				continue;
			const CCreature * creature = tier.back().toCreature();
			if(!creature)
				continue;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				demand[res] += 4 * creature->getGrowth() * creature->getRecruitCost(res);
		}
	}
	ResourceSet income;
	for(const auto * object : cb->getMyObjects())
	{
		if(const auto * ownable = dynamic_cast<const IOwnableObject *>(object))
			income += ownable->dailyIncome();
	}
	const ResourceSet stock = cb->getResourceAmount();
	std::string text;
	for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
	{
		if(res == GameResID::GOLD)
			continue;
		const double supply = stock[res] + 28.0 * income[res];
		const double factor = demand[res] <= 0 ? 0.5 : std::clamp(demand[res] / std::max(1.0, supply), 0.5, 4.0);
		resourceScarcity[res.getNum()] = factor;
		text += boost::str(boost::format(" %s %.1f (need %d, have %d, +%d/day)") % res.toResource()->getJsonKey() % factor % demand[res] % stock[res] % income[res]);
	}
	logAi->debug("FairAI %s day %d resource plan:%s", me.toString(), today, text);
}

double AdventureAI::scarcity(GameResID res) const
{
	auto it = resourceScarcity.find(res.getNum());
	return it == resourceScarcity.end() ? 1.0 : it->second;
}

void AdventureAI::updateEconomy()
{
	Economy e;
	for(const auto * t : perception.ownTowns())
	{
		e.weeklyIncome += 7.0 * t->dailyIncome()[GameResID::GOLD];
		for(size_t level = 0; level < t->creatures.size(); ++level)
		{
			if(t->creatures[level].second.empty())
				continue;
			const CCreature * c = t->creatures[level].second.back().toCreature();
			e.weeklyRecruitCost += t->creatureGrowth(static_cast<int>(level)) * c->getRecruitCost(GameResID::GOLD);
		}
	}
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type == Obj::MINE && object.owner == me && object.subtype.getNum() == GameResID::GOLD)
			e.weeklyIncome += 7000;
	}
	{
		// (surrender 2) what the creatures waiting in our dwellings cost: gold that is spoken for
		double waiting = 0;
		for(const auto * t : perception.ownTowns())
		{
			for(const auto & entry : t->creatures)
			{
				if(entry.first > 0 && !entry.second.empty())
					waiting += static_cast<double>(entry.first) * entry.second.back().toCreature()->getRecruitCost(GameResID::GOLD);
			}
		}
		waitingTroopGold = waiting;
	}
	const double stock = spendable()[GameResID::GOLD];
	e.rho = (e.weeklyIncome + 0.5 * stock) / std::max(500.0, e.weeklyRecruitCost);
	e.armyPremium = std::clamp(e.rho, 1.0, 4.0);
	e.goldWeight = 1.0 / e.armyPremium;
	economy = e;
}

void AdventureAI::upgradeArmy(const CArmedInstance * hero)
{
	if(!hero)
		return;
	// best value gain per gold first; keep a reserve while gold is the bottleneck
	const int64_t reserve = economy.rho > 1.5 ? 0 : 2000;
	for(int round = 0; round < GameConstants::ARMY_SIZE; ++round)
	{
		gateway.checkInterruption();
		SlotID bestSlot;
		CreatureID bestUpgrade;
		double bestGain = 0;
		for(const auto & [slot, stack] : hero->Slots())
		{
			if(!stack || !stack->getCreature())
				continue;
			UpgradeInfo info(stack->getCreatureID());
			cb->fillUpgradeInfo(hero, slot, info);
			if(!info.canUpgrade())
				continue;
			CreatureID upgrade = info.getUpgrade();
			ResourceSet cost = info.getUpgradeCosts() * stack->getCount();
			ResourceSet have = spendable();
			have[GameResID::GOLD] -= reserve;
			if(!have.canAfford(cost))
				continue;
			double gain = static_cast<double>(upgrade.toCreature()->getAIValue() - stack->getCreature()->getAIValue()) * stack->getCount();
			double price = 1;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				price += cost[res] * resourceValue(res);
			if(gain / price > bestGain)
			{
				bestGain = gain / price;
				bestSlot = slot;
				bestUpgrade = upgrade;
			}
		}
		if(!bestSlot.validSlot() || bestGain < 0.3)
			return;
		cb->upgradeCreature(hero, bestSlot, bestUpgrade);
	}
}

double AdventureAI::buildingValue(const CGTownInstance * town, BuildingID id) const
{
	const CTown * info = town->getTown();
	auto it = info->buildings.find(id);
	if(it == info->buildings.end())
		return 0;
	const CBuilding * building = it->second.get();

	auto worth = [this](const ResourceSet & set)
	{
		double total = 0;
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			total += set[res] * resourceValue(res);
		return total;
	};

	// gold-bound economy: only part of new growth can be bought
	const double affordability = std::clamp(economy.rho, 0.3, 1.0);
	const double armyWeight = economy.armyPremium * affordability;
	const double goldWeight = economy.goldWeight;

	double value = 0;

	// income (halls, silo, ...): an upgrade replaces the income of what it upgrades
	ResourceSet produce = building->produce;
	if(building->upgrade != BuildingID::NONE)
	{
		auto base = info->buildings.find(building->upgrade);
		if(base != info->buildings.end())
			produce -= base->second->produce;
	}
	{
		// while buildings are waiting gold is scarce whatever today's stock says
		double incomeWeight = goldWeight;
		if(params.earlyGoldDays > 0)
		{
			// economy first lasts until the enemy is found (earlyGoldContact): from then on troops count
			double until = params.earlyGoldDays;
			double fade = 14.0;
			if(params.earlyGoldContact && contactDay >= 0 && contactDay < until)
			{
				until = contactDay;
				fade = 7.0;
			}
			incomeWeight = std::max(goldWeight, std::clamp(1.0 - (today - until) / fade, 0.0, 1.0));
		}
		value += std::max(0.0, worth(produce)) * params.incomeHorizonDays * incomeWeight;
	}

	auto creatureAt = [info](int level, int upgrade) -> const CCreature *
	{
		if(level < 0 || level >= static_cast<int>(info->creatures.size()))
			return nullptr;
		const auto & tier = info->creatures.at(level);
		if(upgrade < 0 || upgrade >= static_cast<int>(tier.size()))
			return nullptr;
		return tier[upgrade].toCreature();
	};

	const double fortBonus = town->fortLevel() >= CGTownInstance::CASTLE ? 2.0 : (town->fortLevel() >= CGTownInstance::CITADEL ? 1.5 : 1.0);

	if(id.isDwelling())
	{
		int level = BuildingID::getLevelIndexFromDwelling(id);
		int upgrade = BuildingID::getUpgradeNoFromDwelling(id);
		const CCreature * creature = creatureAt(level, upgrade);
		if(creature)
		{
			double perUnit = creature->getAIValue();
			if(upgrade > 0)
			{
				const CCreature * base = creatureAt(level, upgrade - 1);
				perUnit = base ? perUnit - base->getAIValue() : perUnit;
			}
			value += creature->getGrowth() * fortBonus * perUnit * params.goldPerArmyValue * params.dwellingHorizonWeeks * armyWeight;
		}
	}
	else if(id == BuildingID::FORT || id == BuildingID::CITADEL || id == BuildingID::CASTLE)
	{
		double extra = id == BuildingID::CITADEL ? 0.5 : (id == BuildingID::CASTLE ? 0.5 : 0.0);
		double weeklyArmy = 0;
		for(size_t level = 0; level < town->creatures.size(); ++level)
		{
			if(town->creatures[level].second.empty())
				continue;
			const CCreature * c = town->creatures[level].second.back().toCreature();
			weeklyArmy += c->getGrowth() * c->getAIValue();
		}
		value += extra * weeklyArmy * params.goldPerArmyValue * params.dwellingHorizonWeeks * armyWeight;
		value += id == BuildingID::FORT ? 800 : 600; // walls, towers, defence
	}
	else if(id == BuildingID::HORDE_1 || id == BuildingID::HORDE_1_UPGR || id == BuildingID::HORDE_2 || id == BuildingID::HORDE_2_UPGR)
	{
		int which = (id == BuildingID::HORDE_1 || id == BuildingID::HORDE_1_UPGR) ? 0 : 1;
		auto horde = info->hordeLvl.find(which);
		if(horde != info->hordeLvl.end() && horde->second >= 0)
		{
			const CCreature * creature = creatureAt(horde->second, 0);
			if(creature)
				value += std::max(1, creature->getHorde()) * creature->getAIValue() * params.goldPerArmyValue * params.dwellingHorizonWeeks * armyWeight;
		}
	}
	else if(params.magicFocus && id >= BuildingID::MAGES_GUILD_1 && id <= BuildingID::MAGES_GUILD_5)
	{
		// What a guild level does for the hero that leads the army: slowing or hasting a whole army,
		// and from the fourth level on the spells that decide big battles (Chain Lightning,
		// Resurrection, Implosion). Worth it for a hero that can learn the level (spellbook; Wisdom
		// for levels three to five) - a little otherwise: the next leader may. Further towns add
		// other spells of the same levels: half.
		static const std::array<double, 5> worth = {1500, 2500, 3000, 4000, 5000};
		const int level = id.getNum() - BuildingID(BuildingID::MAGES_GUILD_1).getNum() + 1;
		const CGHeroInstance * leader = ownHero(mainHero);
		double share = 0.15;
		if(leader && leader->maxSpellLevel() >= level)
			share = leader->hasSpellbook() || level == 1 ? 1.0 : 0.5;
		int others = 0;
		for(const auto * t : perception.ownTowns())
			others += t != town && t->hasBuilt(id) ? 1 : 0;
		// the army's size is what the spells work on: little early, a lot once the army is big
		const double army = leader ? static_cast<double>(leader->getArmyStrength()) : 0.0;
		const double scale = std::clamp(army / 40000.0, 0.4, 3.0);
		// (magicFocus 2) situational: a guild level is worth a little - too little to be built before
		// dwellings, enough to be built from gold that lies idle (idleBuild)
		value += worth[std::clamp(level, 1, 5) - 1] * share * (others > 0 ? 0.5 : 1.0) * scale * (params.magicFocus >= 2 ? 0.15 : 1.0);
	}
	else if(id == BuildingID::MAGES_GUILD_1)
		value += params.mageGuildValue;
	else if(id == BuildingID::MAGES_GUILD_2)
		value += 0.6 * params.mageGuildValue;
	else if(id == BuildingID::MAGES_GUILD_3)
		value += 0.4 * params.mageGuildValue;
	else if(id == BuildingID::MARKETPLACE)
	{
		// trading unblocks buildings we cannot afford for lack of a resource
		bool anyMarket = false;
		for(const auto * t : perception.ownTowns())
			anyMarket = anyMarket || t->hasBuiltResourceMarketplace();
		value += 800;
		if(!anyMarket && economy.rho > 1.5)
			value += 4000;
	}
	else if(id == BuildingID::TAVERN)
	{
		bool anyTavern = false;
		for(const auto * t : perception.ownTowns())
			anyTavern = anyTavern || t->hasBuilt(BuildingID::TAVERN);
		const int heroes = static_cast<int>(perception.ownHeroes().size());
		// opening: a second hero brings an army and doubles map control
		if(!anyTavern && heroes < 3)
			value += 30000;
		else
			value += heroes < 3 ? 3000 : 300;
	}
	else if(id == BuildingID::BLACKSMITH)
		value += 200;
	else if(id == BuildingID::SHIPYARD)
		value += seaWanted && params.boats ? 6000 : 0;
	else if(id == BuildingID::SHIP)
		value += 0;
	else if(value <= 0)
		value += 100;

	return value;
}

void AdventureAI::buildInTown(const CGTownInstance * town)
{
	const CTown * info = town->getTown();
	// (saveFor, threatSpend) a hero that beats what the town holds was seen near it: troops come first
	const bool danger = (params.saveFor || params.threatSpend) && townInDanger(town);
	// (saveFor) what this town saved is its own to spend today (confirmed again below if the building
	// still cannot be paid); a town in danger needs the gold of any saver
	if(params.saveFor && today - savingsDay <= 1 && (savingsTown == town->id || danger))
	{
		if(danger)
			logAi->debug("FairAI %s day %d: %s is in danger - the savings for a building are released", me.toString(), today, town->getNameTranslated());
		savingsDay = -100;
	}
	const ResourceSet have = spendable();
	// When creatures, not gold, are the bottleneck, gold is cheap: income is worth less and gold
	// costs hurt less, while dwellings are worth more. Rare resources keep their value.
	const double goldWeight = economy.goldWeight;

	auto costOf = [&](BuildingID id)
	{
		auto it = info->buildings.find(id);
		if(it == info->buildings.end())
			return 1e12;
		double total = 0;
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			double c = it->second->resources[res];
			total += res == GameResID::GOLD ? c * goldWeight : c * resourceValue(res);
		}
		return total;
	};

	struct Option
	{
		BuildingID build;
		EBuildingState state;
		double score;
		std::string reason;
		bool income = false; ///< the goal behind this option raises our gold income (a hall, or what a hall needs)
	};
	std::map<BuildingID, Option> options; // best score per buildable candidate

	for(const auto & entry : info->buildings)
	{
		BuildingID goal = entry.first;
		if(town->hasBuilt(goal) || goal == BuildingID::GRAIL)
			continue;
		EBuildingState state = cb->canBuildStructure(town, goal);
		if(state != EBuildingState::ALLOWED && state != EBuildingState::NO_RESOURCES && state != EBuildingState::PREREQUIRES)
			continue;

		double value = buildingValue(town, goal);
		if(value <= 0)
			continue;

		// walk down missing prerequisites to something we could build today
		BuildingID candidate = goal;
		double chainCost = costOf(goal);
		double chainValue = value;
		for(int depth = 0; depth < 4 && state == EBuildingState::PREREQUIRES; ++depth)
		{
			auto missing = town->genBuildingRequirements(candidate, false).getFulfillmentCandidates([town](const BuildingID & b)
			{
				return town->hasBuilt(b);
			});
			if(missing.empty() || missing.front() == candidate)
				break;
			candidate = missing.front();
			chainCost += costOf(candidate);
			chainValue += buildingValue(town, candidate);
			state = cb->canBuildStructure(town, candidate);
		}
		if((state != EBuildingState::ALLOWED && state != EBuildingState::NO_RESOURCES) || candidate == BuildingID::GRAIL)
			continue;
		auto candidateInfo = info->buildings.find(candidate);
		if(candidateInfo == info->buildings.end() || !candidateInfo->second->resources.nonZero())
			continue;
		// a daily build slot is worth something too: favours big steps when we are rich
		double score = chainValue / (chainCost + 1000.0);
		if(state == EBuildingState::NO_RESOURCES)
			score *= 0.9;
		// does the goal, or the step towards it, raise our gold income? (a hall brings more gold than the one it replaces)
		auto raisesIncome = [&](BuildingID id)
		{
			const CBuilding * building = info->buildings.at(id).get();
			int more = building->produce[GameResID::GOLD];
			if(building->upgrade != BuildingID::NONE)
			{
				auto base = info->buildings.find(building->upgrade);
				if(base != info->buildings.end())
					more -= base->second->produce[GameResID::GOLD];
			}
			return more > 0;
		};
		const bool income = raisesIncome(goal) || raisesIncome(candidate);
		auto existing = options.find(candidate);
		// (a step is a step towards income if any goal behind it is - whichever goal scores best)
		const bool incomeBefore = existing != options.end() && existing->second.income;
		if(existing == options.end() || existing->second.score < score)
			options[candidate] = Option{candidate, state, score, info->buildings.at(goal)->getNameTranslated(), income};
		options[candidate].income = options[candidate].income || income || incomeBefore;
	}

	std::vector<Option> ranked;
	for(const auto & entry : options)
		ranked.push_back(entry.second);
	std::sort(ranked.begin(), ranked.end(), [](const Option & a, const Option & b) { return a.score > b.score; });

	if(!ranked.empty())
		logAi->debug("FairAI %s day %d: best build option in %s: %s (%s, score %.2f)", me.toString(), today, town->getNameTranslated(),
			info->buildings.at(ranked.front().build)->getNameTranslated(), ranked.front().state == EBuildingState::ALLOWED ? "affordable" : "missing resources",
			ranked.front().score);
	{
		// remember what our most wanted buildings lack: heroes value those resources more
		const ResourceSet have = spendable();
		int counted = 0;
		for(const auto & option : ranked)
		{
			if(counted >= 3 || option.score < 0.5)
				break;
			++counted;
			if(option.state != EBuildingState::NO_RESOURCES)
				continue;
			const ResourceSet & cost = info->buildings.at(option.build)->resources;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			{
				if(res == GameResID::GOLD)
					continue;
				int missing = cost[res] - have[res];
				if(missing > 0)
					resourceNeed[res.getNum()] = std::max(resourceNeed[res.getNum()], missing);
			}
		}
	}

	// what the three most wanted buildings cost, plus a little: resources beyond that may be traded away
	ResourceSet keep;
	{
		int counted = 0;
		for(const auto & option : ranked)
		{
			if(counted++ >= 3)
				break;
			const ResourceSet & cost = info->buildings.at(option.build)->resources;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				if(res != GameResID::GOLD)
					keep[res] += cost[res];
		}
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			if(res != GameResID::GOLD)
				keep[res] += (res == GameResID::WOOD || res == GameResID::ORE) ? 10 : 4;
	}

	// (armyFirst) Troops that wait in our dwellings compete with the next building for the same
	// gold. What they are worth per gold piece, in the measure of the building scores:
	double troopGold = 0;
	double troopScore = 0;
	if(params.armyFirst || params.idleBuild)
	{
		double troopValue = 0;
		for(const auto * t : perception.ownTowns())
		{
			for(const auto & entry : t->creatures)
			{
				if(entry.first == 0 || entry.second.empty())
					continue;
				const CCreature * type = entry.second.back().toCreature();
				troopGold += static_cast<double>(entry.first) * type->getRecruitCost(GameResID::GOLD);
				troopValue += static_cast<double>(entry.first) * type->getAIValue();
			}
		}
		if(troopGold > 0)
			troopScore = troopValue * params.goldPerArmyValue * std::sqrt(economy.armyPremium) / (troopGold * goldWeight);
	}

	int attempts = 0;
	for(const auto & option : ranked)
	{
		if(attempts >= 4)
			return;
		if(option.score < 0.25)
		{
			// (idleBuild) Gold nobody needs is built with: what waits in our dwellings is paid for,
			// idleGold more stays in the chest, and the building has a value at all. (Until now
			// nothing below this score was ever built - in long games 100,000 to 250,000 gold lay
			// unused while mage guilds above the first level and the like were missing.)
			if(!params.idleBuild || option.score <= 0.01 || option.state != EBuildingState::ALLOWED)
				continue;
			const ResourceSet & price = info->buildings.at(option.build)->resources;
			const ResourceSet chest = spendable();
			const double left = static_cast<double>(chest[GameResID::GOLD]) - price[GameResID::GOLD] - troopGold;
			if(left < params.idleGold)
				continue;
			// wood, ore and the rare resources: what this town's wanted buildings cost stays, and a stock for the others
			bool spare = true;
			for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
			{
				if(res == GameResID::GOLD || price[res] <= 0)
					continue;
				int reserve = (res == GameResID::WOOD || res == GameResID::ORE) ? 15 : 8;
				for(const auto & wanted : ranked)
					if(wanted.score >= 0.25)
						reserve += info->buildings.at(wanted.build)->resources[res];
				spare = spare && chest[res] - price[res] >= reserve;
			}
			if(!spare)
				continue;
		}
		// a building that would leave troops unbought has to be worth more per gold than they are
		if(params.armyFirst && option.score >= 0.25 && option.score < troopScore
			&& static_cast<double>(spendable()[GameResID::GOLD]) - info->buildings.at(option.build)->resources[GameResID::GOLD] < troopGold)
		{
			if(armyFirstLog != today)
				logAi->debug("FairAI %s day %d: %s in %s waits (score %.2f) - troops for %.0f gold come first (worth %.2f per gold)", me.toString(), today,
					info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.score, troopGold, troopScore);
			armyFirstLog = today;
			return;
		}
		const ResourceSet & price = info->buildings.at(option.build)->resources;
		if(params.threatSpend && danger)
		{
			// (threatSpend) the enemy is coming: only walls, and what leaves the troops waiting here paid for
			const bool walls = option.build == BuildingID::FORT || option.build == BuildingID::CITADEL || option.build == BuildingID::CASTLE;
			int64_t waiting = 0;
			for(const auto & entry : town->creatures)
				if(entry.first > 0 && !entry.second.empty())
					waiting += static_cast<int64_t>(entry.first) * entry.second.back().toCreature()->getRecruitCost(GameResID::GOLD);
			if(!walls && static_cast<int64_t>(spendable()[GameResID::GOLD]) - price[GameResID::GOLD] < waiting)
			{
				if(threatSpendLog != today)
					logAi->info("FairAI %s day %d: %s is in danger - %s waits, troops for %lld gold come first", me.toString(), today, town->getNameTranslated(),
						info->buildings.at(option.build)->getNameTranslated(), static_cast<long long>(waiting));
				threatSpendLog = today;
				continue;
			}
		}
		// (saveFor) another town saves for something lesser than this: its savings may pay for it
		const bool otherSaves = params.saveFor && today - savingsDay <= 1 && savingsTown != town->id;
		const int savedDay = savingsDay;
		if(otherSaves && option.score > savingsScore)
			savingsDay = -100;
		bool pays = (option.state != EBuildingState::NO_RESOURCES || buyResourcesFor(town, price, false, &keep)) && spendable().canAfford(price);
		// (saveFor) Only for what raises our income - a hall or what a hall needs: that gold comes
		// back within days. (Saving for every best building left the main hero with a smaller army:
		// 110,000 against 125,000 on day 45 in 16 games, because nothing was bought on 12 to 32 days.)
		if(!pays && params.saveFor && option.score >= params.saveMinScore && logAi->isDebugEnabled() && saveCheckLog != today)
		{
			saveCheckLog = today;
			logAi->debug("FairAI %s day %d: %s in %s cannot be paid (score %.2f, raises income: %s, town in danger: %s, another town saves: %s)", me.toString(), today,
				info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.score, option.income ? "yes" : "no", danger ? "yes" : "no",
				otherSaves ? "yes" : "no");
		}
		if(!pays && params.saveFor && (option.income || params.saveFor >= 2) && option.score >= params.saveMinScore && !danger
			&& !(otherSaves && option.score <= savingsScore))
		{
			// What is missing, in gold: the gold itself and what the market asks for the other resources
			// (-1: a resource is missing and nobody sells it)
			const IMarket * market = resourceMarket();
			auto shortfall = [&]() -> int64_t
			{
				const ResourceSet stock = spendable();
				int64_t gold = static_cast<int64_t>(stock[GameResID::GOLD]) - price[GameResID::GOLD];
				int64_t bought = 0;
				for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				{
					const int lack = res == GameResID::GOLD ? 0 : price[res] - stock[res];
					if(lack <= 0)
						continue;
					int give = 0;
					int get = 0;
					if(!market || !market->getOffer(GameResID::GOLD, res, give, get, EMarketMode::RESOURCE_RESOURCE) || give <= 0 || get <= 0)
						return -1;
					bought += static_cast<int64_t>((lack + get - 1) / get) * give;
				}
				return std::max<int64_t>(0, bought + (bought > 0 ? 1000 : 0) - gold);
			};
			int64_t missing = shortfall();
			if(missing > 0)
			{
				// surplus wood, ore and rare resources are sold for it
				ResourceSet keepToo = keep;
				for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
					if(res != GameResID::GOLD)
						keepToo[res] = std::max(keepToo[res], price[res]);
				// (only when that pays for the building today: a sale that leaves it a day away gains nothing)
				if(sellForGold(missing, keepToo, true) > 0)
				{
					pays = (cb->canBuildStructure(town, option.build) == EBuildingState::ALLOWED || buyResourcesFor(town, price, false, &keep)) && spendable().canAfford(price);
					missing = pays ? 0 : shortfall();
				}
			}
			if(!pays && savingsLog != today && logAi->isDebugEnabled())
				logAi->debug("FairAI %s day %d: %s in %s lacks %lld gold, income %.0f a week - %s", me.toString(), today, info->buildings.at(option.build)->getNameTranslated(),
					town->getNameTranslated(), static_cast<long long>(missing), economy.weeklyIncome,
					missing > 0 && economy.weeklyIncome > 0 && static_cast<double>(missing) * 7.0 / economy.weeklyIncome <= params.saveDays ? "saving" : "too far to save for");
			if(!pays && missing > 0 && economy.weeklyIncome > 0 && static_cast<double>(missing) * 7.0 / economy.weeklyIncome <= params.saveDays)
			{
				savings = price;
				savingsDay = today;
				savingsTown = town->id;
				savingsBuilding = option.build;
				savingsScore = option.score;
				if(savingsLog != today)
					logAi->info("FairAI %s day %d: saving for %s in %s (score %.2f): %lld gold missing, %.1f days of income", me.toString(), today,
						info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.score, static_cast<long long>(missing),
						static_cast<double>(missing) * 7.0 / economy.weeklyIncome);
				savingsLog = today;
				if(statsFile)
					stats[phaseOf(today)]["savingDays"] += 1;
				return;
			}
		}
		if(!pays)
		{
			if(otherSaves)
				savingsDay = savedDay;
			continue; // the rest is put aside (see earmark)
		}
		if(otherSaves && savingsDay != savedDay)
			logAi->debug("FairAI %s day %d: %s in %s (score %.2f) is paid from what was saved for a building that scores %.2f", me.toString(), today,
				info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.score, savingsScore);
		++attempts;
		logAi->info("FairAI %s day %d: building %s in %s (for %s, score %.2f)", me.toString(), today,
			info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.reason, option.score);
		cb->buildBuilding(town, option.build);
		if(otherSaves)
			savingsDay = savedDay; // the saver goes on saving (and counts again tomorrow)
		if(town->hasBuilt(option.build))
			return;
		logAi->warn("FairAI %s: building %s was rejected", me.toString(), info->buildings.at(option.build)->getNameTranslated());
	}
}

const IMarket * AdventureAI::resourceMarket() const
{
	// any own town with a marketplace will do
	for(const auto * t : perception.ownTowns())
	{
		if(t->hasBuiltResourceMarketplace())
		{
			if(const IMarket * market = cb->getMarket(t->id))
				return market;
		}
	}
	return nullptr;
}

int64_t AdventureAI::sellForGold(int64_t goldWanted, const ResourceSet & keep, bool onlyIfEnough)
{
	const IMarket * market = resourceMarket();
	if(goldWanted <= 0 || !market)
		return 0;
	if(onlyIfEnough)
	{
		// what the surplus would bring
		int64_t possible = 0;
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			const int surplus = res == GameResID::GOLD ? 0 : static_cast<int>(spendable()[res]) - keep[res];
			int give = 0;
			int get = 0;
			if(surplus > 0 && market->getOffer(res, GameResID::GOLD, give, get, EMarketMode::RESOURCE_RESOURCE) && give > 0 && get > 0)
				possible += static_cast<int64_t>(surplus / give) * get;
		}
		if(possible < goldWanted)
			return 0;
	}
	const int64_t before = cb->getResourceAmount()[GameResID::GOLD];
	int64_t expected = 0;
	for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
	{
		if(res == GameResID::GOLD || expected >= goldWanted)
			continue;
		const int surplus = static_cast<int>(spendable()[res]) - keep[res];
		int give = 0;
		int get = 0;
		if(surplus <= 0 || !market->getOffer(res, GameResID::GOLD, give, get, EMarketMode::RESOURCE_RESOURCE) || give <= 0 || get <= 0)
			continue;
		const int64_t lots = std::min<int64_t>(surplus / give, (goldWanted - expected + get - 1) / get);
		if(lots <= 0)
			continue;
		logAi->info("FairAI %s day %d: selling %lld %s for %lld gold", me.toString(), today, static_cast<long long>(lots * give), res.toResource()->getJsonKey(),
			static_cast<long long>(lots * get));
		cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, res, GameResID(GameResID::GOLD), static_cast<int>(lots * give));
		gateway.waitTillFree();
		expected += lots * get;
	}
	const int64_t gained = static_cast<int64_t>(cb->getResourceAmount()[GameResID::GOLD]) - before;
	if(statsFile && gained > 0)
		stats[phaseOf(today)]["soldForGold"] += static_cast<double>(gained);
	return gained;
}

bool AdventureAI::townInDanger(const CGTownInstance * town) const
{
	// what the town holds today, behind its walls
	double holds = 0;
	for(const auto & slot : town->Slots())
		holds += stackValue(slot.second.get());
	if(const auto * guard = town->getGarrisonHero())
		holds += static_cast<double>(guard->getArmyStrength());
	if(const auto * visitor = town->getVisitingHero(); visitor && visitor->tempOwner == me)
		holds += static_cast<double>(visitor->getArmyStrength());
	holds *= defenceWalls(town);
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type != Obj::HERO || !isEnemy(object.owner) || today - object.lastSeenDay > 3)
			continue;
		const double strength = observedDanger(object) * heroFactor(object);
		// two days of march
		if(strength > holds && enemyDistance(id, object.visitablePos, strength, town->visitablePos(), 40) <= 40)
			return true;
	}
	return false;
}

bool AdventureAI::buyResourcesFor(const CGTownInstance * town, const ResourceSet & cost, bool forEarmark, const ResourceSet * keep)
{
	// any own town with a marketplace will do
	const IMarket * market = nullptr;
	for(const auto * t : perception.ownTowns())
	{
		if(t->hasBuiltResourceMarketplace())
		{
			market = cb->getMarket(t->id);
			if(market)
				break;
		}
	}
	if(!market)
		return false;

	ResourceSet have = forEarmark ? cb->getResourceAmount() : spendable();
	if(params.sellSurplus && keep)
	{
		// resources piling up beyond what our next buildings need pay for the ones that are missing
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			if(res == GameResID::GOLD)
				continue;
			int missing = cost[res] - have[res];
			for(GameResID source : LIBRARY->resourceTypeHandler->getAllObjects())
			{
				if(missing <= 0)
					break;
				if(source == GameResID::GOLD || source == res)
					continue;
				const int surplus = have[source] - (*keep)[source] - cost[source];
				int give = 0;
				int get = 0;
				if(surplus <= 0 || !market->getOffer(source, res, give, get, EMarketMode::RESOURCE_RESOURCE) || give <= 0 || get <= 0)
					continue;
				const int lots = std::min((missing + get - 1) / get, surplus / give);
				if(lots <= 0)
					continue;
				logAi->info("FairAI %s day %d: trading %d %s for %d %s", me.toString(), today, lots * give, source.toResource()->getJsonKey(),
					lots * get, res.toResource()->getJsonKey());
				cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, source, res, lots * give);
				gateway.waitTillFree();
				missing -= lots * get;
			}
		}
		have = forEarmark ? cb->getResourceAmount() : spendable();
		if(have.canAfford(cost))
			return true;
	}
	// plan the purchase first: everything must be affordable with gold alone
	int64_t goldNeeded = cost[GameResID::GOLD];
	std::vector<std::pair<GameResID, int>> purchases;
	for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
	{
		if(res == GameResID::GOLD)
			continue;
		int missing = cost[res] - have[res];
		if(missing <= 0)
			continue;
		int give = 0;
		int get = 0;
		if(!market->getOffer(GameResID::GOLD, res, give, get, EMarketMode::RESOURCE_RESOURCE) || give <= 0 || get <= 0)
			return false;
		int lots = (missing + get - 1) / get;
		goldNeeded += static_cast<int64_t>(lots) * give;
		purchases.emplace_back(res, lots * give);
	}
	const int64_t reserve = forEarmark ? 0 : 1000;
	if(purchases.empty() || goldNeeded + reserve > have[GameResID::GOLD])
		return false;
	for(const auto & [res, gold] : purchases)
	{
		logAi->info("FairAI %s day %d: buying %s for %d gold", me.toString(), today, res.toResource()->getJsonKey(), gold);
		cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, GameResID(GameResID::GOLD), res, gold);
	}
	return (forEarmark ? cb->getResourceAmount() : spendable()).canAfford(cost);
}

void AdventureAI::manageTowns(bool endOfTurn)
{
	for(const auto * town : perception.ownTowns())
	{
		gateway.checkInterruption();
		updateEconomy();
		buildInTown(town);
		// troops in the garrison defend the town and wait for the main hero
		int64_t reserve = economy.rho > 1.5 ? 0 : 1000;
		if(params.threatSpend && townInDanger(town))
		{
			// (threatSpend) the enemy is coming: every gold piece into the troops that wait here, and
			// wood, ore and the rest are sold for them (they are no use to a town that falls)
			reserve = 0;
			int64_t waiting = 0;
			ResourceSet keep;
			for(const auto & entry : town->creatures)
			{
				if(entry.first == 0 || entry.second.empty())
					continue;
				const CCreature * type = entry.second.back().toCreature();
				waiting += static_cast<int64_t>(entry.first) * type->getRecruitCost(GameResID::GOLD);
				for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
					if(res != GameResID::GOLD)
						keep[res] += static_cast<int>(entry.first) * type->getRecruitCost(res);
			}
			const int64_t lack = waiting - static_cast<int64_t>(spendable()[GameResID::GOLD]);
			if(lack > 0)
				sellForGold(lack, keep, false);
		}
		recruitInTown(town, town->getUpperArmy(), reserve);
		if(params.towerGuard && endOfTurn && town->fortLevel() >= CGTownInstance::CITADEL && town->getUpperArmy()->stacksCount() == 0)
		{
			// (towerGuard) nothing stands in a town with towers: the cheapest creature for sale is bought,
			// whatever the gold is put aside for
			for(size_t level = 0; level < town->creatures.size(); ++level)
			{
				const auto & entry = town->creatures[level];
				if(entry.first == 0 || entry.second.empty())
					continue;
				const CreatureID creature = entry.second.back();
				if(!cb->getResourceAmount().canAfford(creature.toCreature()->getFullRecruitCost()))
					continue;
				logAi->info("FairAI %s day %d: one %s is bought for the towers of %s", me.toString(), today, creature.toCreature()->getJsonKey(), town->getNameTranslated());
				cb->recruitCreatures(town, town->getUpperArmy(), creature, 1, static_cast<int>(level));
				gateway.waitTillFree();
				if(statsFile)
					stats[phaseOf(today)]["towerGuard"] += 1;
				break;
			}
		}
		if(params.spareStock)
		{
			// (spareStock) what the garrison has no slot for: the hero in the gate buys it, or one is hired for it
			double blocked = 0;
			int64_t blockedGold = 0;
			for(const auto & entry : town->creatures)
			{
				if(entry.first == 0 || entry.second.empty())
					continue;
				const CCreature * type = entry.second.back().toCreature();
				if(type && !town->getUpperArmy()->getSlotFor(type->getId()).validSlot())
				{
					blocked += static_cast<double>(entry.first) * type->getAIValue();
					blockedGold += static_cast<int64_t>(entry.first) * type->getRecruitCost(GameResID::GOLD);
				}
			}
			const CGHeroInstance * visitor = town->getVisitingHero();
			if(blocked > 0 && visitor && visitor->tempOwner == me && visitor->id != mainHero && !(supply && supply->courier == visitor->id))
				recruitInTown(town, visitor, reserve);
			else if(!visitor && endOfTurn && blocked >= params.heavyPartValue && town->hasBuilt(BuildingID::TAVERN)
				&& static_cast<int>(perception.ownHeroes().size()) < params.maxHeroes
				&& spendable()[GameResID::GOLD] >= blockedGold + GameConstants::HERO_GOLD_COST + params.heroGoldReserve)
			{
				const CGHeroInstance * best = nullptr;
				for(const auto * candidate : cb->getAvailableHeroes(town))
				{
					if(!best || candidate->level > best->level || (candidate->level == best->level && candidate->getTotalStrength() > best->getTotalStrength()))
						best = candidate;
				}
				if(best)
				{
					logAi->info("FairAI %s day %d: hiring %s (level %d) in %s for the %.0f troops the garrison has no slot for", me.toString(), today,
						best->getNameTranslated(), static_cast<int>(best->level), town->getNameTranslated(), blocked);
					cb->recruitHero(town, best);
					gateway.waitTillFree();
					if(const auto * hired = town->getVisitingHero(); hired && hired->tempOwner == me)
						recruitInTown(town, hired, reserve);
				}
			}
		}
		if(params.garrisonRoom && endOfTurn && economy.rho > 1.5)
		{
			// gold to spare: the garrison's troops become what the town's dwellings sell now
			upgradeArmy(town->getUpperArmy());
			mergeDuplicates(town->getUpperArmy());
		}
		if(endOfTurn && (logAi->isDebugEnabled() || statsFile))
		{
			// what stays unbought at the end of the day, and what stands in the way
			std::string left;
			int64_t leftGold = 0;
			for(int level = static_cast<int>(town->creatures.size()) - 1; level >= 0; --level)
			{
				const auto & entry = town->creatures[level];
				if(entry.first == 0 || entry.second.empty())
					continue;
				const CCreature * type = entry.second.back().toCreature();
				leftGold += static_cast<int64_t>(entry.first) * type->getRecruitCost(GameResID::GOLD);
				std::string why;
				const ResourceSet available = spendable();
				for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
				{
					const int cost = type->getRecruitCost(res);
					if(cost > 0 && available[res] < cost)
						why += " no " + res.toResource()->getJsonKey();
				}
				const bool noSlot = !town->getUpperArmy()->getSlotFor(type->getId()).validSlot();
				if(statsFile)
					stats[phaseOf(today)][noSlot ? "unboughtNoSlot" : (why.empty() ? "unboughtOther" : "unboughtNoGold")] += static_cast<double>(entry.first) * type->getRecruitCost(GameResID::GOLD);
				if(noSlot)
					why += " no slot";
				left += boost::str(boost::format(" %d %s%s;") % entry.first % type->getJsonKey() % why);
			}
			if(!left.empty() && logAi->isDebugEnabled())
			{
				std::string holds;
				for(const auto & [slot, stack] : town->getUpperArmy()->Slots())
					if(stack && stack->getCreature())
						holds += boost::str(boost::format(" %d %s") % stack->getCount() % stack->getCreature()->getJsonKey());
				logAi->debug("FairAI %s day %d: unbought in %s (%lld gold):%s %s holds:%s", me.toString(), today, town->getNameTranslated(),
					static_cast<long long>(leftGold), left, town->getGarrisonHero() ? town->getGarrisonHero()->getNameTranslated() : std::string("garrison"), holds);
			}
		}
	}
}

void AdventureAI::hireHeroes()
{
	// (secondArmy) Troops enough for a second army wait in a town where no hero of ours stands: one is
	// hired there to lead them (the most experienced on offer) - gold that lies idle anyway.
	if(const double bar = secondArmyBar(); bar > 0 && static_cast<int>(perception.ownHeroes().size()) < params.maxHeroes)
	{
		for(const auto * town : perception.ownTowns())
		{
			if(!town->hasBuilt(BuildingID::TAVERN) || town->getVisitingHero() || town->getGarrisonHero())
				continue;
			if(spendable()[GameResID::GOLD] < GameConstants::HERO_GOLD_COST + params.heroGoldReserve)
				break;
			// the seven best stacks of what stands in the town and what is for sale there
			std::vector<double> stacks;
			for(const auto & slot : town->Slots())
				stacks.push_back(stackValue(slot.second.get()));
			std::sort(stacks.begin(), stacks.end(), std::greater<double>());
			double waiting = static_cast<double>(recruitableValue(town, spendable()[GameResID::GOLD] - GameConstants::HERO_GOLD_COST));
			for(size_t i = 0; i < stacks.size() && i < 7; ++i)
				waiting += stacks[i];
			if(waiting < bar)
				continue;
			const CGHeroInstance * best = nullptr;
			for(const auto * candidate : cb->getAvailableHeroes(town))
			{
				if(!best || candidate->level > best->level || (candidate->level == best->level && candidate->getTotalStrength() > best->getTotalStrength()))
					best = candidate;
			}
			if(!best)
				continue;
			logAi->info("FairAI %s day %d: hiring %s (level %d) in %s to lead the troops waiting there (%.0f; a second army needs %.0f)", me.toString(), today,
				best->getNameTranslated(), static_cast<int>(best->level), town->getNameTranslated(), waiting, bar);
			cb->recruitHero(town, best);
			gateway.waitTillFree();
			if(const auto * hired = town->getVisitingHero(); hired && hired->tempOwner == me)
			{
				// (veterans 2) an experienced hero hired for a second army leads the first if it is the better leader
				const ObjectInstanceID hiredId = hired->id;
				if(params.veterans >= 2 && hired->level >= 6)
					takeCommand(hired, town);
				if(const auto * now = ownHero(hiredId))
					manageTownVisit(now, town);
			}
			break; // one a day
		}
	}
	// (townHeroes) Troops stand in a town without a hero of ours: one is hired to lead them - into the
	// garrison at once, where the town's troops become its army.
	if(params.townHeroes && static_cast<int>(perception.ownHeroes().size()) < params.maxHeroes)
	{
		const CGHeroInstance * leader = ownHero(mainHero);
		const double real = std::max(params.heavyPartValue, params.heavyPartShare * (leader ? static_cast<double>(leader->getArmyStrength()) : 0.0));
		for(const auto * town : perception.ownTowns())
		{
			if(!town->hasBuilt(BuildingID::TAVERN) || town->getVisitingHero() || town->getGarrisonHero())
				continue;
			if(spendable()[GameResID::GOLD] < GameConstants::HERO_GOLD_COST + params.heroGoldReserve)
				break;
			double waiting = 0;
			for(const auto & slot : town->Slots())
				waiting += stackValue(slot.second.get());
			if(waiting < real)
				continue;
			const CGHeroInstance * best = nullptr;
			for(const auto * candidate : cb->getAvailableHeroes(town))
			{
				if(!best || candidate->level > best->level || (candidate->level == best->level && candidate->getTotalStrength() > best->getTotalStrength()))
					best = candidate;
			}
			if(!best)
				continue;
			logAi->info("FairAI %s day %d: hiring %s (level %d) in %s as the hero of the %.0f troops standing there", me.toString(), today,
				best->getNameTranslated(), static_cast<int>(best->level), town->getNameTranslated(), waiting);
			cb->recruitHero(town, best);
			gateway.waitTillFree();
			if(const auto * hired = town->getVisitingHero(); hired && hired->tempOwner == me)
			{
				// (veterans 2) an experienced hero does not sit in a garrison: it leads
				const ObjectInstanceID hiredId = hired->id;
				if(params.veterans >= 2 && hired->level >= 6)
					takeCommand(hired, town);
				hired = ownHero(hiredId);
				if(hired && isMain(hired))
					break;
				if(hired && hired->canBeMergedWith(*town))
				{
					cb->swapGarrisonHero(town);
					gateway.waitTillFree();
				}
				else if(hired)
					pickBestCreatures(hired, town);
			}
			break; // one a day
		}
	}
	int wanted = (today <= 7 ? params.heroesWeek1 : (today <= 21 ? params.heroesWeek3 : params.heroesLater)) + params.scouts;
	if(economy.rho > 2 && spendable()[GameResID::GOLD] > 8000)
		wanted += 2;
	// every lost hero feeds the enemy experience: after a loss only replace the minimum
	if(today - lastHeroLossDay < 7 && (params.heroLossPause || spendable()[GameResID::GOLD] <= 15000))
		wanted = std::min(wanted, 2);
	wanted = std::min(wanted, params.maxHeroes);
	const int64_t reserve = today <= 2 ? 0 : params.heroGoldReserve;
	// a hero of ours that surrendered waits in the tavern with its whole army - until the week ends
	const int waiting = rehireHero.load();
	const bool rehire = waiting >= 0 && today - rehireDay.load() < 7 && (today - 1) / 7 == (rehireDay.load() - 1) / 7;
	if(waiting >= 0 && !rehire)
		rehireHero = -1;

	std::vector<const CGTownInstance *> taverns;
	for(const auto * town : perception.ownTowns())
		if(town->hasBuilt(BuildingID::TAVERN))
			taverns.push_back(town);
	if(rehire && params.surrender >= 2)
	{
		// (surrender 2) the army we bought free comes back where the enemy is least likely to strike
		// it again tomorrow (the first version hired it back next to the army it had surrendered to)
		std::stable_sort(taverns.begin(), taverns.end(), [this](const CGTownInstance * a, const CGTownInstance * b)
		{
			return threatAt(a->visitablePos()) < threatAt(b->visitablePos());
		});
	}
	else if(params.veterans >= 2 && !taverns.empty())
	{
		// (veterans 2) an experienced hero on offer is hired where most of our troops at home stand:
		// it will lead them
		bool offered = false;
		for(const auto * candidate : cb->getAvailableHeroes(taverns.front()))
			offered = offered || (candidate->level >= 6 && wouldLead(candidate));
		if(offered)
		{
			auto force = [this](const CGTownInstance * town)
			{
				// (what the hero that leads now carries counts where it stands: the veteran takes it over there)
				double here = static_cast<double>(town->getUpperArmy()->getArmyStrength());
				if(const auto * visitor = town->getVisitingHero(); visitor && visitor->tempOwner == me)
					here += static_cast<double>(visitor->getArmyStrength()) * (isMain(visitor) ? 2.0 : 1.0);
				if(const auto * guard = town->getGarrisonHero(); guard && isMain(guard))
					here += static_cast<double>(guard->getArmyStrength());
				return here;
			};
			std::stable_sort(taverns.begin(), taverns.end(), [&force](const CGTownInstance * a, const CGTownInstance * b)
			{
				return force(a) > force(b);
			});
		}
	}
	for(const auto * town : taverns)
	{
		int heroes = static_cast<int>(perception.ownHeroes().size());
		if(params.townHeroes)
		{
			// (townHeroes) a town's hero is no collector or scout: it does not count against those we want
			for(const auto * own : perception.ownHeroes())
				heroes -= !isMain(own) && own->getVisitedTown() && heavy(own) ? 1 : 0;
		}

		// Who is on offer? Our hero that surrendered (with its army) comes first, then a veteran - a
		// hero of ours that fled, or anybody's fallen hero, with its levels and skills (parameter
		// veterans) -, else the one with the best starting army.
		const CGHeroInstance * best = nullptr;
		bool special = false;
		for(const auto * candidate : cb->getAvailableHeroes(town))
		{
			if(rehire && candidate->getHeroTypeID().getNum() == waiting)
			{
				best = candidate;
				special = true;
				break;
			}
			if(params.veterans && candidate->level >= 6 && (params.veterans != 2 || wouldLead(candidate)))
			{
				if(!special || candidate->level > best->level)
					best = candidate;
				special = true;
			}
			else if(!special && params.heroChoice)
			{
				const bool fighter = !ownHero(mainHero);
				if(!best || tavernHeroValue(candidate, fighter) > tavernHeroValue(best, fighter))
					best = candidate;
			}
			else if(!special && (!best || candidate->getTotalStrength() > best->getTotalStrength()))
				best = candidate;
		}
		if(!best)
			continue;
		if(special ? heroes >= params.maxHeroes : heroes >= wanted)
			return;
		if(static_cast<int>(perception.ownHeroes().size()) >= params.maxHeroes)
			return; // (with town heroes left out of the count above: never more than the game allows)
		if(spendable()[GameResID::GOLD] < GameConstants::HERO_GOLD_COST + (special ? 0 : reserve))
			return;

		// our hero stands in the gate: park him in the garrison while hiring, then swap back
		const CGHeroInstance * visitor = town->getVisitingHero();
		bool parked = false;
		if(visitor)
		{
			if(visitor->tempOwner != me || (today > 3 && !special))
				continue;
			if(!town->getGarrisonHero() && visitor->canBeMergedWith(*town))
			{
				cb->swapGarrisonHero(town);
				gateway.waitTillFree();
				if(town->getVisitingHero())
					continue; // swap refused
				parked = true;
			}
			else if((params.surrender >= 2 && rehire && best->getHeroTypeID().getNum() == waiting) || (params.veterans >= 2 && special && best->level >= 6))
			{
				// (surrender 2) Gate and garrison are both taken (a town's hero and a visitor), or their
				// troops do not fit into one army: the visitor steps out of the gate. The army we paid
				// for waits in the tavern only until the week ends - in the first probe game 66,000
				// gold bought an army of 99,000 free and both our towns were full for the four days left.
				if(!leaveGate(town, int3(-1), best->getNameTranslated()))
					continue;
			}
			else
				continue;
		}

		logAi->info("FairAI %s day %d: hiring %s (level %d) in %s%s", me.toString(), today, best->getNameTranslated(), static_cast<int>(best->level), town->getNameTranslated(),
			rehire && best->getHeroTypeID().getNum() == waiting ? " (back from its surrender, with its army)" : (special ? " (a veteran)" : ""));
		const bool back = rehire && best->getHeroTypeID().getNum() == waiting;
		const bool veteran = params.veterans >= 2 && special && !back && best->level >= 6;
		const HeroTypeID hiredType = best->getHeroTypeID();
		if(back)
			rehireHero = -1;
		cb->recruitHero(town, best);
		gateway.waitTillFree();
		if(veteran)
		{
			const CGHeroInstance * hired = town->getVisitingHero();
			if(hired && hired->tempOwner == me && hired->getHeroTypeID() == hiredType)
				takeCommand(hired, town);
			if(parked)
			{
				// the hero we moved out of the gate stays in the garrison: the gate is the leader's
				parked = false;
			}
		}

		if(parked)
		{
			const CGHeroInstance * hired = town->getVisitingHero();
			const CGHeroInstance * parkedHero = town->getGarrisonHero();
			if(hired && parkedHero)
			{
				// the main hero takes the new hero's troops, then leaves the garrison again
				if(isMain(parkedHero))
					pickBestCreatures(parkedHero, hired);
				cb->swapGarrisonHero(town);
				gateway.waitTillFree();
			}
		}
	}
}

double AdventureAI::tavernHeroValue(const CGHeroInstance * candidate, bool fighter) const
{
	// All of this is what the tavern shows of a hero: its troops, skills and specialty.
	if(!candidate)
		return 0;
	double value = static_cast<double>(candidate->getArmyStrength()) * params.goldPerArmyValue;
	for(const auto & [skill, level] : candidate->secSkills)
		value += skillValueAs(candidate, skill, level, fighter);
	// what the specialty brings in day by day (350 gold, one of a rare resource), over three weeks
	if(const auto bonuses = candidate->getBonusesOfType(BonusType::GENERATE_RESOURCE))
	{
		for(const auto & bonus : *bonuses)
		{
			if(bonus->source != BonusSource::HERO_SPECIAL)
				continue;
			const GameResID res = bonus->subtype.as<GameResID>();
			value += bonus->val * (res == GameResID::GOLD ? 1.0 : resourceValue(res)) * 21.0;
		}
	}
	// a leader fights with its attack and defence
	if(fighter)
		value += 300.0 * (candidate->getPrimSkillLevel(PrimarySkill::ATTACK) + candidate->getPrimSkillLevel(PrimarySkill::DEFENSE));
	return value;
}

void AdventureAI::buyTavernArmies()
{
	// Gold that cannot buy creatures any more still buys the starting armies of tavern heroes:
	// hire, hand the troops to the garrison, dismiss. Never used while gold is the bottleneck.
	if(!params.tavernArmies || economy.rho < 2.0)
		return;
	const int64_t reserve = 4000 + GameConstants::HERO_GOLD_COST;
	for(const auto * town : perception.ownTowns())
	{
		for(int bought = 0; bought < 2; ++bought)
		{
			gateway.checkInterruption();
			if(spendable()[GameResID::GOLD] < reserve || !town->hasBuilt(BuildingID::TAVERN) || town->getVisitingHero())
				break;
			// the game allows only so many heroes on the map at a time (the server refused the hire: eight are out already)
			if(cb->howManyHeroes(false) >= cb->getSettings().getInteger(EGameSettings::HEROES_PER_PLAYER_ON_MAP_CAP))
				break;
			// never a veteran: a hero of ours that fled or surrendered waits in the tavern to be hired back, not to be dismissed
			const CGHeroInstance * pick = nullptr;
			for(const auto * candidate : cb->getAvailableHeroes(town))
			{
				if(candidate->level <= 3 && candidate->getHeroTypeID().getNum() != rehireHero.load())
				{
					pick = candidate;
					break;
				}
			}
			if(!pick)
				break;
			std::set<ObjectInstanceID> before;
			for(const auto * h : perception.ownHeroes())
				before.insert(h->id);
			cb->recruitHero(town, pick);
			gateway.waitTillFree();
			const CGHeroInstance * hired = town->getVisitingHero();
			if(!hired || hired->tempOwner != me || before.count(hired->id))
				break;
			pickBestCreatures(town->getUpperArmy(), hired);
			logAi->info("FairAI %s day %d: bought the army of %s in %s", me.toString(), today, hired->getNameTranslated(), town->getNameTranslated());
			cb->dismissHero(hired);
			gateway.waitTillFree();
		}
	}
}

void AdventureAI::defendTowns()
{
	auto known = perception.knownObjects();
	for(const auto * town : perception.ownTowns())
	{
		bool threatened = false;
		for(const auto & [id, object] : known)
		{
			if(object.type == Obj::HERO && object.visibleNow && isEnemy(object.owner)
				&& enemyDistance(id, object.visitablePos, observedDanger(object) * heroFactor(object), town->visitablePos(), 14) < 14)
			{
				threatened = true;
				break;
			}
		}
		if(threatened)
		{
			savingsDay = -100; // (saveFor) the enemy stands before a town: nothing is saved for a building now
			recruitInTown(town, town->getUpperArmy(), 0);
		}
	}
}

}
