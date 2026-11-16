/*
 * AdventureAI.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AdventureAI.h"
#include "../Combat/CombatSim.h"

#include "../FairAI.h"
#include "../Perception/Perception.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include <vcmi/Faction.h>
#include "../../../lib/CConfigHandler.h"
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
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
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
	read("battlePlan", battlePlan);
	read("fightForecast", fightForecast);
	read("maxLossShare", maxLossShare);
	read("maxLossShareIdle", maxLossShareIdle);
	read("maxLossShareSecondary", maxLossShareSecondary);
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
	read("veterans", veterans);
	read("earlyGoldContact", earlyGoldContact);
	read("regionValue", regionValue);
	read("mainHorizon", mainHorizon);
	read("refillShare", refillShare);
	read("supplyRuns", supplyRuns);
	read("armyFirst", armyFirst);
	read("townAmbush", townAmbush);
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
	Combat::setMultiHex(params.simMultiHex != 0); // the forecasts of the adventure AI use the same simulator
}

AdventureAI::~AdventureAI() = default;

ResourceSet AdventureAI::spendable() const
{
	ResourceSet stock = cb->getResourceAmount();
	if(today - earmarkDay > 3)
		return stock;
	for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		stock[res] = std::max<TResource>(0, stock[res] - earmark[res]);
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
	double takeover = 1.5;
	if(params.couriers && current)
		takeover = std::clamp(1.5 + 0.25 * (static_cast<double>(current->level) - static_cast<double>(strongest->level)), 1.5, 3.0);
	if(!current || strongest->getTotalStrength() > takeover * current->getTotalStrength())
		mainHero = strongest->id;
}

bool AdventureAI::isMain(const CGHeroInstance * hero) const
{
	return hero && hero->id == mainHero;
}

std::shared_ptr<CPathsInfo> AdventureAI::computePaths(const CGHeroInstance * hero, bool ignoreGuards) const
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
	config->options.turnLimit = static_cast<uint8_t>(std::clamp(travelHorizon(hero) + 1, 1, 250));
	cb->calculatePaths(config);
	return result;
}

int AdventureAI::travelHorizon(const CGHeroInstance * hero) const
{
	// on campaign the main hero may march on a far town; couriers may follow it for a week to hand over troops
	if(campaign(hero))
		return std::max(params.maxTravelTurns, 30);
	// the main hero looks further than a few days (mainHorizon): a guard that bars the way out of our
	// corner of the map may stand a week's march from where the hero happens to be
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
	reservedBy.clear();
	pocketExits.clear();
	trail.clear();
	abandonedToday.clear();
	roundTrip.clear();
	forecastCache.clear();
	perception.refresh();
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
	for(const auto * hero : perception.ownHeroes())
	{
		if(const auto * town = hero->getVisitedTown())
			manageTownVisit(hero, town);
	}

	auto timeLeft = [&turnStart]()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - turnStart).count() < TURN_TIME_BUDGET_SEC;
	};

	if(const auto * main = ownHero(mainHero))
	{
		const int3 start = main->visitablePos();
		const ObjectInstanceID id = mainHero;
		mainProductive = false;
		playHero(mainHero);
		const auto * after = ownHero(id);
		// walking about without visiting, fighting or exploring is idle too (patience lowers the safety ratio)
		if(after && (params.concentrate ? !mainProductive : after->visitablePos().dist2d(start) < 3))
			++idleDays;
		else
			idleDays = 0;
	}

	hireHeroes();
	chooseMainHero();

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

	heroesLastTurn.clear();
	for(const auto * hero : perception.ownHeroes())
		heroesLastTurn.insert(hero->id);

	if(std::getenv("FAI_DUMP_MAP") && (today == 7 || today == 14 || today == 28))
		dumpKnowledge();

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
		logAi->debug("FairAI %s day %d: foreign towns known:%s; towns unfound %s; opposition heroes %.0f, guards %.0f; main hero strength %.0f, campaign %s",
			me.toString(), today, towns.empty() ? " none" : towns, huntWanted ? "yes" : "no", oppositionHeroes, oppositionGuards,
			main ? static_cast<double>(main->getTotalStrength()) : 0.0, campaignMode ? "on" : "off");
	}
	logAi->info("FairAI %s day %d end: gold %d, heroes %d, towns %d, main hero %s army %d, mines %d, wood %d, ore %d", me.toString(), today,
		cb->getResourceAmount(GameResID::GOLD), static_cast<int>(perception.ownHeroes().size()), static_cast<int>(perception.ownTowns().size()),
		main ? main->getNameTranslated() : "-", main ? static_cast<int>(main->getArmyStrength()) : 0, mines,
		cb->getResourceAmount(GameResID::WOOD), cb->getResourceAmount(GameResID::ORE));
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
		auto paths = computePaths(hero, false);
		auto freePaths = computePaths(hero, true);
		auto targets = supply && supply->courier == heroId ? supplyTargets(hero, *paths) : collectTargets(hero, *paths, *freePaths);
		if(supply && supply->courier == heroId && targets.empty())
			return;
		{
			// nothing worth doing on this landmass: the sea is the way on
			bool somethingToDo = false;
			for(const auto & t : targets)
				somethingToDo = somethingToDo || (t.description != "retreat" && t.description != "flee");
			if(!somethingToDo && boatAttempts < 2 && seekBoat(hero, *paths, targets))
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
		if(targets.empty() && isMain(hero) && !hero->getVisitedTown())
		{
			// nothing to do: wait in the nearest own town, where garrison and walls help
			const CGTownInstance * home = nullptr;
			double homeCost = 1e9;
			for(const auto * town : perception.ownTowns())
			{
				const CGPathNode * node = paths->getPathInfo(town->visitablePos());
				const bool safe = threatAt(town->visitablePos()) <= tolerance(hero) * defenceAt(hero, town->visitablePos());
				if(safe && node && node->reachable() && node->cost < homeCost && !failedToday.count({heroId.getNum(), town->visitablePos()}))
				{
					home = town;
					homeCost = node->cost;
				}
			}
			if(home && homeCost < 2.0)
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
		const Target & best = targets.front();
		if(targets.size() > 1 && step == 0)
			logAi->debug("FairAI %s: %s alternatives: %s %.0f, %s %.0f", me.toString(), hero->getNameTranslated(),
				targets[1].description, targets[1].score, targets.size() > 2 ? targets[2].description : "-", targets.size() > 2 ? targets[2].score : 0.0);
		if(best.score <= 0)
			return;

		logAi->debug("FairAI %s: %s -> %s %s value %.0f loss %.0f travel %.2f score %.0f", me.toString(), hero->getNameTranslated(),
			best.description, best.tile.toString(), best.value, best.loss, best.travel, best.score);

		const bool safetyMove = best.description == "retreat" || best.description == "flee" || best.description == "home" || best.description == "aside" || best.description == "way out";
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
		activeHero = heroId.getNum();
		bool sent = stepTowards(hero, best.tile, *paths);
		if(sent)
			gateway.waitTillFree();
		activeHero = -1;

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
		if(sent && !progress && hero->movementPointsRemaining() != mpBefore && !best.object.hasValue())
			progress = true; // no object to check: trust the movement
		if(sent && hero->visitablePos().dist2d(posBefore) > 2.5)
		{
			// teleported: both ends of the channel are explored now
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

bool AdventureAI::seekBoat(const CGHeroInstance * hero, const CPathsInfo & paths, std::vector<Target> & targets)
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
	return false;
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
	return isMain(hero) ? params.expValueMain : params.expValueOther;
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

double AdventureAI::expectedLoss(const CGHeroInstance * hero, double danger, bool desperate, bool versusHero) const
{
	if(danger <= 0)
		return 0;
	// Against heroes (heroModel): both sides count as army times what attack and defence add - the
	// measure that told winner from loser in the logged battles; `danger` is given in the same measure.
	const bool model = params.heroModel && versusHero;
	const double strength = model ? fieldStrength(hero) : static_cast<double>(hero->getTotalStrength());
	double ratio = isMain(hero) ? (versusHero ? (model ? params.heroEdge : params.safetyRatioHero) : params.safetyRatio) : params.safetyRatioSecondary;
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
	return static_cast<double>(hero->getArmyStrength()) * hero->getFightingStrength() * std::sqrt((1.0 + std::max(0.0, melee)) / (1.0 - armour));
}

AdventureAI::FightForecast AdventureAI::forecastMonsterFight(const CGHeroInstance * hero, CreatureID creature, int count) const
{
	FightForecast result;
	const CCreature * type = creature.toCreature();
	if(!hero || !type || count <= 0 || hero->stacksCount() == 0)
		return result;

	// same question, same answer: the hero's army and the guards do not change between planning steps
	uint64_t signature = 1469598103934665603ULL;
	for(const auto & [slot, stack] : hero->Slots())
	{
		if(!stack || !stack->getCreature())
			continue;
		signature = (signature ^ static_cast<uint64_t>(stack->getCreatureID().getNum() * 100003 + slot.getNum())) * 1099511628211ULL;
		signature = (signature ^ static_cast<uint64_t>(stack->getCount())) * 1099511628211ULL;
	}
	const auto key = std::make_tuple(hero->id.getNum(), signature, creature.getNum(), count);
	if(auto cached = forecastCache.find(key); cached != forecastCache.end())
		return cached->second;

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
		const Combat::Forecast forecast = Combat::forecastBattle(stacks, BattleSide::ATTACKER, layout);
		const int plan = forecast.bestPlan();
		result.win = forecast.win(plan);
		result.lossShare = forecast.lossShare(plan);
		result.lossValue = result.lossShare * forecast.ownBefore;
	}
	catch(const std::exception & e)
	{
		logAi->warn("FairAI %s: battle forecast failed (%s)", me.toString(), e.what());
	}
	forecastCache[key] = result;
	return result;
}

double AdventureAI::monsterLoss(const CGHeroInstance * hero, const ObservedObject & monster) const
{
	if(!monster.army)
		return 0;
	if(!params.fightForecast || monster.army->units.size() != 1 || !monster.army->units.front().creature.hasValue())
		return expectedLoss(hero, observedDanger(monster));

	// the map shows a range ("a pack": 10-19): judge the likely size, and survive the largest
	const ObservedStack & seen = monster.army->units.front();
	const double middle = 0.5 * (seen.minCount + seen.maxCount);
	const int likelyCount = std::max(1, static_cast<int>(std::lround((1 - params.dangerUpperWeight) * middle + params.dangerUpperWeight * seen.maxCount)));
	const FightForecast likely = forecastMonsterFight(hero, seen.creature, likelyCount);
	if(!likely.win)
		return -1;
	double share = likely.lossShare;
	if(seen.maxCount > likelyCount)
	{
		const FightForecast worst = forecastMonsterFight(hero, seen.creature, seen.maxCount);
		if(!worst.win)
			return -1;
		share = 0.7 * likely.lossShare + 0.3 * worst.lossShare;
	}
	const double limit = isMain(hero) ? std::min(0.6, params.maxLossShare + params.maxLossShareIdle * idleDays) : params.maxLossShareSecondary;
	if(share > limit)
		return -1;
	return share * static_cast<double>(hero->getArmyStrength()) * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
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
	for(const auto & [id, object] : known)
	{
		if(object.type != Obj::HERO || !isEnemy(object.owner) || !object.army)
			continue;
		const int age = today - object.lastSeenDay;
		if(age > 3)
			continue;
		Threat t;
		t.pos = object.visitablePos;
		const double factor = heroFactor(object);
		t.strength = observedDanger(object) * factor * (1.0 + params.threatGrowth * age);
		if(age == 0)
			enemySightings[object.subtype.getNum()] = Sighting{today, observedDanger(object) * factor};
		t.radius = params.threatRadius * (1 + 0.5 * age);
		t.age = age;
		t.hero = id;
		threats.push_back(t);
	}
	// the opposition we know of: enemy heroes seen in the last three weeks (they grow meanwhile), guards on the map
	oppositionHeroes = 0;
	for(const auto & [type, seen] : enemySightings)
	{
		if(today - seen.day <= 21)
			oppositionHeroes = std::max(oppositionHeroes, seen.strength * (1.0 + 0.03 * (today - seen.day)));
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
			if(seen == enemySightings.end() || today - seen->second.day > 28)
				mainsSeen = false;
		}
		// no tavern, no guild: at least something of weight must have shown itself
		if(!guildKnown && leader && oppositionHeroes < 0.25 * fieldStrength(leader))
			mainsSeen = false;
		if(!params.leaderFocus || !leader || today < 21 || oppositionHeroes <= 0)
			campaignMode = false;
		else
		{
			const double edge = fieldStrength(leader) / oppositionHeroes;
			if(edge >= params.campaignEdge && mainsSeen)
				campaignMode = true;
			else if(edge < 0.65 * params.campaignEdge)
				campaignMode = false;
		}
		if(before != campaignMode)
			logAi->info("FairAI %s day %d: campaign %s (main hero %.0f, strongest enemy hero seen lately %.0f, their best heroes seen: %s)", me.toString(), today,
				campaignMode ? "on" : "off", fieldStrength(leader), oppositionHeroes, mainsSeen ? "yes" : "no");
		// On campaign the hero does not walk home for a trickle. But what has grown at home to a good
		// part of its own army is what decides the war (a campaign fought with half of our troops,
		// the other half waiting in the towns, was how games were lost): fetch it, then march again.
		const bool refillBefore = refillMode;
		if(campaignMode && leader && params.refillShare > 0)
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
	}
	// Does an enemy still have towns we have not found? Every enemy in the game has at least one
	// (or is about to drop out); the thieves' guild ranks the players by their number of towns.
	huntWanted = false;
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
	grow(skills, 1.0, std::min(25.0, weight * today / 5.5));
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
			factor *= known->second.magic;
	}
	return factor;
}

void AdventureAI::noteHeroSpell(HeroTypeID hero, SpellID spell, int affected)
{
	if(!hero.hasValue() || !spell.hasValue())
		return;
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
			const Combat::Forecast forecast = Combat::forecastBattle(stacks, BattleSide::ATTACKER, layout);
			const int plan = forecast.bestPlan();
			result.win = forecast.win(plan);
			result.lossShare = forecast.lossShare(plan);
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
	logAi->debug("FairAI %s: %s against hero %d: forecast loss %.2f (at most %.2f), enemy attack %d defence %d, magic x%.2f", me.toString(),
		hero->getNameTranslated(), enemy.subtype.getNum(), likely.lossShare, worst.lossShare, attack, defence, magic);
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
	if(params.townAmbush && hiddenMain > worst)
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
		return isMain(hero) ? params.heroTolerance : 0.9 * params.heroTolerance;
	return isMain(hero) ? params.threatTolerance : params.threatToleranceSecondary;
}

double AdventureAI::defenceAt(const CGHeroInstance * hero, const int3 & tile) const
{
	double strength = fieldStrength(hero);
	for(const auto * town : perception.ownTowns())
	{
		if(town->visitablePos() != tile)
			continue;
		double garrison = 0;
		for(const auto & slot : town->Slots())
			garrison += stackValue(slot.second.get());
		if(const auto * guard = town->getGarrisonHero(); guard && guard != hero)
			garrison += static_cast<double>(guard->getArmyStrength());
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
	for(const auto * town : perception.ownTowns())
	{
		// a hero of ours in the gate carries; one in the garrison comes out when the gate is free
		const CGHeroInstance * carrier = town->getVisitingHero();
		if(carrier && (carrier->tempOwner != me || carrier->id == mainHero))
			continue; // the leader itself is here (or a guest): it takes what it needs
		if(!carrier)
			carrier = town->getGarrisonHero();
		if(!carrier || carrier->tempOwner != me || carrier->id == mainHero)
			continue;
		supplyPossible = true;
		const double stock = townTroops(town, leader);
		if(stock < std::max(1500.0, params.supplyShare * army) || stock <= bestStock)
			continue;
		bestTown = town;
		bestCarrier = carrier;
		bestStock = stock;
	}
	if(!bestTown || leader->getVisitedTown() == bestTown)
		return;
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
	auto paths = computePaths(carrier, false);
	const CGPathNode * node = paths->getPathInfo(leader->visitablePos());
	if(!node || !node->reachable() || node->turns > params.supplyDays)
	{
		logAi->debug("FairAI %s day %d: troops worth %.0f wait in %s, the leader is out of reach (%s)", me.toString(), today, bestStock,
			bestTown->getNameTranslated(), node && node->reachable() ? (boost::format("%d days") % static_cast<int>(node->turns)).str() : "no way");
		return;
	}
	const double carried = static_cast<double>(carrier->getArmyStrength()) + bestStock;
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
	// can the hero still take a step towards `tile` today? (a path exists, and the next tile is within today's movement)
	auto stepToday = [&](const int3 & tile) -> int // 1 yes, 0 not today, -1 no way
	{
		CGPath path;
		if(!paths.getPath(path, tile) || path.nodes.size() < 2 || failedToday.count({hero->id.getNum(), tile}))
			return -1;
		return path.nodes[path.nodes.size() - 2].turns > 0 ? 0 : 1;
	};
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
		const int step = stepToday(leader->visitablePos());
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
	auto paths = computePaths(visitor, false);
	int3 best(-1);
	float bestCost = std::numeric_limits<float>::max();
	for(int dx = -1; dx <= 1; ++dx)
	{
		for(int dy = -1; dy <= 1; ++dy)
		{
			const int3 tile(gate.x + dx, gate.y + dy, gate.z);
			if(tile == gate || tile == mainHero->visitablePos() || !cb->isInTheMap(tile))
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
		return;
	logAi->debug("FairAI %s: %s leaves the gate of %s to let %s in", me.toString(), visitor->getNameTranslated(),
		town->getNameTranslated(), mainHero->getNameTranslated());
	cb->moveHero(visitor, visitor->convertFromVisitablePos(best), false);
	gateway.waitTillFree();
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

double AdventureAI::reinforcementBoost(double troops) const
{
	// Troops in a garrison win nothing. For the main hero a reinforcement that is big compared with
	// its army is worth more than its size: guards it had to leave alone come into reach.
	const CGHeroInstance * leader = ownHero(mainHero);
	if(!leader)
		return 1.0;
	const double army = std::max(1.0, static_cast<double>(leader->getArmyStrength()));
	if(params.fetchByShare)
	{
		// Troops waiting in a town do not run away: fetching them only brings their use forward. A
		// trickle (a few per cent of the army) is not worth a trip - it would keep the hero circling
		// its town; a week's growth that makes the army half as big again is.
		return std::clamp(troops / (params.fetchShare * army), 0.1, 1.5);
	}
	if(!params.couriers)
		return 1.0;
	return 1.0 + std::min(2.0, troops / army);
}

double AdventureAI::objectValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const
{
	const bool main = isMain(hero);
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
		if(params.artifactCare)
		{
			// what it would do for the hero leading our army; the others fetch it for that hero.
			// Even an artifact that only does harm is worth picking up: the enemy shall not have it.
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
				value += creature->getGrowth() * creature->getAIValue() * params.goldPerArmyValue * 0.5 * params.dwellingHorizonWeeks * armyFactor;
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
	case Obj::GARDEN_OF_REVELATION:
	case Obj::MERCENARY_CAMP:
	case Obj::STAR_AXIS:
		return visitedByHero() ? 0 : primary;
	case Obj::ARENA:
		return visitedByHero() ? 0 : 2 * primary;
	case Obj::SCHOOL_OF_MAGIC:
	case Obj::SCHOOL_OF_WAR:
		if(visitedByHero() || !main || spendable()[GameResID::GOLD] < 3000)
			return 0;
		return 2 * primary - 1000;
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
	case Obj::FOUNTAIN_OF_FORTUNE:
	case Obj::FAERIE_RING:
	case Obj::IDOL_OF_FORTUNE:
	case Obj::TEMPLE:
	case Obj::RALLY_FLAG:
	case Obj::OASIS:
	case Obj::WATERING_HOLE:
	case Obj::FOUNTAIN_OF_YOUTH:
	case Obj::SWAN_POND:
		return (!main || visitedByHero()) ? 0 : 150;
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
			if(!hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1))
				value += 300;
			auto trip = roundTrip.find(hero->id);
			const bool onTrip = trip != roundTrip.end() && trip->second.town == town->id;
			if(params.townGuard && !onTrip)
			{
				// the town cannot hold on its own against what can reach it: the main hero should stand guard
				value += params.townDefenceValue * townNeed(town, hero);
			}
			else if(enemySeenNear(town->visitablePos(), 3, 20))
				value += 2500; // enemies seen near the town: the main hero should stand guard
			return value > 300 ? value : 0;
		}
		// without a town the game is lost after seven days: any town is worth everything
		if(perception.ownTowns().empty())
			return 100000;
		if(!campaign(hero))
			return enemy ? params.enemyTownValue : params.neutralTownValue;
		// On campaign: the enemy's towns are how the game is won, and a town taken late is a built-up one
		return (enemy ? 2.0 * params.enemyTownValue : params.neutralTownValue) * (1.0 + today / 28.0);
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
			const double gain = main ? transferGain(hero, other) : (other->id == mainHero ? transferGain(other, hero) : 0.0);
			if(gain < 1)
				return 0;
			// the main hero walks back for troops only as far as it needs them; the others bring them anyway
			// - but a courier on its supply run is met halfway: both walking, the gap closes twice as fast
			const bool courier = supply && supply->courier == other->id && !supply->returning;
			return gain * params.goldPerArmyValue * params.ferryFactor * armyFactor * reinforcementBoost(gain) * (main && !courier ? fetchFactor(hero) : 1.0);
		}
		if(!enemy || !object.visibleNow || !object.army)
			return 0;
		return 3000 + object.army->expectedStrength() * params.goldPerArmyValue;
	}
	default:
		return 0;
	}
}

std::vector<Target> AdventureAI::collectTargets(const CGHeroInstance * hero, const CPathsInfo & paths, const CPathsInfo & freePaths)
{
	std::vector<Target> out;
	const auto known = perception.knownObjects();
	const int3 heroPos = hero->visitablePos();
	const int heroNum = hero->id.getNum();
	const int horizon = travelHorizon(hero);
	const int exploreHorizon = campaign(hero) || (isMain(hero) && params.mainHorizon > params.exploreMaxTurns) ? horizon : params.exploreMaxTurns;

	auto reachable = [&](const CPathsInfo & p, const int3 & tile) -> const CGPathNode *
	{
		const CGPathNode * node = p.getPathInfo(tile);
		if(!node || !node->reachable() || node->turns > horizon)
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
	const bool cautious = !isMain(hero);
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
		if(threatAt(tile, partner ? ObjectInstanceID() : object, cautious) > tolerance(hero) * (defenceAt(hero, tile) + together))
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
	auto addTarget = [&](const int3 & tile, ObjectInstanceID object, double value, double loss, double travel, const std::string & description)
	{
		if(roundTripBudget >= 0 && travel * 2 > roundTripBudget)
		{
			auto it = object.hasValue() ? known.find(object) : known.end();
			const bool enemyHero = it != known.end() && it->second.type == Obj::HERO && isEnemy(it->second.owner);
			// the threat itself may be attacked, but only today: walking off towards it leaves the town open
			if(!enemyHero || (params.antiOscillation && travel > roundTripBudget))
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

		if(const CGPathNode * node = reachable(paths, object.visitablePos))
		{
			const bool foreignHero = object.type == Obj::HERO && object.owner != me;
			const bool foreignTown = object.type == Obj::TOWN && object.owner != me;
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
			if(campaign(hero) && !foreignTown && !foreignHero)
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
				++rejected["too strong"];
				continue;
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
			addTarget(object.visitablePos, id, value, loss, node->cost, "object " + std::to_string(object.type.getNum()));
		}
		else if(const CGPathNode * free = reachable(freePaths, object.visitablePos))
		{
			++rejected["behind guard"];
			const GuardOnPath g = firstGuard(free);
			if(g.guard.hasValue())
			{
				unlockValue[g.guard] += value * unlockShare(g, free);
				if(object.type == Obj::TOWN && object.owner != me)
					strategicUnlock[g.guard] += value * unlockShare(g, free);
			}
		}
		else if(params.seekTowns && object.type == Obj::TOWN && object.owner != me && !isAlly(object.owner)
			&& (isMain(hero) || scoutsToday.count(hero->id)) && (!isEnemy(object.owner) || seeking || campaign(hero) || !isMain(hero)))
		{
			seek.push_back({object.visitablePos.z, value, fogDistances(object.visitablePos)});
		}
		else if(campaign(hero) && object.type == Obj::TOWN && isEnemy(object.owner) && object.visitablePos.z == heroPos.z)
		{
			// an enemy town we know of but cannot find a way to (the land around it is hidden, or the
			// way leads around something): exploring is drawn towards it
			approach.push_back({object.visitablePos, value});
		}
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
		std::map<ObjectInstanceID, double> behindGuard;
		std::map<ObjectInstanceID, double> fogBehind; // most tiles one step behind the guard would reveal
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
					if(!normal && !(free && free->turns <= exploreHorizon && free->action == EPathNodeAction::NORMAL))
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
							behindGuard[g.guard] = std::max(behindGuard[g.guard], value * unlockShare(g, free));
							if(g.count == 1)
								fogBehind[g.guard] = std::max(fogBehind[g.guard], reveal);
						}
					}
				}
			}
		}
		if(bestExplore.score > 0)
			addTarget(bestExplore.tile, ObjectInstanceID(), bestExplore.value, 0, bestExplore.travel, "explore");
		for(const auto & [guard, value] : behindGuard)
		{
			unlockValue[guard] += value;
			strategicUnlock[guard] += value;
		}
		// Land we have not seen behind a guard is more than the strip the first step reveals: on a
		// map of zones it is a new region with its mines, treasures and perhaps a town.
		if(params.regionValue > 0 && isMain(hero))
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
		double loss = monsterLoss(hero, object);
		if(params.fightForecast && isMain(hero) && forecastLogDay != today * 100 + heroNum % 100 && logAi->isDebugEnabled() && object.army->units.size() == 1)
		{
			// once a day: how the main hero judges every guard in reach (diagnostics)
			const ObservedStack & seen = object.army->units.front();
			const double middle = 0.5 * (seen.minCount + seen.maxCount);
			const int likelyCount = std::max(1, static_cast<int>(std::lround((1 - params.dangerUpperWeight) * middle + params.dangerUpperWeight * seen.maxCount)));
			const FightForecast likely = forecastMonsterFight(hero, seen.creature, likelyCount);
			const FightForecast worst = forecastMonsterFight(hero, seen.creature, seen.maxCount);
			forecastLines += boost::str(boost::format(" [%s %d-%d: %s %.2f, at most %s %.2f, old rule %s, loss %.0f]")
				% (seen.creature.hasValue() ? seen.creature.toCreature()->getJsonKey() : std::string("?")) % seen.minCount % seen.maxCount
				% (likely.win ? "win" : "LOSE") % likely.lossShare % (worst.win ? "win" : "LOSE") % worst.lossShare
				% (expectedLoss(hero, danger) < 0 ? "no" : "yes") % loss);
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
		if(isMain(hero))
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
					if(node->action != EPathNodeAction::NORMAL && !ownTown)
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
			addTarget(bestRetreat.tile, ObjectInstanceID(), bestRetreat.value, 0, bestRetreat.travel, "retreat");
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
			if(bestDistance > here + params.fleeGain)
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
	const double perPercent = fighter ? 140.0 : 15.0; // one percent more fighting strength
	const double perMove = fighter ? 200.0 : 120.0;   // one percent more movement
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
			value += v * (fighter ? (necromancer ? 270.0 : 80.0) : 20.0);
			break;
		case BonusType::MAX_LEARNABLE_SPELL_LEVEL:
		{
			// third level spells matter most; the fifth guild level is rarely built
			const double steps = v >= 3 ? 3600 : (v >= 2 ? 3000 : (v >= 1 ? 2000 : 0));
			value += fighter ? steps * (caster ? 1.0 : 0.5) : 150.0 * v;
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
			value += v * (fighter ? school * (caster ? 1.0 : 0.4) : 150.0);
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
				value += v * skillPoint * scope;
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
		cb->recruitCreatures(dwelling, destination, creature, amount, level);
	}
}

// ======================================================================= armies

void AdventureAI::pickBestCreatures(const CArmedInstance * destination, const CArmedInstance * source)
{
	const bool sourceIsHero = source->ID == Obj::HERO;

	// An empty town is taken by any scout walking in: leave the weakest stack behind
	SlotID reserved;
	if(!sourceIsHero && params.keepGarrison && destination->ID == Obj::HERO)
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
		ResourceSet available = spendable();
		available[GameResID::GOLD] = std::max<int64_t>(0, available[GameResID::GOLD] - goldReserve);
		int amount = static_cast<int>(entry.first);
		for(GameResID res : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			int cost = type->getRecruitCost(res);
			if(cost > 0)
				amount = std::min(amount, static_cast<int>(available[res] / cost));
		}
		if(amount <= 0)
			continue;
		const CArmedInstance * target = destination;
		if(!target->getSlotFor(creature).validSlot())
			target = town->getUpperArmy();
		if(!target->getSlotFor(creature).validSlot())
			continue;
		cb->recruitCreatures(town, target, creature, amount, level);
	}
}

void AdventureAI::leaveGarrison(const CGHeroInstance * hero, const CGTownInstance * town)
{
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
	const double stock = spendable()[GameResID::GOLD];
	e.rho = (e.weeklyIncome + 0.5 * stock) / std::max(500.0, e.weeklyRecruitCost);
	e.armyPremium = std::clamp(e.rho, 1.0, 4.0);
	e.goldWeight = 1.0 / e.armyPremium;
	economy = e;
}

void AdventureAI::upgradeArmy(const CGHeroInstance * hero)
{
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
		auto existing = options.find(candidate);
		if(existing == options.end() || existing->second.score < score)
			options[candidate] = Option{candidate, state, score, info->buildings.at(goal)->getNameTranslated()};
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
	if(params.armyFirst)
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
		if(option.score < 0.25 || attempts >= 4)
			return;
		// a building that would leave troops unbought has to be worth more per gold than they are
		if(params.armyFirst && option.score < troopScore
			&& static_cast<double>(spendable()[GameResID::GOLD]) - info->buildings.at(option.build)->resources[GameResID::GOLD] < troopGold)
		{
			if(armyFirstLog != today)
				logAi->debug("FairAI %s day %d: %s in %s waits (score %.2f) - troops for %.0f gold come first (worth %.2f per gold)", me.toString(), today,
					info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.score, troopGold, troopScore);
			armyFirstLog = today;
			return;
		}
		if(option.state == EBuildingState::NO_RESOURCES && !buyResourcesFor(town, info->buildings.at(option.build)->resources, false, &keep))
			continue;
		if(!spendable().canAfford(info->buildings.at(option.build)->resources))
			continue; // the rest is put aside (see earmark)
		++attempts;
		logAi->info("FairAI %s day %d: building %s in %s (for %s, score %.2f)", me.toString(), today,
			info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.reason, option.score);
		cb->buildBuilding(town, option.build);
		if(town->hasBuilt(option.build))
			return;
		logAi->warn("FairAI %s: building %s was rejected", me.toString(), info->buildings.at(option.build)->getNameTranslated());
	}
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

void AdventureAI::manageTowns(bool /*endOfTurn*/)
{
	for(const auto * town : perception.ownTowns())
	{
		gateway.checkInterruption();
		updateEconomy();
		buildInTown(town);
		// troops in the garrison defend the town and wait for the main hero
		const int64_t reserve = economy.rho > 1.5 ? 0 : 1000;
		recruitInTown(town, town->getUpperArmy(), reserve);
	}
}

void AdventureAI::hireHeroes()
{
	int wanted = (today <= 7 ? params.heroesWeek1 : (today <= 21 ? params.heroesWeek3 : params.heroesLater)) + params.scouts;
	if(economy.rho > 2 && spendable()[GameResID::GOLD] > 8000)
		wanted += 2;
	// every lost hero feeds the enemy experience: after a loss only replace the minimum
	if(today - lastHeroLossDay < 7)
		wanted = std::min(wanted, 2);
	wanted = std::min(wanted, params.maxHeroes);
	const int64_t reserve = today <= 2 ? 0 : params.heroGoldReserve;
	// a hero of ours that surrendered waits in the tavern with its whole army - until the week ends
	const int waiting = rehireHero.load();
	const bool rehire = waiting >= 0 && today - rehireDay.load() < 7 && (today - 1) / 7 == (rehireDay.load() - 1) / 7;
	if(waiting >= 0 && !rehire)
		rehireHero = -1;

	for(const auto * town : perception.ownTowns())
	{
		if(!town->hasBuilt(BuildingID::TAVERN))
			continue;
		const int heroes = static_cast<int>(perception.ownHeroes().size());

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
			if(params.veterans && candidate->level >= 6)
			{
				if(!special || candidate->level > best->level)
					best = candidate;
				special = true;
			}
			else if(!special && (!best || candidate->getTotalStrength() > best->getTotalStrength()))
				best = candidate;
		}
		if(!best)
			continue;
		if(special ? heroes >= params.maxHeroes : heroes >= wanted)
			return;
		if(spendable()[GameResID::GOLD] < GameConstants::HERO_GOLD_COST + (special ? 0 : reserve))
			return;

		// our hero stands in the gate: park him in the garrison while hiring, then swap back
		const CGHeroInstance * visitor = town->getVisitingHero();
		bool parked = false;
		if(visitor)
		{
			if(visitor->tempOwner != me || town->getGarrisonHero() || (today > 3 && !special) || !visitor->canBeMergedWith(*town))
				continue;
			cb->swapGarrisonHero(town);
			gateway.waitTillFree();
			if(town->getVisitingHero())
				continue; // swap refused
			parked = true;
		}

		logAi->info("FairAI %s day %d: hiring %s (level %d) in %s%s", me.toString(), today, best->getNameTranslated(), static_cast<int>(best->level), town->getNameTranslated(),
			rehire && best->getHeroTypeID().getNum() == waiting ? " (back from its surrender, with its army)" : (special ? " (a veteran)" : ""));
		if(rehire && best->getHeroTypeID().getNum() == waiting)
			rehireHero = -1;
		cb->recruitHero(town, best);
		gateway.waitTillFree();

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
			recruitInTown(town, town->getUpperArmy(), 0);
	}
}

}
