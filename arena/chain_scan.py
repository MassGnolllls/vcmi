#!/usr/bin/env python3
"""Probe runs of the hero chain: where the troops stand, what the chain delivered.

Usage: chain_scan.py RESULTS_DIR [DAY ...]
Per game and side (the side under test = "mine" plays the theme): share of all troops that stands
in towns, share with the main hero, at the given days; from the packed logs: deliveries, loads
that did not arrive the same day, weekly rounds, heroes lost, seconds per turn.
"""
import datetime
import json
import re
import sys
import tarfile
from pathlib import Path

root = Path(sys.argv[1])
days = [int(d) for d in sys.argv[2:]] or [42, 63, 90, 120, 150]
STAMP = re.compile(r"^(\d\d:\d\d:\d\d)")


def shares(row):
    armies, main, towns = row[0], row[11], row[12]
    total = max(1, armies + towns)
    return towns / total, main / total, total


records = [json.loads(line) for line in (root / "results.jsonl").read_text().splitlines() if line.strip()]
records.sort(key=lambda r: r["match"])
print("game map            side  on |" + " |".join(f" d{d}: towns main total" for d in days))
sums = {}
for rec in records:
    timeline = {t["day"]: t["players"] for t in rec["result"].get("timeline", [])}
    name = rec["map"].split("/")[-1].replace("MIRROR_", "")[:14]
    for colour in sorted(rec["seats"]):
        on = colour in rec["mine"]
        cells = []
        for d in days:
            row = timeline.get(d, {}).get(colour)
            if not row:
                cells.append("     -    -      -")
                continue
            t, m, total = shares(row)
            cells.append(f"   {100 * t:4.0f}% {100 * m:3.0f}% {total / 1000:5.0f}k")
            s = sums.setdefault((on, d), [0, 0.0, 0.0, 0.0])
            s[0] += 1
            s[1] += t
            s[2] += m
            s[3] += total
        print(f"{rec['match']:4d} {name:14s} {colour:5s} {'x' if on else ' '}  |" + " |".join(cells))
print()
for on in (True, False):
    line = "chain on : " if on else "chain off: "
    for d in days:
        s = sums.get((on, d))
        line += f" d{d}: towns {100 * s[1] / s[0]:.0f}% main {100 * s[2] / s[0]:.0f}% total {s[3] / s[0] / 1000:.0f}k |" if s else f" d{d}: - |"
    print(line)
print()

# from the logs
for rec in records:
    packed = list(root.glob(f"*_{rec['match']:04d}.tar.xz"))
    if not packed:
        continue
    counts = {}
    with tarfile.open(packed[0]) as tar:
        member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
        if not member:
            continue
        for raw in tar.extractfile(member):
            line = raw.decode("utf-8", "replace")
            m = re.search(r"FairAI (\w+)[ :]", line)
            if not m:
                continue
            c = counts.setdefault(m.group(1), {"runs": 0, "delivered": 0, "sum": 0.0, "stuck": 0, "round": 0, "lost": 0, "posts": 0, "days": 0, "short": 0, "hops": 0})
            if " chain day " in line and ("waits on its post" in line or "> waits (" in line):
                c["waits"] = c.get("waits", 0) + 1
            elif " chain day " in line and "no run" in line:
                c["norun"] = c.get("norun", 0) + 1
            elif ": troops: main army" in line:
                t = re.search(r"day (\d+): troops: main army (\d+), in our towns (\d+) for the main hero to take, garrisons (\d+), heroes in towns (\d+), other heroes in the field (\d+)", line)
                if t:
                    c.setdefault("troops", []).append(tuple(int(x) for x in t.groups()))
            elif " chain day " in line:
                c["runs"] += 1
                got = re.search(r"delivered (\d+) to", line)
                handed = re.search(r"handed on (\d+) times", line)
                if got and int(got.group(1)) > 0:
                    c["delivered"] += 1
                    c["sum"] += int(got.group(1))
                    c["hops"] += int(handed.group(1)) if handed else 0
                else:
                    c["stuck"] += 1
            elif " chain round" in line:
                c["round"] += 1
            elif "lost a hero" in line:
                c["lost"] += 1
            elif ": chain of " in line:
                c["days"] += 1
                m2 = re.search(r"chain of (\d+) links \((\d+) posts on the road of \d+ wanted \d+ points apart, short by (-?\d+)\)", line)
                if m2:
                    c["posts"] += int(m2.group(2))
                    c["short"] += 1 if int(m2.group(3)) > 0 else 0
    for colour in sorted(rec["seats"]):
        c = counts.get(colour)
        if not c:
            continue
        on = colour in rec["mine"]
        print(f"game {rec['match']} {colour:5s} {'chain' if on else '     '}: chain days {c['days']:3d} (short on {c['short']}), posts/day {c['posts'] / max(1, c['days']):.1f}, "
              f"runs {c['runs']}, delivered {c['delivered']} (sum {c['sum'] / 1000:.0f}k, {c['hops'] / max(1, c['delivered']):.1f} hand-overs each), "
              f"on foot or cut short {c['stuck']}, waiting {c.get('waits', 0)}, no run {c.get('norun', 0)}, rounds {c['round']}, heroes lost {c['lost']}")
        rows = c.get("troops", [])
        for label, pick in (("week start", 1), ("mid-week  ", 4)):
            part = [r for r in rows if r[0] % 7 == pick and r[0] >= 22]
            if part:
                import statistics
                med = lambda xs: statistics.median(xs)
                wait = med([r[2] / max(1, r[1]) for r in part])
                gar = med([(r[3] + r[4]) / max(1, r[1]) for r in part])
                fld = med([r[5] / max(1, r[1]) for r in part])
                main = med([r[1] for r in part])
                print(f"        {label} (from day 22, {len(part)} samples, medians): in towns for the main hero to take {100 * wait:.0f}% of its army, in towns in all {100 * gar:.0f}%, other heroes in the field {100 * fld:.0f}%; main army {main / 1000:.0f}k")
    print(f"game {rec['match']}: wall {rec['result'].get('wallTimeSec', 0):.0f} s, day {rec['result'].get('day')}, score of the chain side {rec.get('score', 0):.2f}, refused {rec.get('refused', {}).get('count', 0)}")
