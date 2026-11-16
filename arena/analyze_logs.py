#!/usr/bin/env python3
"""Aggregate FairAI's own log lines over many arena games (run with --keep-logs).

Answers "where does FairAI lose?" with counts instead of single anecdotes:
  - heroes and garrisons lost: who, how strong, attacker strength, was the attacker known
    (the 'known threat' FairAI logged for that hero at the end of its previous turn)
  - towns lost / taken per day, mines, main hero idle days and army growth stalls
  - targets the main hero pursued per category

Usage: analyze_logs.py RUN_DIR [RUN_DIR ...]
"""
import collections
import json
import re
import sys
from pathlib import Path

LINE = re.compile(r"\] (?:INFO|DEBUG) \[[^\]]*\] ai - (FairAI (\w+)\b.*)$")
DAY_END = re.compile(r"FairAI (\w+) day (\d+) end: gold (\d+), heroes (\d+), towns (\d+), main hero (\S+) army (\d+)")
ENDS_AT = re.compile(r"FairAI (\w+): (\S+)(?: \(main\))? ends at \((\d+) (\d+) (\d+)\), strength (\d+), known threat (\d+)")
BATTLE_AT = re.compile(r"FairAI (\w+) battle at \((\d+) (\d+) (\d+)\): (.*)$")
RESULT = re.compile(r"FairAI (\w+) battle result: (won|lost), (\d+) of our creatures died \(value (\d+) of (\d+), enemy (\d+)\)")
TARGET = re.compile(r"FairAI (\w+): (\S+) -> (\w+)")


def parse_battle(desc):
    """'Daremyth (4334) attacks guards' / 'army (420) is attacked by Bron' / 'Serena (165) is attacked by Bron'"""
    m = re.match(r"(.+?) \((\d+)\) (attacks|is attacked by) (.+)$", desc)
    if not m:
        return None
    return {"who": m.group(1), "strength": int(m.group(2)), "attacking": m.group(3) == "attacks", "other": m.group(4)}


def analyze_log(path, color, stats):
    days = {}
    last_known = {}   # hero -> known threat at the end of its last turn
    battle = None
    main_name = None
    for raw in path.open(errors="replace"):
        m = LINE.search(raw)
        if not m or m.group(2) != color:
            continue
        text = m.group(1)
        if d := DAY_END.match(text):
            day = int(d.group(2))
            days[day] = {"gold": int(d.group(3)), "heroes": int(d.group(4)), "towns": int(d.group(5)), "army": int(d.group(7))}
            main_name = d.group(6)
            continue
        if e := ENDS_AT.match(text):
            last_known[e.group(2)] = (int(e.group(6)), int(e.group(7)))
            continue
        if b := BATTLE_AT.match(text):
            battle = parse_battle(b.group(5))
            continue
        if r := RESULT.match(text):
            if battle:
                won = r.group(2) == "won"
                enemy = int(r.group(6))
                who = battle["who"]
                if battle["attacking"]:
                    key = "attack " + ("guards" if battle["other"] == "guards" else "hero/town")
                    stats["battles"][key + (" won" if won else " LOST")] += 1
                    if not won:
                        stats["attack_losses"].append((path.parent.name, who, battle["strength"], battle["other"], enemy))
                else:
                    kind = "garrison" if who == "army" else ("main" if who == main_name else "secondary")
                    stats["battles"]["defend " + kind + (" won" if won else " LOST")] += 1
                    if not won:
                        own, known = last_known.get(who, (battle["strength"], -1))
                        surprise = "unknown" if known <= 0 else ("known>own" if known > own else "known<own")
                        stats["deaths"][kind + " " + surprise] += 1
                        ratio = enemy / max(1, battle["strength"])
                        stats["death_ratio"][kind].append(ratio)
            battle = None
            continue
        if t := TARGET.match(text):
            if t.group(2) == main_name:
                stats["main_targets"][t.group(3)] += 1
    if not days:
        return
    stats["games"] += 1
    prev_towns = None
    flat = 0
    best_flat = 0
    prev_army = None
    for day in sorted(days):
        towns = days[day]["towns"]
        if prev_towns is not None:
            if towns < prev_towns:
                stats["towns_lost_by_week"][(day - 1) // 7 + 1] += prev_towns - towns
            elif towns > prev_towns:
                stats["towns_won_by_week"][(day - 1) // 7 + 1] += towns - prev_towns
        prev_towns = towns
        army = days[day]["army"]
        if prev_army is not None and army <= prev_army:
            flat += 1
            best_flat = max(best_flat, flat)
        else:
            flat = 0
        prev_army = army
        if day % 7 == 0:
            stats["gold_at_week_end"][day // 7].append(days[day]["gold"])
            stats["towns_at_week_end"][day // 7].append(towns)
            stats["heroes_at_week_end"][day // 7].append(days[day]["heroes"])
    stats["longest_army_stall"].append(best_flat)


def main():
    stats = {
        "games": 0,
        "battles": collections.Counter(),
        "deaths": collections.Counter(),
        "death_ratio": collections.defaultdict(list),
        "attack_losses": [],
        "towns_lost_by_week": collections.Counter(),
        "towns_won_by_week": collections.Counter(),
        "gold_at_week_end": collections.defaultdict(list),
        "towns_at_week_end": collections.defaultdict(list),
        "heroes_at_week_end": collections.defaultdict(list),
        "longest_army_stall": [],
        "main_targets": collections.Counter(),
    }
    for run in sys.argv[1:]:
        for match in sorted(Path(run).glob("match_*.json")):
            info = json.loads(match.read_text())
            colors = [c for c, p in info.get("players", {}).items() if p.get("ai", "").startswith("FairAI")]
            logs = Path(run) / match.name.replace("match_", "logs_").replace(".json", "") / "VCMI_Client_log.txt"
            if not logs.exists():
                continue
            for color in colors:
                analyze_log(logs, color, stats)

    def avg(values):
        return sum(values) / len(values) if values else 0

    print(f"games analysed: {stats['games']}")
    print("\nbattles:")
    for key, n in sorted(stats["battles"].items()):
        print(f"  {key:28s} {n:5d}")
    print("\nlost defences (was the attacker known at the end of our turn?):")
    for key, n in sorted(stats["deaths"].items()):
        print(f"  {key:28s} {n:5d}")
    for kind, ratios in stats["death_ratio"].items():
        ratios = sorted(ratios)
        print(f"  attacker/defender strength, {kind}: median {ratios[len(ratios) // 2]:.1f}")
    print("\ntowns lost / won per week:")
    for week in sorted(set(stats["towns_lost_by_week"]) | set(stats["towns_won_by_week"])):
        print(f"  week {week}: lost {stats['towns_lost_by_week'][week]:3d}, won {stats['towns_won_by_week'][week]:3d}")
    print("\nat week end (average): towns / heroes / gold")
    for week in sorted(stats["towns_at_week_end"]):
        print(f"  week {week}: {avg(stats['towns_at_week_end'][week]):.2f} / {avg(stats['heroes_at_week_end'][week]):.2f} / {avg(stats['gold_at_week_end'][week]):.0f}")
    stalls = sorted(stats["longest_army_stall"])
    if stalls:
        print(f"\nlongest run of days without main army growth: median {stalls[len(stalls) // 2]}, max {stalls[-1]}")
    print("\nmain hero decisions (steps):")
    for key, n in stats["main_targets"].most_common():
        print(f"  {key:12s} {n:6d}")
    if stats["attack_losses"]:
        print("\nlost attacks:")
        for game, who, strength, other, enemy in stats["attack_losses"][:20]:
            print(f"  {game}: {who} ({strength}) vs {other} (enemy {enemy})")


if __name__ == "__main__":
    main()
