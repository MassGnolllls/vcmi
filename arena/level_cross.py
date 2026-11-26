"""level_cross.py JOBDIR... - when FairAI first set foot on the other level of the map, with which hero, and what it was
going for at that moment (the gate itself, an enemy hero, a town ...); and games in which it never did."""
import collections, glob, json, os, re, sys, tarfile

for job in sys.argv[1:]:
    rows = []
    for mfile in sorted(glob.glob(os.path.join(job, "match_*.json"))):
        number = re.search(r"match_(\d+)\.json", mfile).group(1)
        archive = next(iter(glob.glob(os.path.join(job, f"*_{number}.tar.xz"))), None)
        if not archive:
            continue
        result = os.path.basename(archive).split("_")[0]
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        tag = f"FairAI {colour}"
        day = 0
        home = None
        first = None
        last_target = {}
        main_first = None
        for line in log.split("\n"):
            if tag not in line:
                continue
            m = re.search(tag + r" day (\d+)", line)
            if m:
                day = int(m.group(1))
            m = re.search(tag + r": (\S[^:\n]*?) -> (.+?) \((\d+) (\d+) (\d+)\)", line)
            if m:
                last_target[m.group(1)] = m.group(2).split(" value")[0]
                continue
            m = re.search(tag + r": (\S[^:\n]*?)( \(main\))? ends at \((\d+) (\d+) (\d+)\)", line)
            if m:
                z = int(m.group(5))
                if home is None and m.group(2):
                    home = z
                if home is not None and z != home:
                    if first is None:
                        first = (day, m.group(1), last_target.get(m.group(1), "?"))
                    if m.group(2) and main_first is None:
                        main_first = (day, last_target.get(m.group(1), "?"))
        rows.append((int(number), result, first, main_first))
    print(f"== {os.path.basename(job.rstrip('/'))}")
    never = [r for r in rows if r[2] is None]
    print(f"   games in which no hero of ours ever ended a day on the other level: {len(never)} of {len(rows)} ({', '.join(f'g{r[0]} {r[1]}' for r in never)})")
    how = collections.Counter(r[2][2].split(" (")[0] for r in rows if r[2])
    print("   first crossing - what the hero was going for:", dict(how.most_common(8)))
    days = sorted(r[2][0] for r in rows if r[2])
    mdays = sorted(r[3][0] for r in rows if r[3])
    print(f"   first crossing day: median {days[len(days) // 2] if days else '-'}; main hero's first day there: median {mdays[len(mdays) // 2] if mdays else '-'} ({len(mdays)} games)")
    for r in rows:
        if r[2]:
            print(f"   g{r[0]:2d} {r[1]:4s} day {r[2][0]:3d} {r[2][1][:12]:12s} going for {r[2][2][:28]:28s} | main hero: {('day ' + str(r[3][0]) + ' going for ' + r[3][1][:24]) if r[3] else 'never'}")
