#!/usr/bin/env python3
"""Check a map saved by the in-game editor (autopilot mode 13).

    python tools/editor_check.py [game dir, default testrun] [original map for an edit]

Reads, from the game folder:
  WarBreedsHD-editor-expect.txt  every cell the autopilot painted, in order (and "undo")
  WarBreedsHD-editor-save.txt    the editor's own record of the save (path, size, cell diff)
then parses the saved .WBM with tools/wbm.py and checks that
  - it parses and re-serialises byte for byte;
  - its terrain is the original's (or, for a new map, the base terrain) with
    exactly the expected strokes stamped on it, the undone stroke left out;
  - the editor's diff matches;
  - a new map has the asked-for size, players, start cells and Shamans.
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


def main(argv):
    game = argv[1] if len(argv) > 1 else "testrun"
    original = argv[2] if len(argv) > 2 else os.path.join(game, "DATA", "MAPS", "MPMAP1.WBM")
    ok = True

    def check(cond, what):
        nonlocal ok
        print("%s %s" % ("PASS" if cond else "FAIL", what))
        ok = ok and bool(cond)

    save = read_lines(os.path.join(game, "WarBreedsHD-editor-save.txt"))
    expect = read_lines(os.path.join(game, "WarBreedsHD-editor-expect.txt"))
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
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
