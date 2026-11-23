#!/usr/bin/env python3
"""
pursuit.py - exact answer to "can a fast flyer wear down one slow walker without ever being struck?"

The game between one flyer that suffers no retaliation (the kiter: vampire lords, ...) and one
walking stack (the chaser), on a battlefield with obstacles, solved by going through every position:
a state is (hex of the kiter, hex of the chaser) at the start of a round, the kiter to act. It may

  move  - fly to a hex (or stay); then the chaser walks. If the chaser can reach a hex next to the
          kiter, the kiter is struck: lost.
  wait  - the chaser walks first (struck if it reaches the kiter); then the kiter, acting last in
          the round, strikes it from a free hex next to it, or flies somewhere - and is the first to
          act in the next round as well.

The kiter wins a state if it can strike again and again for ever without being struck, whatever
the chaser does (so the result holds for any number of hit points). Computed as the greatest
fixpoint "strike once more, then be in a winning state again" over all states - no sampling.

  pursuit.py --obst 75,76,127 --kiter 86 --chaser 100 [--kspeed 9] [--cspeed 4]
"""
import argparse
import pickle
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from puppet import NB, FIELD, ONFIELD, dist, xy  # noqa: E402


class Pursuit:
    def __init__(self, obstacles, kspeed, cspeed):
        self.obst = frozenset(obstacles)
        self.kspeed, self.cspeed = kspeed, cspeed
        self.valid = [h for h in range(FIELD) if ONFIELD[h] and h not in self.obst]
        self.isvalid = [False] * FIELD
        for h in self.valid:
            self.isvalid[h] = True
        self.bit = [1 << h for h in range(FIELD)]
        self.adj = [0] * FIELD
        for h in self.valid:
            for n in NB[h]:
                if self.isvalid[n]:
                    self.adj[h] |= self.bit[n]
        self.fly = [0] * FIELD
        for a in self.valid:
            m = 0
            for b in self.valid:
                if dist(a, b) <= kspeed:
                    m |= self.bit[b]
            self.fly[a] = m
        # walk[d][b]: hexes the chaser reaches from d in one turn while the kiter stands on b
        self.walk = {}
        for d in self.valid:
            for b in self.valid:
                if b != d:
                    self.walk[(d, b)] = self._walk(d, b)
        self.win = None
        self.rank = None

    def _walk(self, start, blocker):
        seen = {start: 0}
        queue = deque([start])
        mask = self.bit[start]
        while queue:
            cur = queue.popleft()
            if seen[cur] == self.cspeed:
                continue
            for n in NB[cur]:
                if self.isvalid[n] and n != blocker and n not in seen:
                    seen[n] = seen[cur] + 1
                    mask |= self.bit[n]
                    queue.append(n)
        return mask

    def safe(self, kiter, chaser):
        """the chaser cannot strike the kiter in its next turn"""
        return not (self.walk[(chaser, kiter)] & self.adj[kiter])

    @staticmethod
    def bits(mask):
        out = []
        while mask:
            low = mask & -mask
            out.append(low.bit_length() - 1)
            mask ^= low
        return out

    def solve(self, verbose=False):
        valid, bit = self.valid, self.bit
        walkbits = {key: self.bits(mask) for key, mask in self.walk.items()}
        issafe = {key: not (self.walk[key] & self.adj[key[1]]) for key in self.walk}  # key = (chaser, kiter)
        # X / Y as rows: row[d] = mask over kiter hexes v with (v, d) in the set
        full = {d: sum(bit[v] for v in valid if v != d) for d in valid}
        X = dict(full)
        outer = 0
        while True:
            outer += 1
            Y = {d: 0 for d in valid}
            rank = {}
            inner = 0
            while True:
                inner += 1
                # columns of Y: col[v] = mask over chaser hexes d1 with (v, d1) in Y
                col = {v: 0 for v in valid}
                for d in valid:
                    for v in self.bits(Y[d]):
                        col[v] |= bit[d]
                # move: goodA[d] = kiter hexes v1 that are safe from d and from where every answer of the chaser lies in Y
                goodA = {}
                for d in valid:
                    m = 0
                    for v1 in valid:
                        if v1 != d and issafe[(d, v1)] and not (self.walk[(d, v1)] & ~col[v1]):
                            m |= bit[v1]
                    goodA[d] = m
                # wait: after the chaser's answer d1 the kiter strikes into X or flies into Y
                answer = {d1: (self.adj[d1] & X[d1]) | Y[d1] for d1 in valid}
                newY = {}
                changed = False
                for d in valid:
                    row = 0
                    for v in valid:
                        if v == d:
                            continue
                        if Y[d] & bit[v]:
                            row |= bit[v]
                            continue
                        ok = bool(self.fly[v] & goodA[d])
                        if not ok and issafe[(d, v)]:
                            ok = True
                            for d1 in walkbits[(d, v)]:
                                if not (self.fly[v] & ~bit[d1] & answer[d1]):
                                    ok = False
                                    break
                        if ok:
                            row |= bit[v]
                            rank[(v, d)] = inner
                    newY[d] = row
                    if row != Y[d]:
                        changed = True
                Y = newY
                if not changed:
                    break
            size = sum(bin(Y[d]).count("1") for d in valid)
            if verbose:
                print(f"round {outer}: {size} winning states after {inner} steps", flush=True)
            if all(Y[d] == X[d] for d in valid):
                break
            X = Y
        self.win = X
        self.rank = rank
        return X

    def wins(self, kiter, chaser):
        return bool(self.win[chaser] & self.bit[kiter])

    # --- playing by the solution
    def choose_start(self, v, d):
        """the kiter's action at the start of a round: ("move", hex) or ("wait", None); None: no winning action"""
        best = None
        # wait if it is safe and every answer of the chaser leaves a strike or a winning hex
        if self.safe(v, d):
            ok = True
            strikes = True
            for d1 in self.bits(self.walk[(d, v)]):
                options = self.fly[v] & ~self.bit[d1]
                if options & self.adj[d1] & self.win[d1]:
                    continue
                strikes = False
                if not (options & self.win[d1]):
                    ok = False
                    break
            if ok and strikes:
                return ("wait", None)
            if ok:
                best = ("wait", None)
        # otherwise fly to a winning hex that is safe and stays winning; prefer the one from which a wait strikes soonest
        candidates = []
        for v1 in self.bits(self.fly[v] & ~self.bit[d]):
            if not self.safe(v1, d):
                continue
            answers = self.bits(self.walk[(d, v1)])
            if all(self.wins(v1, d1) for d1 in answers):
                worst = max(self.rank.get((v1, d1), 99) for d1 in answers)
                # how far the chaser is after its best approach: the further, the sooner we may wait
                gap = min(dist(v1, d1) for d1 in answers)
                candidates.append((worst, -gap, v1))
        if candidates:
            candidates.sort()
            move = ("move", candidates[0][2])
            if best is None or candidates[0][0] < self.rank.get((v, d), 99):
                return move
        return best

    def choose_late(self, v, d1):
        """the kiter's action at the end of a round in which it waited: ("attack", from hex) / ("move", hex) / ("stay", None)"""
        options = self.fly[v] & ~self.bit[d1]
        strike = self.bits(options & self.adj[d1] & self.win[d1])
        if strike:
            # strike from the hex with the best way out afterwards: lowest rank, i.e. soonest next strike
            strike.sort(key=lambda h: (self.rank.get((h, d1), 99), h))
            return ("attack", strike[0])
        moves = self.bits(options & self.win[d1])
        if moves:
            moves.sort(key=lambda h: (self.rank.get((h, d1), 99), -dist(h, d1), h))
            if moves[0] == v:
                return ("stay", None)
            return ("move", moves[0])
        return None

    def chaser_reply(self, d, v):
        """a move of the chaser that keeps the kiter out of the winning states, if there is one (else None)"""
        if not self.safe(v, d):
            return ("strike", None)
        bad = [d1 for d1 in self.bits(self.walk[(d, v)]) if not self.wins(v, d1)]
        if bad:
            bad.sort(key=lambda h: (dist(h, v), h))
            return ("move", bad[0])
        return None


def load_or_solve(obstacles, kspeed, cspeed, cache_dir=None, verbose=False):
    key = f"pursuit_{kspeed}_{cspeed}_" + "_".join(str(h) for h in sorted(obstacles))
    path = Path(cache_dir or Path(__file__).resolve().parent / "puppet_cache") / (key[:120] + ".pickle")
    if path.exists():
        with open(path, "rb") as f:
            return pickle.load(f)
    game = Pursuit(obstacles, kspeed, cspeed)
    game.solve(verbose)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        pickle.dump(game, f)
    return game


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--obst", default="")
    ap.add_argument("--kiter", type=int, required=True)
    ap.add_argument("--chaser", type=int, required=True)
    ap.add_argument("--kspeed", type=int, default=9)
    ap.add_argument("--cspeed", type=int, default=4)
    args = ap.parse_args()
    obstacles = [int(x) for x in args.obst.split(",") if x]
    game = Pursuit(obstacles, args.kspeed, args.cspeed)
    game.solve(verbose=True)
    total = sum(1 for d in game.valid for v in game.valid if v != d)
    won = sum(bin(game.win[d]).count("1") for d in game.valid)
    print(f"winning states: {won} of {total}")
    print(f"start {xy(args.kiter)} against {xy(args.chaser)}: {'the kiter wins' if game.wins(args.kiter, args.chaser) else 'the chaser holds'}")
    # where can the chaser stand so that the kiter does not win from anywhere?
    holds = [d for d in game.valid if game.win[d] == 0]
    print("chaser hexes from which the kiter never wins:", [xy(d) for d in holds])
    return 0


if __name__ == "__main__":
    sys.exit(main())
