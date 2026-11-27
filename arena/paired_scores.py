#!/usr/bin/env python3
"""Mean score of runs over several seeds, and the paired difference to the first run (95 % interval)."""
import json, sys, math
from pathlib import Path
# usage: scores.py LABEL=DIR1,DIR2,DIR3 ... : per label mean score over the games (W 1, L 0, open: the game's score), and paired differences to the first label
def game_scores(d):
    out = {}
    for line in (Path(d) / "results.jsonl").read_text().splitlines():
        r = json.loads(line); res = r.get("result") or {}
        mine = (r.get("mine") or ["red"])[0]
        if not res.get("timeline"):
            continue
        w = res.get("winner")
        if w == mine: s = 1.0
        elif w: s = 0.0
        else:
            sc = res.get("score") or {}
            s = sc.get(mine, 0.5) if isinstance(sc, dict) else 0.5
        out[r["match"]] = s
    return out
labels = []
for arg in sys.argv[1:]:
    label, dirs = arg.split("=", 1)
    per = {}
    for i, d in enumerate(dirs.split(",")):
        for g, s in game_scores(d).items():
            per[(i, g)] = s
    labels.append((label, per))
base_label, base = labels[0]
for label, per in labels:
    vals = list(per.values())
    m = sum(vals) / len(vals)
    w = sum(1 for v in vals if v == 1.0); l = sum(1 for v in vals if v == 0.0)
    line = f"{label:12s} n {len(vals):3d}  W {w:3d} L {l:3d} open {len(vals)-w-l:3d}  score {m:.3f}"
    if label != base_label:
        common = [k for k in per if k in base]
        diffs = [per[k] - base[k] for k in common]
        md = sum(diffs) / len(diffs)
        sd = math.sqrt(sum((x - md) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0
        line += f"   vs {base_label}: {md:+.3f} ± {1.96 * sd / math.sqrt(len(diffs)):.3f} (paired, {len(diffs)} games)"
    print(line)
