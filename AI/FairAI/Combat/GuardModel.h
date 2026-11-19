/*
 * GuardModel.h, part of VCMI engine - FairAI
 *
 * What a unit that is played by VCMI's BattleAI does next - the guards of a map, the units of an
 * AI player. A native copy of that AI's decision rules (AI/BattleAI: PotentialTargets,
 * AttackPossibility, BattleExchangeEvaluator, BattleEvaluator::selectStackAction) on the
 * simulator's state: no bonus system and no hypothetical battles, so one answer costs microseconds
 * where BattleAI itself on a mirror of the state (FairBattleAI::oracleAction) costs milliseconds.
 * That is what makes it possible to search several rounds ahead against the real behaviour of the
 * guards.
 *
 * Not copied: attacks that reach several hexes (the model counts the main target only), the
 * spells of creatures, sieges, obstacles that harm. `arena/policy_check.py` measures how often
 * the model, the oracle and the real guards agree.
 *
 * Everything the model reads is visible to both sides of a battle.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CombatSim.h"

namespace FAI::Combat
{

/// The action BattleAI would choose for `unit`, whose turn it is. simulationTurns: how many turns
/// BattleAI plays an exchange of blows ahead (it sets itself up with 2 below the fourth difficulty
/// and with 10 from there on). Returns false where the model does not apply (too many units).
/// trace: if given, what the model saw - turn order, every attack with its values, the move target -
/// is written there (to compare with the oracle's view, FAI_POLICYCHECK=2)
bool guardAction(const SimState & state, int unit, int simulationTurns, SimAction & out, std::string * trace = nullptr);

}
