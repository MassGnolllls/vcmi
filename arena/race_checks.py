"""race_checks.py JOBDIR... - do the corrections of the race rule hold? Per run:
- days on which the main hero went for two different towns of a race (oscillation)
- last towns lost (1 -> 0 towns) and where the main hero stood the evening before (in that town or not)
- days without a town; on how many of them the main hero went for a town; "no way known" reasons logged"""
import collections, glob, json, os, re, sys, tarfile

print("run   osc days | last town lost: total, main hero in it the evening before | days without a town, with a town as target, 'no way known' lines")
for job in sys.argv[1:]:
    osc = 0; last_lost = 0; last_in = 0; townless = 0; town_target = 0; noway = 0
    for mfile in sorted(glob.glob(os.path.join(job, "match_*.json"))):
        number = re.search(r"match_(\d+)\.json", mfile).group(1)
        archive = next(iter(glob.glob(os.path.join(job, f"*_{number}.tar.xz"))), None)
        if not archive:
            continue
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        rows = {t["day"]: t["players"][colour][1] for t in match["timeline"] if colour in t["players"]}
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        tag = f"FairAI {colour}"
        mains = collections.Counter(re.findall(tag + r": (\S[^:\n]*?) \(main\) ends at", log))
        if not mains:
            continue
        day = 0
        race_tiles = collections.defaultdict(set)
        town_days = set()
        in_town = {}
        for line in log.split("\n"):
            if tag not in line:
                continue
            m = re.search(tag + r" day (\d+)", line)
            if m:
                day = int(m.group(1))
            m = re.search(r": (\S[^:\n]*?) \(main\) ends at [^;]*?(, in town)?;", line)
            if m:
                in_town[day] = bool(m.group(2))
                continue
            m = re.search(r": (\S[^:\n]*?) -> (race|object 98) \((\d+ \d+ \d+)\)", line)
            if m and m.group(1) in mains:
                if m.group(2) == "race":
                    race_tiles[day].add(m.group(3))
                else:
                    town_days.add(day)
            if "no town of ours" in line and "no way known" in line:
                noway += 1
        osc += sum(1 for d, t in race_tiles.items() if len(t) >= 2)
        for d in sorted(rows):
            if d - 1 in rows and rows[d - 1] == 1 and rows[d] == 0:
                last_lost += 1
                last_in += 1 if in_town.get(d - 1) else 0
            if rows[d] == 0 and d > 7:
                townless += 1
                town_target += 1 if d in town_days else 0
    print(f"{os.path.basename(job.rstrip('/'))[:4]}  {osc:8d} | {last_lost:5d} {last_in:5d}                              | {townless:5d} {town_target:5d} {noway:6d}")
