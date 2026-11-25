#!/usr/bin/env python3
"""Games against another AI: do we hold what we take? Towns changing hands, defences, wardens.

Usage: ward_scan.py RESULTS_DIR [RESULTS_DIR ...]
Per game (our seat = "mine"): outcome; our sieges and how many we won; the enemy's attacks on our
towns - won by them, and how many met a hero of ours; days our number of towns fell / rose; the
enemy's towns on days 100, 200 and at the end; from the packed log: wardens formed and lost, their
armies (median of the twice-weekly samples), moves to another town.
"""
import json
import re
import statistics
import sys
import tarfile
from pathlib import Path

COL = {"red": 0, "blue": 1}
TROOPS = re.compile(r"day (\d+): troops: main army (\d+), .*wardens (\d+) with (\d+)")


def scan_log(root: Path, match: int, colour: str):
    out = {"formed": 0, "ended": 0, "moves": 0, "takes": 0, "cannot": 0, "samples": []}
    packed = list(root.glob(f"*_{match:04d}.tar.xz"))
    if not packed:
        return out
    with tarfile.open(packed[0]) as tar:
        member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
        if not member:
            return out
        tag = f"FairAI {colour}".encode()
        for raw in tar.extractfile(member):
            if tag not in raw:
                continue
            line = raw.decode("utf-8", "replace")
            if "leads the second line of" in line:
                out["formed"] += 1
            elif ": warden " in line and "leads no longer" in line:
                out["ended"] += 1
            elif ": warden " in line and "takes on troops" in line:
                out["takes"] += 1
            elif ": warden " in line and " hunts (" in line:
                out["hunts"] = out.get("hunts", 0) + 1
            elif "moves its army" in line:
                out["moves"] += 1
            elif "cannot hold" in line:
                out["cannot"] += 1
            else:
                t = TROOPS.search(line)
                if t:
                    out["samples"].append(tuple(int(x) for x in t.groups()))
    return out


def main():
    totals = []
    for folder in sys.argv[1:]:
        root = Path(folder)
        rows = [json.loads(line) for line in (root / "results.jsonl").read_text().splitlines() if line.strip()]
        print(f"== {root.name}")
        print("game map          seat  end   day | our sieges (won) | their attacks on our towns (won; met a hero of ours, of those held) | towns fell/rose | their towns d100 d200 end | wardens formed/ended, moves, median army (share of main)")
        for r in sorted(rows, key=lambda r: r["match"]):
            res = r.get("result") or {}
            if not res.get("timeline"):
                print(f"{r['match']:4d} no result")
                continue
            c = r["mine"][0]
            f = "blue" if c == "red" else "red"
            pb = res.get("playerBattles") or []
            ours = [b for b in pb if b[1] == COL[c] and b[8] == 1]
            theirs = [b for b in pb if b[1] == COL[f] and b[8] == 1]
            met = [b for b in theirs if b[9] == 1]
            hunts = [b for b in pb if b[1] == COL[c] and b[8] == 0 and b[9] == 1 and b[4] >= 20000]
            caught = [b for b in pb if b[1] == COL[f] and b[8] == 0 and b[9] == 1 and b[4] >= 20000]
            tl = {t["day"]: t["players"] for t in res["timeline"]}
            days = sorted(tl)
            fell = rose = 0
            for a, b in zip(days, days[1:]):
                if c in tl[a] and c in tl[b]:
                    d = tl[b][c][1] - tl[a][c][1]
                    fell += -d if d < 0 else 0
                    rose += d if d > 0 else 0

            def at(day, colour, index):
                row = tl.get(min(day, days[-1]), {}).get(colour)
                return row[index] if row else None

            log = scan_log(root, r["match"], c)
            late = [s for s in log["samples"] if s[2] > 0]
            army = statistics.median([s[3] for s in late]) if late else 0
            share = statistics.median([s[3] / max(1, s[1]) for s in late]) if late else 0
            name = r["map"].split("/")[-1].replace("MIRROR_", "")[:12]
            outcome = res.get("outcome") or "?"
            won = outcome == "victory" and res.get("winner") == c
            lost = outcome == "victory" and res.get("winner") == f
            state = "won" if won else ("lost" if lost else "open")
            print(f"{r['match']:4d} {name:12s} {c:5s} {state:5s} {res.get('day'):3d} | {len(ours):3d} ({sum(1 for b in ours if b[7] == 0):3d}) | "
                  f"{len(theirs):3d} ({sum(1 for b in theirs if b[7] == 0):3d}; {len(met):2d}, {sum(1 for b in met if b[7] == 1):2d}) | {fell:3d}/{rose:3d} | "
                  f"{at(100, f, 1)} {at(200, f, 1)} {at(days[-1], f, 1)} | {log['formed']}/{log['ended']} (hunting {log.get('hunts', 0)}x), {log['moves']}, {army / 1000:.0f}k ({100 * share:.0f}%) | "
                  f"enemy heroes of 20k+ we beat in the field {sum(1 for b in hunts if b[7] == 0)}/{len(hunts)}, ours of 20k+ they beat {sum(1 for b in caught if b[7] == 0)}/{len(caught)}  score {(r.get('score') or 0):.2f}")
            totals.append((state, res.get("day"), fell, len(theirs), r.get("score") or 0))
    if totals:
        n = len(totals)
        print(f"\n{n} games: won {sum(1 for t in totals if t[0] == 'won')}, lost {sum(1 for t in totals if t[0] == 'lost')}, open {sum(1 for t in totals if t[0] == 'open')}; "
              f"mean score {sum(t[4] for t in totals) / n:.3f}; towns lost per game {sum(t[2] for t in totals) / n:.1f}; enemy attacks on our towns per game {sum(t[3] for t in totals) / n:.1f}; "
              f"median day of the won games {statistics.median([t[1] for t in totals if t[0] == 'won']) if any(t[0] == 'won' for t in totals) else '-'}")


if __name__ == "__main__":
    main()
