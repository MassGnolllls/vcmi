#!/usr/bin/env python3
"""What a switch does on the way to the result: key figures of the AI under test per configuration.

The score of a test (won 1, undecided 0.5, lost 0) says whether a switch helps; these figures say
how. They come from the timeline and the list of battles between players that every arena result
carries (clients built after 16.11.2026, 23:35).

  metrics.py FOLDER [FOLDER ...]                    one row per configuration
  metrics.py FOLDER [FOLDER ...] --factors '{"T": {"townWorth": 1}, "S": {"scouts": 2}}'
                                                    main effect of every switch on every figure

FOLDER holds one sub-folder per configuration and seed (ab_test.py / matrix.py) and ab_summary.md,
from which the parameters of each configuration are read (give --configs JSON when the summary is
missing or covers only a part of the folders).

Figures (ours unless marked "enemy"):
  score, won, lost        the result
  t42, t84, et84          towns on day 42 and 84, the enemy's on day 84
  main84, garr84, gold84  army of the strongest hero, army in towns without a hero, gold on day 84
  heroes84                heroes on day 84
  expl42                  share of the map explored on day 42 (per cent)
  gain, loss              towns gained and lost over the game (sum of the daily changes)
  free                    towns the enemy gained without a battle (its gains minus its won battles at our towns)
  gdef                    army lost in towns defended by the garrison alone (thousands)
  caught                  army lost by heroes attacked in the open (thousands)
  gate                    of that: lost in the gate of a town of ours whose garrison held another hero - the
                          game lets the attacker meet the hero in the gate outside the walls (thousands;
                          results written by clients built after 18.11.2026 11:00)
  gkill                   army value of map guards destroyed (thousands)
  days                    length of the game
"""
import argparse
import collections
import json
import math
import re
import statistics
import sys
from pathlib import Path

COLORS = ["red", "blue", "tan", "green", "orange", "purple", "teal", "pink"]
COLUMNS = ["army", "towns", "heroes", "income", "mines", "explored", "level", "gold", "dwellings", "guardFights", "guardLosses", "mainArmy", "garrisons"]
FIGURES = ["score", "won", "lost", "t42", "t84", "et84", "main84", "garr84", "gold84", "heroes84", "expl42", "gain", "loss", "free", "gdef", "caught", "gate", "gkill", "days"]
THOUSANDS = {"main84", "garr84", "gold84"}
# What FairAI's main hero spent its days on (results written by clients built after 18.11.2026 08:55 carry
# these statistics, by four-week phase): share of the days 29 to 84 by the kind most of its steps went to -
# own town (fetching troops), own hero, chasing an enemy hero, a foreign town, exploring, fighting guards or
# banks, other objects, safety moves, nothing -, share of days 29 to 112 on which exploring had stalled,
# heroes leading a second army and heroes holding a town's troops (mean per day, days 29 to 112), and the
# gold value of creatures left unbought per day (days 57 to 140) for want of a slot or of gold.
BEHAVIOUR = ["mOwnTown", "mOwnHero", "mChase", "mTown", "mExplore", "mFight", "mObject", "mSafety", "mNone", "stall", "leaders", "heavy", "noSlot", "noGold", "watch"]


def read_configs(folder: Path) -> dict:
    configs = {}
    summary = folder / "ab_summary.md"
    if not summary.exists():
        return configs
    for line in summary.read_text().splitlines():
        m = re.match(r"\|\s*([^|`]+?)\s*\|\s*`(\{.*?\})`\s*\|", line)
        if m:
            try:
                configs[m.group(1).strip()] = json.loads(m.group(2))
            except json.JSONDecodeError:
                pass
    return configs


def at(result, colour, day, column):
    rows = {e["day"]: e for e in result.get("timeline", [])}
    if not rows:
        return 0
    entry = rows.get(day) or rows[max(rows)]
    row = (entry.get("players") or {}).get(colour)
    return row[COLUMNS.index(column)] if row and len(row) > COLUMNS.index(column) else 0


def changes(result, colour):
    up = down = 0
    previous = None
    for entry in result.get("timeline", []):
        row = (entry.get("players") or {}).get(colour)
        if not row:
            continue
        if previous is not None:
            up += max(0, row[1] - previous)
            down += max(0, previous - row[1])
        previous = row[1]
    return up, down


def figures(record):
    result = record.get("result") or {}
    players = result.get("players") or {}
    mine = [c for c, p in players.items() if p.get("ai") == "FairAI"] if not record.get("mine") else list(record["mine"])
    if len(mine) != 1 or "timeline" not in result:
        return None
    me = mine[0]
    others = [c for c in players if c != me]
    if not others:
        return None
    enemy = others[0]
    status = players[me].get("status")
    won = result.get("winner") == me or status == "winner"
    lost = status == "loser"
    gain, loss = changes(result, me)
    enemy_gain = sum(changes(result, c)[0] for c in others)
    enemy_won_at_towns = 0
    garrison_lost = 0
    caught = 0
    gate = 0
    for b in result.get("playerBattles", []):
        day, att, dfn, a0, d0, alost, dlost, winner, town, dhero = b[:10]
        if COLORS[dfn] == me:
            if town:
                enemy_won_at_towns += 1 if winner == 0 else 0
                if not dhero:
                    garrison_lost += dlost
            else:
                caught += dlost
                gate += dlost if len(b) > 13 and b[13] else 0
    battles = (result.get("battles") or {}).get(me) or {}
    behaviour = {}
    own = (record.get("fairai") or {}).get(me)
    if own:
        def total(name, phases):
            return sum((own.get(str(p)) or {}).get(name, 0) for p in phases)

        def per_day(name, phases):
            days = total("days", phases)
            return total(name, phases) / days if days else None

        middle, longer, late = (1, 2), (1, 2, 3), (2, 3, 4)
        kinds = {"mOwnTown": ["main.ownTown"], "mOwnHero": ["main.ownHero"], "mChase": ["main.enemyHero"], "mTown": ["main.foreignTown"],
                 "mExplore": ["main.explore"], "mFight": ["main.monster", "main.bank"], "mObject": ["main.object"], "mSafety": ["main.safety"],
                 "mNone": ["main.none"]}
        if total("days", middle):
            for name, parts in kinds.items():
                behaviour[name] = sum(total(part, middle) for part in parts) / total("days", middle)
        for name, source, phases in (("stall", "stalled", longer), ("leaders", "leaders", longer), ("heavy", "heavy", longer),
                                     ("noSlot", "unboughtNoSlot", late), ("noGold", "unboughtNoGold", late), ("watch", "nightWatch", (1, 2, 3, 4))):
            value = per_day(source, phases)
            if value is not None:
                behaviour[name] = value
    return {
        "behaviour": behaviour,
        "score": 1.0 if won else (0.0 if lost else 0.5), "won": 1.0 if won else 0.0, "lost": 1.0 if lost else 0.0,
        "t42": at(result, me, 42, "towns"), "t84": at(result, me, 84, "towns"), "et84": at(result, enemy, 84, "towns"),
        "main84": at(result, me, 84, "mainArmy"), "garr84": at(result, me, 84, "garrisons"), "gold84": at(result, me, 84, "gold"),
        "heroes84": at(result, me, 84, "heroes"), "expl42": at(result, me, 42, "explored"),
        "gain": gain, "loss": loss, "free": max(0, enemy_gain - enemy_won_at_towns),
        "gdef": garrison_lost / 1000.0, "caught": caught / 1000.0, "gate": gate / 1000.0, "gkill": battles.get("guardKills", 0) / 1000.0,
        "days": result.get("day", 0),
    }


def load(folders, extra_configs):
    """{config name: {game key: figures}} and {config name: parameters}"""
    games = collections.defaultdict(dict)
    params = dict(extra_configs)
    for folder in folders:
        for name, value in read_configs(folder).items():
            params.setdefault(name, value)
        for results in sorted(folder.glob("*/results.jsonl")):
            name, _, seed = results.parent.name.rpartition("-s")
            for line in results.read_text().splitlines():
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                f = figures(record)
                if f is None:
                    continue
                key = (folder.name, seed, record.get("map"), tuple(record.get("mine") or []), record.get("match"))
                games[name][key] = f
    return games, params


def fmt(name, value):
    if name in THOUSANDS:
        return f"{value / 1000:.0f}k"
    if name in ("score", "won", "lost"):
        return f"{value:.3f}" if name == "score" else f"{value:.2f}"
    if name in ("gdef", "caught", "gate", "gkill"):
        return f"{value:.0f}k"
    if name in ("days", "expl42"):
        return f"{value:.0f}"
    if name in ("noSlot", "noGold"):
        return f"{value / 1000:.1f}k"
    return f"{value:.2f}"


def behaviour_tables(games, params, factors):
    """the same two tables for FairAI's own statistics, over the games that carry them"""
    rows_of = {name: [r["behaviour"] for r in rows.values() if r.get("behaviour")] for name, rows in games.items()}
    if not any(rows_of.values()):
        return

    def mean(rows, f):
        values = [r[f] for r in rows if f in r]
        return statistics.mean(values) if values else None

    print("\nWhat the main hero did (share of days 29-84 by kind), days stalled (29-112), heroes leading / holding a town (per day), unbought gold value per day (57-140):\n")
    print("| config | games | " + " | ".join(BEHAVIOUR) + " |")
    print("|---|---|" + "---|" * len(BEHAVIOUR))
    for name in sorted(games):
        rows = rows_of[name]
        if rows:
            print(f"| {name} | {len(rows)} | " + " | ".join("-" if mean(rows, f) is None else fmt(f, mean(rows, f)) for f in BEHAVIOUR) + " |")
    if not factors:
        return

    def is_on(config, wanted):
        return all(params[config].get(k) == v for k, v in wanted.items())

    print("\n| switch | games on/off | " + " | ".join(BEHAVIOUR) + " |")
    print("|---|---|" + "---|" * len(BEHAVIOUR))
    for factor, wanted in factors.items():
        on = [r for c in games if is_on(c, wanted) for r in rows_of[c]]
        off = [r for c in games if not is_on(c, wanted) for r in rows_of[c]]
        if not on or not off:
            continue
        cells = []
        for f in BEHAVIOUR:
            a = [r[f] for r in on if f in r]
            b = [r[f] for r in off if f in r]
            if len(a) < 2 or len(b) < 2:
                cells.append("-")
                continue
            effect = statistics.mean(a) - statistics.mean(b)
            half = 1.96 * math.sqrt(statistics.pvariance(a) / len(a) + statistics.pvariance(b) / len(b))
            mark = "**" if abs(effect) > half and half > 0 else ""
            if f in ("noSlot", "noGold"):
                cells.append(f"{mark}{effect / 1000:+.1f}k ±{half / 1000:.1f}{mark}")
            else:
                cells.append(f"{mark}{effect:+.2f} ±{half:.2f}{mark}")
        print(f"| {factor} | {len(on)}/{len(off)} | " + " | ".join(cells) + " |")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+", type=Path)
    ap.add_argument("--factors", default="")
    ap.add_argument("--configs", default="", help="JSON {name: parameters} for folders without (or with a partial) ab_summary.md")
    args = ap.parse_args()
    games, params = load(args.folders, json.loads(args.configs) if args.configs else {})
    if not games:
        print("no results with a timeline below", ", ".join(str(f) for f in args.folders))
        return 1

    print("| config | games | " + " | ".join(FIGURES) + " |")
    print("|---|---|" + "---|" * len(FIGURES))
    for name in sorted(games):
        rows = list(games[name].values())
        print(f"| {name} | {len(rows)} | " + " | ".join(fmt(f, statistics.mean(r[f] for r in rows)) for f in FIGURES) + " |")

    if not args.factors:
        behaviour_tables(games, params, None)
        return 0
    factors = json.loads(args.factors)
    missing = [name for name in games if name not in params]
    if missing:
        print("\nno parameters known for:", ", ".join(sorted(missing)), "(give --configs)")
        return 1

    def is_on(config, wanted):
        return all(params[config].get(k) == v for k, v in wanted.items())

    print("\nMain effects (switch on minus off; where both sides have the same games, paired by map and seat, with a 95 % interval):\n")
    print("| switch | games on/off | " + " | ".join(FIGURES) + " |")
    print("|---|---|" + "---|" * len(FIGURES))
    for factor, wanted in factors.items():
        on = [c for c in games if is_on(c, wanted)]
        off = [c for c in games if not is_on(c, wanted)]
        if not on or not off:
            continue
        n_on = sum(len(games[c]) for c in on)
        n_off = sum(len(games[c]) for c in off)
        cells = []
        for f in FIGURES:
            # per game (map, seat, seed): mean over the configurations with the switch on minus mean over those with it off
            per_key_on = collections.defaultdict(list)
            per_key_off = collections.defaultdict(list)
            for c in on:
                for key, row in games[c].items():
                    per_key_on[key[1:4]].append(row[f])
            for c in off:
                for key, row in games[c].items():
                    per_key_off[key[1:4]].append(row[f])
            common = [k for k in per_key_on if k in per_key_off]
            if len(common) >= 4:
                diffs = [statistics.mean(per_key_on[k]) - statistics.mean(per_key_off[k]) for k in common]
                effect = statistics.mean(diffs)
                half = 1.96 * statistics.stdev(diffs) / math.sqrt(len(diffs))
            else:
                a = [row[f] for c in on for row in games[c].values()]
                b = [row[f] for c in off for row in games[c].values()]
                effect = statistics.mean(a) - statistics.mean(b)
                half = 1.96 * math.sqrt(statistics.pvariance(a) / len(a) + statistics.pvariance(b) / len(b))
            scale = 1000.0 if f in THOUSANDS else 1.0
            unit = "k" if f in THOUSANDS or f in ("gdef", "caught", "gate", "gkill") else ""
            digits = 3 if f == "score" else (2 if f in ("won", "lost", "t42", "t84", "et84", "heroes84", "gain", "loss", "free") else 0)
            mark = "**" if abs(effect) > half and half > 0 else ""
            cells.append(f"{mark}{effect / scale:+.{digits}f}{unit} ±{half / scale:.{digits}f}{mark}")
        print(f"| {factor} | {n_on}/{n_off} | " + " | ".join(cells) + " |")
    behaviour_tables(games, params, factors)
    return 0


if __name__ == "__main__":
    sys.exit(main())
