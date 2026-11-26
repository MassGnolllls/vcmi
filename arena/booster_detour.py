"""booster_detour.py JOBDIR... - places that raise attributes: how many points lay near the main hero's way and were left."""
import collections, glob, json, os, re, statistics, sys, tarfile, zipfile
MAPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "maps", "mirror-m2")
WORTH = {"marlettoTower": 1, "mercenaryCamp": 1, "starAxis": 1, "gardenOfRevelation": 1, "arena": 2, "schoolOfWar": 1, "schoolOfMagic": 1, "libraryOfEnlightenment": 8, "colosseumOfTheMagi": 2}
rows = []
kinds_left = collections.Counter(); kinds_taken = collections.Counter()
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "*.tar.xz"))):
        number = re.search(r"_(\d+)\.tar", path).group(1)
        mfile = os.path.join(job, f"match_{number}.json")
        if not os.path.exists(mfile):
            continue
        match = json.load(open(mfile))
        items = json.loads(zipfile.ZipFile(os.path.join(MAPS, os.path.basename(match["map"]).lower() + ".vmap")).read("objects.json"))
        places = [(v["type"], v["x"], v["y"], v["l"]) for v in items if v.get("type") in WORTH]
        with tarfile.open(path) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        mains = collections.Counter(re.findall(r": (\S[^:\n]*?) \(main\) ends at", log))
        if not mains:
            continue
        main = mains.most_common(1)[0][0]
        ends = [tuple(int(x) for x in m) for m in re.findall(r": " + re.escape(main) + r" \(main\) ends at \((\d+) (\d+) (\d+)\)", log)]
        goals = set(tuple(int(x) for x in m) for m in re.findall(r" " + re.escape(main) + r" -> object \d+ \((\d+) (\d+) (\d+)\)", log))
        home = ends[0][2] if ends else 0
        taken = near8 = near4 = offer_home = offer = 0
        for kind, x, y, l in places:
            w = WORTH[kind]
            offer += w
            offer_home += w if l == home else 0
            if any(g[2] == l and abs(g[0] - x) <= 2 and abs(g[1] - y) <= 2 for g in goals):
                taken += w
                kinds_taken[kind] += 1
                continue
            d = min((max(abs(e[0] - x), abs(e[1] - y)) for e in ends if e[2] == l), default=999)
            near8 += w if d <= 8 else 0
            near4 += w if d <= 4 else 0
            if d <= 8:
                kinds_left[kind] += 1
        rows.append((offer, offer_home, taken, near4, near8))
med = lambda i: statistics.median(r[i] for r in rows)
print(f"{len(rows)} games (medians, in attribute points): on the map {med(0)}, on the main hero's home level {med(1)}, taken by the main hero {med(2)}, "
      f"left although the main hero ended a day within 4 tiles {med(3)}, within 8 tiles {med(4)}")
print("sum over all games: taken", sum(r[2] for r in rows), "| left within 4 tiles", sum(r[3] for r in rows), "| left within 8 tiles", sum(r[4] for r in rows), "| on offer", sum(r[0] for r in rows))
print("left within 8 tiles, by kind:", dict(kinds_left.most_common()))
print("taken, by kind:", dict(kinds_taken.most_common()))
