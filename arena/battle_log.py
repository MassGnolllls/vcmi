#!/usr/bin/env python3
"""Where does an AI lose its armies? Battles between players, from arena results.

Results written by a client built after 16.11.2026 (23:00) list every battle between players
(key playerBattles). This script sorts the battles of the AI under test:

  battle_log.py RESULTS.jsonl [more.jsonl ...]     many games (one line per configuration folder)
  battle_log.py RESULT.json --list                 one game: every battle

Columns of the summary, per game on average:
  lost main    army value lost in battles where our army was at least half of all we had
  lost parts   ... where it was a smaller part (secondary heroes, garrisons): armies beaten one by one
  ... split by where the part stood (town with hero, town garrison alone, in the field)
  killed       enemy army value destroyed in all battles
  lvl          hero levels in the battles of the two main armies (ours : theirs)
"""
import argparse
import collections
import json
import sys
from pathlib import Path

COLORS = ["red", "blue", "tan", "green", "orange", "purple", "teal", "pink"]


def load(path: Path):
    text = path.read_text()
    if path.suffix == ".jsonl":
        return [json.loads(line) for line in text.splitlines() if line.strip()]
    data = json.loads(text)
    return [{"result": data, "mine": []}] if "players" in data else [data]


def army_on(result, color, day):
    """the player's whole army value on that day (timeline: one entry per day, army first)"""
    for entry in result.get("timeline", []):
        if entry["day"] == day:
            return entry["players"].get(color, [0])[0]
    timeline = result.get("timeline", [])
    return timeline[-1]["players"].get(color, [0])[0] if timeline else 0


def battles_of(record):
    """battles of the AI under test: dicts with our view of each"""
    result = record.get("result") or {}
    mine = set(record.get("mine") or [])
    out = []
    for b in result.get("playerBattles", []):
        day, att, dfn, a0, d0, alost, dlost, winner, town, dhero, alvl, dlvl = b[:12]
        how = b[12] if len(b) > 12 else 0
        for side, color in ((0, COLORS[att]), (1, COLORS[dfn])):
            if mine and color not in mine:
                continue
            own0, their0 = (a0, d0) if side == 0 else (d0, a0)
            ownlost, theirlost = (alost, dlost) if side == 0 else (dlost, alost)
            total = max(army_on(result, color, day), own0, 1)
            out.append({
                "day": day, "attacking": side == 0, "won": winner == side, "own": own0, "enemy": their0,
                "lost": ownlost, "killed": theirlost, "town": bool(town), "hero": (alvl > 0) if side == 0 else bool(dhero),
                "level": alvl if side == 0 else dlvl, "enemyLevel": dlvl if side == 0 else alvl,
                "share": own0 / total, "how": how, "color": color,
            })
    return out


def k(value: float) -> str:
    return f"{value / 1000:.0f}k"


def summary(name, records):
    games = [r for r in records if (r.get("result") or {}).get("playerBattles") is not None]
    if not games:
        return None
    n = len(games)
    acc = collections.Counter()
    levels = []
    for record in games:
        for b in battles_of(record):
            acc["fights"] += 1
            acc["killed"] += b["killed"]
            main = b["share"] >= 0.5
            if main:
                acc["lostMain"] += b["lost"]
                if b["level"] and b["enemyLevel"] and b["enemy"] >= 0.3 * b["own"]:
                    levels.append((b["level"], b["enemyLevel"]))
            else:
                acc["lostParts"] += b["lost"]
                if not b["won"]:
                    acc["partsBeaten"] += 1
                    where = "partsTownHero" if b["town"] and b["hero"] and not b["attacking"] else (
                        "partsGarrison" if b["town"] and not b["attacking"] else "partsField")
                    acc[where] += b["lost"]
            if not b["won"] and main:
                acc["mainBeaten"] += 1
    lvl = f"{sum(a for a, _ in levels) / len(levels):.0f}:{sum(b for _, b in levels) / len(levels):.0f}" if levels else "-"
    return (f"| {name} | {n} | {acc['fights'] / n:.0f} | {k(acc['killed'] / n)} | {k(acc['lostMain'] / n)} | {acc['mainBeaten'] / n:.1f} | "
            f"{k(acc['lostParts'] / n)} | {acc['partsBeaten'] / n:.1f} | {k(acc['partsTownHero'] / n)} | {k(acc['partsGarrison'] / n)} | "
            f"{k(acc['partsField'] / n)} | {lvl} |")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--list", action="store_true", help="every battle of the first file's games")
    args = ap.parse_args()
    if args.list:
        for record in load(Path(args.files[0])):
            print("| day | role | own army | share | enemy | lost | killed | where | levels | result |")
            print("|---|---|---|---|---|---|---|---|---|---|")
            for b in battles_of(record):
                where = ("town" if b["town"] else "field") + ("" if b["hero"] else ", no hero")
                print(f"| {b['day']} | {b['color']} {'attacks' if b['attacking'] else 'defends'} | {k(b['own'])} | {b['share']:.0%} | {k(b['enemy'])} | "
                      f"{k(b['lost'])} | {k(b['killed'])} | {where} | {b['level']}:{b['enemyLevel']} | "
                      f"{'won' if b['won'] else 'lost'}{['', ' (escape)', ' (surrender)'][b['how']]} |")
        return 0
    print("| games of | n | fights | killed | lost main | main beaten | lost parts | parts beaten | in town with hero | garrison alone | in the field | lvl |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    shown = 0
    for name in args.files:
        path = Path(name)
        line = summary(path.parent.name if path.name == "results.jsonl" else path.name, load(path))
        if line:
            print(line)
            shown += 1
    if not shown:
        print("no battle lists in these results (needs a client built after 16.11.2026 23:00)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
