#!/usr/bin/env python3
"""
Arena tournament runner for VCMI AIs (headless AI-vs-AI matches).

Plays every map twice with swapped seats so that map/start-position advantages cancel out,
collects the JSON results written by the client (--arena-result) and prints a summary.

Typical use:
  # 1) generate 10 small 2-player maps once
  python3 arena/run_tournament.py genmaps --client build/bin/vcmiclient --count 10 --template 2SM4d --size s

  # 2) play FairAI against Nullkiller2 on all arena maps
  python3 arena/run_tournament.py play --client build/bin/vcmiclient --ai FairAI --opponent Nullkiller2 \
      --max-days 120 --jobs 2 --out arena-results/fair-vs-nk2

The client must be built from the FairAI fork (it contains the --arena-* options).
"""
import argparse
import collections
import json
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def vcmi_data_dir() -> Path:
    """User data dir where VCMI looks for Maps/ (Linux: XDG_DATA_HOME/vcmi)."""
    if os.name == "nt":
        return Path(os.environ["USERPROFILE"]) / "Documents" / "My Games" / "vcmi"
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "vcmi"


ARENA_MAP_SUBDIR = "arena"

# Settings for arena clients: quiet logs (full trace logs are ~0.3 MB per game day) and no
# mod validation (missing music/video files of a minimal data install are irrelevant here)
ARENA_SETTINGS = {
    "logging": {
        "console": {"threshold": "error"},
        "loggers": [{"domain": "global", "level": "info"}, {"domain": "rng", "level": "info"}],
    },
    "mods": {"validation": "off"},
}

# Mods active in arena runs: MMAI (machine-learning combat AI) needs its mod for the model files
ARENA_MOD_SETTINGS = {
    "activePreset": "default",
    "presets": {"default": {"mods": ["vcmi", "core", "mmai"], "settings": {}}},
}


COLORS = ["red", "blue", "tan", "green", "orange", "purple", "teal", "pink"]


def make_config_home(mods="", ai_log=None) -> Path:
    """Private settings directory for one client run (parallel clients would race on a shared one)."""
    config_home = Path(tempfile.mkdtemp(prefix="vcmi-arena-cfg-"))
    (config_home / "vcmi").mkdir(parents=True, exist_ok=True)
    settings = json.loads(json.dumps(ARENA_SETTINGS))
    if ai_log:
        settings["logging"]["loggers"].append({"domain": "ai", "level": ai_log})
    # ARENA_NEUTRAL_AI=StupidAI (or MMAI, FairAI ...): the combat AI of neutral troops for this run (the game's default is BattleAI)
    if os.environ.get("ARENA_NEUTRAL_AI"):
        settings["ai"] = {"combatNeutralAI": os.environ["ARENA_NEUTRAL_AI"]}
    mod_settings = json.loads(json.dumps(ARENA_MOD_SETTINGS))
    for mod in filter(None, (mods or "").split(",")):
        if mod not in mod_settings["presets"]["default"]["mods"]:
            mod_settings["presets"]["default"]["mods"].append(mod)
    (config_home / "vcmi" / "settings.json").write_text(json.dumps(settings, indent=1))
    (config_home / "vcmi" / "modSettings.json").write_text(json.dumps(mod_settings, indent=1))
    return config_home


def client_env(config_home: Path, client=None) -> dict:
    env = dict(os.environ)
    env["XDG_CONFIG_HOME"] = str(config_home)
    env["XDG_CACHE_HOME"] = str(config_home / "cache")
    if client and os.name != "nt":
        # a copied build (snapshot) must load its own libvcmi.so: the run path baked into the binary
        # points at the build directory, where a rebuild replaces the library under running tests
        own = str(Path(client).resolve().parent)
        env["LD_LIBRARY_PATH"] = own + (os.pathsep + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    return env


def map_file(map_name: str):
    """'MAPS/ARENA/NAME' -> path of the map file (any extension), or None"""
    folder, _, stem = map_name.rpartition("/")
    sub = folder.split("/", 1)[1] if "/" in folder else ARENA_MAP_SUBDIR
    for directory in (vcmi_data_dir() / "Maps").glob("*"):
        if directory.is_dir() and directory.name.upper() == sub.upper():
            for path in directory.glob("*"):
                if path.stem.upper() == stem.upper():
                    return path
    return None


def map_seat_groups(map_name: str):
    """Teams of a map as lists of colours, in colour order (free for all: one colour per group).
    Read from the header of .vmap files; other formats are treated as two players, red and blue."""
    import re
    import zipfile
    path = map_file(map_name)
    try:
        with zipfile.ZipFile(path) as z:
            text = z.read("header.json").decode("utf-8", errors="replace")
        header = json.loads(re.sub(r"(?m)^\s*//[^\n]*", "", text))
    except Exception:
        return [["red"], ["blue"]]
    colours = [c for c in COLORS if c in (header.get("players") or {})]
    groups = []
    seen = set()
    for team in header.get("teams") or []:
        members = [c for c in COLORS if c in team and c in colours]
        if members:
            groups.append(members)
            seen.update(members)
    groups += [[c] for c in colours if c not in seen]
    groups.sort(key=lambda g: COLORS.index(g[0]))
    return groups or [["red"], ["blue"]]


def game_score(record: dict):
    """Score of the AI under test in one game: 1 = won, 0 = lost, otherwise its army share at the day
    limit (heroes and garrisons), per seat (so 0.5 means parity however many seats each AI holds). None if the game failed."""
    result = record.get("result") or {}
    players = result.get("players") or {}
    if not players or result.get("outcome") == "stalled":
        return None
    mine = record.get("mine") or (["blue"] if record.get("match", 0) % 2 else ["red"])
    others = [c for c in players if c not in mine]
    winner = result.get("winner") or ""
    if result.get("outcome") == "victory" and winner:
        return 1.0 if winner in mine else 0.0
    # The game's own figure counts the armies of heroes only. What stands in towns without a hero is
    # army as well (day by day in the timeline, column 13): FairAI keeps a third to a half of its
    # troops there, Nullkiller2 next to nothing - until 18.11.2026 an undecided game was scored
    # without them, which flattered every switch that moves troops from garrisons to heroes.
    last = ((result.get("timeline") or [{}])[-1].get("players")) or {}

    def army(colour):
        row = last.get(colour) or []
        return players[colour].get("armyStrength", 0) + (row[12] if len(row) > 12 else 0)

    my = sum(army(c) for c in mine if c in players) / max(1, len(mine))
    their = sum(army(c) for c in others) / max(1, len(others))
    return my / (my + their) if my + their > 0 else 0.5


# ----------------------------------------------------------------------------- maps

def cmd_genmaps(args) -> int:
    maps_dir = vcmi_data_dir() / "Maps" / (args.pool or ARENA_MAP_SUBDIR)
    maps_dir.mkdir(parents=True, exist_ok=True)
    ok = 0
    # several town lists separated by ';' are used in turn (map i gets list i modulo their number):
    # a pool in which every faction starts equally often
    towns_cycle = [t for t in args.towns.split(";") if t] if args.towns else []
    for i in range(args.count):
        seed = args.seed + i
        tag = ""
        if args.players != 2 or args.teams or args.towns or args.tag:
            tag = f"_p{args.players}" + ("t" if args.teams else "") + (f"_{args.tag}" if args.tag else "")
        name = f"{args.template}_{args.size}_l{args.levels}_{args.water}{tag}_{seed}.vmap"
        name = "".join(ch for ch in name if ch.isalnum() or ch in "_.-")  # no spaces or brackets in file names
        out = maps_dir / name
        if out.exists() and not args.force:
            print(f"exists: {out}")
            ok += 1
            continue
        cmd = [args.client, "--headless", "--arena-genmap", str(out), "--arena-template", args.template,
               "--arena-size", args.size, "--arena-levels", str(args.levels), "--arena-seed", str(seed),
               "--arena-water", args.water, "--arena-monsters", args.monsters, "--arena-players", str(args.players)]
        if args.teams:
            cmd += ["--arena-teams", args.teams]
        if towns_cycle:
            cmd += ["--arena-towns", towns_cycle[i % len(towns_cycle)]]
        config_home = make_config_home(args.mods)
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=900, env=client_env(config_home, args.client))
        finally:
            shutil.rmtree(config_home, ignore_errors=True)
        if res.returncode == 0 and out.exists():
            print(f"generated: {out}")
            ok += 1
        else:
            print(f"FAILED seed {seed}: rc={res.returncode}\n{res.stdout[-2000:]}\n{res.stderr[-2000:]}")
    print(f"{ok}/{args.count} maps ready in {maps_dir}")
    return 0 if ok == args.count else 1


def list_arena_maps(pattern: str, pool: str = ""):
    pool = pool or ARENA_MAP_SUBDIR
    maps_dir = vcmi_data_dir() / "Maps" / pool
    maps = sorted(p for p in maps_dir.glob("*.vmap") if pattern in p.name)
    return [f"MAPS/{pool.upper()}/{p.stem.upper()}" for p in maps]


# ----------------------------------------------------------------------------- matches

def play_match(args, match_id: int, map_name: str, ai_red, ai_blue: str, seed: int, out_dir: Path,
               battle_mode: bool = False) -> dict:
    """ai_red/ai_blue: labels of the two seats, or (ai_red = dict colour -> label, ai_blue = list of the
    colours played by the AI under test) for maps with more players."""
    seats = None
    mine = None
    if isinstance(ai_red, dict):
        seats, mine = ai_red, list(ai_blue)
        ai_red, ai_blue = seats.get("red", ""), seats.get("blue", "")
    result_file = out_dir / f"match_{match_id:04d}.json"
    log_dir = out_dir / f"logs_{match_id:04d}"
    log_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    config_home = None
    if os.name != "nt":
        # isolate settings.json per match: parallel clients would otherwise race on it
        # ARENA_AI_LOG, e.g. "debug": detailed AI decisions in the match log
        config_home = make_config_home(getattr(args, "mods", ""), os.environ.get("ARENA_AI_LOG"))
        env = client_env(config_home, args.client)

    # FairAI writes what its main hero spent the days on (and the like) next to the logs; see below
    env["FAI_STATS"] = str(log_dir / "fairai")

    port = args.base_port + match_id % 1000
    if battle_mode:
        # FairAI drives the (trivial) adventure part and hands each side's battle to the requested combat AI
        ai_args = ["--ai", "FairAI", "--ai", "FairAI", "--arena-battle-ai", ai_red, "--arena-battle-ai", ai_blue]
    else:
        # "Adventure+Combat" labels, e.g. "Nullkiller2+FairAI", select both AIs of a seat
        if seats is None:
            seats = {"red": ai_red, "blue": ai_blue}
            mine = ["blue"] if match_id % 2 else ["red"]
        last = max(COLORS.index(c) for c in seats)
        labels = [seats.get(COLORS[i]) or ai_blue or ai_red for i in range(last + 1)]
        ai_args = []
        for label in labels:
            ai_args += ["--ai", label.partition("+")[0]]
        if any("+" in label for label in labels):
            for label in labels:
                ai_args += ["--arena-battle-ai", label.partition("+")[2] or "BattleAI"]
    cmd = [args.client, "--headless", "--testmap", map_name, *ai_args,
           "--arena-result", str(result_file),
           "--arena-max-days", str(getattr(args, "max_days", 120)),
           "--arena-seed", str(seed),
           "--arena-difficulty", str(getattr(args, "difficulty", 4)),
           "--arena-stall-seconds", str(args.stall_seconds),
           "--serverport", str(port),
           "--logLocation", str(log_dir)]
    if getattr(args, "allow_cheats", False):
        cmd.append("--arena-allow-cheats")
    # FairAI parameters per seat: jobs come in pairs, odd match ids have the AI under test as blue
    ai_params = getattr(args, "ai_params", "") or ""
    opp_params = getattr(args, "opponent_params", "") or ""
    if not battle_mode and (ai_params or opp_params):
        for i in range(last + 1):
            cmd += ["--arena-fairai-params", (ai_params if COLORS[i] in mine else opp_params) or "{}"]
    if battle_mode and (ai_params or opp_params):
        # battles: red's combat AI is the first label; with seats given, `mine` says who is under test
        for colour in ("red", "blue"):
            under_test = colour in mine if mine is not None else colour == "red"
            cmd += ["--arena-fairai-params", (ai_params if under_test else opp_params) or "{}"]

    started = time.time()
    record = {"match": match_id, "map": map_name, "red": ai_red, "blue": ai_blue, "seed": seed}
    if not battle_mode:
        record["seats"] = seats
        record["mine"] = mine
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=args.timeout)
        record["returncode"] = proc.returncode
        if result_file.exists():
            record["result"] = json.loads(result_file.read_text())
            # statistics of FairAI's own play, by colour and four-week phase (kept with the result: the logs go)
            for stats in sorted(log_dir.glob("fairai-*.json")):
                try:
                    record.setdefault("fairai", {})[stats.stem[len("fairai-"):]] = json.loads(stats.read_text())
                except (OSError, ValueError):
                    pass
        else:
            record["error"] = "no result file"
            (log_dir / "stdout.txt").write_text(proc.stdout[-200000:])
            (log_dir / "stderr.txt").write_text(proc.stderr[-200000:])
    except subprocess.TimeoutExpired as e:
        record["error"] = f"wall-clock timeout after {args.timeout}s"
        (log_dir / "stdout.txt").write_text((e.stdout or b"").decode(errors="replace")[-200000:] if isinstance(e.stdout, bytes) else (e.stdout or "")[-200000:])
    finally:
        if config_home:
            shutil.rmtree(config_home, ignore_errors=True)
    record["wallSec"] = round(time.time() - started, 1)
    if not battle_mode:
        record["score"] = game_score(record)
    # what the engine turned down in this game (an order a unit could not carry out costs its turn): kept
    # with the result, because the log itself goes
    refused = refused_orders(log_dir)
    if refused:
        record["refused"] = refused
    # --keep-lost-logs: the log of every game the AI under test did not win, packed next to the results -
    # lost_NNNN.tar.xz for a lost or failed game, open_NNNN.tar.xz for one that ran into the day limit
    # (or stalled). Small enough for the worker to send home (0.3 to 0.5 MB a game).
    result = record.get("result") or {}
    won = "error" not in record and result.get("outcome") == "victory" and record.get("score") == 1.0
    lost = record.get("score") == 0 or "error" in record
    if getattr(args, "keep_lost_logs", False) and not won and log_dir.exists():
        try:
            import tarfile
            with tarfile.open(out_dir / f"{'lost' if lost else 'open'}_{match_id:04d}.tar.xz", "w:xz") as archive:
                archive.add(log_dir, arcname=f"match_{match_id:04d}")
        except OSError as e:
            print(f"match {match_id}: could not pack the log: {e}", file=sys.stderr)
    if not args.keep_logs and "error" not in record:
        shutil.rmtree(log_dir, ignore_errors=True)
    return record


REFUSED_MARKS = ("cannot move from", "ended early at", "has no way to hex")


def refused_orders(log_dir: Path, samples: int = 8):
    """Orders the engine turned down (server: "... cannot move from A to B ...") and orders FairAI's own
    last check replaced ("... has no way to hex ..."), counted from a game's log; None if there were none."""
    log = log_dir / "VCMI_Client_log.txt"
    if not log.exists():
        return None
    count, seen = 0, []
    try:
        with open(log, encoding="utf-8", errors="replace") as lines:
            for line in lines:
                if not any(mark in line for mark in REFUSED_MARKS):
                    continue
                # (the client repeats the server's complaint as a system message: count the server's line only)
                if "System message" in line:
                    continue
                count += 1
                if len(seen) < samples:
                    seen.append(line.split(" - ", 1)[-1].strip()[:200])
    except OSError:
        return None
    return {"count": count, "samples": seen} if count else None


def winner_ai(record: dict):
    res = record.get("result")
    if not res or res.get("outcome") != "victory" or not res.get("winner"):
        return None
    seats = record.get("seats") or {"red": record["red"], "blue": record["blue"]}
    return seats.get(res["winner"])


def material(record: dict, color: str) -> float:
    """Adjudication score for timeouts (not a substitute for real wins)."""
    p = record["result"]["players"][color]
    return p["armyStrength"] + 20000 * p["towns"] + 10 * p["income"] + 5000 * p["mines"]


def wilson(wins: float, n: int, z: float = 1.96):
    if n == 0:
        return (0.0, 1.0)
    p = wins / n
    denom = 1 + z * z / n
    centre = p + z * z / (2 * n)
    margin = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return ((centre - margin) / denom, (centre + margin) / denom)


def summarize(records, ai: str, opponent: str) -> str:
    wins = losses = timeouts = errors = 0
    adj_wins = 0
    cheat_flags = 0
    stalled_by = {}
    days = []
    for r in records:
        if "error" in r:
            errors += 1
            continue
        res = r["result"]
        for color, p in res.get("players", {}).items():
            if p.get("cheated"):
                cheat_flags += 1
        if res.get("outcome") == "stalled":
            who = res.get("lastTurnOfAI", "?")
            stalled_by[who] = stalled_by.get(who, 0) + 1
            continue
        w = winner_ai(r)
        # by seat where we know it: in a duel of two FairAI versions both labels are the same
        if r.get("mine") and res.get("outcome") == "victory" and res.get("winner"):
            w = ai if res["winner"] in r["mine"] else opponent
        if w == ai:
            wins += 1
            days.append(res["day"])
        elif w == opponent:
            losses += 1
            days.append(res["day"])
        else:
            timeouts += 1
            seats = r.get("seats") or {"red": r["red"], "blue": r["blue"]}
            mine = r.get("mine") or [c for c, label in seats.items() if label == ai]
            others = [c for c in res.get("players", {}) if c not in mine]
            mine = [c for c in mine if c in res.get("players", {})]
            if mine and others and sum(material(r, c) for c in mine) / len(mine) > sum(material(r, c) for c in others) / len(others):
                adj_wins += 1
    decided = wins + losses
    lo, hi = wilson(wins, decided)
    stalled_txt = ", ".join(f"{k}: {v}" for k, v in sorted(stalled_by.items())) or "none"
    lines = [
        f"# Arena: {ai} vs {opponent}",
        "",
        f"- matches: {len(records)} (runner errors: {errors})",
        f"- decided: {decided} -> {ai} won {wins}, lost {losses}"
        + (f"  (win rate {wins / decided:.0%}, 95% CI {lo:.0%}-{hi:.0%})" if decided else ""),
        f"- day limit reached: {timeouts} (material lead for {ai} in {adj_wins})",
        f"- stalled games (AI stopped responding), by AI on turn: {stalled_txt}",
        f"- average length of decided games: {sum(days) / len(days):.0f} days" if days else "- no decided games",
        f"- cheat flags raised: {cheat_flags}",
    ]
    return "\n".join(lines)


def cmd_play(args) -> int:
    maps = list_arena_maps(args.map_filter, getattr(args, "pool", ""))
    if args.max_maps:
        maps = maps[: args.max_maps]
    if not maps:
        print("no arena maps found - run 'genmaps' first", file=sys.stderr)
        return 1

    out_dir = Path(args.out).resolve()  # the client changes its working directory
    out_dir.mkdir(parents=True, exist_ok=True)

    jobs = []
    match_id = 0
    rng = random.Random(args.seed)
    for map_name in maps:
        seed = rng.randint(1, 2**31 - 1)
        groups = map_seat_groups(map_name)  # teams (or single players) take turns between the two AIs
        for swap in (0, 1):
            seats = {}
            mine = []
            for index, group in enumerate(groups):
                ours = (index + swap) % 2 == 0
                for colour in group:
                    seats[colour] = args.ai if ours else args.opponent
                    if ours:
                        mine.append(colour)
            jobs.append((match_id, map_name, seats, mine, seed))
            match_id += 1

    if getattr(args, "variants", ""):
        return play_variants(args, jobs, len(maps), out_dir)

    print(f"playing {len(jobs)} matches on {len(maps)} maps with {args.jobs} parallel job(s)")
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool, open(out_dir / "results.jsonl", "a") as sink:
        futures = [pool.submit(play_match, args, *job, out_dir) for job in jobs]
        for fut in as_completed(futures):
            rec = fut.result()
            records.append(rec)
            sink.write(json.dumps(rec) + "\n")
            sink.flush()
            res = rec.get("result", {})
            score = rec.get("score")
            print(f"match {rec['match']:4d} {args.ai} as {','.join(rec.get('mine') or [])} of {len(rec.get('seats') or {})}: "
                  f"{res.get('outcome', rec.get('error'))} winner={res.get('winner', '-')} day={res.get('day', '-')} "
                  f"score={'-' if score is None else format(score, '.2f')} [{rec['wallSec']}s]", flush=True)

    summary = summarize(sorted(records, key=lambda r: r["match"]), args.ai, args.opponent)
    (out_dir / "summary.md").write_text(summary + "\n")
    print()
    print(summary)
    return 0



def play_variants(args, jobs, map_count: int, root: Path) -> int:
    """Several parameter sets of the AI under test on the same games, all through ONE pool of
    parallel games: no configuration waits for the last long game of the one before it (played one
    after the other, a third of the machine's time went into such waits). The games are dealt out
    map by map across all variants, so every variant advances at the same pace and a test can be
    read while it runs. Each variant gets its own folder <out>/<name>-s<seed>, as ab_test.py names them."""
    variants = json.loads(args.variants)
    block = max(30, len(jobs) + 5)  # ports: one block per variant
    tasks = []
    folders = {}
    for index, (name, params) in enumerate(variants.items()):
        folder = root / f"{name}-s{args.seed}"
        folder.mkdir(parents=True, exist_ok=True)
        folders[name] = folder
        own = argparse.Namespace(**vars(args))
        own.ai_params = json.dumps(params) if params else ""
        own.base_port = args.base_port + index * block
        for job in jobs:
            tasks.append((job[0], index, name, own, job, folder))
    tasks.sort(key=lambda task: (task[0], task[1]))
    print(f"playing {len(tasks)} matches ({len(variants)} variants x {len(jobs)} on {map_count} maps) with {args.jobs} parallel job(s)", flush=True)
    records = collections.defaultdict(list)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(play_match, own, *job, folder): name for _, _, name, own, job, folder in tasks}
        for fut in as_completed(futures):
            name = futures[fut]
            rec = fut.result()
            records[name].append(rec)
            with open(folders[name] / "results.jsonl", "a") as sink:
                sink.write(json.dumps(rec) + "\n")
            res = rec.get("result", {})
            score = rec.get("score")
            print(f"{name}: match {rec['match']:4d} {args.ai} as {','.join(rec.get('mine') or [])} of {len(rec.get('seats') or {})}: "
                  f"{res.get('outcome', rec.get('error'))} winner={res.get('winner', '-')} day={res.get('day', '-')} "
                  f"score={'-' if score is None else format(score, '.2f')} [{rec['wallSec']}s]", flush=True)
    for name, folder in folders.items():
        (folder / "summary.md").write_text(summarize(sorted(records[name], key=lambda r: r["match"]), args.ai, args.opponent) + "\n")
    return 0


# ----------------------------------------------------------------------------- battles

BATTLE_MAP_SUBDIR = "arenabattles"


def cmd_genbattles(args) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import battle_scenarios

    library = Path(args.library).resolve()
    if not library.exists():
        subprocess.run([args.client, "--headless", "--arena-dump-library", str(library)], capture_output=True, timeout=300)
    scenarios = battle_scenarios.generate(str(library), args.count, args.seed,
                                          kinds=tuple(args.kinds.split(",")), stages=tuple(args.stages.split(",")),
                                          magic_ratio=args.magic_ratio)
    maps_dir = vcmi_data_dir() / "Maps" / BATTLE_MAP_SUBDIR
    maps_dir.mkdir(parents=True, exist_ok=True)
    scen_dir = Path(args.scenario_dir).resolve()
    scen_dir.mkdir(parents=True, exist_ok=True)
    ok = 0
    for sc in scenarios:
        name = f"{args.prefix}_{sc['name']}"
        scen_file = scen_dir / f"{name}.json"
        scen_file.write_text(json.dumps(sc, indent=1))
        out = maps_dir / f"{name}.vmap"
        res = subprocess.run([args.client, "--headless", "--arena-genbattle", str(scen_file), "--arena-out", str(out)],
                             capture_output=True, text=True, timeout=300)
        if res.returncode == 0 and out.exists():
            ok += 1
        else:
            print(f"FAILED {name}: {res.stderr.strip()[-500:]}")
    print(f"{ok}/{len(scenarios)} battle maps in {maps_dir} (scenarios in {scen_dir})")
    return 0 if ok == len(scenarios) else 1


def list_battle_maps(pattern: str):
    maps_dir = vcmi_data_dir() / "Maps" / BATTLE_MAP_SUBDIR
    maps = sorted(p for p in maps_dir.glob("*.vmap") if pattern in p.name)
    return [f"MAPS/{BATTLE_MAP_SUBDIR.upper()}/{p.stem.upper()}" for p in maps]


def battle_score(record: dict, ai: str):
    """(won, score) from the perspective of `ai`; score = own remaining fraction - enemy remaining fraction."""
    battle = record["result"]["battle"]
    mine = [s for s in battle["sides"] if s["battleAI"] == ai]
    theirs = [s for s in battle["sides"] if s["battleAI"] != ai]
    if record["red"] == record["blue"]:
        # same AI on both sides: report from red's point of view
        mine = [s for s in battle["sides"] if s["player"] == "red"]
        theirs = [s for s in battle["sides"] if s["player"] != "red"]
    me, them = mine[0], theirs[0]
    won = record["result"].get("winner") == me["player"]
    return won, me["remainingFraction"] - them["remainingFraction"]


def summarize_battles(records, ai: str, opponent: str) -> str:
    groups = {}
    errors = 0
    wins = n = 0
    scores = []
    by_map = {}
    for r in records:
        if "error" in r or r.get("result", {}).get("outcome") != "battle":
            errors += 1
            continue
        won, score = battle_score(r, ai)
        n += 1
        wins += won
        scores.append(score)
        by_map.setdefault(r["map"], []).append(score)
        key = r["map"].split("_", 2)[-1].rsplit("_", 0)[0]
        parts = r["map"].lower().split("_")
        tag = "/".join(p for p in parts if p in ("mirror", "cross", "early", "mid", "late", "magic", "nomagic"))
        g = groups.setdefault(tag, [0, 0, []])
        g[0] += won
        g[1] += 1
        g[2].append(score)
    lo, hi = wilson(wins, n)
    mean = sum(scores) / len(scores) if scores else 0.0
    sd = (sum((x - mean) ** 2 for x in scores) / max(1, len(scores) - 1)) ** 0.5 if scores else 0.0
    se = sd / max(1, len(scores)) ** 0.5
    # paired per scenario (both sides played): average of the two scores removes side advantage
    paired = [sum(v) / len(v) for v in by_map.values() if len(v) == 2]
    paired_better = sum(1 for v in paired if v > 0.02)
    paired_worse = sum(1 for v in paired if v < -0.02)
    lines = [
        f"# Kampf-Arena: {ai} gegen {opponent}",
        "",
        f"- Kämpfe: {n} (Fehler: {errors})",
        f"- Siege {ai}: {wins}/{n} = {wins / n:.0%} (95% CI {lo:.0%}-{hi:.0%})" if n else "- keine Kämpfe",
        f"- mittlerer Score {ai}: {mean:+.3f} ± {1.96 * se:.3f} (Score = eigener Rest-Anteil - gegnerischer Rest-Anteil)",
        f"- Szenarien mit beiden Seiten: {len(paired)}; {ai} besser in {paired_better}, schlechter in {paired_worse}",
        "",
        "| Gruppe | Kämpfe | Siege | mittlerer Score |",
        "|---|---|---|---|",
    ]
    for tag in sorted(groups):
        w, cnt, sc = groups[tag]
        lines.append(f"| {tag} | {cnt} | {w / cnt:.0%} | {sum(sc) / len(sc):+.3f} |")
    return "\n".join(lines)


def cmd_battles(args) -> int:
    maps = list_battle_maps(args.map_filter)
    if args.max_maps:
        maps = maps[: args.max_maps]
    if not maps:
        print("no battle maps found - run 'genbattles' first", file=sys.stderr)
        return 1
    out_dir = Path(args.out).resolve()  # the client changes its working directory
    out_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    jobs = []
    match_id = 0
    for map_name in maps:
        seed = rng.randint(1, 2**31 - 1)
        groups = map_seat_groups(map_name)  # teams (or single players) take turns between the two AIs
        for swap in (0, 1):
            seats = {}
            mine = []
            for index, group in enumerate(groups):
                ours = (index + swap) % 2 == 0
                for colour in group:
                    seats[colour] = args.ai if ours else args.opponent
                    if ours:
                        mine.append(colour)
            jobs.append((match_id, map_name, seats, mine, seed))
            match_id += 1
    print(f"playing {len(jobs)} battles on {len(maps)} scenarios with {args.jobs} parallel job(s)")
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool, open(out_dir / "results.jsonl", "a") as sink:
        futures = [pool.submit(play_match, args, *job, out_dir, True) for job in jobs]
        for fut in as_completed(futures):
            rec = fut.result()
            records.append(rec)
            sink.write(json.dumps(rec) + "\n")
            sink.flush()
            if args.verbose:
                res = rec.get("result", {})
                b = res.get("battle", {})
                sides = " ".join(f"{s['battleAI']}:{s['remainingFraction']:.2f}" for s in b.get("sides", []))
                print(f"battle {rec['match']:4d} {rec['map'].split('/')[-1]:<40} winner={res.get('winner', rec.get('error'))} {sides} [{rec['wallSec']}s]", flush=True)
    summary = summarize_battles(sorted(records, key=lambda r: r["match"]), args.ai, args.opponent)
    (out_dir / "summary.md").write_text(summary + "\n")
    print()
    print(summary)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("genmaps", help="generate random arena maps")
    g.add_argument("--client", required=True)
    g.add_argument("--count", type=int, default=10)
    g.add_argument("--template", default="2SM4d")
    g.add_argument("--size", default="s", choices=["s", "m", "l", "xl", "h", "xh", "g"])
    g.add_argument("--players", type=int, default=2)
    g.add_argument("--teams", default="", help="team per player, e.g. 0,0,1,1 (default: free for all)")
    g.add_argument("--towns", default="", help="starting town per player, e.g. castle,hota.cove:cove (default: random); several lists separated by ';' are used in turn, one per map")
    g.add_argument("--tag", default="", help="extra text for the map file names")
    g.add_argument("--pool", default="", help="map pool = sub-folder of Maps/ (default: arena)")
    g.add_argument("--mods", default="", help="extra mods to enable, comma-separated (e.g. hota)")
    g.add_argument("--levels", type=int, default=1)
    g.add_argument("--water", default="none", choices=["none", "normal", "islands", "random"])
    g.add_argument("--monsters", default="normal", choices=["weak", "normal", "strong", "random"])
    g.add_argument("--seed", type=int, default=1000)
    g.add_argument("--force", action="store_true")

    p = sub.add_parser("play", help="play a tournament between two AIs")
    p.add_argument("--client", required=True)
    p.add_argument("--ai", required=True, help="AI under test, e.g. FairAI or Nullkiller2+FairAI (adventure+combat)")
    p.add_argument("--opponent", required=True, help="reference AI, e.g. Nullkiller2 or Nullkiller2+BattleAI")
    p.add_argument("--map-filter", default="", help="only maps whose file name contains this text")
    p.add_argument("--mods", default="", help="extra mods to enable, comma-separated (e.g. hota)")
    p.add_argument("--pool", default="", help="map pool = sub-folder of Maps/ (default: arena)")
    p.add_argument("--max-maps", type=int, default=0)
    p.add_argument("--max-days", type=int, default=120)
    p.add_argument("--difficulty", type=int, default=4, help="0-4; both AIs get the same 'ai' start resources")
    p.add_argument("--allow-cheats", action="store_true", help="let the opponent use cheat codes (map reveal)")
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--timeout", type=int, default=3600, help="wall-clock limit per match in seconds")
    p.add_argument("--stall-seconds", type=int, default=60, help="end a match as 'stalled' after this long without progress")
    p.add_argument("--base-port", type=int, default=3100)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--keep-logs", action="store_true")
    p.add_argument("--keep-lost-logs", action="store_true", help="pack the log of every game that was not won: lost_NNNN.tar.xz (lost, failed), open_NNNN.tar.xz (day limit, stalled)")
    p.add_argument("--ai-params", default="", help="FairAI parameter overrides (JSON) for the AI under test")
    p.add_argument("--opponent-params", default="", help="FairAI parameter overrides (JSON) for the opponent")
    p.add_argument("--variants", default="", help="JSON object name -> FairAI parameter overrides: play all of them through one pool of games; --out is then the folder that holds one sub-folder per variant")
    p.add_argument("--out", required=True)

    gb = sub.add_parser("genbattles", help="generate battle scenarios and battle-only maps")
    gb.add_argument("--client", required=True)
    gb.add_argument("--library", default="arena-library.json", help="library dump (created if missing)")
    gb.add_argument("--count", type=int, default=24)
    gb.add_argument("--seed", type=int, default=7)
    gb.add_argument("--kinds", default="mirror,cross")
    gb.add_argument("--stages", default="early,mid,late")
    gb.add_argument("--magic-ratio", type=float, default=0.5)
    gb.add_argument("--prefix", default="set1")
    gb.add_argument("--scenario-dir", default="arena-scenarios")

    b = sub.add_parser("battles", help="battle tournament between two combat AIs")
    b.add_argument("--client", required=True)
    b.add_argument("--ai", required=True, help="combat AI under test, e.g. FairAI")
    b.add_argument("--opponent", required=True, help="reference combat AI, e.g. BattleAI or MMAI")
    b.add_argument("--map-filter", default="")
    b.add_argument("--max-maps", type=int, default=0)
    b.add_argument("--jobs", type=int, default=2)
    b.add_argument("--timeout", type=int, default=600)
    b.add_argument("--stall-seconds", type=int, default=60)
    b.add_argument("--base-port", type=int, default=3300)
    b.add_argument("--seed", type=int, default=42)
    b.add_argument("--keep-logs", action="store_true")
    b.add_argument("--verbose", action="store_true")
    b.add_argument("--ai-params", default="", help="FairAI parameter overrides (JSON) for the combat AI under test")
    b.add_argument("--out", required=True)

    args = ap.parse_args()
    commands = {"genmaps": cmd_genmaps, "play": cmd_play, "genbattles": cmd_genbattles, "battles": cmd_battles}
    return commands[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
