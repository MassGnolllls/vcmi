/*
 * SpellSim.h, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CombatSim.h"

VCMI_LIB_NAMESPACE_BEGIN
class CSpell;
class IBonusBearer;
VCMI_LIB_NAMESPACE_END

namespace FAI::Combat
{

/// The game's rule of what a spell can work on - immunity to the spell, to its level, to its school,
/// and the conditions the spell itself names - asked of a creature outside a battle. (In a battle the
/// game answers this itself; a forecast on the adventure map has no battle to ask.)
bool spellWorksOn(const CSpell & spell, const IBonusBearer & target, CreatureID creature);

/// Share of a damage spell's raw damage that the target takes: protection from its school, weakness to
/// the spell. 0 if the spell does not work on it at all. The chance to resist is not part of it.
double spellDamageShare(const CSpell & spell, const IBonusBearer & target, CreatureID creature);

/// Chance that the target resists a hostile spell (dwarves, a hero's Resistance), 0 to 1.
double spellResistance(const IBonusBearer & target);

/// The spells of a hero that a forecast plays - so far those that do damage -, with what each does to
/// every stack of the battle. `mana` returns the hero's spell points.
std::vector<SimSpell> describeSpells(const HeroSpec & hero, BattleSide side, const std::vector<StackSpec> & stacks, int & mana);

}
