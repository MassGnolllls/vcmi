"""intrusion.py JOBDIR... - the enemy's strongest hero near our towns: how strong we counted ourselves against it,
where our main hero was, and whether we attacked it. Plus all battles between the two big armies."""
import collections, glob, json, os, re, statistics, sys, tarfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_falls as tf
import town_losses_lib as tl

def enemy_tracks(log):
    day = 0
    pos = collections.defaultdict(dict)
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

NEAR = 12
cls = lambda r: "we 1.3x or more" if r >= 1.3 else ("we 1.0-1.3x" if r >= 1.0 else ("we 0.77-1.0x" if r >= 0.77 else "enemy 1.3x or more"))
daysby = collections.Counter(); attacked = collections.Counter(); won = collections.Counter(); ourdist = collections.defaultdict(list)
runs = collections.Counter(); runs_hit = collections.Counter(); games = 0; intr_games = 0
big_we = []; big_they = []
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "*.tar.xz"))):
        number = re.search(r"_(\d+)\.tar", path).group(1)
        mfile = os.path.join(job, f"match_{number}.json")
        if not os.path.exists(mfile):
            continue
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        with tarfile.open(path) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        days = tf.parse(log, colour)
        tracks = enemy_tracks(log)
        towns = set(tf.towns_of(match["map"].lower()))
        games += 1
        for m in re.finditer(r"hero battle on day (\d+) \((we attack|we are attacked)(, at the walls)?\) against ([^:]+): our (\d+) x ([\d.-]+), their (\d+) x ([\d.-]+) - model ratio ([\d.-]+) - (won|lost)", log):
            ours, theirs = int(m.group(5)), int(m.group(7))
            if ours >= 200000 and theirs >= 200000:
                (big_we if m.group(2) == "we attack" else big_they).append((float(m.group(9)), m.group(10) == "won", bool(m.group(3))))
        last_class, in_run, any_intr = None, False, False
        for d in sorted(days):
            note = tf.note(days[d], "opposition heroes", 460)
            m = re.search(r"opposition heroes (\d+) \((.+?), seen (\d+) days ago", note)
            mf = re.search(r"in the field (\d+)\)", note)
            foreign = tl.foreign(days, d)
            if not m or not mf or foreign is None or not days[d]["main"] or int(m.group(1)) < 150000:
                in_run = False
                continue
            name = m.group(2)
            e = at(tracks.get(name, {}), d)
            own = [t for t in towns if "%d %d %d" % t not in foreign]
            if e is None or not own:
                in_run = False
                continue
            near = min(max(abs(e[0] - t[0]), abs(e[1] - t[1])) for t in own if t[2] == e[2]) if any(t[2] == e[2] for t in own) else 999
            if near > NEAR:
                in_run = False
                continue
            any_intr = True
            ratio = int(mf.group(1)) / max(1, int(m.group(1)))
            c = cls(ratio)
            daysby[c] += 1
            p = days[d]["main"]["pos"]
            ourdist[c].append(max(abs(p[0] - e[0]), abs(p[1] - e[1])) if p[2] == e[2] else 999)
            hit = None
            for dd in (d, d + 1):
                for f in days.get(dd, {"fights": []})["fights"]:
                    if "(we attack" in f and f"against {name}:" in f:
                        hit = " - won" in f
            if hit is not None:
                attacked[c] += 1
                won[c] += hit
            if not in_run:
                runs[c] += 1
            in_run = True
        intr_games += any_intr
print(f"{games} games; in {intr_games} the enemy's strongest hero (150,000 or more in our measure) ended a day within {NEAR} tiles of a town of ours")
for c in ("we 1.3x or more", "we 1.0-1.3x", "we 0.77-1.0x", "enemy 1.3x or more"):
    d = ourdist[c]
    same = [x for x in d if x < 999]
    print(f"  {c:20s}: {daysby[c]:4d} days in {runs[c]:3d} visits; our main hero attacked it on {attacked[c]:3d} (won {won[c]}); our main hero that evening: other level {sum(1 for x in d if x >= 999)}, same level median {statistics.median(same) if same else '-'} tiles from it, within 16 tiles {sum(1 for x in same if x <= 16)}")
def show(tag, rows):
    if not rows:
        print(tag, "none"); return
    print(f"{tag}: {len(rows)}, won by FairAI {sum(1 for r in rows if r[1])}; model ratio median {statistics.median(r[0] for r in rows):.2f}; at the walls {sum(1 for r in rows if r[2])}; "
          + "by ratio: " + ", ".join(f"{lab} {sum(1 for r in rows if lo <= r[0] < hi and r[1])}/{sum(1 for r in rows if lo <= r[0] < hi)}" for lab, lo, hi in (("<0.77", -9, 0.77), ("0.77-1.0", 0.77, 1.0), ("1.0-1.3", 1.0, 1.3), ("1.3+", 1.3, 1e9))))
show("battles of 200,000+ on both sides, FairAI attacks", big_we)
show("battles of 200,000+ on both sides, FairAI is attacked", big_they)
