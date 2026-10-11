#!/bin/sh
# Build, install into testrun (the automated-test copy, always hidden) and run
# the headless autopilot. Never touches testgame (the player's copy).
# Usage: autotest.sh [mode] [seconds]   mode 1 = mission test (default), 2 = main-menu Settings test,
#        13 = map editor (TARGET=0 edit, 1 New Map, 2 Exit; then python tools/editor_check.py testrun)
#        14 = event editor + Global AI (TARGET=0 edit and Test Map, 1 a map's own events;
#             then python tools/events_check.py testrun)
#        PROFILE=1 autotest.sh ...      also profile
#        TESTDIR=testrun-x autotest.sh  another hidden test copy (relative to the repo, or absolute as C:/...),
#                                       so two runs can go side by side
set -e
cd "$(dirname "$0")/.."
MODE=${1:-1}
SECS=${2:-40}
T=${TESTDIR:-testrun}
PATH=/c/msys64/mingw32/bin:$PATH sh build.sh 2>&1 | grep -E "error|warning" || true
cp build/wsock32.dll $T/
rm -f $T/auto-*.bmp $T/WarBreedsHD-samples*.txt
sed -i -e "s/^Autopilot=.*/Autopilot=$MODE/" -e "s/^Headless=.*/Headless=1/" -e "s/^Profile=.*/Profile=${PROFILE:-0}/" -e "s/^AutopilotTarget=.*/AutopilotTarget=${TARGET:-0}/" $T/WarBreedsHD.ini
python tools/watchvis.py "$SECS" > build/vis.txt &
powershell -NoProfile -File tools/smoke.ps1 -Seconds "$SECS" -Dir $T
wait
cat build/vis.txt
grep -aE "not closed|autopilot: (saved|key|done|click|right-click)|menu: |editor: " $T/WarBreedsHD.log || true
python tools/hudbounds.py $T
