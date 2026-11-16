#!/usr/bin/env python3
"""Tests over a matrix of map kinds (size, water, players, teams, templates) instead of one map pool.

The goal of FairAI is to play any ordinary map well, so every measurement that matters runs over
several categories. A spec file lists them (see matrix_dev.json):

  matrix.py gen  --client CLIENT --spec matrix_dev.json            generate the map pools (idempotent)
  matrix.py play --client CLIENT --spec matrix_dev.json --out DIR --jobs 6
                 [--configs '{"base": {}, "x": {"param": 1}}'] [--seeds 42,43] [--only m-land-2p,...]

`play` runs a paired A/B test (ab_test.py) per category and writes matrix_summary.md: score per
category and configuration, and the paired difference to the first configuration over all categories
(categories weigh the same, whatever their number of games).

Fresh maps: `gen --seed-offset N` shifts all map seeds, `--pool-suffix X` puts them into separate pools -
use both for acceptance runs on maps the AI was never developed on.
"""
import argparse
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ab_test  # noqa: E402


def load_spec(path: str, suffix: str = ""):
    spec_path = Path(path)
    if not spec_path.exists():
        spec_path = HERE / path
    spec = json.loads(spec_path.read_text())
    for cat in spec["categories"]:
        cat["pool_dir"] = f"{spec['pool']}{suffix}-{cat['name']}"
    return spec


def cmd_gen(args) -> int:
    spec = load_spec(args.spec, args.pool_suffix)
    failed = 0
    for cat in spec["categories"]:
        if args.only and cat["name"] not in args.only.split(","):
            continue
        cmd = [sys.executable, str(HERE / "run_tournament.py"), "genmaps", "--client", args.client,
               "--count", str(cat.get("count", 6)), "--template", cat["template"], "--size", cat["size"],
               "--levels", str(cat.get("levels", 1)), "--water", cat.get("water", "none"),
               "--players", str(cat.get("players", 2)), "--seed", str(cat["seed"] + args.seed_offset),
               "--pool", cat["pool_dir"]]
        if cat.get("teams"):
            cmd += ["--teams", cat["teams"]]
        if cat.get("towns"):
            cmd += ["--towns", cat["towns"]]
        mods = cat.get("mods", spec.get("mods", ""))
        if mods:
            cmd += ["--mods", mods]
        res = subprocess.run(cmd, capture_output=True, text=True)
        last = (res.stdout.strip().splitlines() or ["?"])[-1]
        print(f"{cat['name']}: {last}", flush=True)
        if res.returncode != 0:
            failed += 1
            print(res.stdout[-1500:], res.stderr[-1500:])
    return 1 if failed else 0


def mean_ci(values):
    if not values:
        return float("nan"), float("nan")
    if len(values) == 1:
        return values[0], float("nan")
    return statistics.mean(values), 1.96 * statistics.stdev(values) / math.sqrt(len(values))


def summarise(out: Path, spec, configs: dict, seeds: list, only: str) -> str:
    names = list(configs)
    base = names[0]
    lines = [f"# Matrix test: {len(spec['categories'])} map categories, configs {', '.join(names)}, seeds {', '.join(map(str, seeds))}", "",
             "Score: 1 = win, 0 = loss, otherwise army share per seat at the day limit (0.5 = parity). ± = 95 % interval. "
             "Won and lost count the games decided before the day limit.", "",
             "| category | " + " | ".join(f"{n} (games, score, decided)" for n in names) + " | " +
             " | ".join(f"{n} − {base}" for n in names[1:]) + (" |" if len(names) > 1 else "")]
    lines.append("|---|" + "---|" * (len(names) + max(0, len(names) - 1)))
    per_cat_means = {n: [] for n in names}
    per_cat_diffs = {n: [] for n in names[1:]}
    all_diffs = {n: [] for n in names[1:]}
    for cat in spec["categories"]:
        if only and cat["name"] not in only.split(","):
            continue
        games = {}
        for name in names:
            games[name] = {}
            for seed in seeds:
                for match, score in ab_test.game_scores(out / cat["name"] / f"{name}-s{seed}" / "results.jsonl").items():
                    games[name][(seed, match)] = score
        cells = []
        for name in names:
            scores = list(games[name].values())
            m, ci = mean_ci(scores)
            wins = sum(1 for x in scores if x >= 0.999)
            losses = sum(1 for x in scores if x <= 0.001)
            cells.append(f"{len(scores)}, {m:.3f} ± {ci:.3f}, {wins} won {losses} lost" if scores else "-")
            if scores:
                per_cat_means[name].append(m)
        diffs = []
        for name in names[1:]:
            common = sorted(set(games[name]) & set(games[base]))
            d = [games[name][k] - games[base][k] for k in common]
            m, ci = mean_ci(d)
            diffs.append(f"{m:+.3f} ± {ci:.3f} ({len(d)})" if d else "-")
            if d:
                per_cat_diffs[name].append(m)
                all_diffs[name] += d
        lines.append(f"| {cat['name']} | " + " | ".join(cells + diffs) + " |")
    cells = [f"{statistics.mean(v):.3f}" if v else "-" for v in (per_cat_means[n] for n in names)]
    diffs = []
    for name in names[1:]:
        d = per_cat_diffs[name]
        pooled, pooled_ci = mean_ci(all_diffs[name])
        diffs.append(f"{statistics.mean(d):+.3f} (all games: {pooled:+.3f} ± {pooled_ci:.3f})" if d else "-")
    lines.append("| **mean of categories** | " + " | ".join(cells + diffs) + " |")
    return "\n".join(lines) + "\n"


def cmd_play(args) -> int:
    spec = load_spec(args.spec, args.pool_suffix)
    configs = json.loads(args.configs)
    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    port = args.base_port
    for cat in spec["categories"]:
        if args.only and cat["name"] not in args.only.split(","):
            continue
        if not args.summary_only:
            cmd = [sys.executable, str(HERE / "ab_test.py"), "--client", args.client, "--out", str(out / cat["name"]),
                   "--jobs", str(args.jobs), "--configs", json.dumps(configs), "--seeds", ",".join(map(str, seeds)),
                   "--ai", args.ai, "--opponent", args.opponent,
                   "--pool", cat["pool_dir"], "--max-days", str(cat.get("days", 45)),
                   "--timeout", str(cat.get("timeout", 1800)), "--stall-seconds", str(cat.get("stall", 180)),
                   "--base-port", str(port)]
            if args.max_maps or cat.get("max_maps"):
                cmd += ["--max-maps", str(args.max_maps or cat["max_maps"])]
            mods = cat.get("mods", spec.get("mods", ""))
            if mods:
                cmd += ["--mods", mods]
            if args.keep_logs:
                cmd += ["--keep-logs"]
            print(f"== {cat['name']}", flush=True)
            subprocess.run(cmd)
            port += 100
        (out / "matrix_summary.md").write_text(summarise(out, spec, configs, seeds, args.only))
    text = summarise(out, spec, configs, seeds, args.only)
    (out / "matrix_summary.md").write_text(text)
    print(text)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("--client", required=True)
    g.add_argument("--spec", default="matrix_dev.json")
    g.add_argument("--only", default="")
    g.add_argument("--seed-offset", type=int, default=0)
    g.add_argument("--pool-suffix", default="")
    p = sub.add_parser("play")
    p.add_argument("--client", required=True)
    p.add_argument("--spec", default="matrix_dev.json")
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", default="2")
    p.add_argument("--configs", default='{"base": {}}')
    p.add_argument("--seeds", default="42")
    p.add_argument("--only", default="")
    p.add_argument("--ai", default="FairAI")
    p.add_argument("--opponent", default="Nullkiller2")
    p.add_argument("--max-maps", type=int, default=0)
    p.add_argument("--pool-suffix", default="")
    p.add_argument("--base-port", type=int, default=7000)
    p.add_argument("--keep-logs", action="store_true")
    p.add_argument("--summary-only", action="store_true")
    args = ap.parse_args()
    return cmd_gen(args) if args.cmd == "gen" else cmd_play(args)


if __name__ == "__main__":
    sys.exit(main())
