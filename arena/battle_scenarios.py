#!/usr/bin/env python3
"""
Battle scenario generator for the arena.

Creates balanced battle scenarios (two heroes with armies, optional spells) as JSON files in the
format understood by `vcmiclient --arena-genbattle`. Input is the library dump written by
`vcmiclient --arena-dump-library`.

Scenario kinds
  mirror  both sides get exactly the same hero, army and spells -> pure test of combat skill
  cross   two different factions, armies balanced by AI value (within a few percent)
Stages (army size / hero strength)
  early   week 1-2 armies, levels 1-4, weak heroes
  mid     week 3-5 armies, levels 1-6
  late    week 6-10 armies, all levels, strong heroes
"""
import json
import random

STAGES = {
    "early": {"weeks": (1, 2), "maxLevel": 4, "primary": [1, 1, 1, 1], "spellLevels": 2, "spells": 4},
    "mid": {"weeks": (3, 5), "maxLevel": 6, "primary": [4, 3, 2, 2], "spellLevels": 3, "spells": 6},
    "late": {"weeks": (6, 10), "maxLevel": 7, "primary": [8, 7, 5, 5], "spellLevels": 5, "spells": 8},
}

TERRAINS = ["dirt", "grass", "sand", "snow", "swamp", "rough", "subterra", "lava"]

# Spells that are pointless or problematic in these duels (summons need specific setups, etc.)
EXCLUDED_SPELLS = {"core:summonBoat", "core:scuttleBoat", "core:visions", "core:viewEarth", "core:viewAir",
                   "core:disguise", "core:townPortal", "core:dimensionDoor", "core:fly", "core:waterWalk"}


def load_library(path):
    lib = json.load(open(path))
    creatures = [c for c in lib["creatures"]
                 if not c["special"] and c["faction"] not in ("", "core:neutral") and c["growth"] > 0]
    heroes = [h for h in lib["heroes"] if not h["special"]]
    spells = [s for s in lib["spells"]
              if s["combat"] and not s["special"] and not s["creatureAbility"] and s["id"] not in EXCLUDED_SPELLS
              and 1 <= s["level"] <= 5]
    return creatures, heroes, spells


def faction_army(rng, creatures, faction, stage):
    """Army a player could plausibly own at this stage: a few stacks, sized by growth."""
    cfg = STAGES[stage]
    weeks = rng.uniform(*cfg["weeks"])
    by_level = {}
    for c in creatures:
        if c["faction"] == faction and c["level"] <= cfg["maxLevel"]:
            by_level.setdefault(c["level"], []).append(c)
    levels = sorted(by_level)
    stack_count = rng.randint(min(4, len(levels)), min(7, len(levels)))
    chosen_levels = sorted(rng.sample(levels, stack_count))
    army = []
    for level in chosen_levels:
        creature = rng.choice(by_level[level])
        count = max(1, round(creature["growth"] * weeks * rng.uniform(0.6, 1.2)))
        army.append([creature["id"], count])
    return army


def army_value(army, value_of):
    return sum(value_of[c] * n for c, n in army)


def scale_army(army, factor):
    return [[c, max(1, round(n * factor))] for c, n in army]


def pick_hero(rng, heroes, faction):
    candidates = [h for h in heroes if h["faction"] == faction] or heroes
    return rng.choice(candidates)["id"]


def pick_spells(rng, spells, stage):
    cfg = STAGES[stage]
    pool = [s["id"] for s in spells if s["level"] <= cfg["spellLevels"]]
    return sorted(rng.sample(pool, min(cfg["spells"], len(pool))))


def make_scenario(rng, creatures, heroes, spells, kind, stage, magic, index):
    value_of = {c["id"]: c["aiValue"] for c in creatures}
    factions = sorted({c["faction"] for c in creatures})
    primary = list(STAGES[stage]["primary"])
    terrain = rng.choice(TERRAINS)

    if kind == "mirror":
        faction = rng.choice(factions)
        army = faction_army(rng, creatures, faction, stage)
        hero = pick_hero(rng, heroes, faction)
        spell_list = pick_spells(rng, spells, stage) if magic else []
        sides = [
            {"hero": hero, "primary": primary, "army": army, "spellBook": magic, "spells": spell_list},
            {"hero": hero, "primary": primary, "army": army, "spellBook": magic, "spells": spell_list},
        ]
    else:
        f1, f2 = rng.sample(factions, 2)
        a1 = faction_army(rng, creatures, f1, stage)
        a2 = faction_army(rng, creatures, f2, stage)
        a2 = scale_army(a2, army_value(a1, value_of) / max(1, army_value(a2, value_of)))
        spell_list = pick_spells(rng, spells, stage) if magic else []
        sides = [
            {"hero": pick_hero(rng, heroes, f1), "primary": primary, "army": a1, "spellBook": magic, "spells": spell_list},
            {"hero": pick_hero(rng, heroes, f2), "primary": primary, "army": a2, "spellBook": magic, "spells": spell_list},
        ]

    name = f"b{index:03d}_{kind}_{stage}_{'magic' if magic else 'nomagic'}"
    return {"name": name, "terrain": terrain, "kind": kind, "stage": stage, "magic": magic, "sides": sides}


def generate(library_path, count, seed, kinds=("mirror", "cross"), stages=("early", "mid", "late"), magic_ratio=0.5):
    rng = random.Random(seed)
    creatures, heroes, spells = load_library(library_path)
    scenarios = []
    for i in range(count):
        kind = kinds[i % len(kinds)]
        stage = stages[(i // len(kinds)) % len(stages)]
        magic = rng.random() < magic_ratio
        scenarios.append(make_scenario(rng, creatures, heroes, spells, kind, stage, magic, i))
    return scenarios
