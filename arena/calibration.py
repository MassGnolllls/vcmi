#!/usr/bin/env python3
"""How good is FairAI's hero model? Battles against enemy heroes as the model saw them before they
were fought, against how they ended.

FairAI writes one entry per battle against an enemy hero into its statistics (key "fights", games
played from 18.11.2026 on): day, we attack, siege, our army, our hero's factor x 100, their army,
the factor we gave their hero x 100 (-1: never seen before the battle), won, our loss in per cent,
theirs. The model ratio is (our army x our factor) / (their army x their factor), the defender of a
siege counted --walls times (default 1.5).

Usage:
  calibration.py FOLDER [FOLDER ...] [--min 20000] [--walls 1.5]

FOLDER: anything with results.jsonl files below it. Prints, per kind of battle and bucket of the
model ratio: battles, share won, mean losses of both sides. A model that is right wins about half of
the battles around ratio 1; the edge FairAI asks for (heroEdge, boldEdge) should sit where the
share won is what the situation is worth.
"""
import argparse
import collections
import json
from pathlib import Path

BUCKETS = [(0, 0.5), (0.5, 0.7), (0.7, 0.85), (0.85, 1.0), (1.0, 1.15), (1.15, 1.3), (1.3, 1.6), (1.6, 2.5), (2.5, 1e9)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+", type=Path)
    ap.add_argument("--min", type=float, default=20000, help="both armies at least this strong")
    ap.add_argument("--walls", type=float, default=1.5)
    args = ap.parse_args()
    rows = collections.defaultdict(list)
    reach = collections.defaultdict(list)
    games = unseen = 0
    for folder in args.folders:
        for results in sorted(folder.rglob("results.jsonl")):
            for line in results.read_text().splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                for colour in record.get("mine") or []:
                    fights = ((record.get("fairai") or {}).get(colour) or {}).get("fights")
                    if fights is None:
                        continue
                    games += 1
                    for fight in fights:
                        day, attack, siege, own, own_factor, their, their_factor, won, own_lost, their_lost = fight[:10]
                        if not attack and own >= args.min and len(fight) > 11:
                            reach[("at the walls" if siege else "open field")].append((fight[10], fight[11], won, own * own_lost / 100.0))
                        if min(own, their) < args.min:
                            continue
                        if own_factor <= 0 or their_factor <= 0:
                            unseen += 1
                            rows[("their hero never seen before" if their_factor <= 0 else "our hero's factor unknown", "any")].append((1.0, won, own_lost, their_lost))
                            continue
                        ours = own * own_factor / 100.0 * (args.walls if siege and not attack else 1.0)
                        theirs = their * their_factor / 100.0 * (args.walls if siege and attack else 1.0)
                        kind = ("we attack" if attack else "we are attacked") + (", at the walls" if siege else ", open field")
                        rows[(kind, "ratio")].append((ours / theirs, won, own_lost, their_lost))
    print(f"{games} games with a calibration record; battles with both armies >= {args.min:.0f}")
    if not rows:
        return 1
    for (kind, how), battles in sorted(rows.items()):
        print(f"\n{kind}: {len(battles)} battles, {100 * sum(b[1] for b in battles) / len(battles):.0f} % won")
        if how != "ratio":
            continue
        print("| model ratio | battles | won | our loss | their loss |")
        print("|---|---|---|---|---|")
        for low, high in BUCKETS:
            part = [b for b in battles if low <= b[0] < high]
            if not part:
                continue
            label = f"{low:.2f} - {high:.2f}" if high < 1e8 else f"over {low:.2f}"
            print(f"| {label} | {len(part)} | {100 * sum(b[1] for b in part) / len(part):.0f} % | {sum(b[2] for b in part) / len(part):.0f} % | {sum(b[3] for b in part) / len(part):.0f} % |")
    # attacks on our armies: how far away the reach model had the attacker when our last turn ended
    steps = [(-2, 0, "not among the threats we knew"), (0, 80, "up to 8 tiles"), (80, 120, "8 - 12"), (120, 160, "12 - 16"), (160, 200, "16 - 20"),
             (200, 240, "20 - 24"), (240, 320, "24 - 32"), (320, 601, "32 - 60"), (601, 100000, "over 60")]
    for where, attacks in sorted(reach.items()):
        print(f"\nattacks on our armies of {args.min:.0f} or more, {where}: {len(attacks)} - where our reach model had the attacker the evening before")
        print("| model distance | attacks | share | held | army lost, thousands |")
        print("|---|---|---|---|---|")
        for low, high, label in steps:
            part = [a for a in attacks if low <= a[0] < high]
            if part:
                print(f"| {label} | {len(part)} | {100 * len(part) / len(attacks):.0f} % | {100 * sum(a[2] for a in part) / len(part):.0f} % | {sum(a[3] for a in part) / 1000:.0f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
