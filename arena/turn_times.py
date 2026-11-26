#!/usr/bin/env python3
"""Seconds FairAI takes for its turns, and its errors, from packed game logs.

Usage: turn_times.py RESULTS_DIR
A turn of a colour runs from its "day N economy" line to its last line on the turn thread
([FairAI::makingTurn]) before its next turn. Prints per game and colour: turns, median, 90th
percentile and longest turn in seconds, turns over 30 s, "turn time budget exhausted" warnings,
and FairAI error lines.
"""
import datetime
import re
import statistics
import sys
import tarfile
from pathlib import Path

STAMP = re.compile(r"^\[(\d{4}-\w{3}-\d\d \d\d:\d\d:\d\d\.\d+)\]")
ECON = re.compile(r"FairAI (\w+) day (\d+) economy")
TURN = re.compile(r"\[FairAI::makingTurn\].*FairAI (\w+)[ :]")


def ts(text):
    return datetime.datetime.strptime(text, "%Y-%b-%d %H:%M:%S.%f")


for archive in sorted(Path(sys.argv[1]).glob("*.tar.xz")):
    with tarfile.open(archive) as tar:
        member = next((m for m in tar.getmembers() if m.name.endswith("VCMI_Client_log.txt")), None)
        if not member:
            continue
        turns = {}
        current = {}
        budget = {}
        errors = {}
        for raw in tar.extractfile(member):
            line = raw.decode("utf-8", "replace")
            if "FairAI" not in line:
                continue
            stamp = STAMP.match(line)
            if not stamp:
                continue
            when = ts(stamp.group(1))
            econ = ECON.search(line)
            if econ:
                colour = econ.group(1)
                current[colour] = [when, when]
                turns.setdefault(colour, []).append(current[colour])
                continue
            turn = TURN.search(line)
            if turn and turn.group(1) in current:
                current[turn.group(1)][1] = when
            if "turn time budget exhausted" in line:
                colour = re.search(r"FairAI (\w+)", line)
                budget[colour.group(1) if colour else "?"] = budget.get(colour.group(1) if colour else "?", 0) + 1
            if " ERROR " in line or "] ERROR" in line:
                errors[line.split(" - ", 1)[-1][:120].strip()] = errors.get(line.split(" - ", 1)[-1][:120].strip(), 0) + 1
        for colour, spans in sorted(turns.items()):
            secs = sorted((b - a).total_seconds() for a, b in spans)
            if not secs:
                continue
            p90 = secs[int(0.9 * (len(secs) - 1))]
            print(f"{archive.name} {colour}: {len(secs)} turns, median {statistics.median(secs):.1f} s, 90 % {p90:.1f} s, longest {secs[-1]:.1f} s, "
                  f"over 30 s: {sum(1 for s in secs if s > 30)}, budget exhausted: {budget.get(colour, 0)}")
        for text, count in sorted(errors.items(), key=lambda kv: -kv[1])[:6]:
            print(f"   error x{count}: {text}")
