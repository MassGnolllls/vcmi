#!/usr/bin/env python3
"""The fights of the map "With Sword and Fire" (HotA), staged one by one.

The map is a chain of riddles: quest guards take exactly the troops the hero was given, so every
fight has to be won without losing a creature. Each fight is staged on its own tile of the map (the
same terrain all around, so the same battlefield and the same split of the monsters), with Kyrre as
she stands there - skills, artifacts, army in the arrangement to be tried - and played by a combat
AI or a written strategy against the guards' AI (arena/puppet.py).

  sword.py list
  sword.py run FIGHT --client build/bin/vcmiclient [--army woodElf:2,woodElf:2,...] [--left fairai]
                     [--right battleai] [--seeds 10] [--out DIR] [--nodice]
"""
import argparse
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import puppet

HERE = Path(__file__).resolve().parent / "puppet_scen" / "sword"

KYRRE = {"hero": "kyrre", "primary": [1, 3, 1, 1], "secondary": [["archery", "basic"], ["logistics", "expert"]],
         "artifacts": ["spiritOfOppression"]}

# name: (tile of the monster, creature, number, what Kyrre has that she did not start with, army as it comes)
FIGHTS = {
    "a1-sprites": ((1, 15), "sprite", 20, {}, [["woodElf", 10]]),
    "a2-cerberi": ((3, 16), "cerberus", 6, {}, [["woodElf", 10], ["royalGriffin", 3]]),
    "a3-gnolls": ((9, 17), "gnoll", 40, {}, [["centaur", 40]]),
    "a4-boars": ((1, 22), "boar", 12, {}, [["centaurCaptain", 40]]),
    "a5-skeletons": ((2, 24), "skeletonWarrior", 64, {}, [["centaurCaptain", 40]]),
    "a6-goblins": ((9, 24), "goblin", 50, {"secondary": [["offence", "basic"]]}, [["masterGremlin", 50], ["stoneGargoyle", 4]]),
    "a7-troglodytes": ((21, 23), "infernalTroglodyte", 30, {"secondary": [["offence", "basic"]]}, [["minotaurKing", 1]]),
    "a8-dwarves": ((13, 25), "battleDwarf", 13, {"secondary": [["offence", "basic"]]}, [["minotaurKing", 1]]),
    "a9-harpies": ((20, 20), "harpyHag", 12, {"secondary": [["offence", "basic"]]}, [["minotaurKing", 2]]),
}


def parse_army(text):
    army = []
    for part in text.split(","):
        kind, _, count = part.partition(":")
        army.append([kind.strip(), int(count or 1)])
    return army


def scenario(fight, army=None):
    tile, creature, count, extra, default_army = FIGHTS[fight]
    terrain = json.loads((HERE / "terrain.json").read_text())
    army = army or default_army
    hero = dict(KYRRE)
    hero["secondary"] = KYRRE["secondary"] + extra.get("secondary", [])
    hero["artifacts"] = KYRRE["artifacts"] + extra.get("artifacts", [])
    hero["army"] = army
    tag = "-".join(f"{kind[:3]}{n}" for kind, n in army)
    return {
        "name": f"sw_{fight}_{tag}",
        "note": "With Sword and Fire: " + fight,
        "mapSize": [len(terrain["grid"][0]), len(terrain["grid"])],
        "grid": terrain["grid"],
        "legend": terrain["legend"],
        "tile": list(tile),
        "sides": [hero, {"guard": [creature, count], "stacks": -1, "upgraded": -1}],
    }


def run(client, fight, army, left, right, seeds, out, nodice=False, jobs=2, first_seed=1):
    scen = scenario(fight, army)
    testmap = puppet.generate(client, scen)
    lplay, rplay = puppet.resolve(left), puppet.resolve(right)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)

    def one(seed):
        return puppet.play(client, testmap, lplay, rplay, seed, out, f"{puppet.map_name(scen)}_{left}_{right}_{'n' if nodice else 'x'}{seed}", nodice=nodice)

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        records = list(pool.map(one, range(first_seed, first_seed + seeds)))
    with open(out / "records.jsonl", "a") as sink:
        for rec in records:
            sink.write(json.dumps(rec) + "\n")
    won = clean = 0
    lost = {}
    for rec in records:
        battle = (rec.get("result") or {}).get("battle") or {}
        sides = battle.get("sides") or [{}, {}]
        if battle.get("winnerSide") == "attacker":
            won += 1
            clean += 0 if sides[0].get("lost") else 1
        for kind, n in (sides[0].get("lost") or {}).items():
            lost[kind.split(":")[-1]] = lost.get(kind.split(":")[-1], 0) + n
    n = len(records)
    return {"fight": fight, "army": army, "left": left, "right": right, "battles": n, "won": won, "clean": clean,
            "lost_per_battle": {k: round(v / n, 2) for k, v in lost.items()},
            "errors": sum(1 for r in records if r.get("error")), "rounds": round(sum(r.get("rounds", 0) for r in records) / max(1, n), 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("run")
    p.add_argument("fight")
    p.add_argument("--client", required=True)
    p.add_argument("--army", action="append", help="an arrangement to try, e.g. woodElf:2,woodElf:2,woodElf:6 (repeatable)")
    p.add_argument("--left", default="fairai")
    p.add_argument("--right", default="battleai")
    p.add_argument("--seeds", type=int, default=10)
    p.add_argument("--first-seed", type=int, default=1)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--out", default="sword-out")
    p.add_argument("--nodice", action="store_true")
    args = parser.parse_args()
    if args.cmd == "list":
        for name, (tile, creature, count, extra, army) in FIGHTS.items():
            print(f"{name:<16} {count} {creature} at {tile}; army {army}; extra {extra}")
        return 0
    for text in args.army or [None]:
        result = run(args.client, args.fight, parse_army(text) if text else None, args.left, args.right, args.seeds, args.out,
                     nodice=args.nodice, jobs=args.jobs, first_seed=args.first_seed)
        print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
