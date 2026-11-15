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

#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace Arena
{

namespace
{
std::mutex arenaMutex;
std::map<PlayerColor, std::string> installedAI;
std::vector<JsonNode> endEvents;
bool finished = false;
const auto startTime = std::chrono::steady_clock::now();

// watchdog: detects matches that stopped making progress (e.g. an AI deadlock)
std::atomic<int64_t> lastActivityMs{0};
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

[[noreturn]] void finishMatch(CClient & client, const std::string & outcome, const std::optional<PlayerColor> & winner)
{
	const CGameState & gs = client.gameState();

	JsonNode result;
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
			if(nowMs() - lastActivityMs.load() < stallSeconds * 1000LL)
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

	const int maxDays = static_cast<int>(arenaSettings()["maxDays"].Integer());
	if(maxDays <= 0)
		return;

	std::lock_guard lock(arenaMutex);
	if(finished)
		return;

	if(client.gameState().getDate(Date::DAY) > maxDays)
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
		{"s", CMapHeader::MAP_SIZE_SMALL}, {"m", CMapHeader::MAP_SIZE_MIDDLE}, {"l", CMapHeader::MAP_SIZE_LARGE}, {"xl", CMapHeader::MAP_SIZE_XLARGE}
	};
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

}
