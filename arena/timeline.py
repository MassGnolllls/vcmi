#!/usr/bin/env python3
"""How a game developed, day by day: where does one AI fall behind the other?

Arena results carry a timeline (key figures of every player per day) and battle tallies.
  timeline.py RESULT.json [--every 7]              one game: a table per player
  timeline.py RESULTS.jsonl [--every 7] [--mine]   many games: averages for the AI under test and the opponent
"""
import argparse
import collections
import json
import sys
from pathlib import Path

# mainArmy = army of the strongest hero, garrisons = army in towns without a hero (clients built after 16.11.2026 23:35)
COLUMNS = ["army", "towns", "heroes", "income", "mines", "explored%", "level", "gold", "dwellings", "guardFights", "guardLosses", "mainArmy", "garrisons"]


def load(path: Path):
    text = path.read_text()
    if path.suffix == ".jsonl":
        return [json.loads(line) for line in text.splitlines() if line.strip()]
    data = json.loads(text)
    return [{"result": data, "mine": []}] if "players" in data else [data]


def fmt(value: float) -> str:
    if abs(value) >= 10000:
        return f"{value / 1000:.0f}k"
    if abs(value) >= 100 or float(value).is_integer():
        return f"{value:.0f}"
    return f"{value:.1f}"


def compare(folder: Path, days) -> int:
    """A/B folders (one sub-folder per configuration and seed): key figures of the AI under test side by side"""
    runs = collections.defaultdict(list)
    for results in sorted(folder.glob("*/results.jsonl")):
        runs[results.parent.name.rsplit("-s", 1)[0]] += [r for r in load(results) if (r.get("result") or {}).get("timeline")]
    if not runs:
        print("no results with a timeline below", folder)
        return 1
    shown = ["army", "towns", "heroes", "income", "mines", "explored%", "level", "guardFights", "guardLosses"]
    print("| config | games | day | " + " | ".join(shown) + " | opp.: army | towns | heroes | income | mines | explored% | level | fights |")
    print("|---|---|---|" + "---|" * (len(shown) + 8))
    for name, records in runs.items():
        for day in days:
            mine = collections.defaultdict(list)
            other = collections.defaultdict(list)
            for rec in records:
                result = rec["result"]
                rows = {d["day"]: d for d in result["timeline"]}
                entry = rows.get(day) or rows[max(rows)]
                for colour in result.get("players") or {}:
                    row = (entry.get("players") or {}).get(colour) or [0] * len(COLUMNS)
                    target = mine if colour in (rec.get("mine") or []) else other
                    for column, value in zip(COLUMNS, row):
                        target[column].append(value)
            avg = lambda d, c: sum(d[c]) / max(1, len(d[c]))
            print(f"| {name} | {len(records)} | {day} | " + " | ".join(fmt(avg(mine, c)) for c in shown)
                  + " | " + " | ".join(fmt(avg(other, c)) for c in ("army", "towns", "heroes", "income", "mines", "explored%", "level", "guardFights")) + " |")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="result.json, results.jsonl, or with --compare the folder of an A/B test")
    ap.add_argument("--every", type=int, default=7)
    ap.add_argument("--compare", default="", help="days to show per configuration, e.g. 8,15,22,43")
    args = ap.parse_args()
    if args.compare:
        return compare(Path(args.file), [int(d) for d in args.compare.split(",")])
    records = [r for r in load(Path(args.file)) if (r.get("result") or {}).get("timeline")]
    if not records:
        print("no timeline in these results (needs a client built after 16.11.2026)")
        return 1

    single = len(records) == 1 and not records[0].get("mine")
    # group -> day -> column -> values
    table = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(list)))
    tallies = collections.defaultdict(lambda: collections.defaultdict(list))
    for rec in records:
        result = rec["result"]
        mine = set(rec.get("mine") or [])
        players = result.get("players") or {}

        def group(colour):
            if single:
                return f"{colour} ({players.get(colour, {}).get('ai', '?')})"
            return "AI under test" if colour in mine else "opponent"

        seen = collections.defaultdict(set)
        for day in result["timeline"]:
            for colour, row in day.get("players", {}).items():
                for name, value in zip(COLUMNS, row):
                    table[group(colour)][day["day"]][name].append(value)
                seen[day["day"]].add(colour)
            # players already out of the game count as zero (otherwise the survivors flatter the average)
            for colour in players:
                if colour not in seen[day["day"]]:
                    for name in COLUMNS:
                        table[group(colour)][day["day"]][name].append(0)
        for colour, tally in (result.get("battles") or {}).items():
            for key, value in tally.items():
                tallies[group(colour)][key].append(value)
        for colour in players:
            if colour not in (result.get("battles") or {}):
                for key in ("guardFights", "guardFightsLost", "guardLosses", "guardKills", "playerFights", "playerFightsWon", "playerLosses", "playerKills"):
                    tallies[group(colour)][key].append(0)

    for name in sorted(table):
        print(f"\n## {name}" + ("" if single else f" (average of {len(records)} games, per seat)"))
        print("| day | " + " | ".join(COLUMNS) + " |")
        print("|---|" + "---|" * len(COLUMNS))
        days = sorted(table[name])
        for day in days:
            if day != days[-1] and (day - 1) % args.every != 0:
                continue
            row = table[name][day]
            print(f"| {day} | " + " | ".join(fmt(sum(row[c]) / max(1, len(row[c]))) for c in COLUMNS) + " |")
        t = tallies.get(name)
        if t:
            mean = {k: sum(v) / max(1, len(v)) for k, v in t.items()}
            share = mean["guardLosses"] / mean["guardKills"] if mean.get("guardKills") else 0
            print(f"\nBattles per seat: {mean.get('guardFights', 0):.1f} against guards ({mean.get('guardFightsLost', 0):.1f} lost), "
                  f"killed {fmt(mean.get('guardKills', 0))} for {fmt(mean.get('guardLosses', 0))} own losses (loss per value killed {share:.2f}); "
                  f"{mean.get('playerFights', 0):.1f} against players ({mean.get('playerFightsWon', 0):.1f} won), "
                  f"killed {fmt(mean.get('playerKills', 0))}, lost {fmt(mean.get('playerLosses', 0))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
