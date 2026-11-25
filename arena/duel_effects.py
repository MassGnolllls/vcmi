#!/usr/bin/env python3
"""Effects of the themes of a duel played by plan (run_tournament.py play --duel-plan).

In every game each theme is switched on for exactly one of the two sides (both FairAI, same build), by a
balanced plan: every theme is with the side under test in half of the rows, and every two themes are on
the same side as often as on opposite sides. So every game says something about every theme:

  score of a theme = mean score of the side that played with it (1 won, 0 lost, army share at the day limit)
  0.5 = the theme changes nothing

  duel_effects.py FOLDER [FOLDER ...]     (folders with results.jsonl, one per seed)

The interval is taken from the pairs of games on the same map and seed (seats swapped): what the seat or
the starting hero does to a game cancels within a pair.
"""
import argparse
import json
import math
import statistics
from pathlib import Path

# two-sided 95 % points of Student's t for small samples
T95 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 11: 2.20, 12: 2.18, 15: 2.13,
       20: 2.09, 25: 2.06, 30: 2.04, 40: 2.02, 60: 2.00, 120: 1.98}


def t95(df: int) -> float:
    if df <= 0:
        return float("nan")
    best = max(k for k in T95 if k <= df) if df >= 1 else 1
    return T95[best] if df < 121 else 1.96


def load(folders):
    games = []
    for folder in folders:
        file = Path(folder) / "results.jsonl"
        if not file.exists():
            continue
        for line in file.read_text().splitlines():
            if not line.strip():
                continue
            rec = json.loads(line)
            if rec.get("plan"):
                rec["folder"] = str(folder)
                games.append(rec)
    return games


def theme_names(games):
    names = []
    for rec in games:
        for name in rec["plan"]["mineOn"] + rec["plan"]["otherOn"]:
            if name not in names:
                names.append(name)
    return names


def on_side(rec, theme):
    """Colours of the side that played with the theme, and that side's score (None if the game failed)."""
    mine = rec.get("mine") or []
    with_mine = theme in rec["plan"]["mineOn"]
    colours = mine if with_mine else [c for c in (rec.get("seats") or {}) if c not in mine]
    score = rec.get("score")
    if score is None:
        return colours, None
    return colours, score if with_mine else 1.0 - score


def effects(games):
    rows = []
    for theme in theme_names(games):
        pairs = {}
        won = lost = undecided = failed = 0
        scores = []
        for rec in games:
            colours, score = on_side(rec, theme)
            if score is None:
                failed += 1
                continue
            scores.append(score)
            result = rec.get("result") or {}
            if result.get("outcome") == "victory" and result.get("winner"):
                if result["winner"] in colours:
                    won += 1
                else:
                    lost += 1
            else:
                undecided += 1
            pairs.setdefault((rec["folder"], rec["map"]), []).append(score)
        means = [statistics.mean(v) for v in pairs.values()]
        mean = statistics.mean(scores) if scores else float("nan")
        half = t95(len(means) - 1) * statistics.stdev(means) / math.sqrt(len(means)) if len(means) > 1 else float("nan")
        rows.append({"theme": theme, "games": len(scores), "score": mean, "half": half, "won": won, "lost": lost,
                     "undecided": undecided, "failed": failed})
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folders", nargs="+")
    args = parser.parse_args()
    games = load(args.folders)
    if not games:
        print("no games played by a duel plan found")
        return 1
    failed = sum(1 for g in games if g.get("score") is None)
    print(f"{len(games)} games ({failed} failed) from {len({g['folder'] for g in games})} folder(s)\n")
    print("| Thema | Partien | Wertung der Seite mit dem Thema | 95 % | gewonnen | verloren | offen |")
    print("|---|---|---|---|---|---|---|")
    for row in sorted(effects(games), key=lambda r: -r["score"]):
        print(f"| {row['theme']} | {row['games']} | {row['score']:.3f} | ± {row['half']:.3f} | {row['won']} | {row['lost']} | {row['undecided']} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
