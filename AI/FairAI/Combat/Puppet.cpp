/*
 * Puppet.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Puppet.h"

#include <boost/asio/ip/tcp.hpp>

#include "../../../lib/CStack.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/json/JsonNode.h"

namespace FAI::Combat::Puppet
{

namespace
{

std::mutex lineMutex;
std::unique_ptr<boost::asio::ip::tcp::iostream> line;
bool lineFailed = false;
int reportedUpTo = -1;

int port()
{
	static const int value = []()
	{
		const char * text = std::getenv("FAI_PUPPET_PORT");
		return text ? std::atoi(text) : 0;
	}();
	return value;
}

std::string sides()
{
	static const std::string value = []()
	{
		const char * text = std::getenv("FAI_PUPPET_SIDES");
		return std::string(text ? text : "01");
	}();
	return value;
}

/// one line out, and - if \p answer is given - one line back. False: the listener is gone.
bool exchange(std::string text, std::string * answer)
{
	std::replace(text.begin(), text.end(), '\n', ' '); // (the JSON writer breaks lines; nothing we send has one of its own)
	std::lock_guard<std::mutex> guard(lineMutex);
	if(lineFailed)
		return false;
	if(!line)
	{
		line = std::make_unique<boost::asio::ip::tcp::iostream>("127.0.0.1", std::to_string(port()));
		if(!*line)
		{
			logAi->warn("FairAI puppet|nobody listens on port %d", port());
			lineFailed = true;
			return false;
		}
		boost::system::error_code ignored;
		line->rdbuf()->set_option(boost::asio::ip::tcp::no_delay(true), ignored);
	}
	*line << text << '\n' << std::flush;
	if(answer)
		std::getline(*line, *answer);
	if(!*line)
	{
		logAi->warn("FairAI puppet|the listener is gone");
		lineFailed = true;
		return false;
	}
	return true;
}

bool onField(const battle::Unit * u)
{
	return u->alive() && !u->isGhost() && u->getPosition().isValid();
}

JsonNode describe(const CBattleInfoCallback & battle, const battle::Unit * active)
{
	JsonNode out;
	out["round"].Integer() = battle.battleGetRound();
	out["active"].Integer() = active ? static_cast<int>(active->unitId()) : -1;
	out["terrain"].Integer() = battle.battleTerrainType().getNum();
	out["field"].Integer() = battle.battleGetBattlefieldType().getNum();
	for(const auto * u : battle.battleGetUnitsIf(onField))
	{
		JsonNode n;
		n["id"].Integer() = u->unitId();
		n["side"].Integer() = u->unitSide() == BattleSide::ATTACKER ? 0 : 1;
		n["slot"].Integer() = u->unitSlot().getNum();
		n["type"].String() = u->creatureId().toCreature()->getJsonKey();
		n["count"].Integer() = u->getCount();
		n["base"].Integer() = u->unitBaseAmount();
		n["hp"].Integer() = u->getFirstHPleft();
		n["maxhp"].Integer() = u->getMaxHealth();
		n["pos"].Integer() = u->getPosition().toInt();
		n["wide"].Bool() = u->doubleWide();
		n["speed"].Integer() = u->getMovementRange(0);
		n["fly"].Bool() = u->hasBonusOfType(BonusType::FLYING);
		n["shots"].Integer() = u->isShooter() ? u->acquireState()->shots.available() : 0;
		n["canShoot"].Bool() = battle.battleCanShoot(u);
		n["retal"].Bool() = u->ableToRetaliate();
		n["waited"].Bool() = u->waited();
		n["moved"].Bool() = u->moved();
		n["defending"].Bool() = u->defended();
		n["canMove"].Bool() = u->canMove();
		n["att"].Integer() = u->getAttack(false);
		n["def"].Integer() = u->getDefense(false);
		n["dmg"].Vector().emplace_back(u->getMinDamage(false));
		n["dmg"].Vector().emplace_back(u->getMaxDamage(false));
		n["morale"].Integer() = u->moraleVal();
		n["luck"].Integer() = u->luckVal();
		out["units"].Vector().push_back(n);
	}
	for(const auto & obstacle : battle.battleGetAllObstacles())
		if(obstacle->blocksTiles())
			for(const auto & hex : obstacle->getBlockedTiles())
				out["obst"].Vector().emplace_back(hex.toInt());
	return out;
}

}

bool listening()
{
	return port() > 0 && !lineFailed;
}

bool controls(BattleSide side)
{
	if(!listening())
		return false;
	return sides().find(side == BattleSide::ATTACKER ? '0' : '1') != std::string::npos;
}

void battleBegins()
{
	std::lock_guard<std::mutex> guard(lineMutex);
	reportedUpTo = -1;
}

std::optional<BattleAction> ask(const CBattleInfoCallback & battle, const CStack * stack, BattleSide side)
{
	try
	{
		JsonNode out = describe(battle, stack);
		out["t"].String() = "ask";
		out["side"].Integer() = side == BattleSide::ATTACKER ? 0 : 1;

		// what the unit can do in this turn, as the game sees it: the hexes it can move to, and every
		// enemy it can strike with the hexes to strike from
		const auto reachability = battle.getReachability(stack);
		const auto hexes = battle.battleGetAvailableHexes(reachability, stack, true);
		std::vector<BattleHex> standing;
		standing.push_back(stack->getPosition());
		for(const auto & hex : hexes)
		{
			out["reach"].Vector().emplace_back(hex.toInt());
			standing.push_back(hex);
		}
		const auto enemies = battle.battleGetUnitsIf([&](const battle::Unit * u) { return onField(u) && u->unitSide() != stack->unitSide(); });
		for(const auto * enemy : enemies)
		{
			for(const auto & hex : standing)
			{
				if(!CStack::isMeleeAttackPossible(stack, enemy, hex))
					continue;
				JsonNode pair;
				pair.Vector().emplace_back(static_cast<int>(enemy->unitId()));
				pair.Vector().emplace_back(hex.toInt());
				out["attacks"].Vector().push_back(pair);
			}
			if(battle.battleCanShoot(stack, enemy->getPosition()))
				out["shoot"].Vector().emplace_back(static_cast<int>(enemy->unitId()));
		}

		std::string answer;
		if(!exchange(out.toCompactString(), &answer))
			return std::nullopt;
		JsonNode in(reinterpret_cast<const std::byte *>(answer.data()), answer.size(), "puppet");
		const std::string what = in["a"].String();
		if(what == "wait" && !stack->waited())
			return BattleAction::makeWait(stack);
		if(what == "defend" || what == "wait")
			return BattleAction::makeDefend(stack);
		if(what == "move")
		{
			const BattleHex to(static_cast<si16>(in["hex"].Integer()));
			if(hexes.contains(to))
				return BattleAction::makeMove(stack, to);
			logAi->warn("FairAI puppet|move to %d is not possible", to.toInt());
			return std::nullopt;
		}
		if(what == "shoot" || what == "attack")
		{
			const battle::Unit * target = nullptr;
			for(const auto * enemy : enemies)
				if(static_cast<int64_t>(enemy->unitId()) == in["target"].Integer())
					target = enemy;
			if(!target)
			{
				logAi->warn("FairAI puppet|no such target");
				return std::nullopt;
			}
			if(what == "shoot")
			{
				if(battle.battleCanShoot(stack, target->getPosition()))
					return BattleAction::makeShotAttack(stack, target);
				logAi->warn("FairAI puppet|cannot shoot");
				return std::nullopt;
			}
			const BattleHex from(static_cast<si16>(in["hex"].Integer()));
			if((from == stack->getPosition() || hexes.contains(from)) && CStack::isMeleeAttackPossible(stack, target, from))
				return BattleAction::makeMeleeAttack(stack, target, from);
			logAi->warn("FairAI puppet|attack from %d is not possible", from.toInt());
			return std::nullopt;
		}
		return std::nullopt; // "auto"
	}
	catch(const std::exception & e)
	{
		logAi->warn("FairAI puppet|%s", e.what());
		return std::nullopt;
	}
}

void report(const CBattleInfoCallback & battle, const BattleAction & action, int sequence)
{
	if(!listening() || !action.isUnitAction())
		return;
	{
		std::lock_guard<std::mutex> guard(lineMutex);
		if(sequence <= reportedUpTo)
			return;
		reportedUpTo = sequence;
	}
	try
	{
		const battle::Unit * actor = nullptr;
		for(const auto * u : battle.battleGetUnitsIf(onField))
			if(u->unitId() == action.stackNumber)
				actor = u;
		JsonNode out = describe(battle, actor);
		out["t"].String() = "act";
		out["seq"].Integer() = sequence;
		out["side"].Integer() = action.side == BattleSide::ATTACKER ? 0 : 1;
		out["type"].Integer() = static_cast<int>(action.actionType);
		for(const auto & aim : action.getTarget(&battle))
			out["aim"].Vector().emplace_back(aim.hexValue.toInt());
		exchange(out.toCompactString(), nullptr);
	}
	catch(const std::exception & e)
	{
		logAi->warn("FairAI puppet|%s", e.what());
	}
}

}
