#!/usr/bin/env python3
"""Check a map whose events and Global AI settings the in-game editor edited
(autopilot mode 14, TARGET 0).

    python tools/events_check.py [game dir, default testrun]

Reads WarBreedsHD-events-expect.txt from the game folder (the saved file's
name, the original map, the events and AI settings as the editor held them at
the save), parses the saved .WBM and the original with tools/wbm.py and checks:
  - the saved map parses and re-serialises byte for byte;
  - its trigger section ("ER") holds exactly the editor's events, which are the
    ones the test script built (TIMED 60 -> OK DIALOG 72/73/74; TIMED 150 ->
    CHAT from 231 "hd event fired", PLAYER END 231 victory after 40);
  - the AI state of every computer player holds the editor's settings: the
    test's three changes, everything else as in the original map;
  - the terrain and things are the original's (only events and AI changed);
  - the log shows the Test Map skirmish firing both events and ending.
Exit status 0 when every check passes.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wbm  # noqa: E402

SCRIPT = [
    "E0 C0{60} -> A21{0 72 73 74}",
    "E1 C0{150} -> A12{0 231 'hd event fired'} A11{40 231 1}",
]


def describe(events):
    """The editor's events::Describe() format, from the parsed "ER" section."""
    out = []
    for i, e in enumerate(events["events"]):
        s = "E%d%s" % (i, " P" if e["AJ"] == 1 else "")
        for c in e["conditions"]:
            s += " C0{%d}" % c["CC"] if c["type"] == 0 else " C%d" % c["type"]
        s += " ->"
        for a in e["actions"]:
            t, d = a["type"], a["AD"]
            if t == 12:
                s += " A12{%d %d '%s'}" % (d, a["PL"], a["ST"].split(b"\0")[0].decode("latin-1")[:40])
            elif t == 11:
                s += " A11{%d %d %d}" % (d, a["PL"], a["VI"])
            elif t == 21:
                s += " A21{%d %d %d %d}" % (d, a["T1"], a["T2"], a["T3"])
            elif t == 7:
                s += " A7{%d %d}" % (d, a["QU"])
            else:
                s += " A%d{%d}" % (t, d)
        out.append(s)
    return out


def main(argv):
    game = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                      "testrun")
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + what)
        ok = ok and bool(cond)

    view = os.path.join(game, "WarBreedsHD-events-view.txt")
    if os.path.exists(view):  # TARGET 1: a map's own events as the editor loaded them
        lines = open(view).read().splitlines()
        name = lines[0].split()[1]
        m = wbm.load(os.path.join(game, "DATA", "MAPS", name))
        print("%s, as the event editor showed it:" % name)
        for l in lines[1:]:
            print("  " + l)
        check(describe(m["events"]) == lines[1:], "the event editor holds the file's %d events" % len(lines[1:]))
        log_path = os.path.join(game, "WarBreedsHD.log")
        log = open(log_path, encoding="latin-1").read() if os.path.exists(log_path) else ""
        check("after Esc: event editor closed, events dirty 0" in log, "Esc closed it; looking changed nothing")
        check("reopened: 2 events, dirty 0" in log, "it reopened with the same events")
        check("Exit left the editor at once" in log, "Exit did not ask (no unsaved changes)")
        print("ALL PASS" if ok else "SOME CHECKS FAILED")
        return 0 if ok else 1

    lines = open(os.path.join(game, "WarBreedsHD-events-expect.txt")).read().splitlines()
    head = dict(l.split(" ", 1) for l in lines if l.split(" ", 1)[0] in ("file", "base", "first", "second"))
    held = [l for l in lines if l.startswith("E")]
    ai_held = {}
    for l in lines:
        if l.startswith("ai slot "):
            # "ai slot 1: squad 1.00 base 0.25 scouting 0.00 influence 0.50"
            f = l.replace(":", "").split()
            ai_held[int(f[2])] = {f[i]: float(f[i + 1]) for i in range(3, len(f), 2)}

    path = os.path.join(game, "DATA", "MAPS", head["file"])
    data = open(path, "rb").read()
    m = wbm.parse(data)
    base = wbm.load(os.path.join(game, "DATA", "MAPS", head["base"]))
    print("%s: %d bytes, %d events, %d computer players" % (head["file"], len(data), len(m["events"]["events"]),
                                                            len(m["ai"])))
    check(wbm.dumps(m) == data, "the saved map re-serialises byte for byte")

    saved = describe(m["events"])
    for l in saved:
        print("  " + l)
    check(saved == held, "the file's events are the editor's (%d)" % len(held))
    check(saved == SCRIPT, "they are the events the test built through the event editor")
    check(m["events"]["f14"] == [0] * 8, "no trigger flags set")
    ev = m["events"]["events"]
    if len(ev) == 2:
        check(ev[1]["actions"][0]["ST"].startswith(b"hd event fired\0"), "the CHAT text is NUL-terminated")
        check(all(a["type"] in wbm.ACT_SPEC for e in ev for a in e["actions"]), "every action type is known")

    # Global AI settings.
    first, second = int(head["first"]), int(head["second"])
    want_changed = {(first, "squad"): 1.0, (first, "scouting"): 0.0, (second, "base"): 0.0}
    bad = []
    by_slot = {a["slot"]: a for a in m["ai"]}
    base_slot = {a["slot"]: a for a in base["ai"]}
    for slot, a in by_slot.items():
        if a["marker"] != 0x0D:
            bad.append("slot %d has no AI state" % slot)
            continue
        for k in ("squad", "base", "scouting", "influence"):
            v = a[k]
            if abs(v - ai_held.get(slot, {}).get(k, -1)) > 1e-6:
                bad.append("slot %d %s %.2f, the editor held %.2f" % (slot, k, v, ai_held.get(slot, {}).get(k, -1)))
            want = want_changed.get((slot, k))
            if want is None and slot in base_slot and base_slot[slot]["marker"] == 0x0D:
                want = base_slot[slot][k]
            if want is not None and abs(v - want) > 1e-6:
                bad.append("slot %d %s %.2f, expected %.2f" % (slot, k, v, want))
        print("  AI slot %d: squad %.2f base %.2f scouting %.2f influence %.2f" % (
            slot, a["squad"], a["base"], a["scouting"], a["influence"]))
    check(not bad and len(by_slot) == len(ai_held),
          "the AI settings are the editor's: slot %d squad 5 and scouting 1, slot %d base 1, the rest unchanged%s" % (
              first, second, "" if not bad else " (" + "; ".join(bad) + ")"))

    # The Test Map skirmish keeps the custom map's AI settings (skirmish.cpp).
    log_path = os.path.join(game, "WarBreedsHD.log")
    kept = {}
    if os.path.exists(log_path):
        for line in open(log_path, encoding="latin-1").read().splitlines():
            mk = re.search(r"skirmish: player (\d+) keeps the map's AI: squad ([\d.]+), base ([\d.]+), "
                           r"scouting ([\d.]+), influence ([\d.]+)", line)
            if mk:
                kept[int(mk.group(1))] = [float(x) for x in mk.groups()[1:]]
    wrong = [s for s, v in kept.items() if s not in by_slot or any(
        abs(v[i] - by_slot[s][k]) > 0.005 for i, k in enumerate(("squad", "base", "scouting", "influence")))]
    check(kept and not wrong and first in kept and second in kept,
          "the skirmish kept the map's AI settings for its computer players (%s)%s" % (
              ", ".join("player %d" % s for s in sorted(kept)), "" if not wrong else "; differ: %s" % wrong))

    # Nothing else edited.
    check(m["terrain"] == base["terrain"], "the terrain is the original's")
    types = sorted(o["type"] for o in m["world"]["objects"] if o)
    check(types == sorted(o["type"] for o in base["world"]["objects"] if o), "the things are the original's")

    log_path = os.path.join(game, "WarBreedsHD.log")
    if os.path.exists(log_path):
        log = open(log_path, encoding="latin-1").read().splitlines()
        fired = [l for l in log if "autopilot: skirmish: event fired" in l]
        for l in fired:
            print("  " + l.split("autopilot: ")[-1])
        check(any("(2 -> 1 events)" in l for l in fired) and any("(1 -> 0 events)" in l for l in fired),
              "in the Test Map skirmish both events fired")
        check(any("OK DIALOG shown" in l for l in log), "event 1's OK DIALOG appeared")
        local = [l for l in log if "skirmish player slot" in l and "(local)" in l]
        if local:
            print("  " + local[-1].split("autopilot: ")[-1])
        check(any("the game ended" in l and "SReward" in l for l in log),
              "event 2's PLAYER END (victory for colour 231) ended the skirmish")
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
