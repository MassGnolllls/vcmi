#!/usr/bin/env python3
"""Main effects of switches from an A/B folder in which several switches were varied together.

A test of four switches one at a time compares 24 games against 24 for each of them. Varied
together in a (fractional) factorial design, every switch is on in half of the configurations
and off in the other half: its effect rests on all games of the test.

  effects.py FOLDER [FOLDER ...] --factors '{"N": {"noChase": 1}, "S": {"scouts": 2}}' [--within '{"leaderFocus": 1}']

FOLDER holds one sub-folder per configuration and seed (as written by ab_test.py / matrix.py) and
ab_summary.md, from which the parameters of each configuration are read. A switch counts as "on"
in a configuration that sets all of its parameters to the given values. --within keeps only
configurations that contain these parameters (leaves the plain baseline out of the design).

Per switch: mean score and town share with the switch on and off, their difference with a 95 %
interval - paired by game (map, seat and seed: the mean over the configurations with the switch
on minus the mean over those with it off) -, and wins and losses on either side.
"""
import argparse
import collections
import json
import math
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ab_test import game_scores, town_scores  # noqa: E402


def read_configs(folder: Path) -> dict:
    configs = {}
    summary = folder / "ab_summary.md"
    if not summary.exists():
        return configs
    for line in summary.read_text().splitlines():
        m = re.match(r"\|\s*([^|`]+?)\s*\|\s*`(\{.*?\})`\s*\|", line)
        if m:
            try:
                configs[m.group(1)] = json.loads(m.group(2))
            except ValueError:
                pass
    return configs


def outcomes(results_file: Path) -> dict:
    """match id -> 1 won, -1 lost, 0 undecided"""
    out = {}
    if not results_file.exists():
        return out
    for line in open(results_file):
        record = json.loads(line)
        result = record.get("result") or {}
        if not result.get("players") or result.get("outcome") == "stalled":
            continue
        mine = record.get("mine") or (["blue"] if record.get("match", 0) % 2 else ["red"])
        winner = result.get("winner") or ""
        out[record["match"]] = (1 if winner in mine else -1) if result.get("outcome") == "victory" and winner else 0
    return out


def mean_ci(values):
    if not values:
        return float("nan"), float("nan")
    if len(values) == 1:
        return values[0], float("nan")
    return statistics.mean(values), 1.96 * statistics.stdev(values) / math.sqrt(len(values))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--factors", required=True, help="JSON: switch name -> parameters that are set when it is on")
    ap.add_argument("--within", default="{}", help="JSON: parameters every configuration of the design contains")
    args = ap.parse_args()
    factors = json.loads(args.factors)
    within = json.loads(args.within)

    # game key (folder, seed, match) -> config -> (score, towns, outcome)
    games = collections.defaultdict(dict)
    design = {}
    for name in args.folders:
        folder = Path(name)
        configs = read_configs(folder)
        if not configs:
            print(f"no ab_summary.md with configurations in {folder}", file=sys.stderr)
            continue
        for config, params in configs.items():
            if any(params.get(k) != v for k, v in within.items()):
                continue
            design[config] = {f: all(params.get(k) == v for k, v in on.items()) for f, on in factors.items()}
            for sub in sorted(folder.glob(f"{config}-s*")):
                seed = sub.name.rsplit("-s", 1)[1]
                results = sub / "results.jsonl"
                score, towns, result = game_scores(results), town_scores(results), outcomes(results)
                for match, value in score.items():
                    games[(str(folder), seed, match)][config] = (value, towns.get(match, float("nan")), result.get(match, 0))
    if not design:
        print("no configurations in the design", file=sys.stderr)
        return 1

    played = collections.Counter(c for g in games.values() for c in g)
    print(f"design: {len(design)} configurations, {sum(played.values())} games")
    print()
    print("| config | " + " | ".join(factors) + " | games | score | towns | won | lost |")
    print("|---|" + "---|" * (len(factors) + 5))
    for config, on in design.items():
        rows = [g[config] for g in games.values() if config in g]
        if not rows:
            continue
        print(f"| {config} | " + " | ".join("on" if on[f] else "-" for f in factors) + f" | {len(rows)} | "
              f"{statistics.mean(r[0] for r in rows):.3f} | {statistics.mean(r[1] for r in rows):.3f} | "
              f"{sum(r[2] > 0 for r in rows)} | {sum(r[2] < 0 for r in rows)} |")
    print()
    print("| switch | games on/off | score on | score off | effect (95 %) | towns on | towns off | effect (95 %) | won on/off | lost on/off |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for factor in factors:
        on_rows, off_rows, score_diffs, town_diffs = [], [], [], []
        for game in games.values():
            on = [v for c, v in game.items() if design[c][factor]]
            off = [v for c, v in game.items() if not design[c][factor]]
            on_rows += on
            off_rows += off
            if on and off:
                score_diffs.append(statistics.mean(v[0] for v in on) - statistics.mean(v[0] for v in off))
                town_diffs.append(statistics.mean(v[1] for v in on) - statistics.mean(v[1] for v in off))
        if not on_rows or not off_rows:
            print(f"| {factor} | {len(on_rows)}/{len(off_rows)} | - | - | - | - | - | - | - | - |")
            continue
        s, s_ci = mean_ci(score_diffs)
        t, t_ci = mean_ci(town_diffs)
        print(f"| {factor} | {len(on_rows)}/{len(off_rows)} | {statistics.mean(v[0] for v in on_rows):.3f} | {statistics.mean(v[0] for v in off_rows):.3f} | "
              f"{s:+.3f} ± {s_ci:.3f} | {statistics.mean(v[1] for v in on_rows):.3f} | {statistics.mean(v[1] for v in off_rows):.3f} | {t:+.3f} ± {t_ci:.3f} | "
              f"{sum(v[2] > 0 for v in on_rows)}/{sum(v[2] > 0 for v in off_rows)} | {sum(v[2] < 0 for v in on_rows)}/{sum(v[2] < 0 for v in off_rows)} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
