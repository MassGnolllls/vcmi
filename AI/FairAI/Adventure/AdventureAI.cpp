/*
 * AdventureAI.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AdventureAI.h"

#include "../FairAI.h"
#include "../Perception/Perception.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CConfigHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/entities/ResourceTypeHandler.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/IMarket.h"
#include "../../../lib/ResourceSet.h"
#include "../../../lib/gameState/UpgradeInfo.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"

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

private:
	int3 size;
	std::vector<float> weights;
	std::vector<std::pair<int, int>> offsets;
	int cachedRadius = -1;
	size_t index(int x, int y, int z) const { return (static_cast<size_t>(z) * size.y + y) * size.x + x; }
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
	read("townDefenceValue", townDefenceValue);
	read("retreatValue", retreatValue);
	read("collectorRadius", collectorRadius);
	read("wallsFactor", wallsFactor);
	read("maxHeroes", maxHeroes);
}

// ======================================================================= setup

AdventureAI::AdventureAI(FairAIGateway & gateway, std::shared_ptr<CCallback> cb, Perception & perception, PlayerColor me)
	: gateway(gateway)
	, cb(std::move(cb))
	, perception(perception)
	, me(me)
{
	params.loadOverrides(me.getNum());
}

AdventureAI::~AdventureAI() = default;

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
		if(!strongest || hero->getTotalStrength() > strongest->getTotalStrength())
			strongest = hero;
	}
	if(!strongest)
	{
		mainHero = ObjectInstanceID();
		return;
	}
	if(!current || strongest->getTotalStrength() > 1.5 * current->getTotalStrength())
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
	// Phase 2 keeps to land: no boats, monoliths or gates yet
	config->options.useEmbarkAndDisembark = false;
	config->options.useTeleportTwoWay = false;
	config->options.useTeleportOneWay = false;
	config->options.useTeleportOneWayRandom = false;
	config->options.useTeleportWhirlpool = false;
	config->options.turnLimit = static_cast<uint8_t>(std::clamp(params.maxTravelTurns + 1, 1, 250));
	cb->calculatePaths(config);
	return result;
}

// ======================================================================= turn

void AdventureAI::playTurn()
{
	const auto turnStart = std::chrono::steady_clock::now();
	today = cb->getDate(Date::DAY);
	failedToday.clear();
	reservedBy.clear();
	trail.clear();
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
	updateEconomy();
	logAi->info("FairAI %s day %d economy: income %.0f/week, growth costs %.0f/week, rho %.2f", me.toString(), today,
		economy.weeklyIncome, economy.weeklyRecruitCost, economy.rho);

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

	if(ownHero(mainHero))
		playHero(mainHero);

	hireHeroes();
	chooseMainHero();

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
	for(auto id : others)
	{
		if(!timeLeft())
		{
			logAi->warn("FairAI: turn time budget exhausted on day %d", today);
			break;
		}
		playHero(id);
	}

	hireHeroes();
	manageTowns(true);
	defendTowns();

	heroesLastTurn.clear();
	for(const auto * hero : perception.ownHeroes())
		heroesLastTurn.insert(hero->id);

	const auto * main = ownHero(mainHero);
	logAi->info("FairAI %s day %d end: gold %d, heroes %d, towns %d, main hero %s army %d", me.toString(), today,
		cb->getResourceAmount(GameResID::GOLD), static_cast<int>(perception.ownHeroes().size()), static_cast<int>(perception.ownTowns().size()),
		main ? main->getNameTranslated() : "-", main ? static_cast<int>(main->getArmyStrength()) : 0);
}

void AdventureAI::playHero(ObjectInstanceID heroId)
{
	int failures = 0;
	struct EndOfDayLog
	{
		AdventureAI & ai;
		ObjectInstanceID id;
		~EndOfDayLog()
		{
			const auto * hero = ai.ownHero(id);
			if(hero)
				logAi->info("FairAI %s: %s%s ends at %s, strength %d, known threat %.0f (cautious %.0f)", ai.me.toString(), hero->getNameTranslated(),
					ai.isMain(hero) ? " (main)" : "", hero->visitablePos().toString(), static_cast<int>(hero->getTotalStrength()),
					ai.threatAt(hero->visitablePos()), ai.threatAt(hero->visitablePos(), ObjectInstanceID(), true));
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
			if(!town || town->getVisitingHero())
				return; // the gate is taken
			cb->swapGarrisonHero(town);
			gateway.waitTillFree();
			hero = ownHero(heroId);
			if(!hero || hero->isGarrisoned())
				return;
		}

		perception.refresh();
		updateThreats();
		auto paths = computePaths(hero, false);
		auto freePaths = computePaths(hero, true);
		auto targets = collectTargets(hero, *paths, *freePaths);
		if(targets.empty() && isMain(hero) && !hero->getVisitedTown())
		{
			// nothing to do: wait in the nearest own town, where garrison and walls help
			const CGTownInstance * home = nullptr;
			double homeCost = 1e9;
			for(const auto * town : perception.ownTowns())
			{
				const CGPathNode * node = paths->getPathInfo(town->visitablePos());
				if(node && node->reachable() && node->cost < homeCost && !failedToday.count({heroId.getNum(), town->visitablePos()}))
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

		currentGoal[heroId] = best.tile;
		reservedBy.erase(heroId);
		if(best.object.hasValue())
			reservedBy[heroId] = best.object;

		const int3 posBefore = hero->visitablePos();
		const int mpBefore = hero->movementPointsRemaining();
		const auto objectBefore = perception.knownObjects();
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

		bool progress = sent && (hero->visitablePos() != posBefore || hero->movementPointsRemaining() != mpBefore);
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
			failedToday.insert({heroId.getNum(), best.tile});
			if(++failures >= MAX_FAILURES_PER_HERO)
				return;
			continue;
		}
		failures = 0;

		if(const auto * town = hero->getVisitedTown())
			manageTownVisit(hero, town);
	}
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

double AdventureAI::expectedLoss(const CGHeroInstance * hero, double danger, bool desperate) const
{
	if(danger <= 0)
		return 0;
	const double strength = static_cast<double>(hero->getTotalStrength());
	double ratio = isMain(hero) ? params.safetyRatio : params.safetyRatioSecondary;
	if(desperate)
		ratio = 1.0; // no town left: a coin flip is better than certain defeat
	if(strength < danger * ratio)
		return -1;
	// Lanchester square law: survivors = sqrt(S^2 - D^2)
	const double loss = strength - std::sqrt(strength * strength - danger * danger);
	return loss * params.lossFactor * params.goldPerArmyValue * params.lossAversion * std::sqrt(economy.armyPremium);
}

void AdventureAI::updateThreats()
{
	threats.clear();
	for(const auto & [id, object] : perception.knownObjects())
	{
		if(object.type != Obj::HERO || !object.owner.isValidPlayer() || object.owner == me || !object.army)
			continue;
		if(cb->getPlayerRelations(me, object.owner) != PlayerRelations::ENEMIES)
			continue;
		const int age = today - object.lastSeenDay;
		if(age > 3)
			continue;
		Threat t;
		t.pos = object.visitablePos;
		t.strength = observedDanger(object) * params.enemyHeroFactor;
		t.radius = params.threatRadius * (1 + 0.5 * age);
		t.age = age;
		t.hero = id;
		threats.push_back(t);
	}
}

double AdventureAI::threatAt(const int3 & tile, ObjectInstanceID ignoreHero, bool cautious) const
{
	double worst = 0;
	for(const auto & t : threats)
	{
		if(t.hero == ignoreHero || t.pos.z != tile.z)
			continue;
		if(!cautious && t.age > 2)
			continue;
		double radius = cautious ? params.threatRadius + 8.0 * t.age : t.radius;
		if(t.pos.dist2d(tile) <= radius)
			worst = std::max(worst, t.strength);
	}
	return worst;
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
		if(today - object.lastSeenDay > days || object.visitablePos.z != tile.z)
			continue;
		if(cb->getPlayerRelations(me, object.owner) == PlayerRelations::ENEMIES && object.visitablePos.dist2d(tile) <= radius)
			return true;
	}
	return false;
}

double AdventureAI::defenceAt(const CGHeroInstance * hero, const int3 & tile) const
{
	double strength = static_cast<double>(hero->getTotalStrength());
	for(const auto * town : perception.ownTowns())
	{
		if(town->visitablePos() != tile)
			continue;
		double garrison = 0;
		for(const auto & slot : town->Slots())
			garrison += stackValue(slot.second.get());
		double walls = town->fortLevel() >= CGTownInstance::CITADEL ? 1.5 : (town->fortLevel() >= CGTownInstance::FORT ? 1.3 : 1.0);
		strength = (strength + garrison) * walls;
	}
	return strength;
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

double AdventureAI::objectValue(const CGHeroInstance * hero, const ObservedObject & object, const CGObjectInstance * instance) const
{
	const bool main = isMain(hero);
	const bool own = object.owner == me;
	const bool enemy = object.owner.isValidPlayer() && !own;
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
		return amount * resourceValue(res) * (res == GameResID::GOLD ? goldFactor : 1.0);
	}
	case Obj::CAMPFIRE:
		return 500 + 5 * resourceValue(GameResID::MERCURY);
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
		return 600;
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
		if(visitedByHero() || !main || cb->getResourceAmount(GameResID::GOLD) < 3000)
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
	case Obj::TOWN:
	{
		if(own)
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(instance);
			if(!town || !main)
				return 0;
			double troops = 0;
			for(const auto & slot : town->Slots())
				troops += stackValue(slot.second.get());
			troops += recruitableValue(town, cb->getResourceAmount(GameResID::GOLD));
			double value = troops * params.goldPerArmyValue * params.reinforceFactor * armyFactor;
			if(!hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1))
				value += 300;
			// enemies seen near the town: the main hero should stand guard
			if(enemySeenNear(town->visitablePos(), 3, 20))
				value += 2500;
			return value > 300 ? value : 0;
		}
		// without a town the game is lost after seven days: any town is worth everything
		if(perception.ownTowns().empty())
			return 100000;
		return enemy ? 20000 : 12000;
	}
	case Obj::HERO:
	{
		if(own)
		{
			const CGHeroInstance * other = ownHero(object.id);
			if(!other || other == hero)
				return 0;
			// meeting the main hero hands over troops
			if(main)
				return transferableValue(other) * params.goldPerArmyValue * params.ferryFactor * armyFactor;
			if(other->id == mainHero)
				return transferableValue(hero) * params.goldPerArmyValue * params.ferryFactor * armyFactor;
			return 0;
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

	auto reachable = [&](const CPathsInfo & p, const int3 & tile) -> const CGPathNode *
	{
		const CGPathNode * node = p.getPathInfo(tile);
		if(!node || !node->reachable() || node->turns > params.maxTravelTurns)
			return nullptr;
		return node;
	};

	// first guard (closest to the hero) on the guard-free path to a tile
	std::map<const CGPathNode *, ObjectInstanceID> guardMemo;
	std::function<ObjectInstanceID(const CGPathNode *)> firstGuard = [&](const CGPathNode * node) -> ObjectInstanceID
	{
		if(!node)
			return ObjectInstanceID();
		auto memo = guardMemo.find(node);
		if(memo != guardMemo.end())
			return memo->second;
		ObjectInstanceID result = node->theNodeBefore ? firstGuard(node->theNodeBefore) : ObjectInstanceID();
		if(!result.hasValue() && node->coord != heroPos && cb->isVisible(node->coord))
		{
			auto guards = cb->getGuardingCreatures(node->coord);
			if(!guards.empty())
				result = guards.front()->id;
		}
		guardMemo[node] = result;
		return result;
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

	const double ownStrength = static_cast<double>(hero->getTotalStrength());
	const double heroWorth = 2000 + ownStrength * params.goldPerArmyValue;

	// where the hero ends the day when heading for `node`
	auto stopTile = [](const CGPathNode * node) -> int3
	{
		while(node && node->turns > 0 && node->theNodeBefore)
			node = node->theNodeBefore;
		return node ? node->coord : int3(-1);
	};
	const bool cautious = !isMain(hero);
	auto unsafe = [&](const CGPathNode * node, ObjectInstanceID object) -> bool
	{
		int3 tile = node->coord;
		int3 stop = stopTile(node);
		// collectors are weak: they work close to home
		if(cautious && (!inTerritory(tile) || (stop.isValid() && !inTerritory(stop))))
			return true;
		if(threats.empty())
			return false;
		// attacking the threat itself is judged by the battle estimate
		if(threatAt(tile, object, cautious) > params.threatTolerance * defenceAt(hero, tile))
			return true;
		return stop.isValid() && stop != tile && threatAt(stop, object, cautious) > params.threatTolerance * defenceAt(hero, stop);
	};

	auto addTarget = [&](const int3 & tile, ObjectInstanceID object, double value, double loss, double travel, const std::string & description)
	{
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

	// ---- objects other than monsters
	for(const auto & [id, object] : known)
	{
		if(object.type == Obj::MONSTER || object.visitablePos == heroPos)
			continue;
		if(failedToday.count({heroNum, object.visitablePos}))
			continue;
		const CGObjectInstance * instance = perception.visibleObject(id);
		double value = objectValue(hero, object, instance);
		if(value <= 0 || reservedByOther(id))
			continue;

		if(const CGPathNode * node = reachable(paths, object.visitablePos))
		{
			double danger = guardDanger(object.visitablePos);
			if((object.type == Obj::HERO || object.type == Obj::TOWN) && object.owner != me)
			{
				// everything defending this tile: heroes (with their skills) and a town behind walls
				double defenders = 0;
				bool town = false;
				for(const auto & [otherId, other] : known)
				{
					if(other.visitablePos != object.visitablePos || other.owner == me)
						continue;
					if(other.type == Obj::HERO)
						defenders += observedDanger(other) * params.enemyHeroFactor;
					else if(other.type == Obj::TOWN)
					{
						defenders += observedDanger(other);
						town = true;
					}
				}
				danger += town ? defenders * params.wallsFactor : defenders;
			}
			const bool desperate = object.type == Obj::TOWN && perception.ownTowns().empty();
			double loss = expectedLoss(hero, danger, desperate);
			if(loss < 0 || (!desperate && unsafe(node, object.type == Obj::HERO ? id : ObjectInstanceID())))
				continue;
			// a threatened own town needs its defender
			if(object.type == Obj::TOWN && object.owner == me && isMain(hero))
			{
				double garrison = 0;
				if(const auto * town = dynamic_cast<const CGTownInstance *>(instance))
					for(const auto & slot : town->Slots())
						garrison += stackValue(slot.second.get());
				double threat = threatAt(object.visitablePos);
				if(threat > garrison)
					value += params.townDefenceValue * std::min(1.0, (threat - garrison) / std::max(1.0, threat));
			}
			addTarget(object.visitablePos, id, value, loss, node->cost, "object " + std::to_string(object.type.getNum()));
		}
		else if(const CGPathNode * free = reachable(freePaths, object.visitablePos))
		{
			ObjectInstanceID guard = firstGuard(free);
			if(guard.hasValue())
				unlockValue[guard] += value / (1.0 + free->cost);
		}
	}

	// ---- exploration
	{
		RevealGrid grid(*cb, perception.staleTiles(), params.staleTileWeight);
		const int radius = hero->getSightRadius();
		// exploring stays important until we know where the enemy lives; a strong main hero hunts
		bool enemyTownKnown = false;
		for(const auto & [id, object] : known)
		{
			if(object.type == Obj::TOWN && object.owner.isValidPlayer() && object.owner != me
				&& cb->getPlayerRelations(me, object.owner) == PlayerRelations::ENEMIES)
				enemyTownKnown = true;
		}
		double decay = enemyTownKnown ? std::max(0.25, 1.0 - today / 56.0) : std::max(0.6, 1.0 - today / 56.0);
		if(!enemyTownKnown && isMain(hero) && today > 14)
			decay *= 2.0;
		const double tileValue = params.explorationTileValue * decay;
		const int3 size = cb->getMapSize();
		Target bestExplore;
		bestExplore.score = 0;
		std::map<ObjectInstanceID, double> behindGuard;
		for(int z = 0; z < size.z; ++z)
		{
			for(int x = 0; x < size.x; ++x)
			{
				for(int y = 0; y < size.y; ++y)
				{
					int3 tile(x, y, z);
					if(tile == heroPos || failedToday.count({heroNum, tile}) || trail[hero->id].count(tile))
						continue;
					const CGPathNode * node = reachable(paths, tile);
					const bool normal = node && node->action == EPathNodeAction::NORMAL;
					const CGPathNode * free = normal ? nullptr : reachable(freePaths, tile);
					if(!normal && !(free && free->action == EPathNodeAction::NORMAL))
						continue;
					double reveal = grid.count(tile, radius);
					if(reveal < 4)
						continue;
					double value = reveal * tileValue;
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
						ObjectInstanceID guard = firstGuard(free);
						if(guard.hasValue())
							behindGuard[guard] = std::max(behindGuard[guard], value / (1.0 + free->cost));
					}
				}
			}
		}
		if(bestExplore.score > 0)
			addTarget(bestExplore.tile, ObjectInstanceID(), bestExplore.value, 0, bestExplore.travel, "explore");
		for(const auto & [guard, value] : behindGuard)
			unlockValue[guard] += value;
	}

	// ---- monsters: experience plus whatever they block
	double weakestRejected = 0;
	for(const auto & [id, object] : known)
	{
		if(object.type != Obj::MONSTER || !object.army || failedToday.count({heroNum, object.visitablePos}))
			continue;
		const CGPathNode * node = reachable(paths, object.visitablePos);
		if(!node)
			continue;
		double danger = observedDanger(object);
		double loss = expectedLoss(hero, danger);
		if(loss < 0 && (weakestRejected == 0 || danger < weakestRejected))
			weakestRejected = danger;
		if(loss < 0 || unsafe(node, ObjectInstanceID()))
			continue;
		double hp = 0;
		for(const auto & unit : object.army->units)
		{
			if(unit.creature.hasValue())
				hp += unit.creature.toCreature()->getMaxHealth() * 0.5 * (unit.minCount + unit.maxCount);
		}
		double value = hp * expValue(hero);
		auto unlock = unlockValue.find(id);
		if(unlock != unlockValue.end())
			value += params.unlockFactor * unlock->second;
		if(value <= loss)
			continue;
		addTarget(object.visitablePos, id, value, loss, node->cost, "monster");
	}

	if(out.empty() && weakestRejected > 0)
		logAi->debug("FairAI %s: %s (strength %.0f) has no target; weakest guard in reach %.0f", me.toString(),
			hero->getNameTranslated(), ownStrength, weakestRejected);

	// ---- standing where a stronger enemy hero can reach us: get away (own towns preferred)
	if(threatAt(heroPos, ObjectInstanceID(), cautious) > params.threatTolerance * defenceAt(hero, heroPos))
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
					double margin = defenceAt(hero, tile) * params.threatTolerance - threatAt(tile, ObjectInstanceID(), cautious);
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
					if(t.pos.z == tile.z && (!cautious ? t.age <= 2 : true) && t.strength > params.threatTolerance * ownStrength)
						nearest = std::min(nearest, t.pos.dist2d(tile));
				}
				return nearest;
			};
			const double here = threatDistance(heroPos);
			Target flee;
			double bestDistance = here + 4;
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
			if(bestDistance > here + 4)
				addTarget(flee.tile, ObjectInstanceID(), heroWorth * params.retreatValue * 0.5, 0, flee.travel, "flee");
		}
	}

	return out;
}

// ======================================================================= queries

int AdventureAI::chooseSkill(const CGHeroInstance * hero, const std::vector<SecondarySkill> & skills) const
{
	if(skills.size() < 2)
		return 0;
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

void AdventureAI::onHeroExchange(ObjectInstanceID first, ObjectInstanceID second)
{
	const CGHeroInstance * a = ownHero(first);
	const CGHeroInstance * b = ownHero(second);
	if(!a || !b)
		return;
	if(isMain(b) || (!isMain(a) && b->getTotalStrength() > a->getTotalStrength()))
		std::swap(a, b);
	pickBestCreatures(a, b);
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
		ResourceSet available = cb->getResourceAmount();
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
	for(int guard = 0; guard < 24; ++guard)
	{
		gateway.checkInterruption();
		bool acted = false;

		// 1. join stacks of the same type
		for(const auto & [slot, stack] : source->Slots())
		{
			if(!stack || !stack->getCreature())
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
		ResourceSet available = cb->getResourceAmount();
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

void AdventureAI::manageTownVisit(const CGHeroInstance * hero, const CGTownInstance * town)
{
	if(!hero || !town || town->tempOwner != me)
		return;
	const bool main = isMain(hero);
	if(main)
	{
		recruitInTown(town, hero, 0);
		pickBestCreatures(hero, town);
		if(!hero->hasSpellbook() && town->hasBuilt(BuildingID::MAGES_GUILD_1)
			&& cb->getResourceAmount(GameResID::GOLD) >= GameConstants::SPELLBOOK_GOLD_COST + 500)
		{
			cb->buyArtifact(hero, ArtifactID::SPELLBOOK);
		}
		upgradeArmy(hero);
	}
	else if(ownHero(mainHero))
	{
		// collectors ferry the garrison to the main hero
		pickBestCreatures(hero, town);
	}
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
	const double stock = cb->getResourceAmount(GameResID::GOLD);
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
			ResourceSet have = cb->getResourceAmount();
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
	value += std::max(0.0, worth(produce)) * params.incomeHorizonDays * goldWeight;

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
		value += 700;
	else if(id == BuildingID::MAGES_GUILD_2 || id == BuildingID::MAGES_GUILD_3)
		value += 400;
	else if(id == BuildingID::MARKETPLACE)
		value += 800;
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
	else if(id == BuildingID::SHIPYARD || id == BuildingID::SHIP)
		value += 0;
	else if(value <= 0)
		value += 100;

	return value;
}

void AdventureAI::buildInTown(const CGTownInstance * town)
{
	const CTown * info = town->getTown();
	const ResourceSet have = cb->getResourceAmount();
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

	int attempts = 0;
	for(const auto & option : ranked)
	{
		if(option.score < 0.25 || attempts >= 4)
			return;
		if(option.state == EBuildingState::NO_RESOURCES && !buyResourcesFor(town, info->buildings.at(option.build)->resources))
			continue;
		++attempts;
		logAi->info("FairAI %s day %d: building %s in %s (for %s, score %.2f)", me.toString(), today,
			info->buildings.at(option.build)->getNameTranslated(), town->getNameTranslated(), option.reason, option.score);
		cb->buildBuilding(town, option.build);
		if(town->hasBuilt(option.build))
			return;
		logAi->warn("FairAI %s: building %s was rejected", me.toString(), info->buildings.at(option.build)->getNameTranslated());
	}
}

bool AdventureAI::buyResourcesFor(const CGTownInstance * town, const ResourceSet & cost)
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

	ResourceSet have = cb->getResourceAmount();
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
	const int64_t reserve = 1000;
	if(purchases.empty() || goldNeeded + reserve > have[GameResID::GOLD])
		return false;
	for(const auto & [res, gold] : purchases)
	{
		logAi->info("FairAI %s day %d: buying %s for %d gold", me.toString(), today, res.toResource()->getJsonKey(), gold);
		cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, GameResID(GameResID::GOLD), res, gold);
	}
	return cb->getResourceAmount().canAfford(cost);
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
	int wanted = today <= 7 ? 2 : (today <= 21 ? 3 : 4);
	if(economy.rho > 2 && cb->getResourceAmount(GameResID::GOLD) > 8000)
		wanted += 2;
	// every lost hero feeds the enemy experience: after a loss only replace the minimum
	if(today - lastHeroLossDay < 7)
		wanted = std::min(wanted, 2);
	wanted = std::min(wanted, params.maxHeroes);
	const int64_t reserve = today <= 2 ? 0 : params.heroGoldReserve;

	for(const auto * town : perception.ownTowns())
	{
		if(static_cast<int>(perception.ownHeroes().size()) >= wanted)
			return;
		if(cb->getResourceAmount(GameResID::GOLD) < GameConstants::HERO_GOLD_COST + reserve)
			return;
		if(!town->hasBuilt(BuildingID::TAVERN))
			continue;

		const CGHeroInstance * best = nullptr;
		for(const auto * candidate : cb->getAvailableHeroes(town))
		{
			if(!best || candidate->getTotalStrength() > best->getTotalStrength())
				best = candidate;
		}
		if(!best)
			continue;

		// our hero stands in the gate: park him in the garrison while hiring, then swap back
		const CGHeroInstance * visitor = town->getVisitingHero();
		bool parked = false;
		if(visitor)
		{
			if(visitor->tempOwner != me || town->getGarrisonHero())
				continue;
			cb->swapGarrisonHero(town);
			gateway.waitTillFree();
			if(town->getVisitingHero())
				continue; // swap refused
			parked = true;
		}

		logAi->info("FairAI %s day %d: hiring %s in %s", me.toString(), today, best->getNameTranslated(), town->getNameTranslated());
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

void AdventureAI::defendTowns()
{
	auto known = perception.knownObjects();
	for(const auto * town : perception.ownTowns())
	{
		bool threatened = false;
		for(const auto & [id, object] : known)
		{
			if(object.type == Obj::HERO && object.visibleNow && object.owner.isValidPlayer() && object.owner != me
				&& object.visitablePos.z == town->visitablePos().z && object.visitablePos.dist2d(town->visitablePos()) < 14)
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
