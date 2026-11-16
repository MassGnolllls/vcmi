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
    env = rt.client_env(config_home)
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
                    {"hero": HEROES[0], "primary": [1, 1, 1, 1], "army": [[creature["id"], count]], "spellBook": False, "spells": []},
                    {"hero": HEROES[1], "primary": [1, 1, 1, 1], "army": [[ref_id, ref_count]], "spellBook": False, "spells": []}]}
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
                if at >= 0:
                    lines.append(path.stem + "|" + line[at + 7:].strip())
        shutil.rmtree(out / f"logs_{index:04d}", ignore_errors=True)
        (out / f"match_{index:04d}.json").unlink(missing_ok=True)
        return path.stem, record.get("error"), lines

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(one, i, p) for i, p in enumerate(maps)]
        for fut in as_completed(futures):
            stem, error, lines = fut.result()
            done += 1
            if error:
                print(f"{stem}: {error}", flush=True)
            for line in lines:
                lines_file.write(line + "\n")
            lines_file.flush()
            if done % 50 == 0:
                print(f"{done}/{len(maps)} duels", flush=True)
    print(f"{done} duels played, lines in {out / 'simcheck.txt'}")
    return 0


def cmd_report(args) -> int:
    out = Path(args.out).resolve()
    acting = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.Counter()))
    suffering = collections.defaultdict(lambda: collections.Counter())
    spells_given = collections.defaultdict(collections.Counter)
    idle = collections.defaultdict(list)
    for line in (out / "simcheck.txt").open():
        parts = line.rstrip("\n").split("|")
        if len(parts) >= 6 and parts[1] == "simidle":
            idle[parts[2]].append((int(parts[3]), int(parts[5]) - int(parts[4])))
            continue
        if len(parts) < 17 or parts[1] != "simcheck":
            continue
        (_, _, kind, actor, actor_count, target, _, t_pred, t_real, a_pred, a_real, o_pred, o_real, t_spells, a_spells, inactive, pos_ok) = parts[:17]
        t_pred, t_real, a_pred, a_real, o_pred, o_real = map(int, (t_pred, t_real, a_pred, a_real, o_pred, o_real))
        a = acting[actor][kind]
        a["n"] += 1
        a["tPred"] += t_pred
        a["tReal"] += t_real
        a["aPred"] += a_pred
        a["aRealDamage"] += max(0, a_real)
        a["aHealed"] += max(0, -a_real)
        a["healEvents"] += 1 if a_real < 0 else 0
        a["oPred"] += o_pred
        a["oReal"] += o_real
        a["inactive"] += int(inactive)
        a["posWrong"] += 0 if pos_ok == "1" else 1
        if t_spells != "-":
            for spell in t_spells.split(","):
                spells_given[actor][spell] += 1
        if a_spells != "-":
            for spell in a_spells.split(","):
                spells_given[actor]["self:" + spell] += 1
        if kind in ("attack", "shoot") and target != "-":
            s = suffering[target]
            s["n"] += 1
            s["tPred"] += t_pred
            s["tReal"] += t_real
            s["attackerPred"] += a_pred
            s["attackerReal"] += max(0, a_real)

    def ratio(real, pred):
        if pred <= 0:
            return None if real <= 0 else float("inf")
        return real / pred

    def fmt(r):
        return "-" if r is None else ("inf" if r == float("inf") else f"{r:.2f}")

    report = {}
    rows = []
    for creature in sorted(set(acting) | set(suffering) | set(idle)):
        entry = {"findings": []}
        worst = 0.0
        for kind in ("attack", "shoot"):
            a = acting[creature].get(kind)
            if not a or a["n"] < args.min_samples:
                continue
            dealt = ratio(a["tReal"], a["tPred"])
            taken = ratio(a["aRealDamage"], a["aPred"])
            entry[kind] = {"n": a["n"], "dealt": dealt, "taken": taken, "healed": a["aHealed"], "othersPred": a["oPred"],
                           "othersReal": a["oReal"], "inactive": a["inactive"], "posWrong": a["posWrong"]}
            if dealt is not None and abs(dealt - 1) > args.tolerance:
                entry["findings"].append(f"{kind}: deals {fmt(dealt)}x the predicted damage")
                worst = max(worst, abs(dealt - 1) if dealt != float("inf") else 9)
            if taken is not None and abs(taken - 1) > args.tolerance and (a["aPred"] + a["aRealDamage"]) > 0:
                entry["findings"].append(f"{kind}: takes {fmt(taken)}x the predicted damage in return")
                worst = max(worst, min(9, abs(taken - 1)) if taken != float("inf") else 9)
            if a["healEvents"] > 0:
                entry["findings"].append(f"{kind}: heals itself ({a['aHealed']} HP in {a['healEvents']} of {a['n']} actions)")
                worst = max(worst, 2)
            if a["oReal"] > 1.5 * a["oPred"] + 10:
                entry["findings"].append(f"{kind}: hits other units too ({a['oReal']} HP, predicted {a['oPred']})")
                worst = max(worst, 2)
            if a["inactive"]:
                entry["findings"].append(f"{kind}: put its target out of action in {a['inactive']} of {a['n']} actions")
                worst = max(worst, 3)
            if a["posWrong"] > 0.2 * a["n"]:
                entry["findings"].append(f"{kind}: ends somewhere else than predicted in {a['posWrong']} of {a['n']} actions")
                worst = max(worst, 1)
        for kind in ("move", "wait", "defend"):
            a = acting[creature].get(kind)
            if a and (a["aRealDamage"] + a["aHealed"] + a["oReal"] > 0 or a["posWrong"] > 0):
                entry["findings"].append(f"{kind}: {a['posWrong']} position surprises, own HP change {a['aRealDamage']}/-{a['aHealed']}, others {a['oReal']} (n={a['n']})")
                worst = max(worst, 0.5)
        if spells_given.get(creature):
            entry["effects"] = dict(spells_given[creature])
            entry["findings"].append("applies effects: " + ", ".join(f"{k} x{v}" for k, v in spells_given[creature].most_common()))
            worst = max(worst, 2)
        s = suffering.get(creature)
        if s and s["n"] >= args.min_samples:
            received = ratio(s["tReal"], s["tPred"])
            returned = ratio(s["attackerReal"], s["attackerPred"])
            entry["defending"] = {"n": s["n"], "received": received, "returned": returned}
            if received is not None and abs(received - 1) > args.tolerance:
                entry["findings"].append(f"when attacked: receives {fmt(received)}x the predicted damage")
                worst = max(worst, min(9, abs(received - 1)))
            if returned is not None and abs(returned - 1) > args.tolerance and (s["attackerPred"] + s["attackerReal"]) > 0:
                entry["findings"].append(f"when attacked: attacker suffers {fmt(returned)}x the predicted damage")
                worst = max(worst, min(9, abs(returned - 1)) if returned != float("inf") else 9)
        if idle.get(creature):
            gains = [d for _, d in idle[creature] if d > 0]
            losses = [d for _, d in idle[creature] if d < 0]
            if gains:
                entry["findings"].append(f"between actions: gains health {len(gains)} times (sum {sum(gains)})")
                worst = max(worst, 2)
            if losses:
                entry["findings"].append(f"between actions: loses health {len(losses)} times (sum {sum(losses)})")
                worst = max(worst, 1)
        entry["severity"] = round(worst, 2)
        report[creature] = entry
        rows.append((worst, creature, entry))
    (out / "unit_report.json").write_text(json.dumps(report, indent=1))
    rows.sort(key=lambda r: (-r[0], r[1]))
    clean = [c for w, c, e in rows if not e["findings"]]
    lines = ["# Unit lab: simulator against the real game", "",
             f"{len(rows)} creatures seen, {len(rows) - len(clean)} with findings, {len(clean)} match the simulator "
             f"(tolerance {args.tolerance:.0%}, at least {args.min_samples} actions).", ""]
    for worst, creature, entry in rows:
        if not entry["findings"]:
            continue
        lines.append(f"## {creature}  (severity {entry['severity']})")
        for finding in entry["findings"]:
            lines.append(f"- {finding}")
        lines.append("")
    lines.append("## As predicted")
    lines.append(", ".join(clean))
    (out / "unit_report.md").write_text("\n".join(lines) + "\n")
    print(f"{len(rows)} creatures, {len(rows) - len(clean)} with findings -> {out / 'unit_report.md'}")
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
    r = sub.add_parser("report")
    r.add_argument("--out", required=True)
    r.add_argument("--tolerance", type=float, default=0.15)
    r.add_argument("--min-samples", type=int, default=4)
    args = ap.parse_args()
    return {"gen": cmd_gen, "play": cmd_play, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
