"""Check a pathing test run (autopilot mode 26, [Debug] PathTest) from a test copy's log.

Usage: python tools/path_check.py <test dir>            the run's PASS/FAIL lines
       python tools/path_check.py <dir A> <dir B>        also: both runs' state hashes match
The scenarios (pathing.cpp, TestTurn) log their own `pathing: test N PASS|FAIL ...`
lines and any `pathing: MISMATCH ...` (the grid and the game's collision disagree),
which always fails.
"""
import re
import sys


def read(d):
    return open(d + '/WarBreedsHD.log', encoding='latin-1').read()


ok = True


def check(cond, what):
    global ok
    print(('PASS ' if cond else 'FAIL ') + what)
    ok &= bool(cond)


logs = [read(d) for d in sys.argv[1:]]
for d, log in zip(sys.argv[1:], logs):
    results = re.findall(r'pathing: test \d+ (PASS|FAIL) ?(.*)', log)
    check(results, '%s: the test logged a result' % d)
    for r, what in results:
        check(r == 'PASS', '%s: %s' % (d, what))
    mismatches = re.findall(r'pathing: MISMATCH .*', log)
    check(not mismatches, '%s: grid agrees with the game (%d mismatches)' % (d, len(mismatches)))
    for m in mismatches[:5]:
        print('   ', m)

if len(logs) == 2:
    a, b = (dict(re.findall(r'pathing: hash t=(\d+) ([0-9a-f]+)', log)) for log in logs)
    common = sorted(set(a) & set(b), key=int)
    check(len(common) >= 5, 'enough common hash samples (%d)' % len(common))
    first = next((t for t in common if a[t] != b[t]), None)
    check(first is None, 'state hashes match' + ('' if first is None else ' (first difference at turn %s)' % first))

sys.exit(0 if ok else 1)
