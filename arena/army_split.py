#!/usr/bin/env python3
"""How much of FairAI's field army does the main hero lead? Reads a client log written with
ARENA_AI_LOG=debug and prints, per week, the main hero's share of the strength of all our heroes
that moved, the number of heroes, and how often the lead changed (debugging aid).

  python3 arena/army_split.py VCMI_Client_log.txt [--player red]
"""
import argparse
import re
from collections import defaultdict


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument('log')
	ap.add_argument('--player', default='red')
	args = ap.parse_args()
	ends = re.compile(r'FairAI %s: (\S+)( \(main\))? ends at \((\d+) (\d+) (\d+)\), strength (\d+)' % args.player)
	dayend = re.compile(r'FairAI %s day (\d+) end: gold (\d+), heroes (\d+), towns (\d+), main hero (\S+) army (\d+)' % args.player)
	today = {}
	weeks = defaultdict(lambda: {'share': [], 'heroes': [], 'leaders': [], 'main': [], 'others': [], 'towns': []})
	for line in open(args.log, errors='replace'):
		m = ends.search(line)
		if m:
			today[m.group(1)] = (int(m.group(6)), bool(m.group(2)))
			continue
		m = dayend.search(line)
		if m:
			day = int(m.group(1))
			week = (day - 1) // 7 + 1
			main = sum(s for s, is_main in today.values() if is_main)
			total = sum(s for s, _ in today.values())
			w = weeks[week]
			if total > 0:
				w['share'].append(main / total)
			w['heroes'].append(int(m.group(3)))
			w['leaders'].append(m.group(5))
			w['main'].append(main)
			w['others'].append(total - main)
			w['towns'].append(int(m.group(4)))
			today = {}
	print('week  main share  main strength  others  heroes  towns  leaders')
	for week in sorted(weeks):
		w = weeks[week]
		share = sum(w['share']) / len(w['share']) if w['share'] else float('nan')
		leaders = []
		for name in w['leaders']:
			if not leaders or leaders[-1] != name:
				leaders.append(name)
		print(f"{week:4d}  {share:10.2f}  {sum(w['main']) / len(w['main']) / 1000:12.0f}k  {sum(w['others']) / len(w['others']) / 1000:5.0f}k  "
			f"{sum(w['heroes']) / len(w['heroes']):6.1f}  {sum(w['towns']) / len(w['towns']):5.1f}  {' > '.join(leaders)}")


if __name__ == '__main__':
	main()
