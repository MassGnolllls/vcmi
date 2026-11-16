#!/usr/bin/env python3
"""Run one headless game with any number of AI players (one --ai per seat) and report how it went.

Meant for robustness and speed checks on big or unusual maps (XL, 8 players, water, underground,
hand-made maps) rather than for strength comparisons:

  probe_game.py --client build/bin/vcmiclient --map MAPS/ARENA/NAME --ais FairAI,Nullkiller2,... \
                --days 14 --out /tmp/probe1

Prints wall time, per-player results and FairAI's turn times (from its own log lines).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_tournament as rt  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--client", required=True)
    p.add_argument("--map", required=True, help="e.g. MAPS/ARENA/2SM4D_S_L1_NONE_1001")
    p.add_argument("--ais", required=True, help="comma-separated, one per player in colour order")
    p.add_argument("--days", type=int, default=14)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--difficulty", type=int, default=4)
    p.add_argument("--port", type=int, default=5600)
    p.add_argument("--timeout", type=int, default=3600)
    p.add_argument("--stall-seconds", type=int, default=600)
    p.add_argument("--out", required=True)
    p.add_argument("--mods", default="", help="extra mods to enable, comma-separated")
    args = p.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    result_file = out / "result.json"
    config_home = Path(tempfile.mkdtemp(prefix="vcmi-probe-cfg-"))
    env = rt.client_env(config_home, args.client)
    (config_home / "vcmi").mkdir(parents=True, exist_ok=True)
    settings = json.loads(json.dumps(rt.ARENA_SETTINGS))
    ai_log = os.environ.get("ARENA_AI_LOG")
    if ai_log:
        settings["logging"]["loggers"].append({"domain": "ai", "level": ai_log})
    mod_settings = json.loads(json.dumps(rt.ARENA_MOD_SETTINGS))
    for mod in filter(None, args.mods.split(",")):
        mod_settings["presets"]["default"]["mods"].append(mod)
    (config_home / "vcmi" / "settings.json").write_text(json.dumps(settings, indent=1))
    (config_home / "vcmi" / "modSettings.json").write_text(json.dumps(mod_settings, indent=1))

    cmd = [args.client, "--headless", "--testmap", args.map]
    for ai in args.ais.split(","):
        cmd += ["--ai", ai]
    cmd += ["--arena-result", str(result_file), "--arena-max-days", str(args.days), "--arena-seed", str(args.seed),
            "--arena-difficulty", str(args.difficulty), "--arena-stall-seconds", str(args.stall_seconds),
            "--serverport", str(args.port), "--logLocation", str(out)]
    started = time.time()
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=args.timeout)
        rc = proc.returncode
        (out / "stdout.txt").write_text(proc.stdout[-200000:])
        (out / "stderr.txt").write_text(proc.stderr[-200000:])
    except subprocess.TimeoutExpired:
        rc = "timeout"
    finally:
        shutil.rmtree(config_home, ignore_errors=True)
    wall = time.time() - started
    print(f"return code {rc}, wall time {wall:.0f} s")

    if result_file.exists():
        result = json.loads(result_file.read_text())
        print(f"outcome {result.get('outcome')} on day {result.get('day')}, winner '{result.get('winner', '')}'")
        for color, pl in result.get("players", {}).items():
            print(f"  {color:8s} {pl.get('ai', '?'):12s} status {pl.get('status', '?'):8s} army {pl.get('armyStrength', 0):7d} "
                  f"towns {pl.get('towns', 0)} heroes {pl.get('heroes', 0)} mines {pl.get('mines', 0)} explored {pl.get('mapExploredRatio', 0):.2f}")
    else:
        print("no result file")

    # FairAI turn times: from its "day N: known objects" line to its "day N end" line
    log = out / "VCMI_Client_log.txt"
    if log.exists():
        stamp = re.compile(r"^\[(\d+)-(\w+)-(\d+) (\d+):(\d+):(\d+)\.(\d+)\]")
        begin = {}
        turns = []
        errors = 0
        for line in log.open(errors="replace"):
            if " ERROR " in line or " WARN " in line and "FairAI" in line:
                errors += 1
            m = stamp.match(line)
            if not m:
                continue
            t = int(m.group(4)) * 3600 + int(m.group(5)) * 60 + int(m.group(6)) + int(m.group(7)) / 1e6
            s = re.search(r"FairAI (\w+) day (\d+): known objects", line)
            if s:
                begin[(s.group(1), s.group(2))] = t
            e = re.search(r"FairAI (\w+) day (\d+) end", line)
            if e and (e.group(1), e.group(2)) in begin:
                turns.append((t - begin[(e.group(1), e.group(2))], e.group(1), int(e.group(2))))
        if turns:
            turns.sort()
            total = sum(x[0] for x in turns)
            print(f"FairAI turns: {len(turns)}, mean {total / len(turns):.2f} s, median {turns[len(turns) // 2][0]:.2f} s, "
                  f"max {turns[-1][0]:.2f} s ({turns[-1][1]} day {turns[-1][2]})")
        print(f"error/warning lines in log: {errors}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
