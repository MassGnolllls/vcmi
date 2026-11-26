"""game_reasons.py JOBDIR... - every game FairAI did not win, by the reason it was not won.

lost, 7 days without a town (army intact): no town at the end, and the hero armies were still at least 30 % of the enemy's
lost, main army beaten: the biggest battle of our main army in the last 30 days was lost
lost, other
open, ahead / open, behind (score >= 0.6 / below); for open games: enemy towns known at the end, towns on both sides,
days the main hero ended in a town of ours in the last 100 days, days bound by a race in the last 100 days."""
import collections, glob, json, os, re, sys, tarfile

def last_note(log, colour, key):
    found = None
    for m in re.finditer(r"FairAI " + colour + r" day (\d+): " + key + r"[^\n]*", log):
        found = m.group(0)
    return found or ""

summary = collections.OrderedDict()
for job in sys.argv[1:]:
    name = os.path.basename(job.rstrip("/"))[:4]
    counts = collections.Counter()
    lines = []
    for mfile in sorted(glob.glob(os.path.join(job, "match_*.json"))):
        number = re.search(r"match_(\d+)\.json", mfile).group(1)
        archive = next(iter(glob.glob(os.path.join(job, f"*_{number}.tar.xz"))), None)
        if not archive:
            continue
        result = os.path.basename(archive).split("_")[0]
        if result == "won":
            continue
        match = json.load(open(mfile))
        colour = next(c for c, p in match["players"].items() if p["ai"] == "FairAI")
        other = next(c for c in match["players"] if c != colour)
        idx = ["red", "blue", "tan", "green"].index(colour)
        rows = {t["day"]: t["players"] for t in match["timeline"] if colour in t["players"]}
        last = max(rows)
        me, them = rows[last][colour], rows[last][other]
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt"))
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        if result == "lost":
            townless = 0
            for d in range(last, 0, -1):
                if d in rows and rows[d][colour][1] == 0:
                    townless += 1
                else:
                    break
            ours = [b for b in match["playerBattles"] if last - 30 <= b[0] and (b[1] == idx or b[2] == idx) and max(b[3], b[4]) >= 150000]
            big = max(ours, key=lambda b: b[3] if b[1] == idx else b[4], default=None)
            main_lost = big is not None and ((b := big)[1] == idx and b[7] != 0 or b[2] == idx and b[7] == 0)
            if me[1] == 0 and townless >= 6 and me[0] >= max(300000 * 0, 0.3 * them[0]) and me[0] >= 100000:
                why = "lost, 7 days without a town (army intact)"
            elif main_lost:
                why = "lost, main army beaten"
            else:
                why = "lost, other"
            extra = f"towns 0 for {townless} days, our armies {me[0] // 1000}k vs {them[0] // 1000}k" if me[1] == 0 else f"towns {me[1]}:{them[1]}, our armies {me[0] // 1000}k vs {them[0] // 1000}k"
            if big:
                extra += f"; biggest battle day {big[0]}: {'we' if big[1] == idx else 'they'} attacked, {big[3] // 1000}k vs {big[4] // 1000}k{' at a town' if big[8] else ''}, {'attacker' if big[7] == 0 else 'defender'} won"
        else:
            score = match["players"][colour].get("score") or 0
            s = next((json.loads(l).get("score") for l in open(os.path.join(job, "results.jsonl")) if str(json.loads(l).get("match")) == str(int(number))), None) or 0.0
            why = "open, ahead" if (s or 0) >= 0.6 else "open, behind"
            known = last_note(log, colour, "foreign towns known")
            known = known.split("foreign towns known: ")[1].split(";")[0] if known else "?"
            main_days = [int(m.group(1)) for m in re.finditer(r"FairAI " + colour + r" day (\d+): ", log)]
            home = len(re.findall(r"\(main\) ends at [^;\n]*, in town;", log[len(log) // 2:]))
            bound = sum(1 for m in re.finditer(r"FairAI " + colour + r" day (\d+): [^\n]* is bound by the race", log) if int(m.group(1)) > last - 100)
            lastrace = sum(1 for m in re.finditer(r"FairAI " + colour + r" day (\d+): race for [^\n]*our last town", log) if int(m.group(1)) > last - 100)
            extra = f"score {s:.2f}, towns {me[1]}:{them[1]}, armies {me[0] // 1000}k vs {them[0] // 1000}k, enemy towns known: {known[:40]}; last 100 days: bound {bound}, last-town rule {lastrace}"
        counts[why] += 1
        lines.append(f"   g{int(number):2d} {why:42s} {extra}")
    summary[name] = counts
    print(f"== {os.path.basename(job.rstrip('/'))}")
    print("\n".join(lines))
print("summary:")
for name, counts in summary.items():
    print(f"  {name}: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
