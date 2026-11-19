#!/usr/bin/env python3
"""Main effects of switches on how a game DEVELOPS - not on who wins it.

Who wins a game is a coin with a bias: a switch worth five points of the win rate needs a thousand
games to show. What a side has on day 28 or 42 - guards beaten, mines, towns, land seen, the level of
its best hero, its army - is measured in every game, differs far less from game to game, and is what
the opening is about. This tool reads the day-by-day key figures the arena writes into every result
(timeline) and gives, for every switch of a (fractional) factorial test, the difference between the
configurations with the switch on and those with it off - paired by game (map, seat and seed).

  dev_effects.py FOLDER [FOLDER ...] --factors '{"B": {"breakout": 3}, ...}' [--days 14,28,42] [--within '{...}']

FOLDER as for effects.py: one sub-folder per configuration and seed, and ab_summary.md with the
parameters of each configuration. The figures are those of the side under test (FairAI); with
--opponent those of its opponent in the same games (what our switch does to the other side).

Columns: guards beaten, army value lost against guards, mines, share of the map seen (%), level of the
best hero, towns, income a day, army (heroes and garrisons, AI value), the strongest hero's army, the
army standing in garrisons. ± = 95 % over the games.
"""
import argparse
import collections
import json
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from effects import read_configs, mean_ci  # noqa: E402

# timeline row: [army, towns, heroes, income, mines, explored %, best hero level, gold, dwellings, guard fights, guard losses,
#                army of the strongest hero, army in garrisons]
FIGURES = [("guards", lambda r: r[9]), ("lost", lambda r: r[10]), ("mines", lambda r: r[4]), ("seen", lambda r: r[5]), ("level", lambda r: r[6]),
           ("towns", lambda r: r[1]), ("income", lambda r: r[3]), ("army", lambda r: r[0] + r[12]), ("main", lambda r: r[11]), ("garrisons", lambda r: r[12])]
BIG = {"lost", "income", "army", "main", "garrisons"}


def timelines(results_file: Path, opponent: bool) -> dict:
    """match id -> {day: timeline row of the side under test (or of its first opponent)}"""
    out = {}
    if not results_file.exists():
        return out
    for line in open(results_file):
        record = json.loads(line)
        result = record.get("result") or {}
        if not result.get("timeline"):
            continue
        mine = record.get("mine") or (["blue"] if record.get("match", 0) % 2 else ["red"])
        days = {}
        for entry in result["timeline"]:
            players = entry.get("players") or {}
            if opponent:
                others = [c for c in players if c not in mine]
                if others:
                    days[entry["day"]] = players[others[0]]
            elif mine[0] in players:
                days[entry["day"]] = players[mine[0]]
        out[record["match"]] = days
    return out


def show(name: str, value: float, ci: float = None) -> str:
    if name in BIG:
        return f"{value / 1000:+.1f}k ± {ci / 1000:.1f}k" if ci is not None else f"{value / 1000:.1f}k"
    return f"{value:+.2f} ± {ci:.2f}" if ci is not None else f"{value:.2f}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--factors", required=True, help="JSON: switch name -> parameters that are set when it is on")
    ap.add_argument("--within", default="{}", help="JSON: parameters every configuration of the design contains")
    ap.add_argument("--days", default="14,28,42")
    ap.add_argument("--opponent", action="store_true", help="the opponent's figures instead of ours")
    args = ap.parse_args()
    factors = json.loads(args.factors)
    within = json.loads(args.within)
    days = [int(d) for d in args.days.split(",")]

    games = collections.defaultdict(dict)  # (folder, seed, match) -> config -> {day: row}
    design = {}
    for name in args.folders:
        folder = Path(name)
        configs = read_configs(folder)
        if not configs:
            # (a test that is still running has no summary yet: the sub-folders name the configurations)
            print(f"no ab_summary.md with configurations in {folder}", file=sys.stderr)
            continue
        for config, params in configs.items():
            if any(params.get(k) != v for k, v in within.items()):
                continue
            design[config] = {f: all(params.get(k) == v for k, v in on.items()) for f, on in factors.items()}
            for sub in sorted(folder.glob(f"{config}-s*")):
                seed = sub.name.rsplit("-s", 1)[1]
                for match, rows in timelines(sub / "results.jsonl", args.opponent).items():
                    games[(str(folder), seed, match)][config] = rows
    if not design:
        print("no configurations in the design", file=sys.stderr)
        return 1
    print(f"design: {len(design)} configurations, {sum(len(g) for g in games.values())} games on {len(games)} seats" + (" - the OPPONENT's figures" if args.opponent else ""))
    for day in days:
        print()
        print(f"day {day}")
        print("| switch | " + " | ".join(name for name, _ in FIGURES) + " |")
        print("|---|" + "---|" * len(FIGURES))
        every = [rows[day] for g in games.values() for rows in g.values() if day in rows]
        if not every:
            print("| (no game reached this day) |")
            continue
        print(f"| mean of all ({len(every)}) | " + " | ".join(show(name, statistics.mean(pick(r) for r in every)) for name, pick in FIGURES) + " |")
        for factor in factors:
            cells = []
            for name, pick in FIGURES:
                diffs = []
                for game in games.values():
                    on = [pick(rows[day]) for c, rows in game.items() if design[c][factor] and day in rows]
                    off = [pick(rows[day]) for c, rows in game.items() if not design[c][factor] and day in rows]
                    if on and off:
                        diffs.append(statistics.mean(on) - statistics.mean(off))
                value, ci = mean_ci(diffs)
                cells.append("-" if math.isnan(value) else show(name, value, 0.0 if math.isnan(ci) else ci))
            print(f"| {factor} | " + " | ".join(cells) + " |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
