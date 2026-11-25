#!/usr/bin/env python3
"""The defeats of a duel, side by side with what the chain did before the battle that decided them.

Usage: chain_losses.py REASONS_JSON RESULTS_DIR
For every side that lost: the decisive battle (duel_reasons.py), who fought it (main hero or another), and from the log of
that side up to that day: where its troops stood (the last "troops:" line before), what the chain delivered in the two
weeks before, how many days it had no way, heroes lost so far.
"""
import json, re, sys, tarfile
from pathlib import Path

rows = json.load(open(sys.argv[1])); root = Path(sys.argv[2])
TROOPS = re.compile(r"day (\d+): troops: main army (\d+), in our towns (\d+) for the main hero to take, garrisons (\d+), heroes in towns (\d+), other heroes in the field (\d+)")
CHAIN = re.compile(r"chain day (\d+): (\d+) links, load (\d+) from .*?, delivered (\d+) to")
NOWAY = re.compile(r"chain day (\d+): (\d+) waits in .*no run")
MAIN = re.compile(r": (.+?) \(main\) ends at")
DAY = re.compile(r"FairAI (red|blue) day (\d+)")
BATTLE = re.compile(r"hero battle on day (\d+) \((.*?)\) against (.+?): our (\d+) x ([-\d.]+), their (\d+) x ([-\d.]+) - model ratio ([-\d.]+) - (won|lost), we lost (\d+) %, they (\d+) %")
print("game map          side chain | reason                   | day  who fought      | field ours x f / theirs x g, lost % ours/theirs | before: main army, takeable in towns, in towns in all, others in the field | chain: delivered 14 days before (runs), no-way days 14 days before, heroes lost so far")
agg = {}
for r in sorted(rows, key=lambda r: (r['match'], r['colour'])):
    if r['state'] != 'lost':
        continue
    c = r['colour']; d = r['detail'] or {}
    day = d.get('day') or 0
    rec = [json.loads(l) for l in open(root / 'results.jsonl') if json.loads(l)['match'] == r['match']][0]
    on = c in rec['mine']
    arch = next(iter(root.glob('*_%04d.tar.xz' % r['match'])), None)
    troops = None; delivered = 0; runs = 0; noway = set(); lost = 0; mainname = None; mainAtDay = None; fight = None; today = 0
    if arch:
        t = tarfile.open(arch); m = [x for x in t.getnames() if x.endswith('VCMI_Client_log.txt')][0]
        tag = ('FairAI ' + c).encode()
        for raw in t.extractfile(m):
            if tag not in raw:
                continue
            line = raw.decode('utf-8', 'replace')
            dm = DAY.search(line)
            if dm:
                today = int(dm.group(2))
            mm = MAIN.search(line)
            if mm and today <= day:
                mainname = mm.group(1).split(': ')[-1]
            tm = TROOPS.search(line)
            if tm and int(tm.group(1)) <= day:
                troops = tuple(int(x) for x in tm.groups())
            cm = CHAIN.search(line)
            if cm and day - 14 <= int(cm.group(1)) <= day:
                runs += 1; delivered += int(cm.group(4))
            nm = NOWAY.search(line)
            if nm and day - 14 <= int(nm.group(1)) <= day:
                noway.add(int(nm.group(1)))
            if ('lost a hero' in line or 'a hero was killed' in line) and today <= day:
                lost += 1
            bm = BATTLE.search(line)
            if bm and abs(int(bm.group(1)) - day) <= 1 and bm.group(9) == 'lost' and int(bm.group(4)) >= 0.8 * (d.get('ownField') or 0):
                fight = bm.groups()
    who = d.get('hero') or '?'
    role = 'main' if mainname and who == mainname else 'other'
    name = r['map'].split('/')[-1].replace('MIRROR_', '')[:12]
    f = f"{int(fight[3])/1000:.0f}k x {fight[4]} / {int(fight[5])/1000:.0f}k x {fight[6]}, {fight[9]}/{fight[10]}" if fight else f"{(d.get('ownField') or 0)/1000:.0f}k / {(d.get('enemyField') or 0)/1000:.0f}k"
    tr = f"{troops[1]/1000:.0f}k, {troops[2]/1000:.0f}k, {(troops[3]+troops[4])/1000:.0f}k, {troops[5]/1000:.0f}k (day {troops[0]})" if troops else "-"
    print(f"{r['match']:4d} {name:12s} {c:4s} {'x' if on else ' '}     | {r['reason']:24s} | {day:3d}  {who[:10]:10s} {role:5s} | {f:44s} | {tr:44s} | {delivered/1000:.0f}k ({runs}), {len(noway)}, {lost}")
    a = agg.setdefault(on, {'n': 0, 'main': 0, 'take': [], 'field': [], 'lost': [], 'attacked': 0, 'day': []})
    a['n'] += 1; a['main'] += role == 'main'; a['attacked'] += bool(d.get('attacked')); a['lost'].append(lost); a['day'].append(day)
    if troops:
        a['take'].append(troops[2] / max(1, troops[1])); a['field'].append(troops[5] / max(1, troops[1]))
import statistics
for on in (True, False):
    a = agg.get(on)
    if a:
        med = lambda v: statistics.median(v) if v else float('nan')
        print(f"{'chain' if on else 'no chain'}: {a['n']} defeats, decisive battle day (median) {med(a['day'])}, fought by the main hero {a['main']}, as the attacked side {a['attacked']}; before it: takeable in towns {100*med(a['take']):.0f}% of the main army, other heroes in the field {100*med(a['field']):.0f}%; heroes lost before (median) {med(a['lost'])}")
