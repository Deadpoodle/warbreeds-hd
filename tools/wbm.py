"""Read and write WarBreeds map files (.WBM).

A .WBM file is the game's own save-game archive (binary mode, little endian,
no compression, no chunk directory) written by the staged saver: map header,
terrain and pathing, players, effects, world objects, triggers, AI state,
camera bounds and routes. See re/wbm-format.md for the full specification.

Usage:
  wbm.py --check [files...]        parse and rewrite every map, compare byte for byte
                                   (default: all shipped maps)
  wbm.py --dump <file.WBM>         readable summary
  wbm.py --png <file.WBM> [out.png] [--scale N] [--no-objects]
                                   render the terrain with the game's tile art
                                   (default out: build/<name>.png, scale 0.25)
  wbm.py --json <file.WBM>         print the parsed structure as JSON (bytes as hex)

As a module:
  m = wbm.load(path)      # dict, see parse()
  data = wbm.dumps(m)     # bytes
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "testgame")

# ---------------------------------------------------------------------------
# Names

RACES = ["Tanu", "Sen-Soth", "Kelika", "Magha", "race 4"]

# Thing types (World object types). Art names are the 20-byte table at 0xa1e in
# GAMEPLAY.DAT (art file DATA\<NAME>.MHK); UI names from the game's string table.
TYPE_ART = [
    "tanu1", "tanu2", "tanu3", "tanu4", "tanu5", "tanux1", "tanux2", "tanux3", "tanux4",
    "sen1", "sen2", "sen3", "sen4", "sen5", "sen6", "senx2", "senx3", "senx4",
    "kel1", "kel2", "kel3", "kel4", "kel5", "kel6", "kelx2", "kelx3", "kelx4",
    "mag1", "mag2", "mag3", "mag4", "mag5", "magx2", "magx3", "magx4",
    "yedda", "dhuer", "crx2", "crx3", "crx4", "crx5", "crx6", "crx7", "crx8",
    "refinery", "genelab", "relay", "battery", "mine", "turret", "extower", "obstower",
    "rvtlizer", "advlab", "advrelay", "advturrt", "advbatry", "warp", "howitzer", "collect",
    "devx2", "devx3", "devx4", "pod", "spore", "weed", "corpse",
    "ruins", "ctree", "crock", "stree", "srock", "ttree", "trock",
]
TYPE_NAMES = {
    0: "Tanu shaman", 1: "scout", 2: "raptor", 3: "T-Rex", 4: "Tanu dragon",
    9: "Sen-Soth shaman", 10: "flea", 11: "myrmidon", 12: "beetle", 13: "dragonfly",
    18: "Kelika shaman", 19: "tribble", 20: "sasquatch", 21: "behemoth", 22: "leviathan",
    27: "Magha shaman", 28: "shadow", 29: "dervish", 30: "heavy", 31: "wombat",
    35: "Yedda", 36: "dhuer",
    0x2C: "refinery", 0x2D: "gene lab", 0x2E: "relay", 0x2F: "battery (psi tower)",
    0x30: "mine", 0x31: "turret", 0x32: "electric tower", 0x33: "observation tower (listener)",
    0x34: "revitalizer (healer)", 0x35: "advanced gene lab", 0x36: "advanced relay",
    0x37: "advanced turret", 0x38: "advanced battery", 0x39: "warp", 0x3A: "howitzer (summoner)",
    0x3B: "collector", 0x3F: "pod", 0x40: "spore", 0x41: "weed", 0x42: "corpse",
    0x43: "ruins", 0x44: "ctree", 0x45: "crock", 0x46: "stree", 0x47: "srock",
    0x48: "ttree", 0x49: "trock",
}
EMPTY_SLOT = 0x4A          # "A" value of an unused slot in the object table
NO_ID = 0x0C01             # "no object" id used by many fields

COND_NAMES = ["Timed", "Position", "PlayerDeath", "PlayerCGK", "PlayerDevices", "PlayerSeen",
              "Trigger", "Rect", "ThingMissing"]
ACT_NAMES = ["AddCreature", "Effect", "GameClock", "Audio", "CDAudio", "Capture", "Warp", "Quake",
             "VeteranStatus", "Heal", "Trigger", "PlayerEnd", "Message", "Script", "Remove",
             "SetGizmo", "SetAggression", "SetBounds", "SetImportance", "SetSquadChanging",
             "CopyTerrain", "OKDialog", "MoveIntoView", "RevealMap", "Move2Point", "Warp2Point",
             "SetMobility", "CGK"]


def type_name(t):
    if t in TYPE_NAMES:
        return TYPE_NAMES[t]
    if 0 <= t < len(TYPE_ART):
        return TYPE_ART[t]
    return "type %d" % t


# ---------------------------------------------------------------------------
# Symmetric serializer: the same function reads (dict filled from bytes) or
# writes (bytes produced from the dict), like the engine's own Serialize().

class IO:
    def __init__(self, data=None):
        self.reading = data is not None
        self.d = data
        self.p = 0
        self.out = bytearray()

    def _v(self, fmt, size, o, k):
        if self.reading:
            if self.p + size > len(self.d):
                raise ValueError("truncated at %d reading %r" % (self.p, k))
            v = struct.unpack_from(fmt, self.d, self.p)[0]
            self.p += size
            o[k] = v
            return v
        v = o[k]
        self.out += struct.pack(fmt, v)
        return v

    def u8(self, o, k):
        return self._v("<B", 1, o, k)

    def u16(self, o, k):
        return self._v("<H", 2, o, k)

    def u32(self, o, k):
        return self._v("<I", 4, o, k)

    def raw(self, o, k, n):
        if self.reading:
            if self.p + n > len(self.d):
                raise ValueError("truncated at %d reading %r" % (self.p, k))
            o[k] = bytes(self.d[self.p:self.p + n])
            self.p += n
            return o[k]
        v = o[k]
        if len(v) != n:
            raise ValueError("%r must be %d bytes" % (k, n))
        self.out += v
        return v

    def pt(self, o, k):
        """Engine Pt {vtbl; short x; short y}: two u16 (x, y)."""
        if self.reading:
            t = {}
            self.u16(t, "x")
            self.u16(t, "y")
            o[k] = (t["x"], t["y"])
        else:
            x, y = o[k]
            self.out += struct.pack("<HH", x, y)
        return o[k]

    def fields(self, o, spec):
        """spec: list of (name, size) with size 1/2/4 = uint, 'pt' = point, n>4 = raw bytes."""
        for name, size in spec:
            if size == "pt":
                self.pt(o, name)
            elif size == 1:
                self.u8(o, name)
            elif size == 2:
                self.u16(o, name)
            elif size == 4:
                self.u32(o, name)
            else:
                self.raw(o, name, size)

    def count(self, o, k, fmt, n_reading=None):
        """Write len(o[k]) or read a count; returns the count."""
        size = struct.calcsize(fmt)
        if self.reading:
            n = struct.unpack_from(fmt, self.d, self.p)[0]
            self.p += size
            return n
        n = len(o[k])
        self.out += struct.pack(fmt, n)
        return n

    def items(self, o, k, n, fn):
        """Serialize n items of list o[k] with fn(io, item_dict)."""
        if self.reading:
            lst = []
            for _ in range(n):
                it = {}
                fn(self, it)
                lst.append(it)
            o[k] = lst
        else:
            for it in o[k]:
                fn(self, it)
        return o[k]

    def u16_array(self, o, k, n):
        if self.reading:
            o[k] = list(struct.unpack_from("<%dH" % n, self.d, self.p))
            self.p += 2 * n
        else:
            if len(o[k]) != n:
                raise ValueError("%r must have %d entries" % (k, n))
            self.out += struct.pack("<%dH" % n, *o[k])
        return o[k]

    def u32_array(self, o, k, n):
        if self.reading:
            o[k] = list(struct.unpack_from("<%dI" % n, self.d, self.p))
            self.p += 4 * n
        else:
            if len(o[k]) != n:
                raise ValueError("%r must have %d entries" % (k, n))
            self.out += struct.pack("<%dI" % n, *o[k])
        return o[k]

    def ptlist(self, o, k):
        """u32 count, then count points (engine IList<Pt>, 0x485a10 / inline copies)."""
        n = self.count(o, k, "<I")
        if self.reading:
            o[k] = [struct.unpack_from("<HH", self.d, self.p + 4 * i) for i in range(n)]
            self.p += 4 * n
        else:
            for x, y in o[k]:
                self.out += struct.pack("<HH", x, y)
        return o[k]


# ---------------------------------------------------------------------------
# Section "?" map header (0x432330, MapInfo at 0x567578)

def ser_header(io, h):
    io.u16(h, "version")            # "8"    must be 5
    io.u16(h, "players_max")        # "NPSP"
    io.u16(h, "players_rec")        # "NPRC"
    io.u16(h, "width")              # "MPWD" 64 px cells = tiles / 2
    io.u16(h, "height")             # "MPHT"
    if io.reading:
        n = io.u16(h, "_namelen")   # "NMLN" includes the NUL
        raw = io.raw(h, "_name", n)
        del h["_namelen"], h["_name"]
        if raw.endswith(b"\0") and b"\0" not in raw[:-1]:
            h["name"] = raw[:-1].decode("latin-1")
        else:
            h["name_raw"] = raw
    else:
        raw = h["name_raw"] if "name_raw" in h else h["name"].encode("latin-1") + b"\0"
        io.out += struct.pack("<H", len(raw)) + raw   # "OBJC" one byte per char


# ---------------------------------------------------------------------------
# Section "+" terrain, world state and pathing (World 0x55dd60, 0x42e080)

def ser_terrain(io, t):
    io.u8(t, "width_tiles")         # "4" World+0x9054 (0x566db4), 32 px tiles
    io.u8(t, "height_tiles")        # "5" World+0x9055 (0x566db5)
    # "6": 96 x 96 u16 tile ids at World+8, stored column by column (x outer).
    if io.reading:
        flat = struct.unpack_from("<9216H", io.d, io.p)
        io.p += 2 * 9216
        t["cells"] = [[flat[x * 96 + y] for x in range(96)] for y in range(96)]
    else:
        c = t["cells"]
        io.out += struct.pack("<9216H", *[c[y][x] for x in range(96) for y in range(96)])


def ser_world_state(io, w):
    io.u16(w, "quake_force")        # "GQF"  World+0x904a  explosion energy this tick
    io.u8(w, "f9056")               # "GSS"  World+0x9056  (unknown, 0)
    io.u32(w, "ticks")              # "GSC"  World+0x9058  world tick counter
    io.u8(w, "quake_step")          # "GQS"  World+0x93b0  screen-shake step
    io.u16(w, "clock_on")           # "GAGC" a mission clock exists (World+0x93b4)
    io.u32(w, "clock_count")        # "GGCC" clock value
    io.u16(w, "clock_f0c")          # "GGCST"
    io.u8(w, "clock_f0e")           # "GGCTI"


def ser_region(io, r):
    io.u16(r, "id")                 # region +4
    io.pt(r, "centre")              # +0xc/+0xe world px
    io.pt(r, "f14")                 # +0x14/+0x16 (values 1 / 0xffff)
    n = io.count(r, "links", "<B")
    if io.reading:
        r["links"] = [struct.unpack_from("<HH", io.d, io.p + 4 * i) for i in range(n)]
        io.p += 4 * n
    else:
        for a, b in r["links"]:
            io.out += struct.pack("<HH", a, b)


def ser_pathing(io, p, w_tiles, h_tiles):
    # "7" -> AITerrainAnalysis 0x4a7da0: "<" format, ">" AIMovementGuide 0x4a4670.
    fmt = io.u16(p, "format")
    if fmt != 4:
        return                      # the game reprocesses the map (dialog) and reads nothing more
    io.u16_array(p, "guide", w_tiles * h_tiles)     # region id per tile, row major, 0xffff = none
    n = io.count(p, "regions", "<I")                # regions table 0x592348
    io.items(p, "regions", n, ser_region)


# ---------------------------------------------------------------------------
# Section "=" players, part 1 (APlayerRegistry 0x43ae20, APlayer 0x438720)

def ser_player(io, s):
    if io.u16(s, "race") >= 5:      # "F" (+0x4878); 5 = empty slot
        return
    io.u8(s, "color")               # "G" +0x484e palette index
    io.u16(s, "computer")           # "H" 1 = AIComputerPlayer
    io.u16(s, "index")              # "I" +4 slot number
    io.u8(s, "mask")                # "{" +0x18 1 << index
    io.u8(s, "allies")              # "J" +0x1a alliance bit mask
    io.u16_array(s, "cgk", 5)       # "_" +0x4850 per race level
    io.u16_array(s, "xu", 5)        # "XU" +0x4864
    io.u16(s, "f48f6")              # "}"
    io.pt(s, "start")               # ":" +0x4a1c start position, 64 px cells
    io.u16(s, "fog")                # "a" +0x484c fog of war on
    io.u8(s, "f4a31")               # "dc"
    io.u16_array(s, "f486e", 5)     # "b"
    # "c": 96 x 96 explored flags (+0x2426), column by column; 1 = unexplored.
    io.raw(s, "explored", 9216)
    io.u16_array(s, "stats", 24)    # "d" 3 x 8 counters (+0x4888, stored as u32)
    io.u16_array(s, "f48e8", 3)     # "km"
    io.raw(s, "name", 32)           # "e" +0x48f8 char[32]
    io.u8(s, "f19")                 # "PMSB"
    io.u16_array(s, "races", 5)     # "PCBCF" +0x1c, 1 at own race
    io.u16_array(s, "f4882", 2)     # "PEMM"
    io.u16(s, "f4a44")              # "MPTY"


def ser_players(io, pl):
    io.u16(pl, "f2a")               # "PRHO"
    io.u16(pl, "f2c")               # "PRFO"
    io.items(pl, "slots", 8, ser_player)
    io.u8(pl, "count")              # ";" +0x24


# ---------------------------------------------------------------------------
# Section "FX" effects (AEffectRegistry 0x425160 -> 0x425430, 0x426080)

def ser_effect_a(io, e):
    t = io.u16(e, "kind")           # "YY"
    if 0 <= t < 0xAF and t != 0x8A:
        io.fields(e, [("zz", 2), ("pos", "pt"), ("bb", 2), ("cc", 2), ("dd", 1)])
        if io.u16(e, "moving"):     # "MOVN"
            io.pt(e, "target")      # "pP"


def ser_effect_b(io, e):
    io.fields(e, [("kind", 2), ("kk", 2), ("nn", 2), ("mm", 2), ("oo", 1), ("pos", "pt"), ("dd", 1)])


def ser_effects(io, fx):
    if io.reading:
        fx["layers"] = []
        fx["players"] = []
    for i in range(10):             # 5 layers x 2
        if io.reading:
            fx["layers"].append([])
        holder = {"l": fx["layers"][i]}
        n = io.count(holder, "l", "<H")
        io.items(holder, "l", n, ser_effect_a)
        fx["layers"][i] = holder["l"]
    for i in range(8):              # per player
        if io.reading:
            fx["players"].append([])
        holder = {"l": fx["players"][i]}
        n = io.count(holder, "l", "<H")
        io.items(holder, "l", n, ser_effect_b)
        fx["players"][i] = holder["l"]


# ---------------------------------------------------------------------------
# Section "!" world objects (AThingRegistry 0x450fa0, factory 0x447a90)

def ser_thing(io, o):               # AThing 0x446020
    io.pt(o, "pos")                 # +0x14/+0x16 centre, world px
    io.u16(o, "hp")                 # +0x24


def ser_device(io, o):              # ADevice 0x40e5c0
    ser_thing(io, o)
    io.fields(o, [("f2a", 2), ("f32", 2), ("built", 2), ("f28", 2), ("f2e", 2), ("f30", 1),
                  ("f38", 1), ("f3a", 2), ("power_from", 2), ("f3c", 1), ("group", 1)])


def dev(extra):
    def f(io, o):
        ser_device(io, o)
        io.fields(o, extra)
    return f


def ser_genelab(io, o):             # AGeneLab 0x429e60
    ser_device(io, o)
    io.ptlist(o, "points")
    io.fields(o, [("f1fc", 2), ("f1fe", 2), ("f200", 2), ("f202", 2), ("f204", 2), ("f206", 2),
                  ("f1e8", 2)])
    io.u16_array(o, "f1ec", 4)
    io.pt(o, "f218")


def ser_body(io, o):                # ACreatureBody 0x405400
    ser_thing(io, o)
    io.u16_array(o, "genes", 4)     # +0x54 (default 100)
    io.raw(o, "f64", 4)
    io.raw(o, "f68", 4)
    io.fields(o, [("f4c", 2), ("f2c", 2), ("f2e", 2), ("f30", 2), ("f38", "pt"), ("f3c", 4),
                  ("f40", 1), ("f48", 2), ("f4a", 2), ("f4e", 2), ("f50", 1), ("f52", 2),
                  ("f70", 2), ("f72", 1), ("f74", 2), ("f77", 1), ("f78", 1), ("f7c", 2),
                  ("f80", 2), ("f8c", 1), ("group", 1), ("f79", 1)])
    n = io.count(o, "f90", "<I")
    if io.reading:
        o["f90"] = [struct.unpack_from("<3H", io.d, io.p + 6 * i) for i in range(n)]
        io.p += 6 * n
    else:
        for a in o["f90"]:
            io.out += struct.pack("<3H", *a)
    io.fields(o, [("f88", 2), ("f44", 2), ("f32", 2), ("f76", 1)])


def ser_creature(io, o):            # ACreature 0x49aca0
    ser_body(io, o)
    io.fields(o, [("f138", "pt"), ("f13c", 2), ("f140", 2)])
    io.ptlist(o, "path")
    io.fields(o, [("fc0", 2), ("fc4", 4), ("fec", 2), ("f100", 2), ("f10c", 2), ("ffc", 2),
                  ("f104", "pt"), ("f14c", "pt"), ("f32b", 2), ("f44b", 1), ("fd8", 2), ("fbc", 1)])
    io.ptlist(o, "waypoints")       # +200


def ser_shaman(io, o):              # AShaman 0x4440e0
    ser_creature(io, o)
    io.pt(o, "f16c")
    io.ptlist(o, "build_list")
    io.fields(o, [("f17e", 2), ("f180", 2), ("f18e", 2), ("f198", 1), ("f184", 2), ("f170", 1),
                  ("f1b4", 2), ("f178", "pt"), ("f17c", 1), ("f154", 1)])


def ser_mushroom(io, o):            # AMushroom 0x4451d0 (pods, spores, weeds)
    ser_thing(io, o)
    io.fields(o, [("f28", 2), ("f2c", 1), ("f32", 1), ("f2d", 1), ("f30", 1), ("f31", 1)])


def ser_obstacle(io, o):            # AObstacle 0x434f10
    ser_thing(io, o)
    io.fields(o, [("state", 1), ("phase", 2)])


CLASSES = {}
for _t in range(0, 0x2C):
    CLASSES[_t] = ("ACreature", ser_creature)
for _t in (0, 9, 0x12, 0x1B):
    CLASSES[_t] = ("AShaman", ser_shaman)
CLASSES[0x2C] = ("ARefinery", dev([("f48", 2), ("f4a", 2)]))
CLASSES[0x2D] = CLASSES[0x35] = ("AGeneLab", ser_genelab)
CLASSES[0x2E] = CLASSES[0x36] = ("ARelay", ser_device)
CLASSES[0x2F] = CLASSES[0x38] = ("ABattery", ser_device)
CLASSES[0x30] = ("AMine", ser_device)
CLASSES[0x31] = CLASSES[0x37] = ("ATurret", dev([("f40", 2), ("f41", 1), ("f42", 2), ("f44", 2),
                                                 ("f46", 2)]))
CLASSES[0x32] = ("AElectricTower", dev([("f40", 1), ("f42", 2)]))
CLASSES[0x33] = ("ARadar", dev([("f40", 1), ("f41", 1), ("f42", 1), ("f43", 1)]))
CLASSES[0x34] = ("ARevitalizer", ser_device)
CLASSES[0x39] = ("AWarp", dev([("f40", 2), ("f42", 2), ("f44", 2), ("f46", 2), ("f48", 2)]))
CLASSES[0x3A] = ("AHowitzer", dev([("f48", 2), ("f4a", 2), ("f40", 2)]))
CLASSES[0x3B] = ("ACollector", ser_device)
for _t in (0x3C, 0x3D, 0x3E):
    CLASSES[_t] = ("ADevice", ser_device)
for _t in (0x3F, 0x40, 0x41):
    CLASSES[_t] = ("AMushroom", ser_mushroom)
CLASSES[0x42] = ("ADeath", ser_thing)
for _t in range(0x43, 0x4A):
    CLASSES[_t] = ("AObstacle", ser_obstacle)


def ser_object(io, holder):
    """One slot of the thing table. holder['o'] is None for an empty slot."""
    if io.reading:
        t = struct.unpack_from("<H", io.d, io.p)[0]
        io.p += 2
        if t == EMPTY_SLOT:
            holder["o"] = None
            return
        o = {"type": t}
        holder["o"] = o
    else:
        o = holder["o"]
        if o is None:
            io.out += struct.pack("<H", EMPTY_SLOT)
            return
        io.out += struct.pack("<H", o["type"])
    if o["type"] not in CLASSES:
        raise ValueError("unknown thing type %d" % o["type"])
    io.u16(o, "id")                 # "B" +0x1c
    io.u16(o, "owner")              # "C" player index, 9 = none
    io.u16(o, "x")                  # "D" +0x0a placement x (top-left), px
    io.u16(o, "y")                  # "E" +0x0c
    cls, fn = CLASSES[o["type"]]
    if io.reading:
        o["class"] = cls
    fn(io, o)


def ser_world(io, w):
    if io.reading:
        w["lists"] = []
    for g in range(8):              # World+0x8c, 8 lists of thing ids
        if io.reading:
            n = struct.unpack_from("<I", io.d, io.p)[0]
            io.p += 4
            w["lists"].append(list(struct.unpack_from("<%dI" % n, io.d, io.p)))
            io.p += 4 * n
        else:
            io.out += struct.pack("<I", len(w["lists"][g]))
            io.out += struct.pack("<%dI" % len(w["lists"][g]), *w["lists"][g])
    n = io.count(w, "objects", "<I")
    if io.reading:
        w["objects"] = []
        for _ in range(n):
            h = {}
            ser_object(io, h)
            w["objects"].append(h["o"])
    else:
        for o in w["objects"]:
            ser_object(io, {"o": o})


# ---------------------------------------------------------------------------
# Section "ER" triggers (AEventRegistry 0x41d940, AEvent 0x41d000)

COND_SPEC = {
    0: [("CC", 4)],
    1: [("CS", "pt"), ("CP", 1), ("Cs", 2)],
    2: [("CP", 1)],
    3: [("CP", 1), ("CK", 2), ("CA", 2)],
    4: [("CP", 1), ("Cs", 2), ("CN", 1)],
    5: [("CV", 1), ("CX", 1)],
    6: [("CI", 1)],
    7: [("CS", "pt"), ("CP", 1), ("Cs", 2), ("P2", "pt"), ("CN", 1)],
    8: [("CS", 2), ("CS2", 2)],
}
_CREATE = [("SP", 2), ("PL", 1), ("PO", "pt"), ("G1", 2), ("G2", 2), ("G3", 2), ("G4", 2), ("VI", 1)]
_BOUNDS = [("PO", "pt"), ("P2", "pt")]
ACT_SPEC = {
    0: _CREATE,
    1: [("FT", 2), ("LY", 2), ("FC", 2), ("PO", "pt"), ("CO", 1)],
    2: [("CL", 4), ("ST", 2), ("TI", 1)],
    3: [("RI", 2), ("PO", "pt")],
    4: [("TR", 1), ("TX", 1)],
    5: [("PL", 1), ("PD", 1)],
    6: _CREATE,
    7: [("QU", 1)],
    8: [("PO", "pt"), ("EX", 2)],
    9: [("PO", "pt"), ("PT", 2)],
    10: [("TI", 1)],
    11: [("PL", 1), ("VI", 2)],
    12: [("PL", 1), ("ST", 80)],
    13: [("SC", 80)],
    14: [("RM", 2), ("RM2", 2)],
    15: [("PO", "pt"), ("SG", 2)],
    16: [("PL", 1), ("AG", 1), ("AH", 1), ("AI", 1)],
    17: _BOUNDS,
    18: [("PL", 1), ("AG", 1), ("PO", "pt")],
    19: [("PL", 1), ("AJ", 2)],
    20: [("PO", "pt"), ("P2", "pt"), ("P3", "pt")],
    21: [("T1", 2), ("T2", 2), ("T3", 2)],
    22: [],
    23: _BOUNDS + [("PL", 1)],
    24: [("PO", "pt"), ("P2", "pt"), ("RM", 2)],
    25: [("WSCY", 4)],
    26: _BOUNDS + [("T1", 2)],
    27: [("CP", 1), ("CL", 2), ("CK", 2)],
}


def ser_condition(io, c):
    t = io.u8(c, "type")            # "CT"
    if t not in COND_SPEC:
        raise ValueError("unknown condition type %d" % t)
    if io.reading:
        c["name"] = COND_NAMES[t]
    io.fields(c, COND_SPEC[t])


def ser_action(io, a):
    t = io.u8(a, "type")            # "AT"
    if t not in ACT_SPEC:
        raise ValueError("unknown action type %d" % t)
    if io.reading:
        a["name"] = ACT_NAMES[t]
    io.u32(a, "AD")                 # AAction base 0x415050
    io.fields(a, ACT_SPEC[t])


def ser_event(io, e):
    io.u16(e, "AJ")                 # +0x20
    n = io.count(e, "conditions", "<H")     # "C0"
    io.items(e, "conditions", n, ser_condition)
    n = io.count(e, "actions", "<H")        # "E0"
    io.items(e, "actions", n, ser_action)


def ser_events(io, ev):
    io.u16_array(ev, "f14", 8)
    n = io.count(ev, "events", "<H")
    io.items(ev, "events", n, ser_event)


# ---------------------------------------------------------------------------
# Section "=" players, part 2: AIComputerPlayer state (0x4b7280); human players write nothing

GRID1_CELL = [("x", 2), ("y", 2), ("a", 4), ("b", 2), ("c", 4), ("d", 2), ("e", 4), ("f", 4), ("g", 4)]
SQUAD_SPEC = [("f0", 4), ("f1", 4), ("f2", 2), ("f3", 2), ("f4", 2), ("f5", 2), ("pts", 12),
              ("f6", 4), ("f7", 8), ("f8", 8), ("f9", 2), ("f10", 2), ("f11", 2), ("f12", 2),
              ("f13", 20), ("f14", 12), ("f15", 2), ("f16", 2), ("f17", "pt"), ("f18", 4)]


def ser_squad(io, s):               # 0x4ba950
    n = io.count(s, "members", "<I")
    io.u16_array(s, "members", n)
    io.fields(s, SQUAD_SPEC)


def ser_ai(io, a, w_tiles, h_tiles):
    if io.u16(a, "marker") != 0x0D:
        return
    gw, gh = (w_tiles + 7) // 8, (h_tiles + 7) // 8
    # AIRetina 0x4a9790: grid of 28-byte cells, then five grids of 52-byte cells
    if io.reading:
        a["grid_cells"] = [{} for _ in range(gw * gh)]
    for c in a["grid_cells"]:
        io.fields(c, GRID1_CELL)
    if io.reading:
        a["grids"] = []
        for _ in range(5):
            a["grids"].append(list(struct.unpack_from("<%dI" % (gw * gh * 13), io.d, io.p)))
            io.p += gw * gh * 52
    else:
        for g in a["grids"]:
            io.out += struct.pack("<%dI" % (gw * gh * 13), *g)
    io.u32_array(a, "u32a", 4)
    io.u16_array(a, "u16a", 2)
    io.u32_array(a, "u32b", 13)
    io.raw(a, "floats", 20)         # five float32
    io.u16_array(a, "u16x74", 74)
    io.u16(a, "f4e38")
    n = io.count(a, "targets", "<H")        # 0x4b5170 list of 9-byte entries
    io.items(a, "targets", n, lambda io2, t: io2.fields(t, [("a", 1), ("b", 2), ("c", 2), ("d", 4)]))
    io.u16(a, "f64")
    n = io.count(a, "squads", "<I")
    io.items(a, "squads", n, ser_squad)


# ---------------------------------------------------------------------------
# Stages 7-8: campaign, clock, camera bounds, routes (0x4812f0)

def ser_route(io, r):
    io.ptlist(r, "points")          # 0x485a10
    io.u16(r, "f10")
    io.u8(r, "f12")


def ser_tail(io, t):
    if io.u16(t, "campaign"):       # "CFLG"
        raise ValueError("campaign data (CFLG != 0) is not supported; no shipped map has it")
    io.u32(t, "clock")              # "CLOCK"
    io.u16(t, "x1")                 # "Z1" scroll bounds, world px
    io.u16(t, "x2")                 # "Z2"
    io.u16(t, "y1")                 # "Z3"
    io.u16(t, "y2")                 # "Z4"
    n = io.count(t, "routes", "<H")         # "PW"
    io.items(t, "routes", n, ser_route)


# ---------------------------------------------------------------------------

def serialize(io, m):
    if io.reading:
        for k in ("header", "terrain", "world_state", "pathing", "players", "effects", "world",
                  "events", "tail"):
            m[k] = {}
        m["ai"] = []
    ser_header(io, m["header"])
    if m["header"]["version"] != 5:
        raise ValueError("map version %d, expected 5" % m["header"]["version"])
    t = m["terrain"]
    ser_terrain(io, t)
    ser_world_state(io, m["world_state"])
    ser_pathing(io, m["pathing"], t["width_tiles"], t["height_tiles"])
    ser_players(io, m["players"])
    ser_effects(io, m["effects"])
    ser_world(io, m["world"])
    ser_events(io, m["events"])
    k = 0
    for s in m["players"]["slots"]:
        if s["race"] < 5 and s["computer"]:
            if io.reading:
                m["ai"].append({"slot": s["index"]})
            ser_ai(io, m["ai"][k], t["width_tiles"], t["height_tiles"])
            k += 1
    ser_tail(io, m["tail"])
    if io.reading and io.p != len(io.d):
        raise ValueError("%d trailing bytes" % (len(io.d) - io.p))
    return m


def parse(data):
    return serialize(IO(data), {})


def dumps(m):
    io = IO()
    serialize(io, m)
    return bytes(io.out)


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())


def save(m, path):
    with open(path, "wb") as f:
        f.write(dumps(m))


def shipped_maps():
    import glob
    return sorted(glob.glob(os.path.join(GAME, "DATA", "MAPS", "*.WBM"))) + \
        sorted(glob.glob(os.path.join(GAME, "CAMPAIGN", "*.WBM")))


# ---------------------------------------------------------------------------
# Terrain types (TERRAIN.HOO): 0 ground, 1 blocked, 2 pod soil, per 32 px tile

def load_hoo():
    for p in (os.path.join(GAME, "DATA", "TERRAIN.HOO"), os.path.join(ROOT, "wbedit", "EditData", "Terrain.hoo")):
        try:
            d = open(p, "rb").read()
        except OSError:
            continue
        n = struct.unpack_from("<I", d)[0]
        return [d[4 + 4 * i:8 + 4 * i] for i in range(n)]
    return []


def tile_type(m, hoo, tx, ty):
    """Terrain type of 32 px tile (tx, ty), like World 0x42ea40."""
    t = m["terrain"]
    if not (0 <= tx < t["width_tiles"] and 0 <= ty < t["height_tiles"]):
        return 1
    v = t["cells"][ty >> 1][tx >> 1]
    if v >= len(hoo):
        return 0
    return hoo[v][(ty & 1) * 2 + (tx & 1)]


# ---------------------------------------------------------------------------
# Commands

def cmd_check(paths):
    ok = 0
    for p in paths:
        d = open(p, "rb").read()
        try:
            m = parse(d)
            out = dumps(m)
        except Exception as e:
            print("FAIL %-14s %s" % (os.path.basename(p), e))
            continue
        if out == d:
            ok += 1
            print("ok   %-14s %8d bytes  %-26s objects %4d  events %2d" % (
                os.path.basename(p), len(d), m["header"].get("name", "?"),
                sum(1 for o in m["world"]["objects"] if o), len(m["events"]["events"])))
        else:
            i = next((i for i in range(min(len(d), len(out))) if d[i] != out[i]), min(len(d), len(out)))
            print("DIFF %-14s first difference at %d (sizes %d / %d)" % (os.path.basename(p), i, len(d), len(out)))
    print("%d/%d maps round-trip byte for byte" % (ok, len(paths)))
    return ok == len(paths)


def cmd_dump(path):
    from collections import Counter
    m = load(path)
    h, t = m["header"], m["terrain"]
    print("%s: \"%s\"" % (os.path.basename(path), h.get("name")))
    print("  size %d x %d cells of 64 px (%d x %d tiles of 32 px, %d x %d px)" % (
        h["width"], h["height"], t["width_tiles"], t["height_tiles"], t["width_tiles"] * 32, t["height_tiles"] * 32))
    print("  players: max %d, recommended %d, slots used %d" % (h["players_max"], h["players_rec"], m["players"]["count"]))
    shamans = {}
    for o in m["world"]["objects"]:
        if o and o["type"] in (0, 9, 0x12, 0x1B):
            shamans.setdefault(o["owner"], o)
    for s in m["players"]["slots"]:
        if s["race"] >= 5:
            continue
        sh = shamans.get(s["index"])
        print("  player %d: %-8s %-8s colour %3d  start cell %s%s" % (
            s["index"], RACES[s["race"]], "computer" if s["computer"] else "human", s["color"], s["start"],
            "  shaman at px %s" % (sh["pos"],) if sh else ""))
    used = Counter()
    hoo = load_hoo()
    if hoo:
        for ty in range(t["height_tiles"]):
            for tx in range(t["width_tiles"]):
                used[tile_type(m, hoo, tx, ty)] += 1
        print("  terrain types (32 px tiles): ground %d, blocked %d, pod soil %d" % (used[0], used[1], used[2]))
    objs = [o for o in m["world"]["objects"] if o]
    print("  thing table: %d slots, %d objects" % (len(m["world"]["objects"]), len(objs)))
    by = Counter((o["type"]) for o in objs)
    for typ, n in sorted(by.items()):
        own = Counter(o["owner"] for o in objs if o["type"] == typ)
        print("    %4d x %-28s (type 0x%02x)  owners %s" % (n, type_name(typ), typ, dict(sorted(own.items()))))
    p = m["pathing"]
    if p["format"] == 4:
        print("  pathing: format 4, %d regions" % len(p["regions"]))
    else:
        print("  pathing: format %d (not processed; the game will reprocess)" % p["format"])
    ev = m["events"]["events"]
    print("  triggers: %d" % len(ev))
    for i, e in enumerate(ev):
        cs = ", ".join(c["name"] for c in e["conditions"])
        acts = []
        for a in e["actions"]:
            s = a["name"]
            for k in ("ST", "SC"):
                if k in a:
                    s += ' "%s"' % a[k].split(b"\0")[0].decode("latin-1")
            acts.append(s)
        print("    %2d: if %s -> %s" % (i, cs or "-", "; ".join(acts)))
    for a in m["ai"]:
        print("  AI state for player %d: %s, %d squads" % (a["slot"], "present" if a["marker"] == 0x0D else "none",
                                                          len(a.get("squads", []))))
    tl = m["tail"]
    print("  scroll bounds px (%d,%d)-(%d,%d), clock %d, routes %d" % (tl["x1"], tl["y1"], tl["x2"], tl["y2"],
                                                                     tl["clock"], len(tl["routes"])))


def terrain_art():
    """(frames, palette) from TERRAIN.MHK: tile id = frame index of tBMH 9000."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mhkimg
    for p in (os.path.join(GAME, "DATA", "TERRAIN.MHK"), os.path.join(ROOT, "wbedit", "EditData", "Terrain.MHK")):
        try:
            res = mhkimg.parse(p)
        except OSError:
            continue
        pal = mhkimg.palette(res["tPAL"][0][1])
        return list(mhkimg.images(res["tBMH"][0][1])), pal
    raise SystemExit("TERRAIN.MHK not found (or locked by a running game)")


PLAYER_RGB = [(255, 64, 64), (64, 128, 255), (64, 220, 64), (255, 220, 0), (255, 128, 0),
              (200, 64, 255), (0, 230, 230), (255, 255, 255)]


def cmd_png(path, out, scale, objects=True):
    from PIL import Image, ImageDraw
    m = load(path)
    frames, pal = terrain_art()
    flat = [c for rgb in pal for c in rgb]
    t = m["terrain"]
    W, H = (t["width_tiles"] + 1) // 2, (t["height_tiles"] + 1) // 2
    tiles = {}
    im = Image.new("RGB", (W * 64, H * 64))
    for y in range(H):
        for x in range(W):
            v = t["cells"][y][x]
            if v not in tiles:
                if v < len(frames) and frames[v][0] == 64 and frames[v][1] == 64:
                    ti = Image.frombytes("P", (64, 64), frames[v][3])
                    ti.putpalette(flat)
                    tiles[v] = ti.convert("RGB")
                else:
                    tiles[v] = Image.new("RGB", (64, 64), (255, 0, 255))
            im.paste(tiles[v], (x * 64, y * 64))
    im = im.crop((0, 0, t["width_tiles"] * 32, t["height_tiles"] * 32))
    if scale != 1:
        im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)
    if objects:
        # Markers: obstacles dark grey dots, pods/spores/weeds lilac dots (white ring if
        # owned), creatures filled circles and devices squares in the owner's colour,
        # player start cells large rings.
        dr = ImageDraw.Draw(im)
        r = max(2, round(10 * scale))
        for o in m["world"]["objects"]:
            if not o:
                continue
            x, y = o["pos"][0] * scale, o["pos"][1] * scale
            typ = o["type"]
            col = PLAYER_RGB[o["owner"]] if o["owner"] < 8 else (160, 160, 160)
            if typ >= 0x42:
                dr.ellipse((x - r / 2, y - r / 2, x + r / 2, y + r / 2), fill=(70, 70, 70))
            elif typ >= 0x3F:
                dr.ellipse((x - r / 2, y - r / 2, x + r / 2, y + r / 2), fill=(230, 160, 255),
                           outline=col if o["owner"] < 8 else None)
            elif typ >= 0x2C:
                dr.rectangle((x - r, y - r, x + r, y + r), fill=col, outline=(0, 0, 0))
            else:
                dr.ellipse((x - r, y - r, x + r, y + r), fill=col, outline=(0, 0, 0))
        for s in m["players"]["slots"]:
            if s["race"] < 5:
                cx, cy = (s["start"][0] * 64 + 32) * scale, (s["start"][1] * 64 + 32) * scale
                col = PLAYER_RGB[s["index"] % 8]
                dr.ellipse((cx - 4 * r, cy - 4 * r, cx + 4 * r, cy + 4 * r), outline=col, width=max(2, r // 2))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    im.save(out)
    print("wrote %s (%dx%d)" % (out, im.width, im.height))


def to_jsonable(x):
    if isinstance(x, bytes):
        return x.hex()
    if isinstance(x, dict):
        return {k: to_jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [to_jsonable(v) for v in x]
    return x


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd, args = argv[0], argv[1:]
    if cmd == "--check":
        return 0 if cmd_check(args or shipped_maps()) else 1
    if cmd == "--dump":
        for p in args:
            cmd_dump(p)
        return 0
    if cmd == "--json":
        print(json.dumps(to_jsonable(load(args[0])), indent=1))
        return 0
    if cmd == "--png":
        scale = 0.25
        objects = "--no-objects" not in args
        if "--scale" in args:
            i = args.index("--scale")
            scale = float(args[i + 1])
            del args[i:i + 2]
        args = [a for a in args if a != "--no-objects"]
        out = args[1] if len(args) > 1 else os.path.join(
            ROOT, "build", os.path.splitext(os.path.basename(args[0]))[0] + ".png")
        cmd_png(args[0], out, scale, objects)
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
