WarBreeds HD 0.15.2
===================

An unofficial patch for WarBreeds (Red Orb Entertainment, 1998) that makes it
run well on modern Windows: native widescreen resolutions, smooth zoom, sharp
scaling, windowed mode, working cutscenes, a Settings page on the main menu,
modern controls, a map editor, and the game's own multiplayer working
again. You need your own copy of the game (tested
with the GOG.com release).

Nothing in the game's own files is changed. The patch is a set of extra files
next to the game, and removing them puts everything back as it was.


WHAT'S IN THIS FOLDER
---------------------

  wsock32.dll      The patch itself (loaded automatically by the game).
  Fonts\           Orbitron, an optional modern font (SIL Open Font License).
  Menus\           Widescreen artwork for the 4:3 menu screens.
  Lang\            Languages: French and German, and how to add more.
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

2. Copy wsock32.dll and the Fonts, Menus and Lang folders into that folder,
   next to WARBREED.EXE. (If Windows asks for permission, allow it.)

3. Start the game as usual. The main menu shows "WarBreeds HD v0.15.2" at the
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
             interface size, language.

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
  - The interface is sized so every panel fits the window, even the tallest
    (the Gene Lab+'s Select Components), and panels stay put at every zoom.
  - Widescreen menu screens. Menu Art = Original shows the original 4:3
    pictures with black bars instead (see the note on the artwork below).
  - F2 shows a frame-rate counter.

Cutscenes
  - The movies play again (the game needed QuickTime 2, which modern Windows
    lacks). Movie Scaling and Movie Size are on the Video tab. Space skips.

Map editor
  - Map Editor on the main menu: edit any skirmish map, or New Map (size
    32-96 cells per side, base terrain, 2-8 players).
  - The map opens in the normal game view, frozen and without fog; scroll,
    zoom and the minimap work as in a mission.
  - The tools panel is in the top-right corner, with tabs: Terrain,
    Things, Units, Players, Map, Events, AI. Tab folds it to a one-line strip (and opens
    it again); the chosen tool stays in use, so you can paint behind it.
  - Terrain: pick a terrain and a brush size (1, 2, 3, 5), then drag to
    paint; an outline at the cursor shows what a stroke will cover.
    Shift+drag fills a rectangle, right-click picks the terrain under the
    cursor. Ctrl+Z / Ctrl+Y undo and redo any change.
  - Things: trees and rocks (on their own ground), ruins, crops; Fill Pods,
    Spores or Weeds fills every empty cropland tile; right-click removes.
  - Units: creatures and devices for the chosen player. Select a thing to
    drag it, delete it (Del), or set hit points, rank and gizmos.
  - Players: type, race, CGK levels, available clans; click the map to set
    the start point (shown as a numbered ring).
  - Map: name, fog, players supported and recommended.
  - Events: the original's event (trigger) editor, the game's own screen
    for it: conditions (timers, positions, deaths, ...) start actions
    (dialogs, messages, reinforcements, victory, ...). Shift / Ctrl /
    Ctrl+Shift with Left and Right step through events / conditions /
    actions, A adds, R removes, * makes an event persistent, Esc closes.
  - AI: each computer player's squad, base, scouting and influence. A
    skirmish on a map you made uses these settings (the original game
    always replaced them with the clan defaults). The game's own skirmish
    maps still use the clan defaults, so they play as they always have.
  - Check lists possible problems (start points, Shamans, power, start
    points cut off on foot, ...); saving runs it too and only warns.
  - Smart painting, as in the original editor: terrain blends into its
    neighbours, and only goes next to terrain it has transitions for (Lava
    next to Tar, for example). A refused stroke says what is in the way.
  - Save As (up to 8 letters) processes the map for pathfinding and saves it
    in DATA\MAPS, where the skirmish setup finds it. The game's own maps are
    never overwritten. Test Map saves and starts a skirmish on the map; it
    asks first, so click it twice.

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
  - The whole battlefield is redrawn every frame: fast flying units no longer
    leave trails ([Render] FullRedraw=0 in WarBreedsHD.ini restores the
    original behaviour).
  - With the Orbitron font, text that would run wider than the original font
    is narrowed to fit, so titles stay inside their bars.


LANGUAGES
---------

Settings > Interface > Language shows the game in another language, at
once: menus, units, briefings, result screens, and WarBreeds HD's own pages
and map editor. French and German are included. They were drafted by
machine (an AI model), so corrections are welcome:
https://github.com/Deadpoodle/warbreeds-hd

Each language is a text file in the Lang folder (WarBreedsHD_fr.txt, ...);
the Settings page lists every file it finds. Lang\README.txt explains how to
fix a translation or add a new language. Voices, movies and the text you
type stay English.


SKIRMISH TEAMS AND COLOURS
--------------------------

The skirmish setup has Team and Color columns, like the multiplayer lobby.

  - Click a row's team badge to cycle it: none, 1, 2, 3, 4. Players on the
    same team are allied (computer players too) and win together. With
    every team on none, the skirmish is the original: all computer players
    allied against you.
  - With Random placement, teammates start near each other.
  - Wiped out while a teammate still fights? You watch the rest of the
    battle; if your team wins, the result is Victory.
  - Click a row's colour to choose it, right-click for automatic. Each
    colour can be used once; a Random clan gets its colour at the start.


CAMPAIGN PROGRESS
-----------------

Each campaign (Tanu Ascendancy, Magha Uprising) remembers where you are
every time a mission briefing opens (in WarBreedsHD-progress.ini next to the
game). Click that campaign again to Continue from that mission, or Start
Over. Finishing a campaign clears its record. Saved games work as before.


MULTIPLAYER
-----------

The main menu's Multiplayer works again: the game's own LAN and Internet
play over TCP/IP, for up to four players. (On the GOG release it only showed
an error, and earlier versions of this patch broke it too; both are fixed.)

  - Every player needs WarBreeds HD, all on the same version.
  - Windows needs DirectPlay. If the game says it can't open multiplayer
    communications, turn it on: Control Panel > Programs > Turn Windows
    features on or off > Legacy Components > DirectPlay.
  - To host: Multiplayer, enter your name, Internet TCP/IP, Create.
    To join: the same, but Join, then type the host's IP address
    (on a home network it looks like 192.168.x.x; "ipconfig" shows it).
  - The host may need to allow WARBREED.EXE through Windows Firewall.
    Over the Internet, the host's router must forward DirectPlay's ports
    (TCP and UDP 47624, and 2300-2400).
  - IPX, the game's other 1998 option, doesn't exist on modern Windows.
  - After Start, the game takes about half a minute to begin while it
    measures each player's connection.
  - Two copies on one PC work too; each copy takes its own CPU core.

Tested with two copies on one PC; games between PCs are not tested yet.


UNINSTALL
---------

Delete these from the game folder:
  wsock32.dll, Fonts\, Menus\, Lang\, WarBreedsHD.ini, WarBreedsHD.log,
  WarBreedsHD-progress.ini,
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
