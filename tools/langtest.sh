#!/bin/sh
# Language tour: for each language and font, autopilot mode 23 (targets 0-3) in a
# hidden test copy, with every screen's screenshot kept in
# build/langshots/<lang>-<font>/ and the patch's squeeze lines and missing-text
# notes from the log.
# Usage: langtest.sh [langs] [fonts] [targets]   e.g. langtest.sh "fr de" "original orbitron" "0 1 2 3"
#        TESTDIR=testrun-x langtest.sh ...      another hidden test copy
set -e
cd "$(dirname "$0")/.."
LANGS=${1:-fr de}
FONTS=${2:-original orbitron}
TARGETS=${3:-0 1 2 3}
T=${TESTDIR:-testrun}
mkdir -p "$T/Lang"
cp lang/WarBreedsHD_*.txt "$T/Lang/" 2>/dev/null || true
INI="$T/WarBreedsHD.ini"
for lang in $LANGS; do
  for font in $FONTS; do
    out="build/langshots/$lang-$font"
    rm -rf "$out"
    mkdir -p "$out"
    sed -i -e "s/^Font=.*/Font=$font/" "$INI"
    for t in $TARGETS; do
      # Every target starts in the language (target 2 changes it as it goes).
      if grep -q "^Language=" "$INI"; then sed -i "s/^Language=.*/Language=$lang/" "$INI"; else sed -i "s/^\[Display\]/[Display]\r\nLanguage=$lang/" "$INI"; fi
      TARGET=$t TESTDIR=$T tools/autotest.sh 23 90 > "$out/run-$t.txt" 2>&1 || true
      cp "$T"/auto-lang*-screen.bmp "$out/" 2>/dev/null || true
      grep -aE "fonts: .*squeezed|lang:|stuck" "$T/WarBreedsHD.log" >> "$out/log.txt" || true
    done
    echo "$lang $font: $(ls "$out"/*.bmp 2>/dev/null | wc -l) screens"
  done
done
sed -i -e "s/^Font=.*/Font=original/" -e "s/^Language=.*/Language=en/" "$INI"
