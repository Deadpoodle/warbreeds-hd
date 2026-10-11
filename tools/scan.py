"""Analyse WarBreedsHD-scan.bin (see src/scan.cpp) to find variables that move
the way the camera does while the player scrolls right, down, left, up.

Usage: scan.py <scan.bin> [--top N] [--addr 0x...]
"""
import struct
import sys
from collections import defaultdict

import numpy as np

path = sys.argv[1]
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 40
only = int(sys.argv[sys.argv.index("--addr") + 1], 16) if "--addr" in sys.argv else None

snaps = []  # list of (time, {base: bytes})
with open(path, "rb") as f:
    while True:
        h = f.read(12)
        if len(h) < 12:
            break
        magic, t, n = struct.unpack("<3I", h)
        assert magic == 0x50414E53, hex(magic)
        regions = {}
        for _ in range(n):
            base, size = struct.unpack("<2I", f.read(8))
            regions[base] = f.read(size)
        snaps.append((t, regions))
if "--until" in sys.argv:
    limit = float(sys.argv[sys.argv.index("--until") + 1]) * 1000
    snaps = [s for s in snaps if s[0] - snaps[0][0] <= limit]
print(f"{len(snaps)} snapshots over {(snaps[-1][0] - snaps[0][0]) / 1000:.1f}s")

# Only regions present (same base and size) in every snapshot.
common = set(snaps[0][1])
for _, r in snaps[1:]:
    common &= {b for b in r if len(r[b]) == len(snaps[0][1].get(b, b""))}

times = np.array([t for t, _ in snaps]) - snaps[0][0]
cands = []
for base in sorted(common):
    arr = np.stack([np.frombuffer(r[base], dtype="<i2") for _, r in snaps])  # [T, N]
    for kind, view in (("i16", arr), ("i32", arr.view("<i4") if arr.shape[1] % 2 == 0 else None)):
        if view is None:
            continue
        step = 2 if kind == "i16" else 4
        d = np.diff(view.astype(np.int64), axis=0)
        moving = (d != 0).sum(axis=0)
        rng = view.max(axis=0).astype(np.int64) - view.min(axis=0)
        maxstep = np.abs(d).max(axis=0)
        ok = (moving >= 4) & (moving <= 0.8 * len(d)) & (rng >= 64) & (rng <= 20000) & (maxstep <= 400)
        for i in np.nonzero(ok)[0]:
            addr = base + i * step
            if only is not None and addr != only:
                continue
            s = view[:, i].astype(np.int64)
            distinct = len(np.unique(s))
            sign = np.sign(d[:, i])
            nz = sign[sign != 0]
            reversals = int((nz[1:] != nz[:-1]).sum()) if len(nz) > 1 else 0
            # Camera-like: many distinct positions, few direction reversals.
            if distinct >= 5 and reversals <= 4:
                cands.append((distinct - 3 * reversals, addr, kind, s))

cands.sort(key=lambda c: -c[0])
for runs, addr, kind, s in cands[:top]:
    idx = np.linspace(0, len(s) - 1, min(24, len(s))).astype(int)
    print(f"{addr:#010x} {kind} run={runs:3d}  " + " ".join(str(v) for v in s[idx]))
