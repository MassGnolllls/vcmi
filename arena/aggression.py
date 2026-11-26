"""aggression.py JOBDIR... - how boldly FairAI's main hero played in each run: sieges of enemy towns, the day of the
first one, days it ended in a town of ours, days it was bound by a race, towns lost and won, length and outcome."""
import glob, json, os, re, statistics, sys, tarfile

def med(v):
    v = [x for x in v if x is not None]
    return statistics.median(v) if v else float("nan")

print("run                               games  W  L  open | sieges  first siege | towns lost  won | main in own town  bound | intercepts  guards  portals | day of end")
for job in sys.argv[1:]:
    sieges, first, lost, won, home, bound, ends, inter, guards, portals = [], [], [], [], [], [], [], [], [], []
    w = l = o = 0
    for mfile in sorted(glob.glob(os.path.join(job, "match_*.json"))):
        number = re.search(r"match_(\d+)\.json", mfile).group(1)
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        idx = ["red", "blue", "tan", "green"].index(colour)
        archive = next(iter(glob.glob(os.path.join(job, f"*_{number}.tar.xz"))), None)
        result = os.path.basename(archive).split("_")[0] if archive else "?"
        w += result == "won"; l += result == "lost"; o += result == "open"
        mine = [b for b in match["playerBattles"] if b[1] == idx and b[8]]
        sieges.append(len(mine))
        first.append(min((b[0] for b in mine), default=None))
        rows = {t["day"]: t["players"][colour][1] for t in match["timeline"] if colour in t["players"]}
        lost.append(sum(1 for d in rows if d - 1 in rows and rows[d] < rows[d - 1]))
        won.append(sum(1 for d in rows if d - 1 in rows and rows[d] > rows[d - 1] and d > 20))
        ends.append(match["day"])
        if archive:
            with tarfile.open(archive) as tar:
                member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
                log = tar.extractfile(member).read().decode("utf-8", "replace") if member else ""
            total = len(re.findall(r"\(main\) ends at", log))
            in_town = len(re.findall(r"\(main\) ends at [^;]*, in town;", log))
            home.append(100.0 * in_town / total if total else None)
            bound.append(100.0 * len(re.findall(r"is bound by the race", log)) / max(1, match["day"]))
            inter.append(len(re.findall(r"may meet the army that comes", log)))
            guards.append(len(re.findall(r"guard for ", log)))
            portals.append(len(re.findall(r"casts Town Portal", log)))
    n = w + l + o
    print(f"{os.path.basename(job.rstrip('/'))[:33]:33s} {n:5d} {w:2d} {l:2d} {o:4d}  | {med(sieges):5.1f}  {med(first):9.0f}   | {med(lost):6.1f} {med(won):6.1f}   | {med(home):10.0f} %  {med(bound):6.1f} % | {sum(inter):6d} {sum(guards):8d} {sum(portals):7d}   | {med(ends):5.0f}")
