import re
def foreign(days, d):
    for n in days.get(d, {"notes": []})["notes"]:
        if "foreign towns known" in n:
            return set(re.findall(r"[a-z]+\((\d+ \d+ \d+)\)", n.split("towns unfound")[0]))
    return None
def fallen_town(days, d):
    before = foreign(days, d - 2)
    if before is None:
        before = foreign(days, d - 3)
    after = foreign(days, d)
    if after is None:
        after = foreign(days, d + 1)
    new = sorted((after or set()) - (before or set()))
    return tuple(int(x) for x in new[0].split()) if new else None
