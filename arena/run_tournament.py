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


# ----------------------------------------------------------------------------- maps

def cmd_genmaps(args) -> int:
    maps_dir = vcmi_data_dir() / "Maps" / ARENA_MAP_SUBDIR
    maps_dir.mkdir(parents=True, exist_ok=True)
    ok = 0
    for i in range(args.count):
        seed = args.seed + i
        name = f"{args.template}_{args.size}_l{args.levels}_{args.water}_{seed}.vmap".replace(" ", "")
        out = maps_dir / name
        if out.exists() and not args.force:
            print(f"exists: {out}")
            ok += 1
            continue
        cmd = [args.client, "--headless", "--arena-genmap", str(out), "--arena-template", args.template,
               "--arena-size", args.size, "--arena-levels", str(args.levels), "--arena-seed", str(seed),
               "--arena-water", args.water, "--arena-monsters", args.monsters]
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if res.returncode == 0 and out.exists():
            print(f"generated: {out}")
            ok += 1
        else:
            print(f"FAILED seed {seed}: rc={res.returncode}\n{res.stdout[-2000:]}\n{res.stderr[-2000:]}")
    print(f"{ok}/{args.count} maps ready in {maps_dir}")
    return 0 if ok == args.count else 1


def list_arena_maps(pattern: str):
    maps_dir = vcmi_data_dir() / "Maps" / ARENA_MAP_SUBDIR
    maps = sorted(p for p in maps_dir.glob("*.vmap") if pattern in p.name)
    return [f"MAPS/{ARENA_MAP_SUBDIR.upper()}/{p.stem.upper()}" for p in maps]


# ----------------------------------------------------------------------------- matches

def play_match(args, match_id: int, map_name: str, ai_red: str, ai_blue: str, seed: int, out_dir: Path,
               battle_mode: bool = False) -> dict:
    result_file = out_dir / f"match_{match_id:04d}.json"
    log_dir = out_dir / f"logs_{match_id:04d}"
    log_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    config_home = None
    if os.name != "nt":
        # isolate settings.json per match: parallel clients would otherwise race on it
        config_home = Path(tempfile.mkdtemp(prefix="vcmi-arena-cfg-"))
        env["XDG_CONFIG_HOME"] = str(config_home)
        env["XDG_CACHE_HOME"] = str(config_home / "cache")
        (config_home / "vcmi").mkdir(parents=True, exist_ok=True)
        settings = json.loads(json.dumps(ARENA_SETTINGS))
        ai_log = os.environ.get("ARENA_AI_LOG")  # e.g. "debug": detailed AI decisions in the match log
        if ai_log:
            settings["logging"]["loggers"].append({"domain": "ai", "level": ai_log})
        (config_home / "vcmi" / "settings.json").write_text(json.dumps(settings, indent=1))
        (config_home / "vcmi" / "modSettings.json").write_text(json.dumps(ARENA_MOD_SETTINGS, indent=1))

    port = args.base_port + match_id % 1000
    if battle_mode:
        # FairAI drives the (trivial) adventure part and hands each side's battle to the requested combat AI
        ai_args = ["--ai", "FairAI", "--ai", "FairAI", "--arena-battle-ai", ai_red, "--arena-battle-ai", ai_blue]
    else:
        # "Adventure+Combat" labels, e.g. "Nullkiller2+FairAI", select both AIs of a seat
        red_adv, _, red_combat = ai_red.partition("+")
        blue_adv, _, blue_combat = ai_blue.partition("+")
        ai_args = ["--ai", red_adv, "--ai", blue_adv]
        if red_combat or blue_combat:
            ai_args += ["--arena-battle-ai", red_combat or "BattleAI", "--arena-battle-ai", blue_combat or "BattleAI"]
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
        red_params, blue_params = (opp_params, ai_params) if match_id % 2 else (ai_params, opp_params)
        cmd += ["--arena-fairai-params", red_params or "{}", "--arena-fairai-params", blue_params or "{}"]

    started = time.time()
    record = {"match": match_id, "map": map_name, "red": ai_red, "blue": ai_blue, "seed": seed}
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=args.timeout)
        record["returncode"] = proc.returncode
        if result_file.exists():
            record["result"] = json.loads(result_file.read_text())
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
    if not args.keep_logs and "error" not in record:
        shutil.rmtree(log_dir, ignore_errors=True)
    return record


def winner_ai(record: dict):
    res = record.get("result")
    if not res or res.get("outcome") != "victory" or not res.get("winner"):
        return None
    return record["red"] if res["winner"] == "red" else record["blue"]


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
        if w == ai:
            wins += 1
            days.append(res["day"])
        elif w == opponent:
            losses += 1
            days.append(res["day"])
        else:
            timeouts += 1
            my_color = "red" if r["red"] == ai else "blue"
            other = "blue" if my_color == "red" else "red"
            if material(r, my_color) > material(r, other):
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
    maps = list_arena_maps(args.map_filter)
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
        for swap in (False, True):
            red, blue = (args.opponent, args.ai) if swap else (args.ai, args.opponent)
            jobs.append((match_id, map_name, red, blue, seed))
            match_id += 1

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
            print(f"match {rec['match']:4d} {rec['red']:>12} (red) vs {rec['blue']:<12} (blue): "
                  f"{res.get('outcome', rec.get('error'))} winner={res.get('winner', '-')} day={res.get('day', '-')} "
                  f"[{rec['wallSec']}s]", flush=True)

    summary = summarize(sorted(records, key=lambda r: r["match"]), args.ai, args.opponent)
    (out_dir / "summary.md").write_text(summary + "\n")
    print()
    print(summary)
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
        for swap in (False, True):
            red, blue = (args.opponent, args.ai) if swap else (args.ai, args.opponent)
            jobs.append((match_id, map_name, red, blue, seed))
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
    g.add_argument("--size", default="s", choices=["s", "m", "l", "xl"])
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
    p.add_argument("--ai-params", default="", help="FairAI parameter overrides (JSON) for the AI under test")
    p.add_argument("--opponent-params", default="", help="FairAI parameter overrides (JSON) for the opponent")
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
    b.add_argument("--out", required=True)

    args = ap.parse_args()
    commands = {"genmaps": cmd_genmaps, "play": cmd_play, "genbattles": cmd_genbattles, "battles": cmd_battles}
    return commands[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
