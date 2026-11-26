"""booster_count.py JOBDIR... - places that raise attack/defence/power/knowledge: how many each side's leading hero went to."""
import collections, glob, os, re, statistics, sys, tarfile
IDS = {"4": 2, "23": 1, "32": 1, "41": 8, "47": 1, "51": 1, "61": 1, "107": 1}
NAMES = {"arena": 2, "marlettoTower": 1, "gardenOfRevelation": 1, "libraryOfEnlightenment": 8, "schoolOfMagic": 1, "mercenaryCamp": 1, "starAxis": 1, "schoolOfWar": 1, "colosseumOfTheMagi": 2}
out = collections.defaultdict(list)
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "*.tar.xz"))):
        result = os.path.basename(path).split("_")[0]
        with tarfile.open(path) as tar:
            member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
            if member is None:
                continue
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        mains = collections.Counter(re.findall(r": (\S[^:\n]*?) \(main\) ends at", log))
        if not mains:
            continue
        main = mains.most_common(1)[0][0]
        ours = set(re.findall(r" " + re.escape(main) + r" -> object (\d+) \((\d+ \d+ \d+)\)", log))
        ours_colosseum = set(re.findall(r" " + re.escape(main) + r" -> object 319 \((\d+ \d+ \d+)\)", log))
        our_points = sum(IDS[i] for i, _ in ours if i in IDS) + 2 * len(ours_colosseum)
        theirs = collections.defaultdict(set)
        for name, pos, hero in re.findall(r"ExecuteHeroChain (\w+)\((\d+ \d+ \d+)\) by (\S[^\n]*?) (?:with prio|\(value)", log):
            if name in NAMES:
                theirs[hero].add((name, pos))
        top = max(theirs.values(), key=lambda s: sum(NAMES[n] for n, _ in s), default=set())
        out[result].append((our_points, sum(NAMES[n] for n, _ in top), len(mains)))
for result, rows in out.items():
    print(f"{result:5s} n={len(rows):2d}: points from places our leading hero went for: median {statistics.median(r[0] for r in rows)}, Nullkiller2's leading hero: {statistics.median(r[1] for r in rows)}; heroes that led our army in a game: median {statistics.median(r[2] for r in rows)}")
