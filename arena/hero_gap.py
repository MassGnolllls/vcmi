"""hero_gap.py JOBDIR... - the leading heroes of both sides compared over all games of the given jobs.

For every game: the hero leading the strongest army of each side in the 30 days before day 60, 90, 120
and at the largest battle between the two players. Splits attack/defence/power/knowledge into the part from
artifacts and the rest (class, level-ups, places on the map)."""
import glob, json, os, re, statistics, sys, tarfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vjson

NAMES = ["attack", "defence", "spellpower", "knowledge"]
def artifact_table():
    table = {}
    def take(data, prefix):
        for key, value in data.items():
            if not isinstance(value, dict):
                continue
            bonuses = value.get("bonuses")
            if bonuses is None:
                table.setdefault((prefix + key.split(":")[-1]).lower(), [0, 0, 0, 0])
                continue
            row = [0, 0, 0, 0]
            items = bonuses.values() if isinstance(bonuses, dict) else bonuses
            for b in items:
                if isinstance(b, dict) and b.get("type") == "PRIMARY_SKILL":
                    sub = str(b.get("subtype", "")).split(".")[-1].lower()
                    for i, n in enumerate(NAMES):
                        if sub == n or (n == "spellpower" and sub in ("spellpower", "power")):
                            row[i] += int(b.get("val", 0))
            table[(prefix + key.split(":")[-1]).lower()] = row
    take(vjson.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "config", "artifacts.json")), "")
    for path in glob.glob(os.path.join(os.environ.get("FAI_HOTA_MODS", "/home/claude/dl/hota-1.7/mods"), "**", "*.json"), recursive=True):
        if "textPolishing" in path or "Description" in path or "rtifact" not in path:
            continue
        try:
            take(vjson.load(path), "")
        except Exception:
            pass
    return table
ART = artifact_table()

def art_points(hero):
    total = [0, 0, 0, 0]
    unknown = 0
    for a in hero.get("artifacts", []):
        row = ART.get(a.split(":")[-1].lower())
        if row is None:
            unknown += 1
            continue
        total = [x + y for x, y in zip(total, row)]
    return total, unknown

def battles(log):
    day = 0
    lines = log.split("\n")
    i = 0
    while i < len(lines):
        l = lines[i]
        m = re.search(r"FairAI (?:red|blue) day (\d+)", l)
        if m:
            day = int(m.group(1))
        if "ARENA_BATTLE {" in l:
            buf = ["{"]
            i += 1
            while i < len(lines) and not lines[i].startswith("}"):
                buf.append(lines[i])
                i += 1
            buf.append("}")
            try:
                yield day, json.loads("\n".join(buf))
            except Exception:
                pass
        i += 1

def side_of(s):
    return "fair" if s["battleAI"] == "FairAI" else "other"

snap = {k: {"fair": [], "other": []} for k in ("d60", "d90", "d120", "big", "big won", "big lost", "big open")}
pairs = {"won": [], "lost": [], "open": []}
arts_seen = {"fair": {}, "other": {}}
games = 0
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "*.tar.xz"))):
        with tarfile.open(path) as tar:
            member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
            if member is None:
                continue
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        games += 1
        records = {"fair": [], "other": []}
        big = None
        for day, j in battles(log):
            sides = j["sides"]
            for s in sides:
                if s["player"] != "neutral" and "hero" in s:
                    records[side_of(s)].append((day, s["initialStrength"], s["hero"]))
            if all(s["player"] != "neutral" and "hero" in s for s in sides) and side_of(sides[0]) != side_of(sides[1]):
                size = min(s["initialStrength"] for s in sides)
                if big is None or size > big[0]:
                    big = (size, {side_of(s): s["hero"] for s in sides})
        for key, limit in (("d60", 60), ("d90", 90), ("d120", 120)):
            for who in ("fair", "other"):
                window = [r for r in records[who] if limit - 30 < r[0] <= limit]
                if window:
                    snap[key][who].append(max(window, key=lambda r: r[1])[2])
        if big:
            outcome = os.path.basename(path).split("_")[0]
            if outcome in pairs:
                f, o = big[1]["fair"], big[1]["other"]
                fa, oa = art_points(f)[0], art_points(o)[0]
                pairs[outcome].append([f["level"] - o["level"]] + [a - b for a, b in zip(f["primary"], o["primary"])] + [sum(fa) - sum(oa)]
                                      + [(sum(f["primary"]) - sum(fa) - f["level"]) - (sum(o["primary"]) - sum(oa) - o["level"])])
            for who in ("fair", "other"):
                snap["big"][who].append(big[1][who])
                if outcome in pairs:
                    snap["big " + outcome][who].append(big[1][who])
                for a in set(big[1][who].get("artifacts", [])):
                    arts_seen[who][a] = arts_seen[who].get(a, 0) + 1

def med(values):
    return statistics.median(values) if values else float("nan")

print(f"{games} games")
print("when  side   n  level |  A    D    P    K  | from artifacts A D P K | artifacts | rest beyond level | morale luck mana spells")
for key in ("d60", "d90", "d120", "big", "big won", "big lost", "big open"):
    for who in ("fair", "other"):
        heroes = snap[key][who]
        if not heroes:
            continue
        prim = [h["primary"] for h in heroes]
        arts = [art_points(h)[0] for h in heroes]
        count = [len([a for a in h.get("artifacts", []) if not a.endswith(("catapult", "ballista", "ammoCart", "firstAidTent", "spellBook", "cannon"))]) for h in heroes]
        rest = [sum(p) - sum(a) - (h["level"] - 1) for p, a, h in zip(prim, arts, heroes)]
        print(f"{key:8s} {who:5s} {len(heroes):3d}  {med([h['level'] for h in heroes]):5.1f} | "
              + " ".join(f"{med([p[i] for p in prim]):4.1f}" for i in range(4)) + " | "
              + " ".join(f"{med([a[i] for a in arts]):4.1f}" for i in range(4)) + f" | {med(count):5.1f}     | {med(rest):5.1f}             | "
              + f"{med([h.get('morale', 0) for h in heroes]):4.1f} {med([h.get('luck', 0) for h in heroes]):4.1f} {med([h.get('manaMax', 0) for h in heroes]):5.0f} {med([len(h.get('spellbook', [])) for h in heroes]):4.1f}")
skills = {}
for who in ("fair", "other"):
    for h in snap["big"][who]:
        for k, v in (h.get("secondary") or {}).items():
            skills.setdefault(k.split(":")[-1], {"fair": [], "other": []})[who].append(v)
n = {who: max(1, len(snap["big"][who])) for who in ("fair", "other")}
print("skills at the largest battle (share of heroes having it / of those at expert):")
for k, v in sorted(skills.items(), key=lambda kv: -(len(kv[1]["fair"]) + len(kv[1]["other"]))):
    print(f"  {k:14s} fair {len(v['fair']) / n['fair']:4.0%} ({sum(1 for x in v['fair'] if x == 3) / n['fair']:4.0%})   other {len(v['other']) / n['other']:4.0%} ({sum(1 for x in v['other'] if x == 3) / n['other']:4.0%})")
print("unknown artifacts:", sum(art_points(h)[1] for who in ("fair", "other") for h in snap["big"][who]))

print("paired difference FairAI minus opponent at the largest battle (median): level, A, D, P, K, artifact points, points from places")
for outcome, rows in pairs.items():
    if rows:
        print(f"  {outcome:5s} n={len(rows):2d}: " + "  ".join(f"{med([r[i] for r in rows]):+5.1f}" for i in range(7)))
top = {who: sorted(arts_seen[who].items(), key=lambda kv: -kv[1])[:0] for who in arts_seen}
