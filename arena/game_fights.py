#!/usr/bin/env python3
"""Fights against map guards taken from logged games, staged as battles of the guard benchmark.

The guard benchmark (guard_bench.py) lets made-up armies meet guards at fixed strength ratios. A game's
fights are different: most are easy, a few are dear, the armies are what a town had to offer by then.
This tool reads the fights FairAI really fought from the logs of games (client logs written with
ARENA_AI_LOG=debug: battle actions and hits name every stack) and stages them again - our army as it
stood, the guards as one stack of their kind and number (the game splits them by its own rule), and
the hero as it went into the fight: level, the four skills with its artifacts counted in, secondary
skills, the spells it could cast, its spell points, its war machines (logs since 20.11.2026 have a
line "battle hero|..." for that; from older logs the hero comes by name with the skills of the
benchmark's stage and without spells - as with --plain-hero).

  game_fights.py collect --logs 'DIR/*/VCMI_Client_log.txt' ... --out arena/gamefights/NAME.json
                         [--min-loss 0.2 --min-miss 0.1] [--sample 300 --seed 7] --suffix __s0 --client CLIENT
  game_fights.py gen     --set arena/gamefights/NAME.json --client CLIENT      (the maps, into Maps/guardbench)
  then: guard_bench.py play --client CLIENT --ai FairAI --filter __s0 --out DIR [--ai-params JSON]

Sets in the fork (from 91 probe games of 19.11.2026, 13 seats, to day 42):
  arena/gamefights/sample300.json  (suffix __s0) 300 fights drawn at random from 2,685 - a picture of a game's fights
  arena/gamefights/dearest69.json  (suffix __g0) the 69 different fights that cost 20 % of the army or more,
                                   or 10 points more than the combat AI's own forecast at the first move
A suffix must not be part of another set's names (the benchmark's own maps end in __r030__0 and the like).
"""
import argparse
import glob
import json
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import battle_scenarios  # noqa: E402
import run_tournament as rt  # noqa: E402

MASTERY = {1: "basic", 2: "advanced", 3: "expert"}
STACK = re.compile(r"(\w+) battle stack \[(\d+)\]: (\d+) of (\S+) from slot (-?\d+) of armyobj=(\d+)")


def forecast_loss(own, enemy, before):
    if before <= 0:
        return 1.0
    if enemy <= 0:
        return max(0.0, 1.0 - own / before)
    return 1.0 - (own - enemy) / before if own > enemy else 1.0


def library(args):
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    path = work / "library.json"
    if not path.exists():
        config_home = rt.make_config_home("")
        try:
            subprocess.run([args.client, "--headless", "--arena-dump-library", str(path)], capture_output=True, timeout=600, env=rt.client_env(config_home, args.client))
        finally:
            shutil.rmtree(config_home, ignore_errors=True)
    return json.loads(path.read_text())


def read_fights(patterns):
    """Every fight of ours against guards in these logs: day, hero, both armies as they first show, the real loss."""
    fights = []
    for pattern in patterns:
        for path in sorted(glob.glob(pattern)):
            game = Path(path).parent.name
            day, cur = 0, None
            for line in open(path, errors="replace"):
                m = re.search(r"FairAI (\w+) day (\d+):", line)
                if m:
                    day = int(m.group(2))
                m = re.search(r"FairAI (\w+) battle at \(([\d ]+)\): (.+?) \((\d+)\) attacks guards", line)
                if m:
                    cur = {"game": game, "day": day, "color": m.group(1), "hero": m.group(3), "army": int(m.group(4)), "own": {}, "guards": {}, "plan": None, "spells": 0}
                    continue
                if cur is None:
                    continue
                if "FairAI planforecast|" in line:
                    cur["plan"] = [float(x) for x in line.strip().split("FairAI planforecast|")[1].split("|")]
                elif " battle hero|" in line:
                    # our hero as it went into the fight: key | level | attack | defence | power | knowledge (artifacts
                    # counted in) | spell points | their limit | skill=level,... | spells it can cast | war machines
                    part = line.rstrip("\n").split(" battle hero|")[1].split("|")
                    if len(part) >= 11:
                        cur["heroIs"] = {"id": part[0], "level": int(part[1]), "primary": [int(x) for x in part[2:6]], "mana": int(part[6]), "manaLimit": int(part[7]),
                                         "secondary": [[k, MASTERY[int(v)]] for k, v in (item.split("=") for item in part[8].split(",") if item) if 1 <= int(v) <= 3],
                                         "spells": [x for x in part[9].split(",") if x], "machines": [x for x in part[10].split(",") if x]}
                elif "battle spell: our hero" in line:
                    cur["spells"] += 1
                elif "battle action:" in line or "battle hit:" in line:
                    for sm in STACK.finditer(line):
                        side, sid, count, creature, slot = sm.group(1), int(sm.group(2)), int(sm.group(3)), sm.group(4), int(sm.group(5))
                        book = cur["own"] if side == cur["color"] else (cur["guards"] if side == "neutral" else None)
                        if book is not None and sid not in book and slot >= 0:
                            book[sid] = (creature, count, slot)
                elif "battle result:" in line:
                    m = re.search(r"battle result: (\w+), (\d+) of our creatures died \(value (\d+) of (\d+), enemy (\d+)\)", line)
                    if m and cur["own"] and cur["guards"]:
                        cur["real"] = int(m.group(3)) / max(1, int(m.group(4))) if m.group(1) == "won" else 1.0
                        cur["enemy"] = int(m.group(5))
                        cur["forecast"] = forecast_loss(cur["plan"][2], cur["plan"][3], cur["plan"][0]) if cur["plan"] else None
                        fights.append(cur)
                    cur = None
    return fights


def stage(scenarios, args):
    """Scenario files and maps for these fights; returns how many maps were made."""
    work = Path(args.work).resolve()
    (work / "scenarios").mkdir(parents=True, exist_ok=True)
    maps_dir = rt.vcmi_data_dir() / "Maps" / "guardbench"
    maps_dir.mkdir(parents=True, exist_ok=True)
    config_home = rt.make_config_home("")
    env = rt.client_env(config_home, args.client)
    made = 0
    try:
        for scenario in scenarios:
            scen = work / "scenarios" / f"{scenario['name']}.json"
            scen.write_text(json.dumps(scenario, indent=1))
            out = maps_dir / f"{scenario['name']}.vmap"
            res = subprocess.run([args.client, "--headless", "--arena-genbattle", str(scen), "--arena-out", str(out)], capture_output=True, text=True, timeout=300, env=env)
            if res.returncode == 0 and out.exists():
                made += 1
            else:
                print(f"FAILED {scenario['name']}: {res.stderr.strip()[-200:]}")
    finally:
        shutil.rmtree(config_home, ignore_errors=True)
    print(f"{made} battles staged in {maps_dir}")
    return made


def cmd_collect(args):
    lib = library(args)
    value = {c["id"]: c["aiValue"] for c in lib["creatures"]}
    heroes = {h["name"]: h for h in lib["heroes"]}
    fights = read_fights(args.logs)
    chosen = [f for f in fights if f["real"] >= args.min_loss or (f["forecast"] is not None and f["real"] - f["forecast"] >= args.min_miss)]
    # the same fight shows up in several series of games on the same map: once is enough
    unique = {}
    for f in sorted(chosen, key=lambda f: -f["real"]):
        key = (f["hero"], tuple(sorted((c, k) for c, k, _ in f["own"].values())), tuple(sorted((c, k) for c, k, _ in f["guards"].values())))
        unique.setdefault(key, f)
    chosen = list(unique.values())
    if args.sample and len(chosen) > args.sample:
        chosen = random.Random(args.seed).sample(sorted(chosen, key=lambda f: (f["game"], f["day"], f["army"])), args.sample)
    print(f"{len(fights)} fights against guards in the logs, {len(chosen)} different ones chosen")
    scenarios = []
    for n, f in enumerate(sorted(chosen, key=lambda f: -f["real"])):
        own = sorted(f["own"].values(), key=lambda s: s[2])
        kinds = {}
        for creature, count, _ in f["guards"].values():
            kinds[creature] = kinds.get(creature, 0) + count
        # the guard's own kind: the one most stacks are of (one stack may be the upgraded kind)
        base = max(kinds, key=lambda c: (sum(1 for g in f["guards"].values() if g[0] == c), kinds[c]))
        hero = heroes.get(f["hero"])
        if not hero or base not in value or any(c not in value for c, _, _ in own):
            continue
        own_value = sum(value[c] * k for c, k, _ in own)
        stage_name = "early" if own_value < 15000 else ("mid" if own_value < 60000 else "late")
        is_ = f.get("heroIs")
        if is_ and not args.plain_hero:
            # the hero as the log has it. Intelligence only raises the limit of spell points, and the staged
            # hero is given its points directly: left out, so that the limit is not counted twice
            side = {"hero": is_["id"], "level": is_["level"], "primary": is_["primary"], "mana": {"full": is_["manaLimit"], "none": 0}.get(args.mana, is_["mana"]),
                    "secondary": [x for x in is_["secondary"] if x[0] not in ("core:intelligence", "intelligence")],
                    "army": [[c, k] for c, k, _ in own], "spellBook": bool(is_["spells"]), "spells": [x for x in is_["spells"] if x not in battle_scenarios.EXCLUDED_SPELLS],
                    "machines": is_["machines"]}
        else:
            side = {"hero": hero["id"], "primary": battle_scenarios.STAGES[stage_name]["primary"], "army": [[c, k] for c, k, _ in own], "spellBook": False, "spells": []}
        scenarios.append({"name": f"game-{f['game']}-d{f['day']:02d}-{n:03d}{args.suffix}", "terrain": "grass", "kind": "guard", "stage": stage_name, "magic": bool(side["spells"]),
                          "sides": [side, {"guard": [base, sum(kinds.values())]}],
                          "inGame": {"day": f["day"], "lost": round(f["real"], 3), "forecast": None if f["forecast"] is None else round(f["forecast"], 3), "heroCast": f["spells"],
                                     **({"manaLimit": is_["manaLimit"]} if is_ else {})}})
    Path(args.out).write_text(json.dumps(scenarios, indent=1) + "\n")
    print(f"{len(scenarios)} fights written to {args.out}")
    return 0 if stage(scenarios, args) else 1


def cmd_gen(args):
    scenarios = json.loads(Path(args.set).read_text())
    library(args)
    return 0 if stage(scenarios, args) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("--logs", nargs="+", required=True, help="client logs of games (glob patterns)")
    c.add_argument("--out", required=True, help="the set: a JSON file with the fights")
    c.add_argument("--client", required=True)
    c.add_argument("--work", default="guardbench-work")
    c.add_argument("--suffix", required=True, help="end of the map names, e.g. __s0: what guard_bench.py play --filter takes")
    c.add_argument("--min-loss", type=float, default=0.0, help="only fights that cost at least this share of the army ...")
    c.add_argument("--min-miss", type=float, default=2.0, help="... or this much more than the combat AI's forecast at the first move")
    c.add_argument("--sample", type=int, default=0, help="this many of the fights, drawn at random")
    c.add_argument("--seed", type=int, default=1)
    c.add_argument("--mana", choices=["logged", "full", "none"], default="logged", help="spell points the hero starts the staged fight with: as in the game, its full store, or none (what its spells are worth)")
    c.add_argument("--plain-hero", action="store_true", help="the hero by name with the skills of the benchmark's stage and without spells, even where the log has the hero as it was")
    c.set_defaults(func=cmd_collect)
    g = sub.add_parser("gen")
    g.add_argument("--set", required=True)
    g.add_argument("--client", required=True)
    g.add_argument("--work", default="guardbench-work")
    g.set_defaults(func=cmd_gen)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
