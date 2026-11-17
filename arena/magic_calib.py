#!/usr/bin/env python3
"""How much army is an enemy hero's magic worth? Hero against hero, real battles, armies of unequal size.

FairAI decides on the adventure map whether its main army can beat the enemy's: army values times
what the heroes add. What attack and defence add follows from the game's damage formula; what a mass
Slow or a book of damage spells adds was a guess (1.6 and 1.4 times the army). This tool measures it:
the same two armies meet at several size ratios, once without magic and once per magic case, and the
ratio at which half of the battles are won shows what the magic stands for.

  magic_calib.py gen    --client CLIENT [--mods hota] [--work DIR] [--ratios 0.7,1,1.4,2,2.8] [--pairs 12]
  magic_calib.py play   --client CLIENT --out DIR [--mods hota] [--jobs 2] [--ai FairAI] [--opponent BattleAI] [--ai-params JSON]
  magic_calib.py report --out DIR

Red is the AI under test and attacks; blue is the opponent (BattleAI: what Nullkiller2 fights with).
Ratio = red's army value / blue's. Both heroes are the same hero with the same primary skills, so
that only army size and the magic of the case differ.

Cases (who has what; "mass" = expert in the school, so the spell reaches the whole army):
  plain        no spell book on either side
  slow         blue: mass Slow
  slowhaste    blue: mass Slow and mass Haste
  damage       blue: Lightning Bolt, Chain Lightning, Meteor Shower, Implosion
  both_slow    both: mass Slow
  haste_slow   red: mass Haste; blue: mass Slow
  own_slow     red: mass Slow; blue nothing
  own_damage   red: the damage spells; blue nothing
  blind, berserk, resurrect, prayer   blue: that spell, expert in its school
  both_damage  both: the damage spells
"""
import argparse
import collections
import json
import math
import random
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import battle_scenarios  # noqa: E402
import run_tournament as rt  # noqa: E402

SUBDIR = "magiccalib"
SLOW = {"spells": ["core:slow"], "secondary": [["core:earthMagic", "expert"]]}
HASTE = {"spells": ["core:haste"], "secondary": [["core:airMagic", "expert"]]}
SLOWHASTE = {"spells": ["core:slow", "core:haste"], "secondary": [["core:earthMagic", "expert"], ["core:airMagic", "expert"]]}
DAMAGE = {"spells": ["core:lightningBolt", "core:chainLightning", "core:meteorShower", "core:implosion"],
          "secondary": [["core:earthMagic", "expert"], ["core:airMagic", "expert"]]}
BLIND = {"spells": ["core:blind"], "secondary": [["core:fireMagic", "expert"]]}
BERSERK = {"spells": ["core:berserk"], "secondary": [["core:fireMagic", "expert"]]}
RESURRECT = {"spells": ["core:resurrection"], "secondary": [["core:earthMagic", "expert"]]}
PRAYER = {"spells": ["core:prayer"], "secondary": [["core:waterMagic", "expert"]]}
NONE = {"spells": [], "secondary": []}
CASES = {  # name: (red, blue)
    "plain": (NONE, NONE),
    "slow": (NONE, SLOW),
    "slowhaste": (NONE, SLOWHASTE),
    "damage": (NONE, DAMAGE),
    "both_slow": (SLOW, SLOW),
    "haste_slow": (HASTE, SLOW),
    "own_slow": (SLOW, NONE),
    "own_damage": (DAMAGE, NONE),
    "blind": (NONE, BLIND),
    "berserk": (NONE, BERSERK),
    "resurrect": (NONE, RESURRECT),
    "prayer": (NONE, PRAYER),
    "both_damage": (DAMAGE, DAMAGE),
}


def cmd_gen(args) -> int:
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    library = work / "library.json"
    config_home = rt.make_config_home(args.mods)
    env = rt.client_env(config_home, args.client)
    try:
        if not library.exists() or args.force:
            subprocess.run([args.client, "--headless", "--arena-dump-library", str(library)], capture_output=True, timeout=600, env=env)
        lib = json.loads(library.read_text())
        creatures, heroes, _ = battle_scenarios.load_library(str(library))
        value_of = {c["id"]: c["aiValue"] for c in lib["creatures"]}
        factions = sorted({c["faction"] for c in creatures})
        might = [h for h in heroes if not h["magicClass"]]
        ratios = [float(r) for r in args.ratios.split(",")]
        primary = [int(x) for x in args.primary.split(",")]
        maps_dir = rt.vcmi_data_dir() / "Maps" / SUBDIR
        if args.force and maps_dir.exists():
            shutil.rmtree(maps_dir)
        maps_dir.mkdir(parents=True, exist_ok=True)
        rng = random.Random(args.seed)
        index = []
        made = failed = 0
        for pair in range(args.pairs):
            f_red, f_blue = rng.choice(factions), rng.choice(factions)
            stage = args.stage
            blue_army = battle_scenarios.faction_army(rng, creatures, f_blue, stage)
            red_base = battle_scenarios.faction_army(rng, creatures, f_red, stage)
            red_base = battle_scenarios.scale_army(red_base, battle_scenarios.army_value(blue_army, value_of) / max(1, battle_scenarios.army_value(red_base, value_of)))
            hero = rng.choice(might)["id"]
            terrain = rng.choice(battle_scenarios.TERRAINS)
            for ratio in ratios:
                red_army = battle_scenarios.scale_army(red_base, ratio)
                real = battle_scenarios.army_value(red_army, value_of) / max(1, battle_scenarios.army_value(blue_army, value_of))
                for case in args.cases.split(","):
                    red_magic, blue_magic = CASES[case]
                    name = f"calib{args.tag}_{case}_r{int(round(ratio * 100)):03d}_{pair:02d}"
                    sides = []
                    for army, magic in ((red_army, red_magic), (blue_army, blue_magic)):
                        sides.append({"hero": hero, "primary": primary, "army": army, "spellBook": bool(magic["spells"]),
                                      "spells": magic["spells"], "secondary": magic["secondary"]})
                    scenario = {"name": name, "terrain": terrain, "kind": "calib", "stage": stage, "magic": True, "sides": sides}
                    scen_file = work / "scenarios" / f"{name}.json"
                    scen_file.parent.mkdir(parents=True, exist_ok=True)
                    scen_file.write_text(json.dumps(scenario, indent=1))
                    out = maps_dir / f"{name}.vmap"
                    res = subprocess.run([args.client, "--headless", "--arena-genbattle", str(scen_file), "--arena-out", str(out)],
                                         capture_output=True, text=True, timeout=300, env=env)
                    if res.returncode == 0 and out.exists():
                        made += 1
                        index.append({"name": name, "case": case, "ratio": ratio, "realRatio": real, "pair": pair})
                    else:
                        failed += 1
                        print(f"FAILED {name}: {res.stderr.strip()[-300:]}")
        index_file = maps_dir / "index.json"  # sets made earlier (other --tag) stay in the index
        if index_file.exists():
            fresh = {e["name"] for e in index}
            index = [e for e in json.loads(index_file.read_text()) if e["name"] not in fresh] + index
        index_file.write_text(json.dumps(index, indent=1))
        print(f"{made} battles ready in {maps_dir} ({failed} failed)")
        return 0 if made else 1
    finally:
        shutil.rmtree(config_home, ignore_errors=True)


def cmd_play(args) -> int:
    maps_dir = rt.vcmi_data_dir() / "Maps" / SUBDIR
    maps = sorted(p for p in maps_dir.glob("*.vmap") if args.filter in p.name)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    play_args = SimpleNamespace(client=args.client, base_port=args.base_port, timeout=300, stall_seconds=60, keep_logs=False,
                                mods=args.mods, max_days=3, difficulty=4, ai_params=args.ai_params, battle_params=args.ai_params)
    done = errors = 0

    def one(index, path):
        rec = rt.play_match(play_args, index, f"MAPS/{SUBDIR.upper()}/{path.stem.upper()}", args.ai, args.opponent, args.seed + index, out, True)
        shutil.rmtree(out / f"logs_{index:04d}", ignore_errors=True)
        return rec

    with ThreadPoolExecutor(max_workers=args.jobs) as pool, open(out / "results.jsonl", "a") as sink:
        futures = [pool.submit(one, i, p) for i, p in enumerate(maps)]
        for fut in as_completed(futures):
            rec = fut.result()
            done += 1
            errors += 1 if rec.get("error") else 0
            (out / f"match_{rec['match']:04d}.json").unlink(missing_ok=True)
            sink.write(json.dumps(rec) + "\n")
            sink.flush()
            if done % 200 == 0:
                print(f"{done}/{len(maps)} battles ({errors} failed)", flush=True)
    print(f"{done} battles played ({errors} failed)")
    return cmd_report(SimpleNamespace(out=str(out)))


def half_point(points):
    """Army ratio at which half of the battles are won: a logistic curve in the logarithm of the ratio,
    fitted by a coarse search (few points, no need for more)."""
    best = None
    for centre in [x / 100.0 for x in range(-150, 151, 2)]:  # ln of the ratio
        for slope in (2, 3, 4, 6, 8, 12):
            loss = 0.0
            for ratio, wins, games in points:
                p = 1.0 / (1.0 + math.exp(-slope * (math.log(ratio) - centre)))
                p = min(max(p, 1e-6), 1 - 1e-6)
                loss -= wins * math.log(p) + (games - wins) * math.log(1 - p)
            if best is None or loss < best[0]:
                best = (loss, centre, slope)
    return math.exp(best[1])


def cmd_report(args) -> int:
    out = Path(args.out)
    index = {e["name"].upper(): e for e in json.loads((rt.vcmi_data_dir() / "Maps" / SUBDIR / "index.json").read_text())}
    cells = collections.defaultdict(lambda: [0, 0, 0.0, 0.0])  # (case, ratio) -> games, wins, sum of own loss when won, sum of real ratio
    for line in (out / "results.jsonl").open():
        rec = json.loads(line)
        entry = index.get(rec["map"].split("/")[-1].upper())
        battle = (rec.get("result") or {}).get("battle")
        if not entry or not battle:
            continue
        red = next((s for s in battle["sides"] if s["player"] == "red"), None)
        if not red or not red.get("initialStrength"):
            continue
        won = rec["result"].get("winner") == "red"
        cell = cells[(entry["case"], entry["ratio"])]
        cell[0] += 1
        cell[1] += 1 if won else 0
        cell[2] += min(1.0, red["lostStrength"] / red["initialStrength"]) if won else 0.0
        cell[3] += entry["realRatio"]
    cases = [c for c in CASES if any(k[0] == c for k in cells)]
    ratios = sorted({k[1] for k in cells})
    lines = [f"# Magic calibration: {out.name}", "",
             "Red (the AI under test) attacks blue. Cell: battles won of battles played, and the share of red's army lost in the battles it won.", "",
             "| case | " + " | ".join(f"army x{r:g}" for r in ratios) + " | half of the battles won at | magic stands for |",
             "|---|" + "---|" * (len(ratios) + 2)]
    plain_half = None
    report = {}
    for case in cases:
        points = []
        row = []
        for ratio in ratios:
            games, wins, loss, real = cells.get((case, ratio), [0, 0, 0.0, 0.0])
            if games:
                points.append((real / games, wins, games))
                row.append(f"{wins}/{games}" + (f" ({loss / wins:.2f})" if wins else ""))
            else:
                row.append("-")
        half = half_point(points) if points else float("nan")
        if case == "plain":
            plain_half = half
        worth = f"x{half / plain_half:.2f} army on blue's side" if plain_half and case != "plain" else "-"
        report[case] = {"half": half, "points": points}
        lines.append(f"| {case} | " + " | ".join(row) + f" | x{half:.2f} | {worth} |")
    lines += ["", "\"Magic stands for\": by how much red's army must grow to keep half of the battles, compared with the case without magic "
              "(below 1: the magic is red's own and saves that much army)."]
    text = "\n".join(lines)
    (out / "calib_report.md").write_text(text + "\n")
    (out / "calib_report.json").write_text(json.dumps(report, indent=1))
    print(text)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("--client", required=True)
    g.add_argument("--mods", default="")
    g.add_argument("--work", default="magiccalib-work")
    g.add_argument("--ratios", default="0.7,1.0,1.4,2.0,2.8")
    g.add_argument("--pairs", type=int, default=12)
    g.add_argument("--stage", default="late")
    g.add_argument("--primary", default="8,7,5,5", help="attack,defence,power,knowledge of both heroes")
    g.add_argument("--cases", default=",".join(CASES))
    g.add_argument("--seed", type=int, default=3)
    g.add_argument("--tag", default="", help="part of the map names, to keep several sets side by side (play --filter calibTAG_)")
    g.add_argument("--force", action="store_true")
    p = sub.add_parser("play")
    p.add_argument("--client", required=True)
    p.add_argument("--mods", default="")
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--ai", default="FairAI")
    p.add_argument("--opponent", default="BattleAI")
    p.add_argument("--filter", default="")
    p.add_argument("--seed", type=int, default=7000)
    p.add_argument("--base-port", type=int, default=7400)
    p.add_argument("--ai-params", default="")
    r = sub.add_parser("report")
    r.add_argument("--out", required=True)
    args = ap.parse_args()
    return {"gen": cmd_gen, "play": cmd_play, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
