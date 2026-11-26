"""seen_coming.py JOBDIR... - attacks on FairAI by big enemy armies: what FairAI's own reach model said about the attacker
before the attack, and how Nullkiller2 brought the army there (hero chains: several heroes handing the army on in one day)."""
import collections, glob, os, re, statistics, sys, tarfile
unknown = 0; tiles = []; ages = []; walls_unknown = 0; walls = 0
chain_heroes = []; chain_towns = 0
for job in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(job, "*.tar.xz"))):
        with tarfile.open(path) as tar:
            member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
            if member is None:
                continue
            log = tar.extractfile(member).read().decode("utf-8", "replace")
        seen_lines = set()
        for m in re.finditer(r"hero battle on day (\d+) \(we are attacked(, at the walls)?\) against ([^:]+): our (\d+) x [\d.-]+, their (\d+) x [\d.-]+ - model ratio [\d.-]+ - (won|lost)[^;]*; (it was not among the threats we knew|our reach model had it ([\d.]+) tiles away \(sighting (\d+) day)", log):
            key = (m.group(1), m.group(3), m.group(4))
            if key in seen_lines or int(m.group(5)) < 200000:
                continue
            seen_lines.add(key)
            at_walls = bool(m.group(2))
            walls += at_walls
            if m.group(7).startswith("it was not"):
                unknown += 1
                walls_unknown += at_walls
            else:
                tiles.append(float(m.group(8)))
                ages.append(int(m.group(9)))
        # Nullkiller2's chains towards towns: how many heroes carry the army in one go
        for m in re.finditer(r"Executing hero chain towards town\((\d+ \d+ \d+)\)\. Path ([^\n]+)", log):
            names = re.findall(r"([A-Z][A-Za-z' ]+?)\[[0-9a-f]+\]->", m.group(2))
            chain_towns += 1
            chain_heroes.append(len(set(names)))
n = unknown + len(tiles)
print(f"attacks on FairAI by an enemy hero with 200,000 or more: {n} (at the walls of a town: {walls})")
print(f"  the attacker was not among the threats FairAI knew: {unknown} ({100 * unknown / max(1, n):.0f} %), at the walls {walls_unknown}")
if tiles:
    q = statistics.quantiles(tiles, n=4)
    print(f"  known, but the reach model had it out of reach: {len(tiles)}; tiles short: quartiles {q[0]:.0f} / {q[1]:.0f} / {q[2]:.0f}; sighting age 0 days: {sum(1 for a in ages if a == 0)}, 1-3 days: {sum(1 for a in ages if 1 <= a <= 3)}, older: {sum(1 for a in ages if a > 3)}")
if chain_heroes:
    c = collections.Counter(min(x, 4) for x in chain_heroes)
    print(f"Nullkiller2 marches on towns ({chain_towns} logged): heroes taking part in one march: 1 -> {c[1]}, 2 -> {c[2]}, 3 -> {c[3]}, 4+ -> {c[4]}")
