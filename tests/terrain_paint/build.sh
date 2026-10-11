#!/bin/sh
# Build and run the smart terrain painter tests (src/editor/terrain_paint.cpp).
#   tests/terrain_paint/build.sh            build, run, render the PNGs
#   tests/terrain_paint/build.sh --no-run   build only
# Needs the game data in testrun/ (or testgame/): DATA/TERRAIN.MHK and the
# shipped maps. Output: build/terrain_paint/ (report.txt, *.tpg, *.png).
set -e
cd "$(dirname "$0")/../.."
PY=${PYTHON:-$(command -v python)}  # before PATH changes: MSYS2's python has no PIL
BIN=/c/msys64/mingw32/bin
export PATH="$BIN:$PATH"
CXX=${CXX:-$BIN/g++.exe}
mkdir -p build/terrain_paint
"$CXX" -std=c++17 -O2 -Wall -Wextra -static -Isrc/editor \
    -o build/terrain_paint/terrain_paint_test.exe \
    tests/terrain_paint/test_main.cpp src/editor/terrain_paint.cpp
echo "built build/terrain_paint/terrain_paint_test.exe"
[ "$1" = "--no-run" ] && exit 0
./build/terrain_paint/terrain_paint_test.exe . build/terrain_paint | tee build/terrain_paint/report.txt
"$PY" tools/terrain_paint_render.py build/terrain_paint
