#!/usr/bin/env python3
"""Why a side lost: the main reason of every defeat in a duel of two FairAI seats, and what each theme changed.

  duel_reasons.py FOLDER [FOLDER ...] [--json OUT.json] [--all]

FOLDER holds results.jsonl and the packed logs of all games (run_tournament.py play --keep-all-logs --ai-log debug).
With a duel plan (--duel-plan) the sides are compared theme by theme (on against off); without one the side
under test ("neu") against the other.

For every side of every game the log gives: the battle that decided it (both heroes, armies, spell points),
what the side held in total that day (heroes and towns), what its hero knew the evening before (known threat
against its defence), and a set of measures a theme is meant to move (troops left in towns, gold unspent,
spells of the main hero, hero level and guard fights early on ...).

Main reason of a defeat, first rule that applies:
  Eigener Angriff verloren   the loser attacked in its decisive battle
  Ohne Zauberpunkte          army at least 0.7 of the enemy's, under a quarter of its spell points, the enemy over half
  Held unterlegen            armies about level (0.85 or more), attack + defence at least 6 points lower
  Knapp verloren             armies about level, heroes too
  Gesehen, falsch reagiert   weaker in the field, and the evening before its hero knew a threat above its defence
                             (not counted when it stood in a town with less in total than the attacker brought)
  Geteilte Armee gestellt    weaker in the field, though heroes and towns together held more than the attacker brought
  Entwicklung verloren       weaker in total: the game was lost before the battle (or no single battle decided it)
The decisive battle of a side is the first one that cost it a quarter of all its troops (heroes and towns).
Games that ran into the day limit: "Gegner nie erreicht" (fewer than two big battles between heroes),
"Nicht beendet" (one side 1.5 times the other in total: for that side), else "Patt".
"""
import argparse
import json
import math
import re
import statistics
import tarfile
from collections import Counter, defaultdict
from pathlib import Path

DAY = re.compile(r"FairAI (red|blue) day (\d+)")
BATTLE_AT = re.compile(r"FairAI (red|blue) battle at \((\d+) (\d+) (\d+)\): (.*)")
SPELL = re.compile(r"FairAI (red|blue) battle spell: (our|enemy) (hero|creature) casts (.+?) on (\d+) units")
ENDS = re.compile(r"FairAI (red|blue): (.+?)( \(main\))? ends at \((\d+) (\d+) (\d+)\), strength (\d+), known threat (\d+) \(cautious (\d+)\), "
                  r"defence (\d+), mp (\d+)(, in town)?; level (\d+), A(\d+) D(\d+) P(\d+) K(\d+), mana (\d+)/(\d+)")
CAMPAIGN = re.compile(r"FairAI (red|blue) day (\d+): foreign towns known: .*campaign (\w+)")
SPELLS = re.compile(r"FairAI (red|blue) day (\d+): main hero (.+?) level (\d+), spells by level (\d+)/(\d+)/(\d+)/(\d+)/(\d+), wisdom (\d+)")
BIG = 100000
COLOURS = ("red", "blue")

REASONS = ["Eigener Angriff verloren", "Ohne Zauberpunkte", "Held unterlegen", "Knapp verloren", "Gesehen, falsch reagiert",
           "Geteilte Armee gestellt", "Entwicklung verloren", "Gegner nie erreicht", "Nicht beendet", "Patt"]

# what a theme is meant to move (the measures are computed for every side; these are shown with the theme)
AIMS = {
    "Truppen sammeln": ["garrisonAtClash", "garrison90", "caught"],
    "Zweite Armee": ["garrison90", "garrisonAtClash", "caught"],
    "Gegner suchen": ["explored100", "campaignDays", "attacksMade"],
    "Früher angreifen": ["campaignDays", "attacksMade", "attacksWonShare"],
    "Städte zählen": ["towns90", "towns150"],
    "Sicher stehen": ["threatNights", "caught"],
    "Gold nutzen": ["gold90", "gold150", "income90"],
    "Magie": ["spellLevel98", "highSpells147"],
    "Held": ["attackDefence100", "level100"],
    "Wege und Ziele": ["level42", "guardFights42", "income42"],
    "Kampf gegen Wachen": ["guardLossShare", "guardFights42"],
    "Gegner einschätzen": ["attacksWonShare", "attacksMade", "caught"],
    "Früh kämpfen": ["level42", "guardFights42", "army42"],
    "Reichweite": ["threatNights", "caught", "income90"],
    "Zauberwahl": ["manaAtClash", "castsPerGuardFight"],
    "neu": ["caught", "threatNights", "garrison90", "level42", "income90"],
}
LABELS = {
    "garrisonAtClash": "Truppenanteil in Städten vor der ersten großen Schlacht", "garrison90": "Truppenanteil in Städten an Tag 90",
    "caught": "große Armeen im Feld gestellt und geschlagen (Anzahl)", "explored100": "erkundet an Tag 100 (%)",
    "campaignDays": "Tage im Feldzug", "attacksMade": "große Schlachten als Angreifer", "attacksWonShare": "davon gewonnen (Anteil)",
    "towns90": "Städte an Tag 90", "towns150": "Städte an Tag 150", "threatNights": "Zugenden des Haupthelden unter stärkerer Bedrohung",
    "gold90": "Gold unverbaut an Tag 90", "gold150": "Gold unverbaut an Tag 150", "income90": "Einkommen an Tag 90",
    "spellLevel98": "höchste Zauberstufe des Haupthelden an Tag 98", "highSpells147": "Zauber der Stufen 4 und 5 an Tag 147",
    "attackDefence100": "Attack + Defense des Haupthelden an Tag 100", "level100": "Heldenstufe an Tag 100",
    "level42": "Heldenstufe an Tag 42", "guardFights42": "Wachenkämpfe bis Tag 42", "income42": "Einkommen an Tag 42",
    "army42": "Armee samt Städten an Tag 42", "guardLossShare": "Verlust je Wachenkampf (Anteil der Armee)",
    "manaAtClash": "Zauberpunkte vor großen Schlachten (Anteil)", "castsPerGuardFight": "Heldenzauber je Wachenkampf",
}


def hero_name(context: str) -> str:
    return context.split(" (", 1)[0] if context else ""


def scan_log(archive: Path):
    """One pass over a game's log: battles between the players, guard fights, the heroes' evenings, campaign days, spells."""
    tar = tarfile.open(archive)
    name = next(n for n in tar.getnames() if n.endswith("VCMI_Client_log.txt"))
    day = 0
    battle = None
    record_lines = None
    depth = 0
    last_row = {c: {} for c in COLOURS}   # hero name -> its latest "ends at"
    main_rows = {c: {} for c in COLOURS}  # day -> row of the main hero
    out = {"battles": [], "guards": {c: {"fights": 0, "lost": 0.0, "initial": 0.0, "casts": 0} for c in COLOURS},
           "campaignDays": {c: 0 for c in COLOURS}, "spells": {c: {} for c in COLOURS}, "threatNights": {c: 0 for c in COLOURS},
           "nights": {c: 0 for c in COLOURS}}

    def finish_record(text):
        nonlocal battle
        try:
            rec = json.loads(text)
        except ValueError:
            battle = None
            return
        sides = rec.get("sides") or []
        players = [s.get("player") for s in sides]
        spells = battle["spells"] if battle else {}
        context = battle["context"] if battle else {}
        if len(players) == 2 and all(p in COLOURS for p in players):
            entry = {"day": day, "attacker": players[0], "defender": players[1], "rounds": rec.get("rounds"),
                     "winner": players[0] if rec.get("winnerSide") == "attacker" else players[1], "sides": {}}
            for s in sides:
                c = s["player"]
                who = hero_name(context.get(c, ""))
                entry["sides"][c] = {"start": s.get("initialStrength") or 0, "lost": s.get("lostStrength") or 0, "hero": s.get("hero"),
                                     "name": who, "evening": dict(last_row[c].get(who) or {}),
                                     "casts": sum(1 for x in spells.get(c, []) if x[0] == "hero")}
            out["battles"].append(entry)
        else:
            for s in sides:
                c = s.get("player")
                if c in COLOURS:
                    g = out["guards"][c]
                    g["fights"] += 1
                    g["lost"] += s.get("lostStrength") or 0
                    g["initial"] += s.get("initialStrength") or 0
                    g["casts"] += sum(1 for x in spells.get(c, []) if x[0] == "hero")
        battle = None

    for raw in tar.extractfile(name):
        line = raw.decode("utf-8", "replace")
        if record_lines is not None:
            record_lines.append(line)
            depth += line.count("{") - line.count("}")
            if depth <= 0:
                finish_record("".join(record_lines))
                record_lines = None
            continue
        k = line.find("ARENA_BATTLE ")
        if k >= 0:
            text = line[k + 13:]
            depth = text.count("{") - text.count("}")
            if depth <= 0:
                finish_record(text)
            else:
                record_lines = [text]
            continue
        if "FairAI" not in line:
            continue
        m = ENDS.search(line)
        if m:
            c = m.group(1)
            row = {"day": day, "main": bool(m.group(3)), "strength": int(m.group(7)), "threat": int(m.group(8)), "defence": int(m.group(10)),
                   "mp": int(m.group(11)), "inTown": bool(m.group(12)), "level": int(m.group(13)), "A": int(m.group(14)), "D": int(m.group(15)),
                   "P": int(m.group(16)), "K": int(m.group(17)), "mana": int(m.group(18)), "manaMax": int(m.group(19))}
            last_row[c][m.group(2)] = row
            if row["main"]:
                main_rows[c][day] = row
                out["nights"][c] += 1
                if row["threat"] > row["defence"] and row["strength"] >= BIG:
                    out["threatNights"][c] += 1
            continue
        m = BATTLE_AT.search(line)
        if m:
            if battle is None:
                battle = {"context": {}, "spells": {}}
            battle["context"][m.group(1)] = m.group(5)
            continue
        m = SPELL.search(line)
        if m:
            if m.group(2) == "our":
                if battle is None:
                    battle = {"context": {}, "spells": {}}
                battle["spells"].setdefault(m.group(1), []).append((m.group(3), m.group(4)))
            continue
        m = CAMPAIGN.search(line)
        if m:
            day = int(m.group(2))
            if m.group(3) != "off":
                out["campaignDays"][m.group(1)] += 1
            continue
        m = SPELLS.search(line)
        if m:
            out["spells"][m.group(1)][int(m.group(2))] = [int(m.group(i)) for i in range(5, 10)]
            continue
        m = DAY.search(line)
        if m and "battle" not in line:
            day = int(m.group(2))
    out["mainRows"] = main_rows
    return out


def nearest(rows: dict, day: int, back: int = 7):
    for d in range(day, max(0, day - back) - 1, -1):
        if d in rows:
            return rows[d]
    return None


def analyse_game(rec: dict, archive: Path):
    """Two sides of a game: measures and (for a side that did not win) the main reason."""
    log = scan_log(archive)
    result = rec.get("result") or {}
    timeline = {c: {} for c in COLOURS}
    for entry in result.get("timeline") or []:
        for c, row in (entry.get("players") or {}).items():
            if c in timeline:
                timeline[c][int(entry.get("day"))] = row

    def total(c, day):
        row = nearest(timeline[c], day, 3)
        return (row[0] + row[12]) if row and len(row) > 12 else 0

    battles = log["battles"]
    big = [b for b in battles if all(b["sides"][c]["hero"] and b["sides"][c]["start"] >= BIG for c in COLOURS)]
    last_day = int(result.get("day") or 0)
    decided = result.get("outcome") == "victory" and result.get("winner") in COLOURS
    sides = {}
    for c in COLOURS:
        foe = "blue" if c == "red" else "red"
        tl = timeline[c]

        def at(day, index, default=None):
            row = nearest(tl, day, 0)
            return row[index] if row and len(row) > index and row[1] > 0 else default

        m = {}
        clash = big[0] if big else None
        if clash:
            row = nearest(tl, clash["day"] - 1, 3)
            if row and row[0] + row[12] > 0:
                m["garrisonAtClash"] = row[12] / (row[0] + row[12])
        row = nearest(tl, 90, 0)
        if row and row[1] > 0 and row[0] + row[12] > 0:
            m["garrison90"] = row[12] / (row[0] + row[12])
        m["caught"] = sum(1 for b in battles if b["defender"] == c and b["winner"] != c and b["sides"][c]["hero"]
                          and b["sides"][c]["start"] >= BIG and not b["sides"][c]["evening"].get("inTown"))
        m["explored100"] = at(100, 5)
        m["campaignDays"] = log["campaignDays"][c]
        made = [b for b in big if b["attacker"] == c]
        m["attacksMade"] = len(made)
        if made:
            m["attacksWonShare"] = sum(1 for b in made if b["winner"] == c) / len(made)
        m["towns90"], m["towns150"] = at(90, 1), at(150, 1)
        m["threatNights"] = log["threatNights"][c]
        m["gold90"], m["gold150"], m["income90"] = at(90, 7), at(150, 7), at(90, 3)
        spells = log["spells"][c]
        if 98 in spells:
            m["spellLevel98"] = max((i + 1 for i, n in enumerate(spells[98]) if n > 0), default=0)
        if 147 in spells:
            m["highSpells147"] = spells[147][3] + spells[147][4]
        main100 = nearest(log["mainRows"][c], 100, 5)
        if main100:
            m["attackDefence100"] = main100["A"] + main100["D"]
            m["level100"] = main100["level"]
        m["level42"], m["guardFights42"], m["income42"] = at(42, 6), at(42, 9), at(42, 3)
        row = nearest(tl, 42, 0)
        if row and row[1] > 0:
            m["army42"] = row[0] + row[12]
        g = log["guards"][c]
        if g["initial"] > 0:
            m["guardLossShare"] = g["lost"] / g["initial"]
        if g["fights"]:
            m["castsPerGuardFight"] = g["casts"] / g["fights"]
        shares = [b["sides"][c]["hero"].get("manaStart", 0) / max(1, b["sides"][c]["hero"].get("manaMax", 0)) for b in big
                  if b["sides"][c]["hero"].get("manaMax", 0) >= 20]
        if shares:
            m["manaAtClash"] = statistics.mean(shares)

        state = "open"
        if decided:
            state = "won" if result["winner"] == c else "lost"
        side = {"colour": c, "state": state, "measures": {k: v for k, v in m.items() if v is not None}, "reason": None, "detail": {}}

        if state == "lost":
            # the battle that turned the game: the first one that cost this side a quarter of all it had
            # (heroes and towns) - later ones are the end of a game already lost
            fought = [b for b in battles if b["sides"][c]["lost"] > 0]
            turning = [b for b in fought if b["sides"][c]["lost"] >= 0.25 * max(total(c, b["day"] - 1), b["sides"][c]["start"], 1)]
            if not turning:
                side["reason"] = "Entwicklung verloren"
                side["detail"] = {"attrition": True}
            else:
                b = turning[0]
                own, their = b["sides"][c], b["sides"][foe]
                ratio = own["start"] / max(1, their["start"])
                own_total = max(total(c, b["day"] - 1), own["start"])
                hero, enemy = own["hero"] or {}, their["hero"] or {}
                gap = sum((enemy.get("primary") or [0, 0])[:2]) - sum((hero.get("primary") or [0, 0])[:2])
                own_mana = hero.get("manaStart", 0) / max(1, hero.get("manaMax", 0)) if hero.get("manaMax", 0) >= 20 else 1.0
                enemy_mana = enemy.get("manaStart", 0) / max(1, enemy.get("manaMax", 0)) if enemy.get("manaMax", 0) >= 20 else 0.0
                evening = own["evening"]
                seen = bool(evening) and evening.get("threat", 0) > evening.get("defence", 0) and b["defender"] == c
                detail = {"day": b["day"], "attacked": b["defender"] == c, "ratio": round(ratio, 2), "ownField": own["start"], "enemyField": their["start"],
                          "ownTotal": own_total, "heroGap": gap, "ownMana": round(own_mana, 2), "enemyMana": round(enemy_mana, 2), "seen": seen,
                          "split": own_total >= their["start"], "hero": own["name"], "mp": evening.get("mp"), "inTown": evening.get("inTown")}
                if b["attacker"] == c:
                    reason = "Eigener Angriff verloren"
                elif ratio >= 0.7 and own_mana < 0.25 and enemy_mana >= 0.5:
                    reason = "Ohne Zauberpunkte"
                elif ratio >= 0.85:
                    reason = "Held unterlegen" if (gap >= 6 or not hero) else "Knapp verloren"
                elif own_total < their["start"] and (evening.get("inTown") or not seen):
                    reason = "Entwicklung verloren"  # weaker with everything it had, and nowhere better to stand
                elif seen:
                    reason = "Gesehen, falsch reagiert"
                elif own_total >= their["start"]:
                    reason = "Geteilte Armee gestellt"
                else:
                    reason = "Entwicklung verloren"
                side["reason"], side["detail"] = reason, detail
        elif state == "open":
            mine_total, their_total = total(c, last_day), total(foe, last_day)
            if len(big) < 2:
                side["reason"] = "Gegner nie erreicht"
            elif mine_total >= 1.5 * max(1, their_total):
                side["reason"] = "Nicht beendet"
            elif their_total >= 1.5 * max(1, mine_total):
                side["reason"] = None  # behind and alive: no defeat, and not this side's failure to finish
            else:
                side["reason"] = "Patt"
            side["detail"] = {"ownTotal": mine_total, "enemyTotal": their_total, "bigBattles": len(big)}
        sides[c] = side
    return sides


def mean_half(values):
    values = [v for v in values if v is not None]
    if len(values) < 2:
        return (values[0] if values else float("nan")), float("nan"), len(values)
    return statistics.mean(values), 1.96 * statistics.stdev(values) / math.sqrt(len(values)), len(values)


def fmt(value):
    if value != value:
        return "–"
    if abs(value) >= 1000:
        return f"{value / 1000:.0f}k"
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.2f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folders", nargs="+")
    parser.add_argument("--json", default="")
    parser.add_argument("--all", action="store_true", help="every measure for every theme, not only those a theme aims at and those that moved")
    args = parser.parse_args()

    rows = []  # one per side
    for folder in map(Path, args.folders):
        file = folder / "results.jsonl"
        if not file.exists():
            continue
        for line in file.read_text().splitlines():
            if not line.strip():
                continue
            rec = json.loads(line)
            archive = next(iter(folder.glob(f"*_{rec['match']:04d}.tar.xz")), None)
            if archive is None or rec.get("score") is None:
                continue
            sides = analyse_game(rec, archive)
            mine = rec.get("mine") or []
            plan = rec.get("plan")
            for colour, side in sides.items():
                if plan:
                    on = plan["mineOn"] if colour in mine else plan["otherOn"]
                    themes = {name: name in on for name in plan["mineOn"] + plan["otherOn"]}
                else:
                    themes = {"neu": colour in mine}
                side.update({"folder": folder.name, "match": rec["match"], "map": rec["map"], "themes": themes})
                rows.append(side)
    if not rows:
        print("no games with logs found")
        return 1
    if args.json:
        Path(args.json).write_text(json.dumps(rows, ensure_ascii=False))

    games = len(rows) // 2
    print(f"{games} Partien, {len(rows)} Seiten: gewonnen {sum(r['state'] == 'won' for r in rows)}, verloren {sum(r['state'] == 'lost' for r in rows)}, "
          f"offen {sum(r['state'] == 'open' for r in rows)}\n")
    overall = Counter(r["reason"] for r in rows if r["reason"])
    print("## Gründe (alle Seiten)\n")
    for reason in REASONS:
        if overall[reason]:
            print(f"- {reason}: {overall[reason]}")
    print()

    seen = [r["detail"] for r in rows if r["reason"] == "Gesehen, falsch reagiert"]
    if seen:
        field = [d for d in seen if not d.get("inTown")]
        print(f"Gesehen, falsch reagiert ({len(seen)}): im Feld {len(field)} (davon mit weniger als 600 Bewegungspunkten {sum((d.get('mp') or 0) < 600 for d in field)}), "
              f"in einer Stadt {len(seen) - len(field)}; insgesamt mehr Truppen als der Angreifer mitbrachte: {sum(bool(d.get('split')) for d in seen)}\n")

    themes = list(rows[0]["themes"])
    shown = [r for r in REASONS if overall[r]]
    print("## Gründe je Thema: Seiten mit dem Thema an / aus\n")
    print("| Thema | gewonnen | verloren | " + " | ".join(shown) + " |")
    print("|---|---|---|" + "---|" * len(shown))
    for theme in themes:
        on = [r for r in rows if r["themes"].get(theme)]
        off = [r for r in rows if not r["themes"].get(theme)]
        cells = [f"{sum(r['state'] == 'won' for r in on)} / {sum(r['state'] == 'won' for r in off)}",
                 f"{sum(r['state'] == 'lost' for r in on)} / {sum(r['state'] == 'lost' for r in off)}"]
        for reason in shown:
            cells.append(f"{sum(r['reason'] == reason for r in on)} / {sum(r['reason'] == reason for r in off)}")
        print(f"| {theme} | " + " | ".join(cells) + " |")
    print()

    print("## Zielmaße je Thema: Mittel an / aus, Unterschied (95 %)\n")
    measures = sorted({k for r in rows for k in r["measures"]})
    for theme in themes:
        on = [r for r in rows if r["themes"].get(theme)]
        off = [r for r in rows if not r["themes"].get(theme)]
        lines = []
        for measure in measures:
            a, ha, na = mean_half([r["measures"].get(measure) for r in on])
            b, hb, nb = mean_half([r["measures"].get(measure) for r in off])
            if na < 2 or nb < 2:
                continue
            half = math.sqrt(ha * ha + hb * hb)
            aimed = measure in AIMS.get(theme, [])
            moved = half > 0 and abs(a - b) > half
            if aimed or args.all or moved:
                mark = " ←" if aimed else ""
                star = " *" if moved else ""
                lines.append(f"  - {LABELS.get(measure, measure)}: {fmt(a)} / {fmt(b)}, {'+' if a >= b else '−'}{fmt(abs(a - b))} ± {fmt(half)}{star}{mark}")
        print(f"- **{theme}**")
        print("\n".join(lines) if lines else "  - (keine Maße)")
    print("\n← Zielmaß des Themas, * Unterschied größer als sein 95-%-Bereich")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
