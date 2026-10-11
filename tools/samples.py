"""Summarise prof sampler output: per phase, the hottest leaf modules and the
game functions (and call chains) the samples were in.
Usage: samples.py <samples.txt>... [--phase P]"""
import bisect
import collections
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
asm = os.path.join(here, "..", "re", "text.asm")

# Function starts: targets of direct calls in the disassembly.
starts = set()
for line in open(asm, encoding="utf-8", errors="replace"):
    m = re.search(r"\bcall 0x([0-9a-f]+)$", line.rstrip())
    if m:
        starts.add(int(m.group(1), 16))
starts = sorted(starts)


def func(addr):
    i = bisect.bisect_right(starts, addr) - 1
    return "%06x" % starts[i] if i >= 0 else "?"


argv = sys.argv[1:]
want = None
if "--phase" in argv:
    i = argv.index("--phase")
    want = argv[i + 1]
    del argv[i:i + 2]
args = argv
rows = []
for path in args:
    for line in open(path):
        p = line.split()
        if len(p) >= 7 and (want is None or p[0] == want):
            rows.append(p)
by_phase = collections.defaultdict(list)
for r in rows:
    by_phase[r[0]].append(r)
for phase, rs in by_phase.items():
    print(f"== {phase}: {len(rs)} samples")
    leaf = collections.Counter()
    chain = collections.Counter()
    for r in rs:
        mod = r[2].split("+")[0]
        leaf[func(int(r[1], 16)) if mod.upper().startswith("WARBREED") else mod] += 1
        rets = [int(x, 16) for x in r[3:7] if int(x, 16)]
        chain[" < ".join(func(a) for a in rets[:3])] += 1
    print("  leaf:", ", ".join(f"{k} {v}" for k, v in leaf.most_common(8)))
    for k, v in chain.most_common(12):
        print(f"  {v:5d}  {k}")
