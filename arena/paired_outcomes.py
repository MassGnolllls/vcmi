#!/usr/bin/env python3
"""Per game index the outcome of several runs side by side (W/L/o; X: no result)."""
import json, sys
from pathlib import Path
# usage: paired.py LABEL=DIR ... ; prints per game index W/L/O(score) for the side under test, and the town of the map
runs = []
for arg in sys.argv[1:]:
    label, d = arg.split("=", 1)
    rows = {}
    for line in (Path(d) / "results.jsonl").read_text().splitlines():
        r = json.loads(line)
        res = r.get("result") or {}
        mine = r.get("mine") or []
        winner = res.get("winner") or r.get("winner")
        if not res.get("timeline"):
            rows[r["match"]] = ("X", r.get("map", "")); continue
        if winner in mine: o = "W"
        elif winner: o = "L"
        else: o = "o"
        rows[r["match"]] = (o, r.get("map", "").split("/")[-1].replace("MIRROR_", "")[:12])
    runs.append((label, rows))
print("game " + " ".join(f"{l[:9]:>9s}" for l, _ in runs) + "  map")
for g in range(24):
    cells = [rows.get(g, ("-", ""))[0] for _, rows in runs]
    m = next((rows[g][1] for _, rows in runs if g in rows), "")
    print(f"{g:4d} " + " ".join(f"{c:>9s}" for c in cells) + "  " + m)
