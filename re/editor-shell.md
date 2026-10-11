# The in-game map editor, Stage 1: the shell

How WarBreeds HD's map editor opens a map, takes over input, paints, undoes, saves and tests, and which engine code it uses. The painters are separate: Stage 1 ships a raw tile stamper, and the smart painter is described in `re/terrain-painter.md`. Background research: `re/editor-original.md` (the 1998 editor, §7 the plan) and `re/wbm-format.md` (the file format). Code: `src/editor/` and the map pages in `src/menu.cpp`.

Confidence marks are as in the other notes: **H** = read from the code and confirmed in the running game (headless tests), **M** = from the code only.

---

## 1. What the player sees

- **Main menu:** a "Map Editor" item under "Settings". Quit moves down another row.
- **Map list page:** "New Map", then every `DATA\MAPS\*.WBM` with the name from its header and its file name, for example "4 Corners (MPMAP1)". Shipped maps come first in number order, then the player's maps by name. The list scrolls like a long settings tab (11 rows, mouse wheel, "More above/below"). "Back" returns. The hint line says that built-in maps are always saved under a new name.
- **New Map page:**
  - Size: presets 32×32, 48×48, 64×64 and 96×96 cells, or Width and Height in steps of 8 (32–96);
  - Terrain: the base terrain, from the palette;
  - Players: 2–8;
  - "Create Map" and "Back". Click steps a value forwards, right-click backwards.
- **Editor:** the map in the mission view, with the simulation stopped, no fog and the minimap always on. The camera, edge scrolling, arrow keys, smooth zoom and minimap clicks work as in a mission. A tools panel sits at the bottom left:
  - the terrain palette (12 rows, pages "< Prev / Next >" when the painter has more);
  - Brush 1, 2, 3, 5 (square, in cells);
  - Undo / Redo (also Ctrl+Z / Ctrl+Y);
  - Save As (type a name, Enter saves, Esc cancels), Test Map, Exit;
  - a status line.
- **Mouse:** left-drag paints; Shift+left-drag fills the dragged rectangle; right-click picks the terrain under the cursor. Clicks on the minimap keep their stock meaning (the camera jumps there).
- **Keys:** only the arrow keys reach the game. Unit commands, the Esc menu, the console and the game's other hotkeys are off in the editor. Alt+F4, Alt+Enter, F2 and the numpad zoom keys still work.
- **Exit** with unsaved changes asks for a second click, then returns to the map list.
- **Test Map** saves first if the map has unsaved changes, then leaves for the skirmish setup with the map selected and starts the skirmish.

## 2. Opening a map **H**

The editor uses the single-player load path that a saved game takes from the main menu:

1. `SGame +0x460` = 1: the console's "update pause" (`SGame::Cmd_UpdatePause 0x485360`). Only the SGame constructor (`0x47a700`) clears it, so it holds through loading. `SGame::IsTurnDue 0x47d090` then never lets a turn run: no AI, no creature movement, no tile animation, no pathing work (`AGrid::Tick 0x42dcf0` runs inside a turn).
2. `Scene_Send 0x471560(2, 0, path)`: `SGame::SetParam 0x4803e0` code 0 → `SetMissionName 0x480540` (path at `+0x1fe`, load pending `+0x1fc`).
3. `Scene_Send(2, 2, 0)`: not a skirmish (`SGame +0x82`). At load stage 8 the local player is then the first player in the file, and `Game_ApplyOptions`/`SetupSkirmish` do not run.
4. `Game_StartCommandModel 0x470780(0x32)`: single-player command model. It queues the loading screen (SProgress, scene 0x25) as the new root. Stage 8 of `SGame::LoadMapStage 0x480610` clears `+0x1fc`.

When SGame is the root again with a local player, the editor starts:

- the selection is cleared (`AThingRegistry 0x44b680(0)`);
- the tile animator is stopped (`TileAnimator::Stop 0x455420` on `0x562568`), which puts the lava cells back to their base tiles;
- the local player's fog flag (`APlayer +0x484c`) is cleared, and the old value is kept;
- the panel is built.

Every frame it keeps `+0x460` = 1 and the minimap shown: `Minimap 0x5517b0 +0x962c` = 0 and `+0x9624` = 1. `Minimap::Tick 0x433520` counts `+0x962c` up while the player has no radar building, and `IsShown 0x433e10` needs it below 20.

When SGame stops being the root, the editor forgets its panel (Scene::Close deleted the widgets with the scene's own) and clears `+0x460`, so the next mission runs normally.

## 3. The tools panel **H**

- The panel is built from the game's own text buttons: `Scene::AddTextButton 0x495cb0` on SGame, style 25 (WBMd14). They are WTextButton2, vtable `0x50925c`.
- SGame draws its widgets after the interface overlay, so they land in WarBreeds HD's interface layer (`src/layers.cpp`). The renderer anchors that element to the bottom-left corner at the interface scale, so it keeps one size at every zoom and maps clicks back to frame pixels. That made the game's widgets the more reliable choice over drawing in the Direct3D 11 layer: hit-testing, scaling and anchoring come free.
- Each button gets a copy of the WTextButton2 vtable with its own Draw (slot 1):
  - the title's Draw first fills the panel rectangle with the palette's darkest index, straight into the interface port (`layers::InterfacePixelAddress`), so the text has a background;
  - Draw does the hover highlight;
  - the selected palette entry and brush are "lit". Style `0x1b` is yellow only in the menus' palette, so while a lit button draws, text colour 1 (`0x550910[1] +4` = `0x80000000 | palette index`) points at the mission palette's nearest yellow.
- Positions are set every frame: the frame size changes with the smooth zoom.

## 4. Input **H**

The editor chains SGame's vtable slots after `controls.cpp`:

| Slot | Function | In the editor |
|---|---|---|
| `0x1c` LButtonDown | `0x47d500` | Panel → tool. Minimap → stock. Info panel → ignored. Otherwise a stroke starts (Shift: a fill rectangle) |
| `0x20` LButtonUp | controls → `0x47e430` | Ends the stroke (one undo record). Stock only while dragging on the minimap |
| `0x24` / `0x28` RButtonDown / Up | `0x47e6a0` / controls → `0x47e6f0` | Down = pick; no orders |
| `0x2c` MouseMove | stock | Stock first (edge scrolling, minimap drag), then paints along the line from the last cell |
| `0x30` LButtonDblClk | `0x47eea0` | Same as LButtonDown |

- Mouse positions come in frame pixels; world = SGame `0x47ec60` / `0x47ec80`, cell = world >> 6.
- The game polls the cursor once a frame (`FrameTick 0x46f400` → `Input_MouseMove 0x474230`), so a fast drag is joined up cell by cell.
- Keys are filtered in the window procedure (`editor::OnMessage`): WM_KEYDOWN, WM_KEYUP and WM_CHAR never reach the game, except the arrow keys. Ctrl and Shift are tracked from the messages as well as GetKeyState, so posted test input works.

## 5. Painting and undo

- **Grid:** `AGrid 0x55dd60 +8` (`0x55dd68`), u16 tile id per 64 px cell, 96×96, row-major. The map size is `0x566db4/5` (32 px tiles) / 2.
- **Redraw** after every change:
  - `AGrid::MarkAllCellsDirty 0x42f210(1, 0)`, because `DrawTerrain` only blits dirty cells;
  - `Minimap::Refresh 0x433820`.
- **Undo:**
  - Each stroke or fill snapshots the grid (18 KB) when it starts.
  - When it ends, the cells whose tile changed become one record (x, y, before, after).
  - Undo writes "before", redo writes "after". Up to 200 records.
  - "Dirty" (the * in the title) compares the undo depth with the depth at the last save.

### The painter interface (`src/editor/painter.h`)

```cpp
class Grid {                                  // the in-game one reads and writes AGrid
    virtual int Width() const;                // map size in cells
    virtual int Height() const;
    virtual uint16_t Get(int x, int y) const;
    virtual void Set(int x, int y, uint16_t tile);
};
struct CellChange { int16_t x, y; uint16_t before, after; };
using Changes = std::vector<CellChange>;
class Painter {
    virtual int TypeCount() const;            // the palette
    virtual std::string TypeName(int type) const;
    virtual bool Paint(Grid&, int type, int size, int x, int y, Changes& changed);
    virtual bool Fill(Grid&, int type, int x0, int y0, int x1, int y1, Changes& changed);
    virtual int Pick(const Grid&, int x, int y);   // may add a palette entry
    virtual std::string Message() const;           // why the last paint was refused
};
```

- The brush of `size` cells (1, 2, 3, 5) at the cursor cell covers `x-(size-1)/2 .. x+size/2` (`BrushFirst`/`BrushLast`), the same in y. In "radius" terms, odd sizes have radius (size-1)/2 around the cell, and size 2 takes the cursor cell and its right and lower neighbours.
- `Fill` takes any two opposite corners and clips them to the map.
- A painter may rewrite neighbours (transitions) and reports every cell it writes. The shell doesn't rely on that report for undo; it diffs the grid.
- **RawPainter** (`raw_paint.*`) is Stage 1's stand-in:
  - Its palette is the uniform terrains: Sand, Crystal, Tar, Roots, Crushed Crystal, Hardened Tar, Cropland, Hot Mud, Lava, Crystal Canyon, Crystal Cliff.
  - Each stamps the lowest tile id of its TRID class, read from the game's own table at `AGrid +0x905c` (`0x566dbc`, 418 entries, filled from TERRAIN.MHK at load).
  - The picker maps a uniform variant to its terrain. Anything else becomes a "Tile n" entry.
- The smart painter (`terrain_paint.*`, `SmartPainter`) implements the same interface.

## 6. Saving **H**

1. **Name:**
   - up to 8 characters (A–Z, 0–9, `_`, `-`), saved as `DATA\MAPS\<NAME>.WBM`;
   - the 19 shipped names (MPMAP1–18, MPMAP25) are refused;
   - another existing file needs a second Enter;
   - the map's own file is replaced without asking.
2. **Lava animation:** `AGrid::BuildTileAnimList 0x42ff30` then `TileAnimator::Stop`. The animator keeps the cells it animates and puts their base ids back when stopped, which both the guide build and the saver do; a stale list would turn painted cells back into lava.
3. **Map processing** (the 1998 editor's "Processing Map..."):
   - `AITerrainAnalysis::Rebuild 0x4a8030` on `[0x55dd64]` regenerates the obstacle map and the guide points;
   - then `AIMovementGuide::ConnectStep 0x4a5200` (guide = analysis `+0x1c`) runs until `linkI` (`+0x24`) reaches the point count `[0x592350]`. In the game it runs 200 pairs per turn; the editor runs it in 40 ms slices per frame with a percentage in the status line. About 60 ms for MPMAP1's 318 points.
   - ConnectStep finishes (`PruneRedundant 0x4a5990`, `FillLookupAlongLinks 0x4a5f00`, "Map processed") on the call after the last pair. When the pair count is a multiple of 200 that call never comes, so the editor makes those two calls itself.
4. **Save:**
   - path into `SGame +0x88`, `+0x86` = 1;
   - the next `SGame::Tick` calls `SGame::SaveGame 0x480be0` (call at `0x47cfe4`), which runs `SerializeAllStages 0x4812c0`. That is the game's own writer, the same one the console's "save map" uses.
   - The editor hooks that call to put the fog flag back for the write and keep its tick out of the way while `SaveGame`'s own `ForceRedraw` runs a frame.
   - It also hooks the `StoreCameraInPlayer 0x481260` call (at `0x480da9`): that would store the editor's view centre as player 0's start cell.
5. **What changes in a saved map apart from the terrain:**
   - `SaveGame` names the map after its file (`MapInfo::SetName`);
   - the guide and regions are rebuilt;
   - the world tick counter and the AI state are whatever the paused simulation holds;
   - player colours are written in the retail numbering. At load, the game converts the 1998 editor's colours (238, 230 … for players 0, 1 …) with `0x471a80`: a colour with `(c − 0xb7) % 8 ≠ 0` gains 9. So a re-saved shipped map stores 247, 239 … Triggers are converted the same way (17 other callers), and maps saved by the game load unchanged.
6. **Test Map:** after the save, the editor leaves for the hub (`QueueSceneChange 0x4709a0(0x21, 0x10)`), then:
   - when the main menu is up, it does what its Skirmish item does: `Scene_Send(5, 1, 0)`, `QueueSceneChange(0x21, 5)`;
   - on the skirmish setup, `Scene_Send(5, 0, "NAME.WBM")` (`SMultiSetup::SetParam 0x48e5c0`, the same case the setup's own map chooser uses, 0xe);
   - then the Start button: `SMultiSetup::OnCommand 0x48ce30` with the widget at `+0x28fc`.
   - The skirmish then runs as from the menu. The map list in the skirmish options is a file dialog over `DATA\MAPS` (`SOpenDialog`, sent by `SMultiplayerOption 0x48b280`), so saved maps appear there too.

## 7. New Map **H**

The game has no "new world" routine. The 1998 editor's `OnNewDocument` resets and fills its own MFC document, so the in-game route is a file:

1. `editor::WriteBlankMap` (`mapfile.cpp`) writes `WarBreedsHD-newmap.wbm` in the game folder (not in `DATA\MAPS`, so it never shows in a list) from the format in `re/wbm-format.md`:
   - **Header:** version 5, supported and recommended = the player count, the size, the name.
   - **Terrain:** tile counts = 2 × cells, 96×96 cells of one tile; world counters and clock all zero.
   - **Pathing:** format 4 with every tile "no region" and no regions. That avoids format 0, whose load asks with a message box to reprocess. The editor processes before every save anyway.
   - **Players:** N slots, all but slot 0 computer:
     - races Tanu, Kelika, Sen-Soth, Magha in turn;
     - colours 238 − 8i;
     - CGK limits 5, 5, 5, 5, 2;
     - fog on and nothing explored;
     - start cells spread over the map: opposite corners first, then edge midpoints, inset about 12%.
   - **Effects and things:** no effects and no things.
   - **Triggers:** none.
   - **AI:** marker 0 per computer player, so the loader reads no AI state and the AI starts fresh.
   - **Tail:** no campaign; scroll bounds = the map.
2. It opens like any map. On entry the editor:
   - fills the map with the chosen terrain (the painter's Fill, not undoable);
   - gives each player a Shaman of its race at its start cell with the engine's own placement (`AThingRegistry::PlaceCreature 0x4472d0`, as the console's place command and the autopilot use);
   - deletes the temporary file.
3. Save As writes a complete map through the game's saver: the fresh AI objects write full AI state, and the Shamans are normal things.

## 8. Tests

`tools/autotest.sh 13 <seconds>` (headless), with `TARGET`:

- **0:** Map Editor → "4 Corners", then:
  - Lava brush 1 over 6 cells, Sand brush 3 over 4, Crystal brush 1 over 3;
  - Ctrl+Z, right-click pick (Lava);
  - Save As HDTEST;
  - zoom out and back in;
  - Test Map.
- **1:** Map Editor → New Map, 48×48, Create; one Lava stroke; Save As HDNEW; Test Map.
- **2:** Map Editor → "East vs. West"; one stroke; Exit (asks), Exit; back on the map list.

Every step leaves a screenshot (`auto-e*`, `auto-n*`, `auto-x*`). Then:

```
python tools/editor_check.py testrun
```

It replays the logged strokes (`WarBreedsHD-editor-expect.txt`) on the original map (or on the base terrain for a new map). It compares the result with the saved file parsed by `tools/wbm.py` and with the editor's own diff (`WarBreedsHD-editor-save.txt`, written only under `[Debug] Autopilot`). It also checks the round trip, processed pathing, the undone stroke, the pick, and, for a new map, the size, slots, Shamans and AI state.

## 9. Open questions

- A shipped map re-saved by the editor keeps the AI state of the paused load. A new map's AI starts fresh (marker 0 at its first load). Whether fresh AI state differs in play from the 1998 editor's saved state isn't known. **M**
- The skirmish setup's Start shuffles or keeps start slots as configured, so Test Map uses whatever the setup page last had.
- Pods, trees, rocks, start points and devices are Stage 2. A new map has no pod soil until Cropland is painted.
