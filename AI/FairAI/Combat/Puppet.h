/*
 * Puppet.h, part of VCMI engine - FairAI
 *
 * Test bench only: the units of a side are played from outside the game. With FAI_PUPPET_PORT set,
 * FairBattleAI describes the battle before every decision as one line of JSON to a program that
 * listens on that port of this machine, and plays the action it answers (arena/puppet.py). So both
 * sides of a battle can be played by strategies written and searched outside the game - against
 * each other, or against a combat AI - with the game itself as the referee.
 *
 * Nothing is told that the side could not see on the battlefield, and nothing is done that a
 * player could not do: the answer is an ordinary unit action. Without FAI_PUPPET_PORT (every real
 * game) nothing of this runs.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleSide.h"

VCMI_LIB_NAMESPACE_BEGIN
class CBattleInfoCallback;
class CStack;
VCMI_LIB_NAMESPACE_END

namespace FAI::Combat::Puppet
{

/// Is the test bench's remote control on for this side? (FAI_PUPPET_PORT, FAI_PUPPET_SIDES: "0" the
/// attacker, "1" the defender, "01" both - the default)
bool controls(BattleSide side);
/// Is anybody listening at all? (then every action of the battle is reported, see report())
bool listening();
/// Ask what \p stack does. nullopt: no answer, or the answer "auto" - FairBattleAI decides itself.
std::optional<BattleAction> ask(const CBattleInfoCallback & battle, const CStack * stack, BattleSide side);
/// Tell the listener an action that is about to happen (whoever chose it), with the battle before it.
/// \p sequence: the number of this action in the battle, counted by the caller - when both sides are
/// FairBattleAI each is told of every action, and only the first report of a number is passed on.
void report(const CBattleInfoCallback & battle, const BattleAction & action, int sequence);
/// A new battle begins: numbering starts again.
void battleBegins();

}
