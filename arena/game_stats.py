#!/usr/bin/env python3
"""Per-match statistics of a full-game arena run (results.jsonl from run_tournament.py play).

Usage: game_stats.py RUN_DIR [--ai FairAI] [--brief]

Shows for every match the final army strength, towns, heroes, unspent gold and explored map
share of the AI under test and of its opponent, plus averages. Useful for runs with a day
limit, where most games end undecided.
"""
import argparse
import json
import statistics
import sys
from pathlib import Path

FIELDS = [("armyStrength", "army", "%7d"), ("towns", "towns", "%d"), ("heroes", "heroes", "%d"),
          ("gold", "gold", "%6d"), ("mapExploredRatio", "explored", "%.2f")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--ai", default="FairAI", help="name (or adventure part of the label) of the AI under test")
    ap.add_argument("--brief", action="store_true", help="only the summary")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(Path(args.run_dir) / "results.jsonl")]
    mine = {f[0]: [] for f in FIELDS}
    theirs = {f[0]: [] for f in FIELDS}
    wins = losses = undecided = 0
    for r in sorted(rows, key=lambda r: r["match"]):
        res = r.get("result") or {}
        players = res.get("players") or {}
        me = [c for c, p in players.items() if p.get("ai", "").split("+")[0] == args.ai.split("+")[0]]
        other = [c for c, p in players.items() if c not in me]
        if len(me) != 1 or len(other) != 1:
            if not args.brief:
                print(f"{r['match']:3d} {r.get('error', 'no result')}")
            continue
        a, b = players[me[0]], players[other[0]]
        winner = res.get("winner") or ""
        if winner == me[0]:
            wins += 1
        elif winner:
            losses += 1
        else:
            undecided += 1
        for key, _, _ in FIELDS:
            mine[key].append(a.get(key, 0))
            theirs[key].append(b.get(key, 0))
        if not args.brief:
            fa = " ".join(lbl + " " + (fmt % a.get(k, 0)) for k, lbl, fmt in FIELDS)
            fb = " ".join(lbl + " " + (fmt % b.get(k, 0)) for k, lbl, fmt in FIELDS)
            print(f"{r['match']:3d} {res.get('outcome', '?'):8s} {('won' if winner == me[0] else ('lost' if winner else '-')):4s} "
                  f"day {res.get('day', 0):3d} | {fa} | opp {fb}")

    n = len(mine["armyStrength"])
    if not n:
        print("no matches")
        return 1
    print(f"\n{args.ai}: {wins} won, {losses} lost, {undecided} undecided ({n} matches)")
    for key, lbl, _ in FIELDS:
        print(f"  {lbl:9s} mean {statistics.mean(mine[key]):10.2f}  vs {statistics.mean(theirs[key]):10.2f}"
              f"   median {statistics.median(mine[key]):10.2f}  vs {statistics.median(theirs[key]):10.2f}")
    ahead = sum(1 for x, y in zip(mine["armyStrength"], theirs["armyStrength"]) if x > y)
    print(f"  larger army at the end in {ahead} of {n} matches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
