WarBreeds HD 0.15.2
===================

An unofficial patch for WarBreeds (Red Orb Entertainment, 1998) that makes it
run well on modern Windows: native widescreen resolutions, smooth zoom, sharp
scaling, windowed mode, working cutscenes, a Settings page on the main menu,
modern controls, a map editor, and the game's own multiplayer working
again. You need your own copy of the game (tested with the GOG.com and
Steam releases, which have the same game files).

Nothing in the game's own files is changed. The patch is a set of extra files
next to the game, and removing them puts everything back as it was.


WHAT'S IN THIS FOLDER
---------------------

  wsock32.dll      The patch itself (loaded automatically by the game).
  Fonts\           Free fonts (SIL Open Font License): Orbitron, an optional
                   modern font for English, and Roboto Condensed, the French
                   menus' stand-in for the 1998 French release's font.
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
     Steam: right-click WarBreeds in your library, then Manage > Browse
     local files.
     Usual places:  C:\Program Files (x86)\GOG Galaxy\Games\Warbreeds
                    C:\GOG Games\Warbreeds
                    C:\Program Files (x86)\Steam\steamapps\common\WarBreeds

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
             builder click, quick-build keys, group build, placement
             range. Long tabs
             scroll with the mouse wheel ("More above/below").
  Interface  Menu art (widescreen / original), font (original / Orbitron;
             English only: other languages use their own), interface size,
             language.

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
  - A mission or a loaded game starts centred on your base.

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
    buttons and keys still work. They all build it (with Improved
    pathfinding they start it together; with Original the nearest starts it
    and the others help once it stands).
  - Placing a Refinery, Relay, Relay+, Turret, Plasma Cannon or Electric
    Tower shows its range ring at the cursor, like the rings of the
    Refineries and Relays already built: what a Refinery collects (every
    energy pod inside), what a Relay powers, how far a turret picks targets
    by itself. Settings > Controls > Placement Range.
  - Pods can be planted in fog: the Shaman walks there and plants.
  - Optional Modern mouse scheme: left-click selects, right-click gives
    orders. Classic (the original) is the default.
  - Alt+F4 quits.

Pathfinding (Settings > Controls > Pathfinding: Improved, the default, or
Original)
  - Units plan their whole route across the map, around rocks, cliffs,
    water and buildings, instead of walking into dead ends and jittering.
  - They know the terrain and rocks even under fog, learn about enemy
    buildings when they see them, and replan when one blocks the way.
  - A click on a spot units can't reach sends them to the nearest spot they
    can reach, where they stop.
  - A group gets a spot each around the click. Formation moves (Space) are
    unchanged.
  - An idle friend in the way is asked to step aside. In a jam a unit waits
    about a second, then goes another way if that isn't much longer. A unit
    that makes no progress for 10 seconds stops.
  - A building site is kept clear: the player's other units keep off it
    once a builder is on its way, so they can't make the building fail.
  - Group build: every selected builder walks to the site, takes its own
    spot round it with the build animation, and they start the building
    together (four start it in about a quarter of the time), then all build
    it on. With Original, the others wait until its first stage is up.
  - Unit speeds are unchanged; flyers fly as before.
  - Also fixes the 1998 bug that misread which tiles are blocked, and the
    one that let units and buildings onto animated water.
  - Applies to every unit, the computer players' too, from the next
    mission. In multiplayer everyone uses the host's choice (Original if a
    player has an older WarBreeds HD).

Skirmish objectives
  - The Esc menu's Objectives lists every enemy to defeat, with a green
    check beside each one you have beaten.

Fixes
  - A 1998 crash in long games: the game's memory of an enemy building seen
    earlier could, once that building was destroyed out of sight and its
    number reused, be read from the wrong thing and crash (divide by zero).
  - Pins the game to one CPU core. The 1998 timer code can hang or crash on
    multi-core CPUs (the GOG release does the same).
  - The in-mission Game Options resolution slider is removed: resolution is
    set from the main menu now.
  - The whole battlefield is redrawn every frame: fast flying units no longer
    leave trails ([Render] FullRedraw=0 in WarBreedsHD.ini restores the
    original behaviour).
  - With the Orbitron font, text that would run wider than the original font
    is narrowed to fit, so titles stay inside their bars. Orbitron is made
    for English: in other languages the Font setting shows "By language".


LANGUAGES
---------

Settings > Interface > Language shows the game in another language, at
once: menus, units, map names, briefings, result screens, and WarBreeds
HD's own pages and map editor. French and German are included. Their game
text, unit, building and map names are the 1998 French and German releases'
own. Their menus
are shown in a look-alike of those releases' font, and the HUD and labs in
the game's own fonts, so it fits and stays readable. To see them in the
original fonts instead, copy the files in the CD's FONTS folder into the
game's Fonts folder. WarBreeds HD's own text in both was drafted by machine
(an AI model), so corrections are welcome:
https://github.com/Deadpoodle/warbreeds-hd

Each language is a text file in the Lang folder (WarBreedsHD_fr.txt, ...);
the Settings page lists every file it finds. Lang\README.txt explains how to
fix a translation or add a new language. Movies and the text you type stay
English.

FRENCH OR GERMAN VOICES

The spoken announcements ("construction complete", "unit lost", ...) can be
French or German, taken from your own copy of that version of the game. No
game audio comes with the patch, and your English voices are never changed.

1. On your French or German WarBreeds CD (or its disc image), find
   WARBREED\DATA\GLOBSFX.MHK (French: about 4.5 MB, German 4.3 MB).
2. In your WarBreeds game folder (the one with WARBREED.EXE), make a folder
   named Voices, and inside it one named after the language: fr for
   French, de for German.
3. Copy GLOBSFX.MHK into it, keeping its name:

     WarBreeds\DATA\GLOBSFX.MHK       the English voices: leave this alone
     WarBreeds\Voices\fr\GLOBSFX.MHK  from the French CD
     WarBreeds\Voices\de\GLOBSFX.MHK  from the German CD

4. On the main menu, Settings > Interface > Voices:
   By language (the default) - the voices of the language the game is
     shown in, if you have them, else English;
   English, or a language you added - those voices, whatever the text.
   The change is immediate. A file that isn't a WarBreeds GLOBSFX.MHK is
   ignored (WarBreedsHD.log says so) and English plays instead.


SKIRMISH TEAMS, COLOURS AND SAVES
---------------------------------

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

Saving: the Esc menu in a skirmish has Load Game and Save Game, as in a
campaign. A skirmish save is two files in DATA\Saves: <name>.wbs (the
game's own) and <name>.wbs.hd (who you were and the skirmish setup). Keep
them together; without the .hd the game loads as the original would have.
While you watch an ally after your own defeat, Save Game is not available.


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
  - Windows Firewall may ask to allow WARBREED.EXE: allow it (private and
    public networks for Internet play).
  - IPX, the game's other 1998 option, doesn't exist on modern Windows.
  - After Start, the game takes about half a minute to begin while it
    measures each player's connection.
  - Two copies on one PC work too; each copy takes its own CPU core.

Over the Internet. DirectPlay needs every player to be reachable from the
others: TCP and UDP port 47624 (the host) and 2300-2400 (everyone). WarBreeds
HD asks each player's router to forward these by itself (UPnP), and corrects
the addresses players behind a router send, so on most home connections the
host just gives the others their internet address (search "what is my IP").
WarBreedsHD.log says what happened ("upnp: ...").
  - If the log says "no router answered", UPnP is off on that router: turn
    it on in the router's settings, or forward UDP port 47624 by hand to the
    host's PC (players have confirmed that this one port is enough).
  - If it says the router's internet address is private (CGNAT, common on
    mobile and some fibre connections), others can't reach that PC: let a
    player with a normal connection host, or use a VPN.
  - A virtual LAN always works: ZeroTier, Tailscale or Hamachi. Everyone
    joins the same network, and the host gives out the VPN address.
(Ports 6073 and 41624, seen in some guides, aren't used by this game.)

[Network] NatFix and UPnP in WarBreedsHD.ini turn these off. If the game
ever crashes in multiplayer, WarBreedsHD.log ends with "crash:" lines and
WarBreedsHD-crash.dmp is written beside it: please send both.

Tested with two copies on one PC; games between PCs over the Internet are
being tested now: reports are welcome.


UNINSTALL
---------

Delete these from the game folder:
  wsock32.dll, Fonts\, Menus\, Lang\, WarBreedsHD.ini, WarBreedsHD.log,
  WarBreedsHD-progress.ini,
  and DDrawCompat.ini if it's there.
Skirmish saves leave a <name>.wbs.hd beside each save in DATA\Saves:
delete those too (the saves then load as the original game would load
them).

The game is then exactly as before.


OTHER VERSIONS AND SYSTEMS
--------------------------

  Steam version:  works the same as GOG's: Steam sells the same game files.

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
Fonts, all SIL Open Font License 1.1 (licences in Fonts\OFL-<name>.txt):
Orbitron by The League of Moveable Type and the Orbitron Project Authors;
Roboto Condensed by The Roboto Project Authors.
The French game text is the 1998 French release's own, included with the
original developers' blessing.
See LICENSE.txt.
