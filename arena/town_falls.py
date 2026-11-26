"""town_falls.py JOBDIR... [--all] - for every lost game: how the first own town fell and how the game ended.

Per game: the day of the first loss, what attacked, what stood in the town, where the main hero was and what
it was doing, what FairAI knew about the enemy, and the last ten days (towns, main army, what the main hero did)."""
import collections, glob, json, os, re, sys, tarfile, zipfile

MAPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "maps", "mirror-m2")

def towns_of(map_name):
    path = os.path.join(MAPS, os.path.basename(map_name).replace(".vmap", "") + ".vmap")
    if not os.path.exists(path):
        return []
    items = json.loads(zipfile.ZipFile(path).read("objects.json"))
    return [(v["x"] - 2, v["y"], v["l"]) for v in items if v.get("type") == "town"]

def parse(log, colour):
    tag = f"FairAI {colour}"
    days = collections.defaultdict(lambda: {"main": None, "targets": collections.Counter(), "notes": [], "sieges": [], "fights": []})
    day = 0
    main_name = None
    for line in log.split("\n"):
        if tag not in line:
            continue
        m = re.search(tag + r" day (\d+)", line)
        if m:
            day = int(m.group(1))
        d = days[day]
        m = re.search(r": (\S[^:]*?) \(main\) ends at \((\d+) (\d+) (\d+)\), strength (\d+), known threat (\d+)[^;]*?(, in town)?; level (\d+), A(\d+) D(\d+) P(\d+) K(\d+)", line)
        if m:
            main_name = m.group(1)
            d["main"] = dict(name=main_name, pos=(int(m.group(2)), int(m.group(3)), int(m.group(4))), strength=int(m.group(5)), threat=int(m.group(6)),
                             in_town=bool(m.group(7)), level=int(m.group(8)))
            continue
        if main_name and f": {main_name} -> " in line or (main_name and f" {main_name} -> " in line):
            m = re.search(r" -> ([a-z ]+?)(?: \d+)? (?:enemy )?\((\d+) (\d+) (\d+)\)", line)
            if m:
                kind = m.group(1).strip()
                if kind == "object":
                    obj = re.search(r"-> object (\d+)( enemy)?", line)
                    kind = {"98": "town", "34": "hero"}.get(obj.group(1), "object") + (" (enemy)" if obj.group(2) else "")
                d["targets"][kind + " (%s %s %s)" % m.group(2, 3, 4) if kind.startswith(("town", "hero")) else kind] += 1
            continue
        if "is outmatched" in line or "opposition heroes" in line or "troops: main army" in line or " end: gold" in line or "unbought in" in line:
            d["notes"].append(line[line.find(tag) + len(tag):].strip()[:460])
        elif "battle is a siege of" in line:
            d["sieges"].append(line[line.find("siege of") + 9:].strip()[:40])
        elif "hero battle on day" in line:
            d["fights"].append(line[line.find("hero battle on day"):][:230])
    return days

def note(d, key, cut=230):
    for n in d["notes"]:
        if key in n:
            return n[:cut]
    return "-"

if __name__ == "__main__":
    show_all = "--all" in sys.argv
    for job in [a for a in sys.argv[1:] if not a.startswith("--")]:
        for path in sorted(glob.glob(os.path.join(job, "lost_*.tar.xz")) + (glob.glob(os.path.join(job, "open_*.tar.xz")) if show_all else [])):
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
                days = parse(tar.extractfile(member).read().decode("utf-8", "replace"), colour)
            last = max(rows)
            towns = {d: rows[d][colour][1] for d in rows if colour in rows[d]}
            first = next((d for d in sorted(towns) if d - 1 in towns and towns[d] < towns[d - 1]), None)
            losses = sum(1 for d in towns if d - 1 in towns and towns[d] < towns[d - 1])
            gains = sum(1 for d in towns if d - 1 in towns and towns[d] > towns[d - 1] and d > 20)
            print(f"=== {os.path.basename(job)[:5]} game {int(number)} {os.path.basename(match['map'])[7:-12]} FairAI {colour}: ends day {match['day']} ({match['outcome']}); towns lost {losses}x, won {gains}x")
            if first is None:
                print("   no town lost before the end")
                continue
            r_me, r_other = rows[first - 1][colour], rows[first - 1][other]
            print(f"   first loss day {first}: towns {towns[first - 1]} -> {towns[first]} (enemy held {r_other[1]}); day before: our main army {r_me[11]}, garrisons {r_me[12]}, all heroes {r_me[0]}, gold {r_me[7]}; enemy main army {r_other[11]}, all heroes {r_other[0]}")
            fights = [b for b in match["playerBattles"] if first - 1 <= b[0] <= first and b[2] == me_idx and b[8]]
            for b in fights[:3]:
                print(f"   attack on a town day {b[0]}: attacker {b[3]} (hero level {b[10]}) vs {b[4]} in the town ({'with' if b[9] else 'no'} hero), attacker lost {b[5]}, we {b[6]}, winner {'attacker' if b[7] == 0 else 'defender'}")
            if not fights:
                print("   no battle: the town was empty")
            all_towns = towns_of(match["map"])
            for back in (3, 2, 1, 0):
                d = days.get(first - back)
                if not d or not d["main"]:
                    continue
                m = d["main"]
                near = min((abs(m["pos"][0] - t[0]) + abs(m["pos"][1] - t[1]) + (200 if m["pos"][2] != t[2] else 0), t) for t in all_towns) if all_towns else None
                top = ", ".join(f"{k} x{v}" for k, v in d["targets"].most_common(3))
                print(f"   day {first - back}: {m['name']} L{m['level']} at {m['pos']}{' in town' if m['in_town'] else ''}, strength {m['strength']}, known threat {m['threat']}; went for: {top}")
            before = days.get(first - 1, {"notes": [], "fights": []})
            print("   knew:", note(before, "opposition heroes", 300))
            for back in range(0, 8):
                t = note(days.get(first - back, {"notes": []}), "troops: main army")
                if t != "-":
                    print(f"   day {first - back}", t)
                    break
            for back in (1, 0):
                for f in days.get(first - back, {"fights": []})["fights"][:2]:
                    print("   ", f)
            print(f"   --- the end: towns by day " + " ".join(f"{d}:{towns[d]}" for d in range(max(1, last - 9), last + 1) if d in towns))
            for d in range(max(1, last - 8), last + 1):
                day = days.get(d)
                if not day or not day["main"]:
                    continue
                m = day["main"]
                top = ", ".join(f"{k} x{v}" for k, v in day["targets"].most_common(2))
                out = note(day, "is outmatched", 200)
                print(f"   day {d}: {m['name']} at {m['pos']}{' in town' if m['in_town'] else ''} strength {m['strength']} threat {m['threat']}; {top}" + (f" | {out[out.find('is outmatched'):]}" if out != "-" else ""))
            endrow = rows[last]
            print(f"   last day: our hero armies {endrow[colour][0]}, towns {endrow[colour][1]}, gold {endrow[colour][7]}; enemy hero armies {endrow[other][0]}, towns {endrow[other][1]}")
