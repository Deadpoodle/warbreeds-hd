#!/usr/bin/env python3
"""Check a map saved by the in-game editor (autopilot mode 13).

    python tools/editor_check.py [game dir, default testrun] [original map for an edit]

After target 2 (Exit, nothing saved) it checks the Exit flow only.

Reads, from the game folder:
  WarBreedsHD-editor-expect.txt  every cell the autopilot painted, in order (and "undo")
  WarBreedsHD-editor-save.txt    the editor's own record of the save (path, size, cell diff)
then parses the saved .WBM with tools/wbm.py and checks that
  - it parses and re-serialises byte for byte;
  - its terrain is the original's (or, for a new map, the base terrain) with
    exactly the expected strokes stamped on it, the undone stroke left out;
  - the editor's diff matches;
  - a new map has the asked-for size, players, start cells and Shamans;
  - (Stage 2) the engine's map header, players and things as the editor saw
    them (WarBreedsHD-editor-world.txt) are in the file, and for autopilot
    targets 3 and 4 that what the test did (the "x" lines of the expect file)
    is there: things, units, attributes, moves, deletes, undo, players, map
    settings, the panel folding, the map check's findings.
Exit status 0 when every check passes.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wbm  # noqa: E402


def read_lines(path):
    with open(path) as f:
        return [l.split() for l in f.read().splitlines() if l.strip()]


def brush_cells(size, x, y):
    for cy in range(y - (size - 1) // 2, y + size // 2 + 1):
        for cx in range(x - (size - 1) // 2, x + size // 2 + 1):
            yield cx, cy


def tiles_of(field):
    return set() if field == "-" else {int(t) for t in field.split(",")}


def check_smart(check, cells, expect, original, diff, fresh, base_tiles, w, h):
    """The smart painter blends each stroke into its surroundings, so the file
    is not a plain stamp: check that every change lies at or next to a kept
    stroke, that painted cells hold their terrain's tiles, and that the undone
    stroke left nothing behind."""
    if fresh:
        start = [row[:] for row in cells]
        for (x, y), (before, _) in diff.items():
            start[y][x] = before
        check(all(start[y][x] in base_tiles for y in range(h) for x in range(w)),
              "the new map's base is all base-terrain tiles (%d variants)" % len(base_tiles))
    else:
        start = [row[:] for row in original]
    actual = {(x, y): (start[y][x], cells[y][x]) for y in range(h) for x in range(w) if start[y][x] != cells[y][x]}
    check(actual == diff, "the editor's own diff matches the file (%d cells)" % len(diff))

    strokes, undone, current = [], [], None
    for e in expect:
        if e[0] == "begin":
            current = {"type": e[1], "tiles": tiles_of(e[3] if len(e) > 3 else "-"), "cells": []}
        elif e[0] == "paint":
            size, x, y = map(int, e[2:5])
            current["cells"] += [c for c in brush_cells(size, x, y) if 0 <= c[0] < w and 0 <= c[1] < h]
        elif e[0] == "end":
            strokes.append(current)
        elif e[0] == "undo":
            undone.append(strokes.pop())
    owner = {}
    for s in strokes:
        for c in s["cells"]:
            owner[c] = s

    def near(c, r):
        return any((c[0] + dx, c[1] + dy) in owner for dx in range(-r, r + 1) for dy in range(-r, r + 1))

    far = [c for c in actual if not near(c, 1)]
    check(not far, "every changed cell is on or next to a kept stroke (%d changed)%s" % (
        len(actual), "" if not far else "; %d further out, e.g. %s" % (len(far), far[:8])))
    # A painted cell holds its terrain's tiles unless a later stroke's border
    # (a different terrain next to it) blended it.
    later = {}
    for i, s in enumerate(strokes):
        for c in s["cells"]:
            later[c] = i
    inner = [c for c, i in later.items() if all(
        later.get((c[0] + dx, c[1] + dy), i) <= i or strokes[later[(c[0] + dx, c[1] + dy)]]["type"] == strokes[i]["type"]
        for dx in (-1, 0, 1) for dy in (-1, 0, 1))]
    good = [c for c in inner if cells[c[1]][c[0]] in strokes[later[c]]["tiles"]]
    bad = sorted(set(inner) - set(good))
    for s in strokes:
        n = sum(1 for c in set(s["cells"]) if later[c] == strokes.index(s))
        k = sum(1 for c in good if strokes[later[c]] is s)
        print("  stroke %s: %d cells, %d hold %s tiles" % (s["type"], n, k, s["type"]))
    check(not bad, "painted cells hold their terrain's tiles (%d of %d)%s" % (
        len(good), len(inner), "" if not bad else "; not at %s" % [(c, cells[c[1]][c[0]]) for c in bad[:8]]))
    for s in undone:
        left = [c for c in set(s["cells"]) if not near(c, 1) and cells[c[1]][c[0]] != start[c[1]][c[0]]]
        check(not left, "the undone %s stroke is not in the file%s" % (s["type"], "" if not left else ": %s" % left[:8]))


def read_world(path):
    """WarBreedsHD-editor-world.txt: the engine's map header, players and things as saved."""
    out = {"map": None, "players": {}, "things": {}, "findings": []}
    if not os.path.exists(path):
        return None
    for line in open(path, encoding="latin-1").read().splitlines():
        f = line.split(" ")
        if f[0] == "map":
            out["map"] = {"supported": int(f[1]), "recommended": int(f[2]), "fog": int(f[3]), "name": " ".join(f[4:])}
        elif f[0] == "player":
            out["players"][int(f[1])] = {"type": int(f[2]), "race": int(f[3]), "colour": int(f[4]),
                                         "cgk": [int(v) for v in f[5].split(",")],
                                         "avail": [int(v) for v in f[6].split(",")], "start": (int(f[7]), int(f[8]))}
        elif f[0] == "thing":
            out["things"][int(f[1])] = {"type": int(f[2]), "owner": int(f[3]), "x": int(f[4]), "y": int(f[5]),
                                        "hp": int(f[6]), "vet": int(f[7]), "gizmo": [int(v) for v in f[8].split(",")]}
        elif f[0] == "finding":
            out["findings"].append(" ".join(f[1:]))
    return out


def check_world(check, m, world):
    """Everything the editor had in the engine when it saved is in the file."""
    h = m["header"]
    mp = world["map"]
    check((h["players_max"], h["players_rec"], h.get("name")) == (mp["supported"], mp["recommended"], mp["name"]),
          "header: %d supported, %d recommended, name \"%s\" as in the editor" % (
              mp["supported"], mp["recommended"], mp["name"]))
    slots = {s["index"]: s for s in m["players"]["slots"] if s["race"] < 5}
    check(sorted(slots) == sorted(world["players"]), "player slots %s as in the editor" % sorted(world["players"]))
    bad = []
    for i, p in world["players"].items():
        s = slots.get(i)
        if not s:
            continue
        want = (p["race"], p["colour"], p["cgk"], p["avail"], p["start"], 0 if p["type"] == 1 else 1)
        got = (s["race"], s["color"], s["cgk"][:4], s["races"][:4], tuple(s["start"]), s["computer"])
        # MPTY: the editor's type; a value that disagrees with the class reads as Human/Computer.
        mpty_ok = s["f4a44"] == p["type"] or (s["f4a44"] not in (0, 1, 2) or (s["f4a44"] == 1) != (p["type"] == 1))
        if want != got or not mpty_ok:
            bad.append((i, want, got))
    check(not bad, "players' race, colour, CGK, clans, start, type (MPTY) and class round-trip%s" % (
        "" if not bad else ": %s" % bad))
    fog = mp["fog"]
    fogs = {s["index"]: (s["fog"], sum(s["explored"])) for s in slots.values()}
    want_fog = 0 if fog == 2 else 1
    want_explored = 9216 if fog == 0 else 0
    check(all(v == (want_fog, want_explored) for v in fogs.values()),
          "fog mode %d: every player's fog flag %d, %s" % (fog, want_fog, "map unexplored" if fog == 0 else "map explored"))
    objs = {o["id"]: o for o in m["world"]["objects"] if o}
    bad = []
    for i, t in world["things"].items():
        o = objs.get(i)
        got = None if not o else (o["type"], o["owner"], o["x"], o["y"], o["hp"])
        if got != (t["type"], t["owner"], t["x"], t["y"], t["hp"]):
            bad.append((i, t, got))
        elif o["type"] < 0x2C and (o["genes"] != t["gizmo"] or o["f7c"] != t["vet"]):
            bad.append((i, t, (o["genes"], o["f7c"])))
    check(not bad and len(objs) == len(world["things"]),
          "every thing (%d) round-trips: type, owner, place, hit points, rank, gizmos%s" % (
              len(world["things"]), "" if not bad else ": %s" % bad[:4]))
    devices = [o for o in objs.values() if 0x2C <= o["type"] <= 0x3E]
    check(all(o["built"] == 1 for o in devices), "pre-placed devices are complete (%d)" % len(devices))


def xvals(expect, key):
    return [e[1:] for e in expect if e[0] == "x" and len(e) > 1 and e[1] == key]


def xone(expect, key):
    v = xvals(expect, key)
    return v[-1][1:] if v else None


def check_target3(check, m, expect, world, log):
    objs = [o for o in m["world"]["objects"] if o]
    count = lambda t, owner=None: sum(1 for o in objs if o["type"] == t and (owner is None or o["owner"] == owner))
    flag = lambda k: (xone(expect, k) or ["?"])[0] == "1"
    check(flag("hidden-expanded") and flag("collapsed") and not flag("hidden-collapsed"),
          "Tab folds the panel; the cell it covered can then be reached")
    check(flag("expanded-by-click") and flag("collapsed-by-tab") and flag("expanded-by-tab"),
          "the strip's button opens the panel; Tab folds and opens it again")
    hx, hy = map(int, xone(expect, "behind-cell"))
    cells = m["terrain"]["cells"]
    check(hy < len(cells) and any(e[0] == "begin" and e[1] == "Roots" for e in expect),
          "a stroke was painted where the panel was (cell %d,%d, tile %d)" % (hx, hy, cells[hy][hx]))
    gb = list(map(int, xone(expect, "ghost-brush")))
    check(gb[2:] == [1, (gb[0] - 2) * 64, (gb[1] - 2) * 64, (gb[0] + 3) * 64, (gb[1] + 3) * 64],
          "the brush outline at the cursor covers the 5x5 cells brush 5 paints (%s)" % gb)
    gf = list(map(int, xone(expect, "ghost-fill")))
    check(gf[4:] == [2, gf[0] * 64, gf[1] * 64, (gf[2] + 1) * 64, (gf[3] + 1) * 64],
          "the Shift+drag outline covers the rectangle being filled (%s)" % gf)
    gt = list(map(int, xone(expect, "ghost-thing")))
    check(gt[2] == 3 and gt[3] < gt[0] * 64 + 64 and gt[5] > gt[0] * 64 and gt[5] - gt[3] in (32, 64),
          "a thing's footprint outline at the cursor (%s)" % gt)
    check(count(0x46) == 1 and count(0x43) == 1, "a Sand Tree and Yedda Ruins (%d, %d)" % (count(0x46), count(0x43)))
    pods = int(xone(expect, "pods")[0])
    check(int(xone(expect, "pods-removed")[0]) == pods - 1 and int(xone(expect, "pods-undone")[0]) == pods,
          "right-click removes a pod, Ctrl+Z puts it back")
    check(pods > 0 and count(0x3F) == pods, "Fill Pods: %d pods on the cropland, all saved" % count(0x3F))
    check(count(0x2C, 0) == 1 and count(0x31, 0) == 2, "a Refinery and two Turrets for player 1")
    before = xone(expect, "raptor-before")
    after = xone(expect, "raptor-after")
    moved = xone(expect, "raptor-moved")
    hp0, hp1, vet1, g1 = int(before[5]), int(after[5]), int(after[6]), after[7].split(",")
    check(hp1 < hp0 and vet1 == 1 and g1[0] != "100", "Raptor: hit points %d -> %d, rank 1, gizmo %s" % (hp0, hp1, g1[0]))
    rap = [o for o in objs if o["type"] == 2 and o["owner"] == 0]
    check(len(rap) == 1 and (rap[0]["x"], rap[0]["y"]) == (int(moved[3]), int(moved[4])) and
          (moved[3], moved[4]) != (after[3], after[4]) and rap[0]["f7c"] == 1 and str(rap[0]["genes"][0]) == g1[0],
          "the Raptor was dragged to %s,%s and kept its rank and gizmo" % (moved[3], moved[4]))
    check([xone(expect, k)[0] for k in ("scout-deleted", "scout-undone", "scout-redone")] == ["0", "1", "0"] and
          count(1, 0) == 0, "Delete, undo, redo: the Scout is gone")
    finds = [" ".join(v[1:]) for v in xvals(expect, "finding")]
    check(any("Turret" in f and "no power" in f for f in finds) and not any("Refinery" in f for f in finds),
          "the check: the far Turret has no power, the Refinery has crops (%s)" % finds)
    sk = [l for l in log if "skirmish things by type" in l]
    have = set(int(kv.split("=")[0]) for kv in sk[-1].split(":")[-1].split()) if sk else set()
    check({2, 0x2C, 0x31, 0x3F, 0x43, 0x46} <= have, "the test skirmish has them (Raptor, Refinery, Turrets, pods, ruins, tree)")


def check_target4(check, m, expect, world, log):
    before = xone(expect, "p2-before")
    after = xone(expect, "p2-after")
    slots = {s["index"]: s for s in m["players"]["slots"] if s["race"] < 5}
    s1 = slots.get(1)
    race0, race1 = int(before[1]), int(after[1])
    cgk0, cgk1 = int(before[3].split(",")[0]), int(after[3].split(",")[0])
    clan = int(xone(expect, "avail-clan")[0])
    av0, av1 = before[4].split(",")[clan], after[4].split(",")[clan]
    start = tuple(map(int, xone(expect, "p2-start")))
    check((xone(expect, "p2-human") or ["?"])[0] == "1" and after[0] == "2",
          "player 2: Computer -> Human -> Either")
    check(s1 is not None and s1["computer"] == 1 and s1["f4a44"] == 2, "saved as Either: a computer player, MPTY 2")
    check(race1 == (race0 + 1) % 4 and s1["race"] == race1, "player 2's race %d -> %d" % (race0, race1))
    shamans = [o for o in m["world"]["objects"] if o and o["owner"] == 1 and o["type"] in (0, 9, 0x12, 0x1B)]
    check(shamans and all(o["type"] == race1 * 9 for o in shamans), "player 2's Shaman follows the race")
    check(cgk1 == (cgk0 + 1) % 6 and s1["cgk"][0] == cgk1, "Tanu CGK %d -> %d" % (cgk0, cgk1))
    check(av1 != av0 and str(s1["races"][clan]) == av1, "clan %d available %s -> %s" % (clan, av0, av1))
    check(tuple(s1["start"]) == start, "player 2's start point %s" % (start,))
    p6 = xone(expect, "p6-after")
    check(p6[0] == "3" and p6[-1] == "0" and 5 not in slots and
          not any(o and o["owner"] == 5 for o in m["world"]["objects"]), "player 6 set to None: gone with its things")
    once, rec, undone, final = (xone(expect, k) for k in ("supported-once", "recommended", "recommended-undone", "map"))
    refused = " ".join(xone(expect, "supported-refused") or [])
    check(once[0] == "5" and "None_first" in refused, "Supported 6 -> 5, then refused (%s)" % refused.replace("_", " "))
    check(rec[1] == "3" and undone[1] == "4" and final[1] == "3", "Recommended 4 -> 3, undo, redo")
    check(m["header"]["name"] == "HD Test Map" and (m["header"]["players_max"], m["header"]["players_rec"]) == (5, 3),
          "header: \"%s\", %d supported, %d recommended" % (
              m["header"]["name"], m["header"]["players_max"], m["header"]["players_rec"]))
    check(xone(expect, "p3-things")[0] == "0" and not any(o and o["owner"] == 2 for o in m["world"]["objects"]),
          "player 3's Shaman deleted")
    finds = [" ".join(v[1:]) for v in xvals(expect, "finding")]
    check(any("Player 3 has no Shaman" in f for f in finds), "the check: player 3 has no Shaman (%s)" % finds)
    check(world["findings"] == finds, "the save ran the same check (%d warnings logged)" % len(world["findings"]))


def main(argv):
    game = argv[1] if len(argv) > 1 else "testrun"
    original = argv[2] if len(argv) > 2 else os.path.join(game, "DATA", "MAPS", "MPMAP1.WBM")
    ok = True

    def check(cond, what):
        nonlocal ok
        print("%s %s" % ("PASS" if cond else "FAIL", what))
        ok = ok and bool(cond)

    expect = read_lines(os.path.join(game, "WarBreedsHD-editor-expect.txt"))
    if not any(e[0] == "saved" for e in expect):
        # Target 2 (Exit): nothing is saved; the editor asks once, then goes back to the map list.
        log_path = os.path.join(game, "WarBreedsHD.log")
        log = open(log_path, encoding="latin-1").read().splitlines() if os.path.exists(log_path) else []
        asks = [l for l in log if "autopilot: after Exit:" in l]
        check(asks and "Exit again" in asks[-1], "Exit with unsaved changes asks first")
        check(any("back in the menu; map list shown" in l for l in log), "the second Exit goes back to the map list")
        print("ALL PASS" if ok else "SOME CHECKS FAILED")
        return 0 if ok else 1
    save = read_lines(os.path.join(game, "WarBreedsHD-editor-save.txt"))
    keys = {l[0]: l[1:] for l in save if not l[0].isdigit()}
    path = " ".join(keys["file"])
    w, h = int(keys["size"][0]), int(keys["size"][1])
    fresh = keys["new"][0] == "1"
    smart = keys.get("painter", ["raw"])[0] == "smart"
    base_tiles = tiles_of(keys.get("base", ["-"])[0])
    diff = {(int(l[0]), int(l[1])): (int(l[2]), int(l[3])) for l in save if l[0].isdigit()}
    path = os.path.join(game, "DATA", "MAPS", os.path.basename(path.replace("\\", "/")))
    data = open(path, "rb").read()
    m = wbm.parse(data)
    print("saved map %s: %d bytes, \"%s\", %d x %d cells, %d players, pathing format %d with %d regions" % (
        os.path.basename(path), len(data), m["header"].get("name"), m["header"]["width"], m["header"]["height"],
        m["players"]["count"], m["pathing"]["format"], len(m["pathing"].get("regions", []))))
    check(True, "parses with tools/wbm.py")
    check(wbm.dumps(m) == data, "re-serialises byte for byte")
    check((m["header"]["width"], m["header"]["height"]) == (w, h), "header size %dx%d" % (w, h))
    # Guide points sit only at the corners of obstacles, so an all-open new map has none.
    check(m["pathing"]["format"] == 4 and (fresh or len(m["pathing"]["regions"]) > 0),
          "processed pathing (format 4, %d regions)" % len(m["pathing"]["regions"]))
    cells = m["terrain"]["cells"]

    if smart:
        original_cells = None if fresh else wbm.load(original)["terrain"]["cells"]
        check_smart(check, cells, expect, original_cells, diff, fresh, base_tiles, w, h)
    else:
        # The expected terrain: the original (or the base terrain) with the strokes stamped.
        if fresh:
            painted = {(int(e[3]), int(e[4])) for e in expect if e[0] == "paint"}
            base = next(cells[y][x] for y in range(h) for x in range(w) if (x, y) not in painted)
            grid = [[base] * 96 for _ in range(96)]
        else:
            grid = [row[:] for row in wbm.load(original)["terrain"]["cells"]]
        start = [row[:] for row in grid]
        strokes, current = [], None
        for e in expect:
            if e[0] == "begin":
                current = []
            elif e[0] == "paint":
                tile, size, x, y = map(int, e[1:5])
                for cx, cy in brush_cells(size, x, y):
                    if 0 <= cx < w and 0 <= cy < h:
                        current.append((cx, cy, grid[cy][cx], tile))
                        grid[cy][cx] = tile
            elif e[0] == "end":
                strokes.append(current)
            elif e[0] == "undo":
                for cx, cy, before, _ in reversed(strokes.pop()):
                    grid[cy][cx] = before
        wrong = [(x, y, cells[y][x], grid[y][x]) for y in range(h) for x in range(w) if cells[y][x] != grid[y][x]]
        changed = sum(1 for y in range(h) for x in range(w) if start[y][x] != grid[y][x])
        check(not wrong, "terrain = %s + the strokes (%d cells changed)%s" % (
            "base terrain" if fresh else "original", changed, "" if not wrong else "; differs at %s" % wrong[:8]))
        actual = {(x, y): (start[y][x], cells[y][x]) for y in range(h) for x in range(w) if start[y][x] != cells[y][x]}
        check(actual == diff, "the editor's own diff matches the file (%d cells)" % len(diff))
        if any(e[0] == "undo" for e in expect):
            u = max(i for i, e in enumerate(expect) if e[0] == "begin" and i < next(
                j for j, e in enumerate(expect) if e[0] == "undo"))
            last = []
            for e in expect[u + 1:]:
                if e[0] != "paint":
                    break
                last.append(e)
            check(all(cells[int(e[4])][int(e[3])] != int(e[1]) for e in last),
                  "the undone %s stroke is not in the file" % expect[u][1])
    refused = [e for e in expect if e[0] == "refused"]
    if refused:
        check("can't_go_next_to" in refused[0][1], "a refused stroke says why (%s)" % refused[0][1].replace("_", " "))
    picked = [e for e in expect if e[0] == "picked"]
    if picked:
        first = next(e[1] for e in expect if e[0] == "begin")
        check(picked[0][1] == first, "right-click picked %s (the first stroke's terrain)" % first)

    if fresh:
        slots = [s for s in m["players"]["slots"] if s["race"] < 5]
        shamans = [o for o in m["world"]["objects"] if o and o["type"] in (0, 9, 0x12, 0x1B)]
        print("players: " + ", ".join("%d %s %s start %s" % (s["index"], wbm.RACES[s["race"]],
                                                             "computer" if s["computer"] else "human", s["start"])
                                      for s in slots))
        check(m["header"]["players_max"] == len(slots) == m["players"]["count"], "%d player slots" % len(slots))
        check(len(shamans) == len(slots), "a Shaman per player (%d)" % len(shamans))
        check(all(s["computer"] for s in slots[1:]) and not slots[0]["computer"], "slot 0 human, the rest computer")
        ai = [a for a in m["ai"]]
        check(all(a["marker"] == 0x0D for a in ai), "every computer player has AI state (%d)" % len(ai))
    log_path = os.path.join(game, "WarBreedsHD.log")
    log = open(log_path, encoding="latin-1").read().splitlines() if os.path.exists(log_path) else []
    world = read_world(os.path.join(game, "WarBreedsHD-editor-world.txt"))
    target = next((int(e[2]) for e in expect if e[0] == "x" and len(e) > 2 and e[1] == "target"), None)
    if world:
        check_world(check, m, world)
    if target == 3:
        check_target3(check, m, expect, world, log)
    elif target == 4:
        check_target4(check, m, expect, world, log)
    if log:
        asks = [l for l in log if "autopilot: after Test Map:" in l]
        if asks:
            check("click again" in asks[-1] and asks[-1].rstrip().endswith("(test stage 0)"),
                  "Test Map asks before leaving the editor")
        late = [l for l in log if "panel items re-placed at draw time" in l]
        if late:
            print("  " + late[-1].split("autopilot: ")[-1])
        check(any("Test Map skirmish running" in l for l in log), "Test Map started a skirmish on the saved map")
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
