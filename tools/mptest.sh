#!/bin/sh
# Multiplayer on one PC: two hidden copies at once over TCP/IP. HOST (default
# testrun-hud) hosts as autopilot mode 20 target 1; JOIN (default testrun) joins
# 127.0.0.1 as target 2. Both play about 40 s of a match, then quit; each logs
# "in a game as player N, turn T" (turns past 0 mean the lockstep is running).
# Usage: mptest.sh [seconds]   COMMS=1 also logs the engine's comms debug text.
set -e
cd "$(dirname "$0")/.."
SECS=${1:-170}
HOST=${HOST:-testrun-hud}
JOIN=${JOIN:-testrun}
PATH=/c/msys64/mingw32/bin:$PATH sh build.sh 2>&1 | grep -E "error|warning" || true
for pair in "$HOST 1" "$JOIN 2"; do
  set -- $pair
  cp build/wsock32.dll $1/
  rm -f $1/auto-*.bmp
  sed -i -e "s/^Autopilot=.*/Autopilot=20/" -e "s/^Headless=.*/Headless=1/" -e "s/^AutopilotTarget=.*/AutopilotTarget=$2/" \
         -e "s/^CommsLog=.*/CommsLog=${COMMS:-0}/" $1/WarBreedsHD.ini
done
python tools/watchvis.py "$SECS" > build/vis-mp.txt &
powershell -NoProfile -File tools/smoke.ps1 -Seconds "$SECS" -Dir $HOST > build/smoke-host.txt &
sleep 4
powershell -NoProfile -File tools/smoke.ps1 -Seconds "$((SECS-4))" -Dir $JOIN > build/smoke-join.txt
wait
cat build/vis-mp.txt
for d in $HOST $JOIN; do
  echo "== $d"
  grep -aE "pinned|in a game|MessageBox|ERROR|Finished SYNC" $d/WarBreedsHD.log || true
done
