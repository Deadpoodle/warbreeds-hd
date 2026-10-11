WarBreeds HD 0.11.0
===================

An unofficial patch for WarBreeds (Red Orb Entertainment, 1998) that makes it
run well on modern Windows: native widescreen resolutions, smooth zoom, sharp
scaling, windowed mode, working cutscenes, a Settings page on the main menu,
modern controls and a map editor. You need your own copy of the game (tested
with the GOG.com release).

Nothing in the game's own files is changed. The patch is a set of extra files
next to the game, and removing them puts everything back as it was.


WHAT'S IN THIS FOLDER
---------------------

  wsock32.dll      The patch itself (loaded automatically by the game).
  Fonts\           Orbitron, an optional modern font (SIL Open Font License).
  Menus\           Widescreen artwork for the 4:3 menu screens.
  Controls.html    Every keyboard and mouse control, original and new.
  README.txt       This file.
  LICENSE.txt      Licences and credits.


INSTALL (WINDOWS)
-----------------

1. Find your WarBreeds folder, the one that contains WARBREED.EXE.
     GOG Galaxy: select WarBreeds in your library, click the settings icon
     next to Play, then Manage installation > Show folder.
     Usual places:  C:\Program Files (x86)\GOG Galaxy\Games\Warbreeds
                    C:\GOG Games\Warbreeds

2. Copy wsock32.dll and the Fonts and Menus folders into that folder, next to
   WARBREED.EXE. (If Windows asks for permission, allow it.)

3. Start the game as usual. The main menu shows "WarBreeds HD v0.11.0" at the
   bottom right and has a new Settings item.

On first launch the patch creates WarBreedsHD.ini (all settings, with
comments) and WarBreedsHD.log next to the game.


SETTINGS
--------

Use Settings on the main menu: click a setting to step to its next value,
right-click for the previous one. Changes are saved straight away, and most
take effect immediately.

  Video      Display mode (fullscreen / windowed), resolution, scaling,
             VSync, FPS counter, movie scaling and size.
  Controls   Smooth arrow-key scrolling, WASD, scroll speed, zoom limits,
             Alt+F4, capture mouse, mouse scheme (Classic / Modern),
             builder click, quick-build keys, group build. Long tabs
             scroll with the mouse wheel ("More above/below").
  Interface  Menu art (widescreen / original), font (original / Orbitron),
             interface size.

Every option is also in WarBreedsHD.ini, with an explanation.


WHAT THE PATCH DOES
-------------------

Display
  - Runs missions at your desktop resolution (or one you pick), any shape:
    16:9, 16:10, 21:9. You see more of the battlefield.
  - Sharp, clean scaling through Direct3D 11. Fullscreen (borderless) or
    windowed; Alt+Enter switches.
  - Smooth zoom with the mouse wheel, towards the cursor. The interface stays
    one size while you zoom.
  - Widescreen menu screens. Menu Art = Original shows the original 4:3
    pictures with black bars instead (see the note on the artwork below).
  - F2 shows a frame-rate counter.

Cutscenes
  - The movies play again (the game needed QuickTime 2, which modern Windows
    lacks). Movie Scaling and Movie Size are on the Video tab. Space skips.

Map editor
  - Map Editor on the main menu: edit the terrain of any skirmish map, or
    New Map (size 32-96 cells per side, base terrain, 2-8 players).
  - The map opens in the normal game view, frozen and without fog; scroll,
    zoom and the minimap work as in a mission.
  - Pick a terrain and a brush size (1, 2, 3, 5), then drag to paint.
    Shift+drag fills a rectangle, right-click picks the terrain under the
    cursor, Ctrl+Z / Ctrl+Y undo and redo.
  - Smart painting, as in the original editor: terrain blends into its
    neighbours, and only goes next to terrain it has transitions for (Lava
    next to Tar, for example). A refused stroke says what is in the way.
  - Save As (up to 8 letters) processes the map for pathfinding and saves it
    in DATA\MAPS, where the skirmish setup finds it. The game's own maps are
    never overwritten. Test Map saves and starts a skirmish on the map.

Controls (full list in Controls.html)
  - Mouse wheel and numpad +/- zoom. Arrow keys scroll smoothly.
  - Unit groups: double-tap a number to jump to the group, Shift+number adds
    it to your selection (Ctrl+number and number work as before).
  - Shift+drag adds a box of units to the selection.
  - Quick-build: B then a letter (R Refinery, T Turret ...); P then 1-3 for
    pods.
  - Builders clicked onto your unfinished or damaged building finish or
    repair it.
  - Group build: with several builders selected, the build, plant and repair
    buttons and keys still work. The nearest one starts the building and the
    others walk over and help build it.
  - Pods can be planted in fog: the Shaman walks there and plants.
  - Optional Modern mouse scheme: left-click selects, right-click gives
    orders. Classic (the original) is the default.
  - Alt+F4 quits.

Fixes
  - Pins the game to one CPU core. The 1998 timer code can hang or crash on
    multi-core CPUs (the GOG release does the same).
  - The in-mission Game Options resolution slider is removed: resolution is
    set from the main menu now.


UNINSTALL
---------

Delete these from the game folder:
  wsock32.dll, Fonts\, Menus\, WarBreedsHD.ini, WarBreedsHD.log,
  and DDrawCompat.ini if it's there.

The game is then exactly as before.


OTHER VERSIONS AND SYSTEMS
--------------------------

  Steam version:  not tested yet. If the patch doesn't recognise the game it
                  shows a message, changes nothing, and the game runs as
                  normal.

  Linux / Steam Deck (Proton or Wine): not tested yet. Install the same way,
                  then make Wine load the patch: in Steam, set the game's
                  launch options to
                      WINEDLLOVERRIDES="wsock32=n,b" %command%
                  or with plain Wine, run the game with that variable set.


TROUBLESHOOTING
---------------

  - To turn the whole patch off without uninstalling, set Enabled=0 under
    [General] in WarBreedsHD.ini.
  - If something looks wrong, WarBreedsHD.log (next to the game) says what
    the patch did. Include it with any bug report.
  - Settings got into a muddle? Settings > Reset to Defaults (click twice),
    or delete WarBreedsHD.ini and it's recreated on the next launch.


A NOTE ON THE ARTWORK
---------------------

The widescreen menu pictures were made by extending the game's original
artwork with AI image generation, then retouched so the original 4:3 picture
sits unchanged in the middle. If you'd rather not use them, set Menu Art to
Original in Settings, or delete the Menus folder.


CREDITS
-------

WarBreeds (c) 1998 Red Orb Entertainment / Broderbund. This patch is an
unofficial fan project, not affiliated with or endorsed by the rights holders.
Orbitron font by Matt McInerney, SIL Open Font License 1.1 (Fonts\OFL.txt).
See LICENSE.txt.
