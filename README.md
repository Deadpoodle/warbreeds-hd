# WarBreeds HD

An unofficial patch that makes **WarBreeds** (Red Orb Entertainment, 1998) run well on modern Windows:
- native widescreen resolutions;
- smooth zoom;
- sharp scaling;
- windowed mode;
- working cutscenes;
- a Settings page;
- modern controls;
- a map editor.

It needs your own copy of the game, and has been tested with the GOG.com release.

The patch changes none of the game's files. It is a DLL that sits next to the game, so removing it puts everything back.

**[Download the latest release](https://github.com/Deadpoodle/warbreeds-hd/releases/latest)**, then follow the README inside the zip.

![Smooth zoom and scrolling](docs/screenshots/zoom-scroll.gif)

## Before and after

| Original (GOG release) | WarBreeds HD |
|---|---|
| ![Original main menu](docs/screenshots/menu-before.jpg) | ![Widescreen main menu with Settings](docs/screenshots/menu-after.jpg) |
| The 640×480 menu with black bars | Widescreen art, a Settings item, the version label |
| ![Original 640x480 mission](docs/screenshots/game-before.jpg) | ![The same mission at 2560x1440](docs/screenshots/game-after.jpg) |
| Missions at 640×480 | Missions at your desktop resolution |

| Zoomed out | Zoomed in |
|---|---|
| ![Zoomed out](docs/screenshots/game-zoomed-out.jpg) | ![Zoomed in](docs/screenshots/game-zoomed-in.jpg) |

| Settings page | Optional Orbitron font |
|---|---|
| ![Settings page](docs/screenshots/settings.jpg) | ![Settings page in Orbitron](docs/screenshots/settings-orbitron.jpg) |

![The opening cutscene, playing again](docs/screenshots/movie.jpg)

*The GIF and the zoomed shots are from a real skirmish (recorded in a 1920×1080 window). The before/after gameplay pair comes from the automated test harness at 2560×1440, with fog of war switched off (a debug option) so the terrain shows.*

## What it does

**Display**
- Missions run at your desktop resolution (or one you pick), in any shape: 16:9, 16:10 or 21:9.
- Scaling is crisp, through our own Direct3D 11 presenter.
- Borderless fullscreen or a resizable window; Alt+Enter switches between them.
- Smooth mouse-wheel zoom towards the cursor. The interface stays one size while the battlefield zooms.
- Widescreen menu screens. A setting brings back the original 4:3 art for anyone who'd rather not see AI-extended artwork.
- F2 shows a frame-rate counter.

**Cutscenes**
- The movies play again. The game needed QuickTime 2 for Windows; the patch replaces it with its own Cinepak/IMA4 player.
- Movies are upscaled on the GPU, with a choice of three scaling modes: Enhanced, Smooth and Sharp.

**Settings page**
- A native Settings item on the main menu, with Video, Controls and Interface tabs.
- Changes are saved and applied immediately.

**Map editor**
- A Map Editor item on the main menu: edit any skirmish map's terrain, or start a new map (32–96 cells per side, 2–8 players).
- The map opens in the normal game view with everything frozen and no fog, so the camera, zoom and minimap work as in a mission.
- Pick a terrain, a brush size (1, 2, 3 or 5), then drag to paint; Shift+drag fills a rectangle; right-click picks the terrain under the cursor. Undo and redo with Ctrl+Z and Ctrl+Y.
- Smart painting, as in the original editor: terrain blends into its neighbours with the game's own transition tiles, and only goes next to terrain it has transitions for (Lava next to Tar, for example). A refused stroke says what is in the way.
- Save As writes the map to `DATA\MAPS` after the game's own map processing (pathfinding), so it appears in the skirmish map list. Built-in maps are never overwritten. Test Map (click twice: it asks first) saves and starts a skirmish on it.

**Controls** (the full list is in [docs/controls.html](docs/controls.html))
- Arrow keys scroll smoothly. Optional WASD.
- Unit groups: double-tap a number to centre on the group; Shift+number adds the group to the selection.
- Shift+drag adds a box of units to the selection.
- Quick-build: B, then a letter (R Refinery, T Turret, …); P, then 1–3 for pods.
- Builders clicked onto your own unfinished or damaged building go and finish or repair it.
- Group build: with several builders selected, the build, plant and repair buttons and keys still work. The nearest builder starts the building and the others walk over and help build it.
- Pods can be planted in fog: the Shaman walks there and plants.
- An optional **Modern mouse scheme**: left-click selects, right-click gives orders. Classic is the default.
- Alt+F4 quits.

**Fixes**
- Pins the game to one CPU core. The 1998 timer code hangs on multi-core CPUs, and GOG's build does the same.
- Removes the in-mission resolution slider, which fought the new resolution handling.

**Fonts**
- An optional modern font, Orbitron, which switches live.

## Install

1. Download the zip from [Releases](https://github.com/Deadpoodle/warbreeds-hd/releases/latest).
2. Copy `wsock32.dll`, `Fonts\` and `Menus\` into your WarBreeds folder, next to `WARBREED.EXE`.
3. Start the game. The main menu shows the version at the bottom right and a new Settings item.

To uninstall, delete those files plus `WarBreedsHD.ini` and `WarBreedsHD.log`. The zip's README.txt has the details, including notes for Steam and Linux, neither of which has been tested yet.

## Building from source

The patch is a 32-bit DLL, built with MinGW-w64.

**Windows (MSYS2):**
```bash
pacman -S mingw-w64-i686-gcc
```
```bash
sh build.sh
```
The DLL lands in `build/wsock32.dll`.

**Linux (cross-compiling):**
```bash
CXX=i686-w64-mingw32-g++ ./build.sh
```

To make a release zip, run `python tools/package.py`. It needs Pillow, and takes the menu art from a folder of PNGs (`--art DIR`).

## How it works

The game imports `WSOCK32.dll`, so a proxy `wsock32.dll` in the game folder is loaded at start-up. It forwards the real Winsock calls and patches the game in memory, with no edits to `WARBREED.EXE`. Everything was found by reverse engineering the 1998 executable.

| Path | What's there |
|---|---|
| `src/` | The patch: renderer (`render/`), zoom, interface layers, menu and Settings page, controls, fonts, cutscene player (`movie/`) |
| `tools/` | The headless test harness (`autotest.sh`, `smoke.ps1`), the disassembly helper (`wbre.py`), Mohawk `.MHK` image extraction (`mhkimg.py`), the menu-art pipeline, packaging |
| `re/` | Reverse-engineering reports: every hotkey, every mouse action, building in fog |
| `WIKI_LOG.md` | The full development log: findings, addresses, dead ends and decisions |
| `docs/` | The controls reference and screenshots |

The patch includes a debug autopilot and a headless mode, so most testing ran hidden, with scripted input and screenshots, while the game was played normally on the same PC.

## Licence

The WarBreeds HD code, tools and documentation are released into the **public domain** ([The Unlicense](LICENSE)): do whatever you want with them.

Not covered:
- **Orbitron font:** under the SIL Open Font License.
- **WarBreeds itself:** © 1998 Red Orb Entertainment / Broderbund. No game files are included.
- **Widescreen menu artwork in the releases:** derived from the game's original art.

This is an unofficial fan project, not affiliated with or endorsed by the rights holders.
