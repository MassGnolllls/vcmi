#!/usr/bin/env python3
"""Summarise worker jobs (branch fairai-results): games, wins, losses and a score per job.

Score per game: 1 win, 0 loss, otherwise the army share of the AI under test at the day limit.

Usage: job_report.py RESULTS_DIR [--filter TEXT]
"""
import argparse
import json
import statistics
import sys
from pathlib import Path


def score_games(results_file: Path):
    scores, wins, losses = [], 0, 0
    for line in open(results_file):
        record = json.loads(line)
        result = record.get("result") or {}
        players = result.get("players") or {}
        if len(players) != 2:
            continue
        me, other = ("blue", "red") if record["match"] % 2 else ("red", "blue")
        winner = result.get("winner") or ""
        if winner == me:
            wins += 1
            scores.append(1.0)
        elif winner == other:
            losses += 1
            scores.append(0.0)
        else:
            a = players[me].get("armyStrength", 0)
            b = players[other].get("armyStrength", 0)
            scores.append(a / (a + b) if a + b > 0 else 0.5)
    return scores, wins, losses


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir")
    ap.add_argument("--filter", default="")
    args = ap.parse_args()
    for job_dir in sorted(Path(args.results_dir).iterdir()):
        if not job_dir.is_dir() or job_dir.name.startswith(".") or args.filter not in job_dir.name:
            continue
        status = json.loads((job_dir / "status.json").read_text()) if (job_dir / "status.json").exists() else {}
        results = job_dir / "results.jsonl"
        if not results.exists():
            print(f"{job_dir.name:34s} {status.get('state', '?'):8s}")
            continue
        scores, wins, losses = score_games(results)
        if not scores:
            print(f"{job_dir.name:34s} {status.get('state', '?'):8s} no games")
            continue
        ci = 1.96 * statistics.stdev(scores) / len(scores) ** 0.5 if len(scores) > 1 else 0
        print(f"{job_dir.name:34s} {status.get('state', '?'):8s} games {len(scores):3d}  won {wins:3d}  lost {losses:3d}"
              f"  score {statistics.mean(scores):.3f} ± {ci:.3f}  ({status.get('minutes', '-')} min)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
