# The in-game map editor, Stage 2: things, units, players, map settings, the check

Stage 1 (`re/editor-shell.md`) opened a map frozen in the mission scene and painted terrain. Stage 2 adds the rest of the 1998 editor except the event (trigger) editor and the Global AI editor: obstacles, ruins and crops; creatures and devices with their attributes; players and start points; the map's name, fog and player counts; undo for all of it; and a map check the original never had. Code: `src/editor/editor.cpp` (panel, tabs, tools, undo), `world.*` (engine access), `validate.*` (the check), `tabs.h` (the tab hook for Stage 3). Background: `re/editor-original.md` §5.2–5.4, 5.7, 5.10, §7.

Confidence marks as elsewhere: **H** = read from the code and confirmed in the running game (headless tests), **M** = from the code only, **L** = a guess.

---

## 1. What the player sees

- The panel is in the **top-right corner** (Stage 1 had it bottom-left, where it hid the map you wanted to paint). Rows: the title with **Hide**; two rows of tabs (Terrain, Things, Units, Players, Map; Events and AI appear once a module registers them); 15 rows for the tab's page; Undo / Redo / **Check**; Save As / Test Map / Exit; two status rows.
- **Tab** (or Hide) folds the panel to a one-row strip, "Show (Tab)" plus the current tool ("Roots 5", "Sand Tree", "Raptor"). The tool stays in use, so the area behind the panel can be painted. Tab or the strip's button opens it again.
- **An outline at the cursor** shows what a click would do: the cells the brush paints (BrushFirst/BrushLast), the rectangle of a Shift+drag fill while dragging, the footprint a thing will take (Things, Units), the start cell (Players). Hidden over the panel and the minimap. Computed at draw time from the camera and cursor of that frame, so it follows smooth zoom.
- **Things:** the Smart Palette's 10 thing items (TRPL 1022: Crystal/Sand/Tar Tree and Rock, Yedda Ruins, Pod, Spore, Weed), names from the game's display-name table. Left-click or drag places one per 32 px tile; right-click removes the crop or obstacle under the cursor; Fill Pods / Spores / Weeds; Clear Crops.
- **Units:** a player selector (`<` `>`, the player's colour as a swatch), Creatures (TRPL 2000's 21) and Devices (16), paged. Select mode: click a thing to select it (a box is drawn round it), drag to move it; the page then shows hit points (`-` `+`, a tenth of the maximum per click, right-click for 1), the rank (Rookie, Squaddie, Veteran, Hero) and each gizmo slot (click: next allowed gizmo; right-click: previous), Delete (also the Del key). Right-click deselects.
- **Players:** type (Computer, Human, Either, None; player 1 is fixed Human), race, CGK level 0–5 per clan (click +1, right-click −1), Available per clan (the own race can't be switched off). A click on the map sets the chosen player's start point. Every player's start point is drawn on the map as a ring in its colour with its number; the chosen player's has a second ring.
- **Map:** name (click, type, Enter; Esc cancels), size (read only), fog (fog and shroud / fog, no shroud / no fog), players supported and recommended (`-` `+`).
- **Check** runs the map check and lists the findings on the page (Close, or any tab, returns). Save runs it too, logs every finding and says "N warning(s): Check" in the status line; it never stops a save.

## 2. Things **H**

| What | Address | Notes |
|---|---|---|
| `AThingRegistry::CreateAt` | `0x446db0` thiscall (player, type, x, y, skipChecks, activate, id) → id, `ret 0x1c` | x, y = top-left px. The top-left object-grid cell must be free whatever `skipChecks` says. Crops: snapped to their tile, owner forced to 9, pod soil required always. Obstacles (0x43–0x49): `AnyObjectInBox` always. Devices with checks: collision, `HasPower`, Refinery: a crop within reach. |
| `PlaceCreature` | `0x4472d0` (player, type, x, y, 4 gizmos) → id, `ret 0x20` | `CreateAt(…, 0, 0, 0xc01)`, `SetGizmo` per slot ≠ 100, then `ActivateThing`. |
| `ActivateThing` | `0x4473a0` **stdcall** (thing), `ret 4` | Names says "cdecl" in older notes; calling it cdecl unbalances the stack (a crash at the next return, seen in a test). |
| `RemoveThing` | `0x4481e0` (id, flag), `ret 8` | flag 1 = quiet: no remains effect and no corpse (`ADeath` 0x42); unlinks the grid, nulls the table slot. The object goes on the deferred-delete list. |
| `CanPlace` | `0x4517e0` (player, cx, cy, type, ignoreUnits, skipSite, a6) | 0 fits, 1 blocked, 2 fits but no power / no crop near a Refinery. |
| `ADevice::BuildStep` | `0x40c8a0` thiscall (short), `ret 4` | The cheat placement (`SGame::LButtonDown 0x47d6d3`) loops it until `+0x2c` complete; so does the editor (at most 20,000 steps). |
| `ACreatureBody::SetGizmo` | `0x405230` (slot, gizmo), `ret 8` | 0x13 Screamer fills slot and slot+2 (the second holds 0x15, which `SetGizmo` ignores). |
| `ACreatureBody::SetVeteranLevel` | `0x406e20` (level), `ret 4` | HP scaled to the new maximum. |
| `APlayer::HasGizmoTech` | `0x43a420` (gizmo, plus) | The gizmo list per slot: the slot's group (`g_GizmoSlotGroup 0x54eaf0`) and the owner's CGK, Gene Lab+ level. |
| `AGrid::TerrainTypeAt` | `0x42ea40` (ILogicalC* {`0x5030ec`, x, y}) | 0 ground, 1 blocked, 2 pod soil, per 32 px tile. |

- **Placement rules.** Creatures: as the Gene Lab places them (`PlaceCreature`). Devices: `CanPlace`; 1 refuses, 2 places anyway with a note ("no power here", "no crops in reach"), then `CreateAt(skipChecks = 1)` and `BuildStep` to complete, because the 1998 editor placed unpowered devices too (they blink). Devices and obstacles go on the 32 px tile grid. Crops: `CreateAt`'s own checks. Trees and rocks: every tile under the footprint must have the subtype's corner (crushed crystal 1, roots 7, hardened tar 4, from the smart painter's TRST tables: the cell's tile kind, the corner of the tile's quadrant); ruins: every tile terrain type 0. These are the manual's rules; the original's `ATerrainObstacle` code (MapEditor.exe 0x4531b0–0x4548d4) was not decoded. **M** for the original's exact rules.
- **Fill Cropland** is the console's `crop` / the original's 0x402fd0: every pod-soil tile, `CreateAt(9, type, tx·32, ty·32, 0, 1, 0xc01)`. **Clear Crops** removes every 0x3f–0x41 quietly (the console's `crop` uses Cmd Kill, which needs the simulation to run).
- **Picking.** `World::PickAt 0x44c530` → `PickInLayers 0x44bf00` skips class 0x17 things (obstacles, corpses) and tests sprite pixels, so the editor picks itself: the nearest thing whose radius (`g_ThingRadius 0x54cf08`, footprint/2, at least 16 px, +4) covers the point.
- **The game's selection stays empty** in the editor. With a thing selected, the game's info panel (top right) shows its panel there, under the editor's. The editor marks its selection with a box in the world overlay and shows hit points on its own page.
- **Gizmo slots** per species: `AGeneLabInterface::ClearUnusedSlots 0x42c3e0` (none: Shamans, Jubjub, Thumper, Dervish; arms only: Scout, Myrmidon, Aphid, Shadow, Reaver; no slot 4: Raptor, Ophidian, Mongrel, Behemoth; head only: Snipe, Viper; all four: Dhuganaya, Leviathan). Other types: `g_HasArmSlots 0x54c590` / `g_HasHeadSlot 0x54ce48`. Slot 1 is the head except on the Leviathan, whose four slots take arm gizmos (the cheat placement picks `0x47dcb0(4, 0)` for it). **M** for the Leviathan and the Dhuganaya's fourth slot.
- **Names**: `g_ThingDisplayName 0x550178` char[74][20], `g_GizmoName 0x54fcc0` char[25][20].

## 3. Undo and thing handles **H**

- An undo record is a list of operations applied in order and undone in reverse: terrain cells (as Stage 1), a thing by value, a player by value, the map header by value, or a module's custom function (`tabs.h` `PushCustomEdit`).
- A thing that is removed and put back gets a new engine id, so records name things by a **handle** (a map handle ↔ id kept by the editor; a thing gets one when a record first touches it). A thing operation has a "before" and an "after" (`ThingRec`: type, owner, top-left, hit points, rank, gizmos, crop stage or obstacle rotation); either may be absent (placed, deleted). Applying removes the old and restores the new with `CreateAt(skipChecks = 1)`.
- Moves and rank/gizmo changes replace the thing (remove, then put it back changed); hit points alone change in place (vtable `SetHP` +0x5c). Consecutive hit-point clicks on one thing fold into one record.
- A loop over the thing table that puts things back must collect the ids first: new things append to the table (a race change looped over its own new Shamans until the game crashed, `RevealObjectsAt 0x44c570` on a null table slot).
- `[Debug] Autopilot` builds log any thing-grid entry (World +0x10c, 2 × 192 × 192 u16) whose id has no thing after each record (`editor: DEBUG … stale`); none in the tests.

## 4. Players **H** (layout), **M** (type semantics)

- **Type numbering** (the question in `re/editor-original.md` §8): dialog 135's type combos list **Computer, Human, Either, None** (DLGINIT 135), so 0 Computer, 1 Human, 2 Either, 3 None. The editor's player record (MapEditor.exe doc +0x9068, 0x34 bytes) is read by 0x404550 and applied by 0x404640:
  - no player and type ≠ 3 → `CreatePlayer(race 0, colour 238 − 8i, i, isAI = type is 0 or 2)`;
  - type 3 → `DeletePlayer(i, 0, removeThings = 1)`;
  - Human ↔ other → the console's toggle (retail `ToggleComputerPlayer 0x43aa70`);
  - then 0x404a10: the type into the player (old +0x4a24 = retail **+0x4a44**, "MPTY" in the file), CGK per clan (`SetCGK`), and the four "available" flags into old +0xa/+0xc/+0xe/+0x10 = retail **+0x1c[clan]** ("PCBCF"). The dialog's columns are Tanu, Kelika, Sen-Soth, Magha (clans 0, 2, 1, 3).
  - New map defaults (0x403c90): player 1 Human, the others below Recommended Computer, the rest None; CGK 2 and available 1 for every clan.
  - The shipped maps fit: MPTY 1 (or 2) for slot 0, 0 for the others.
- Retail reads MPTY only in the constructor (2), the serializer and the copy constructor: it has no effect in play. What matters is the class (H flag: `AIComputerPlayer` or `APlayer`) and whether the slot exists. The editor writes MPTY so maps carry the original's meaning; Either saves as a computer player.
- The editor's changes: None → a type: `APlayerRegistry::CreatePlayer 0x43b850(race, 247 − 8i, i, isAI)` with the original's defaults (race in New Map's order, CGK 2, all clans available, a start cell spread like New Map's). Human ↔ Computer/Either: `MakeHuman 0x43c710` / `MakeComputer 0x43c7d0` (the skirmish setup's conversions). A type → None: the player's things are removed first (undoable records), then the player is destroyed as `DeletePlayer 0x43abf0` does, **without** `PlayerDefeated 0x43b780`: with one player left that declares a winner and ends the "game". `APlayer::Destroy 0x43a5e0` (fastcall) or `AIComputerPlayer::Destroy 0x4b5d20` by `vtbl+0x7c IsHuman`, then `operator delete 0x4eb770`, the slot cleared, the count (+0x24) lowered.
- Race: `+0x4878` written directly, and the player's Shamans replaced by the new race's (undoable). Not `APlayer::SetClan 0x4379a0`: with `keepLevels = 0` it swaps CGK entries and converts all the player's creatures to clan equivalents (`0x44f1b0`, removing those without one), which isn't reversible.
- Fields: CGK `+0x4850` int[5]; available `+0x1c` u16[5]; start `+0x4a1c`/`+0x4a1e` (cells); colour `+0x484e`; fog `+0x484c`; explored `+0x2426` u8[96·96] (x + y·96, 1 = unexplored).
- Player 1 (and the local player) can't change type: the original fixed it, and deleting the local player would end the editor's mission.

## 5. The map header and fog **H**

- `MapInfo 0x567578`: +2 supported, +4 recommended, +0xc name (char*). The editor writes the counts directly: `MapInfo::SetSupportedPlayers 0x432100` (the console's `supports`) also deletes players. Lowering Supported is refused while a player exists at or above the new count ("Set player 5 to None first"); Recommended follows Supported down.
- **Name**: `SGame::SaveGame` names the map after its file (`MapInfo::SetName 0x432150`, call at **`0x480cd2`**). The editor hooks that call: an editor save keeps the Map tab's name. A new map without a name still takes the file's (Stage 1's behaviour); an opened map keeps its name (Stage 1 renamed "4 Corners" saved as HDTEST to "HDTEST").
- **Fog modes** (the original's three): fog flag `+0x484c` on every player, and the explored maps all unexplored (mode 0), all explored (1), or fog off (2). As `APlayerRegistry::SetFogMode 0x43ce00` plus the way back to the shroud. Registry `+0x2a` (PRHO) = mode 0, `+0x2c` (PRFO) = fog on (the loader forces PRFO to 1, so the per-player flags carry it). The editor keeps the local player's flag off while editing and applies the mode to every player just before the write: the frozen world still marks the cells round each unit explored at load, which Stage 1 saved.
- `[Debug] NoFog` cleared the local player's fog flag in the frame tick that `SaveGame`'s own redraw runs, so test saves had fog off for player 1. NoFog now leaves the editor alone.

## 6. Start points and the world overlay **H**

- `PLYRSTRT.MHK` isn't in retail. The editor draws its own: `layers::SetWorldOverlay` is called where the world-space part of the interface call ends (`0x47c072`), before the interface layer starts, with the battlefield's back buffer current; `layers::WorldPixelAddress` addresses it (the port's pixmap, as for the interface port). Rings in the player's colour (`+0x484e` is a palette index that draws as the player's colour) with a dark edge and a 3×5-font number; the selected thing's box; the tool outline.
- The terrain only redraws cells marked dirty, so overlay pixels stayed behind as the cursor moved (trails in the first screenshots). After drawing, the overlay marks the cells it touched dirty in both of AGrid's dirty buffers (`+0x4848`, 2 × u8[96·96]; a cell is drawn if either is set). The buffer's index order wasn't checked, so both `y·96+x` and `x·96+y` are set (a few extra cells redrawn).

## 7. The check (`validate.cpp`) **H** for what it reads, **M** for the power model

| Finding | How |
|---|---|
| Recommended > Supported | the header |
| Player n is None but the map supports N; player n exists beyond Supported | the registry against the header |
| No start point on the map; still the top-left corner (the original's default) | `+0x4a1c` |
| No Shaman (or a Gene Lab and a Psi Tower) | types 0/9/0x12/0x1b; 0x2d/0x35 and 0x2f/0x38 |
| Units or devices that belong to no player | owner pointer null (the loader gives things of a missing player no owner) |
| Crops off cropland | `TerrainTypeAt` ≠ 2 under the crop (the loader drops them) |
| A device without power; a Refinery without crops in reach | below |
| A start point on blocked ground; player n can't reach player m's start on foot | flood fill on the game's obstacle map: `tObstacleMap::Regenerate 0x4a7a70` on AITerrainAnalysis +4 (cells at +0x14, `y·w + x`; blocked = terrain 1 or 2, or under a tree/rock/ruin), the guide graph's own input |
| Cropland leaves a narrow gap at an edge | pod soil 1–2 tiles in from an edge with open ground between (the manual: keep cropland at the edge or well away) |

- **Power.** The paused world never links pods to Refineries or devices to providers, so the runtime flags (`+0x28`, `+0x34`) say nothing in the editor. The check models it: a Refinery is a source when `RefineryPodNearby 0x4518c0` finds a crop (as `CanPlace` asks); a Relay (0x2e/0x36) carries power when it sits in a powered provider's field; a device that draws power (`g_DevicePower 0x54d010` > 0) needs to sit in such a field of its owner. The field radius is the table `FindPowerProvider` uses (`0x54de68 + 2·type`, via `0x44eb00`). Capacity is not checked.
- Reachability uses ground only; flyers and Warps aren't considered (the manual's advice: separated regions need flyers or warps).

## 8. The panel **H**

- A pool of 80 `WTextButton2` added to SGame once. Every frame the title, tab rows, the page and the common rows list `PanelItem`s (row, x, text, id, lit, swatch) and the pool takes them; unused buttons get empty text and draw nothing. Items are placed at draw time (`Place`), as in Stage 1.
- **Top right**: x = frame width − 10 − panel width, y = 16. The renderer anchors interface elements by which third of the frame their centre is in; the panel's centre stays in the top-right third at every zoom level, so it is anchored there.
- **Clicks** reach the panel only where it is drawn: `OnPanel` uses the current geometry (one row when folded). A world point under the scaled panel can't be clicked: the renderer maps it to the panel. The test helpers check that with the interface mapping (`render::InterfacePointToClient` of points inside the panel and the minimap).
- **Tabs for Stage 3** (`tabs.h`): `enum Tab { … kTabEvents, kTabAi }`; `RegisterTab(kTabEvents, &page)` with a `TabPage` (Label, Items, OnClick, OnMapClick, OnShow, OnClose); `PushCustomEdit(what, apply)` for undo; `SetPanelStatus`. A registered tab shows in the second tab row.

## 9. Tests

`TARGET=n tools/autotest.sh 13 <secs>`, then `python tools/editor_check.py testrun`. Saves under `[Debug] Autopilot` also write `WarBreedsHD-editor-world.txt`: the engine's map header, players, things and the check's findings as they were when saved.

- **0, 1, 2** as Stage 1 (the strokes' base cell moved to 30 % / 45 % of the frame, clear of the panel). The checker now also compares the world record with the file (every thing's type, owner, place, hit points, rank, gizmos; players' race, colour, CGK, clans, start, class and type; the header; the fog). Target 2 saves nothing: the checker checks the Exit flow only.
- **3** (things and units, New Map 48×48): brush outline, Shift+drag fill outline, Roots brush 5, **Tab** (the panel folds; a cell it covered is now reachable), a Roots stroke there, the strip's button, Tab twice; Cropland; a Sand Tree on the roots (footprint outline), Yedda Ruins, Fill Pods (96), a right-click removes a pod, Ctrl+Z; a Refinery, a Turret beside it and one far away, a Raptor and a Scout; Select: Raptor hit points −10 %, rank Squaddie, Hammer in arm 1, dragged elsewhere; the Scout deleted, undone, redone; Check: only the far Turret has no power; Save As HDTHING; Test Map: the skirmish has the Raptor, Refinery, both Turrets, the pods, the ruins and the tree.
- **4** (players and map, 4 Corners): player 2 Computer → Human → Either, race Magha → Tanu (its Shaman follows), Tanu CGK +1, a clan toggled, a start point; player 6 → None (its Shaman goes); name "HD Test Map", fog changed, Supported 6 → 5 then refused, Recommended 4 → 3, undo, redo; player 3's Shaman deleted; Check: "Player 3 has no Shaman"; Save As HDPLAY; Test Map.

## 10. Open questions

- The original's exact obstacle rules (`ATerrainObstacle`, MapEditor.exe 0x4531b0–0x4548d4); the manual's are used. **L**
- The power model is static and ignores capacity; a map the check passes could still run short of power with many devices on one Refinery. **M**
- Gizmos are a list, not the Gene Lab UI (`AGeneLabInterface`). The original offered "With Gizmos" at placement; here a creature is placed bare and fitted in Select mode.
- The terrain's dirty-buffer index order (row- or column-major) is unconfirmed (both are marked).
- Things placed under a later terrain stroke stay (a tree on lava). The check flags crops off cropland; other things on unsuitable ground aren't checked.
- Map Preferences (dialog 136, the New Map defaults in the registry) has no equivalent; New Map has its own page.
