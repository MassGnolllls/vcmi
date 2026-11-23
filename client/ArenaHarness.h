/*
 * ArenaHarness.h, part of VCMI engine
 *
 * Headless AI-vs-AI test harness ("arena") used to evaluate AIs objectively:
 *  - per-player AI selection (--ai, one value per player color in order)
 *  - deterministic seed, difficulty and cheat permission for the match
 *  - day limit for drawn-out games
 *  - machine-readable result file (JSON) with per-player statistics
 *  - random map generation to a file (--arena-genmap)
 *
 * Active only when the client was started with --arena-result.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN
class PlayerColor;
class EVictoryLossCheckResult;
class StatisticDataSet;
struct BattleResult;
VCMI_LIB_NAMESPACE_END

class CClient;
class CServerHandler;

namespace boost::program_options
{
class variables_map;
}

namespace Arena
{
/// Arena mode is on (result file requested)
bool isActive();

/// Store arena-related command line options in the session settings
void storeOptions(const boost::program_options::variables_map & vm);

/// AI requested for this player via --ai, empty if none
std::string aiForPlayer(const PlayerColor & color);

/// Remember which AI was actually installed for a player (reported in results)
void noteInstalledAI(const PlayerColor & color, const std::string & aiName);

/// Called by the test-game starter after the map was selected, before the game starts
void configureLobby(CServerHandler & handler);

/// Hooks from client net pack handling
void onPackApplied(CClient & client);
void onPlayerStartsTurn(CClient & client, const PlayerColor & player);
void onNewTurn(CClient & client);
void onPlayerEndsGame(CClient & client, const PlayerColor & player, const EVictoryLossCheckResult & result);

/// --arena-genmap: generate a random map into a file and return the process exit code
int generateMap(const boost::program_options::variables_map & vm);

/// --arena-genbattle: build a battle-only map (two heroes, battle starts immediately) from a scenario JSON
int generateBattleMap(const boost::program_options::variables_map & vm);

/// --arena-dump-library: write creatures, heroes, spells and skills as JSON (input for scenario generators)
int dumpLibrary(const boost::program_options::variables_map & vm);

/// --arena-dump-map MAPS/DIR/NAME: objects, armies, heroes and terrain of a map as JSON (--arena-out)
int dumpMap(const boost::program_options::variables_map & vm);

/// Battle result hook (called before the result is applied to the game state)
void onBattleResult(CClient & client, const BattleResult & result);
}
