#!/usr/bin/env python3
"""
Static fairness check for FairAI.

FairAI may only use information a human player in the same seat could see. This script
scans the FairAI sources for API uses that would bypass that rule. It complements the
runtime checks of the arena (cheat codes forbidden, 'cheated' flag reported per player).

Rules
  1. Never, anywhere: cheat codes / chat commands, direct access to the full game state
     or the raw map, unchecked object/tile access, raw creature slot containers.
  2. Only inside Perception/: the info callbacks that describe foreign armies
     (getHeroInfo, getTownInfo, ArmyDescriptor). Everything else must use Perception.

Exit code 0 = clean, 1 = violations found.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "AI" / "FairAI"

FORBIDDEN_EVERYWHERE = {
    r"\bsendMessage\s*\(": "chat/cheat commands are not allowed",
    r"vcmieagles|vcmiwhoami|nwcrevelation|nwcwhatisthematrix|vcmiolorin|vcmisilmaril": "cheat code literal",
    r"\bgameState\s*\(\s*\)": "direct access to the full game state",
    r"\bgetMap\s*\(\s*\)": "direct access to the raw map",
    r"\bgetObjInstance\s*\(": "unchecked object access (bypasses visibility)",
    r"\bgetTileUnchecked\s*\(": "unchecked tile access (bypasses visibility)",
    r"\bisVisibleFor\s*\(": "visibility queries for other players",
    r"\bgetObj\s*\([^;]*,\s*false\s*\)": "object access with visibility errors silenced",
    r"\.stacks\b|->stacks\b": "raw creature slot container",
    r"\bfogOfWarMap\b": "raw fog-of-war data (use Perception)",
}

ONLY_IN_PERCEPTION = {
    r"\bgetHeroInfo\s*\(": "foreign hero info must go through Perception",
    r"\bgetTownInfo\s*\(": "foreign town info must go through Perception",
    r"\bArmyDescriptor\s*\(": "army descriptors must go through Perception",
}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main() -> int:
    if not ROOT.exists():
        print(f"FairAI sources not found at {ROOT}")
        return 1

    violations = []
    files = sorted(p for p in ROOT.rglob("*") if p.suffix in (".cpp", ".h"))
    for path in files:
        rel = path.relative_to(ROOT)
        in_perception = rel.parts[0] == "Perception"
        code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, line in enumerate(code.splitlines(), 1):
            for pattern, why in FORBIDDEN_EVERYWHERE.items():
                if re.search(pattern, line):
                    violations.append(f"{rel}:{lineno}: {why}: {line.strip()}")
            if not in_perception:
                for pattern, why in ONLY_IN_PERCEPTION.items():
                    if re.search(pattern, line):
                        violations.append(f"{rel}:{lineno}: {why}: {line.strip()}")

    print(f"checked {len(files)} FairAI source files")
    if violations:
        print(f"{len(violations)} fairness violation(s):")
        for v in violations:
            print("  " + v)
        return 1
    print("no fairness violations found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
