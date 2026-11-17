#!/usr/bin/env python3
"""Guard benchmark: how much of its army does a combat AI lose against map guards?

Most battles of a game are fought against neutral guards, and every creature lost there is missing
later. This benchmark lets a hero with a plausible army attack a guard stack of every creature of
the game (with mods: HotA etc.) at several strength ratios, in real battles: the game splits the
guards by its own rules and plays them with its neutral combat AI.

  guard_bench.py gen    --client CLIENT [--mods hota] [--work DIR] [--ratios 0.3,0.5,0.8] [--armies 2]
  guard_bench.py play   --client CLIENT --ai FairAI [--mods hota] --out DIR [--jobs 2] [--ai-params JSON]
  guard_bench.py report --out DIR [--against OTHER_DIR]

Numbers: loss = share of the hero's army value that died (1.0 when the battle was lost).
"""
import argparse
import collections
import json
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

SUBDIR = "guardbench"
# which armies meet which guards: weak guards are fought early, dragons late
STAGE_BY_LEVEL = {1: ["early"], 2: ["early"], 3: ["early", "mid"], 4: ["mid"], 5: ["mid", "late"], 6: ["late"], 7: ["late"]}


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
        town_creatures, heroes, _ = battle_scenarios.load_library(str(library))
        value_of = {c["id"]: c["aiValue"] for c in lib["creatures"]}
        factions = sorted({c["faction"] for c in town_creatures})
        might = [h for h in heroes if not h["magicClass"]]
        # everything that can stand on a map as a guard; war machines and creatures without a level are left out
        guards = [c for c in lib["creatures"] if c["aiValue"] > 0 and c["hp"] > 0 and c["speed"] > 0 and 1 <= c["level"] <= 7]
        if args.only:
            guards = [c for c in guards if c["id"] in args.only.split(",")]
        ratios = [float(r) for r in args.ratios.split(",")]
        maps_dir = rt.vcmi_data_dir() / "Maps" / SUBDIR
        if args.force and maps_dir.exists():
            shutil.rmtree(maps_dir)
        maps_dir.mkdir(parents=True, exist_ok=True)
        rng = random.Random(args.seed)
        # --primary 4,3,8,6 for every hero, or early=1,1,3,2;mid=3,3,6,5;late=6,6,10,8 by the stage of its army
        primary_by_stage = {}
        for part in filter(None, args.primary.split(";")):
            stage_name, _, numbers = part.rpartition("=")
            primary_by_stage[stage_name] = [int(x) for x in numbers.split(",")]
        index = []
        made = failed = 0
        for guard in guards:
            stages = STAGE_BY_LEVEL.get(max(1, min(7, guard["level"])), ["mid"])
            for ratio in ratios:
                for copy in range(args.armies):
                    stage = stages[copy % len(stages)]
                    faction = rng.choice(factions)
                    army = battle_scenarios.faction_army(rng, town_creatures, faction, stage)
                    army_value = battle_scenarios.army_value(army, value_of)
                    count = max(1, round(ratio * army_value / guard["aiValue"]))
                    real_ratio = count * guard["aiValue"] / army_value
                    if abs(real_ratio - ratio) > 0.35 * ratio:
                        # a single dragon is already too much for this army: make the army fit the guard
                        army = battle_scenarios.scale_army(army, real_ratio / ratio)
                        army_value = battle_scenarios.army_value(army, value_of)
                    hero = rng.choice([h for h in might if h["faction"] == faction] or might)["id"]
                    name = f"{safe(guard['id'])}__r{int(ratio * 100):03d}__{copy}{args.tag}"
                    # --spells: the hero carries a spell book with these spells (and --primary: its skills),
                    # to see what the hero's magic does to a guard fight and how well that is forecast
                    spell_list = [x for x in args.spells.split(",") if x]
                    primary = primary_by_stage.get(stage) or primary_by_stage.get("") or battle_scenarios.STAGES[stage]["primary"]
                    scenario = {"name": name, "terrain": rng.choice(battle_scenarios.TERRAINS), "kind": "guard", "stage": stage, "magic": bool(spell_list),
                                "sides": [{"hero": hero, "primary": primary, "army": army, "spellBook": bool(spell_list), "spells": spell_list},
                                          {"guard": [guard["id"], count]}]}
                    scen_file = work / "scenarios" / f"{name}.json"
                    scen_file.parent.mkdir(parents=True, exist_ok=True)
                    scen_file.write_text(json.dumps(scenario, indent=1))
                    out = maps_dir / f"{name}.vmap"
                    res = subprocess.run([args.client, "--headless", "--arena-genbattle", str(scen_file), "--arena-out", str(out)],
                                         capture_output=True, text=True, timeout=300, env=env)
                    if res.returncode == 0 and out.exists():
                        made += 1
                        index.append({"name": name, "guard": guard["id"], "level": guard["level"], "count": count, "ratio": ratio,
                                      "stage": stage, "faction": faction, "armyValue": army_value})
                    else:
                        failed += 1
                        print(f"FAILED {name}: {res.stderr.strip()[-300:]}")
        # sets made earlier (other --tag) stay in the index
        index_file = maps_dir / "index.json"
        if index_file.exists():
            fresh = {e["name"] for e in index}
            index = [e for e in json.loads(index_file.read_text()) if e["name"] not in fresh] + index
        index_file.write_text(json.dumps(index, indent=1))
        print(f"{made} guard battles ready in {maps_dir} ({failed} failed, {len(guards)} creatures)")
        return 0 if made else 1
    finally:
        shutil.rmtree(config_home, ignore_errors=True)


def cmd_play(args) -> int:
    maps_dir = rt.vcmi_data_dir() / "Maps" / SUBDIR
    maps = sorted(p for p in maps_dir.glob("*.vmap") if args.filter in p.name)
    if args.max_maps:
        maps = maps[:: max(1, len(maps) // args.max_maps)][: args.max_maps]
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    play_args = SimpleNamespace(client=args.client, base_port=args.base_port, timeout=300, stall_seconds=60, keep_logs=True,
                                mods=args.mods, max_days=3, difficulty=4, ai_params=args.ai_params, battle_params=args.ai_params)
    done = errors = 0

    def one(index, path):
        rec = rt.play_match(play_args, index, f"MAPS/{SUBDIR.upper()}/{path.stem.upper()}", args.ai, "BattleAI", args.seed + index, out, True)
        log = out / f"logs_{index:04d}" / "VCMI_Client_log.txt"
        if log.exists():
            for line in log.open(errors="replace"):
                at = line.find("FairAI forecast|")
                if at >= 0:  # the simulator's forecast of this battle (see FairBattleAI::logForecast)
                    rec["forecast"] = [float(x) for x in line[at:].strip().split("|")[1:]]
                    break
        if not args.keep_logs and not rec.get("error"):
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
    print(f"{done} battles played ({errors} failed), results in {out / 'results.jsonl'}")
    return cmd_report(SimpleNamespace(out=str(out), against="", top=15))


def forecast_report(folder: Path):
    """How well did the simulator forecast these battles? Lines for the report."""
    rows = []
    for line in (folder / "results.jsonl").open():
        rec = json.loads(line)
        battle = (rec.get("result") or {}).get("battle")
        f = rec.get("forecast")
        if not battle or not f or len(f) < 8:
            continue
        hero = next((s for s in battle["sides"] if s["player"] != "neutral"), None)
        if not hero or not hero["initialStrength"] or f[0] <= 0:
            continue
        won = rec["result"].get("winner") == hero["player"]
        actual = min(1.0, hero["lostStrength"] / hero["initialStrength"]) if won else 1.0
        def loss(own, enemy, before):
            # lost, won, or not finished in time (then: what is left after trading the rest)
            if own <= 0 or before <= 0:
                return 1.0
            if enemy <= 0:
                return max(0.0, 1.0 - own / before)
            return 1.0 - (own - enemy) / before if own > enemy else 1.0

        plans = [loss(f[2], f[3], f[0]), loss(f[5], f[6], f[0])]
        # the same battle forecast from the armies alone, as the adventure AI sees it beforehand
        from_map = min(loss(f[10], f[11], f[8]), loss(f[12], f[13], f[8])) if len(f) >= 14 else None
        # ... and with the hero's damage spells played (parameter spellForecast): the better of all four plans
        with_spells = min(from_map, loss(f[14], f[15], f[8]), loss(f[17], f[18], f[8])) if len(f) >= 20 else None
        rows.append((actual, min(plans), plans[0], plans[1], from_map, with_spells))
    if len(rows) < 5:
        return []
    n = len(rows)
    lines = ["", "## Forecast of the simulator (before the first move) against the real result", "",
             "| forecast | battles | mean forecast loss | mean real loss | mean error | mean absolute error |", "|---|---|---|---|---|---|"]
    for label, pick in (("better of both plans", 1), ("hold", 2), ("advance", 3), ("from the armies alone (as on the map)", 4),
                        ("from the armies and the hero's damage spells", 5)):
        chosen = [r for r in rows if r[pick] is not None]
        if not chosen:
            continue
        pred = [r[pick] for r in chosen]
        real = [r[0] for r in chosen]
        wrong = sum(1 for p, a in zip(pred, real) if (p >= 1.0) != (a >= 1.0))
        lines.append(f"| {label} | {len(chosen)} | {mean(pred):.3f} | {mean(real):.3f} | {mean(p - a for p, a in zip(pred, real)):+.3f} | "
                     f"{mean(abs(p - a) for p, a in zip(pred, real)):.3f} (winner wrong in {wrong}) |")
    mp, mr = mean(r[1] for r in rows), mean(r[0] for r in rows)
    cov = sum((r[1] - mp) * (r[0] - mr) for r in rows)
    var_p = sum((r[1] - mp) ** 2 for r in rows)
    var_r = sum((r[0] - mr) ** 2 for r in rows)
    if var_p > 0 and var_r > 0:
        lines.append("")
        lines.append(f"Correlation of forecast and real loss: {cov / (var_p * var_r) ** 0.5:.2f}.")
    for title, pick in (("forecast loss", 1), ("forecast loss from the armies alone", 4), ("forecast loss from the armies and the hero's damage spells", 5)):
        chosen = [r for r in rows if r[pick] is not None]
        if not chosen:
            continue
        lines += ["", f"| {title} | battles | mean real loss | lost battles |", "|---|---|---|---|"]
        for lo, hi in ((0, 0.02), (0.02, 0.05), (0.05, 0.1), (0.1, 0.2), (0.2, 0.4), (0.4, 0.999), (0.999, 1.01)):
            group = [r for r in chosen if lo <= r[pick] < hi]
            if group:
                lines.append(f"| {lo:.2f} - {min(hi, 1.0):.2f} | {len(group)} | {mean(r[0] for r in group):.3f} | {sum(1 for r in group if r[0] >= 1.0)} |")
    return lines


def load_results(folder: Path):
    """scenario name -> (loss share, won)"""
    result = {}
    for line in (folder / "results.jsonl").open():
        rec = json.loads(line)
        battle = (rec.get("result") or {}).get("battle")
        if not battle:
            continue
        hero = next((s for s in battle["sides"] if s["player"] != "neutral"), None)
        if not hero or not hero["initialStrength"]:
            continue
        won = rec["result"].get("winner") == hero["player"]
        loss = min(1.0, hero["lostStrength"] / hero["initialStrength"]) if won else 1.0
        result[rec["map"].rsplit("/", 1)[-1].upper()] = (loss, won)
    return result


def mean(values):
    values = list(values)
    return sum(values) / len(values) if values else 0.0


def interval(values):
    values = list(values)
    if len(values) < 2:
        return 0.0
    m = mean(values)
    sd = (sum((v - m) ** 2 for v in values) / (len(values) - 1)) ** 0.5
    return 1.96 * sd / len(values) ** 0.5


def cmd_report(args) -> int:
    out = Path(args.out).resolve()
    index = {e["name"].upper(): e for e in json.loads((rt.vcmi_data_dir() / "Maps" / SUBDIR / "index.json").read_text())}
    mine = load_results(out)
    other = load_results(Path(args.against).resolve()) if args.against else None
    names = sorted(n for n in mine if n in index and (other is None or n in other))
    if not names:
        print("no results")
        return 1
    lines = [f"# Guard benchmark: {out.name}" + (f" against {Path(args.against).name}" if other else ""), "",
             f"{len(names)} battles. Loss = share of the hero's army value that died (lost battle = 1.0); ± = 95 %.", ""]
    header = "| Guards / own army | battles | won | loss |" + (" other: won | other: loss | difference |" if other else "")
    lines += [header, "|---|---|---|---|" + ("---|---|---|" if other else "")]

    def row(label, group):
        text = f"| {label} | {len(group)} | {mean(mine[n][1] for n in group):.0%} | {mean(mine[n][0] for n in group):.3f} ± {interval(mine[n][0] for n in group):.3f} |"
        if other:
            diffs = [mine[n][0] - other[n][0] for n in group]
            text += f" {mean(other[n][1] for n in group):.0%} | {mean(other[n][0] for n in group):.3f} | {mean(diffs):+.3f} ± {interval(diffs):.3f} |"
        return text

    for ratio in sorted({index[n]["ratio"] for n in names}):
        lines.append(row(f"{ratio:.2f}", [n for n in names if index[n]["ratio"] == ratio]))
    lines.append(row("all", names))
    lines += ["", "## By guard level", "", header, "|---|---|---|---|" + ("---|---|---|" if other else "")]
    for level in sorted({index[n]["level"] for n in names}):
        lines.append(row(f"level {level}", [n for n in names if index[n]["level"] == level]))

    by_guard = collections.defaultdict(list)
    for n in names:
        by_guard[index[n]["guard"]].append(n)
    table = []
    for guard, group in by_guard.items():
        loss = mean(mine[n][0] for n in group)
        diff = mean(mine[n][0] - other[n][0] for n in group) if other else 0.0
        table.append((guard, len(group), mean(mine[n][1] for n in group), loss, diff))
    (out / "guard_report.json").write_text(json.dumps({g: {"battles": c, "won": w, "loss": l, "diff": d} for g, c, w, l, d in table}, indent=1))
    lines += ["", f"## Guards that cost the most (top {args.top})", "", "| Guard | battles | won | loss |" + (" difference |" if other else ""), "|---|---|---|---|" + ("---|" if other else "")]
    for guard, count, won, loss, diff in sorted(table, key=lambda t: -t[3])[: args.top]:
        lines.append(f"| {guard} | {count} | {won:.0%} | {loss:.3f} |" + (f" {diff:+.3f} |" if other else ""))
    if other:
        lines += ["", f"## Biggest differences to {Path(args.against).name}", "", "| Guard | battles | loss | difference |", "|---|---|---|---|"]
        ranked = sorted(table, key=lambda t: t[4])
        for guard, count, won, loss, diff in ranked[: args.top] + ranked[-args.top:]:
            lines.append(f"| {guard} | {count} | {loss:.3f} | {diff:+.3f} |")
    lines += forecast_report(out)
    text = "\n".join(lines) + "\n"
    (out / ("guard_report.md" if not other else f"guard_vs_{Path(args.against).name}.md")).write_text(text)
    print("\n".join(lines[: 22 + (0 if not other else 0)]))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("--client", required=True)
    g.add_argument("--mods", default="")
    g.add_argument("--work", default="guardbench-work")
    g.add_argument("--ratios", default="0.3,0.5,0.8", help="guard strength as a share of the hero's army (AI value)")
    g.add_argument("--armies", type=int, default=2, help="different armies per guard and ratio")
    g.add_argument("--seed", type=int, default=5)
    g.add_argument("--only", default="")
    g.add_argument("--spells", default="", help="spells in the hero's book, e.g. core:magicArrow,core:lightningBolt (default: no book)")
    g.add_argument("--primary", default="", help="the hero's attack,defence,power,knowledge, or by stage: early=1,1,3,2;mid=3,3,6,5;late=6,6,10,8 (default: the stage's might hero)")
    g.add_argument("--tag", default="", help="suffix of the map names, to keep several sets side by side (play --filter)")
    g.add_argument("--force", action="store_true")
    p = sub.add_parser("play")
    p.add_argument("--client", required=True)
    p.add_argument("--ai", required=True, help="combat AI of the hero, e.g. FairAI or BattleAI")
    p.add_argument("--mods", default="")
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--filter", default="")
    p.add_argument("--max-maps", type=int, default=0)
    p.add_argument("--seed", type=int, default=9000)
    p.add_argument("--base-port", type=int, default=7000)
    p.add_argument("--ai-params", default="", help="FairAI parameter overrides (JSON) for the hero's side")
    p.add_argument("--keep-logs", action="store_true")
    r = sub.add_parser("report")
    r.add_argument("--out", required=True)
    r.add_argument("--against", default="", help="results of another run on the same battles: paired comparison")
    r.add_argument("--top", type=int, default=15)
    args = ap.parse_args()
    return {"gen": cmd_gen, "play": cmd_play, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
