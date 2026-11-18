/*
 * ArenaHarness.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ArenaHarness.h"

#include "Client.h"
#include "CServerHandler.h"

#include "../lib/CConfigHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/ExtraOptionsInfo.h"
#include "../lib/GameLibrary.h"
#include "../lib/StartInfo.h"
#include "../lib/callback/EditorCallback.h"
#include "../lib/constants/StringConstants.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/gameState/EVictoryLossCheckResult.h"
#include "../lib/gameState/GameStatistics.h"
#include "../lib/json/JsonNode.h"
#include "../lib/mapping/CMap.h"
#include "../lib/mapping/CMapHeader.h"
#include "../lib/mapping/CMapService.h"
#include "../lib/rmg/CMapGenOptions.h"
#include "../lib/rmg/CMapGenerator.h"
#include "../lib/rmg/CRmgTemplate.h"
#include "../lib/rmg/CRmgTemplateStorage.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/bonuses/BonusEnum.h"
#include "../lib/bonuses/BonusSelector.h"
#include "../lib/bonuses/BonusList.h"
#include "../lib/bonuses/Bonus.h"
#include "../lib/CBonusTypeHandler.h"
#include "../lib/CRandomGenerator.h"
#include "../lib/CSkillHandler.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/entities/hero/CHero.h"
#include "../lib/entities/hero/CHeroClass.h"
#include "../lib/entities/hero/CHeroHandler.h"
#include "../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGCreature.h"
#include "../lib/mapObjects/army/CStackInstance.h"
#include "../lib/mapping/CMapEditManager.h"
#include "../lib/modding/IdentifierStorage.h"
#include "../lib/modding/ModScope.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/mapping/MapFormat.h"
#include <vcmi/Faction.h>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace Arena
{

namespace
{
std::mutex arenaMutex;
std::map<PlayerColor, std::string> installedAI;
std::vector<JsonNode> endEvents;
std::vector<JsonNode> timeline; ///< one entry per game day: key figures of every player
struct BattleTally
{
	int guardFights = 0;      ///< battles against neutral armies (map guards, banks)
	int guardFightsLost = 0;
	int64_t guardLosses = 0;  ///< own army value lost in them
	int64_t guardKills = 0;   ///< neutral army value destroyed
	int playerFights = 0;     ///< battles against other players
	int playerFightsWon = 0;
	int64_t playerLosses = 0;
	int64_t playerKills = 0;
};
std::map<PlayerColor, BattleTally> battleTally;
/// every battle between players: [day, attacker, defender, attacker's army value, defender's, attacker's loss,
/// defender's loss, winner (0 attacker, 1 defender), fought at a town (0/1), defender led by a hero (0/1),
/// attacker's hero level, defender's hero level, how it ended (0 normal, 1 escape, 2 surrender)]
std::vector<JsonNode> playerBattles;
bool finished = false;
const auto startTime = std::chrono::steady_clock::now();

// watchdog: detects matches that stopped making progress (e.g. an AI deadlock)
std::atomic<int64_t> lastActivityMs{0};
/// ... and matches in which things keep happening but no day passes (an AI repeating one order for
/// ever, a battle without end): such a game ran into the one-hour limit while five slots waited for it
std::atomic<int64_t> lastDayMs{0};
std::atomic<int> lastTurnPlayer{-1};
std::atomic<bool> watchdogStarted{false};

int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
}

const JsonNode & arenaSettings()
{
	return settings["session"]["arena"];
}

JsonNode playerStats(const CGameState & gs, const PlayerState & ps)
{
	StatisticDataSetEntry e = StatisticDataSet::createEntry(&ps, &gs, StatisticDataSet());

	JsonNode node;
	node["ai"].String() = installedAI.count(ps.color) ? installedAI.at(ps.color) : std::string("?");
	node["status"].String() = ps.status == EPlayerStatus::WINNER ? "winner" : (ps.status == EPlayerStatus::LOSER ? "loser" : "ingame");
	node["team"].Integer() = ps.team.getNum();
	{
		// the faction chosen (or rolled) for this seat at game start
		const auto & infos = gs.getStartInfo()->playerInfos;
		auto info = infos.find(ps.color);
		const bool known = info != infos.end() && info->second.castle.hasValue() && info->second.castle.getNum() >= 0;
		node["faction"].String() = known ? info->second.castle.toEntity(LIBRARY)->getJsonKey() : std::string("?");
	}
	node["cheated"].Bool() = ps.cheated;
	node["towns"].Integer() = e.numberTowns;
	node["heroes"].Integer() = e.numberHeroes;
	node["armyStrength"].Integer() = e.armyStrength;
	node["totalExperience"].Integer() = e.totalExperience;
	node["maxHeroLevel"].Integer() = e.maxHeroLevel;
	node["income"].Integer() = e.income;
	node["mapExploredRatio"].Float() = e.mapExploredRatio;
	node["townBuiltRatio"].Float() = e.townBuiltRatio;
	node["numberArtifacts"].Integer() = e.numberArtifacts;
	node["numberDwellings"].Integer() = e.numberDwellings;
	node["score"].Integer() = e.score;
	node["gold"].Integer() = ps.resources[EGameResID::GOLD];

	int mines = 0;
	for(const auto & m : e.numMines)
		mines += m.second;
	node["mines"].Integer() = mines;
	return node;
}

[[noreturn]] void finishMatch(CClient & client, const std::string & outcome, const std::optional<PlayerColor> & winner, const JsonNode & extra = JsonNode())
{
	const CGameState & gs = client.gameState();

	JsonNode result = extra;
	if(lastTurnPlayer.load() >= 0)
	{
		PlayerColor turnOf(lastTurnPlayer.load());
		result["lastTurnOf"].String() = turnOf.toString();
		result["lastTurnOfAI"].String() = installedAI.count(turnOf) ? installedAI.at(turnOf) : std::string("?");
	}
	result["outcome"].String() = outcome; // "victory", "timeout"
	result["winner"].String() = winner ? winner->toString() : std::string();
	result["day"].Integer() = gs.getDate(Date::DAY);
	result["map"].String() = gs.getStartInfo()->mapname;
	result["seed"].Integer() = arenaSettings()["seed"].Integer();
	result["difficulty"].String() = GameConstants::DIFFICULTY_NAMES[gs.getStartInfo()->difficulty];
	result["cheatsAllowed"].Bool() = gs.getStartInfo()->extraOptionsInfo.cheatsAllowed;
	result["wallTimeSec"].Float() = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

	for(const auto & player : gs.players)
	{
		if(player.first.isValidPlayer())
			result["players"][player.first.toString()] = playerStats(gs, player.second);
	}
	for(const auto & ev : endEvents)
		result["endEvents"].Vector().push_back(ev);
	for(const auto & day : timeline)
		result["timeline"].Vector().push_back(day);
	for(const auto & fight : playerBattles)
		result["playerBattles"].Vector().push_back(fight);
	for(const auto & [color, tally] : battleTally)
	{
		JsonNode & node = result["battles"][color.toString()];
		node["guardFights"].Integer() = tally.guardFights;
		node["guardFightsLost"].Integer() = tally.guardFightsLost;
		node["guardLosses"].Integer() = tally.guardLosses;
		node["guardKills"].Integer() = tally.guardKills;
		node["playerFights"].Integer() = tally.playerFights;
		node["playerFightsWon"].Integer() = tally.playerFightsWon;
		node["playerLosses"].Integer() = tally.playerLosses;
		node["playerKills"].Integer() = tally.playerKills;
	}

	const std::string path = arenaSettings()["resultFile"].String();
	{
		std::ofstream out(path, std::ios::trunc);
		out << result.toString();
	}

	logGlobal->info("ARENA: match finished (%s), result written to %s", outcome, path);
	std::cout << "ARENA_RESULT " << result.toCompactString() << std::endl;
	std::fflush(nullptr);
	// The in-process server and all AI threads die with us - this is intended for test runs
	std::_Exit(0);
}
}

bool isActive()
{
	return !arenaSettings()["resultFile"].isNull() && !arenaSettings()["resultFile"].String().empty();
}

void storeOptions(const po::variables_map & vm)
{
	Settings arena = settings.write["session"]["arena"];

	if(vm.count("arena-result"))
		arena["resultFile"].String() = vm["arena-result"].as<std::string>();
	if(vm.count("arena-max-days"))
		arena["maxDays"].Integer() = vm["arena-max-days"].as<int>();
	if(vm.count("arena-seed"))
		arena["seed"].Integer() = vm["arena-seed"].as<int>();
	if(vm.count("arena-difficulty"))
		arena["difficulty"].Integer() = vm["arena-difficulty"].as<int>();
	if(vm.count("arena-stall-seconds"))
		arena["stallSeconds"].Integer() = vm["arena-stall-seconds"].as<int>();
	arena["cheatsAllowed"].Bool() = vm.count("arena-allow-cheats") > 0;

	if(vm.count("arena-fairai-params"))
	{
		JsonNode list;
		for(const auto & text : vm["arena-fairai-params"].as<std::vector<std::string>>())
		{
			try
			{
				list.Vector().push_back(JsonNode(reinterpret_cast<const std::byte *>(text.data()), text.size(), "arena-fairai-params"));
			}
			catch(const std::exception & e)
			{
				logGlobal->error("ARENA: cannot parse FairAI parameters '%s': %s", text, e.what());
				list.Vector().emplace_back();
			}
		}
		arena["fairaiParams"] = list;
	}

	if(vm.count("arena-battle-ai"))
	{
		JsonNode list;
		for(const auto & name : vm["arena-battle-ai"].as<std::vector<std::string>>())
			list.Vector().emplace_back(name);
		arena["battleAI"] = list;
	}

	if(vm.count("ai"))
	{
		JsonNode list;
		for(const auto & name : vm["ai"].as<std::vector<std::string>>())
			list.Vector().emplace_back(name);
		arena["ai"] = list;
	}
}

std::string aiForPlayer(const PlayerColor & color)
{
	const JsonNode & list = arenaSettings()["ai"];
	if(!color.isValidPlayer() || list.isNull())
		return {};
	const auto & vec = list.Vector();
	if(color.getNum() < static_cast<int>(vec.size()))
		return vec[color.getNum()].String();
	return {};
}

void noteInstalledAI(const PlayerColor & color, const std::string & aiName)
{
	std::lock_guard lock(arenaMutex);
	installedAI[color] = aiName;
}

void configureLobby(CServerHandler & handler)
{
	if(!isActive())
		return;

	const JsonNode & arena = arenaSettings();
	if(!arena["difficulty"].isNull())
		handler.setDifficulty(static_cast<int>(arena["difficulty"].Integer()));

	ExtraOptionsInfo options = handler.si->extraOptionsInfo;
	options.cheatsAllowed = arena["cheatsAllowed"].Bool();
	handler.setExtraOptionsInfo(options);
}

void onPackApplied(CClient & client)
{
	if(!isActive())
		return;

	lastActivityMs.store(nowMs());

	if(watchdogStarted.exchange(true))
		return;

	const int stallSeconds = arenaSettings()["stallSeconds"].isNull() ? 120 : static_cast<int>(arenaSettings()["stallSeconds"].Integer());
	CClient * clientPtr = &client;
	std::thread([clientPtr, stallSeconds]()
	{
		while(true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			// (no day for twice the stall limit, ten minutes at least: a day of a long game takes seconds)
			const int64_t dayLimitMs = std::max(600, 2 * stallSeconds) * 1000LL;
			const bool noDay = lastDayMs.load() > 0 && nowMs() - lastDayMs.load() >= dayLimitMs;
			if(nowMs() - lastActivityMs.load() < stallSeconds * 1000LL && !noDay)
				continue;

			// Nothing happened for too long. Try to read the game state for statistics;
			// if the state is locked by a stuck thread, report without waiting forever.
			std::shared_lock gsLock(CGameState::mutex, std::defer_lock);
			for(int i = 0; i < 50 && !gsLock.try_lock(); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

			std::lock_guard lock(arenaMutex);
			if(finished)
				return;
			finished = true;
			finishMatch(*clientPtr, "stalled", std::nullopt);
		}
	}).detach();
}

void onPlayerStartsTurn(CClient & client, const PlayerColor & player)
{
	if(!isActive())
		return;
	lastTurnPlayer.store(player.getNum());
}

void onNewTurn(CClient & client)
{
	if(!isActive())
		return;

	lastDayMs.store(nowMs());

	const int maxDays = static_cast<int>(arenaSettings()["maxDays"].Integer());
	if(maxDays <= 0)
		return;

	std::lock_guard lock(arenaMutex);
	if(finished)
		return;

	const CGameState & gs = client.gameState();
	if(!gs.getMap().battleOnly)
	{
		// key figures per day, compact: [army, towns, heroes, income, mines, explored %, best hero level, gold, dwellings, guard fights, guard losses,
		// army of the strongest hero, army standing in town garrisons (without a hero)]
		JsonNode day;
		day["day"].Integer() = gs.getDate(Date::DAY);
		for(const auto & player : gs.players)
		{
			if(!player.first.isValidPlayer() || player.second.status != EPlayerStatus::INGAME)
				continue;
			StatisticDataSetEntry e = StatisticDataSet::createEntry(&player.second, &gs, StatisticDataSet());
			int mines = 0;
			for(const auto & m : e.numMines)
				mines += m.second;
			const BattleTally & tally = battleTally[player.first];
			// how the army is spread: the strongest single hero, and what stands in towns without a hero
			int64_t mainArmy = 0;
			int64_t garrisons = 0;
			for(const auto * hero : player.second.getHeroes())
				mainArmy = std::max<int64_t>(mainArmy, static_cast<int64_t>(hero->getArmyStrength()));
			for(const auto * town : player.second.getTowns())
				if(!town->getGarrisonHero())
					garrisons += static_cast<int64_t>(town->getArmyStrength());
			JsonNode & row = day["players"][player.first.toString()];
			for(int64_t value : {static_cast<int64_t>(e.armyStrength), static_cast<int64_t>(e.numberTowns), static_cast<int64_t>(e.numberHeroes), static_cast<int64_t>(e.income),
				static_cast<int64_t>(mines), static_cast<int64_t>(std::lround(e.mapExploredRatio * 100)), static_cast<int64_t>(e.maxHeroLevel),
				static_cast<int64_t>(player.second.resources[EGameResID::GOLD]), static_cast<int64_t>(e.numberDwellings),
				static_cast<int64_t>(tally.guardFights), tally.guardLosses, mainArmy, garrisons})
			{
				row.Vector().emplace_back(value);
			}
		}
		timeline.push_back(day);
	}

	if(gs.getDate(Date::DAY) > maxDays)
	{
		finished = true;
		finishMatch(client, "timeout", std::nullopt);
	}
}

void onPlayerEndsGame(CClient & client, const PlayerColor & player, const EVictoryLossCheckResult & result)
{
	if(!isActive())
		return;

	std::lock_guard lock(arenaMutex);
	if(finished)
		return;

	JsonNode ev;
	ev["player"].String() = player.toString();
	ev["victory"].Bool() = result.victory();
	ev["day"].Integer() = client.gameState().getDate(Date::DAY);
	endEvents.push_back(ev);

	const CGameState & gs = client.gameState();

	if(result.victory())
	{
		finished = true;
		finishMatch(client, "victory", player);
	}

	// A loss decides the match once at most one player is still in the game
	std::vector<PlayerColor> stillPlaying;
	for(const auto & p : gs.players)
	{
		if(p.first.isValidPlayer() && p.first != player && p.second.status == EPlayerStatus::INGAME)
			stillPlaying.push_back(p.first);
	}

	if(stillPlaying.size() <= 1)
	{
		finished = true;
		finishMatch(client, "victory", stillPlaying.empty() ? std::nullopt : std::optional<PlayerColor>(stillPlaying.front()));
	}
}

int generateMap(const po::variables_map & vm)
{
	const std::string outFile = vm["arena-genmap"].as<std::string>();
	const std::string templateName = vm.count("arena-template") ? vm["arena-template"].as<std::string>() : std::string("2SM4d");
	const std::string size = vm.count("arena-size") ? vm["arena-size"].as<std::string>() : std::string("s");
	const int levels = vm.count("arena-levels") ? vm["arena-levels"].as<int>() : 1;
	const int seed = vm.count("arena-seed") ? vm["arena-seed"].as<int>() : 1;
	const std::string water = vm.count("arena-water") ? vm["arena-water"].as<std::string>() : std::string("none");
	const std::string monsters = vm.count("arena-monsters") ? vm["arena-monsters"].as<std::string>() : std::string("normal");
	const int players = vm.count("arena-players") ? vm["arena-players"].as<int>() : 2;

	static const std::map<std::string, int> sizes = {
		{"s", CMapHeader::MAP_SIZE_SMALL}, {"m", CMapHeader::MAP_SIZE_MIDDLE}, {"l", CMapHeader::MAP_SIZE_LARGE}, {"xl", CMapHeader::MAP_SIZE_XLARGE},
		{"h", CMapHeader::MAP_SIZE_HUGE}, {"xh", CMapHeader::MAP_SIZE_XHUGE}, {"g", CMapHeader::MAP_SIZE_GIANT}
	};
	// optional per-player lists (red, blue, ...): "0,0,1,1" teams, "castle,hota.cove:cove" starting towns
	auto splitList = [](const std::string & text)
	{
		std::vector<std::string> parts;
		if(!text.empty())
			boost::split(parts, text, boost::is_any_of(","));
		return parts;
	};
	const auto teams = splitList(vm.count("arena-teams") ? vm["arena-teams"].as<std::string>() : std::string());
	const auto towns = splitList(vm.count("arena-towns") ? vm["arena-towns"].as<std::string>() : std::string());
	static const std::map<std::string, EWaterContent::EWaterContent> waters = {
		{"random", EWaterContent::RANDOM}, {"none", EWaterContent::NONE}, {"normal", EWaterContent::NORMAL}, {"islands", EWaterContent::ISLANDS}
	};
	static const std::map<std::string, EMonsterStrength::EMonsterStrength> strengths = {
		{"random", EMonsterStrength::RANDOM}, {"weak", EMonsterStrength::GLOBAL_WEAK}, {"normal", EMonsterStrength::GLOBAL_NORMAL}, {"strong", EMonsterStrength::GLOBAL_STRONG}
	};

	try
	{
		CMapGenOptions options;
		options.setWidth(sizes.at(size));
		options.setHeight(sizes.at(size));
		options.setLevels(levels);
		// templates are stored as "<mod>:<name>" - accept the plain display name as well
		const CRmgTemplate * chosen = nullptr;
		for(const CRmgTemplate * tmpl : LIBRARY->tplh->getTemplates())
		{
			if(boost::iequals(tmpl->getId(), templateName) || boost::iequals(tmpl->getName(), templateName))
			{
				chosen = tmpl;
				break;
			}
		}
		if(!chosen)
			throw std::runtime_error("unknown RMG template: " + templateName);
		options.setMapTemplate(chosen);
		options.setHumanOrCpuPlayerCount(players);
		options.setCompOnlyPlayerCount(0);
		options.setWaterContent(waters.at(water));
		options.setMonsterStrength(strengths.at(monsters));
		for(int i = 0; i < players; ++i)
			options.setPlayerTypeForStandardPlayer(PlayerColor(i), EPlayerType::HUMAN); // human or AI may take the seat
		for(int i = 0; i < players && i < static_cast<int>(teams.size()); ++i)
			if(!teams[i].empty())
				options.setPlayerTeam(PlayerColor(i), TeamID(std::stoi(teams[i])));
		for(int i = 0; i < players && i < static_cast<int>(towns.size()); ++i)
		{
			if(towns[i].empty() || towns[i] == "random")
				continue;
			std::string scope = ModScope::scopeGame();
			std::string id = towns[i];
			if(auto colon = id.find(':'); colon != std::string::npos)
			{
				scope = id.substr(0, colon);
				id = id.substr(colon + 1);
			}
			auto faction = LIBRARY->identifiers()->getIdentifier(scope, "faction", id, true);
			if(!faction)
				throw std::runtime_error("unknown faction: " + towns[i]);
			options.setStartingTownForPlayer(PlayerColor(i), FactionID(*faction));
		}

		EditorCallback callback(nullptr);
		CMapGenerator generator(options, &callback, seed);
		std::unique_ptr<CMap> map = generator.generate();
		callback.setMap(map.get());

		map->name.appendRawString(boost::str(boost::format("Arena %s %s seed %d") % templateName % size % seed));

		CMapService mapService;
		mapService.saveMap(map, outFile);
		std::cout << "ARENA_MAP " << outFile << std::endl;
		return 0;
	}
	catch(const std::exception & e)
	{
		std::cerr << "ARENA_MAP_FAILED " << e.what() << std::endl;
		return 1;
	}
}


namespace
{
std::optional<si32> resolveId(const std::string & type, const std::string & name)
{
	std::string scope = ModScope::scopeGame();
	std::string id = name;
	auto colon = name.find(':');
	if(colon != std::string::npos)
	{
		scope = name.substr(0, colon);
		id = name.substr(colon + 1);
	}
	return LIBRARY->identifiers()->getIdentifier(scope, type, id, true);
}

template<typename T>
T requireId(const std::string & type, const std::string & name)
{
	auto id = resolveId(type, name);
	if(!id)
		throw std::runtime_error("unknown " + type + ": " + name);
	return T(*id);
}

std::string sideName(BattleSide side)
{
	return side == BattleSide::ATTACKER ? "attacker" : (side == BattleSide::DEFENDER ? "defender" : "none");
}
}

/// the name of a kind of bonus: the built-in ones by the engine's own list, those of the game's files by their identifier
static std::string bonusName(BonusType type)
{
#define BONUS_NAME(x) #x,
	static const std::vector<std::string> builtin = { BONUS_LIST };
#undef BONUS_NAME
	const auto index = static_cast<size_t>(type);
	if(index < builtin.size())
		return builtin[index];
	return LIBRARY->bth->bonusToString(type);
}

int dumpLibrary(const po::variables_map & vm)
{
	JsonNode out;

	for(const auto & creature : LIBRARY->creh->objects)
	{
		if(!creature)
			continue;
		JsonNode c;
		c["id"].String() = creature->getJsonKey();
		c["name"].String() = creature->getNameSingularTranslated();
		c["level"].Integer() = creature->getLevel();
		c["faction"].String() = creature->getFactionID().hasValue() ? creature->getFactionID().toEntity(LIBRARY)->getJsonKey() : std::string();
		c["aiValue"].Integer() = creature->getAIValue();
		c["fightValue"].Integer() = creature->getFightValue();
		c["hp"].Integer() = creature->getBaseHitPoints();
		c["speed"].Integer() = creature->getBaseSpeed();
		c["attack"].Integer() = creature->getBaseAttack();
		c["defense"].Integer() = creature->getBaseDefense();
		c["damageMin"].Integer() = creature->getBaseDamageMin();
		c["damageMax"].Integer() = creature->getBaseDamageMax();
		c["shots"].Integer() = creature->getBaseShots();
		c["doubleWide"].Bool() = creature->isDoubleWide();
		c["growth"].Integer() = creature->getGrowth();
		c["gold"].Integer() = creature->getRecruitCost(EGameResID::GOLD);
		c["hasUpgrades"].Bool() = creature->hasUpgrades();
		c["special"].Bool() = creature->special;
		// every ability, as the game holds it after all mods: kind of bonus, what it refers to, value
		// (for the catalogue of abilities, arena/ability_catalog.py)
		for(const auto & bonus : creature->getExportedBonusList())
		{
			if(!bonus)
				continue;
			JsonNode b;
			b["type"].String() = bonusName(bonus->type);
			try
			{
				if(bonus->subtype.hasValue())
					b["subtype"].String() = bonus->subtype.toString();
			}
			catch(const std::exception &)
			{
				b["subtype"].String() = "?";
			}
			b["val"].Integer() = bonus->val;
			if(bonus->type == BonusType::SPELL_AFTER_ATTACK || bonus->type == BonusType::SPELL_BEFORE_ATTACK || bonus->type == BonusType::SPELLCASTER
				|| bonus->type == BonusType::SPELL_LIKE_ATTACK || bonus->type == BonusType::ENCHANTER || bonus->type == BonusType::SPECIFIC_SPELL_POWER)
			{
				const SpellID spell = bonus->subtype.as<SpellID>();
				if(spell.hasValue() && spell.toSpell())
					b["spell"].String() = spell.toSpell()->getJsonKey();
			}
			c["bonuses"].Vector().push_back(b);
		}
		out["creatures"].Vector().push_back(c);
	}

	for(const auto & hero : LIBRARY->heroh->objects)
	{
		if(!hero)
			continue;
		JsonNode h;
		h["id"].String() = hero->getJsonKey();
		h["name"].String() = hero->getNameTranslated();
		h["class"].String() = hero->heroClass ? hero->heroClass->getJsonKey() : std::string();
		h["faction"].String() = hero->heroClass ? hero->heroClass->faction.toEntity(LIBRARY)->getJsonKey() : std::string();
		h["magicClass"].Bool() = hero->heroClass && hero->heroClass->affinity == CHeroClass::MAGIC;
		h["special"].Bool() = hero->special;
		out["heroes"].Vector().push_back(h);
	}

	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell)
			continue;
		JsonNode sp;
		sp["id"].String() = spell->getJsonKey();
		sp["level"].Integer() = spell->getLevel();
		sp["combat"].Bool() = spell->isCombat();
		sp["adventure"].Bool() = spell->isAdventure();
		sp["special"].Bool() = spell->isSpecial();
		sp["creatureAbility"].Bool() = spell->isCreatureAbility();
		out["spells"].Vector().push_back(sp);
	}

	for(const auto & skill : LIBRARY->skillh->objects)
	{
		if(skill)
			out["skills"].Vector().emplace_back(skill->getJsonKey());
	}

	const std::string path = vm["arena-dump-library"].as<std::string>();
	std::ofstream file(path, std::ios::trunc);
	file << out.toString();
	std::cout << "ARENA_LIBRARY " << path << std::endl;
	return 0;
}

int generateBattleMap(const po::variables_map & vm)
{
	const std::string scenarioFile = vm["arena-genbattle"].as<std::string>();
	const std::string outFile = vm.count("arena-out") ? vm["arena-out"].as<std::string>() : std::string("battle.vmap");

	try
	{
		std::ifstream in(scenarioFile);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		JsonNode scenario(reinterpret_cast<const std::byte *>(text.data()), text.size(), scenarioFile);

		auto rng = &CRandomGenerator::getDefault();
		auto map = std::make_unique<CMap>(nullptr);
		map->version = EMapFormat::VCMI;
		map->creationDateTime = std::time(nullptr);
		map->width = 10;
		map->height = 10;
		map->mapLayers = {MapLayerId::SURFACE};
		map->battleOnly = true;
		map->name.appendRawString(scenario["name"].isNull() ? std::string("Arena battle") : scenario["name"].String());

		EditorCallback callback(map.get());
		map->cb = &callback;

		map->initTerrain();
		map->getEditManager()->clearTerrain(rng);
		map->getEditManager()->getTerrainSelection().selectAll();
		const std::string terrain = scenario["terrain"].isNull() ? std::string("dirt") : scenario["terrain"].String();
		map->getEditManager()->drawTerrain(requireId<TerrainId>("terrain", terrain), 0, rng);

		map->players[0].canComputerPlay = true;
		map->players[0].canHumanPlay = true;
		map->players[1] = map->players[0];

		const auto & sides = scenario["sides"].Vector();
		if(sides.size() != 2)
			throw std::runtime_error("scenario needs exactly 2 sides");

		for(int sel = 0; sel < 2; ++sel)
		{
			const JsonNode & side = sides[sel];
			if(!side["guard"].isNull())
			{
				// map guards instead of a hero: a wandering monster the first side's hero attacks; the game
				// splits it into stacks by its own rules (hero strength, position, chance of an upgraded stack)
				if(sel != 1)
					throw std::runtime_error("only the second side can be a guard");
				auto creature = requireId<CreatureID>("creature", side["guard"].Vector().at(0).String());
				int count = static_cast<int>(side["guard"].Vector().at(1).Integer());
				auto guardFactory = LIBRARY->objtypeh->getHandlerFor(Obj::MONSTER, creature);
				auto guard = std::dynamic_pointer_cast<CGCreature>(guardFactory->create(&callback, guardFactory->getTemplates().front()));
				guard->initialCharacter = CGCreature::Character::SAVAGE;
				guard->neverFlees = true;
				guard->notGrowingTeam = true;
				guard->putStack(SlotID(0), std::make_unique<CStackInstance>(&callback, creature, std::max(1, count)));
				guard->pos = int3(5, 5, 0);
				map->getEditManager()->insertObject(guard);
				map->players[1].canComputerPlay = false; // nobody owns the guards
				map->players[1].canHumanPlay = false;
				continue;
			}
			auto heroType = requireId<HeroTypeID>("hero", side["hero"].String());
			auto factory = LIBRARY->objtypeh->getHandlerFor(Obj::HERO, heroType.toHeroType()->heroClass->getId());
			auto templates = factory->getTemplates();
			auto hero = std::dynamic_pointer_cast<CGHeroInstance>(factory->create(&callback, templates.front()));
			hero->setHeroType(heroType);
			hero->setOwner(PlayerColor(sel));
			hero->pos = sel == 0 ? int3(5, 6, 0) : int3(5, 5, 0);

			const auto & primary = side["primary"].Vector();
			for(size_t i = 0; i < GameConstants::PRIMARY_SKILLS; i++)
				hero->pushPrimSkill(PrimarySkill(i), i < primary.size() ? primary[i].Integer() : 0);

			hero->clearSlots();
			const auto & army = side["army"].Vector();
			for(size_t slot = 0; slot < army.size() && slot < GameConstants::ARMY_SIZE; ++slot)
			{
				auto creature = requireId<CreatureID>("creature", army[slot].Vector().at(0).String());
				int count = static_cast<int>(army[slot].Vector().at(1).Integer());
				if(count > 0)
					hero->setCreature(SlotID(slot), creature, count);
			}

			bool spellBook = side["spellBook"].isNull() ? !side["spells"].Vector().empty() : side["spellBook"].Bool();
			if(!hero->getArt(ArtifactPosition::SPELLBOOK) && spellBook)
				hero->putArtifact(ArtifactPosition::SPELLBOOK, map->createArtifact(ArtifactID::SPELLBOOK));
			else if(hero->getArt(ArtifactPosition::SPELLBOOK) && !spellBook)
				hero->removeArtifact(ArtifactPosition::SPELLBOOK);

			if(side["warMachines"].Bool())
			{
				hero->putArtifact(ArtifactPosition::MACH1, map->createArtifact(ArtifactID::BALLISTA));
				hero->putArtifact(ArtifactPosition::MACH2, map->createArtifact(ArtifactID::AMMO_CART));
				hero->putArtifact(ArtifactPosition::MACH3, map->createArtifact(ArtifactID::FIRST_AID_TENT));
			}

			for(const auto & spell : side["spells"].Vector())
				hero->addSpellToSpellbook(requireId<SpellID>("spell", spell.String()));

			for(const auto & skill : LIBRARY->skillh->objects)
				hero->setSecSkillLevel(SecondarySkill(skill->getId()), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			static const std::map<std::string, MasteryLevel::Type> levels = {
				{"basic", MasteryLevel::BASIC}, {"advanced", MasteryLevel::ADVANCED}, {"expert", MasteryLevel::EXPERT}
			};
			for(const auto & skill : side["secondary"].Vector())
				hero->setSecSkillLevel(requireId<SecondarySkill>("skill", skill.Vector().at(0).String()), levels.at(skill.Vector().at(1).String()), ChangeValueMode::ABSOLUTE);

			map->getEditManager()->insertObject(hero);
		}

		CMapService mapService;
		mapService.saveMap(map, outFile);
		std::cout << "ARENA_BATTLE_MAP " << outFile << std::endl;
		return 0;
	}
	catch(const std::exception & e)
	{
		std::cerr << "ARENA_BATTLE_MAP_FAILED " << e.what() << std::endl;
		return 1;
	}
}

void onBattleResult(CClient & client, const BattleResult & result)
{
	if(!isActive())
		return;

	const CGameState & gs = client.gameState();
	const auto * battle = gs.getBattle(result.battleID);
	if(!battle)
		return;

	JsonNode info;
	info["winnerSide"].String() = sideName(result.winner);
	info["result"].String() = result.result == EBattleResult::NORMAL ? "normal" : (result.result == EBattleResult::ESCAPE ? "escape" : "surrender");

	const JsonNode & battleAIs = arenaSettings()["battleAI"];
	for(BattleSide side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		JsonNode s;
		PlayerColor player = battle->getSidePlayer(side);
		s["player"].String() = player.isValidPlayer() ? player.toString() : std::string("neutral");
		std::string battleAI = "?";
		if(battleAIs.isVector() && player.isValidPlayer() && player.getNum() < static_cast<int>(battleAIs.Vector().size()))
			battleAI = battleAIs.Vector()[player.getNum()].String();
		s["battleAI"].String() = battleAI;

		const CArmedInstance * army = battle->getSideArmy(side);
		uint64_t initial = army ? army->getArmyStrength() : 0;
		uint64_t lost = 0;
		for(const auto & casualty : result.casualties[side])
			lost += static_cast<uint64_t>(casualty.first.toCreature()->getAIValue()) * casualty.second;
		s["initialStrength"].Integer() = initial;
		s["lostStrength"].Integer() = lost;
		s["remainingFraction"].Float() = initial ? std::max(0.0, 1.0 - static_cast<double>(lost) / initial) : 0.0;
		info["sides"].Vector().push_back(s);
	}

	if(!gs.getMap().battleOnly)
	{
		logGlobal->info("ARENA_BATTLE %s", info.toCompactString());
		std::lock_guard lock(arenaMutex);
		if(battle->getSidePlayer(BattleSide::ATTACKER).isValidPlayer() && battle->getSidePlayer(BattleSide::DEFENDER).isValidPlayer())
		{
			const auto * attackerHero = battle->getSideHero(BattleSide::ATTACKER);
			const auto * defenderHero = battle->getSideHero(BattleSide::DEFENDER);
			JsonNode fight;
			auto add = [&fight](int64_t value)
			{
				JsonNode n;
				n.Integer() = value;
				fight.Vector().push_back(n);
			};
			add(gs.getDate(Date::DAY));
			add(battle->getSidePlayer(BattleSide::ATTACKER).getNum());
			add(battle->getSidePlayer(BattleSide::DEFENDER).getNum());
			add(info["sides"].Vector().at(0)["initialStrength"].Integer());
			add(info["sides"].Vector().at(1)["initialStrength"].Integer());
			add(info["sides"].Vector().at(0)["lostStrength"].Integer());
			add(info["sides"].Vector().at(1)["lostStrength"].Integer());
			add(result.winner == BattleSide::ATTACKER ? 0 : 1);
			add(battle->getDefendedTown() ? 1 : 0);
			add(defenderHero ? 1 : 0);
			add(attackerHero ? attackerHero->level : 0);
			add(defenderHero ? defenderHero->level : 0);
			add(result.result == EBattleResult::NORMAL ? 0 : (result.result == EBattleResult::ESCAPE ? 1 : 2));
			// the defender stood in the gate of a town whose garrison held another hero: fought outside the walls, alone
			add(defenderHero && defenderHero->getVisitedTown() && !battle->getDefendedTown() ? 1 : 0);
			playerBattles.push_back(fight);
		}
		for(BattleSide side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const BattleSide otherSide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
			const PlayerColor player = battle->getSidePlayer(side);
			if(!player.isValidPlayer())
				continue;
			const int64_t lost = info["sides"].Vector().at(static_cast<int>(side))["lostStrength"].Integer();
			const int64_t killed = info["sides"].Vector().at(static_cast<int>(otherSide))["lostStrength"].Integer();
			BattleTally & tally = battleTally[player];
			if(battle->getSidePlayer(otherSide).isValidPlayer())
			{
				tally.playerFights++;
				tally.playerFightsWon += result.winner == side ? 1 : 0;
				tally.playerLosses += lost;
				tally.playerKills += killed;
			}
			else
			{
				tally.guardFights++;
				tally.guardFightsLost += result.winner == side ? 0 : 1;
				tally.guardLosses += lost;
				tally.guardKills += killed;
			}
		}
		return;
	}

	std::lock_guard lock(arenaMutex);
	if(finished)
		return;
	finished = true;

	std::optional<PlayerColor> winner;
	if(result.winner == BattleSide::ATTACKER || result.winner == BattleSide::DEFENDER)
		winner = battle->getSidePlayer(result.winner);

	JsonNode extra;
	extra["battle"] = info;
	finishMatch(client, "battle", winner, extra);
}

}
