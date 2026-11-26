#!/usr/bin/env python3
"""One line per run: how the side under test ("mine") did.

Usage: run_compare.py LABEL=RESULTS_DIR [LABEL=RESULTS_DIR ...]
Columns: games, won / lost / open, mean score; day of the end of decided games (median); the winner's
first siege (median); for the side under test: sieges and towns lost per game, the day of the first
big battle between heroes (both armies 100,000 or more), score of the open games.
"""
import json
import statistics
import sys
from pathlib import Path

COL = {"red": 0, "blue": 1}
BIG = 100000


def med(values):
    return statistics.median(values) if values else float("nan")


print(f"{'run':34s} {'n':>3s} {'won':>4s} {'lost':>4s} {'open':>4s} {'score':>6s} | {'end':>5s} {'1st siege':>9s} | {'sieges':>6s} {'lost twn':>8s} {'1st big':>7s} | open scores")
for arg in sys.argv[1:]:
    label, folder = arg.split("=", 1)
    rows = [json.loads(line) for line in (Path(folder) / "results.jsonl").read_text().splitlines() if line.strip()]
    won = lost = opened = 0
    scores, ends, firsts, sieges, towns_lost, big_first, open_scores = [], [], [], [], [], [], []
    for r in rows:
        res = r.get("result") or {}
        if not res.get("timeline"):
            continue
        c = r["mine"][0]
        f = "blue" if c == "red" else "red"
        score = r.get("score")
        if score is not None:
            scores.append(score)
        pb = res.get("playerBattles") or []
        if res.get("outcome") == "victory" and res.get("winner") in COL:
            w = res["winner"]
            if w == c:
                won += 1
            else:
                lost += 1
            ends.append(res["day"])
            mine_sieges = [b[0] for b in pb if b[1] == COL[w] and b[8] == 1]
            if mine_sieges:
                firsts.append(min(mine_sieges))
        else:
            opened += 1
            open_scores.append(score or 0)
        sieges.append(sum(1 for b in pb if b[1] == COL[c] and b[8] == 1))
        tl = {t["day"]: t["players"] for t in res["timeline"]}
        days = sorted(tl)
        fell = 0
        for a, b in zip(days, days[1:]):
            if c in tl[a] and c in tl[b]:
                fell += max(0, tl[a][c][1] - tl[b][c][1])
        towns_lost.append(fell)
        big = [b[0] for b in pb if b[9] == 1 and b[8] == 0 and min(b[3], b[4]) >= BIG]
        if big:
            big_first.append(min(big))
    n = won + lost + opened
    print(f"{label:34s} {n:3d} {won:4d} {lost:4d} {opened:4d} {statistics.mean(scores) if scores else float('nan'):6.3f} | {med(ends):5.0f} {med(firsts):9.0f} | "
          f"{sum(sieges) / max(1, n):6.1f} {sum(towns_lost) / max(1, n):8.1f} {med(big_first):7.0f} | {' '.join(f'{s:.2f}' for s in sorted(open_scores))}")
