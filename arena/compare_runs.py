#!/usr/bin/env python3
"""Paired comparison of two battle-arena runs (same scenarios): which variant scores better?"""
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_tournament import battle_score  # noqa: E402


def load(run_dir, ai):
    scores = {}
    for line in open(Path(run_dir) / "results.jsonl"):
        r = json.loads(line)
        if "error" in r or r.get("result", {}).get("outcome") != "battle":
            continue
        _, s = battle_score(r, ai)
        scores.setdefault(r["map"], []).append(s)
    return {m: sum(v) / len(v) for m, v in scores.items()}


def main():
    a_dir, b_dir, ai = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "FairAI"
    a, b = load(a_dir, ai), load(b_dir, ai)
    common = sorted(set(a) & set(b))
    diffs = [b[m] - a[m] for m in common]
    n = len(diffs)
    mean = sum(diffs) / n
    sd = math.sqrt(sum((d - mean) ** 2 for d in diffs) / max(1, n - 1))
    se = sd / math.sqrt(n)
    print(f"{n} common scenarios: mean(A)={sum(a[m] for m in common) / n:+.3f} mean(B)={sum(b[m] for m in common) / n:+.3f}")
    print(f"B - A = {mean:+.3f} ± {1.96 * se:.3f} (95%); B better in {sum(d > 0.02 for d in diffs)}, worse in {sum(d < -0.02 for d in diffs)}")


if __name__ == "__main__":
    main()
