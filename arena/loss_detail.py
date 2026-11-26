#!/usr/bin/env python3
"""FairAI's defeats against another AI, one block each: how the two sides developed, and the battle that decided it.

Usage: loss_detail.py REASONS_JSON RESULTS_DIR
For every side under test ("mine") that lost: the reason (duel_reasons.py); on days 28, 56, 84 and the day
before the decisive battle both sides' troops (heroes + towns), towns, income and best hero level; the
decisive battle from the ARENA_BATTLE record (matched by our army's strength): both heroes (type, level,
attack/defence/power/knowledge, morale, the expert skills), both armies, losses.
"""
import json
import re
import sys
import tarfile
from pathlib import Path

rows = json.load(open(sys.argv[1]))
root = Path(sys.argv[2])
recs = {json.loads(l)["match"]: json.loads(l) for l in open(root / "results.jsonl") if l.strip()}


def battles(archive):
    with tarfile.open(archive) as tar:
        member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
        text = tar.extractfile(member).read().decode("utf-8", "replace")
    out = []
    for m in re.finditer(r"ARENA_BATTLE (\{)", text):
        i = m.start(1)
        depth = 0
        for j in range(i, len(text)):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
        try:
            out.append(json.loads(text[i:j + 1]))
        except ValueError:
            pass
    return out


def hero(side):
    h = side.get("hero") or {}
    if not h:
        return "no hero"
    p = h.get("primary") or [0, 0, 0, 0]
    experts = [k.split(":")[-1] for k, v in (h.get("secondary") or {}).items() if v == 3]
    return f"{h.get('type', '?').split(':')[-1]} L{h.get('level')} A{p[0]} D{p[1]} P{p[2]} K{p[3]} morale {h.get('morale')}, expert: {', '.join(experts)}"


def army(side):
    return ", ".join(f"{n} {c.split(':')[-1]}" for c, n, _ in side.get("stacks", []) if c.split(":")[-1] not in ("ballista", "firstAidTent", "catapult", "ammoCart", "arrowTower"))


for r in sorted(rows, key=lambda r: r["match"]):
    rec = recs.get(r["match"])
    if not rec or r["state"] != "lost" or r["colour"] not in rec["mine"]:
        continue
    c = r["colour"]
    f = "blue" if c == "red" else "red"
    d = r.get("detail") or {}
    day = d.get("day") or 0
    tl = {t["day"]: t["players"] for t in rec["result"]["timeline"]}
    name = rec["map"].split("/")[-1].replace("MIRROR_", "")[:14]
    print(f"== game {r['match']} {name} {c}: {r['reason']}, decisive battle day {day} ({'attacked' if d.get('attacked') else 'we attacked'}{', in a town' if d.get('inTown') else ''}), field ratio {d.get('ratio')}")
    for t in (28, 56, 84, max(1, day - 1)):
        a = tl.get(t, {}).get(c)
        b = tl.get(t, {}).get(f)
        if a and b:
            print(f"   day {t:3d}: troops ours {(a[0] + a[12]) / 1000:5.0f}k / theirs {(b[0] + b[12]) / 1000:5.0f}k, towns {a[1]}/{b[1]}, income {a[3] / 1000:.1f}k/{b[3] / 1000:.1f}k, level {a[6]}/{b[6]}, gold {a[7] / 1000:.0f}k/{b[7] / 1000:.0f}k")
    archive = next(iter(root.glob(f"*_{r['match']:04d}.tar.xz")), None)
    if archive and d.get("ownField"):
        for b in battles(archive):
            sides = b.get("sides", [])
            ours = [s for s in sides if s.get("player") == c]
            theirs = [s for s in sides if s.get("player") == f]
            if ours and theirs and abs(ours[0].get("initialStrength", 0) - d["ownField"]) <= max(5, 0.002 * d["ownField"]):
                o, t = ours[0], theirs[0]
                print(f"   ours:   {hero(o)}; {o['initialStrength'] / 1000:.0f}k, left {o.get('remainingFraction', 0):.2f}: {army(o)}")
                print(f"   theirs: {hero(t)}; {t['initialStrength'] / 1000:.0f}k, left {t.get('remainingFraction', 0):.2f}: {army(t)}")
                break
