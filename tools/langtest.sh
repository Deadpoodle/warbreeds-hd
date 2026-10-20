#!/bin/sh
# Language tour: for each language and font, autopilot mode 23 (targets 0-5) in a
# hidden test copy, with every screen's screenshot kept in
# build/langshots/<lang>-<font>/ and the patch's squeeze lines and missing-text
# notes from the log.
# Usage: langtest.sh [langs] [fonts] [targets]   e.g. langtest.sh "fr de" "original orbitron" "0 1 2 3"
#        TESTDIR=testrun-x langtest.sh ...      another hidden test copy
# Targets: 0 menus, Settings, briefing, mission, Esc menu; 1 skirmish HUD and build list;
#          2 live language switching; 3 map editor; 4 Gene Lab; 5 Device Lab;
#          10 a skirmish's pod list; 11 Device Lab on a two-line name; 12 the skirmish map list;
#          13, 14 the map editor's AI and Events tabs.
# Fonts: original, orbitron, or release: the original setting with the release's
#        own fonts from re/<lang>/disc/FONTS (tools/locale_extract.py) in the test
#        copy's Fonts folder, as a player who copied them from the CD; removed after.
set -e
cd "$(dirname "$0")/.."
LANGS=${1:-fr de}
FONTS=${2:-original orbitron}
TARGETS=${3:-0 1 2 3 4 5}
T=${TESTDIR:-testrun}
mkdir -p "$T/Lang"
cp lang/WarBreedsHD_*.txt lang/WarBreedsHD_*.fonts "$T/Lang/" 2>/dev/null || true
mkdir -p "$T/Fonts"
cp third_party/fonts/*/*.ttf "$T/Fonts/"
INI="$T/WarBreedsHD.ini"
for lang in $LANGS; do
  for font in $FONTS; do
    out="build/langshots/$lang-$font"
    rm -rf "$out"
    mkdir -p "$out"
    setting=$font
    added=""
    if [ "$font" = release ]; then
      setting=original
      for f in re/$lang/disc/FONTS/*; do
        [ -f "$f" ] || continue
        cp "$f" "$T/Fonts/"
        added="$added $(basename "$f")"
      done
    fi
    sed -i -e "s/^Font=.*/Font=$setting/" "$INI"
    for t in $TARGETS; do
      # Every target starts in the language (target 2 changes it as it goes).
      if grep -q "^Language=" "$INI"; then sed -i "s/^Language=.*/Language=$lang/" "$INI"; else sed -i "s/^\[Display\]/[Display]\r\nLanguage=$lang/" "$INI"; fi
      TARGET=$t TESTDIR=$T tools/autotest.sh 23 90 > "$out/run-$t.txt" 2>&1 || true
      cp "$T"/auto-lang*-screen.bmp "$out/" 2>/dev/null || true
      grep -aE "fonts: |lang:|stuck" "$T/WarBreedsHD.log" >> "$out/log.txt" || true
    done
    for f in $added; do rm -f "$T/Fonts/$f"; done
    echo "$lang $font: $(ls "$out"/*.bmp 2>/dev/null | wc -l) screens"
  done
done
sed -i -e "s/^Font=.*/Font=original/" -e "s/^Language=.*/Language=en/" "$INI"
# The screens are kept in build/langshots; the test copy keeps none.
rm -f "$T"/*.bmp "$T"/*.BMP
