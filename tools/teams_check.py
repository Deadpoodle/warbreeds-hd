"""Check a skirmish teams run (autopilot mode 21) from the test copy's log.

Usage: python tools/teams_check.py <test dir> <target>
  0  2v2: teammates allied both ways and nobody else, picked colours kept,
     unique colours, teammates' starts nearest each other, watching after the
     human's defeat, then victory
  1  no teams: the stock alliances (every computer player allied, the human alone)
  2  everyone on one team: Start refused
  3  2v2, the ally defeated first: defeat at once
"""
import math
import re
import sys

d, target = sys.argv[1], int(sys.argv[2])
log = open(d + '/WarBreedsHD.log', encoding='latin-1').read()
ok = True


def check(cond, what):
    global ok
    print(('PASS ' if cond else 'FAIL ') + what)
    ok &= bool(cond)


players = {}
for m in re.finditer(r'teams: player (\d) \((human|AI)\) team (\d) allies (0x[0-9a-f]+|0) colour (0x[0-9a-f]+) start (-?\d+),(-?\d+)', log):
    players[int(m[1])] = dict(human=m[2] == 'human', team=int(m[3]), mask=int(m[4], 16), colour=m[5],
                              start=(int(m[6]), int(m[7])))

if target == 2:
    check('teams: Start refused' in log, 'Start refused with everyone on one team')
    check(not players, 'no game started')
else:
    check(len(players) == 4, '4 players in the game (%d)' % len(players))
    colours = [p['colour'] for p in players.values()]
    check(len(set(colours)) == len(colours), 'every player has a different colour')

if target in (0, 3):
    for i, p in players.items():
        mates = {j for j, q in players.items() if q['team'] == p['team']}
        want = sum(1 << j for j in mates)
        check(p['mask'] == want, 'player %d (team %d) allied with exactly its team (%#x)' % (i, p['team'], p['mask']))
    picked = re.findall(r'teams: slot (\d) colour (0x[0-9a-f]+) \(picked\)', log)
    check(len(picked) == 2, 'two picked colours reach the start (%d)' % len(picked))
    for slot, c in picked:
        if int(slot) in players:
            check(players[int(slot)]['colour'] == c, 'player %s has its picked colour %s' % (slot, c))
    check('colour automatic' in log, 'right-click put a colour back to automatic')
    for i, p in players.items():
        mate = [q for j, q in players.items() if j != i and q['team'] == p['team']]
        foes = [q for q in players.values() if q['team'] != p['team']]
        if mate and foes:
            dm = math.dist(p['start'], mate[0]['start'])
            check(dm <= min(math.dist(p['start'], f['start']) for f in foes) + 1e-6,
                  'player %d starts nearer its team-mate than any enemy' % i)

if target == 0:
    check('an ally still fights: watching' in log, 'the human watches after defeat while an ally lives')
    check("team won: victory" in log, 'the team-mate\'s win is the human\'s victory')
    check(re.search(r'now \.\?AVSReward@@.*Victory!', log), 'the result screen says Victory')
if target == 1:
    humans = [i for i, p in players.items() if p['human']]
    ais = [i for i, p in players.items() if not p['human']]
    aimask = sum(1 << i for i in ais)
    check('no teams set: the stock alliances' in log, 'no teams: nothing changed')
    check(all(players[i]['mask'] == aimask for i in ais), 'every computer player allied with the others (stock)')
    check(all(players[i]['mask'] == 1 << i for i in humans), 'the human allied with nobody (stock)')
if target == 3:
    check('defeated with no ally left: defeat' in log, 'the human, with no ally left, is defeated at once')
    check(re.search(r'now \.\?AVSReward@@.*Defeat@', log), 'the result screen says Defeat')

print('ALL PASS' if ok else 'FAILED')
sys.exit(0 if ok else 1)
