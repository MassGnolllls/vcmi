#!/usr/bin/env python3
"""Policy check: how often does the simulator's enemy do what the real enemy does?

FairAI's combat look-ahead plays the enemy with a default policy (CombatSim: charge or stalk).
A look-ahead is only as good as that guess. With FAI_POLICYCHECK=1 the combat AI logs, before
every action of an enemy unit, what each policy would have done in its place:

  FairAI policycheck|creature|count|round|waited|kind|target|hex|chargeKind|chargeSame|chargeApart|stalkKind|stalkSame|stalkApart

  policy_check.py DIR [DIR ...]     (folders with logs_*/VCMI_Client_log.txt, e.g. of guard_bench.py play --keep-logs)

Agreement: same kind of action; for attacks and shots the same target; for moves how many hexes
the two destinations lie apart.
"""
import collections
import sys
from pathlib import Path


def rows(folders):
    for folder in folders:
        for log in sorted(Path(folder).glob("**/VCMI_Client_log.txt")):
            for line in log.open(errors="replace"):
                at = line.find("FairAI policycheck|")
                if at < 0:
                    continue
                f = line[at:].strip().split("|")
                if len(f) < 14:  # (older logs: without the oracle)
                    continue
                row = {"creature": f[1], "count": int(f[2]), "round": int(f[3]), "waited": f[4] == "1", "kind": f[5], "target": f[6], "hex": int(f[7]),
                       "charge": (f[8], f[9] == "1", int(f[10])), "stalk": (f[11], f[12] == "1", int(f[13]))}
                if len(f) >= 18:  # the oracle: BattleAI's own evaluation on a mirror of the state, and its time in ms
                    row["oracle"] = (f[14], f[15] == "1", int(f[16]))
                    row["ms"] = float(f[17])
                if len(f) >= 19:  # the play-outs' shortcut says "it waits" without asking the oracle
                    row["shortcut"] = f[18] == "1"
                if len(f) >= 28:  # the native copy of BattleAI's rules (GuardModel), its time in microseconds, same answer as the oracle?
                    row["native"] = (f[19], f[20] == "1", int(f[21]))
                    row["micros"] = float(f[22])
                    row["agree"] = f[23] == "1"
                    row["answers"] = (int(f[24]), int(f[25]), int(f[26]), int(f[27]))
                yield row


def main():
    data = list(rows(sys.argv[1:]))
    if not data:
        print("no policycheck lines found")
        return 1
    print(f"{len(data)} enemy actions")
    models = ["charge", "stalk"] + (["oracle"] if all("oracle" in r for r in data) else []) + (["native"] if all("native" in r for r in data) else [])
    for model in models:
        print(f"\n## simulator's enemy: {model}")
        print("| real action | n | same kind | same kind and target | sim does instead |")
        print("|---|---|---|---|---|")
        for kind in ("wait", "defend", "move", "attack", "shoot"):
            sel = [r for r in data if r["kind"] == kind]
            if not sel:
                continue
            same = [r for r in sel if r[model][0] == kind]
            exact = [r for r in same if kind not in ("attack", "shoot") or r[model][1]]
            other = collections.Counter(r[model][0] for r in sel if r[model][0] != kind)
            extra = ""
            if kind == "move" and same:
                apart = [r[model][2] for r in same if r[model][2] >= 0]
                if apart:
                    extra = f"; destinations {sum(apart) / len(apart):.1f} hexes apart, {sum(1 for a in apart if a <= 1) / len(apart):.0%} within one"
            print(f"| {kind} | {len(sel)} | {len(same) / len(sel):.0%} | {len(exact) / len(sel):.0%} | "
                  f"{', '.join(f'{k} {v}' for k, v in other.most_common()) or '-'}{extra} |")
        total = sum(1 for r in data if r[model][0] == r["kind"] and (r["kind"] not in ("attack", "shoot") or r[model][1]))
        print(f"\nall actions: {total / len(data):.0%} as predicted")
    if "oracle" in models:
        ms = sorted(r["ms"] for r in data)
        print(f"\noracle: {sum(ms) / len(ms):.2f} ms per decision (median {ms[len(ms) // 2]:.2f}, 90 % under {ms[int(len(ms) * 0.9)]:.2f}, max {ms[-1]:.1f})")
        print("\n## oracle: what it misses")
        misses = collections.Counter()
        for r in data:
            if not (r["oracle"][0] == r["kind"] and (r["kind"] not in ("attack", "shoot") or r["oracle"][1])):
                misses[(r["creature"], r["kind"], r["oracle"][0])] += 1
        for (creature, real, told), n in misses.most_common(15):
            print(f"- {creature}: does {real}, oracle says {told} ({n})")
    if "native" in models:
        us = sorted(r["micros"] for r in data)
        agree = sum(1 for r in data if r["agree"])
        print(f"\n## native model against the oracle")
        print(f"same answer in {agree} of {len(data)} decisions ({agree / len(data):.1%}); "
              f"{sum(us) / len(us):.0f} microseconds per decision (median {us[len(us) // 2]:.0f}, 90 % under {us[int(len(us) * 0.9)]:.0f}, max {us[-1]:.0f})")
        print("| oracle says | n | native agrees | native says instead |")
        print("|---|---|---|---|")
        for kind in ("wait", "defend", "move", "attack", "shoot"):
            sel = [r for r in data if r["oracle"][0] == kind]
            if not sel:
                continue
            ok = sum(1 for r in sel if r["agree"])
            other = collections.Counter(r["native"][0] + (" (other target or hex)" if r["native"][0] == kind else "") for r in sel if not r["agree"])
            print(f"| {kind} | {len(sel)} | {ok / len(sel):.0%} | {', '.join(f'{k} {v}' for k, v in other.most_common()) or '-'} |")
        misses = collections.Counter()
        for r in data:
            if not r["agree"]:
                misses[(r["creature"], r["oracle"][0], r["native"][0])] += 1
        print("\nmost frequent disagreements:")
        for (creature, told, native), n in misses.most_common(12):
            print(f"- {creature}: oracle {told}, native {native} ({n})")
    if all("shortcut" in r for r in data):
        short = [r for r in data if r["shortcut"]]
        wrong = [r for r in short if r["kind"] != "wait"]
        waits = sum(1 for r in data if r["kind"] == "wait")
        print(f"\nshortcut (nothing to attack, not waited: it waits): taken in {len(short)} of {len(data)} actions, wrong in {len(wrong)}; "
              f"it covers {sum(1 for r in short if r['kind'] == 'wait')} of the {waits} real waits")
        for r in wrong[:8]:
            print(f"- {r['creature']}: does {r['kind']}")
    # where the better model fails most
    print("\n## stalk: misses by round and by whether the unit had waited")
    print("| round | waited | n | as predicted |")
    print("|---|---|---|---|")
    groups = collections.defaultdict(list)
    for r in data:
        ok = r["stalk"][0] == r["kind"] and (r["kind"] not in ("attack", "shoot") or r["stalk"][1])
        groups[(min(r["round"], 3), r["waited"])].append(ok)
    for key in sorted(groups):
        g = groups[key]
        print(f"| {key[0]}{'+' if key[0] == 3 else ''} | {'yes' if key[1] else 'no'} | {len(g)} | {sum(g) / len(g):.0%} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
