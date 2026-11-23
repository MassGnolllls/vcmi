"""
puppet_plays.py - strategies for battles played from outside the game (arena/puppet.py).

A strategy is a function (state, memory) -> action: {"a": "wait" | "defend" | "auto"},
{"a": "move", "hex": h}, {"a": "attack", "target": id, "hex": h}, {"a": "shoot", "target": id}.
"auto" leaves the decision to FairAI's own combat AI.
"""
from puppet import State, NB, FIELD, ONFIELD, dist, hx, xy, unit_hexes  # noqa: F401


def auto(state, memory):
    return {"a": "auto"}


PLAYS = {"auto": auto}


# ------------------------------------------------------------------------------------------ one flyer against one walker

def kite_solved(state, memory):
    """A flyer nobody retaliates against (vampire lords) against one walking stack: played by the exact
    solution of the pursuit game (arena/pursuit.py) - wait while out of reach, strike at the end of the
    round, fly out of reach at the start of the next."""
    import pursuit
    me = state.active
    foes = state.foes()
    if len(foes) != 1 or me["wide"] or foes[0]["wide"]:
        return {"a": "auto"}
    foe = foes[0]
    game = memory.get("game")
    if game is None or memory.get("speeds") != (me["speed"], foe["speed"]):
        game = pursuit.load_or_solve(sorted(state.obst), me["speed"], foe["speed"])
        memory["game"], memory["speeds"] = game, (me["speed"], foe["speed"])
    v, d = me["pos"], foe["pos"]
    if not me["waited"]:
        choice = game.choose_start(v, d)
        if choice is None:
            memory["lost_at"] = (state.round, v, d)
            return {"a": "auto"}
        if choice[0] == "wait":
            return {"a": "wait"}
        return {"a": "defend"} if choice[1] == v else {"a": "move", "hex": choice[1]}
    choice = game.choose_late(v, d)
    if choice is None:
        return {"a": "auto"}
    if choice[0] == "attack":
        return {"a": "attack", "target": foe["id"], "hex": choice[1]}
    if choice[0] == "move":
        return {"a": "move", "hex": choice[1]}
    return {"a": "defend"}


# ------------------------------------------------------------------------------------------ simple opponents

def _strike_or_approach(state, target_value):
    """strike the reachable enemy with the highest target_value; else walk towards the nearest one"""
    me = state.active
    foes = {u["id"]: u for u in state.foes()}
    if state.shoot:
        best = max(state.shoot, key=lambda i: target_value(foes[i]))
        return {"a": "shoot", "target": best}
    if state.attacks:
        tid, frm = max(state.attacks, key=lambda p: (target_value(foes[p[0]]), -dist(me["pos"], p[1])))
        return {"a": "attack", "target": tid, "hex": frm}
    if not state.reach_now:
        return {"a": "defend"}
    dists = state.distances(me)

    def gap(h):
        occ = state.occupied(without=(me["id"],))
        best = 99
        for f in foes.values():
            for fh in unit_hexes(f):
                for a in unit_hexes(me, h):
                    best = min(best, dist(a, fh))
        return best
    target = min(state.reach_now, key=lambda h: (gap(h), dists.get(h, 99)))
    if target == me["pos"]:
        return {"a": "defend"}
    return {"a": "move", "hex": target}


def rush(state, memory):
    """every unit strikes the most valuable enemy stack in reach, otherwise closes in"""
    return _strike_or_approach(state, lambda u: u["count"] * u["maxhp"])


def hold_centre(state, memory):
    """a walker that stays in the middle of the field and strikes only what comes next to its reach"""
    me = state.active
    if state.attacks:
        return _strike_or_approach(state, lambda u: u["count"] * u["maxhp"])
    centre = hx(8, 5)
    if me["pos"] == centre or not state.reach_now:
        return {"a": "defend"}
    target = min(state.reach_now, key=lambda h: dist(h, centre))
    if dist(target, centre) >= dist(me["pos"], centre):
        return {"a": "defend"}
    return {"a": "move", "hex": target}


def chase_solved(state, memory):
    """the walker of the pursuit game played against the solution: strike if it can; else move to where
    the flyer is no longer winning, if there is such a hex; else close in"""
    import pursuit
    me = state.active
    foes = state.foes()
    if len(foes) != 1 or me["wide"] or foes[0]["wide"] or state.attacks:
        return rush(state, memory)
    foe = foes[0]
    game = memory.get("game")
    if game is None:
        game = pursuit.load_or_solve(sorted(state.obst), foe["speed"], me["speed"])
        memory["game"] = game
    reply = game.chaser_reply(me["pos"], foe["pos"])
    if reply and reply[0] == "move":
        memory["escapes"] = memory.get("escapes", 0) + 1
        return {"a": "defend"} if reply[1] == me["pos"] else {"a": "move", "hex": reply[1]}
    return rush(state, memory)


PLAYS.update({"kite": kite_solved, "rush": rush, "centre": hold_centre, "chase": chase_solved})


# ------------------------------------------------------------------------------------------ helpers

AI_VALUE = {"grandElf": 331, "centaur": 100, "pegasus": 518, "dwarf": 138, "gnollMarauder": 90, "gnoll": 56,
            "mightyGorgon": 1028, "ancientBehemoth": 5397, "goblin": 60, "hobgoblinWolfRider": 203, "dreadKnight": 2382,
            "vampireLord": 783, "hydra": 4120, "chaosHydra": 5931, "dendroidGuard": 517}


def kind(unit):
    return unit["type"].split(":")[-1]


def value_of(unit):
    return AI_VALUE.get(kind(unit), unit["maxhp"] * 10)


def gap(state, a, b, a_pos=None, b_pos=None):
    """distance in hexes between two units (their nearest hexes)"""
    return min(dist(x, y) for x in unit_hexes(a, a_pos) for y in unit_hexes(b, b_pos))


def go(state, target, exact_only=False, avoid=()):
    """the active unit walks to `target` (head hex), or as near to it as it gets this turn - never
    ending with one of its hexes on `avoid`"""
    me = state.active
    if me["pos"] == target:
        return {"a": "defend"}
    if target in state.reach_now:
        return {"a": "move", "hex": target}
    options = [h for h in state.reach_now if not any(x in avoid for x in unit_hexes(me, h))]
    if exact_only or not options:
        return {"a": "defend"}
    back = state.distances(me, start=target)  # steps from the target, for our kind of unit
    best = min(options, key=lambda h: (back.get(h, 99 + dist(h, target)), dist(h, target)))
    if back.get(best, 99 + dist(best, target)) >= back.get(me["pos"], 99 + dist(me["pos"], target)):
        return {"a": "defend"}
    return {"a": "move", "hex": best}


def strike(state, target_id, key=None):
    """the active unit strikes `target_id` from the best hex it can strike from (key: lower is better); None if out of reach"""
    options = [frm for tid, frm in state.attacks if tid == target_id]
    if not options:
        return None
    me = state.active
    frm = min(options, key=key or (lambda h: dist(h, me["pos"])))
    return {"a": "attack", "target": target_id, "hex": frm}


def melee_damage(attacker, defender, count=None):
    """average damage of one strike by the game's formula (attack against defence only)"""
    count = attacker["count"] if count is None else count
    base = count * (attacker["dmg"][0] + attacker["dmg"][1]) / 2
    diff = attacker["att"] - defender["def"]
    return base * (1 + 0.05 * min(diff, 60)) if diff >= 0 else base * max(0.3, 1 + 0.025 * diff)


def kills_value(attacker, defender):
    """value an average strike of `attacker` kills in `defender`"""
    dmg = melee_damage(attacker, defender)
    total = (defender["count"] - 1) * defender["maxhp"] + defender["hp"]
    dead = defender["count"] if dmg >= total else int((dmg + defender["maxhp"] - defender["hp"]) // defender["maxhp"])
    return dead * value_of(defender) + 0.01 * dmg


def flee(state, foes=None, avoid=(), away_from=None):
    """the active unit steps to where none of `foes` can strike it in their next turn - as far from them as
    it gets (and from the hex `away_from`, where they are heading), never ending on `avoid`"""
    me = state.active
    foes = state.foes() if foes is None else foes
    best, best_key = None, None
    for h in [me["pos"]] + list(state.reach_now):
        if any(x in avoid for x in unit_hexes(me, h)) and not (h == me["pos"] and not state.reach_now):
            continue
        danger = sum(1 for f in foes if state.can_strike_fast(f, me, h))
        far = min(gap(state, me, f, h) for f in foes)
        k = (danger, -far)
        if away_from is not None:
            # behind the enemy, where it came from: it walks towards `away_from`, and whoever fled there too is caught
            k = (danger, far < 7, -dist(h, away_from), -far)
        if best_key is None or k < best_key:
            best, best_key = h, k
    return {"a": "defend"} if best == me["pos"] else {"a": "move", "hex": best}


def worst_kill(state, enemy, placed):
    """the most valuable kill `enemy` (a walker) can make in its next turn if our units stand as in `placed`
    (unit id -> head hex; everybody else as now)"""
    occ = set()
    where = {}
    for u in state.units.values():
        if u["side"] == enemy["side"]:
            if u["id"] != enemy["id"]:
                occ.update(unit_hexes(u))
            continue
        pos = placed.get(u["id"], u["pos"])
        where[u["id"]] = unit_hexes(u, pos)
        occ.update(where[u["id"]])
    reach = state.reach(enemy, occ)
    worst = 0.0
    for uid, hexes in where.items():
        if any(State.touching(unit_hexes(enemy, h), hexes) for h in reach):
            worst = max(worst, kills_value(enemy, state.units[uid]))
    return worst


def shield(state, shields, enemy, memory):
    """The active unit is one of `shields` (cheap stacks): it stands where the enemy's best kill in its next
    turn is worth least, counting on the shields that act after it to do the same."""
    me = state.active
    later = [u for u in shields if u["id"] != me["id"] and not u["moved"] and not u["defending"]]
    others_occ = state.occupied(without=tuple(u["id"] for u in shields))

    def options(u, taken):
        occ = others_occ | taken
        for o in shields:
            if o["id"] != u["id"] and o["id"] not in placed:
                occ = occ | set(unit_hexes(o))
        out = [u["pos"]] if not any(x in occ for x in unit_hexes(u)) else []
        out += [h for h in state.reach(u, occ) if h != u["pos"]]
        return out

    best, best_key = me["pos"], None
    placed = {}
    for p in options(me, set()):
        placed = {me["id"]: p}
        taken = set(unit_hexes(me, p))
        for nxt in later[:1]:  # one unit ahead is enough for a wall of two or three
            choice, choice_score = nxt["pos"], None
            for q in options(nxt, taken):
                score = worst_kill(state, enemy, {me["id"]: p, nxt["id"]: q})
                if choice_score is None or score < choice_score:
                    choice, choice_score = q, score
            placed[nxt["id"]] = choice
        score = worst_kill(state, enemy, placed)
        key = (score, 0 if p == me["pos"] else 1, dist(p, me["pos"]))
        if best_key is None or key < best_key:
            best, best_key = p, key
    return {"a": "defend"} if best == me["pos"] else {"a": "move", "hex": best}


# ------------------------------------------------------------------------------------------ picture 1: shooters in a corner

def b1_left(state, memory):
    """Grand elves in the corner (1,0), the dwarves next to them on (2,0), the three single centaurs as
    a wall around both - heads on (4,0), (4,1) and (2,1), which leaves no hex next to elves or dwarves.
    The stack of three centaurs and the pegasus keep out of reach. The elves wait while the enemy is
    beyond full range and shoot once it has come closer."""
    me = state.active
    gnolls = state.foes()
    if not gnolls:
        return {"a": "defend"}
    slot = me["slot"]
    pocket = {hx(x, y) for x in range(1, 7) for y in range(0, 5)}  # nobody else gets in the way there
    if slot == 0:  # the elves
        if state.shoot:
            far = min(gap(state, me, f) for f in gnolls) > 10
            if far and not me["waited"]:
                return {"a": "wait"}
            return {"a": "shoot", "target": max(state.shoot, key=lambda i: state.units[i]["count"])}
        return {"a": "auto"}
    dwarves = state.by_slot(0, 3)
    dwarves_home = dwarves is None or dwarves["pos"] == hx(2, 0)
    way_in = {hx(1, 2), hx(2, 1), hx(2, 0)}  # what the dwarves still have to walk over
    if slot == 3:  # the dwarves: two steps to (1,3), then over (1,2) and (2,1) to (2,0)
        if me["pos"] == hx(2, 0):
            return {"a": "defend"}
        return go(state, hx(1, 3) if state.round == 1 else hx(2, 0))
    if slot in (1, 2):  # three centaurs, pegasus: out of the way and out of reach
        return flee(state, gnolls, avoid=pocket)
    # the single centaurs, by slot: (4,0), (4,1), and (2,1) once the dwarves are in (until then it waits on (3,3))
    singles = sorted((u for u in state.mine() if u["slot"] in (4, 5, 6)), key=lambda u: u["slot"])
    if len(singles) == 3:
        place = {4: hx(4, 0), 5: hx(4, 1), 6: hx(2, 1) if dwarves_home else hx(3, 3)}[slot]
    else:
        # one fell: the others stand where the gnolls' best kill of this round is worth least
        return shield(state, singles, gnolls[0], memory)
    if me["pos"] == place:
        return {"a": "defend"}
    return go(state, place, avoid=() if dwarves_home or state.round == 1 else way_in)


def b1_right(state, memory):
    """The gnolls: straight at the shooters, beyond their full range while that costs no time, then the
    most valuable kill in reach."""
    me = state.active
    foes = {u["id"]: u for u in state.foes()}
    if state.attacks:
        tid, frm = max(state.attacks, key=lambda p: (kills_value(me, foes[p[0]]), -dist(me["pos"], p[1])))
        return {"a": "attack", "target": tid, "hex": frm}
    shooters = [u for u in foes.values() if u["shots"] > 0]
    goal = max(foes.values(), key=lambda u: u["count"] * value_of(u))
    if not state.reach_now:
        return {"a": "defend"}
    # steps still to go to a hex next to anything of the enemy, counted from every hex we reach now
    occ = state.occupied(without=(me["id"],))
    best, best_key = None, None
    targets = [h for u in foes.values() for uh in unit_hexes(u) for h in NB[uh] if ONFIELD[h] and h not in occ and h not in state.obst]
    goal_hexes = [h for uh in unit_hexes(goal) for h in NB[uh] if ONFIELD[h] and h not in state.obst]
    for h in [me["pos"]] + list(state.reach_now):
        d = state.distances(me, occ, start=h)
        to_any = min((d.get(t, 99) for t in targets), default=99)
        to_goal = min((d.get(t, 99) for t in goal_hexes), default=99)
        in_range = any(gap(state, me, s, h) <= 10 for s in shooters)
        # rounds until we can strike (anything / the goal) matter most; then stay out of full range
        k = ((to_goal + me["speed"] - 1) // me["speed"], (to_any + me["speed"] - 1) // me["speed"], in_range, to_goal, to_any)
        if best_key is None or k < best_key:
            best, best_key = h, k
    return {"a": "defend"} if best == me["pos"] else {"a": "move", "hex": best}


PLAYS.update({"b1left": b1_left, "b1right": b1_right})


# ------------------------------------------------------------------------------------------ pictures 4 and 5: a fortress at the edge

def pending(u):
    """has not acted in this round yet"""
    return not u["moved"]


class Fortress:
    """One strong stack two hexes wide (the striker) and six cheap ones against one fast enemy stack two
    hexes wide. The striker stands where few hexes lead to it - at the left edge in an odd row (head
    (2,y), tail (1,y): an enemy two hexes wide needs (2,y-1), (2,y+1) or the pair (3,y)+(4,y) to stand
    next to it, three cheap stacks close all of it) or in a corner (six close every way in twice over, so
    that one fallen stack opens nothing). Whenever the enemy stands in reach at our turn, one cheap
    stack attacks it first and takes the retaliation; the striker, which has waited, strikes afterwards
    without being struck back.

    home / walls: where striker and cheap stacks (by slot) end up; home1 / walls1: where they stand after
    the first round, if the final places cannot be reached at once; spares: cheap stacks that are no part
    of the wall wait there and step into a gap."""

    def __init__(self, striker_slot, home, walls, spares=None, unabsorbed=False, sortie=True, home1=None, walls1=None):
        self.striker_slot, self.home, self.walls, self.spares = striker_slot, home, walls, spares or {}
        self.unabsorbed, self.sortie, self.home1, self.walls1 = unabsorbed, sortie, home1, walls1

    def chance(self, state, striker, enemy, smalls):
        """can a cheap stack that has not acted yet attack the enemy, and the striker strike after it?"""
        if not enemy["retal"]:
            return bool(state.strike_hexes(striker, unit_hexes(enemy), state.occupied(without=(striker["id"],))))
        for u in smalls:
            if pending(u) and state.strike_hexes(u, unit_hexes(enemy)):
                occ = state.occupied(without=(striker["id"], u["id"]))
                if state.strike_hexes(striker, unit_hexes(enemy), occ):
                    return True
        return False

    def __call__(self, state, memory):
        me = state.active
        foes = state.foes()
        if not foes:
            return {"a": "defend"}
        enemy = max(foes, key=lambda u: u["count"] * value_of(u))
        striker = state.by_slot(me["side"], self.striker_slot)
        mine_now = [tid for tid, _ in state.attacks if tid == enemy["id"]]
        smalls = [u for u in state.mine() if u["slot"] != self.striker_slot]
        if me["slot"] == self.striker_slot:
            home = self.home1 if (state.round == 1 and self.home1 is not None) else self.home
            if not me["waited"]:
                if me["pos"] != home and home in state.reach_now and not self.chance(state, me, enemy, smalls):
                    return {"a": "move", "hex": home}
                return {"a": "wait"}
            if mine_now and (not enemy["retal"] or self.unabsorbed) and (self.sortie or any(f == me["pos"] for t, f in state.attacks)):
                return strike(state, enemy["id"], key=lambda h: (h != me["pos"], dist(h, self.home)))
            if me["pos"] != home and home in state.reach_now and not mine_now:
                return {"a": "move", "hex": home}
            return {"a": "defend"}
        if striker is None:
            return rush(state, memory)
        wall_hexes = list(self.walls.values())
        on_wall = {u["pos"]: u for u in smalls if u["pos"] in wall_hexes}

        # --- take the retaliation? Only if the striker can strike afterwards, and nobody cheaper who acts later can do it
        if enemy["retal"] and mine_now and pending(striker):
            occ = state.occupied(without=(striker["id"], me["id"]))
            if self.sortie:
                reachable = bool(state.strike_hexes(striker, unit_hexes(enemy), occ))
            else:
                reachable = State.touching(unit_hexes(striker), unit_hexes(enemy))
            if reachable:
                def cost(u):
                    return (u["count"] > 1, u["slot"])
                better = [u for u in smalls if u["id"] != me["id"] and pending(u) and cost(u) < cost(me)
                          and state.strike_hexes(u, unit_hexes(enemy))]
                if not better:
                    return strike(state, enemy["id"])
        # --- the enemy cannot strike back any more in this round: cheap stacks outside the wall add their blow
        if not enemy["retal"] and mine_now and me["pos"] not in wall_hexes and not pending(striker):
            return strike(state, enemy["id"])
        # --- places
        if state.round == 1:
            first = (self.walls1 or self.walls).get(me["slot"], self.spares.get(me["slot"]))
            return go(state, first) if first is not None else {"a": "defend"}
        if striker["pos"] != self.home:
            return {"a": "defend"}  # the striker is out (or still on its way): nobody shuffles around it
        place = self.walls.get(me["slot"])
        if place is not None:
            return go(state, place) if me["pos"] != place else {"a": "defend"}
        empty = [h for h in wall_hexes if h not in on_wall]
        if empty:
            back = {h: state.distances(me, start=h).get(me["pos"], 99) for h in empty}
            target = min(empty, key=lambda h: back[h])
            others = [u for u in smalls if u["id"] != me["id"] and u["pos"] not in wall_hexes and pending(u) and u["slot"] not in self.walls]
            if not any(state.distances(u, start=target).get(u["pos"], 99) < back[target] for u in others):
                return go(state, target)
        spot = self.spares.get(me["slot"])
        if spot is not None and state.round <= 2:
            return go(state, spot)
        return {"a": "defend"}


def striker_can_reach(state, striker, me, pos):
    """could the enemy's strong stack strike us on `pos` in its next turn - its own cheap stacks stepping aside for it"""
    return state.can_strike_fast(striker, me, pos, ignore_allies=True)


def hit_and_run(normal_hit=True, prefer="striker"):
    """The fast stack: wait at the start of a round, strike at its end - and so be the first to act in
    the next round as well: strike again, or step out of reach before the enemy can answer. It never
    stands in the reach of the enemy's strong stack (the striker) while the enemy has its turn.
    normal_hit: it also strikes the striker at the start of a round when it can (then the enemy answers
    in the same round). prefer: what it strikes at the end of a round - the striker, or the cheap
    stacks one by one until none is left ("smalls")."""

    def play(state, memory):
        me = state.active
        foes = state.foes()
        if not foes:
            return {"a": "defend"}
        striker = max(foes, key=lambda u: u["count"] * value_of(u))
        smalls = [u for u in foes if u["id"] != striker["id"]]
        on_striker = [(t, f) for t, f in state.attacks if t == striker["id"]]
        on_smalls = [(t, f) for t, f in state.attacks if t != striker["id"]]
        spare_striker = prefer == "smalls" and smalls

        def unsafe(pos):
            return striker_can_reach(state, striker, me, pos)

        def staging():
            # a hex the striker cannot reach, from which our next strike is near
            best, best_key = me["pos"], None
            aims = smalls if (spare_striker or not on_striker and prefer == "smalls" and smalls) else [striker]
            for h in [me["pos"]] + list(state.reach_now):
                bad = unsafe(h)
                near = min(gap(state, me, u, h) for u in aims)
                far = gap(state, me, striker, h)
                key = (bad, near if not bad else -far, -far)
                if best_key is None or key < best_key:
                    best, best_key = h, key
            return {"a": "defend"} if best == me["pos"] else {"a": "move", "hex": best}

        if not me["waited"]:
            if on_striker and normal_hit and not spare_striker:
                t, f = on_striker[0]
                return {"a": "attack", "target": t, "hex": f}
            if not unsafe(me["pos"]):
                return {"a": "wait"}
            return staging()
        # the end of the round: we act last, and first again in the next
        if on_striker and not (spare_striker and on_smalls):
            if not spare_striker or not smalls:
                t, f = on_striker[0]
                return {"a": "attack", "target": t, "hex": f}
        best, best_key = None, None
        for t, f in on_smalls:
            victim = state.units[t]
            key = (-kills_value(me, victim), dist(f, me["pos"]))
            if best_key is None or key < best_key:
                best, best_key = (t, f), key
        if best:
            return {"a": "attack", "target": best[0], "hex": best[1]}
        if on_striker and not smalls:
            t, f = on_striker[0]
            return {"a": "attack", "target": t, "hex": f}
        return staging()

    return play


# the corner (1,0)+(2,0), every way in closed twice: (3,0)+(4,0), (1,1)+(2,1), (3,1)+(4,1)
B5C = dict(striker_slot=1, home=hx(2, 0), home1=hx(3, 0),
           walls1={0: hx(2, 1), 2: hx(4, 0), 3: hx(4, 1), 4: hx(1, 1), 5: hx(1, 3), 6: hx(1, 5)},
           walls={0: hx(2, 1), 2: hx(3, 0), 3: hx(4, 0), 4: hx(1, 1), 5: hx(3, 1), 6: hx(4, 1)})
B4C = dict(striker_slot=0, home=hx(2, 0),
           walls1={1: hx(2, 1), 2: hx(3, 0), 3: hx(3, 1), 4: hx(1, 2), 5: hx(1, 4), 6: hx(1, 6)},
           walls={1: hx(2, 1), 2: hx(3, 0), 3: hx(3, 1), 4: hx(1, 1), 5: hx(4, 1), 6: hx(4, 0)})
B5 = dict(striker_slot=1, home=hx(2, 3), walls={2: hx(2, 2), 3: hx(2, 4), 4: hx(3, 3)}, spares={0: hx(1, 2), 5: hx(1, 4), 6: hx(1, 5)})
B4 = dict(striker_slot=0, home=hx(2, 3), walls={2: hx(2, 2), 3: hx(3, 3), 4: hx(2, 4)}, spares={1: hx(1, 2), 5: hx(1, 4), 6: hx(1, 5)})
PLAYS.update({
    "b5fort": Fortress(**B5), "b5fortU": Fortress(**B5, unabsorbed=True),
    "b4fort": Fortress(**B4), "b4fortU": Fortress(**B4, unabsorbed=True), "b4fortH": Fortress(**B4, sortie=False),
    "b5corner": Fortress(**B5C), "b4corner": Fortress(**B4C), "b4cornerU": Fortress(**B4C, unabsorbed=True),
    "hitrun": hit_and_run(True, "striker"), "endhit": hit_and_run(False, "striker"), "strip": hit_and_run(False, "smalls"),
})


class Net:
    """The strong stack (striker) walks to the middle of the field with its cheap stacks around it - never
    ahead of them. From the middle it reaches almost every hex, so the fast enemy finds no place to wait
    in safety and no time to pick off the cheap stacks one by one; wherever it stands at our turn, a
    cheap stack attacks first and takes the retaliation, then the striker strikes."""

    def __init__(self, striker_slot, anchor, pace=4, unabsorbed=False):
        self.striker_slot, self.anchor, self.pace, self.unabsorbed = striker_slot, anchor, pace, unabsorbed
        self.helper = Fortress(striker_slot, anchor, {}, {}, unabsorbed=unabsorbed)

    def __call__(self, state, memory):
        me = state.active
        foes = state.foes()
        if not foes:
            return {"a": "defend"}
        enemy = max(foes, key=lambda u: u["count"] * value_of(u))
        striker = state.by_slot(me["side"], self.striker_slot)
        smalls = [u for u in state.mine() if u["slot"] != self.striker_slot]
        mine_now = [tid for tid, _ in state.attacks if tid == enemy["id"]]
        if me["slot"] == self.striker_slot:
            if not me["waited"]:
                if self.helper.chance(state, me, enemy, smalls) or not state.reach_now:
                    return {"a": "wait"}
                if mine_now and not smalls:
                    return strike(state, enemy["id"])
                # towards the anchor, at the pace of the cheap stacks
                if me["pos"] == self.anchor:
                    return {"a": "wait"}
                back = state.distances(me, start=self.anchor)
                here = state.distances(me)
                options = [h for h in state.reach_now if here.get(h, 99) <= self.pace]
                if not options:
                    return {"a": "wait"}
                best = min(options, key=lambda h: (back.get(h, 99 + dist(h, self.anchor)), dist(h, self.anchor)))
                if back.get(best, 99) >= back.get(me["pos"], 99):
                    return {"a": "wait"}
                return {"a": "move", "hex": best}
            if mine_now and (not enemy["retal"] or self.unabsorbed or not smalls):
                return strike(state, enemy["id"], key=lambda h: (h != me["pos"], dist(h, self.anchor)))
            return {"a": "defend"}
        if striker is None:
            return rush(state, memory)
        if enemy["retal"] and mine_now and pending(striker):
            occ = state.occupied(without=(striker["id"], me["id"]))
            if state.strike_hexes(striker, unit_hexes(enemy), occ):
                def cost(u):
                    return (u["count"] > 1, u["slot"])
                better = [u for u in smalls if u["id"] != me["id"] and pending(u) and cost(u) < cost(me)
                          and state.strike_hexes(u, unit_hexes(enemy))]
                if not better:
                    return strike(state, enemy["id"])
        if not enemy["retal"] and mine_now and not pending(striker):
            return strike(state, enemy["id"])
        # stay around the striker, two hexes from it, on the side the enemy is on
        if not state.reach_now:
            return {"a": "defend"}
        core = unit_hexes(striker)
        def key(h):
            d = min(dist(h, c) for c in core)
            return (abs(d - 2), gap(state, me, enemy, h))
        best = min([me["pos"]] + list(state.reach_now), key=key)
        return {"a": "defend"} if best == me["pos"] else {"a": "move", "hex": best}


PLAYS.update({"b4net": Net(0, hx(9, 5)), "b4netU": Net(0, hx(9, 5), unabsorbed=True), "b5net": Net(1, hx(8, 4))})


# ------------------------------------------------------------------------------------------ picture 3: one flyer that drains life against several walkers

def vl_drain(floor=120, greed=1.0):
    """Vampire lords (fly, nobody retaliates, health drained comes back and raises the fallen) against
    several walkers. Every choice is weighed as health won back by our strikes against health the
    enemy's answers cost: wait while at most one enemy reaches us (the strike at the end of the round
    is free, and we are the first to act in the next), strike where few can answer, fly to where
    nobody reaches us when a strike would cost more than it brings."""

    def play(state, memory):
        me = state.active
        foes = state.foes()
        if not foes:
            return {"a": "defend"}
        foe_by_id = {u["id"]: u for u in foes}
        total = (me["count"] - 1) * me["maxhp"] + me["hp"]
        deficit = me["base"] * me["maxhp"] - total
        enemy_hp = sum((u["count"] - 1) * u["maxhp"] + u["hp"] for u in foes)
        # health we can still drain is what the enemy has left: no hit may cost more than that can pay back
        scarce = enemy_hp < 2 * deficit + 3 * me["maxhp"]

        def hit(u):
            return melee_damage(u, me)

        def hp(u):
            return (u["count"] - 1) * u["maxhp"] + u["hp"]

        def threats(pos, speed_factor=1, without=()):
            return [u for u in foes if u["id"] not in without and state.can_strike_fast(u, me, pos, speed=u["speed"] * speed_factor)]

        def dealt(u):
            return min(melee_damage(me, u), hp(u))

        def worth(strikes, incoming, later=None):
            """health our strikes bring (they heal what is missing) against health lost, plus a little for progress"""
            value = 0.0
            missing = deficit
            for d in strikes:
                value += min(d, missing + incoming) + greed * d
            value -= incoming * (3.0 if scarce else 1.0)
            if total - 1.35 * incoming < floor:
                value -= 500
            if later is not None:
                value += 6 if later == 0 else 3 if later == 1 else 0
            return value

        positions = [me["pos"]] + list(state.reach_now)
        best_dealt = max((dealt(u) for u in foes), default=0)

        if not me["waited"]:
            options = []
            here = threats(me["pos"])
            # wait: those who reach us strike, then we strike at the end of the round
            options.append((worth([best_dealt], sum(hit(u) for u in here)) + 1, ("wait",)))
            for tid, frm in state.attacks:
                victim = foe_by_id[tid]
                d = dealt(victim)
                answer = threats(frm, without=(tid,) if d >= hp(victim) else ())
                options.append((worth([d], sum(hit(u) for u in answer)) + 2, ("attack", tid, frm)))
            for q in positions:
                now = threats(q)
                later = len(threats(q, 2)) if not now else None
                options.append((worth([], sum(hit(u) for u in now), later), ("move", q)))
            value, choice = max(options, key=lambda o: o[0])
            if choice[0] == "wait":
                return {"a": "wait"}
            if choice[0] == "attack":
                return {"a": "attack", "target": choice[1], "hex": choice[2]}
            return {"a": "defend"} if choice[1] == me["pos"] else {"a": "move", "hex": choice[1]}

        # the end of the round: a free strike. Whom, and from where? We act again at once: from a hex where few can
        # answer a second strike, on the unit nearest to its death (a dead unit answers nothing)
        best, best_key = None, None
        safe_cost = {}

        def cost_at(q, without=()):
            k = (q, without)
            if k not in safe_cost:
                safe_cost[k] = sum(hit(u) for u in threats(q, without=without))
            return safe_cost[k]

        field = [h for h in range(FIELD) if ONFIELD[h] and h not in state.obst]
        occupied = state.occupied(without=(me["id"],))
        for tid, frm in state.attacks:
            victim = foe_by_id[tid]
            d = dealt(victim)
            dies = d >= hp(victim)
            gone = (tid,) if dies else ()
            # what the next round costs at least: strike again from here, or fly on to the best hex in reach
            again = cost_at(frm, gone)
            away = min((cost_at(q, gone) for q in field if q not in occupied and dist(frm, q) <= me["speed"]), default=again)
            after = min(again - 0.5 * best_dealt, away)
            key = (after - (40 if dies else 0) - 0.2 * min(d, deficit), hp(victim) - d, dist(frm, me["pos"]))
            if best_key is None or key < best_key:
                best, best_key = (tid, frm), key
        if best:
            return {"a": "attack", "target": best[0], "hex": best[1]}
        q = min(positions, key=lambda h: (sum(hit(u) for u in threats(h)), -min(gap(state, me, u, h) for u in foes)))
        return {"a": "defend"} if q == me["pos"] else {"a": "move", "hex": q}

    return play


PLAYS.update({"vl": vl_drain(), "vlgreedy": vl_drain(greed=0.5)})
