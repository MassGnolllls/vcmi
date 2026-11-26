"""warning_time.py JOBDIR... - before each town FairAI lost: where the attacker really was on the evenings before
(from the opponent's own move log), where our main hero was, and whether FairAI had the attacker in sight."""
import collections, glob, json, os, re, statistics, sys, tarfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_falls as tf
import town_losses_lib as tl

def enemy_tracks(log):
    day = 0
    pos = collections.defaultdict(dict)   # hero -> day -> position at the end of that day
    for line in log.split("\n"):
        if "starting turn, day" in line:
            m = re.search(r"starting turn, day (\d+)", line)
            if m:
                day = int(m.group(1))
        elif "Hero " in line and " moved from (" in line:
            m = re.search(r"Hero (.+?) moved from \((\d+) (\d+) (\d+)\) to \((\d+) (\d+) (\d+)\)", line)
            if m:
                pos[m.group(1)][day] = (int(m.group(5)), int(m.group(6)), int(m.group(7)))
    return pos

def at(track, day):
    best = None
    for d in sorted(track):
        if d <= day:
            best = track[d]
    return best

def dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1])) + (1000 if a[2] != b[2] else 0)

rows = []
explored = []
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "lost_*.tar.xz")) + glob.glob(os.path.join(job, "open_*.tar.xz"))):
        number = re.search(r"_(\d+)\.tar", path).group(1)
        mfile = os.path.join(job, f"match_{number}.json")
        if not os.path.exists(mfile):
            continue
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        other = next(c for c in match["players"] if c != colour)
        timeline = {t["day"]: t["players"] for t in match["timeline"]}
        with tarfile.open(path) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        days = tf.parse(log, colour)
        tracks = enemy_tracks(log)
        towns = {d: timeline[d][colour][1] for d in timeline if colour in timeline[d]}
        first = True
        for d in sorted(towns):
            if d - 1 not in towns or towns[d] >= towns[d - 1]:
                continue
            town = tl.fallen_town(days, d)
            if first:
                explored.append((timeline[d - 1][colour][5], timeline[d - 1][other][5]))
                first = False
            if town is None:
                continue
            # the hero of the opponent that stood on the town's gate on the day it fell
            attacker, entered = None, None
            named = re.search(r"\((.+?), seen (\d+) days ago", tf.note(days.get(d - 1, {"notes": []}), "opposition heroes", 460))
            named = named.group(1) if named else None
            for dd in (d, d - 1):
                for name, track in tracks.items():
                    if track.get(dd) == town and (attacker is None or name == named):
                        attacker, entered = name, dd
            if attacker is None:
                continue
            row = {"entered": entered, "town": town, "evenings": [], "is_named": attacker == named}
            for k in range(1, 7):
                e = at(tracks[attacker], entered - k)
                ours = None
                for back in (0, 1):
                    dd = days.get(entered - k - back)
                    if dd and dd["main"]:
                        ours = dd["main"]["pos"]
                        break
                seen = None
                note = tf.note(days.get(entered - k, {"notes": []}), "opposition heroes", 460)
                m = re.search(r"\((.+?), seen (\d+) days ago", note)
                if m and m.group(1) == attacker:
                    seen = int(m.group(2))
                row["evenings"].append((dist(e, town) if e else None, dist(ours, town) if ours else None, seen))
            rows.append(row)
print(f"{len(rows)} town losses with the attacker's way known")
print("evenings before | attacker: tiles from the town (median, same level) / on the other level | our main hero: tiles (median) / other level | we were nearer | attacker named as strongest enemy and seen that day")
for k in range(6):
    att = [r["evenings"][k][0] for r in rows if r["evenings"][k][0] is not None]
    our = [r["evenings"][k][1] for r in rows if r["evenings"][k][1] is not None]
    both = [(r["evenings"][k][0], r["evenings"][k][1]) for r in rows if r["evenings"][k][0] is not None and r["evenings"][k][1] is not None]
    seen = [r["evenings"][k][2] for r in rows]
    near = [a for a in att if a < 1000]
    onear = [o for o in our if o < 1000]
    print(f"   {k + 1}             | {statistics.median(near) if near else '-':>5} / {sum(1 for a in att if a >= 1000):2d} of {len(att):2d}"
          f"                           | {statistics.median(onear) if onear else '-':>5} / {sum(1 for o in our if o >= 1000):2d} of {len(our):2d}"
          f"            | {sum(1 for a, o in both if o < a):2d} of {len(both):2d}     | seen that day {sum(1 for s in seen if s == 0):2d}, 1-3 days ago {sum(1 for s in seen if s in (1, 2, 3)):2d}, not the named one {sum(1 for s in seen if s is None):2d}")
if explored:
    print(f"explored share the day before the first loss: FairAI median {statistics.median(e[0] for e in explored)} %, opponent {statistics.median(e[1] for e in explored)} % (n={len(explored)})")

named_rows = [r for r in rows if r["is_named"]]
print(f"attacker was the hero FairAI listed as the strongest enemy: {len(named_rows)} of {len(rows)}")
for k in range(4):
    seen = [r["evenings"][k][2] for r in named_rows]
    near = [r["evenings"][k][0] for r in named_rows if r["evenings"][k][0] is not None and r["evenings"][k][0] < 1000]
    print(f"   {k + 1} evening(s) before: seen that day {sum(1 for x in seen if x == 0)}, seen 1-3 days ago {sum(1 for x in seen if x in (1, 2, 3))}, older or another hero listed {sum(1 for x in seen if x is None or x > 3)}; its distance to the town (same level) median {statistics.median(near) if near else '-'}")
moved_away = sum(1 for r in rows if r["evenings"][0][1] is not None and r["evenings"][2][1] is not None and r["evenings"][0][1] > r["evenings"][2][1] and r["evenings"][2][1] < 1000)
print(f"our main hero was farther from the town on the last evening than three evenings before: {moved_away} of {sum(1 for r in rows if r['evenings'][0][1] is not None and r['evenings'][2][1] is not None and r['evenings'][2][1] < 1000)}")
