"""Check a campaign progress run (autopilot mode 22) from the test copy's log.

Usage: python tools/campaign_check.py <test dir> <target>
  0  fresh Tanu: mission 1 recorded, won, mission 2 recorded
  1  Continue: the briefing opens at mission 2 (run after target 0)
  2  Start Over: the briefing opens at mission 1 (run after target 0 or 1)
  3  a broken record: Continue falls back to mission 1
"""
import configparser
import re
import sys

d, target = sys.argv[1], int(sys.argv[2])
log = open(d + '/WarBreedsHD.log', encoding='latin-1').read()
ini = configparser.ConfigParser()
ini.read(d + '/WarBreedsHD-progress.ini')
ok = True


def check(cond, what):
    global ok
    print(('PASS ' if cond else 'FAIL ') + what)
    ok &= bool(cond)


briefs = re.findall(r'autopilot: briefing \d: campaign (\d+) chapter (\d+) set (\d+)', log)
mission = ini.getint('Tanu', 'Mission', fallback=0)
if target == 0:
    check('mission 1 of 12) recorded' in log, 'the first briefing recorded mission 1 of 12')
    check(len(briefs) == 2 and briefs[1] == ('1000', '0', '1'), 'after a win, the next briefing is mission 2')
    check(mission == 2, 'the record says mission 2 (%d)' % mission)
else:
    check('has progress' in log, 'clicking Tanu asks first')
    check(len(briefs) == 1, 'one briefing opened')
    want = ('1000', '0', '1') if target == 1 else ('1000', '0', '0')
    check(briefs and briefs[0] == want, 'the briefing is mission %s (%s)' % (2 if target == 1 else 1, briefs[:1]))
    check(mission == (2 if target == 1 else 1), 'the record says mission %d (%d)' % (2 if target == 1 else 1, mission))
    if target == 3:
        check("doesn't fit the campaign" in log, 'the broken record fell back to starting over')

print('ALL PASS' if ok else 'FAILED')
sys.exit(0 if ok else 1)
