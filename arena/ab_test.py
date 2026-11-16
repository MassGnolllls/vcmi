#!/usr/bin/env python3
"""A/B test of FairAI parameter sets on one build.

Every configuration plays exactly the same games (maps, sides, game seeds), so the comparison
can be paired: per game the score difference to the first configuration ("base"). Pairing
removes most of the map-to-map noise. Written for worker jobs, where all configurations of a
job must run on the same commit.

Usage:
  ab_test.py --client C --out DIR --jobs N --configs '{"base": {}, "x": {"safetyRatio": 1.2}}'
             [--seeds 42,43] [any further `run_tournament.py play` options, e.g. --max-days 45]

Output: DIR/<config>-s<seed>/ (run_tournament output per run) and DIR/ab_summary.md, updated
after every run. Score per game: 1 win, 0 loss, else the army share at the day limit.
"""
import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def game_scores(results_file: Path) -> dict:
    """match id -> score of the AI under test"""
    scores = {}
    if not results_file.exists():
        return scores
    for line in open(results_file):
        record = json.loads(line)
        if "score" in record:  # written by the runner (any number of players)
            if record["score"] is not None:
                scores[record["match"]] = record["score"]
            continue
        result = record.get("result") or {}
        players = result.get("players") or {}
        if len(players) != 2:
            continue
        me, other = ("blue", "red") if record["match"] % 2 else ("red", "blue")
        winner = result.get("winner") or ""
        if winner == me:
            score = 1.0
        elif winner == other:
            score = 0.0
        else:
            a = players[me].get("armyStrength", 0)
            b = players[other].get("armyStrength", 0)
            score = a / (a + b) if a + b > 0 else 0.5
        scores[record["match"]] = score
    return scores


def mean_ci(values):
    if not values:
        return float("nan"), float("nan")
    if len(values) == 1:
        return values[0], float("nan")
    return statistics.mean(values), 1.96 * statistics.stdev(values) / len(values) ** 0.5


def summarise(out: Path, configs: dict, seeds: list) -> str:
    names = list(configs)
    per_config = {}
    for name in names:
        games = {}
        for seed in seeds:
            for match, score in game_scores(out / f"{name}-s{seed}" / "results.jsonl").items():
                games[(seed, match)] = score
        per_config[name] = games
    base = names[0]
    lines = [f"# A/B test ({len(names)} configs, seeds {', '.join(map(str, seeds))})", "",
             f"Paired against `{base}`: mean score difference over the games both played (95% CI).", "",
             "| config | params | games | won | lost | score | diff vs base | pairs |",
             "|---|---|---|---|---|---|---|---|"]
    for name in names:
        games = per_config[name]
        scores = list(games.values())
        m, ci = mean_ci(scores)
        won = sum(1 for s in scores if s == 1.0)
        lost = sum(1 for s in scores if s == 0.0)
        if name == base:
            diff, pairs = "-", len(scores)
        else:
            common = sorted(set(games) & set(per_config[base]))
            d, dci = mean_ci([games[k] - per_config[base][k] for k in common])
            diff = f"{d:+.3f} ± {dci:.3f}" if common else "-"
            pairs = len(common)
        lines.append(f"| {name} | `{json.dumps(configs[name])}` | {len(scores)} | {won} | {lost} | "
                     f"{m:.3f} ± {ci:.3f} | {diff} | {pairs} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", default="2")
    ap.add_argument("--configs", required=True, help="JSON object: name -> FairAI parameter overrides")
    ap.add_argument("--seeds", default="42")
    ap.add_argument("--ai", default="FairAI")
    ap.add_argument("--opponent", default="Nullkiller2")
    ap.add_argument("--summary-only", action="store_true", help="only rewrite ab_summary.md from existing runs")
    ap.add_argument("--keep-existing", action="store_true", help="do not replay a config/seed that already has results (add configs to an earlier test)")
    args, play_args = ap.parse_known_args()
    configs = json.loads(args.configs)
    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if not args.summary_only:
        for seed in seeds:
            for name, params in configs.items():
                done = out / f"{name}-s{seed}" / "results.jsonl"
                if args.keep_existing and done.exists() and done.stat().st_size > 0:
                    print(f"keeping {name}-s{seed}", flush=True)
                    continue
                cmd = [sys.executable, str(HERE / "run_tournament.py"), "play", "--client", args.client,
                       "--ai", args.ai, "--opponent", args.opponent, "--jobs", str(args.jobs), "--seed", str(seed),
                       "--out", str(out / f"{name}-s{seed}"), *play_args]
                if params:
                    cmd += ["--ai-params", json.dumps(params)]
                print("$ " + " ".join(cmd), flush=True)
                subprocess.run(cmd, check=False)
                (out / "ab_summary.md").write_text(summarise(out, configs, seeds))
    summary = summarise(out, configs, seeds)
    (out / "ab_summary.md").write_text(summary)
    print(summary, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
