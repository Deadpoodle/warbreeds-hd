"""Experiment: put WxH into the 5th resolution slot (originally 1280x1024) of a
test copy of WARBREED.EXE, select it in prefs.dbg, and let DDrawCompat expose it.

Usage: exp_slot5.py <game dir> <width> <height>
"""
import os
import struct
import sys

game, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
exe = os.path.join(game, "WARBREED.EXE")
assert "testgame" in os.path.abspath(game).lower(), "refusing to touch a non-test install"

buf = bytearray(open(exe, "rb").read())
# 004921b2  68 00 04 00 00  push 0x400   (height)
# 004921b7  68 00 05 00 00  push 0x500   (width)
TEXT_DELTA = 0x400000 + 0x1000 - 0x400
for va, val in ((0x4921b2, h), (0x4921b7, w)):
    off = va - TEXT_DELTA
    assert buf[off] == 0x68, hex(buf[off])
    buf[off + 1:off + 5] = struct.pack("<I", val)
open(exe, "wb").write(buf)

prefs = os.path.join(game, "DATA", "prefs.dbg")
p = bytearray(open(prefs, "rb").read())
assert p[:4] == b"MHWK"
p[0x24:0x28] = struct.pack("<HH", w, h)
open(prefs, "wb").write(p)

with open(os.path.join(game, "DDrawCompat-WARBREED.ini"), "w") as f:
    f.write(f"SupportedResolutions = native, 640x480, 800x600, 1024x768, {w}x{h}\n")
print(f"patched slot 5 -> {w}x{h}, prefs -> {w}x{h}")
