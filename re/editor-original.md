# The original WarBreeds Map Editor (1998): features, UI, workflow and engine overlap

This is research for an in-game map editor in WarBreeds HD. It covers what the official editor does, how it does it, and how much of it already exists inside the retail game. The `.WBM` file format is documented separately.

**Inputs:**

- `wbedit/`, the player's copy of the official editor (beta 1.0, March 1998):
  - `MapEditor.exe`;
  - the WinHelp manual `EditData/WBMEHELP.HLP` with its contents file `.CNT`;
  - `Editor_Readme.txt`;
  - the data in `EditData/`.
- The retail game `testgame/WARBREED.EXE` (GOG build) and its existing Ghidra export in `re/ghidra-export/`.

Nothing was run. All of the following came from static analysis:

- a Ghidra 12.1 headless analysis of `MapEditor.exe` (4,281 functions);
- `pefile` resource dumps of both executables;
- recovery of MSVC RTTI vtables in both;
- a capstone-based function matcher;
- a small WinHelp decoder written for this job.

All addresses are virtual addresses with image base 0x400000. "Editor" means `MapEditor.exe` and "game" means the retail `WARBREED.EXE`. Anything marked **(uncertain)** is an inference that hasn't been confirmed.

---

## 1. Key findings

1. **The editor is the game engine with an MFC shell around it.**
   - About 85% of the editor's non-library code matches functions in the retail game:
     - 2,097 of its 2,942 application and engine functions match exactly (masked hash);
     - 392 more match closely (same instruction sequence, different struct offsets).
   - The map view is the game's own `SGame` mission scene drawn into an MFC window. Units, devices, effects, power zones and the event editor are all drawn by the engine.
   - Only about 84 KB of code is editor-specific:
     - the terrain auto-transition painter (about 19 KB);
     - the MFC windows and dialogs;
     - the `AEditor` input layer;
     - the palette and "terrain thing" classes.
2. **Much of the editor's feature set is already inside `WARBREED.EXE`:**
   - **The event (trigger) editor.** The engine-drawn `SEvent` scene, with all 9 condition and 28 action classes and their panel labels, is compiled into the game as scene ID 0x16 (object 0x5790d8, constructor 0x478280). No code path in the game was found that opens it.
   - **Map processing (pathfinding "guide graph").** It is started by 0x4a4eb0 (through 0x4a8030) and advanced 200 point pairs per call by 0x4a5200, which prints "Map processed" when it finishes. The editor's Save runs exactly this step.
   - **Saving and loading a map from a mission.** `SGame` message 1 with a path (scene slot 0x58, 0x4803e0) queues a save, and message 0 queues a load. The editor's File > Save sends that same message to its `SGame` (see section 5.8).
   - **A developer console that edits maps.** Typing `SPAMSPAMSPAMHUMBUG` enables F9 (see `re/hotkeys.md`). Its command table at 0x507360 includes the following, with the handler of each:

     | Command | Handler |
     |---|---|
     | `save map` | 0x4830b0 |
     | `load map` | 0x4830a0 |
     | `process map` | 0x482ab0 |
     | `save terrain` / `load terrain` (`terrain.hoo`) | 0x482fa0 / 0x483020 |
     | `place` | 0x483590 |
     | `create` (player) | 0x482e70 |
     | `player ` | 0x482980 |
     | `supports` / `recommended` | 0x482900 / 0x482940 |
     | `cgk ` | 0x485690 |
     | `fog ` | 0x485630 |
     | `crop ` | 0x4857a0 |
     | `aggression ` | 0x4825e0 |
     | `bounds on` / `bounds off` | 0x483480 / 0x4834a0 |
     | `makemap` | 0x485510 |

   - **Terrain tile stamping.** `ATerrainTile` exists in the game (vtable 0x504cdc). Its method at 0x4559e0 writes a tile into the world grid. It is byte-for-byte the editor's 0x452f50.
   - **The terrain tables the painter needs.** `TRID`, `TRST`, `TRVA`, `TRPL` and `TRTH` in `TERRAIN.MHK` are identical in the retail data. The game only reads `TRID` and `TRVA`.
3. **What the game lacks:**
   - **The smart-palette painter.** It picks tiles automatically from corner terrain types and is editor-only (0x43c450–0x441439). It is described in full in section 5.1 so it can be re-implemented.
   - **The palette data and UI.** This includes the palette thumbnails, which are `BITMAP` resources 139–414 in `MapEditor.exe` only.
   - **The start-point marker art** (`PLYRSTRT.MHK`, which is not in retail `DATA`).
   - **The MFC dialogs** for map, player and AI attributes.
4. **The editor's engine is an older build than retail.**
   - Struct offsets and globals differ: the editor's world is at 0x54c380 and the game's at 0x55dd60.
   - The game string table (resource IDs 1–650) was renumbered: 314 IDs differ, and 236 exist only in the editor.
   - The art was remapped to a Windows-safe palette, and retail has 1–4 more terrain images.
   - So no editor code can be lifted byte-for-byte. Every editor routine listed here has to be rebuilt against the game's addresses, or re-implemented.
5. **Two corrections to existing notes:**
   - The game's **O** key (0x480280 → 0x449300 on `[0x579278]`, listed in `re/hotkeys.md` as "likely a shroud/visibility view") is the **power-zone view**. The editor's View > Power Zones calls the same function (editor 0x484c00).
   - The OK DIALOG event action takes string-table IDs. Use the retail table: the editor's numbers don't match it.

---

## 2. The manual (WBMEHELP.HLP)

**Format:**

- WinHelp 4 (`|SYSTEM` minor 33), compiled in March 1998, titled "WarBreeds Map Editor Help".
- Topic blocks are 4 KB, LZ77-compressed, with Hall phrase compression (`|PhrIndex` and `|PhrImage`).
- 42 topics, 2 of them empty. 75 bitmaps (`|bm0`–`|bm74`): the dialogs, toolbar buttons and terrain samples.
- Some hotspots are clickable icons; for example, `bm1`, the smart palette, links each of its 21 icons to a terrain topic.

The decoder (`hlp.py` and `hlpbm.py`), the full decoded text and the PNGs of every bitmap are kept in the researcher's scratch folder, not in the repository.

**Contents (`.CNT`):**

- Getting Started; Map Design Tips.
- Using the Painting Tools: the Toolbar, the Terrain Palette with its terrain, obstacle and crop types, the Devices palette, the Creatures palette.
- Using the Menu: File, View, Tools (Global AI Editor, Auto Fill, Fill Cropland, Event Editor), Settings (Place Starting Point, Map Attributes, Player Attributes, Auto Scroll).

### Topic summaries

- **Getting Started.** Create a new map, paint terrain, cropland and obstacles, select each player and give them creatures and devices ("at least one shaman"), set each player's start point, and save. The manual says "six basic steps" but lists five. Maps are `.WBM` files and go in `DATA\MAPS`.
- **Creating a New Map** (dialog 130):
  - Size: High and Wide, each 32–96 terrain tiles. These are probably 64-pixel cells **(uncertain)**; see section 5.7.
  - Three fog options: Fog and Shroud on; Fog on, Shroud off; both off. The manual explains that the shroud is lifted permanently once explored, while the fog covers whatever isn't currently seen.
  - Starting terrain: sand, crystal or tar.
  - Players Supported 2–8, and Players Recommended no more than Supported.
  - Map Name, which is what the game's load dialog shows. It isn't the file name.
- **Opening a Map.** File > Open replaces the current map.
- **Saving a Map.** Save and Save As add `.WBM`. A "Processing Map..." progress dialog (386) then embeds pathfinding data for obstacles (lava, mud, canyons, rocks). It takes longer on bigger maps.
- **View Options:**
  - Toolbar; Status Bar; Zoom In/Out; Mini Map.
  - Power Zones: the bubbles around Refineries and Relay or Advanced Relay Towers, for the selected player only.
- **The Toolbar:**
  - New, Open, Save.
  - Power zones, Mini map, Zoom, Delete selection (creatures, devices, pods), Auto-fill.
  - The current-player list ("Player One"...).
  - While the event editor is open, a second group of buttons for events, conditions and actions.
- **The Terrain Palette:**
  - It sits at the top left, with a drop-down list of palettes.
  - The **Smart Palette** (first in the list) paints a terrain type and places the border tiles automatically.
  - The terrain-specific palettes are for hand-editing the border tiles.
  - Click a tile, then click and drag on the map.
- **Terrain types:**
  - *Crystal, sand and tar* are the three base types. They can be painted over one another, and the borders are automatic.
  - *Crystal canyons* are impassable to ground units. They go on crystal; paint crystal to shrink them or to make islands.
  - *Crushed crystal* (on crystal), *roots* (on sand) and *hardened tar* (on tar) can take cropland.
  - *Hot mud* (sand) and *lava* (tar) are impassable except to flyers.
  - *Crystal cliffs* are plateaus. They are usually impassable but can have openings.
  - *Cropland* is the only terrain where pods, spores and weeds grow. It can be painted on crushed crystal, roots or hardened tar, but not directly on a base type.
- **Obstacles:**
  - Desert obstacles (sand trees and rocks) go only on roots.
  - Crystal trees and rocks go only on crushed crystal.
  - Tar trees and rocks go only on hardened tar.
  - Yedda ruins go on any open terrain and are mostly underground.
- **Crops.** Pods, spores and weeds, painted only on cropland. Crops start spreading into empty adjacent cropland as soon as the map loads.
- **Placing Devices.**
  - Pick from the Devices button of the Creatures/Devices palette, then click on the map. The colour shows the owning player.
  - An unpowered device blinks its red light (standby).
  - To power it: cropland, then pods, then a Refinery next to the cropland, then Relays or Advanced Relays to extend the zone. Use the power-zone view to check, and watch the Refinery's energy level.
- **Placing Creatures.**
  - Pick from the Creatures button. Don't give players creatures they can't build.
  - Creatures that carry weapons ("components" or "gizmos") open a **Select Components** dialog that works like the game's Gene Lab:
    - choose a component, then click a slot on the creature;
    - **With Gizmos**: then right-click on the map to place it;
    - **Frame Only**: place it without components.
  - The current player's CGK limits which components are offered.
- **Creature Attributes** (right-click a selected creature, dialog 415):
  - Hit points, from 1 to the maximum.
  - Veteran status: Rookie, Squaddie, Veteran or Hero. Each level adds 50% of the base maximum hit points.
- **Device Attributes** (right-click, dialog 418). Hit points only.
- **Selecting Players.** You place creatures, devices and start points for one player at a time, chosen in the toolbar's player list.
- **Place Starting Point** (Settings menu). Each player's start point is where their camera opens. It defaults to the top-left corner. It is shown as a large circle in the player's colour.
- **Player Attributes** (dialog 135), for each of the 8 players:
  - CGK level 0–5 for each of the four clans: 0 means none of that clan's weapons, 5 means all of them.
  - An Available Clans check box for each clan: whether the player can build that clan's creatures.
  - Type:
    - *Computer*: an AI in single-player; it doesn't exist in multiplayer.
    - *Human*: a multiplayer slot only.
    - *Either*: an AI in single-player, and a human or empty slot in multiplayer.
    - *None*: the player doesn't exist.
    - Player 1 is always Human.
- **Map Attributes** (dialog 387). Like New Map, but the size is greyed out: fog mode, Supported, Recommended and Name.
- **Global AI Editor** (dialog 132), for each AI player:
  - Squad Aggression: Low–High.
  - Base Defense: Defensive (turrets and towers) to Offensive (gene labs, psi towers and expansion).
  - Scouting: Low–High, default High.
  - AI Influence: More, meaning the AI may treat these settings as advice, to Less, meaning it follows them closely.
- **Auto Fill.**
  - Toolbar button or Tools menu. Pick a Smart Palette type and drag a rectangle.
  - The usual painting rules still apply; for example, no cropland on lava.
- **Fill Cropland.** Fills every empty cropland cell on the whole map with pods, spores or weeds without touching existing objects, or clears all crops.
- **Auto Scroll.** Edge scrolling on or off.
- **Event Editor.** Opened and closed from the Tools menu, or closed with Esc. It is a large tan panel drawn over the map.
  - An event is a set of **conditions**, all of which must hold, plus a set of **actions** and a **persistent** flag.
  - A persistent event fires every clock cycle while its conditions hold, and is never removed. The manual warns this can cause "bizarre side-effects".
  - The panel shows "Event *n* of *m*" (or "No Events"), "\*PERSISTENT\*", and two columns, "Condn. *i* of *k*" and the action equivalent.
  - The top box of each column picks the type. The boxes below it are the parameters.
  - There are five kinds of box: menu (its list appears top right), cycling, numeric entry (digits and Backspace, with a cap), text entry, and point (click the map; a grey cross marks a condition and a red X an action).
- **Conditions** (9): TIMED (cycles 0–9999, roughly 20–30 cycles a second), STEP ON POINT, PLAYER DEATH, PLAYER CGK, PLAYER DEVICES (fully working devices only), ENEMY SEEN (between enemies only), TRIGGER (8 global flags, 0–7, set by actions), STEP IN RECT (keep it to 3–4 tiles or it slows the game), THING MISSING (nothing at the point: use it for "device destroyed").
- **Actions.**
  - Every action has a **DELAY** in cycles. In a persistent event it should be 0.
  - The 27 documented actions:

    | Action | Parameters and notes |
    |---|---|
    | ADD CREATURE | Species, player, position, 4 gizmo slots |
    | AUDIO | Sound resource 50–106: weapons, explosions, UI clicks, clan deaths |
    | CAPTURE | Hands one player's assets to another, who is then defeated |
    | CD AUDIO | Track |
    | CGK | Player, clan, level |
    | CHAT | From player, text |
    | COMMAND SCRIPT | Plays a recorded command script file, e.g. the `.SKT` files in `DATA` |
    | COPY TERRAIN | Copies a rectangle of tiles only. Avoid cropland and obstacles: they break the guide graph |
    | EFFECT | Effect index 0–170: death sequences, explosions, rubble, puddles, clan logos, spinning gizmos and so on. Also direction, remap colour and air/ground |
    | HEAL | Hit points restored at a point |
    | MOVE INTO VIEW | Moves all creatures inside the visible bounds |
    | MOVE TO POINT | Optionally kills the creature if the destination is blocked |
    | OK DIALOG | Title, first and last line, as **string-table IDs** |
    | PLAYER END | Victory or defeat for a player |
    | QUAKE | Magnitude 0–7 |
    | REMOVE OBJECT | The only way to remove floating obstacles |
    | RGN IMPORTANCE | Region desirability for an AI player |
    | SET AGGRESSION | Base, squad and explore aggression, each from defensive to offensive |
    | SET GIZMO | Gizmo reward at a point; ground creatures only |
    | SET MOBILITY | Freezes or frees creatures in a rectangle |
    | SQUAD CHANGING | Whether an AI player may change squad types |
    | START GAME CLOCK | Countdown shown under the minimap. Once per mission. Can set a trigger when it expires |
    | TRIGGER | Sets trigger *n* |
    | VETERAN | Minimum veteran status at a point |
    | VISIBLE BOUNDS | Limits the viewable area. Must stay at least 1280x1024 pixels |
    | WARP | ADD CREATURE with a warp flash |
    | WARP TO POINT | MOVE TO POINT with a warp flash; safe in persistent events |

  - The code has a 28th action class, `AAction_RevealMap` (menu label "REVEAL MAP"), which the manual doesn't mention.
- **Event Editor Examples.** Four worked examples:
  - a heal pad (persistent);
  - a pair of floating circ saws;
  - "seeing a player hands you their units";
  - a mission whose map opens up when a player dies: an initial timed bounds change, then a quake, effects, a removed rock and the bounds reset.
- **Map Design Tips:**
  - Use at least two base terrains.
  - Keep cropland right at the map edge or well away from it, so creatures can't get stuck behind it.
  - Give everyone a shaman, or gene labs and psi.
  - Use cropland as a barrier, but sparingly.
  - Keep obstacles jagged, not straight.
  - Spread out several small cropland patches.
  - Vary the open terrain with its subtypes.
  - Make choke points; narrow ones favour the small Sen-Soth creatures.
  - Small maps are often better.
  - Power every device you place.
  - Avoid too many starting combat units, which favours shaman sniping (and the Tanu). Balance them with turrets.
  - Separated regions need flyers or warps, so give enough CGK for them.

**Map requirements, as the manual and code give them.** There is no explicit validation step (see section 5.10). These requirements come from the dialogs and the advice:

- size 32–96 by 32–96 tiles;
- 2–8 players supported, with Recommended no more than Supported;
- player 1 is always Human;
- a start point for each player;
- at least one shaman, or a way to build one, per player, if base building is wanted;
- power for any pre-placed device.

---

## 3. Executable resources

`MapEditor.exe` describes itself (VERSIONINFO 1) as "WarBreeds Map Editor", FileVersion 1,0,0,1. It is MFC 4.2, statically linked (class names `AfxWnd42s` and so on), single-document (`CSingleDocTemplate`). The help file name is `wbmehelp.hlp`.

### Menu 128 and every command

The handlers come from the MFC message maps at 0x4d9110–0x4db0c8.

| ID | Menu path | Handler | What it does |
|---|---|---|---|
| 57600 | File > New Map (Ctrl+N) | MFC, then `CMapEditorDoc::OnNewDocument` 0x403c90 | New Map dialog, defaults read from the registry (`Width`, `Height`, `Terrain`, `NumPlayers\Supported`/`Recommended`) |
| 57601 | File > Open (Ctrl+O) | 0x403a60, then `OnOpenDocument` 0x403fa0 | |
| 57603 | File > Save (Ctrl+S) | MFC, then `OnSaveDocument` 0x4040f0 | See section 5.8 |
| 57604 | File > Save As | MFC | |
| 57616 | File > Recent Map | MFC MRU | |
| 57665 | File > Exit | 0x4027d0 | |
| 59392 / 59393 | View > Toolbar / Status Bar | MFC | |
| 32772 | View > Zoom In/Out | 0x4027e0, which calls 0x484c20 | |
| 32773 | View > Mini Map | 0x402970 (frame), 0x405bb0 (view) | `CMiniMapWnd` |
| 32774 | View > Power Zones | 0x402850, which calls 0x484c00 (= game 0x480280) | |
| 32775 | Tools > Global AI Editor | 0x404190 | Dialog 132 |
| 32777 | Tools > Event Editor | 0x4028d0 | Pushes or pops the engine scene `SEvent` through 0x47d020 (= game 0x4709a0) with (0x1f, 3, 5) or (0x23, 0, 5) |
| 32771 | Tools > Auto Fill | 0x402a50 | Toggles `AEditor` auto-fill (0x415ad0) |
| 32812–32814 | Tools > Fill Cropland > with Pods / Spores / Weeds | 0x403070 / 0x403080 / 0x403090, which call 0x402fd0 with thing type 0x3f / 0x40 / 0x41 | |
| 32815 | Tools > Fill Cropland > clear all crops | 0x4030a0 | |
| 32778 | Settings > Place Starting Point | 0x404d80 | Toggles start-point mode (`AEditor`+0x330b0) |
| 32780 | Settings > Player Attributes | 0x4043d0 | Dialog 135 |
| 32783 | Settings > Map Attributes | 0x404b60 | Dialog 387 |
| 32781 | Settings > Map Preferences | 0x4038f0 | Dialog 136: the **defaults** for New Map, kept in the registry. Not in the manual |
| 32785 | Settings > Auto Scroll | 0x4029f0 | |
| 57664 | Help > About | 0x403880 | Dialog 100 |
| 32782 | Help > Help | 0x403a40 | WinHelp |

**Toolbar-only commands:**

- 32786 Delete selection (0x402b70, which calls 0x44df10 = game 0x44d960).
- The event-editor buttons:
  - 32797 / 32798 / 32808 / 32799 / 32809: Prev, Next, Add, Remove event; Toggle persistent.
  - 32800–32803: Prev, Next, Add, Remove condition.
  - 32804–32807: the same for actions.
  - Their handlers are 0x402ee0–0x402fc0.

**Hidden or unused commands:**

- 32776 (0x404780) opens dialog 138 "AI Goals" (Item Selected, Attack and Defend sliders). It isn't on any menu.
- 32779 maps to an empty handler (0x47d120).
- 32784 and 32788 ("Process Map") have tooltip strings but no menu item. Processing is reachable with Ctrl+P (see below).
- 32787 and 32789–32793 are leftover duplicate tooltip strings.

**Accelerators (table 128):**

- Ctrl+N, Ctrl+O and Ctrl+S.
- F6 and Shift+F6 (MFC next and previous pane; they do nothing here).

**Keys handled in the engine by `AEditor`** (`AEditor::OnKeyDown`, slot 0, 0x4156a0; arguments are virtual key, modifiers, ASCII):

- Ctrl+P runs Process Map (0x404ab0).
- Ctrl+Z toggles the power-zone view (0x484c00).
- Delete deletes the selection.
- Page Down and key code 4 set flags at +0x330e6 and +0x330e4; their purpose is unknown **(uncertain)**.
- 'M' is swallowed, presumably to block a game hotkey.
- All other keys go on to the engine scene; the event editor's Esc and its text entry arrive that way.

**Toolbar 128.** 16x15 buttons in the order:

New, Open, Save | Power zones (32774), Minimap (32773), Zoom (32772), Delete (32786), Auto fill (32771) | event buttons | condition buttons | action buttons

The images are in bitmap 128 (336x15, 21 images). The player combo is a `CStyleBar` (string 364, "Player selection list").

### Dialogs (control IDs)

- **100 About.**
- **130 New Map Setup:**
  - Size High 1003, Wide 1004;
  - fog radio buttons 1070–1072;
  - Terrain Type combo 1005;
  - Players Supported edit 1008 with spin 1009;
  - Recommended edit 1010 with spin 1011;
  - Map Name 1012.
- **136 Map Preferences.** The same as 130 without the name and fog fields.
- **387 Map Attributes:**
  - Size 1068 and 1069, read-only;
  - fog 1073–1075;
  - Supported 1063 with spin 1064;
  - Recommended 1062 with spin 1065;
  - Name 1060.
- **132 Global AI Settings.** Player combo 1014; trackbars 1056 (squad), 1057 (base defense), 1018 (scouting) and 1021 (AI influence).
- **138 AI Goals** (unused). Trackbars 1015 (Attack) and 1017 (Defend).
- **135 Player Attributes**, 8 rows:
  - a colour icon in each row (static 1098–1105 showing icons 423–430);
  - Type combos 1203, 1212, 1221, 1266, 1275, 1284 and 1293 (none for player 1, who is "Human");
  - four CGK-level combos per row (for example 1024 and 1089–1091);
  - four Available-Clans checks per row (for example 1025 and 1092–1094);
  - the combo items are in DLGINIT 135.
- **415 Creature Attributes.** Item 1085; HP 1083; maximum 1088; Veteran combo 1084 (strings 365–368: Rookie, Squaddie, Veteran, Hero).
- **418 Device Attributes.** Item 1090; HP 1093; maximum 1095.
- **386 Processing Map.** Progress bar 1058, polled by a timer (`CProcessDlg` at 0x408880; timer handler at 0x4089c0).
- **137 ReadMe** (a disclaimer) and **30721** (the MFC "New" type list).

### String tables

- **IDs 1–650: a copy of the game's own string table from an older build.** It has mission briefings and titles, unit descriptions, gizmo and device names and the multiplayer UI. Compared with retail:
  - 119 entries are identical;
  - 314 have different text under the same ID, because the table was renumbered;
  - 236 exist only in the editor.
- **IDs 338–368: editor strings.**
  - "Gizmo Selection", "With Gizmos", "Frame Only".
  - The palette names, from "Smart Palette" to "Crystal Cliffs" (342–357), plus 6 "Terrain Palette Placeholder" entries.
  - Veteran ranks.
- **IDs 32771–32809: command tooltips.**
- **IDs 57344 and up: MFC defaults.**
- Status-bar string 61204 is "Player %d".

### Bitmaps, icons and cursors

- **128:** the toolbar strip.
- **139–414: 32x32 palette thumbnails**, referenced by data (section 5.1):
  - `TRVA` thumbnails 139–413 are the terrain tiles;
  - `TRTH` thumbnails 340–414 are the objects: creatures 340–358 and 390, crops 359–361, obstacles 363–368, devices 369–383.
  - **None of these are in the retail exe.**
- **26567** (70x39) is of unknown purpose. **30994–30996** are MFC's standard bitmaps.
- **Icons 423–430:** the 8 player colours. Cursors 14 and 15 are MFC's.

---

## 4. Program structure

**MFC classes (from RTTI):**

| Class | Vtable |
|---|---|
| `CMapEditorApp` | 0x4d9f1c |
| `CMainFrame` | 0x4d99e4 |
| `CStyleBar` | 0x4d9ae4 |
| `CMapEditorDoc` | 0x4da224 |
| `CMapEditorView` | 0x4da3c4 |
| `CMapWnd` | 0x4da7e4 |
| `CMiniMapWnd` | 0x4da9dc |
| `CPaletteCtrl` | 0x4daccc |
| `CCreatureAttribsDlg` | |
| `CDeviceAttribDlg` | |
| `CGlobalAIDlg` | |
| `CMapAttribsDlg` | |
| `CMapPreferences` | |
| `CPlayerAttsDlg` | |
| `CProcessDlg` | |

`CMapEditorDoc::Serialize` is empty (0x41ca50). Loading and saving are done by `OnOpenDocument` and `OnSaveDocument` (vtable slots 31 and 32).

**Engine side.** The editor keeps the game's scene system, with its own small scene table at 0x47d1b0:

- Scene 1 = 0x555d50, a `SGame`-derived mission scene: the map view.
- Scene 3 = 0x5993b8, `SEvent`.
- `AEditor` (vtable 0x4db59c) sits inside scene 1 at +0x3c (0x555d8c):
  - it turns mouse and keys into tool actions;
  - it owns two "palsets" (`ATerrainPalset`, 0x4db5ac): +0xc for the terrain palettes (`TRPL` 1000) and +0x48 for creatures and devices (`TRPL` 2000);
  - it owns the start-point painter at +0x330ac (`ATerrainStartPoint`, art ID 4600 = `PLYRSTRT.MHK`).
- Editor-only paint-object classes all derive from `APaintObject`, which also exists in the game (0x5045bc):
  - `ATerrainContainer`, `ATerrainPalette`, `ATerrainTile`, `ATerrainObstacle`, `ATerrainThing`, `ATerrainCreature`, `ATerrainStartPoint`, `ATerrainGizmoSelector`;
  - `ADevicePlaceholder` exists in both.

---

## 5. The tools and how they work

### 5.1 Terrain painting and automatic transitions

**Grid.** The world (editor 0x54c380, game 0x55dd60) keeps a **96x96 grid of u16 tile-image IDs at +8**:

- game: grid at 0x55dd68, read by `World::GetTile` (0x434080);
- editor: grid at 0x54c388;
- each cell is 64x64 pixels;
- out of range reads as image 0x13e;
- the map size is stored in 32-pixel units, as two bytes: editor 0x555394/5, game world+0x9054/5.

**Data.** All of it is in `TERRAIN.MHK` and identical in retail.

- **`TRID` 1000–1221** (222 tile *kinds*): the class of each kind, 0–23.
- **`TRST` 1000–1221:** the **four corner terrain types** of each kind, in the order top-left, top-right, bottom-left, bottom-right. Each is 0–13.
- **`TRVA` 1000–1221:** the palette thumbnail ID, a count *n*, then *n* pairs of (cumulative percent, image index 0–417), then 100.
  - These are the random visual variants.
  - The game picks one with 0x455a30, using the game's deterministic RNG 0x471320. The editor uses `rand`.
- **`TRPL`:** palette definitions. Each is a count, a 0, then items of (default flag, ID, category), all big-endian u16.
  - Categories: 0 = a link to another palette; 1 = a menu entry; 2 = a tile kind; 3 = a `TRTH` obstacle; 4 = a `TRTH` creature; 5 = a `TRTH` device; 6 = a `TRTH` crop.
  - `TRPL` 1000 is the 16-entry drop-down, in the same order as strings 342–357. Through the links 1001–1044 it reaches:
    - the Smart Palette, 1022 (21 items, matching help image `bm1`);
    - the specific palettes 1005–1018 and 1023.
  - `TRPL` 2000 holds Creatures (1020, 21 items) and Devices (1021, 16 items).
- **`TRTH` 1000–1046:** (thumbnail, thing type) for obstacles, creatures, devices and crops. The type IDs match the event editor's numbering; for example, crops are 63, 64 and 65.

**Corner types.** These were identified from the uniform tiles and the cropland rules:

| Value | Terrain |
|---|---|
| 0 | Crystal |
| 1 | Crushed crystal (can take cropland) |
| 2 | Crystal canyon (impassable) |
| 3 | Tar |
| 4 | Hardened tar (can take cropland) |
| 5 | Lava |
| 6 | Sand |
| 7 | Roots (can take cropland) |
| 8 | Hot mud |
| 9 | Cropland edge |
| 10 | Cropland interior |
| 11 | Canyon wall **(uncertain)** |
| 12 | Crystal cliff |
| 13 | Cliff detail piece **(uncertain)** |
| 14 | In lookups only: "keep the existing corner" |

**Tile classes (`TRID`):**

- 0–10 and 12 are the uniform terrains above.
- 11 and 13 are wall and detail pieces.
- 14 is a mixed border between the base types 0, 3 and 6.
- 15–20 are base/subtype borders: 0/1, 0/2, 3/4, 3/5, 6/7, 6/8.
- 21–23 are subtype/cropland borders: 1/9, 4/9, 7/9.

**Compatibility table** (0x440aa0, `Allowed(newType, existingClass)`; the inner jump tables were checked against the disassembly):

| Existing class | May be painted with |
|---|---|
| 0 | 0, 1, 2, 3, 6, 11, 12, 14, 15, 16 |
| 1 | 0, 1, 9, 15, 21 |
| 2 | 0, 2, 11, 16 |
| 3 | 0, 3, 4, 5, 6, 14, 17, 18 |
| 4 | 3, 4, 9, 17, 22 |
| 5 | 3, 5, 18 |
| 6 | 0, 3, 6, 7, 8, 14, 19, 20 |
| 7 | 6, 7, 9, 19, 23 |
| 8 | 6, 8, 20 |
| 9 | 1, 4, 7, 9, 10, 21, 22, 23 |
| 10 | 9, 10 |
| 11 | 0, 2, 11 |
| 12 | 12 |
| 13 | nothing |
| 14 | 0, 3, 6, 14 |
| 15 / 16 / 17 / 18 / 19 / 20 | the pair of types it borders, plus itself: {0, 1, 15}, {0, 2, 16}, {3, 4, 17}, {3, 5, 18}, {6, 7, 19}, {6, 8, 20} |
| 21 / 22 / 23 | {1, 9, 21}, {4, 9, 22}, {7, 9, 23} |

**The painter object** is set up by 0x43c450, then 0x43c570 (reads `TRID`), 0x43c600 (reads `TRST`) and 0x43c7b0. Its fields:

| Offset | Contents |
|---|---|
| +0x000 | Class of each kind |
| +0x1bc | Class of each image (418 entries) |
| +0x510 | A 15^4 = 50,625-entry table from (c0, c1, c2, c3) to a tile-kind object. The index is ((c0·15 + c1)·15 + c2)·15 + c3 |
| +0x31c14 | Corners of each image |
| +0x32924 | Corners of each kind |
| +0x33014 | Retry list |

**Smart-palette stroke.** The flow from `AEditor` 0x414de0 through 0x43ca70:

1. The mouse is converted to a 64-pixel cell, and nothing happens until the cell changes.
   - Start-point mode → the start-point object's `Paint`.
   - Creatures/devices palette active → that palette's `Paint`.
   - Smart palette → 0x43ca70.
   - Any other terrain palette → the chosen tile is stamped **as is**, with no transitions, by `ATerrainTile` slot 5 (`PaintAt`, editor 0x452f50 = game 0x4559e0).
2. 0x43ca70 dispatches on the selected kind's class:
   - 0 (crystal), depending on the target cell:
     - over canyon classes 2, 11 or 16 → 0x43e6c0;
     - over cliffs (12 or 13) → 0x43daa0, which does nothing and reports success **(uncertain whether intentional)**;
     - otherwise the generic painter, 0x43cc70.
   - 1, 4 or 7 (cropland-capable subtypes) → 0x43dab0.
   - 2 (canyon) → 0x43dbc0.
   - 3, 5, 6 or 8 → 0x43cc70.
   - 9 (cropland) → 0x43ea20.
   - 12 (cliff) → 0x43cd70.
   - 14 → "Tile not implemented yet".
3. **The generic painter, 0x43cc70:**
   1. Check the whole **3x3 neighbourhood** with the compatibility table (0x440720). If it fails, show "You cannot paint that there".
   2. **Propagate corners** (0x43f730, which calls 0x43f850 for each of the 8 neighbours). Each neighbour's corners that touch the painted cell are rewritten to the new type. Border classes 15–23 are first reduced to their base or subtype.
   3. When a neighbour on the far side can't accept the change, the propagation reaches one cell further and **recurses** from any neighbour that changed. This ripple keeps every shared corner consistent.
   4. Re-validate (0x440510).
   5. Place (0x440030):
      - stamp the selected kind in the cell;
      - for each neighbour, look up the kind whose corners are (new corner where shared, 14 elsewhere) with 0x4403c0, using the 15^4 table, and stamp it;
      - if no kind matches, show "Couldn't find tile!" and put the cell on the retry list.
   6. The status messages are printed through 0x47d060.
4. **Special cases:**
   - **Canyons** (0x43dbc0, 0x43de60, 0x43deb0, 0x43e140, 0x43e300, 0x43e5c0): the canyon's south wall (corner 11) is drawn down extra rows for depth. These functions recurse up and down to fix wall tiles and use dedicated wall kinds (painter +0x590 and +0x7a4).
   - **Cliffs** (0x43cd70, 0x43ceb0, 0x43d2e0, 0x43d640): they refuse layouts where cliff corners would meet diagonally, and fill the 8 surrounding cells from tuples such as (14,14,14,12) or (12,12,14,14).
   - **Cropland** (0x43ea20): first the generic paint, then 0x43eb90 checks the four diagonal neighbours (0x43ed30, 0x43ef30, 0x43f130, 0x43f330) and promotes corners to **interior (10)** where cropland surrounds them.
   - **Subtypes next to cropland** (0x43dab0, then 0x43f530): interior corners in the 3x3 area are first turned back into edge corners (9). This is the source of the "Couldn't find fertile ground tile" message.
5. **Auto Fill** (`AEditor` 0x414a80, on mouse-up of a drag rectangle):
   - with the Smart Palette, 0x43c860 runs 0x43ca70 for every 64-pixel cell in the rectangle;
   - otherwise the tile is stamped across it, apparently in 25-pixel steps **(uncertain)**.

**What this means for us.**

- The data is in retail and the tile-stamping primitive is in the game.
- The smart painter (step 3 above and its special cases) must be **written by us**. The best route is a clean re-implementation from the tables, rather than a port of the decompiled code:
  1. keep the corners of every cell;
  2. paint corner values;
  3. look up the tile kind from the 15^4 table;
  4. apply the compatibility table and the canyon, cliff and cropland rules.
- Canyons and cliffs are the hard part.

### 5.2 Obstacles, crops and ruins

These are `TRTH` items in the Smart Palette: tile category 3 for obstacles and ruins, 6 for crops. They are placed as world "things".

- **Placement rules.** Per the manual: obstacles on their matching subtype, ruins on open terrain, crops on cropland. The editor checks them with its paint-object classes; the game's own placement check is `World_ValidatePlacement` (0x4517e0) **(uncertain which checks the editor performs)**.
- **Fill Cropland** (0x402fd0) visits every 32-pixel cell. Where the world terrain type (0x430ef0 = game **0x42ea40**) is 2 (soil a crop can grow on), it creates a thing with 0x4484c0 (≈ game **0x446db0**). The arguments are 9, the thing type (pod 0x3f, spore 0x40 or weed 0x41), x·32, y·32, 0, 1 and 0xc01.
- **Clear all crops** (0x4030a0) walks the thing registry at `[0x599414]`.

### 5.3 Creatures, devices and gizmos

- The **Creatures/Devices palettes** are `TRPL` 2000, and the selected player owns what is placed.
- Placement goes through the engine's thing creation, so units appear exactly as in the game, power LEDs included.
- **Gizmos** are the weapon components: `GIZMO*.MHK` art plus the gizmo names in the editor's event tables (from HAMMER to MISSILE, with "(h)" marking head slots).
  - Creatures that carry gizmos open `ATerrainGizmoSelector`. It reuses the game's Gene Lab UI class `AGeneLabInterface`, which is in both executables, in a "Select Components" window.
  - Available gizmos follow the player's CGK.
- **Delete** uses game 0x44d960 (editor 0x44df10).
- **Picking** uses `World_PickAt` (game 0x44c530 = editor 0x44cf60).
- **Right-clicking** a selected creature or device opens dialog 415 or 418 to edit hit points and veteran rank.

### 5.4 Players and start positions

- **Player Attributes** (8 players): CGK level 0–5 per clan, available clans, type.
- When a new map is created (0x403c90), for each supported player:
  - 4 CGK values are set to 2, 4 clan flags to 1 and 4 more values to 0 **(the meaning of those defaults is uncertain)**;
  - player 1 is set to type 1;
  - the other players below Recommended get type 0, and the rest type 3.
  - That suggests 0 = Either and 3 = None **(uncertain)**.
  - The per-player records are 0x34 bytes, starting at document+0x9068.
- **Start points.** In Place Starting Point mode, a click places the current player's marker (`ATerrainStartPoint`, art from `PLYRSTRT.MHK` 4600, which isn't in retail). It is a large coloured circle; see help image `bm50`.
- The **toolbar player combo** chooses which player new objects belong to.
- The game's console has `create`, `player`, `supports` and `recommended` commands for the same data.

### 5.5 Resources and power

There is no "resource" placement apart from crops. Pods, spores and weeds are the power source; Refineries draw from crops next to them, and relays extend the zone.

The **power-zone view** is the game's 0x480280. In the retail game it is the **O** key; this is a correction to `re/hotkeys.md`.

### 5.6 Events and mission scripting

- Covered in section 2.
- The UI is the **engine scene `SEvent`**, drawn with the game's widget classes (`WText`, `WTextButton`, `WNumberBar` and so on).
- The event model (`AEventRegistry`, `ACondition_*`, `AAction_*`) is **shared with the game**, which runs the events. Every class has the same vtable shape in both, a third to a half of the slots match exactly, and the editor's panel builders have game equivalents. For example, the TIMED panel ("Game Cycles") is editor 0x418a90 and game 0x412d60.
- **Command scripts** are recorded command streams; the game console has `stop recording`. They are played by the COMMAND SCRIPT action; the `.SKT` files in `DATA` are examples.

### 5.7 Map properties

- **Name, fog mode, players supported and recommended:** dialogs 130, 387 and 136.
- **Size.** 32–96 per side. The engine grid holds 96x96 cells of 64 pixels (0x60 bounds checks everywhere) and stores the size as bytes in 32-pixel units. So the dialog's numbers are **64-pixel cells** **(likely)**. For comparison, the retail "4 Corners" map is 176x184 32-pixel units, which is 88x92 cells.
- **Starting terrain.** Sand, crystal or tar fills the map.
- **Visible bounds** can only be changed by the VISIBLE BOUNDS event action.

### 5.8 Saving, processing and testing

`OnSaveDocument` (0x4040f0):

1. If the event editor is on top, pop it (0x47d340 = game 0x471690).
2. Run **Process Map** (0x404ab0, dialog 386). This starts the movement-guide (pathfinding) build:
   - the guide object is at world+0x1c;
   - the build starts at 0x467700 (≈ game 0x4a4eb0) and steps at 0x467a60 (= game 0x4a5200);
   - progress is the guide's +0x24 divided by the point count (editor `[0x53d9b8]`, game `[0x592350]`).
3. Call **scene 1's message slot 0x58 with (1, path)**: 0x47d1d0 (= game 0x471560), then 0x484d50 (= game `SGame` 0x4803e0).
   - Message 1 stores the path at `SGame`+0x300 and sets +0x2fe, so the engine's own save runs on the next frame. That save is game 0x480ac0, the same routine the console's `save map` uses.
   - Message 0 is the load counterpart (path at +0x1fe, flag at +0x1fc).

So **the map file is written by the game engine's own save code**. Whether retail's `save map` produces a byte-identical `.WBM` is for the format work to confirm.

There is **no "test map" command**. The workflow is to save into `DATA\MAPS` and play it in the game.

The string "The map format has changed... You must reprocess this map" (ID 2) is in both executables, so the game can also detect maps that need reprocessing.

### 5.9 View and navigation

- `CMapWnd` shows the engine scene.
- `CMiniMapWnd` is a separate window.
- Zoom (0x484c20) and scroll bars.
- Auto-scroll at the window edges (Settings menu).

### 5.10 Validation

There is no map validator. All checking is local:

- **Paint rules:** "You cannot paint that there", "Invalid combination", "Couldn't find tile!", "Couldn't find fertile ground tile".
- **Dialog ranges:** size 32–96; players 2–8; Recommended no more than Supported (DDV).
- **Player selection:** a "No Player Selected!" string exists.
- **The required processing pass** on save.
- **Advice the manual gives but nothing enforces:** shamans, start points, powered devices.

An in-game editor should add a real validator: see section 7.

---

## 6. Shared engine code

**Method.**

1. Every editor function was hashed with absolute addresses and call targets masked, and the hashes matched against the game export (`re/ghidra-export/functions.tsv`).
2. A second pass matched what was left by mnemonic sequence: exactly where possible, otherwise by `difflib` similarity of at least 0.85.
3. RTTI vtables were recovered in both executables (about 300 each) and compared slot by slot.

**Results (code below 0x4a5000, i.e. without MFC and the C runtime):**

| | Functions | Bytes |
|---|---|---|
| Total | 2,942 | 545,440 |
| Exact match | 2,097 (1,506 unique) | 316,808 |
| Close match | 392 | 144,539 |
| No match | 453 | 84,093 |

The largest unmatched blocks are the editor's own code:

| Range | Size | Contents |
|---|---|---|
| 0x43bfd0–0x441439 | 19 KB | The terrain painter |
| 0x401100–0x405247 and 0x405950–0x407f66 | 15 KB | The MFC application, views and dialogs |
| 0x414010–0x4162fd | 6 KB | `AEditor` |
| 0x4531b0–0x4548d4 | 3 KB | The terrain paint objects |
| 0x409ac0–0x40af6c | | Unidentified |

**Shared classes** (present in both with the same vtable shape):

- `AThing`, `ADevice`, `ACreature`, `ACreatureBody`, `AShaman`.
- Every device class: battery, refinery, relay, turret, warp, mine, radar, revitalizer, gene lab, howitzer, collector, electric tower.
- `AMushroom` (crops), `AObstacle`, `APlayer`, `AIComputerPlayer`, `AISquad`, `AIRetina`, `AIMovementGuide`, `AITerrainAnalysis`.
- `AEffect` and all its subclasses.
- `AEventRegistry` and every `ACondition_*` and `AAction_*`.
- `SGame`, `SEvent`, `Scene`, `Widget` and the `W*` widgets.
- The Mohawk I/O classes: `tMohawkInput`, `tMohawkOutput`, `IResourceStream`, `tFile`, `INamedResource`.
- `ATerrainTile`, `APaintObject`, `AGrid`, `AGameClock`, `AGeneLab`, `AGeneLabInterface`.

**Selected correspondences** (editor = game):

| Function | Editor | Game |
|---|---|---|
| Terrain loader (`TERRAIN.MHK`, `TRID`) | 0x4300f0 | 0x42db60 |
| `ATerrainTile` constructor | 0x452a20 | 0x4554d0 |
| `ATerrainTile` load (`TRVA`) | 0x452b60 | 0x455600 |
| `ATerrainTile::PaintAt` | 0x452f50 | 0x4559e0 |
| Register a tile class | 0x453200 | 0x455c40 |
| Terrain type at a 32-pixel cell | 0x430ef0 | 0x42ea40 |
| Create a thing | 0x4484c0 | ≈ 0x446db0 |
| Delete the selection | 0x44df10 | 0x44d960 |
| Pick | 0x44cf60 | 0x44c530 |
| `APlayer_CanBuild` | 0x436ed0 | 0x437b50 |
| Request a scene change | 0x47d020 | 0x4709a0 |
| Send a scene message | 0x47d1d0 | 0x471560 |
| Pop a scene | 0x47d340 | 0x471690 |
| `SetResolution` | 0x47cd10 | ≈ 0x46fa80 |
| `SEvent` constructor | 0x47f9d0 | 0x478280 |
| Event panel header | 0x480850 | 0x478f10 |
| Event panel "Condn." | 0x480a90 | 0x479150 |
| Movement guide: start | 0x467700 | ≈ 0x4a4eb0 |
| Movement guide: step | 0x467a60 | 0x4a5200 |
| Movement guide: console | 0x4689d0 | 0x4a60b0 |
| Terrain obstacle analysis | 0x471910 | 0x4a8040 |
| `terrain.hoo` write / read | 0x430d80 / 0x430e10 | 0x42e8d0 / 0x42e960 |
| `SGame` message handler | 0x484d50 | 0x4803e0 |
| `load map` | 0x4895e0 | 0x4855a0 |
| Power-zone toggle | 0x484c00 | 0x480280 |

---

## 7. Proposed feature list for the in-game editor

Each feature is marked **[G]** where it can call the game's own code (with our glue), **[O]** where we have to write it, or **[G+O]** where it is both. Entry is from the main menu, the same way as the Settings page (`src/menu.cpp`: replace `SMissionMenu` Init and OnCommand, and add an "Editor" text button).

### Level 1: minimum useful (edit an existing map's terrain and save it)

1. **[G+O]** **Editor mode.**
   - Load a map into `SGame` through the normal skirmish path, or scene message 0 with a path.
   - Freeze the simulation and AI (update pause: the console's `update pause` is 0x485360).
   - Take over mouse and keys in `SGame` the way `controls.cpp` does.
2. **[G]** Camera, zoom and minimap: already ours and the game's.
3. **[O]** **Raw tile stamping** from a tile picker, using the `TRPL` palettes:
   - write the grid cell with game `ATerrainTile::PaintAt` (0x4559e0), or directly to `0x55dd68[y][x]`;
   - mark the cells for redraw with 0x42f210.
   - Thumbnails must be drawn from the tile images (`TERRAIN.MHK` `tBMH` 9000), because the editor's 32x32 bitmaps aren't in retail.
4. **[G]** **Process and save:**
   - start the guide build with game 0x4a8030 (this = `[0x55dd64]`);
   - step it until `guide+0x24 >= [0x592350]` (0x4a5200 runs in the world update);
   - then save with scene message (2?, 1, path). The `SGame` scene ID is 2 (0x567ef8) **(verify)**.
   - The format work should confirm the output is a valid `.WBM`.
5. **[O]** **Test:** save to `DATA\MAPS` and start a skirmish on it.

### Level 2: practical editor

6. **[O]** **Smart painting with automatic transitions.** Re-implement section 5.1: corner model, 15^4 lookup, compatibility table, propagation, and the canyon, cliff and cropland rules, using `TRST`, `TRID` and `TRVA`.
7. **[O]** Brush sizes and **auto-fill rectangle.** Auto-fill is just the smart painter applied cell by cell.
8. **[O]** Undo and redo: the original has none, and a grid snapshot is enough.
9. **[G+O]** **Place obstacles, ruins and crops:**
   - create things with game 0x446db0 and check them with `World_ValidatePlacement` (0x4517e0);
   - **Fill cropland** is a loop over 0x42ea40 and 0x446db0;
   - clear crops with the thing registry.
10. **[G+O]** **Place creatures and devices** for the selected player with game thing creation. Show power zones with game 0x480280.
11. **[G]** Select, move and delete: `World_PickAt`, `World_Select`, delete 0x44d960.
12. **[O]** **Start points.** We draw our own marker (`PLYRSTRT.MHK` isn't in retail) and write the player's start position. The field is to be found with the format work.
13. **[O]** **Validator** (the original had none):
    - a start point for every supported player;
    - each player has a shaman, or a gene lab plus psi;
    - devices are powered;
    - cropland isn't one cell from the edge;
    - every start point can be reached (with the game's guide graph);
    - Recommended is no more than Supported;
    - every player who owns units isn't of type None.

### Level 3: complete (matches or exceeds the original)

14. **[O, with G hooks]** **Map and player settings:**
    - name, fog mode, Supported and Recommended, CGK per clan, available clans, type;
    - these can be set through the game console handlers (`supports` 0x482900, `recommended` 0x482940, `fog` 0x485630, `cgk` 0x485690, `create` 0x482e70) or by writing the fields directly once they are mapped.
15. **[G+O]** **Global AI settings:** squad, base, scouting, influence. The console's `aggression` (0x4825e0) and the AI player fields.
16. **[G+O]** **Unit attributes:** hit points, veteran rank and gizmos. Gizmo choice through the game's Gene Lab interface (`AGeneLabInterface`) or a simple list.
17. **[G]** **Event editor:**
    - try pushing the retail `SEvent` scene with game 0x4709a0(0x1f, 0x16, 5) and popping it with (0x23, 0, 5);
    - its toolbar commands (next or previous, add or remove for events, conditions and actions, and persistent) would need our own buttons or keys, wired to the `SEvent` methods behind editor commands 32797–32809. The editor handlers (0x402ee0–0x402fc0) forward to them; the game addresses are still to be mapped.
    - If `SEvent` turns out not to work in retail, use **[O]**: our own UI over `AEventRegistry`.
18. **[O]** **New map:** size, base terrain and player count.
    - The editor's `OnNewDocument` (0x403c90) calls 0x433810 (= game 0x432150) and 0x434df0 (= game 0x433d00), which look like "world reset" and "fill" **(verify)**.
19. **[O]** Copy and paste of terrain regions, symmetry or mirror tools, and random placement of decorations. These are modern additions.
20. **[O]** A command-script recorder or player UI. Lowest priority.

---

## 8. Open questions

- Is the `SEvent` scene in retail complete and safe to push (ID 0x16)? It has never been reachable in retail.
- Do the game's `save map` and `SGame` message-1 save produce a `.WBM` identical to the editor's? This is for the format work.
- Which player-record field holds the start point? Which type number means Human, Computer, Either or None?
- The exact meaning of corner values 10, 11 and 13. They are inferred here from how the code treats them.
- Does painting crystal over cliffs really do nothing (0x43daa0 just returns 1)?
- The auto-fill step size with non-smart palettes (25 pixels?), and the `AEditor` flags set by Page Down and key 4.
- `DHUER.MHK` exists only in the editor's data (its purpose wasn't checked). The editor's art (`tBMH`/`tPAL`) uses a remapped palette and an older image set (424 terrain images against retail's 425–428). Tile image IDs above 417 in retail are therefore unknown to the editor's tables.
