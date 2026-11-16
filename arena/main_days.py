#!/usr/bin/env python3
"""What did FairAI's main hero do, day by day? Reads a client log written with ARENA_AI_LOG=debug.

  main_days.py LOG [--player red]

One line per day: where the main hero ended, its strength, the kinds of steps it took, fights, and
why it had no target if it had none. Days on which it ends where it started are marked "stuck".
"""
import argparse
import re
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--player", default="red")
    args = ap.parse_args()
    tag = f"FairAI {args.player}"
    day = 0
    info = {}
    for line in open(args.log, errors="replace"):
        if tag not in line:
            continue
        m = re.search(rf"{tag} day (\d+) economy", line)
        if m:
            day = int(m.group(1))
            info[day] = {"targets": [], "fights": 0, "end": "", "nt": [], "pos": None}
            continue
        if day == 0:
            continue
        d = info[day]
        m = re.search(rf"{tag}: (\S+) -> (\w+)(?: (\d+))?(?: (own|enemy))? \((\d+) (\d+) (\d+)\) value (\d+) loss (\d+)", line)
        if m:
            # heroes: "34 own" (walking up to our own hero hands over troops) or "34 enemy" (a hunt)
            d["targets"].append((m.group(1), m.group(2), (m.group(3) or "") + (" " + m.group(4) if m.group(4) else ""), (m.group(5), m.group(6))))
        if "battle at" in line and " attacks " in line:
            d["fights"] += 1
        m = re.search(rf"{tag}: (\S+) \(strength (\d+)\) has no target; weakest guard in reach (\d+);.*rejected:(.*)", line)
        if m:
            d["nt"].append((m.group(1), m.group(3), m.group(4).strip()))
        m = re.search(rf"{tag}: (\S+) \(main\) ends at \((\d+) (\d+) (\d+)\), strength (\d+), known threat (\d+).*mp (\d+)(, in town)?", line)
        if m:
            d["main"] = m.group(1)
            d["pos"] = (m.group(2), m.group(3))
            d["end"] = f"{m.group(1)} at ({m.group(2)},{m.group(3)}) strength {int(m.group(5)) // 1000}k threat {int(m.group(6)) // 1000}k mp {m.group(7)}{' in town' if m.group(8) else ''}"
    previous = None
    stuck = 0
    for day in sorted(info):
        d = info[day]
        main_hero = d.get("main", "?")
        kinds = {}
        objects = {}
        for who, kind, sub, tile in d["targets"]:
            if who != main_hero:
                continue
            kinds[kind] = kinds.get(kind, 0) + 1
            if kind == "object":
                objects[sub] = objects.get(sub, 0) + 1
        no_target = [x for x in d["nt"] if x[0] == main_hero]
        same = previous is not None and d["pos"] == previous
        stuck += 1 if same else 0
        steps = ", ".join(f"{k} {v}" for k, v in kinds.items())
        if objects:
            steps += " (object types " + ", ".join(f"{k}x{v}" for k, v in sorted(objects.items(), key=lambda kv: -kv[1])[:3]) + ")"
        print(f"day {day:3d}  {d['end']:58s} {'stuck ' if same else '      '}fights {d['fights']}  {steps}"
              + (f"  NO TARGET (weakest guard {no_target[0][1]}; {no_target[0][2]})" if no_target else ""))
        previous = d["pos"]
    print(f"{stuck} of {len(info)} days ended where they began")
    return 0


if __name__ == "__main__":
    sys.exit(main())
