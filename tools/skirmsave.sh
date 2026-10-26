#!/bin/sh
# Skirmish save and load: autopilot mode 24 in the hidden test copy.
#   target 0: a skirmish saved from the game and loaded again from the Esc menu,
#             played on a minute, then won (the result screen leads to the setup);
#   target 1: that save loaded from the main menu's Load Game;
#   target 2: that save without its .hd (as a stock save) loaded inside a skirmish;
#   target 3: watching an ally after defeat, Save Game is refused.
#   target 4: a player's save copied in as DATA/Saves/HDSKIRM.wbs (and .hd), loaded and played a minute;
# Each run's screenshots go to build/skirmsave/; the PASS/FAIL lines are printed.
# Usage: skirmsave.sh [targets]   e.g. skirmsave.sh "0 1 2"
set -e
cd "$(dirname "$0")/.."
TARGETS=${1:-0 1 2 3}
T=${TESTDIR:-testrun}
out=build/skirmsave
rm -rf "$out"
mkdir -p "$out"
for t in $TARGETS; do
  TARGET=$t TESTDIR=$T tools/autotest.sh 24 150 > "$out/run-$t.txt" 2>&1 || true
  cp "$T"/auto-skirmsave-*.bmp "$out/" 2>/dev/null || true
  grep -aE "autopilot: (before|after|a minute|PASS|FAIL|stuck|Esc menu)|skirmish save|teams: " "$T/WarBreedsHD.log" > "$out/log-$t.txt" || true
  echo "== target $t"
  grep -aE "PASS|FAIL|stuck|skirmish save: (FAIL|can.t)" "$out/log-$t.txt" || echo "(no verdict: see $out/log-$t.txt)"
done
# The test saves stay in the test copy for the next run; the screens are in build/.
rm -f "$T"/*.bmp "$T"/*.BMP
