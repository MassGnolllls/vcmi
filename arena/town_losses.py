"""town_losses.py JOBDIR... - every town FairAI lost in the lost and open games: what attacked, what stood there,
where the main hero was (tiles from the town, same level or not), how strong it was and what it was after."""
import collections, glob, json, os, re, statistics, sys, tarfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_falls as tf

def foreign(days, d):
    for n in days.get(d, {"notes": []})["notes"]:
        if "foreign towns known" in n:
            return set(re.findall(r"[a-z]+\((\d+ \d+ \d+)\)", n.split("towns unfound")[0])), n
    return None, ""

rows_out = []
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "lost_*.tar.xz")) + glob.glob(os.path.join(job, "open_*.tar.xz"))):
        number = re.search(r"_(\d+)\.tar", path).group(1)
        if not os.path.exists(os.path.join(job, f"match_{number}.json")):
            continue
        match = json.load(open(os.path.join(job, f"match_{number}.json")))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        other = next(c for c in match["players"] if c != colour)
        me_idx = ["red", "blue", "tan", "green"].index(colour)
        rows = {t["day"]: t["players"] for t in match["timeline"]}
        with tarfile.open(path) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            days = tf.parse(tar.extractfile(member).read().decode("utf-8", "replace"), colour)
        towns = {d: rows[d][colour][1] for d in rows if colour in rows[d]}
        tag = f"{os.path.basename(job)[:4]} g{int(number)} {os.path.basename(match['map'])[7:-12][:4]}"
        for d in sorted(towns):
            if d - 1 not in towns or towns[d] >= towns[d - 1]:
                continue
            # the town that fell: a foreign town that is new in our list
            before, _ = foreign(days, d - 2)
            if before is None:
                before, _ = foreign(days, d - 3)
            after, _ = foreign(days, d)
            if after is None:
                after, _ = foreign(days, d + 1)
            new = sorted((after or set()) - (before or set()))
            main = None
            for back in (1, 2):
                if days.get(d - back) and days[d - back]["main"]:
                    main = days[d - back]
                    break
            fights = [b for b in match["playerBattles"] if d - 1 <= b[0] <= d and b[2] == me_idx and b[8]]
            att = max((b[3] for b in fights), default=0)
            held = max((b[4] for b in fights), default=0)
            r_me, r_other = rows[d - 1][colour], rows[d - 1][other]
            dist, level = None, None
            if main and new:
                t = tuple(int(x) for x in new[0].split())
                p = main["main"]["pos"]
                dist = max(abs(p[0] - t[0]), abs(p[1] - t[1]))
                level = p[2] == t[2]
            opp = tf.note(days.get(d - 1, {"notes": []}), "opposition heroes", 400)
            m = re.search(r"opposition heroes (\d+) \((.+?), seen (\d+) days ago: army (\d+) x hero ([\d.]+)", opp)
            mf = re.search(r"main hero strength \d+ \(army (\d+), in the field (\d+)\)", tf.note(days.get(d - 1, {"notes": []}), "opposition heroes", 460))
            top = main["targets"].most_common(1)[0][0] if main and main["targets"] else "-"
            rows_out.append(dict(tag=tag, day=d, left=towns[d], att=att, held=held, main_army=r_me[11], enemy_main=r_other[11],
                                 main_strength=main["main"]["strength"] if main else 0, threat=main["main"]["threat"] if main else 0,
                                 dist=dist, level=level, target=top.split(" (")[0], in_town=main["main"]["in_town"] if main else False,
                                 our_field=int(mf.group(2)) if mf else 0, our_army=int(mf.group(1)) if mf else 0, enemy_field=int(m.group(1)) if m else 0, enemy_factor=float(m.group(5)) if m else 0, seen=int(m.group(3)) if m else -1))
print("game            day left | attacker   in town | our main army (field)  enemy main (field, factor, seen) | main: tiles away, level, known threat, was after")
for r in rows_out:
    where = "?" if r["dist"] is None else f"{r['dist']:3d} {'same' if r['level'] else 'OTHER'}"
    print(f"{r['tag']:15s} {r['day']:3d} {r['left']:2d}   | {r['att']:8d} {r['held']:8d} | {r['main_army']:8d} ({r['main_strength']:8d})  {r['enemy_main']:8d} ({r['enemy_field']:8d}, {r['enemy_factor']:.2f}, {r['seen']:2d}d) | {where:9s} threat {r['threat']:8d} {r['target'][:16]}{' [in town]' if r['in_town'] else ''}")
n = len(rows_out)
empty = sum(1 for r in rows_out if r["held"] < 0.1 * max(1, r["att"]) or r["att"] == 0)
stronger = sum(1 for r in rows_out if r["main_army"] > 1.2 * r["att"] and r["att"] > 0)
far = [r for r in rows_out if r["dist"] is not None and (not r["level"] or r["dist"] > 20)]
print(f"{n} town losses; garrison under a tenth of the attacker or no battle: {empty}; our main army over 1.2x the attacker's: {stronger} of {sum(1 for r in rows_out if r['att'] > 0)} with a battle")
known = [r for r in rows_out if r["dist"] is not None]
print(f"main hero's place known for {len(known)}: other level {sum(1 for r in known if not r['level'])}, same level over 20 tiles {sum(1 for r in known if r['level'] and r['dist'] > 20)}, within 20 tiles {sum(1 for r in known if r['level'] and r['dist'] <= 20)}")
print("what the main hero was after the day before:", collections.Counter(r["target"] for r in rows_out).most_common(8))
print(f"main hero's known threat the evening before was 0 in {sum(1 for r in rows_out if r['threat'] == 0)} of {n}")
weaker = sum(1 for r in rows_out if r["enemy_field"] > r["main_strength"] > 0)
print(f"the strongest enemy hero we knew counted more than our main hero (field strength) in {weaker} of {n}")

import statistics as st
ok = [r for r in rows_out if r["our_army"] > 100000 and r["enemy_factor"] > 0]
print(f"model: our hero factor median {st.median(r['our_field'] / r['our_army'] for r in ok):.2f}, enemy's {st.median(r['enemy_factor'] for r in ok):.2f}; our field / enemy's field median {st.median(r['our_field'] / max(1, r['enemy_field']) for r in ok):.2f} (n={len(ok)})")
print("  we counted more than the enemy:", sum(1 for r in ok if r["our_field"] > r["enemy_field"]), "; at least 1.3x:", sum(1 for r in ok if r["our_field"] >= 1.3 * r["enemy_field"]), "; enemy at least 1.3x us:", sum(1 for r in ok if r["enemy_field"] >= 1.3 * r["our_field"]))
for r in rows_out:
    r["ratio"] = round(r["our_field"] / max(1, r["enemy_field"]), 2)
print("  per loss our field / enemy field:", " ".join(f"{r['ratio']:.1f}" for r in ok))
