# Reverse engineering WarBreeds

WarBreeds HD is built without the game's source code. Everything in it rests on taking `WARBREED.EXE` apart: decompiling it with Ghidra, reading the disassembly, and checking the findings in the running game. This document is the guide to that work and a log of what was found, for anyone working on WarBreeds or on a similar old game.

## Start here

1. Read this page for the setup, the methods and the history.
2. Then [`re/subsystems.md`](../re/subsystems.md): for each part of the game (scenes, map, units, players and AI, pathfinding, saving), its entry points and the report that covers it.
3. Then the report for the area you need, in [`re/`](../re/). The [map of the reports](#map-of-the-reports) below says what question each one answered.

[`re/names.tsv`](../re/names.tsv) is the authority on names: about 2,100 functions, globals, labels and vtables, with notes. [`re/types.tsv`](../re/types.tsv) holds the struct layouts. Older reports sometimes use an earlier name for the same address; the address is what counts.

## The target

| Item | Value |
|---|---|
| Executable | `WARBREED.EXE` from the GOG.com release: 1,355,264 bytes, SHA-256 `0E0768693A62BA89FCD5F1FD56DFCDA68206D32BC6D5B5C285792996115061E3` |
| Format | 32-bit PE, image base 0x400000, not large-address-aware |
| Compiler | Microsoft Visual C++ 5 (linker 5.2), with RTTI; no Rich header |
| Engine | Broderbund/Red Orb's Mohawk engine: `.MHK` resource archives, a scene stack, 8-bit palettised drawing through GDI |
| Steam release | not checked; its exe may differ, and every address here is for the GOG build |

What GOG puts next to the 1998 exe, and why it matters:

- **`ddraw.dll`** is [DDrawCompat](https://github.com/narzoul/DDrawCompat) 0.6.0. The game uses DirectDraw only to change display modes and draws everything with GDI, which is why DDrawCompat works and surface-based wrappers (cnc-ddraw) show only a corner of the screen.
- **`WARBREED.DLL`** replaces `VERSION.dll` in the exe's import table. It also re-points the game's DirectPlay `CoCreateInstance` call to its own loader, which breaks multiplayer (see the [log](#2026-10-12-multiplayer-restored)).
- **`_inmm.dll`** replaces `WINMM.dll` to play the CD music from `MUSIC\trackNN.ogg`.
- **`dplayx.dll`** in the game folder is a stub with no `DllGetClassObject`.
- DDrawCompat also pins the game to one CPU core. The 1998 timer and thread code deadlocks on several cores, so anything that replaces DDrawCompat must do the same.

## Tools and setup

- **Ghidra 12.1** with a JDK 21, as a persistent project. [`re/ghidra.md`](../re/ghidra.md) has the commands. In short:
  - [`tools/ghidra/MarkVisualStudio.java`](../tools/ghidra/MarkVisualStudio.java) runs before analysis and tells Ghidra the exe is MSVC, so its RTTI analyzer runs at all.
  - [`tools/ghidra/ApplyNames.java`](../tools/ghidra/ApplyNames.java) walks the MSVC 5 RTTI itself (Ghidra can't read this compiler's object locators), names every class's vtable slots and makes them `__thiscall`, then applies `names.tsv` and `types.tsv`. It can be re-run at any time; the two text files carry the knowledge.
  - [`tools/ghidra/ExportCaches.java`](../tools/ghidra/ExportCaches.java) writes a grep-able export: every function decompiled (also split per class), the call graph, cross-references, strings and classes. Research is done by grepping that export.
- **Python helpers** in [`tools/`](../tools/), run from a venv with `pefile` and `capstone`:

  | Tool | Use |
  |---|---|
  | `wbre.py` | linear disassembly of `.text` into a text cache, string search, RTTI lookup of vtables |
  | `peinfo.py` | sections, imports and exports of any PE file |
  | `peek.py` | read a running game's memory, read-only, and name the RTTI class of what pointers point at |
  | `scan.py` | find variables that move the way the camera does (from the patch's memory scanner) |
  | `mhkimg.py` | extract images and palettes from Mohawk `.MHK` bitmap sets |
  | `wbm.py` | read and write `.WBM` maps and saved games |
  | `samples.py` | turn the patch's instruction-pointer sampler output into hot game functions |
  | `gen_def.py` | generate the proxy DLL's export list from the system `wsock32.dll` |

## How the patch gets in

The patch never edits `WARBREED.EXE` on disk. Each technique below checks the original bytes or pointer before changing anything, and logs a `FAILED (unexpected code)` line instead of patching code it doesn't recognise.

| Technique | What it does | Example |
|---|---|---|
| Proxy DLL | The exe imports `WSOCK32.dll` statically, and it isn't a Windows "known DLL", so a `wsock32.dll` in the game folder loads at start-up and forwards the real exports | [`src/proxy.cpp`](../src/proxy.cpp), [`src/wsock32.def`](../src/wsock32.def) |
| Import-table hooks | Replace an entry in the exe's import table: every call the game makes to that API comes to us | GDI frame copies and text in [`src/render/render.cpp`](../src/render/render.cpp), [`src/fonts.cpp`](../src/fonts.cpp); `CoInitialize` for multiplayer in [`src/main.cpp`](../src/main.cpp) |
| Vtable slots | Replace one virtual method of a class (a scene's Init, OnCommand or mouse handler), call the original from ours | the Settings page in [`src/menu.cpp`](../src/menu.cpp); the campaign screen in [`src/campaign.cpp`](../src/campaign.cpp) |
| Call-site redirection | Rewrite the 4-byte target of one `call` instruction, so only that caller is changed | the skirmish setup in [`src/skirmish.cpp`](../src/skirmish.cpp) and [`src/skirmish_teams.cpp`](../src/skirmish_teams.cpp) |
| Byte patches | Replace a short instruction sequence with a call into the patch, or a jump | the defeat test in `skirmish_teams.cpp`; the comms debug text in `main.cpp` |
| Calling the game | Call the game's own functions by address, so the result is exactly what the game would do | creating units, saving maps, applying alliances |

## Conventions

- **Confidence.** Every claim in the reports carries a mark: **H** read from the code (the decompiler checked against the disassembly), **M** the mechanism is clear but its purpose is inferred, **L** a guess to test in play.
- **Names.** `Class::Method` for methods, `g_Name` for globals. `re/names.tsv` wins over any report.
- **Two "world" objects.** `[0x579278]` is the RTTI class `AThingRegistry` (things, selection, groups); several reports and `src/game.h` call it `World`. `0x55dd60` is `AGrid` (the terrain cells and map size); the map-format report calls it "World". They are different objects.
- **Calling conventions come from the call site, not the decompiler's label.** Ghidra called `ConfigureAI` cdecl; the caller sets ECX and pushes one argument with no stack clean-up, so it is a thiscall that pops its argument. A cdecl hook unbalanced the stack. Hooks for such functions are written as `__fastcall` (ECX, an unused EDX, then the stack arguments).
- **Reports cite addresses and summarise.** They don't reproduce decompiled code.

## Testing what you found

A finding counts once it has been seen in the running game, and every feature built on one gets a test that runs again before each release. The patch has a test harness built in:

- `[Debug] Headless=1` creates the game window hidden, off-screen and never active, reports no audio devices, and answers any message box OK. Tests never appear on the player's screen.
- `[Debug] Autopilot=N` drives the game through a scripted scenario with posted input and a virtual cursor, logs what it sees, and saves screenshots of the frame and each interface layer. There are modes for missions, menus, the map editor, multiplayer, skirmish teams and the campaign.
- [`tools/autotest.sh`](../tools/autotest.sh) builds the patch, installs it in a hidden copy of the game and runs a mode. [`tools/mptest.sh`](../tools/mptest.sh) runs a host and a joiner at once. Checkers (`editor_check.py`, `events_check.py`, `teams_check.py`, `campaign_check.py`) read the log and pass or fail.
- Several hidden copies can run side by side, each on its own CPU core.

## Map of the reports

| Report | The question it answered |
|---|---|
| [`subsystems.md`](../re/subsystems.md) | Where does each part of the game start? (the index) |
| [`ghidra.md`](../re/ghidra.md) | How is the Ghidra project built, named and exported? |
| [`scenes.md`](../re/scenes.md) | How do scenes, widgets, the main loop and input work? |
| [`hotkeys.md`](../re/hotkeys.md) | What does every key do in a mission, and which keys are free? |
| [`mouse.md`](../re/mouse.md) | How do mouse clicks become selections and orders? |
| [`fogbuild.md`](../re/fogbuild.md) | Why can't a Shaman build or plant on fogged ground? |
| [`units.md`](../re/units.md) | How are things, creatures and devices organised, and where are their stats, the gene pool and rally points? |
| [`map-loading.md`](../re/map-loading.md) | How is a map loaded, and how is the terrain grid laid out? |
| [`wbm-format.md`](../re/wbm-format.md) | What is in a `.WBM` map or saved game, byte by byte? |
| [`tutorial-terrain.md`](../re/tutorial-terrain.md) | Why did small maps draw terrain only in the top-left corner at HD sizes? |
| [`editor-original.md`](../re/editor-original.md) | What could the official 1998 Map Editor do, and how much of it is the game engine? |
| [`terrain-painter.md`](../re/terrain-painter.md) | How does the original editor's smart terrain painting choose tiles? |
| [`editor-shell.md`](../re/editor-shell.md) | How can a map be opened, edited and saved inside the game? |
| [`editor-stage2.md`](../re/editor-stage2.md) | How are trees, units, players and map settings edited and checked? |
| [`editor-events.md`](../re/editor-events.md) | Where is the event editor in the retail game, and how does the Global AI work? |
| [`players-ai.md`](../re/players-ai.md) | How do players, alliances, the computer players and the command model work? |
| [`save-load.md`](../re/save-load.md) | How are games saved and loaded, and where is campaign progress kept? |
| [`pathfinding.md`](../re/pathfinding.md) | Why do units get stuck, especially behind C-shaped obstacles? |

## Log

One entry per investigation, oldest first. Each says what the question was, how it was answered, what was found, the key addresses, and where the detail is.

### 2026-10-10: Reconnaissance

- **Question:** how does the game choose its resolution, and can it run wider than 4:3?
- **Method:** PE headers and imports (`peinfo.py`), the preferences file, a test copy with a patched resolution table.
- **Found:**
  - The resolution is read from `DATA\prefs.dbg`, not hard-coded.
  - A five-slot mode table at `0x579490` is filled by `0x492150`, and the live mode switch is `0x46fa80`.
  - Each scene asks for its own size: menus 640×480, missions the preference.
  - Patching slot 5 to 1920×1080 gave a full 16:9 mission with more map visible: the engine copes with any size.
  - There is no static `ddraw.dll` import; DirectDraw is loaded at run time and used only for mode changes.
- **Dead end:** the table-and-prefs edit worked but needed both changed, because the prefs loader falls back to 640×480 when the saved size isn't in the table. Hooking the mode switch instead made both unnecessary.

### 2026-10-10: Camera and zoom

- **Found:**
  - The camera is `SGame+0x38/+0x3a` (world pixels), with scroll bounds at `+0x44..+0x4a`, clamped by `0x47d340`.
  - The view size is at `+0x4c/+0x4e`.
  - The game already has a 2× zoom-out on the Z key (`0x47d430`).
- **Method:** a memory scanner recorded which variables moved with the camera while scrolling (`scan.py`). The camera wasn't in any obvious global; the recording found it in the mission scene object.
- **Result:** resolution-stepping zoom first (a black flash on each step from the real mode change), then the game's own Z zoom (instant, but only two levels), then the patch's own renderer for smooth zoom.

### 2026-10-10: Rendering

- **Question:** where does a frame go to the screen?
- **Method:** an attempt with cnc-ddraw failed: it showed only a 106×160 corner. Its call log revealed that **WarBreeds never creates a DirectDraw surface**. A GDI call trace (`[Debug] GdiTrace`) then found the one place that draws.
- **Found:**
  - Mohawk draws everything into 8-bit DIBs and presents with **one `StretchBlt` per frame**, from `0x4dc87e`.
  - The frame pipeline: compose `0x46f400`, mission draw `0x47b770`, interface overlay `0x47c000`, present `0x4693a0`.
  - All drawing goes through the back-buffer handle at `[0x5509a4]`.
- **Result:**
  - A fake DirectDraw that "sets" modes instantly and a Direct3D 11 presenter that takes the 8-bit frame and palette ([`src/render/`](../src/render/)).
  - The interface is split into its own layer by swapping the back-buffer handle for the interface phase, so it can be drawn at a fixed size while the battlefield zooms ([`src/layers.cpp`](../src/layers.cpp)).
  - Ports must be locked before `SetPort` accepts them.
- **Also found:** the mode switch calls `ChangeDisplaySettingsA` itself (import slot `0x59599c`) on every change, menus included. Modern Windows refuses the 8-bit modes, but each refusal costs ~25 ms, which is why menu transitions were slow. The patch answers it without touching the display.

### 2026-10-10: Menus, the Settings page and the cursor

- **Found:**
  - The main menu is the `SMissionMenu` scene (vtable `0x508304`). Its items are text buttons added in Init (slot 5, `0x48fcf0`) and handled by command id in OnCommand (slot 27, `0x48ff40`).
  - Scene changes are queued with `0x4709a0(type, scene, delay)`.
  - An unused "Options" string suggests the 1998 game once had an options item.
  - The text-button fields used: `+0x0c` rect, `+0x14` hotkey, `+0x20` command, `+0x2c` text, `+0x74` visible. Hit-testing and hotkeys ignore `+0x74`, so a hidden button must also be moved away.
- **Result:** the Settings page is two replaced vtable slots and the game's own buttons, with no code patches ([`src/menu.cpp`](../src/menu.cpp)). The menu cursor is drawn into a private port and presented separately, so it can move over the widescreen sides.
- **Detail:** [`re/scenes.md`](../re/scenes.md).

### 2026-10-10: Cutscenes

- **Question:** why don't the movies play?
- **Found:**
  - The movies are QuickTime files: Cinepak video and IMA4 audio.
  - The game reaches QuickTime for Windows 2.x through static glue: `LoadLibraryA("QTIM32.DLL")` and `_EntryPoint` at `0x4eb420`, then about 100 stubs from `0x4eae6a` that pass a selector.
  - Without the DLL every call fails and each cutscene scene ends at once.
- **Result:**
  - Our own QuickTime atom parser with Cinepak and IMA4 decoders, bit-exact against ffmpeg on all eight movies (`tools/movtest.cpp`).
  - A stand-in `_EntryPoint` that implements the selectors the game uses, so the game's own cutscene scenes play them unchanged ([`src/movie/`](../src/movie/)).
- **Trap:** several soundtracks are shorter than their video. The movie clock followed the audio and stopped, so it now carries on in real time.

### 2026-10-10: Fonts

- **Found:** all text is GDI: `AddFontResourceA` on the game's own bitmap `.FON` faces, then `CreateFontIndirectA`, `TextOutA` and `DrawTextA` into the 8-bit frame. Layout uses `GetTextExtentPointA` and `GetCharABCWidthsA`.
- **Result:**
  - An optional TrueType face (Orbitron), substituted per font at `CreateFontIndirectA` and swapped in at `SelectObject` ([`src/fonts.cpp`](../src/fonts.cpp)).
  - Strings that would run wider than in the original face are measured and drawn narrower, so they stay inside boxes sized for the original (IAT slots `0x5956ac`, `0x595670`, `0x5959b8`).

### 2026-10-10: Stability

- **Found:** intermittent hangs at start-up once the patch's renderer replaced DDrawCompat. The 1998 timer and thread code deadlocks on multi-core CPUs, and DDrawCompat had been pinning it to one core all along. The patch pins it now.
- **Found:** the game quits if another WarBreeds window exists (`FindWindowA` at `0x47449d`). Headless tests answer that one call "none", so they can run beside a copy being played.

### 2026-10-10 and 11: Controls and building

- **Hotkeys and mouse:**
  - Every mission key was mapped from the dispatch table at `0x47f576`. Unit groups were already there, on Ctrl+0–9.
  - The mission mouse was mapped from SGame's vtable slots `+0x1c..+0x30`; a left click resolves on button-up.
  - Detail: [`re/hotkeys.md`](../re/hotkeys.md), [`re/mouse.md`](../re/mouse.md).
- **Building in fog:**
  - Buildings could always be placed in fog; only their placement ghost was hidden.
  - Pods were refused by one visibility test at click time (`0x442b10`).
  - Detail: [`re/fogbuild.md`](../re/fogbuild.md).
- **Group build:**
  - The builder panel exists only with exactly one Shaman selected. `World::DrawInfoPanel 0x449410` and `World::PanelClickDown 0x44f620` both test the selection count.
  - Construction can only be joined through the Repair order, once the building exists.
  - Detail: [`src/controls.cpp`](../src/controls.cpp).

### 2026-10-11: Small maps drew terrain only in a corner

- **Found:**
  - `SGame::SetViewSize 0x47b520` halves the view whenever it is at least as large as the map.
  - A 1998 screen never reached a map's size, so this only ever undid a zoom-out; an HD frame can.
  - Terrain is drawn, and clicks are picked, only inside camera..camera+view.
- **Detail:** [`re/tutorial-terrain.md`](../re/tutorial-terrain.md).

### 2026-10-11: The Ghidra deep pass

- **Question:** before the large features (the map editor, pathfinding), can the whole exe be named and typed once, so research stops starting from raw addresses?
- **Method and traps:**
  - MSVC 5 executables have no Rich header, so Ghidra reports the compiler as unknown and its RTTI analyzer refuses to run. `MarkVisualStudio.java` fixes that.
  - Ghidra still can't parse MSVC 5's object locators, so `ApplyNames.java` walks the vtables itself.
  - About 544 functions are reached only through vtables and were missed by auto-analysis; the script creates them.
  - Ghidra guesses `__fastcall` for methods without stack arguments; they are converted to `__thiscall` so `this` is typed.
  - When several research reports are merged, fix a shared vocabulary first: "World" meant two different objects.
- **Result:**
  - About 1,930 of 5,150 functions named, 288 classes and about 100 hand-typed structs, all in two text files that re-apply to a fresh project in minutes.
  - The export the later research reads.
- **Detail:** [`re/ghidra.md`](../re/ghidra.md).

### 2026-10-11: The map editor

- **Questions:** what could Red Orb's separate 1998 Map Editor do, and how much of it is already in the game?
- **Found:**
  - The editor is mostly the game engine in an MFC shell.
  - Its smart terrain painter could be rebuilt from its binary and the tables in the player's own `TERRAIN.MHK`. It was checked against all 54 shipped maps: corners give back the right tile for 99.98% of tiles.
  - A map opens through the normal single-player load with the update loop paused (`SGame+0x460`), and saves through the game's own pathing processor and saver.
  - The retail exe still contains the editor's complete **event editor scene** (`SEvent`, id 0x16), only unreachable. Pushing it over the editor's mission scene brings it back.
  - At load, the game converts the 1998 editor's player colours to the retail numbering (`0x471a80`).
- **Traps:**
  - The game opens `TERRAIN.MHK` without sharing, so the patch reads it at start-up, before the game opens it.
  - `ActivateThing 0x4473a0` is stdcall; called as cdecl it crashed at the next return.
- **Detail:** [`re/editor-original.md`](../re/editor-original.md), [`re/terrain-painter.md`](../re/terrain-painter.md), [`re/editor-shell.md`](../re/editor-shell.md), [`re/editor-stage2.md`](../re/editor-stage2.md), [`re/editor-events.md`](../re/editor-events.md), [`re/wbm-format.md`](../re/wbm-format.md).

### 2026-10-11: Interface at HD sizes

- **Found:**
  - The tallest panel, the Gene Lab+'s Select Components with all 15 components, reaches 585 game pixels. The game lays panels out inside its own frame, so a frame under 600 px pushes the panel's header off the top.
  - The game redraws only the 64 px cells something marked as changed, and fast flyers left trails where nothing did. The redraw logic is original: the trails appear with the patch's layers switched off. Marking every cell each frame (`AGrid::MarkAllCellsDirty 0x42f210`) fixes it.
- **Lesson:** switch the patch's own subsystem off before blaming it. One run tells you whether a glitch is yours or the original's.

### 2026-10-11: Skirmish AI settings

- **Found:**
  - Every map stores Global AI settings per computer player (`AIComputerPlayer +0x4d80..+0x4d90`), but the skirmish setup's `ConfigureAI 0x43c2f0` always overwrote them with clan defaults. A map's own settings never applied.
  - There is no AI difficulty: the Esc menu's AI Options only adds armour to the human's units.
- **Detail:** [`re/players-ai.md`](../re/players-ai.md), [`src/skirmish.cpp`](../src/skirmish.cpp).

### 2026-10-12: Multiplayer restored

- **Question:** why does the main menu's Multiplayer only show "An error occurred while attempting to open multiplayer communications"?
- **Method:**
  - Log the result of every step of the network start-up `0x4c7a30`.
  - When a hook on `CoCreateInstance` never fired, dump the call instruction in memory.
  - Route the engine's own comms debug text (`0x4c60f0`, compiled in but filtered) to the patch's log (`[Debug] CommsLog=1`).
- **Found, two separate faults:**
  - The patch's own menu-art loader left COM initialised on the game thread, so the game's `CoInitialize` returned S_FALSE, which it treats as failure.
  - GOG's `WARBREED.DLL` rewrites the operand of the DirectPlay `CoCreateInstance` call (at `0x4c7a79`) to a loader that asks GOG's stub `dplayx.dll` for `DllGetClassObject`, which it doesn't have. Pointing the call back at the import (COM, which loads the system DirectPlay) fixes it.
- **Also found:**
  - The start handshake (host `0x4c0090`, player `0x4c0350`) sets the start 36 s ahead and averages 20 pings per player.
  - The host drops anyone averaging 1.2 s or more.
  - Two test copies pinned to the same core starved each other's network threads and failed exactly that check.
- **Testing on one PC:** two hidden copies, one hosting and one joining 127.0.0.1, driven through the lobby. Text boxes need real key-downs, not only `WM_CHAR`, and painted button labels are clicked by position.
- **Detail:** [`src/main.cpp`](../src/main.cpp) (top of the file).

### 2026-10-12: Researched, not built

- **Humans against computer players in multiplayer.**
  - The setup code already turns spare map slots into computer players in multiplayer (`AssignPlayers 0x43bd40`). The host's Start routine zeroes the count at `0x48d424`, and the lobby hides the option.
  - The AI looks deterministic enough for lockstep: it uses only the synced random generator `0x471320` and game-turn timers.
  - Shelved by choice. The findings are in [`docs/nice-to-have.md`](nice-to-have.md).
- **Pathfinding.**
  - Both planners are A* in form but greedy in behaviour, capped, and return a path to the closest node they reached.
  - The movement code accepts that partial path as success.
  - Guide points exist only at convex corners, so the inside of a C-shaped obstacle has none.
  - That is why units walk into a C and stop.
  - Detail: [`re/pathfinding.md`](../re/pathfinding.md).

### 2026-10-12: Skirmish teams and colours, campaign progress

- **Found:**
  - The multiplayer lobby's Team and Color widgets are built in skirmish too, only never drawn or clickable (`SMultiSetup +0x279c/+0x237c`).
  - The engine's alliances are one bit mask per player (`APlayer+0x18`), read by targeting, the AI's threat maps, calls for help and the victory check. `ApplyTeams 0x43c8f0` allies equal teams both ways.
  - Campaign progress lives only in memory (`ACampaign` at `[0x57928c]`: chapter `+4`, chapter list `+0xc`, id `+0x18`). Nothing carries over between missions, so a recorded chapter and set is all a resume needs.
- **Traps:**
  - The stock team-cycle function edits "the local player's slot", i.e. always the human's row.
  - The stock row click pairs slot *s* with widget *perm[s]*, which only agree before start positions are shuffled.
  - The start shuffle moves each row's clan and human flag to another slot but leaves its team behind.
- **Detail:** [`re/players-ai.md`](../re/players-ai.md) §2–3 and 7, [`re/save-load.md`](../re/save-load.md), [`src/skirmish_teams.cpp`](../src/skirmish_teams.cpp), [`src/campaign.cpp`](../src/campaign.cpp).

### 2026-10-12: Translations

- **Question:** where does the game's text come from, and can it be replaced from language files?
- **Found:**
  - Almost all of it is one Win32 STRINGTABLE in the exe (ids 1–652, 580 used, English). `IGlobalData`'s constructor `0x464d70`, a static initialiser run before WinMain, copies it with `LoadStringA` into a list (data `[0x55dd54]`, count `[0x55dd58]`, index = id − 1, each entry an `IWinString` holding a `char*`). The Gene Lab, Device Lab, SDialog, SOpenDialog, SReward and `MessageBoxRes` call `LoadStringA` themselves.
  - Hooking the `LoadStringA` import (slot `0x595a3c`) from our DllMain, before those initialisers run, replaces every use. A live language change rebuilds the cached list with the game's own allocator (`0x4eba50` / free `0x4eb770`, so its exit-time free still works) and relabels the text buttons on screen.
  - Some ids are keys, not text: 82 "WarBreeds" is the window class and the single-instance check; 80–91, 590, 591 and 606 are config and Red Orb Zone keys.
  - Mission titles are the maps' header names, read by `0x489f00` (three call sites). Chat lines in campaign maps go out through `AAction_Message`'s call to `APlayerRegistry::SendMessage` at `0x419419`. Three `.data` literals (the TCP/IP prompts, "Error No Missions!") are pushed by single instructions.
  - **The game's bitmap fonts hold their accented letters in Mac Roman order** (é is byte 0x8E, not Windows-1252's 0xE9), and GDI silently drops the bytes Windows-1252 leaves undefined (ç 0x8D, è 0x8F, ê 0x90, ù 0x9D) through an ANSI font. A private in-memory copy of each `.FON` with its FNT `dfCharSet` set to `SYMBOL_CHARSET` passes every byte to its glyph, in `TextOutA`, `DrawTextA` and the measuring calls alike. `ExtTextOutW` with `ETO_GLYPH_INDEX` also draws them, but `DrawTextA`'s word-wrapping would have needed rewriting.
  - Hard limits: `WText::SetText 0x499da0` copies unbounded into 1024 bytes (briefings); text buttons hold 59 bytes; the info panel 39; the "Squad" word 8; `PostMessage` 81.
- **Dead end:** fitting every translation into its English's width. Most text has room to spare ("Quitter" against "Quit" on a full menu row), so squeezing is opt-in per string (`~`) for the tight spots the screenshots show.
- **Detail:** [`src/lang.cpp`](../src/lang.cpp), [`src/fonts.cpp`](../src/fonts.cpp), [`lang/README.txt`](../lang/README.txt), [`tools/lang_check.py`](../tools/lang_check.py).

## What isn't published, and why

The repository publishes **addresses, names, struct layouts, notes and our own code**. These stay on the developer's machine:
- the disassembly cache;
- the Ghidra project and its decompiled export;
- the game's art and data;
- the official 1998 editor.

They are the game's code and content. To get them, use your own copy of the game: [`re/ghidra.md`](../re/ghidra.md) rebuilds the project and the export from it in a few minutes, and the names and types re-apply automatically.

## Keeping this current

Every new investigation adds a log entry here and, if it produced a report, a row in the map of the reports. Every feature or fix also updates the README.
