#!/usr/bin/env python3
"""Unit lab: does FairAI's battle simulator know what every creature really does?

Every creature of the game (with mods: HotA etc.) fights plain reference units in real battles
(FairAI on both sides, no hero spells). Before each action the simulator predicts its result and
logs the comparison with what the game did (FAI_SIMCHECK, see FairBattleAI::actionStarted).
This script builds the duels, plays them and writes a report per creature: damage dealt and taken
versus prediction, healing, effects applied to the target, units put out of action, movement
surprises, changes between actions (regeneration, poison).

  unit_lab.py gen    --client CLIENT [--mods hota] [--value 8000] [--only core:vampireLord,...]
  unit_lab.py play   --client CLIENT [--mods hota] --out DIR [--jobs 2] [--filter text]
  unit_lab.py report --out DIR                      -> DIR/unit_report.md, DIR/unit_report.json

Reading the report: ratio 1.00 = the simulator's expectation matches the game on average.
"""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_tournament as rt  # noqa: E402

LAB_SUBDIR = "unitlab"
# plain opponents: a walker, a shooter and a flyer without special abilities
REFERENCES = ["core:ogre", "core:orcChieftain", "core:wyvern"]
# a might hero without spell book or war machine whose speciality (resistance) does not touch the duel
HEROES = ["core:clancy", "core:clancy"]


def safe(name: str) -> str:
    return "".join(ch if ch.isalnum() else "-" for ch in name)


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
        creatures = {c["id"]: c for c in lib["creatures"]}
        heroes = {h["id"] for h in lib["heroes"]}
        for hero in HEROES:
            if hero not in heroes:
                print(f"hero {hero} missing in the library", file=sys.stderr)
                return 1
        maps_dir = rt.vcmi_data_dir() / "Maps" / LAB_SUBDIR
        maps_dir.mkdir(parents=True, exist_ok=True)
        wanted = [c for c in lib["creatures"] if c["aiValue"] > 0 and c["hp"] > 0 and c["speed"] > 0]
        if args.only:
            wanted = [c for c in wanted if c["id"] in args.only.split(",")]
        made = failed = 0
        def stacks(creature_id, count):
            # up to three stacks: more actions per battle, neighbours for breath and round attacks
            parts = min(3, count)
            sizes = [count // parts + (1 if i < count % parts else 0) for i in range(parts)]
            return [[creature_id, size] for size in sizes]

        for creature in wanted:
            count = max(1, round(args.value / creature["aiValue"]))
            total = count * creature["aiValue"]
            for ref_id in REFERENCES:
                ref = creatures[ref_id]
                ref_count = max(1, round(total / ref["aiValue"]))
                name = f"{safe(creature['id'])}__vs__{safe(ref_id)}"
                out = maps_dir / f"{name}.vmap"
                if out.exists() and not args.force:
                    made += 1
                    continue
                scenario = {"name": name, "terrain": "sand", "kind": "lab", "stage": "lab", "magic": False, "sides": [
                    {"hero": HEROES[0], "primary": [1, 1, 1, 1], "army": stacks(creature["id"], count), "spellBook": False, "spells": []},
                    {"hero": HEROES[1], "primary": [1, 1, 1, 1], "army": stacks(ref_id, ref_count), "spellBook": False, "spells": []}]}
                scen_file = work / "scenarios" / f"{name}.json"
                scen_file.parent.mkdir(parents=True, exist_ok=True)
                scen_file.write_text(json.dumps(scenario, indent=1))
                res = subprocess.run([args.client, "--headless", "--arena-genbattle", str(scen_file), "--arena-out", str(out)],
                                     capture_output=True, text=True, timeout=300, env=env)
                if res.returncode == 0 and out.exists():
                    made += 1
                else:
                    failed += 1
                    print(f"FAILED {name}: {res.stderr.strip()[-300:]}")
        print(f"{made} duel maps ready in {maps_dir} ({failed} failed, {len(wanted)} creatures)")
        return 0
    finally:
        shutil.rmtree(config_home, ignore_errors=True)


def cmd_play(args) -> int:
    maps_dir = rt.vcmi_data_dir() / "Maps" / LAB_SUBDIR
    maps = sorted(p for p in maps_dir.glob("*.vmap") if args.filter in p.name)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.environ["FAI_SIMCHECK"] = "1"
    play_args = SimpleNamespace(client=args.client, base_port=args.base_port, timeout=300, stall_seconds=60, keep_logs=True,
                                mods=args.mods, max_days=3, difficulty=4)
    done = 0
    lines_file = open(out / "simcheck.txt", "a")

    def one(index, path):
        name = f"MAPS/{LAB_SUBDIR.upper()}/{path.stem.upper()}"
        record = rt.play_match(play_args, index, name, "FairAI", "FairAI", 1000 + index, out, True)
        log = out / f"logs_{index:04d}" / "VCMI_Client_log.txt"
        lines = []
        if log.exists():
            for line in log.open(errors="replace"):
                at = line.find("FairAI sim")
                if at < 0:
                    at = line.find("FairAI dmgcheck")
                if at >= 0:
                    lines.append(path.stem + "|" + line[at + 7:].strip())
        if not record.get("error"):  # failed duels keep their logs
            shutil.rmtree(out / f"logs_{index:04d}", ignore_errors=True)
        (out / f"match_{index:04d}.json").unlink(missing_ok=True)
        return path.stem, record.get("error"), lines

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(one, i + round_no * len(maps), p) for round_no in range(args.rounds) for i, p in enumerate(maps)]
        for fut in as_completed(futures):
            stem, error, lines = fut.result()
            done += 1
            if error:
                print(f"{stem}: {error}", flush=True)
            for line in lines:
                lines_file.write(line + "\n")
            lines_file.flush()
            if done % 100 == 0:
                print(f"{done}/{len(maps) * args.rounds} duels", flush=True)
    print(f"{done} duels played, lines in {out / 'simcheck.txt'}")
    return 0


def cmd_report(args) -> int:
    out = Path(args.out).resolve()
    acting = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    suffering = collections.defaultdict(collections.Counter)
    effects = collections.defaultdict(collections.Counter)
    idle = collections.defaultdict(list)
    tested = set()
    formula = collections.defaultdict(list)  # (attacker, defender, kind) -> ratios own formula / game
    for line in (out / "simcheck.txt").open():
        parts = line.rstrip("\n").split("|")
        duel = parts[0]
        tested.add(duel.split("__vs__")[0])
        if len(parts) >= 7 and parts[1] == "dmgcheck":
            game, own = float(parts[5]), float(parts[6])
            formula[(parts[2], parts[3], parts[4])].append(own / game if game > 0 else float("inf"))
            continue
        if len(parts) >= 6 and parts[1] == "simidle":
            idle[parts[2]].append(int(parts[5]) - int(parts[4]))
            continue
        if len(parts) < 17 or parts[1] != "simcheck":
            continue
        (_, _, kind, actor, _, target, _, t_pred, t_real, a_pred, a_real, o_pred, o_real, t_spells, a_spells, inactive, pos_ok) = parts[:17]
        t_pred, t_real, a_pred, a_real, o_pred, o_real = map(int, (t_pred, t_real, a_pred, a_real, o_pred, o_real))
        a = acting[actor][kind]
        a["n"] += 1
        a["tPred"] += t_pred
        a["tReal"] += t_real
        a["aPred"] += a_pred          # own health lost (negative: gained) while acting
        a["aReal"] += a_real
        a["aScale"] += abs(a_pred) + abs(a_real)
        a["oPred"] += o_pred
        a["oReal"] += o_real
        a["inactive"] += int(inactive)
        a["posWrong"] += 0 if pos_ok == "1" else 1
        if t_spells != "-":
            for spell in t_spells.split(","):
                effects[actor]["on its target: " + spell] += 1
        if kind in ("attack", "shoot") and target != "-":
            if a_spells != "-":  # what the attacker caught from the defender's answer
                for spell in a_spells.split(","):
                    effects[target]["on its attacker: " + spell] += 1
            s = suffering[target]
            s["n"] += 1
            s["tPred"] += t_pred
            s["tReal"] += t_real
            s["backPred"] += a_pred
            s["backReal"] += a_real
            s["backScale"] += abs(a_pred) + abs(a_real)

    def ratio(real, pred):
        if pred <= 0:
            return None if real <= 0 else float("inf")
        return real / pred

    def fmt(r):
        return "-" if r is None else ("inf" if r == float("inf") else f"{r:.2f}")

    def deviation(r):
        return 0.0 if r is None else (9.0 if r == float("inf") else abs(r - 1))

    plain = {safe(r) for r in REFERENCES}
    report = {}
    rows = []
    for creature in sorted(set(acting) | set(suffering) | set(idle)):
        key = safe(creature)
        if key in plain and key not in tested:
            continue
        entry = {"findings": [], "samples": 0}
        worst = 0.0
        for kind in ("attack", "shoot"):
            a = acting[creature].get(kind)
            if not a:
                continue
            entry["samples"] += a["n"]
            if a["n"] < args.min_samples:
                continue
            dealt = ratio(a["tReal"], a["tPred"])
            entry[kind] = {"n": a["n"], "dealt": dealt, "ownPred": a["aPred"], "ownReal": a["aReal"], "othersPred": a["oPred"],
                           "othersReal": a["oReal"], "inactive": a["inactive"], "posWrong": a["posWrong"]}
            if deviation(dealt) > args.tolerance:
                entry["findings"].append(f"{kind}: deals {fmt(dealt)}x the predicted damage (n={a['n']})")
                worst = max(worst, deviation(dealt))
            own_gap = abs(a["aReal"] - a["aPred"]) / max(1, a["aScale"])
            if a["aScale"] > 0 and own_gap > args.tolerance and abs(a["aReal"] - a["aPred"]) > 5 * a["n"]:
                entry["findings"].append(f"{kind}: own health changes by {-a['aReal']:+d}, predicted {-a['aPred']:+d} (n={a['n']})")
                worst = max(worst, 2 * own_gap)
            others_gap = abs(a["oReal"] - a["oPred"])
            if others_gap > 0.15 * max(1, a["tReal"]) and others_gap > 0.3 * max(a["oReal"], a["oPred"]):
                entry["findings"].append(f"{kind}: other units lose {a['oReal']} health, predicted {a['oPred']} (n={a['n']})")
                worst = max(worst, 1.5)
            if a["inactive"]:
                entry["findings"].append(f"{kind}: put its target out of action in {a['inactive']} of {a['n']} actions")
                worst = max(worst, 3 * a["inactive"] / a["n"] + 1)
            if a["posWrong"] > 0.2 * a["n"]:
                entry["findings"].append(f"{kind}: ends somewhere else than predicted in {a['posWrong']} of {a['n']} actions")
                worst = max(worst, 1)
        for kind in ("move", "wait", "defend"):
            a = acting[creature].get(kind)
            if a and key not in plain and (a["aScale"] + a["oReal"] > 0 or a["posWrong"] > 0):
                entry["findings"].append(f"{kind}: {a['posWrong']} position surprises, own health {-a['aReal']:+d}, others lose {a['oReal']} (n={a['n']})")
                worst = max(worst, 0.5)
        if effects.get(creature):
            entry["effects"] = dict(effects[creature])
            entry["findings"].append("applies effects " + ", ".join(f"{k} x{v}" for k, v in effects[creature].most_common()))
            worst = max(worst, 2)
        s = suffering.get(creature)
        if s and s["n"] >= args.min_samples:
            received = ratio(s["tReal"], s["tPred"])
            entry["defending"] = {"n": s["n"], "received": received, "backPred": s["backPred"], "backReal": s["backReal"]}
            if deviation(received) > args.tolerance:
                entry["findings"].append(f"when attacked: loses {fmt(received)}x the predicted health (n={s['n']})")
                worst = max(worst, min(9.0, deviation(received)))
            back_gap = abs(s["backReal"] - s["backPred"]) / max(1, s["backScale"])
            if s["backScale"] > 0 and back_gap > args.tolerance and abs(s["backReal"] - s["backPred"]) > 5 * s["n"]:
                entry["findings"].append(f"when attacked: its attackers lose {s['backReal']} health, predicted {s['backPred']} (n={s['n']})")
                worst = max(worst, 2 * back_gap)
        if idle.get(creature):
            gains = [d for d in idle[creature] if d > 0]
            losses = [d for d in idle[creature] if d < 0]
            if gains:
                entry["findings"].append(f"between actions: gains health {len(gains)} times (sum {sum(gains)})")
                worst = max(worst, 1.5)
            if losses and key not in plain:
                entry["findings"].append(f"between actions: loses health {len(losses)} times (sum {sum(losses)})")
                worst = max(worst, 1)
        entry["severity"] = round(worst, 2)
        report[creature] = entry
        rows.append((worst, creature, entry))
    (out / "unit_report.json").write_text(json.dumps(report, indent=1))
    rows.sort(key=lambda r: (-r[0], r[1]))
    clean = [c for w, c, e in rows if not e["findings"] and e["samples"] >= args.min_samples]
    thin = [c for w, c, e in rows if not e["findings"] and e["samples"] < args.min_samples]
    flagged = [r for r in rows if r[2]["findings"]]
    lines = ["# Unit lab: simulator against the real game", "",
             f"{len(rows)} creatures: {len(flagged)} with findings, {len(clean)} as predicted, {len(thin)} with too few actions "
             f"(tolerance {args.tolerance:.0%}, at least {args.min_samples} actions).", ""]
    for worst, creature, entry in flagged:
        lines.append(f"## {creature}  (severity {entry['severity']})")
        for finding in entry["findings"]:
            lines.append(f"- {finding}")
        lines.append("")
    lines += ["## As predicted", ", ".join(clean), "", "## Too few actions to judge", ", ".join(thin)]
    if formula:
        # FairAI's own damage formula (forecasts on the adventure map) against the game's estimate
        by_attacker = collections.defaultdict(list)
        for (attacker, defender, kind), ratios in formula.items():
            by_attacker[attacker].append((kind, defender, sum(ratios) / len(ratios), len(ratios)))
        lines += ["", "## Damage formula: pairs where FairAI's own formula differs from the game by more than 3 %", "",
                  "| attacker | kind | against | own / game | seen |", "|---|---|---|---|---|"]
        for attacker in sorted(by_attacker):
            for kind, defender, ratio, n in sorted(by_attacker[attacker]):
                lines.append(f"| {attacker} | {kind} | {defender} | {ratio:.2f} | {n} |")
    else:
        lines += ["", "## Damage formula", "No pair differed from the game by more than 3 % (or the run was made without the check)."]
    (out / "unit_report.md").write_text("\n".join(lines) + "\n")
    print(f"{len(rows)} creatures: {len(flagged)} with findings, {len(clean)} as predicted, {len(thin)} thin "
          f"(total severity {sum(r[0] for r in flagged):.1f}) -> {out / 'unit_report.md'}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("--client", required=True)
    g.add_argument("--mods", default="")
    g.add_argument("--work", default="unitlab-work")
    g.add_argument("--value", type=int, default=8000, help="AI value of each army")
    g.add_argument("--only", default="")
    g.add_argument("--force", action="store_true")
    p = sub.add_parser("play")
    p.add_argument("--client", required=True)
    p.add_argument("--mods", default="")
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--filter", default="")
    p.add_argument("--base-port", type=int, default=8000)
    p.add_argument("--rounds", type=int, default=2, help="how often every duel is played (different random seeds)")
    r = sub.add_parser("report")
    r.add_argument("--out", required=True)
    r.add_argument("--tolerance", type=float, default=0.15)
    r.add_argument("--min-samples", type=int, default=4)
    args = ap.parse_args()
    return {"gen": cmd_gen, "play": cmd_play, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
