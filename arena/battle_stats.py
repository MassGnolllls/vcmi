#!/usr/bin/env python3
"""Classify FairAI's adventure-map battles from kept match logs (run_tournament.py play --keep-logs).

Usage: battle_stats.py RUN_DIR [--list N]
"""
import argparse
import collections
import glob
import re
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--list", type=int, default=0, help="print the first N lost battles")
    args = ap.parse_args()
    stats = collections.Counter()
    lost = []
    start = re.compile(r"FairAI (\w+) battle at \(([^)]*)\): (.*?) \((\d+)\) (attacks|is attacked by) (.*)$")
    result = re.compile(r"FairAI (\w+) battle result: (won|lost), (\d+) of our")
    for path in sorted(glob.glob(f"{args.run_dir}/logs_*/VCMI_Client_log.txt")):
        pending = None
        for line in open(path, errors="replace"):
            m = start.search(line)
            if m:
                pending = (m.group(2), m.group(3), int(m.group(4)), m.group(5), m.group(6).strip())
                continue
            m = result.search(line)
            if m and pending:
                pos, who, strength, mode, opponent = pending
                if mode == "is attacked by":
                    kind = "attacked by hero" if opponent != "monsters" else "attacked by monsters"
                else:
                    kind = "we attack guards" if opponent == "guards" else "we attack hero/town"
                stats[(kind, m.group(2))] += 1
                if m.group(2) == "lost":
                    lost.append((path.split("/")[-2], kind, who, strength, opponent, pos))
                pending = None
    for (kind, outcome), n in sorted(stats.items()):
        print(f"{kind:22s} {outcome:5s} {n:4d}")
    weak = sum(1 for l in lost if l[3] < 1000)
    print(f"lost battles: {len(lost)}, of these with own strength < 1000: {weak}")
    for l in lost[:args.list]:
        print("  ", *l)
    return 0


if __name__ == "__main__":
    sys.exit(main())
