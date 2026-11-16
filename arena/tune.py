#!/usr/bin/env python3
"""Phase 4: automatic tuning of FairAI's adventure parameters by playing games.

A simple (1+lambda) evolution strategy in a normalised parameter space:
every generation, `lambda` mutants of the current best are played on the same maps (both
seats) against a fixed opponent; a mutant replaces the best if it scores higher. The step
size shrinks when no mutant wins (1/5th-rule style).

Score of one game: 1 for a win, 0 for a loss, otherwise the army share of the AI under test
at the day limit (army / (army + opponent army)). With a day limit of ~28-45 days the army
share is informative and much less noisy than wins alone.

Usage (on a machine with spare cores, e.g. the second PC under WSL2):
    python3 tune.py --client SNAPSHOT/vcmiclient --opponent Nullkiller2 --maps 12 --days 28 \
        --generations 30 --lam 4 --jobs 6 --out tune1

Results: tune1/history.jsonl (every evaluation), tune1/best.json (best parameters so far).
Pass the result to games with FAI_PARAMS="$(cat tune1/best.json)" or --ai-params.
"""
import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# name: (min, max, default) - defaults mirror AI/FairAI/Adventure/Params.h
SPACE = {
    "safetyRatio": (1.05, 1.8, 1.3),
    "safetyRatioHero": (0.95, 1.5, 1.05),
    "threatTolerance": (0.8, 1.8, 1.4),
    "explorationTileValue": (8, 80, 36),
    "dwellingHorizonWeeks": (1.5, 6, 4),
    "incomeHorizonDays": (5, 24, 14),
    "reinforceFactor": (0.3, 1.6, 0.8),
    "ferryFactor": (0.0, 1.2, 0.5),
    "collectorRadius": (6, 24, 12),
    "unlockFactor": (0.2, 1.2, 0.7),
    "lossFactor": (0.3, 1.2, 0.7),
    "stickiness": (1.0, 1.6, 1.2),
}


def to_unit(params):
    return {k: (params[k] - lo) / (hi - lo) for k, (lo, hi, _) in SPACE.items()}


def from_unit(unit):
    return {k: round(lo + min(1.0, max(0.0, unit[k])) * (hi - lo), 4) for k, (lo, hi, _) in SPACE.items()}


def evaluate(args, params, tag, out_dir):
    run_dir = out_dir / tag
    cmd = [sys.executable, str(HERE / "run_tournament.py"), "play", "--client", args.client,
           "--ai", "FairAI", "--opponent", args.opponent, "--map-filter", args.map_filter,
           "--max-maps", str(args.maps), "--max-days", str(args.days), "--jobs", str(args.jobs),
           "--timeout", str(args.timeout), "--stall-seconds", "40", "--seed", str(args.map_seed),
           "--base-port", str(args.base_port), "--ai-params", json.dumps(params), "--out", str(run_dir)]
    if args.opponent == "FairAI":
        cmd += ["--opponent-params", json.dumps(args.opponent_params or {})]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    scores = []
    for line in open(run_dir / "results.jsonl"):
        record = json.loads(line)
        result = record.get("result") or {}
        players = result.get("players") or {}
        if len(players) != 2:
            continue
        # jobs come in pairs: even match ids have the AI under test as red
        me, other = ("blue", "red") if record["match"] % 2 else ("red", "blue")
        winner = result.get("winner") or ""
        if winner == me:
            scores.append(1.0)
        elif winner == other:
            scores.append(0.0)
        else:
            a = players[me].get("armyStrength", 0)
            b = players[other].get("armyStrength", 0)
            scores.append(a / (a + b) if a + b > 0 else 0.5)
    return sum(scores) / len(scores) if scores else 0.0, len(scores)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client", required=True)
    ap.add_argument("--opponent", default="Nullkiller2")
    ap.add_argument("--opponent-params", type=json.loads, default=None, help="for self-play against FairAI")
    ap.add_argument("--map-filter", default="2SM4d_s_l1_none")
    ap.add_argument("--maps", type=int, default=12)
    ap.add_argument("--days", type=int, default=28)
    ap.add_argument("--generations", type=int, default=20)
    ap.add_argument("--lam", type=int, default=4, help="mutants per generation")
    ap.add_argument("--sigma", type=float, default=0.15, help="initial step size (share of each range)")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=400)
    ap.add_argument("--map-seed", type=int, default=42)
    ap.add_argument("--base-port", type=int, default=3600)
    ap.add_argument("--start", help="JSON file with starting parameters (default: Params.h defaults)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(1)
    best = {k: d for k, (_, _, d) in SPACE.items()}
    if args.start:
        best.update(json.load(open(args.start)))
    best_score, n = evaluate(args, best, "g000_base", out_dir)
    history = open(out_dir / "history.jsonl", "a")
    history.write(json.dumps({"gen": 0, "tag": "base", "score": best_score, "games": n, "params": best}) + "\n")
    history.flush()
    print(f"start: score {best_score:.3f} over {n} games")
    sigma = args.sigma
    for gen in range(1, args.generations + 1):
        improved = False
        base_unit = to_unit(best)
        for i in range(args.lam):
            unit = {k: v + rng.gauss(0, sigma) for k, v in base_unit.items()}
            cand = from_unit(unit)
            score, n = evaluate(args, cand, f"g{gen:03d}_{i}", out_dir)
            history.write(json.dumps({"gen": gen, "tag": i, "score": score, "games": n, "params": cand}) + "\n")
            history.flush()
            print(f"gen {gen} cand {i}: score {score:.3f} (best {best_score:.3f})")
            if score > best_score:
                # re-evaluate on the same maps to guard against lucky runs
                again, n2 = evaluate(args, cand, f"g{gen:03d}_{i}_check", out_dir)
                combined = (score * n + again * n2) / max(1, n + n2)
                if combined > best_score:
                    best, best_score, improved = cand, combined, True
                    json.dump(best, open(out_dir / "best.json", "w"), indent=1)
                    print(f"  new best {best_score:.3f}")
        sigma = min(0.3, sigma * 1.3) if improved else max(0.03, sigma * 0.8)
    json.dump(best, open(out_dir / "best.json", "w"), indent=1)
    print(f"done: best score {best_score:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
