#!/usr/bin/env python3
"""Mirror maps: both players get the same world.

As HotA's mirror templates do it, the underground is an exact copy of the surface. A map for two
players from VCMI's own generator (one level) becomes the first player's world: the second player's
town is taken out, a subterranean gate is put where the two players' ways meet - as far from the one
town as from the other -, and the whole level with everything on it is copied into the underground,
where the copy of the first player's town belongs to the second player. The gate on the surface leads
to the gate of the copy at the same place: each player comes out in the other's world where the other
would come out in his.

What the game rolls at the start (mage guilds, creature banks, random artifacts, the split of the
monsters, ...) is rolled once for an object and its copy: the map's description carries the word
"arena-mirror", which the game reads (lib/gameState/CGameState.cpp, CGCreature::hashByPosition).

Taken out because they would not be the same for both: the grail and the obelisks (one grail for two
worlds), cartographers (they map one level - the other player's world for the one below), prisons (a
hero exists once). Monoliths of the copy get colours of their own, so that no monolith leads into
the other world.

  mirror_maps.py make --client build/bin/vcmiclient --out DIR [--template "Skirmish M"] [--size m]
                      [--factions castle,rampart,...] [--seed 52000]
  mirror_maps.py mirror BASE.vmap OUT.vmap
  mirror_maps.py check MAP.vmap
  mirror_maps.py install --src DIR --pool mirror-m     (into the game's Maps folder)
  mirror_maps.py play --src maps/mirror-m --pool mirror-m -- RUN_TOURNAMENT_PLAY_ARGS
                      (install, then run_tournament.py play on that pool: every map twice, seats swapped)
"""
import argparse
import copy
import io
import json
import re
import shutil
import subprocess
import sys
import zipfile
from collections import deque
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_tournament as rt  # noqa: E402

FACTIONS = ["castle", "rampart", "tower", "inferno", "necropolis", "dungeon", "stronghold", "fortress",
            "conflux", "hota.cove:cove", "hota.factory:factory", "hota.bulwark:bulwark"]

DROP = {"grail", "obelisk", "cartographer", "prison", "heroPlaceholder", "randomHero"}
MONOLITHS = {"monolithTwoWay": [f"monolith{i}" for i in range(1, 9)],
             "monolithOneWayEntrance": [f"monolith{i}" for i in range(1, 9)],
             "monolithOneWayExit": [f"monolith{i}" for i in range(1, 9)]}
GATE_TEMPLATE = {"animation": "AvTCave", "editorAnimation": "", "mask": ["VVVV", "VVBV", "VBAB"],
                 "visitableFrom": ["---", "+-+", "+++"]}
MARKER = "arena-mirror"
COPY = 100000  # the copy of "kind_N" is "kind_(N + COPY)": instance names end in a number (CMap::parseUidCounter)


# ------------------------------------------------------------------------------------- reading

def loads(text):
    """VCMI's JSON: comments (// ...) and trailing commas"""
    out = []
    i, n, in_str = 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\":
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            out.append(c)
        elif c == "/" and text[i + 1:i + 2] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        else:
            out.append(c)
        i += 1
    return json.loads(re.sub(r",(\s*[}\]])", r"\1", "".join(out)))


def read_map(path):
    with zipfile.ZipFile(path) as z:
        return {name: loads(z.read(name).decode("utf-8")) for name in z.namelist()}


def write_map(files, path):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, json.dumps(data, ensure_ascii=False, indent=1))
    Path(path).write_bytes(buf.getvalue())


# ------------------------------------------------------------------------------------ geometry

def footprint(obj):
    """(blocked tiles, visitable tiles) of an object; its position is the bottom right of its mask"""
    blocked, visitable = set(), set()
    mask = (obj.get("template") or {}).get("mask") or []
    ax, ay = obj["x"], obj["y"]
    for r, row in enumerate(reversed(mask)):
        for c, ch in enumerate(reversed(row)):
            tile = (ax - c, ay - r)
            if ch in "BA":
                blocked.add(tile)
            if ch == "A":
                visitable.add(tile)
    return blocked, visitable


class World:
    """one level: what can be walked on, and where objects stand"""

    def __init__(self, terrain, objects):
        self.h = len(terrain)
        self.w = len(terrain[0])
        self.land = [[not cell.startswith(("wt", "rc")) for cell in row] for row in terrain]
        self.blocked = set()
        self.visit = {}        # tile -> object
        self.monster_tiles = set()
        self.guarded = set()   # tiles next to a monster
        self.teleport = {}     # visitable tile of a monolith -> tiles it leads to
        channels = {}
        for obj in objects:
            b, v = footprint(obj)
            self.blocked |= b
            for t in v:
                self.visit[t] = obj
            if obj["type"] == "monster":
                for t in v:
                    self.monster_tiles.add(t)
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            self.guarded.add((t[0] + dx, t[1] + dy))
            if obj["type"] == "monolithTwoWay":
                channels.setdefault(obj["subtype"], []).extend(v)
        for tiles in channels.values():
            for t in tiles:
                self.teleport[t] = [o for o in tiles if o != t]

    def inside(self, t):
        return 0 <= t[0] < self.w and 0 <= t[1] < self.h

    def free(self, t):
        return self.inside(t) and self.land[t[1]][t[0]] and t not in self.blocked

    def distances(self, start_tiles):
        """steps from the start tiles; monsters count as passable (a fight), other objects end a way"""
        dist = {t: 0 for t in start_tiles}
        queue = deque(start_tiles)
        while queue:
            cur = queue.popleft()
            nexts = []
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx or dy:
                        nexts.append((cur[0] + dx, cur[1] + dy))
            if cur in self.teleport and dist[cur] > 0:
                nexts += self.teleport[cur]
            for t in nexts:
                if t in dist or not self.inside(t):
                    continue
                if self.free(t) or t in self.monster_tiles:
                    dist[t] = dist[cur] + 1
                    queue.append(t)
                elif t in self.visit:
                    dist[t] = dist[cur] + 1  # reached, but no way through
                    if t in self.teleport:
                        queue.append(t)
        return dist


def entry_tiles(world, obj):
    """free tiles from which an object can be visited (the row above its visitable tile excluded)"""
    _, vis = footprint(obj)
    out = []
    for (x, y) in vis:
        for dx in (-1, 0, 1):
            for dy in (0, 1):
                t = (x + dx, y + dy)
                if (dx or dy) and world.free(t):
                    out.append(t)
    return out


# -------------------------------------------------------------------------------------- mirror

def mirror(files, label=""):
    header = files["header.json"]
    objects = files["objects.json"]
    terrain = files["surface_terrain.json"]
    if "underground_terrain.json" in files or len(header.get("mapLevels", {})) != 1:
        raise ValueError("the base map must have one level")
    towns = {o.get("options", {}).get("owner"): o for o in objects if o["type"] == "town" and o.get("options", {}).get("owner")}
    if set(towns) != {"red", "blue"}:
        raise ValueError(f"the base map needs a red and a blue town, has {sorted(towns)}")
    red, blue = towns["red"], towns["blue"]

    kept = [o for o in objects if o is not blue and o["type"] not in DROP]
    dropped = sorted({o["type"] for o in objects if o["type"] in DROP})

    # where the two ways meet: a gate as far from the red town as from the blue one
    full = World(terrain, objects)
    d_red = full.distances(entry_tiles(full, red))
    d_blue = full.distances(entry_tiles(full, blue))
    world = World(terrain, kept)
    reach_before = {t for t in world.visit if t in d_red}
    gate_b, gate_v = footprint({"x": 0, "y": 0, "template": GATE_TEMPLATE})
    best = None
    for ay in range(world.h):
        for ax in range(world.w):
            cells = {(ax + x, ay + y) for (x, y) in gate_b | gate_v}
            if not all(world.free(c) and c not in world.visit and c not in world.guarded for c in cells):
                continue
            (vx, vy), = [(ax + x, ay + y) for (x, y) in gate_v]
            below = [t for t in ((vx - 1, vy + 1), (vx, vy + 1), (vx + 1, vy + 1)) if world.free(t) and t in d_red and t in d_blue]
            if not below:
                continue
            # near the shortest way between the two towns, and near its middle
            e = min(below, key=lambda t: (d_red[t] + d_blue[t] + 2 * abs(d_red[t] - d_blue[t])))
            score = (d_red[e] + d_blue[e] + 2 * abs(d_red[e] - d_blue[e]), abs(d_red[e] - d_blue[e]))
            if best is None or score < best[0]:
                best = (score, ax, ay, cells)
    if best is None:
        raise ValueError("no place for the gate")
    _, gx, gy, cells = best
    (vx, vy), = [(gx + x, gy + y) for (x, y) in gate_v]
    e = min((t for t in ((vx - 1, vy + 1), (vx, vy + 1), (vx + 1, vy + 1)) if t in d_red and t in d_blue),
            key=lambda t: abs(d_red[t] - d_blue[t]))
    gap, total = d_red[e] - d_blue[e], d_red[e] + d_blue[e]
    shortest = min(d_red[t] + d_blue[t] for t in d_red if t in d_blue)
    gate = {"instanceName": f"subterraneanGate_{COPY - 1}", "l": 0, "subtype": "object", "template": GATE_TEMPLATE,
            "type": "subterraneanGate", "x": gx, "y": gy}
    # the gate must not cut anything off
    after = World(terrain, kept + [gate])
    d_after = after.distances(entry_tiles(after, red))
    lost = [t for t in reach_before if t not in d_after and t not in cells]
    if lost:
        raise ValueError(f"the gate at {gx},{gy} would cut off {len(lost)} objects")

    # monoliths of the copy: colours not used on the surface
    used = {}
    for o in kept:
        if o["type"] in MONOLITHS:
            used.setdefault(o["type"], set()).add(o["subtype"])
    recolour = {}
    for kind, subs in used.items():
        free = [s for s in MONOLITHS[kind] if s not in subs]
        if len(free) < len(subs):
            raise ValueError(f"not enough colours of {kind} for the copy")
        for old, new in zip(sorted(subs), free):
            recolour[(kind, old)] = new

    surface = kept + [gate]
    names = {o["instanceName"] for o in surface}
    for name in names:
        prefix, _, index = name.rpartition("_")
        if not index.isdigit() or int(index) >= COPY - 1 and name != gate["instanceName"]:
            raise ValueError(f"instance name {name} does not end in a small number")

    def copy_name(name):
        prefix, _, index = name.rpartition("_")
        return f"{prefix}_{int(index) + COPY}"

    def rename(value):
        if isinstance(value, dict):
            return {k: rename(v) for k, v in value.items()}
        if isinstance(value, list):
            return [rename(v) for v in value]
        if isinstance(value, str) and value in names:
            return copy_name(value)
        return value

    underground = []
    for o in surface:
        c = copy.deepcopy(o)
        c["l"] = 1
        c["instanceName"] = copy_name(o["instanceName"])
        if "options" in c:
            c["options"] = rename(c["options"])
            if c["options"].get("owner") == "red":
                c["options"]["owner"] = "blue"
        if (c["type"], c.get("subtype")) in recolour:
            c["subtype"] = recolour[(c["type"], c["subtype"])]
        underground.append(c)

    out_header = copy.deepcopy(header)
    out_header["mapLevels"]["underground"] = {"height": len(terrain), "index": 1, "layer": "core:underground", "width": len(terrain[0])}
    players = out_header["players"]
    players["blue"] = copy.deepcopy(players["red"])
    if "mainTown" in players["blue"]:
        players["blue"]["mainTown"]["l"] = 1
    faction = red.get("subtype", "?")
    text = f"Mirror map {label}: {faction} against {faction}; the underground is a copy of the surface ({MARKER})"
    out_header["name"] = {"exactStrings": [f"Mirror {faction} {label}".strip()], "localStrings": None, "message": [0], "numbers": None, "stringsTextID": None}
    out_header["description"] = {"exactStrings": [text], "localStrings": None, "message": [0], "numbers": None, "stringsTextID": None}

    out = dict(files)
    out["header.json"] = out_header
    out["objects.json"] = surface + underground
    out["underground_terrain.json"] = copy.deepcopy(terrain)
    info = {"faction": faction, "gate": [gx, gy], "gap": gap, "way": total, "shortest": shortest, "dropped": dropped,
            "recoloured": {f"{k}:{o}": n for (k, o), n in recolour.items()}, "objects": len(surface)}
    return out, info


def check(files):
    """the two levels are the same, but for owners, names and monolith colours"""
    objs = files["objects.json"]
    top = {(o["x"], o["y"], o["type"]): o for o in objs if o["l"] == 0}
    low = {(o["x"], o["y"], o["type"]): o for o in objs if o["l"] == 1}
    problems = []
    if set(top) != set(low):
        problems.append(f"objects differ: {len(set(top) ^ set(low))}")
    for key, o in top.items():
        c = low.get(key)
        if not c:
            continue
        a = json.dumps(o.get("options", {}), sort_keys=True).replace('"red"', '"blue"')
        b = re.sub(r"_(\d+)\b", lambda m: "_" + str(int(m.group(1)) - COPY) if int(m.group(1)) >= COPY else m.group(0),
                   json.dumps(c.get("options", {}), sort_keys=True))
        if a != b:
            problems.append(f"options differ at {key}")
        if o.get("template") != c.get("template"):
            problems.append(f"template differs at {key}")
    if files["surface_terrain.json"] != files.get("underground_terrain.json"):
        problems.append("terrain differs")
    gates = [o for o in objs if o["type"] == "subterraneanGate"]
    if len(gates) != 2 or (gates[0]["x"], gates[0]["y"]) != (gates[1]["x"], gates[1]["y"]):
        problems.append(f"gates: {[(g['x'], g['y'], g['l']) for g in gates]}")
    players = files["header.json"]["players"]
    if set(players) != {"red", "blue"}:
        problems.append(f"players: {sorted(players)}")
    return problems


# ------------------------------------------------------------------------------------ commands

def cmd_make(args):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    pool = "mirror-base"
    base_dir = rt.vcmi_data_dir() / "Maps" / pool
    factions = args.factions.split(",") if args.factions else FACTIONS
    report = []
    for i, faction in enumerate(factions):
        made = None
        for attempt in range(args.attempts):
            seed = args.seed + 100 * i + attempt
            cmd = [sys.executable, str(HERE / "run_tournament.py"), "genmaps", "--client", args.client, "--count", "1",
                   "--template", args.template, "--size", args.size, "--players", "2", "--towns", f"{faction},{faction}",
                   "--pool", pool, "--mods", args.mods, "--levels", "1", "--water", args.water, "--seed", str(seed), "--force"]
            res = subprocess.run(cmd, capture_output=True, text=True)
            made_files = [p for p in base_dir.glob(f"*_{seed}.vmap")]
            if res.returncode != 0 or not made_files:
                print(f"{faction} seed {seed}: generation failed", file=sys.stderr)
                continue
            try:
                files, info = mirror(read_map(made_files[0]), label=str(seed))
            except ValueError as e:
                print(f"{faction} seed {seed}: {e}", file=sys.stderr)
                continue
            problems = check(files)
            if problems:
                print(f"{faction} seed {seed}: {problems[:3]}", file=sys.stderr)
                continue
            name = f"mirror_{faction.split(':')[-1]}_{args.size}_{seed}.vmap"
            write_map(files, out / name)
            info.update({"map": name, "seed": seed})
            report.append(info)
            made = name
            print(json.dumps(info))
            break
        if not made:
            print(f"{faction}: no map after {args.attempts} attempts", file=sys.stderr)
    (out / "mirror_maps.json").write_text(json.dumps(report, indent=1))
    return 0 if len(report) == len(factions) else 1


def cmd_mirror(args):
    files, info = mirror(read_map(args.base), label=Path(args.base).stem)
    problems = check(files)
    write_map(files, args.out)
    print(json.dumps(info))
    print("check:", problems or "ok")
    return 0 if not problems else 1


def cmd_check(args):
    problems = check(read_map(args.map))
    print(problems or "ok")
    return 0 if not problems else 1


def cmd_install(args):
    dest = rt.vcmi_data_dir() / "Maps" / args.pool
    dest.mkdir(parents=True, exist_ok=True)
    n = 0
    for f in sorted(Path(args.src).glob("*.vmap")):
        shutil.copy2(f, dest / f.name)
        n += 1
    print(f"{n} maps in {dest}")
    return 0 if n else 1


def cmd_play(args, rest):
    src = Path(args.src)
    if not src.is_absolute():
        src = HERE / src
    args.src = str(src)
    if cmd_install(args) != 0:
        return 1
    if rest and rest[0] == "--":
        rest = rest[1:]
    cmd = [sys.executable, str(HERE / "run_tournament.py"), "play", "--pool", args.pool, *rest]
    print(" ".join(cmd), flush=True)
    return subprocess.run(cmd).returncode


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--client", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--template", default="Skirmish M")
    m.add_argument("--size", default="m")
    m.add_argument("--water", default="none")
    m.add_argument("--mods", default="hota")
    m.add_argument("--factions", default="")
    m.add_argument("--seed", type=int, default=52000)
    m.add_argument("--attempts", type=int, default=8)
    r = sub.add_parser("mirror")
    r.add_argument("base")
    r.add_argument("out")
    c = sub.add_parser("check")
    c.add_argument("map")
    i = sub.add_parser("install")
    i.add_argument("--src", required=True)
    i.add_argument("--pool", default="mirror-m")
    g = sub.add_parser("play")
    g.add_argument("--src", default="maps/mirror-m", help="folder of mirror maps (relative: to arena/)")
    g.add_argument("--pool", default="mirror-m")
    args, rest = p.parse_known_args()
    if args.cmd == "play":
        return cmd_play(args, rest)
    if rest:
        p.error(f"unknown arguments: {rest}")
    return {"make": cmd_make, "mirror": cmd_mirror, "check": cmd_check, "install": cmd_install}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
