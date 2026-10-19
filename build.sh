#!/bin/sh
# Build WarBreeds HD (wsock32.dll) with a 32-bit MinGW-w64 toolchain.
#   Windows: MSYS2 mingw32 (C:/msys64/mingw32/bin on PATH, or set CXX)
#   Linux:   CXX=i686-w64-mingw32-g++ ./build.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-/c/msys64/mingw32/bin/g++.exe}
mkdir -p build
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -shared -static -s \
    -o build/wsock32.dll src/main.cpp src/scan.cpp src/gditrace.cpp src/diag.cpp src/camera.cpp src/layers.cpp src/autopilot.cpp src/headless.cpp src/zoom.cpp src/watchdog.cpp src/prof.cpp src/meter.cpp src/menu.cpp src/editor/editor.cpp src/editor/events.cpp src/editor/ai_settings.cpp src/editor/raw_paint.cpp src/editor/terrain_paint.cpp src/editor/mapfile.cpp src/editor/world.cpp src/editor/validate.cpp src/fonts.cpp src/controls.cpp src/skirmish.cpp src/skirmish_teams.cpp src/skirmish_save.cpp src/campaign.cpp src/lang.cpp src/voices.cpp src/net.cpp src/upnp.cpp src/render/render.cpp src/render/ddfake.cpp src/render/d3d11.cpp src/render/image.cpp src/movie/mov.cpp src/movie/audio.cpp src/movie/qt.cpp src/proxy.cpp src/wsock32.def \
    -ld3d11 -ldxgi -luuid -lgdi32 -luser32 -lole32 -lws2_32 -lkernel32
echo "built build/wsock32.dll"
