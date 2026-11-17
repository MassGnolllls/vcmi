#!/usr/bin/env python3
"""Catalogue of creature abilities: what every unit of the game can do, and what FairAI knows of it.

Reads the library dump of the game as it runs (all mods applied: `vcmiclient --arena-dump-library`),
groups the creatures by kind of ability and marks each kind with what FairAI's battle simulator and
forecasts do with it. Nothing is taken from memory of the game: the list is what the installed game
defines, so a new creature or a changed ability shows up here as "unknown" until it is dealt with.

  ability_catalog.py --client CLIENT [--mods hota] [--out catalog.md]
  ability_catalog.py --library library.json [--out catalog.md]

States
  formula     part of the damage both sides deal (the game's own estimate in battle, FairAI's copy of
              the formula in forecasts); nothing else to model
  simulated   in the simulator, confirmed by the unit lab
  switch      in the simulator behind a switch that is still off (name in brackets)
  open        not modelled: the simulator plays as if the unit did not have it
  no effect   does not change a battle between armies (map, upkeep, flavour)
"""
import argparse
import collections
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# kind of bonus -> (state, how FairAI handles it)
KNOWN = {
    # --- plain values
    "PRIMARY_SKILL": ("formula", "attack and defence"),
    "STACK_HEALTH": ("formula", "health"),
    "STACKS_SPEED": ("simulated", "speed and turn order"),
    "CREATURE_DAMAGE": ("formula", "damage range"),
    "SHOTS": ("simulated", "ammunition"),
    "SHOOTER": ("simulated", "shoots; melee and range penalties"),
    "FLYING": ("simulated", "flies over obstacles and units"),
    "LUCK": ("simulated", "expected value of the luck dice"),
    "MORALE": ("simulated", "expected value of the morale dice"),
    "NO_MORALE": ("simulated", "morale dice do not apply"),
    "NO_LUCK": ("simulated", "luck dice do not apply"),
    # --- attack and retaliation
    "ADDITIONAL_ATTACK": ("simulated", "strikes or shoots twice"),
    "ADDITIONAL_RETALIATION": ("simulated", "more than one retaliation a round"),
    "UNLIMITED_RETALIATIONS": ("simulated", "retaliates every time"),
    "NO_RETALIATION": ("simulated", "never retaliates"),
    "BLOCKS_RETALIATION": ("simulated", "is not struck back"),
    "ATTACKS_ALL_ADJACENT": ("simulated", "hits everything around it"),
    "RETURN_AFTER_STRIKE": ("simulated", "flies back after its strike"),
    "JOUSTING": ("simulated", "damage grows with the distance charged"),
    "NO_MELEE_PENALTY": ("formula", "full damage in melee"),
    "NO_DISTANCE_PENALTY": ("simulated", "full damage at any range"),
    "NO_WALL_PENALTY": ("open", "matters in sieges, which the simulator does not play"),
    "FREE_SHOOTING": ("simulated", "shoots with an enemy next to it"),
    "FIRE_SHIELD": ("simulated", "burns its attacker"),
    "LIFE_DRAIN": ("simulated", "heals by the damage it deals - to the living only"),
    "HP_REGENERATION": ("simulated", "top creature heals every round"),
    "DOUBLE_DAMAGE_CHANCE": ("simulated", "death blow, as expected value"),
    "ENEMY_DEFENCE_REDUCTION": ("formula", "ignores part of the target's defence"),
    "ENEMY_ATTACK_REDUCTION": ("formula", "ignores part of the attacker's attack"),
    "GENERAL_DAMAGE_REDUCTION": ("formula", "takes less damage"),
    "GENERAL_ATTACK_REDUCTION": ("formula", "deals less damage"),
    "GENERAL_DAMAGE_PREMY": ("formula", "deals more damage"),
    "PERCENTAGE_DAMAGE_BOOST": ("formula", "deals more damage"),
    "HATE": ("formula", "more damage against the creatures it hates"),
    "HATES_TRAIT": ("formula", "more damage against a kind of creature"),
    "DAMAGE_RECEIVED_CAP": ("formula", "damage per hit is capped"),
    "CHARGE_IMMUNITY": ("simulated", "takes no jousting bonus"),
    "SLAYER": ("formula", "more damage against dragons and the like"),
    "FORGETFULL": ("formula", "shoots at half strength"),
    # --- several hexes
    "TWO_HEX_ATTACK_BREATH": ("switch", "breath reaches the hex behind the target [simMultiHex]"),
    "WIDE_BREATH": ("switch", "breath over a wide area [simMultiHex]"),
    "PRISM_HEX_ATTACK_BREATH": ("switch", "breath fans out [simMultiHex]"),
    "THREE_HEADED_ATTACK": ("switch", "bites the neighbours of its target [simMultiHex]"),
    "MULTIHEX_UNIT_ATTACK": ("switch", "attack reaches further hexes, own troops included [simMultiHex]"),
    "MULTIHEX_ENEMY_ATTACK": ("switch", "attack reaches further hexes, enemies only [simMultiHex]"),
    "MULTIHEX_ANIMATION": ("no effect", "how the attack is drawn"),
    "SPELL_LIKE_ATTACK": ("switch", "shot bursts on the hexes around the target, friend and foe [simMultiHex]"),
    # --- beyond damage
    "DEATH_STARE": ("switch", "kills whole creatures of the target, whatever they are worth [simAfterHit]"),
    "ACID_BREATH": ("switch", "acid damage and lost defence [simAfterHit]"),
    "SPELL_AFTER_ATTACK": ("switch", "by what the spell does: puts out of action, ages, poisons, curses, weakens, bolts [simAfterHit]"),
    "SPELL_BEFORE_ATTACK": ("open", "casts before it strikes"),
    "SPECIFIC_SPELL_POWER": ("switch", "strength of the unit's own spell [simAfterHit, for spells after an attack]"),
    "CREATURE_SPELL_POWER": ("open", "strength of the unit's spells"),
    "CREATURE_ENCHANT_POWER": ("open", "how long the unit's spells last"),
    "SPELLCASTER": ("open", "casts a spell instead of attacking"),
    "ADJACENT_SPELLCASTER": ("open", "casts on a neighbour instead of attacking"),
    "ENCHANTER": ("open", "casts a mass spell every few rounds"),
    "RANDOM_SPELLCASTER": ("open", "casts a random helpful spell"),
    "CASTS": ("open", "how often the unit can cast"),
    "DAEMON_SUMMONING": ("open", "raises demons from the dead"),
    "REBIRTH": ("open", "part of the stack rises again when it is destroyed"),
    "SOUL_STEAL": ("open", "grows by the creatures it kills"),
    "MANA_DRAIN": ("open", "takes spell points from the enemy hero every round"),
    "MANA_CHANNELING": ("open", "passes the enemy hero's spent spell points to ours"),
    "MAGIC_MIRROR": ("open", "may throw a spell back"),
    "MAGIC_RESISTANCE": ("open", "chance to resist hostile spells"),
    "SPELL_DAMAGE_REDUCTION": ("open", "takes less damage from spells"),
    "MORE_DAMAGE_FROM_SPELL": ("open", "takes more damage from a spell"),
    "SPELL_IMMUNITY": ("open", "immune to a spell (the hero's spells are not part of the forecast yet)"),
    "SPELL_SCHOOL_IMMUNITY": ("simulated", "fire immunity against fire shield and fireball shots; hero spells are not part of the forecast yet"),
    "LEVEL_SPELL_IMMUNITY": ("simulated", "immune to spells up to a level: spell-like shots and effects after an attack do nothing"),
    "NEGATIVE_EFFECTS_IMMUNITY": ("open", "immune to hostile spells of a school"),
    "MIND_IMMUNITY": ("switch", "cannot be blinded or hypnotised [simAfterHit]"),
    "RECEPTIVE": ("open", "friendly spells of a school work despite immunity"),
    "DISPEL_IMMUNITY": ("open", "its spells cannot be dispelled"),
    "ENCHANTED": ("open", "under a lasting spell from the start"),
    "SPELL_RESISTANCE_AURA": ("open", "neighbours resist spells"),
    "HEALER": ("simulated", "first aid tent"),
    "FIRST_STRIKE": ("open", "retaliates before it is struck"),
    "RANGED_RETALIATION": ("open", "shoots back at shooters"),
    "FEROCITY": ("open", "strikes again when it has killed"),
    "REVENGE": ("open", "damage grows with its own losses"),
    "DEFENSIVE_STANCE": ("open", "more defence while defending"),
    "TRANSMUTATION": ("open", "may turn its target into another creature"),
    "DESTRUCTION": ("open", "kills an extra share of the target"),
    "BLOCK": ("open", "may block a hit"),
    "ACCURATE_SHOT": ("switch", "see DEATH_STARE"),
    "LIMITED_SHOOTING_RANGE": ("open", "shoots only so far"),
    "CATAPULT": ("open", "sieges"),
    "CATAPULT_EXTRA_SHOTS": ("open", "sieges"),
    "WALL_DESTROYER": ("open", "sieges"),
    "SIEGE_WEAPON": ("simulated", "war machine: does not move, knows no morale"),
    # --- what a unit is
    "UNDEAD": ("simulated", "no morale; life drain, death stare, ageing and poison do nothing"),
    "NON_LIVING": ("simulated", "no morale; life drain, death stare, ageing and poison do nothing"),
    "MECHANICAL": ("simulated", "no morale; life drain, death stare, ageing and poison do nothing"),
    "GARGOYLE": ("simulated", "not living: life drain, death stare, ageing and poison do nothing"),
    "DRAGON_NATURE": ("no effect", "what the unit is (targets of some artifacts)"),
    "KING": ("formula", "target of slayer"),
    "NOT_ACTIVE": ("simulated", "blinded, paralysed or petrified units skip their turns until attacked"),
    "LIVING": ("simulated", "the game's own mark of the living: life drain, death stare, ageing and poison work on these only"),
    "FEARFUL": ("switch", "fear: enemies of azure dragons may lose their turn, the fearless do not [simAfterHit]"),
    "NONE": ("no effect", "an ability this version of the engine does not know (written for a newer one): it does nothing in the game either"),
    "BLOCKS_RANGED_RETALIATION": ("open", "is not shot back at (matters against the two units that shoot back)"),
    "CPU_CONTROLLED": ("no effect", "war machines the game steers itself"),
    "CHANGES_SPELL_COST_FOR_ALLY": ("open", "our hero's spells cost less (the price of spell points does not know it)"),
    "CHANGES_SPELL_COST_FOR_ENEMY": ("open", "the enemy hero's spells cost more"),
    "ON_COMBAT_EVENT": ("open", "effects triggered by events of the battle (yeti runemaster)"),
    "YETI_RUNE_LEVEL_COUNTER": ("open", "runes of the yeti runemaster"),
    # --- outside battles
    "SKELETON_TRANSFORMER_TARGET": ("no effect", "what the skeleton transformer makes of it"),
    "VISIONS": ("no effect", "adventure map"),
    "SUMMON_GUARDIANS": ("open", "summons guards when the battle starts"),
    "TERRAIN_NATIVE": ("open", "at home on a terrain: speed and skills there"),
    "CREATURE_GROWTH": ("no effect", "town growth"),
    "GENERATE_RESOURCE": ("no effect", "daily resources from creatures in an army (not counted in the economy yet)"),
    "SPECIAL_CRYSTAL_GENERATION": ("no effect", "weekly crystals of the crystal dragon (not counted in the economy yet)"),
    "NO_TERRAIN_PENALTY": ("no effect", "adventure map: the engine's pathfinder handles it"),
    "LOW_MORALE": ("no effect", "upkeep"),
    "SEA_MOVEMENT": ("no effect", "adventure map"),
    "LAND_MOVEMENT": ("no effect", "adventure map"),
    "SIGHT_RADIUS": ("no effect", "adventure map"),
    "STACK_EXPERIENCE": ("no effect", "stack experience is off in the tested rules"),
    "SPECIAL_UPGRADE": ("no effect", "upgrade paths"),
    "HASTE": ("formula", "speed"),
}

ORDER = ["open", "switch", "simulated", "formula", "no effect"]


def load_library(args):
    if args.library:
        return json.loads(Path(args.library).read_text())
    import run_tournament as rt
    home = rt.make_config_home(args.mods)
    out = Path(home) / "library.json"
    subprocess.run([args.client, "--headless", "--arena-dump-library", str(out)], capture_output=True, timeout=600,
                   env=rt.client_env(home, args.client))
    return json.loads(out.read_text())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--client")
    ap.add_argument("--mods", default="")
    ap.add_argument("--library")
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    if not args.client and not args.library:
        ap.error("--client or --library")
    lib = load_library(args)

    creatures = [c for c in lib["creatures"] if c.get("aiValue", 0) > 0 and c.get("hp", 0) > 0]
    kinds = collections.defaultdict(list)          # kind of bonus -> creatures
    spells = collections.defaultdict(set)          # kind of bonus -> spells named
    for creature in creatures:
        seen = set()
        for bonus in creature.get("bonuses", []):
            kind = bonus.get("type") or "?"
            if bonus.get("spell"):
                spells[kind].add(bonus["spell"].split(":")[-1])
            if kind not in seen:
                seen.add(kind)
                kinds[kind].append(creature["id"])

    by_state = collections.defaultdict(list)
    for kind, units in kinds.items():
        state, how = KNOWN.get(kind, ("unknown", "not in the catalogue yet - look it up and decide"))
        by_state[state].append((kind, units, how))

    lines = ["# What creatures can do, and what FairAI knows of it", "",
             f"{len(creatures)} creatures of the installed game (mods: {args.mods or 'none'}), {len(kinds)} kinds of ability.", ""]
    counts = {state: len(by_state.get(state, [])) for state in ["unknown"] + ORDER}
    lines.append(" | ".join(f"{state}: {counts[state]}" for state in ["unknown"] + ORDER if counts[state]))
    lines.append("")
    for state in ["unknown"] + ORDER:
        rows = sorted(by_state.get(state, []), key=lambda r: (-len(r[1]), r[0]))
        if not rows:
            continue
        lines += [f"## {state}", "", "| ability | creatures | examples | what FairAI does with it |", "|---|---|---|---|"]
        for kind, units, how in rows:
            examples = ", ".join(u.split(":")[-1] for u in units[:5]) + (" ..." if len(units) > 5 else "")
            named = ""
            if spells.get(kind):
                named = " Spells: " + ", ".join(sorted(spells[kind])) + "."
            lines.append(f"| {kind} | {len(units)} | {examples} | {how}.{named} |")
        lines.append("")
    text = "\n".join(lines)
    if args.out:
        Path(args.out).write_text(text + "\n")
        print(f"{args.out}: " + ", ".join(f"{counts[s]} {s}" for s in ["unknown"] + ORDER if counts[s]))
    else:
        print(text)
    return 1 if counts["unknown"] else 0


if __name__ == "__main__":
    sys.exit(main())
