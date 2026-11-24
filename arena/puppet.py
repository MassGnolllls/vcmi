#!/usr/bin/env python3
"""
puppet.py - battles played from outside the game: both sides by strategies written here.

The game is the referee: FairBattleAI describes the battle before every decision of a side that is
played from outside (AI/FairAI/Combat/Puppet.h, FAI_PUPPET_PORT) and plays the action it is told.
A side can also be left to a combat AI (FairAI, BattleAI, StupidAI). Three ways to play a battle:
exactly as the game does, without the dice of morale and luck (--nodice), and with a fixed seed.

  puppet.py gen   --client CLIENT --scen SCEN.json [--mods hota]            build the battle map
  puppet.py play  --client CLIENT --scen SCEN.json --left WHO --right WHO   play it
                  [--seeds 20] [--first-seed 1] [--nodice] [--jobs 2] [--out DIR] [--show]
  puppet.py show  TRACE.json [--step N]                                     the field before action N

WHO: a strategy of arena/puppet_plays.py, or fairai / battleai / stupidai (that combat AI plays).
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_tournament as rt  # noqa: E402

SUBDIR = "puppet"
W, H = 17, 11  # the battlefield: 17 x 11 hexes, the outer columns (x = 0 and 16) are not entered
FIELD = W * H
ENGINE_AI = {"fairai": "FairAI", "battleai": "BattleAI", "stupidai": "StupidAI"}


# ------------------------------------------------------------------------------------------ geometry

def hx(x, y):
    return x + W * y


def xy(h):
    return h % W, h // W


def _neighbours(h):
    x, y = xy(h)
    odd = y % 2
    cand = [(x - 1 if odd else x, y - 1), (x if odd else x + 1, y - 1), (x + 1, y),
            (x if odd else x + 1, y + 1), (x - 1 if odd else x, y + 1), (x - 1, y)]
    return tuple(hx(a, b) for a, b in cand if 0 <= a < W and 0 <= b < H)


NB = [_neighbours(h) for h in range(FIELD)]
ONFIELD = [1 <= h % W <= 15 for h in range(FIELD)]


def dist(a, b):
    """distance in hexes as the game counts it (BattleHex::getDistance)"""
    x1, y1 = xy(a)
    x2, y2 = xy(b)
    x1 += y1 // 2
    x2 += y2 // 2
    dx, dy = x2 - x1, y2 - y1
    if (dx >= 0 and dy >= 0) or (dx < 0 and dy < 0):
        return max(abs(dx), abs(dy))
    return abs(dx) + abs(dy)


def unit_hexes(unit, pos=None):
    pos = unit["pos"] if pos is None else pos
    if unit["wide"]:
        return (pos, pos - 1 if unit["side"] == 0 else pos + 1)
    return (pos,)


class State:
    """One description of the battle, with the questions strategies ask about it."""

    def __init__(self, msg):
        self.m = msg
        self.round = msg["round"]
        self.units = {u["id"]: u for u in msg["units"]}
        self.obst = frozenset(msg.get("obst", []))
        self.active = self.units.get(msg.get("active", -1))
        self.side = msg.get("side", self.active["side"] if self.active else 0)
        self.reach_now = msg.get("reach", [])
        self.attacks = [tuple(p) for p in msg.get("attacks", [])]  # (target id, hex to strike from)
        self.shoot = msg.get("shoot", [])

    # --- who is where
    def mine(self):
        return [u for u in self.units.values() if u["side"] == self.side]

    def foes(self, side=None):
        side = self.side if side is None else side
        return [u for u in self.units.values() if u["side"] != side]

    def by_slot(self, side, slot):
        for u in self.units.values():
            if u["side"] == side and u["slot"] == slot:
                return u
        return None

    def by_type(self, side, name):
        return [u for u in self.units.values() if u["side"] == side and u["type"].endswith(name)]

    def occupied(self, without=()):
        occ = set()
        for u in self.units.values():
            if u["id"] in without:
                continue
            occ.update(unit_hexes(u))
        return occ

    # --- movement, as the game computes it (CBattleInfoCallback::makeBFS and the rule for flyers)
    def can_stand(self, unit, pos, occ):
        for h in unit_hexes(unit, pos):
            if h < 0 or h >= FIELD or not ONFIELD[h] or h in self.obst or h in occ:
                return False
        return True

    def distances(self, unit, occ=None, start=None):
        """hex -> steps for the unit's head; occ: hexes taken by others (default: as things stand)"""
        if occ is None:
            occ = self.occupied(without=(unit["id"],))
        start = unit["pos"] if start is None else start
        out = {start: 0}
        if unit["fly"]:
            for h in range(FIELD):
                if h != start and self.can_stand(unit, h, occ):
                    out[h] = dist(start, h)
            return out
        queue = deque([start])
        while queue:
            cur = queue.popleft()
            for n in NB[cur]:
                if n not in out and self.can_stand(unit, n, occ):
                    out[n] = out[cur] + 1
                    queue.append(n)
        return out

    def reach(self, unit, occ=None, start=None, speed=None):
        speed = unit["speed"] if speed is None else speed
        if not unit.get("canMove", True):
            speed = 0
        return {h: d for h, d in self.distances(unit, occ, start).items() if d <= speed}

    @staticmethod
    def touching(hexes_a, hexes_b):
        for a in hexes_a:
            for b in hexes_b:
                if b in NB[a]:
                    return True
        return False

    def strike_hexes(self, attacker, target_hexes, occ=None, start=None, speed=None):
        """the hexes from which `attacker` could strike something standing on target_hexes in one turn"""
        if occ is None:
            occ = self.occupied(without=(attacker["id"],))
        return [h for h in self.reach(attacker, occ, start, speed) if self.touching(unit_hexes(attacker, h), target_hexes)]

    def can_strike_fast(self, attacker, target, target_pos=None, speed=None, ignore_allies=False):
        """as can_strike, but the attacker's ways are computed once per state, as if the target were not
        there (never too short). ignore_allies: its own side's other units step aside for it."""
        speed = attacker["speed"] if speed is None else speed
        if not attacker.get("canMove", True):
            speed = 0
        without = [attacker["id"], target["id"]]
        if ignore_allies:
            without += [u["id"] for u in self.units.values() if u["side"] == attacker["side"]]
        key = (attacker["id"], tuple(sorted(set(without))))
        cache = self.__dict__.setdefault("_dist_cache", {})
        if key not in cache:
            cache[key] = self.distances(attacker, self.occupied(without=tuple(without)))
        d = cache[key]
        hexes = unit_hexes(target, target_pos)
        taken = set(hexes)
        for th in hexes:
            for n in NB[th]:
                heads = (n,) if not attacker["wide"] else (n, n + 1 if attacker["side"] == 0 else n - 1)
                for head in heads:
                    steps = d.get(head)
                    if steps is None or steps > speed:
                        continue
                    mine = unit_hexes(attacker, head)
                    if any(x in taken for x in mine):
                        continue
                    if State.touching(mine, hexes):
                        return True
        return False

    def can_strike(self, attacker, target, target_pos=None, speed=None):
        """could `attacker` strike `target` (standing on target_pos) in its next turn?"""
        occ = self.occupied(without=(attacker["id"], target["id"]))
        hexes = unit_hexes(target, target_pos)
        occ.update(hexes)
        return bool(self.strike_hexes(attacker, hexes, occ, speed=speed))


def render(msg, mark=None):
    """the field as text: A-G our... the attacker's stacks by slot, a-g the defender's, # obstacle"""
    st = State(msg)
    grid = {}
    for h in st.obst:
        grid[h] = "#"
    for u in st.units.values():
        ch = chr((ord("A") if u["side"] == 0 else ord("a")) + u["slot"])
        for h in unit_hexes(u):
            grid[h] = ch
    for h in (mark or []):
        grid[h] = "*" if grid.get(h, ".") == "." else grid[h]
    lines = ["     " + " ".join(f"{x:>2}" for x in range(1, 16))]
    for y in range(H):
        row = " ".join(f"{grid.get(hx(x, y), '.'):>2}" for x in range(1, 16))
        lines.append(f"{y:>2}  " + (" " if y % 2 == 0 else "") + row)
    legend = []
    for u in sorted(st.units.values(), key=lambda u: (u["side"], u["slot"])):
        ch = chr((ord("A") if u["side"] == 0 else ord("a")) + u["slot"])
        x, y = xy(u["pos"])
        legend.append(f"{ch}={u['count']} {u['type'].split(':')[-1]}@({x},{y}) hp{u['hp']} sp{u['speed']}"
                      + ("" if u["retal"] else " noretal") + (" waited" if u["waited"] else ""))
    return "\n".join(lines) + "\n" + "\n".join(legend)


# ------------------------------------------------------------------------------------------ the listener

class Brain(threading.Thread):
    """Listens on a port of this machine, answers the game's questions with the sides' strategies and
    keeps the record of the battle."""

    def __init__(self, strategies):
        super().__init__(daemon=True)
        self.strategies = strategies  # side -> function(State, memory) -> action, or None
        self.memory = [{}, {}]
        self.trace = []
        self.errors = []
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(4)
        self.port = self.server.getsockname()[1]
        self.done = False
        self.round = 0  # the latest round heard of (play() ends battles that go on for ever)

    def run(self):
        self.server.settimeout(1.0)
        while not self.done:
            try:
                conn, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            try:
                self.serve(conn)
            except Exception as e:  # noqa: BLE001 - the battle goes on with the combat AI
                self.errors.append(f"listener: {e!r}")
            finally:
                conn.close()

    def serve(self, conn):
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        stream = conn.makefile("rwb")
        for raw in stream:
            msg = json.loads(raw)
            self.round = max(self.round, msg.get("round", 0))
            if msg.get("t") == "ask":
                side = msg["side"]
                action = {"a": "auto"}
                strategy = self.strategies.get(side)
                if strategy:
                    try:
                        action = strategy(State(msg), self.memory[side]) or {"a": "auto"}
                    except Exception as e:  # noqa: BLE001
                        import traceback
                        self.errors.append(f"strategy of side {side}, round {msg['round']}: {e!r}\n{traceback.format_exc()}")
                self.trace.append({"t": "answer", "side": side, "unit": msg["active"], "round": msg["round"], "answer": action})
                stream.write((json.dumps(action) + "\n").encode())
                stream.flush()
            elif len(self.trace) < 6000:
                self.trace.append(msg)

    def close(self):
        self.done = True
        try:
            self.server.close()
        except OSError:
            pass


# ------------------------------------------------------------------------------------------ playing

def map_name(scen):
    return rt_safe(scen["name"])


def rt_safe(name):
    return "".join(c if c.isalnum() else "_" for c in name)


def generate(client, scen, mods="hota"):
    """the battle map of a scenario (written into the game's map folder); returns its name for --testmap"""
    maps_dir = rt.vcmi_data_dir() / "Maps" / SUBDIR
    maps_dir.mkdir(parents=True, exist_ok=True)
    out = maps_dir / (map_name(scen) + ".vmap")
    config_home = rt.make_config_home(mods) if os.name != "nt" else None
    try:
        env = rt.client_env(config_home, client) if config_home else dict(os.environ)
        tmp = out.with_suffix(".scen.json")
        tmp.write_text(json.dumps(scen))
        res = subprocess.run([client, "--headless", "--arena-genbattle", str(tmp), "--arena-out", str(out)],
                             capture_output=True, text=True, timeout=600, env=env)
        tmp.unlink(missing_ok=True)
        if "ARENA_BATTLE_MAP " not in res.stdout:
            raise RuntimeError("map not built: " + (res.stdout + res.stderr)[-400:])
    finally:
        if config_home:
            shutil.rmtree(config_home, ignore_errors=True)
    return f"MAPS/{SUBDIR.upper()}/{out.stem.upper()}"


def play(client, testmap, left, right, seed, out_dir, tag, nodice=False, mods="hota", base_port=3400, timeout=600, keep_trace=True, max_rounds=250):
    """One battle. left / right: a function (strategy) or the name of a combat AI. Returns the record."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    result_file = out_dir / f"result_{tag}.json"
    log_dir = out_dir / f"logs_{tag}"
    log_dir.mkdir(parents=True, exist_ok=True)
    result_file.unlink(missing_ok=True)

    strategies = {0: left if callable(left) else None, 1: right if callable(right) else None}
    brain = Brain(strategies)
    brain.start()

    config_home = None
    env = dict(os.environ)
    left_ai = "FairAI" if callable(left) else ENGINE_AI[left]
    right_ai = "FairAI" if callable(right) else ENGINE_AI[right]
    os_env_neutral = {"ARENA_NEUTRAL_AI": right_ai}
    if os.name != "nt":
        old = os.environ.get("ARENA_NEUTRAL_AI")
        os.environ["ARENA_NEUTRAL_AI"] = right_ai  # (make_config_home reads it; parallel battles of one call share it)
        config_home = rt.make_config_home(mods)
        if old is None:
            os.environ.pop("ARENA_NEUTRAL_AI", None)
        else:
            os.environ["ARENA_NEUTRAL_AI"] = old
        env = rt.client_env(config_home, client)
    env.update(os_env_neutral)
    env["FAI_PUPPET_PORT"] = str(brain.port)
    env["FAI_PUPPET_SIDES"] = ("0" if callable(left) else "") + ("1" if callable(right) else "") or "-"
    env["FAI_STATS"] = str(log_dir / "fairai")
    if nodice:
        env["ARENA_NO_MORALE_LUCK"] = "1"
    else:
        env.pop("ARENA_NO_MORALE_LUCK", None)

    cmd = [client, "--headless", "--testmap", testmap, "--ai", "FairAI", "--ai", "FairAI",
           "--arena-battle-ai", left_ai, "--arena-battle-ai", "BattleAI",
           "--arena-result", str(result_file), "--arena-max-days", "3", "--arena-seed", str(seed),
           "--arena-difficulty", "4", "--arena-stall-seconds", "120",
           "--serverport", str(base_port + (hash(tag) + seed) % 2000), "--logLocation", str(log_dir)]
    record = {"tag": tag, "seed": seed, "nodice": nodice}
    started = time.time()
    try:
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while True:
            try:
                record["returncode"] = proc.wait(timeout=1.0)
                break
            except subprocess.TimeoutExpired:
                # the game knows no draw: a battle in which nobody gets anywhere is ended here
                if brain.round > max_rounds or time.time() - started > timeout:
                    proc.kill()
                    proc.wait()
                    record["error"] = f"no end after {brain.round} rounds" if brain.round > max_rounds else "timeout"
                    record["rounds"] = brain.round
                    break
        if "error" not in record:
            if result_file.exists():
                record["result"] = json.loads(result_file.read_text())
            else:
                record["error"] = "no result"
    finally:
        brain.close()
        if config_home:
            shutil.rmtree(config_home, ignore_errors=True)
    record["seconds"] = round(time.time() - started, 1)
    record.setdefault("rounds", brain.round)
    if brain.errors:
        record["strategy_errors"] = brain.errors[:5]
    record["actions"] = sum(1 for t in brain.trace if t.get("t") == "act")
    if keep_trace:
        (out_dir / f"trace_{tag}.json").write_text(json.dumps(brain.trace))
    if not record.get("error") and not os.environ.get("PUPPET_KEEP_LOGS"):
        shutil.rmtree(log_dir, ignore_errors=True)
    return record


def outcome(record):
    """(winner side or None, value lost left, value lost right, losses left {type: n}, losses right) of a record"""
    res = record.get("result") or {}
    battles = res.get("battles") or res.get("playerBattles") or []
    return battles


def resolve(name):
    if name in ENGINE_AI:
        return name
    import puppet_plays
    return puppet_plays.PLAYS[name]


def cmd_gen(args):
    scen = json.loads(Path(args.scen).read_text())
    print(generate(args.client, scen, args.mods))
    return 0


def cmd_play(args):
    scen = json.loads(Path(args.scen).read_text())
    testmap = generate(args.client, scen, args.mods)
    left, right = resolve(args.left), resolve(args.right)
    out = Path(args.out)
    seeds = list(range(args.first_seed, args.first_seed + args.seeds))

    def one(seed):
        return play(args.client, testmap, left, right, seed, out, f"{map_name(scen)}_{args.left}_{args.right}_{'n' if args.nodice else 'x'}{seed}",
                    nodice=args.nodice, mods=args.mods)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        records = list(pool.map(one, seeds))
    with open(out / "records.jsonl", "a") as sink:
        for rec in records:
            sink.write(json.dumps(rec) + "\n")
    for rec in records:
        print(json.dumps({k: v for k, v in rec.items() if k != "result"})[:300])
        if args.show:
            print(json.dumps(rec.get("result"))[:1500])
    return 0


def cmd_show(args):
    trace = json.loads(Path(args.trace).read_text())
    acts = [t for t in trace if t.get("t") == "act"]
    names = {5: "walk", 6: "wait", 7: "defend", 8: "attack", 9: "shoot", 12: "bad morale"}
    if args.step is not None:
        steps = [args.step]
    elif args.all:
        steps = range(len(acts))
    else:
        steps = [0]
    for i in steps:
        a = acts[i]
        st = State(a)
        u = st.active
        print(f"--- action {i}, round {a['round']}: side {a['side']} {u['count'] if u else '?'} {u['type'].split(':')[-1] if u else '?'}"
              f" {names.get(a['type'], a['type'])} {[xy(h) for h in a.get('aim', [])]}")
        if not args.brief:
            print(render(a, mark=a.get("aim")))
    return 0


def cmd_list(args):
    trace = json.loads(Path(args.trace).read_text())
    acts = [t for t in trace if t.get("t") == "act"]
    names = {5: "walk", 6: "wait", 7: "defend", 8: "attack", 9: "shoot", 12: "badmorale"}
    for i, a in enumerate(acts):
        st = State(a)
        u = st.active
        counts = " ".join(f"{chr((65 if v['side'] == 0 else 97) + v['slot'])}{v['count']}" for v in sorted(st.units.values(), key=lambda v: (v["side"], v["slot"])))
        print(f"{i:>3} r{a['round']} {'L' if a['side'] == 0 else 'R'} {chr((65 if u['side'] == 0 else 97) + u['slot']) if u else '?'}"
              f" {names.get(a['type'], a['type']):<7} {str([xy(h) for h in a.get('aim', [])]):<20} | {counts}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("gen")
    p.add_argument("--client", required=True)
    p.add_argument("--scen", required=True)
    p.add_argument("--mods", default="hota")
    p.set_defaults(func=cmd_gen)
    p = sub.add_parser("play")
    p.add_argument("--client", required=True)
    p.add_argument("--scen", required=True)
    p.add_argument("--left", required=True)
    p.add_argument("--right", required=True)
    p.add_argument("--seeds", type=int, default=1)
    p.add_argument("--first-seed", type=int, default=1)
    p.add_argument("--nodice", action="store_true")
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--out", default="puppet_out")
    p.add_argument("--mods", default="hota")
    p.add_argument("--show", action="store_true")
    p.set_defaults(func=cmd_play)
    p = sub.add_parser("show")
    p.add_argument("trace")
    p.add_argument("--step", type=int)
    p.add_argument("--all", action="store_true")
    p.add_argument("--brief", action="store_true")
    p.set_defaults(func=cmd_show)
    p = sub.add_parser("list")
    p.add_argument("trace")
    p.set_defaults(func=cmd_list)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
