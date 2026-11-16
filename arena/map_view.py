#!/usr/bin/env python3
"""Draws a .vmap map as text: terrain passability, towns, guards, mines - to understand a game's
topology when reading its log (the AI itself never reads this; it is a debugging aid only).

  python3 arena/map_view.py MAP.vmap [--level 0] [--zones]

Legend: '#' blocked, '.' free, '~' water, 'T' town (digit = owner+1 when owned), 'm' monster,
'M' monolith/gate, 'x' mine, '$' other visitable object, 'H' hero.
--zones adds the connected regions when all monsters count as walls, and lists each monster with the
regions it separates.
"""
import argparse
import json
import re
import sys
import zipfile
from collections import deque


def load_lenient(text):
	text = re.sub(r'^\s*//.*$', '', text, flags=re.M)
	return json.loads(text)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument('map')
	ap.add_argument('--level', type=int, default=0)
	ap.add_argument('--zones', action='store_true')
	args = ap.parse_args()

	z = zipfile.ZipFile(args.map)
	names = z.namelist()
	objects = load_lenient(z.read('objects.json').decode('utf-8', 'replace'))
	if isinstance(objects, dict):
		objects = list(objects.values())
	terrain_name = 'surface_terrain.json' if args.level == 0 else 'underground_terrain.json'
	if terrain_name not in names:
		sys.exit('no such level')
	terrain = load_lenient(z.read(terrain_name).decode('utf-8', 'replace'))
	# terrain[x][y] or [y][x]? VCMI writes rows of tiles: outer index = y
	height = len(terrain)
	width = len(terrain[0])
	grid = [['.' for _ in range(width)] for _ in range(height)]
	blocked = [[False] * width for _ in range(height)]
	for y in range(height):
		for x in range(width):
			code = terrain[y][x]
			if code.startswith('wt'):
				grid[y][x] = '~'
				blocked[y][x] = True
			elif code.startswith('rc'):
				grid[y][x] = '#'
				blocked[y][x] = True

	monsters = []
	towns = []
	visitable = {}
	for obj in objects:
		if obj.get('l', 0) != args.level:
			continue
		kind = obj.get('type', '')
		x0, y0 = obj.get('x', 0), obj.get('y', 0)
		mask = obj.get('template', {}).get('mask', [])
		rows = len(mask)
		for r, line in enumerate(mask):
			cols = len(line)
			for c, ch in enumerate(line):
				x = x0 - (cols - 1 - c)
				y = y0 - (rows - 1 - r)
				if not (0 <= x < width and 0 <= y < height):
					continue
				if ch in 'BH':
					blocked[y][x] = True
					if grid[y][x] == '.':
						grid[y][x] = '#'
				elif ch in 'AT':
					visitable[(x, y)] = kind
		if kind == 'monster' or kind.startswith('randomMonster'):
			amount = obj.get('options', {}).get('amount', obj.get('amount', '?'))
			monsters.append((x0, y0, obj.get('subtype', kind), amount))
		elif kind in ('town', 'randomTown'):
			owner = obj.get('options', {}).get('owner', obj.get('owner'))
			towns.append((x0, y0, obj.get('subtype', '?'), owner))

	symbol = {'monster': 'm', 'town': 'T', 'randomTown': 'T', 'mine': 'x', 'abandonedMine': 'x', 'hero': 'H', 'randomHero': 'H',
		'monolithOneWayEntrance': 'M', 'monolithOneWayExit': 'M', 'monolithTwoWay': 'M', 'subterraneanGate': 'M', 'prison': 'P'}
	for (x, y), kind in visitable.items():
		ch = symbol.get(kind)
		if ch is None:
			ch = 'm' if kind.startswith('randomMonster') else '$'
		grid[y][x] = ch
	colors = {'red': 1, 'blue': 2, 'tan': 3, 'green': 4, 'orange': 5, 'purple': 6, 'teal': 7, 'pink': 8}
	for x, y, sub, owner in towns:
		# the entrance of a town is two tiles left of its anchor
		for (vx, vy), kind in visitable.items():
			if kind in ('town', 'randomTown') and abs(vx - (x - 2)) <= 0 and vy == y:
				if owner in colors:
					grid[vy][vx] = str(colors[owner])

	region = None
	if args.zones:
		# regions with monsters (and the tiles next to them) as walls
		wall = [[blocked[y][x] for x in range(width)] for y in range(height)]
		guard_zone = {}
		for mx, my, sub, amount in monsters:
			for dx in (-1, 0, 1):
				for dy in (-1, 0, 1):
					x, y = mx + dx, my + dy
					if 0 <= x < width and 0 <= y < height:
						if not blocked[y][x] or (dx == 0 and dy == 0):
							guard_zone.setdefault((x, y), []).append((mx, my))
		region = [[-1] * width for _ in range(height)]
		sizes = []
		for y in range(height):
			for x in range(width):
				if region[y][x] != -1 or (wall[y][x] and (x, y) not in visitable) or (x, y) in guard_zone:
					continue
				if blocked[y][x] and (x, y) not in visitable:
					continue
				index = len(sizes)
				queue = deque([(x, y)])
				region[y][x] = index
				count = 0
				while queue:
					cx, cy = queue.popleft()
					count += 1
					for dx in (-1, 0, 1):
						for dy in (-1, 0, 1):
							nx, ny = cx + dx, cy + dy
							if not (0 <= nx < width and 0 <= ny < height) or region[ny][nx] != -1:
								continue
							if (nx, ny) in guard_zone:
								continue
							if blocked[ny][nx] and (nx, ny) not in visitable:
								continue
							# visitable objects are entered, not crossed - close enough for a sketch
							region[ny][nx] = index
							queue.append((nx, ny))
				sizes.append(count)
		big = [i for i, s in enumerate(sizes) if s >= 25]
		letter = {i: chr(ord('a') + n % 26) for n, i in enumerate(sorted(big, key=lambda i: -sizes[i]))}

	print('    ' + ''.join(str(x // 10 % 10) for x in range(width)))
	print('    ' + ''.join(str(x % 10) for x in range(width)))
	for y in range(height):
		line = ''
		for x in range(width):
			ch = grid[y][x]
			if region is not None and ch == '.' and region[y][x] in letter:
				ch = letter[region[y][x]]
			line += ch
		print(f'{y:3d} {line}')

	print('\ntowns:')
	for x, y, sub, owner in towns:
		print(f'  ({x - 2},{y}) {sub} owner {owner}')
	if args.zones:
		print('\nregions (tiles):', ', '.join(f'{letter[i]}={sizes[i]}' for i in sorted(big, key=lambda i: -sizes[i])))
		print('\nmonsters between regions:')
		for mx, my, sub, amount in sorted(monsters):
			touching = set()
			for dx in range(-2, 3):
				for dy in range(-2, 3):
					x, y = mx + dx, my + dy
					if 0 <= x < width and 0 <= y < height and region[y][x] in letter:
						touching.add(letter[region[y][x]])
			if len(touching) >= 2:
				print(f'  ({mx},{my}) {sub} x{amount}: {"-".join(sorted(touching))}')


if __name__ == '__main__':
	main()
