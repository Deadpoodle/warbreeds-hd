# WarBreeds HD patch: project log and wiki notes

Running log of the investigation, what the patch changes, and facts worth adding to
<https://www.pcgamingwiki.com/wiki/WarBreeds> (the page is a stub as of 2026-10-10:
widescreen, 4K, windowed etc. are all blank).

## Tested version

| Item | Value |
|---|---|
| Store | GOG.com (gameId 1979367321, buildId 59030556949195281) |
| Install path used | `a GOG Galaxy install folder` |
| `WARBREED.EXE` | 1,355,264 bytes, SHA-256 `0E0768693A62BA89FCD5F1FD56DFCDA68206D32BC6D5B5C285792996115061E3` |
| `WARBREED.DLL` (GOG shim) | 97,792 bytes, SHA-256 `E2BE6B350C47E96297F66D3B4AF40BDEDB8F27739061399B026E572F3E1D3D49` |
| `ddraw.dll` (GOG-bundled) | DDrawCompat v0.6.0, SHA-256 `32D86C4317C25D687682E3A8E53D4258147F0F94CD682C0A06FECCCD9F151280` |
| `_inmm.dll` (GOG shim) | SHA-256 `39528A27C63CFF1EBA702E9D538E2B3A6C4BA124CB931D26BFF7A64CA7CCF8E1` |
| Test machine | Windows 10 22H2 (19045), modern AMD GPU, 2560x1440 @ 144 Hz |

The Steam release (app 2464660) has not been checked; its exe may differ.

## Facts for the wiki (verified)

- **What GOG ships on top of the 1998 exe:**
  - `ddraw.dll` is [DDrawCompat](https://github.com/narzoul/DDrawCompat) v0.6.0. It writes `DDrawCompat-WARBREED.log` in the game folder. Settings can go in `DDrawCompat-WARBREED.ini` next to the exe, and Shift+F11 opens its in-game config overlay.
  - `WARBREED.EXE`'s import table has been edited: `VERSION.dll` → `WARBREED.DLL` (a modern MSVC-built shim exporting the three version APIs, which also calls `AddFontResourceExA`) and `WINMM.dll` → `_INMM.dll` (CD audio replacement; music is shipped as `MUSIC\trackNN.ogg`).
- **Executable:** 32-bit, MSVC 5.0 linker (linker version 5.2), image base 0x400000, not large-address-aware.
- **Config files:**
  - `WARBREED.CFG` (game folder): install paths and debug logging (`LogFile=DARWIN.DOG`).
  - `DATA\prefs.dbg`: player preferences in Mohawk (`MHWK`) resource format. It contains the keys `OPTNPRFRESW` / `OPTNPRFRESH` (resolution width/height), `OPTNPRFSPEED`, `CDAUPRFENABLE` and others. With default settings it stores 640x480.
- **Engine:** Broderbund/Red Orb "Mohawk" engine (`.MHK` archives, `MOHAWK32.MMX`). Video cutscenes are QuickTime `.MOV`.

## Roadmap

1. **Widescreen / arbitrary resolution** (in progress). Must have: 1920x1080. Nice to have: 2560x1440 and ultrawide (2560x1080, 3440x1440).
2. **Camera zoom in/out** to see more of the map.
1b. **Widescreen menus (tester request):** the 640x480 menu screens are pillarboxed, and the mouse feels captured at the 4:3 edge.
   - The renderer can draw a wider background image behind each 4:3 menu frame, chosen by scene class (e.g. `SWarBreedsScreen`). The image's centre 4:3 area matches the original.
   - The tester will outpaint the exported originals with image generation for his own copy. Plain black bars remain the default.
   - In menus, stop pinning the cursor to the 4:3 edge.
2a. **In-game Settings screen** (player request; replaces the external tool idea below). **First version done (v0.9):** a native "Settings" item and page on the main menu, built from the engine's own text buttons. See the 2026-10-10 log.
   - Still to do:
     - more settings: zoom mode, renderer and the interface layout (restart-required options need a "(restart)" note);
     - a max-FPS setting;
     - a modern/sci-fi menu font option (tester request), e.g. substitute the engine's menu font WBLg24 (font table `0x550890`, loaded by `0x4d82d9`) with a bundled freely-licensed font, with the original kept as the default;
     - the in-mission "Game Options" (SOption) resolution slider fights our resolution handling and needs hiding or redirecting.
   - A "Settings" item is added to the main menu in the game's own font.
   - It opens a settings screen drawn by our renderer over the main-menu background, using the game's bundled fonts. It reads and writes `WarBreedsHD.ini` and applies changes live where possible.
   - Alternative considered: driving the engine's own unreachable `SOption` scene and its `WSlider`/`WOptionButton`/`WCycleButton` widgets. That would look fully native but means more coupling to engine internals.
   - The ini stays as the storage format, for Proton users and hand-editing.
2a-old. **External configuration tool.** The retail main menu has no Options item, and the resolution picker in the exe's options screen isn't reachable from the main menu. Settings therefore live in `WarBreedsHD.ini`, with a small GUI configurator to edit it.
2a2. **Interface redesign (later):**
   - ~~An always-on minimap~~ Dropped (tester, 2026-10-10): the minimap is a gameplay unlock; it appears once a certain building is built, so Modern keeps that rule. The selection/build panel can still sit below it when it's there. The engine's minimap is `ARadar`, drawn by the interface overlay `0x47c000` via `0x5517b0` → `0x432eb0`.
   - Optionally restyle the HUD/panels into an Age of Empires-style bar along the bottom of the screen.
   - Keep the panel on screen, dimmed or deactivated, when nothing is selected, instead of having it disappear and reappear.
   - Exposed as a user setting: **Interface: Classic** (original layout and behaviour) **vs Modern** (minimap, bottom bar, persistent panel).
   - Builds on the engine-level interface layer: once interface drawing is redirected to its own port, elements can be placed freely.
2b. **Controls:**
   - Keyboard (arrow-key/d-pad) camera movement jumps in fixed steps. Make it scroll smoothly like mouse edge-scrolling.
   - Configurable camera keys: arrows and/or WASD, set in the ini. Opt-in, because it may clash with existing letter hotkeys, which need mapping first.
   - Quick-build hotkeys: the game has B/P/R/U/G for a single selected Shaman's Build/Plant/Repair/Upgrade/Get Genes modes, but no keys for the build list itself (16 structures) or the panel buttons. Candidates: Alt+letter or Ctrl+letter, which are free (see `re/hotkeys.md`).
   - **Unit groups already exist** (static analysis, 2026-10-10):
     - Ctrl+0–9 assigns (a byte at creature `+0x8d`, saved in save games and drawn next to the sprite);
     - 0–9 reselects, and dead units drop out because dead things are removed from the world;
     - Alt+0–9 reselects and centres the camera.
     Missing: double-tap to centre, and Shift+N to add to the selection (it acts like plain N); a unit can only be in one group.
   - Optional "modern" mouse scheme (AoE2 style): left-click selects, right-click moves/attacks/acts. The original scheme uses left-click for everything and right-click to clear selection; keep it as the default.
   - **Mouse map done** (static analysis, `re/mouse.md`, 2026-10-10):
     - Clicks reach SGame through vtable `0x507534` slots +0x1c..+0x30; the left click is resolved on button-up (`0x47e1d0`).
     - Classic: left on an own unit selects it (Shift toggles); with units selected, left on an enemy, a resource or the ground orders (`0x44c6d0`; move/target chosen by `0x450d40`, the same function that picks the cursor); left on an own building selects the building.
     - Right-up deselects, or moves the selection when on the minimap. Mouse events carry only Shift. The game ignores `WM_RBUTTONDBLCLK`, so a quick second right-click is lost.
     - Edge scroll zones are 35 px top/bottom and 30 px at the sides. The game never uses the wheel or the middle button.
     - Modern scheme plan: replace SGame's left-up, right-down and right-up slots (as `menu.cpp` does), and map `WM_RBUTTONDBLCLK` to a right-down.
   - **Builder on an unfinished building:** clicking a builder onto it only selects the building. Repair is the only way to resume construction: the builder's work tick runs the build step (`0x40c8a0`) until progress `+0x32` completes (`+0x2c`), then repairs HP. Repair refuses unpowered buildings (except type 0x2c), and switched-off ones (double-click turns them on).
   - **Controls added (`src/controls.cpp`, 2026-10-10; the tester's choices):**
     - **Unit groups:** Shift+N adds group N to the selection, and a double tap of N (released in between, within 350 ms) centres the camera. Ctrl+N, N and Alt+N are the game's own.
     - **Quick-build:** with a Shaman's build list open (B), a letter starts placing a building (`[Input] QuickBuild`):
       - R Refinery, G Gene Lab, L Relay, P Psi Tower, M Mine, T Turret, E Electric, N Listener, H Healer, W Warp, S Summoner, C Collector;
       - Shift+G/L/T/P for the upgraded versions.
       With the pod list open (P), 1–3 pick a pod. Unavailable letters keep their usual meaning, so S still stops.
       It does what the list button does: capture, then state 3 and `+0x188 = 0x2c+i`, then `0x442c90`; pods use state 5, `0x3f+i` and `0x471560(2, 0x58, 0)`. Availability comes from `APlayer::CanBuild 0x437b50(type)`, the check the list itself uses. The button word `+0x16` is the *pressed* flag, not "visible".
     - **Builder click** (`[Input] BuilderRepair=1`, Settings → Controls → Builder Click):
       - What it does: with builders selected, a plain left click on an own building that is switched on, powered and unfinished or damaged sends each builder the game's Repair order (`Cmd` vtable +0x88, (builderId, targetId)), plus the acknowledge voice.
       - Hook: SGame LButtonUp slot `0x507554`.
       - Every other click goes to the stock handler unchanged.
     - **Placement rules found while testing:**
       - Pods need terrain type 2 (the pale lavender ground), via `0x442b10`.
       - A Refinery needs a matching object within about 3–4 tiles (`0x4518c0` → `0x44e9d0`); the tester says that's something planted nearby. Validator `0x4517e0` returns 2 when it's missing.
     - **Building and planting into fog** (`re/fogbuild.md`; `[Input] BuildInFog=1`):
       - Placing a *building* on fogged ground already worked in the stock game: no validator has a fog test, and the Shaman walks there and builds. Only the ghost is hidden there, because effects in non-visible 64 px cells aren't drawn (`0x41e0b0`).
       - When a placement really is refused, it is usually code 2: no power in range, or no pod near a Refinery.
       - *Pods* were refused in fog: the plant click check `0x442b10` requires the cell to be currently visible.
       - Now a plant click on fogged battlefield ground sends the stock plant order (`Cmd+0x58`, a one-point path), so the Shaman walks there and plants, or does nothing if the spot isn't pod soil.
       - In build/plant mode the cursor over fog is the move cursor rather than a plain arrow.
       - Hooks: AShaman click-down `0x504a44` and cursor `0x504a50`. Panel and minimap clicks go to the stock handler.
       - Verified headless: the Shaman walked about 600 px into fog to the spot; nothing was planted there (not soil).
       - Quick-build now seeds the ghost with world coordinates (`0x47ec60/0x47ec80`).
     - **Modern mouse scheme** (`[Input] MouseScheme=classic|modern`, Settings → Controls → Mouse Scheme; Classic is the default):
       - **Left-up** (SGame slot `0x507554`):
         - a plain click only selects: the object under the cursor replaces the selection (clear, then the stock `World::Click 0x44a860`, which only selects when nothing is selected);
         - Shift+click adds or removes own units;
         - empty ground clears the selection.
         Modes (build/plant/repair), held-key modes (A, G, C, T, Space, Tab, PgUp/PgDn), drag boxes, the minimap and the panel keep the stock behaviour.
       - **Right-up** (slot `0x50755c`):
         - in a build/plant/repair mode, leave the mode and keep the selection (deselect and reselect the captured Shaman);
         - otherwise the stock order the cursor shows (`World::OrderAt 0x44c6d0`: move or target);
         - builders onto an own unfinished or damaged building: Repair;
         - units onto building types 0x34/0x39: the stock enter order;
         - on the minimap: stock (move there).
       - Right-down is the stock handler, which already cancels a pending left click.
       - In both schemes, `WM_RBUTTONDBLCLK` is turned into `WM_RBUTTONDOWN`, because the game drops it.
       - Headless test `[Debug] Autopilot=9` (with `MouseScheme=modern`) verified each step: left selects; left on ground deselects; right on ground moves and keeps the selection; right in plant mode cancels and keeps it. The Classic mission regression still passes.
       - **Shift+drag adds a box to the selection** in both schemes. The stock game turned Shift+drag into a Shift+click at the release point, because `0x4804c0` turns off the drag box while Shift is held. Our left-up hook does the 32 px drag test itself and calls `World::BoxSelect 0x44afe0(&rect{x0,y0,x1,y1}, ground, air, add=1)`, honouring PgUp/PgDn. Verified headless (`Autopilot=9`): nothing selected, then a Shift+drag round the Shaman selects it.
     - **Group build** (player request; `[Input] GroupBuild=1`, Settings → Controls → Group Build): several builders selected together can build. See the 2026-10-11 log.
     - **Headless test:** `[Debug] Autopilot=8` (`AutopilotTarget` = the quick-build key) selects the Shaman, plants a pod (P, 1), quick-builds, stops, clicks the building, then tests the groups. It logs the selection state, own buildings and the valid spots.
       - Verified: quick-plant planted on the first click; quick-build entered Refinery placement; Ctrl+1, the double tap and Shift+1 all worked; the mission regression still passes.
       - The builder-click repair needs a real unfinished building, and a fresh skirmish has no valid Refinery spot until a pod grows, so the tester checks that one in play.
2c. **Cutscenes (in progress):** the GOG copy ships all the movies (`MOVIES\*.MOV`, QuickTime); they play in VLC but not in the game, because QuickTime for Windows (`QTIM32.DLL`) isn't included. Fix: our own `_EntryPoint` replacement plus a built-in Cinepak/IMA4 decoder (see the 2026-10-10 log).
   - No `Movies\` override folder is needed: the movies aren't missing, just unplayable (tester, 2026-10-10).
   - **Movie scaling (tester request): done.** Settings → Video → `Movie Scaling` (`[Render] MovieScaling=enhanced|smooth|sharp`) and `Movie Size` (`[Render] MovieSize=full|original`), done non-generatively on the GPU at playback (see the log).
     - Options: Enhanced (default: Cinepak deblocking and debanding, Lanczos-2 upscale clamped against halos, CAS-style sharpening), Smooth (bilinear), Sharp (crisp pixels). Movie Size: Full Width or Original (the size it had on a 640x480 screen, scaled to the screen height).
     - No AI upscaling and no frame interpolation; the movies stay at their original 12 fps.
     - The movies are about 3.2:1, so they fill the width: about 2560x800 on 16:9 at 1440p, about 3440x1075 on 21:9.
2d. **Alt+F4** should quit the game, through its normal shutdown path where possible.
2e. **Frame rate (tester request):**
   - An FPS counter (`[Render] ShowFPS=1`; done) and a **max FPS** setting, both on the Settings page.
   - The tester finds the main-menu animations very fast. The game composes about 64 frames/s, one per Windows timer tick, both under GOG's DDrawCompat and with our renderer, so the patch hasn't sped anything up. The animations most likely advance once per frame and were tuned for slower 1998 machines.
   - To find out before adding a cap: is only animation tied to the frame rate, or game logic too? A cap must not change gameplay speed unless that is the original intent.
   - Measured on the main menu with `[Debug] GlobeMeter=1`: the globe changes every second frame (32 changes/s at 64 fps). That is identical through our renderer and through GOG's DDrawCompat (renderer off). The tester still sees the untouched GOG copy's globe as slightly slower; a screen-video comparison is the next step if it matters (ffmpeg is available; needs the tester's screen, so ask first).
2f. **Documentation (tester; required before release):** document every addition and finding for the PCGamingWiki page and the GitHub README:
   - all ini options, including `[Debug]` ones;
   - hotkeys: F2 FPS counter; debug F10/F11/F12; wheel / numpad +/- zoom; Alt+F4;
   - **controls reference done:** `docs/controls.html` (published as a private artifact, 2026-10-11) lists every stock mission key and mouse action, then the HD additions. Keep it in step with `re/hotkeys.md`, `re/mouse.md` and new controls;
   - what each fix does, and the known limitations;
   - the main-menu Settings page: click steps a value forwards, right-click backwards, and every change is saved to `WarBreedsHD.ini` immediately (there is no separate Save; Back just returns to the menu).

   Note: the game itself binds F9 and F10 in missions (key table at `0x47fd14`), so the debug F10 hotkey must move before release. F1–F8 are **not** free either: they are the formation slots (apply / Ctrl save / Alt apply + centre). F11, F12, Alt+letters and most Ctrl+letters are free. The full mission key map is in `re/hotkeys.md`.
2g. **Multiplayer (tester, 2026-10-10; before the level editor):**
   - **Fix the shipped LAN multiplayer:** it uses DirectPlay (DPLAYX.dll is imported; Windows 98-era), which is disabled or broken on modern Windows. Investigate what fails (DirectPlay availability, IPX/TCP service providers, enumeration) and make LAN play work, e.g. a DirectPlay replacement or shim.
   - **Online play:** direct IP connection first; then possibly a lobby browser via a small Cloudflare Worker (players register and list games, then connect peer-to-peer or through a relay).
   - Max FPS setting dropped (tester request): the game always runs at its fixed ~64 fps tick whatever the game speed.
   - Alt-tab during a cutscene ends it like Space (tester confirmed); that's acceptable.
3. **Level editor** (after 1 and 2): check feasibility. The retail game has no editor exposed. Work out whether one is hidden in the exe or whether one would need building, and whether the in-game menus need rebuilding to reach it. Read the GOG forum threads first: <https://www.gog.com/forum/warbreeds#1756555827>.
4. **Linux / Proton**: check that the patched game runs under Proton/Wine. Likely just configuration, e.g. `WINEDLLOVERRIDES` for `ddraw`/`_inmm`/the patch DLL. Document the steps for the wiki's Steam Play section.
5. Later, optional: gameplay patches and other modernisation.
6. **Release:** a public GitHub repo with source, a README, and install instructions (which files go where, GOG vs Steam, Proton notes). Licensing: our code MIT; cnc-ddraw (MIT) credited with its licence included. No game files or videos in the repo.
   - **Packaging and installer (tester request):** a GitHub Releases download that is easy to install on **Windows and Linux**. Either an installer executable or a script that works on the folder we provide. It should:
     - find or ask for the game folder (GOG Galaxy, standalone GOG, Steam; Wine/Proton prefixes on Linux);
     - copy `wsock32.dll` and a default `WarBreedsHD.ini`;
     - never overwrite the user's own ini;
     - offer uninstall;
     - on Linux, set or print the `WINEDLLOVERRIDES="wsock32=n,b"` launch option.

   - **First shareable build: v0.10.0** (2026-10-11), made by `tools/package.py` as `dist/WarBreedsHD-0.10.0.zip` (37 MB):
     - the zip holds `wsock32.dll`, `Fonts\` (Orbitron and its OFL), `Menus\`, `README.txt` and `LICENSE.txt` (both from `release/`), and `Controls.html` (`docs/controls.html` with its own document skeleton);
     - installation is manual, as in the README: copy the DLL and the two folders next to `WARBREED.EXE`;
     - The tester chose to include the widescreen menu art. It ships as JPEG quality 95 with no chroma subsampling: 126 MB as PNG becomes 36 MB. The seam step across the 4:3 edge measures the same as with the PNGs.
     - Verified on a clean copy of the GOG install: unzip, follow the README, then run the Settings test and the full mission test headless; both passed. No ini ships (the patch writes its commented default on first run), and no `DDrawCompat.ini` appears with the Direct3D renderer.
     - An unrecognised `WARBREED.EXE` (e.g. Steam, untested) gets a message box, and the patch changes nothing.
     - The installer and the Linux/Proton test are still to do.
## Investigation log

### 2026-10-10
- Found the install, its config and its logs. DDrawCompat's log shows the game was run once at default settings.
- Found resolution width/height stored in `prefs.dbg` (0x24: `80 02 E0 01` = 640, 480). So the engine reads its resolution from settings and doesn't hardcode it everywhere. Promising for widescreen.
- Dumped PE headers and imports (`tools/peinfo.py`). There is no static `ddraw.dll` import, so DirectDraw is loaded at runtime (`LoadLibraryA` / COM).
- **The engine already supports several resolutions.** The function at `0x492150` fills a 5-slot table at `0x579490`. Each slot is 8 bytes: vtable, then `short w, short h`.

  | Slot | Default |
  |---|---|
  | 1 | 640x480 |
  | 2 | 800x600 |
  | 3 | 1024x768 |
  | 4 | 1152x864 |
  | 5 | 1280x1024 |

  - The labels are copied from string resources into 20-byte buffers at `0x579418`.
  - The startup loop at `0x4922ae` keeps only the slots that pass `0x4d8a76`, which appears to be a "display mode available?" check. Indices of the passing slots go to `0x57940c` and the count to `0x5794b8`. The options screen lists only those.
  - GOG's DDrawCompat only advertises 640x480/800x600/1024x768 by default, so 1152x864 and 1280x1024 are probably never offered on the GOG release.
- Current resolution is held as a short RECT at `0x5508f8` (`0, 0, w, h`). The default 640x480 is set at `0x468b8a`.
- Picking a resolution in the options list calls `0x46fa80(w, h, 256, 1)`, which changes resolution live. `0x469630` applies a new size only if `w >= 640 && h >= 480`; it then calls `0x46a050` to rebuild the display and `0x468bc0`. Live switching is the planned mechanism for camera zoom.
- Experiment 1 (prepared): in a test copy, set slot 5 to 1920x1080 by changing the immediates of `push 0x400` at `0x4921b2` and `push 0x500` at `0x4921b7`. Also set prefs to 1920x1080 and add `SupportedResolutions = native, 640x480, 800x600, 1024x768, 1920x1080` to `DDrawCompat-WARBREED.ini`. Script: `tools/exp_slot5.py`.
- **Experiment 1 result: widescreen works.** Launched the test copy. The main menu stays at 640x480 (4:3 artwork, pillarboxed by DDrawCompat). There is no Options item on the main menu, so the resolution picker isn't reachable from there. On entering a mission the game ran at **1920x1080, filling the whole 16:9 screen**. DDrawCompat scaled it to the 2560x1440 desktop with no pillarboxing, and the visible map area grew accordingly. Capture: `re/shot2_1080.png`, taken with `tools/wincap.py` (PrintWindow on the game window only). The tester checked by hand: mouse clicks land correctly across the whole screen, edge scrolling reaches all four edges and the far side of the map, the HUD/panels are placed sensibly, and there are no redraw glitches beyond the old 640x480 area.
- `0x46fa80(packed w|h<<16, h, 256, 1)` is the live resize routine:
  1. `0x4695f0` sets the DirectDraw mode (with `this` = `0x551558`).
  2. `SetWindowPos` resizes the game window (HWND at `0x579290`).
  3. Every registered view (`0x55aea8[0..count]`, count at `0x51bee8`) gets a resize callback through vtable +0x4c.

  Quirk in `0x469630`: the mode change is skipped if *either* the width or the height equals the current value (it should require both). So 1920x1080 → 1920x1200 would be silently ignored.
- **How each screen picks its resolution:**
  - Every "scene" object (menus, mission) stores the resolution it wants at +0x28/+0x2a. The constructors at `0x4769e3` and `0x494fb7` default these to 640x480.
  - When a scene activates, `0x4715d3` / `0x473aeb` pass those values to `0x46fa80`.
  - The options/prefs loader `0x491cc0` reads `OPTNPRF*`. At `0x492040`–`0x49211b` it searches the *available* slots for the saved width/height and **falls back to slot 0 (640x480) if it isn't found**. That is why the experiment needed both the table patch and the prefs edit.
  - Patch strategy: hook `0x46fa80` and substitute the configured or zoomed resolution whenever the mission scene requests a mode. The table and prefs then don't matter.
- **Scene classes seen in logs (RTTI names):**

  | Class | Screen | Resolution |
  |---|---|---|
  | `SRedOrbCut` | Red Orb logo | 640x480 |
  | `SIntroCut` | Intro | 640x480 |
  | `SWarBreedsScreen` | Main menu | 640x480 |
  | `SHeadquarters` | Headquarters | 640x480 |
  | `SMultiSetup` | Skirmish/multiplayer setup | 640x480 |
  | `SProgress` | Loading | 640x480 |
  | `SGame` | Mission | Prefs/options resolution |
  | `SOption` | Options | Exists, but not reachable from the main menu |
  | `SCut` | Cutscenes | |
- **Patch v0.1.0 (wsock32.dll proxy) works on the unmodified GOG exe.**
  - Missions start at 1920x1080 on a 2560x1440 desktop with no exe edits.
  - Mouse wheel / numpad +/- step through 1280x720, 1600x900, 1920x1080 and 2560x1440 by calling the game's own `0x46fa80`. Each step takes about 0.1 s.
- **Problems the tester reported with v0.1.0 zoom:**
  1. The HUD scales with the battlefield. A zoomed-out view gives a tiny HUD; he wants it a fixed size.
  2. The desktop flashes between steps. This is the `SetWindowPos` in `0x46fa80` shrinking the window to the new mode size.
  3. The zoom is anchored top-left instead of centred.
  4. Too jumpy: one wheel notch can fire several steps.
- **Intermittent startup crash (watching):**
  - Two launches at 10:50 crashed about 1 s after start with an access violation in DDrawCompat (`DDRAW.DLL` v0.6.0, offset `0xb0246`), before the first mode set.
  - Both were launched right after a previous instance had been force-killed.
  - Seven later launches with identical settings didn't reproduce it, including with every patch feature toggled on and off.
  - Not yet known whether the unpatched GOG game does this too.
- **Patch v0.2.0 test** (tester request):
  - Zoom now steps one level per notch and goes through the frame tick.
  - The desktop no longer shows through.
  - A **black flash remains on each step**. It is DirectDraw/DDrawCompat recreating surfaces on the mode change and can't be avoided while we use real mode changes.
  - Alt+F4 quits cleanly; the watchdog wasn't needed.
- **The tester wants smooth zoom.** Planned in our own renderer:
  1. An animated transition: GPU-scale the last frame from the old to the new zoom while the game switches size underneath, so no black frame is shown.
  2. Stretch goal: continuous zoom, if our own DirectDraw makes a "mode change" cheap enough to resize every frame.
- Camera recording (`tools/scan.py`): the camera is not in `.data` or the first 0x1000 bytes of the `SGame` object or the objects at `0x579260`–`0x5792bc`. `0x5792ac` climbs steadily and looks like a clock.
- `0x47b520` (called from the `SGame` resize handler, `this` = `SGame`) stores the map view size at `SGame+0x4c/+0x4e`.
  - **If `SGame+0x5e == 1` the view size is doubled.** This may be a built-in 2x zoom-out mode; to investigate.
  - Map size in tiles is the bytes at `0x566db4` (w) and `0x566db5` (h); tiles are 32 px.
- **Camera found** (confirmed against the recording):
  - The camera is `SGame+0x38` (x) and `+0x3a` (y), shorts in world pixels.
  - Scroll bounds are at `+0x44..+0x4a`.
  - `0x47d340` clamps the camera so that camera + view stays within the bounds.
  - The test map is 5632x5888 px. Keyboard scrolling moves about 1,500 px per 0.25 s in large jumps, which is the "set jumps" the tester noticed.
- **The game has a built-in 2x zoom-out on the Z key.**
  - The `SGame` key handler (`0x47f207`: `cmp bl, 0x5a`) calls `0x4802a0`, which toggles `0x47d430(1/0)`.
  - That function doubles or halves the view size (`+0x4c/+0x4e`), sets `+0x5e`, and re-centres the camera.
  - It only works if the doubled view fits inside the map.
  - The key-dispatch jump table at `0x47f576` will be useful for the WASD and quick-build work.
- **Patch v0.3.0:** `[Zoom] Mode=native` (default) maps the mouse wheel / numpad +/- to the game's own Z zoom, called from the frame tick.
  - The tester: zoom is instant and centred, the HUD stays fixed and there's no flicker.
  - Only two levels (1x and 0.5x), which isn't enough. **Smooth multi-level zoom needs our own renderer** (next phase).
  - `Mode=resolution` keeps the v0.2 resolution-stepping zoom.
- **Renderer phase, attempt 1 (cnc-ddraw):** cloned cnc-ddraw (MIT, commit `279a057`, 2026-08-30) into `third_party/` and built it with MinGW. Under it, WarBreeds showed only a ~106x160 top-left block of each frame, with every renderer (d3d9/opengl/gdi).
  - The debug build's call log explains why: **WarBreeds never creates a DirectDraw surface.** Its only DirectDraw calls are `EnumDisplayModes`, `SetDisplayMode`, `SetCooperativeLevel`, `GetCaps` and `QueryInterface`.
  - All drawing is **GDI**: the Mohawk engine renders into 8-bit DIBs and copies them to the window, with lots of `SelectPalette`/`RealizePalette`. DirectDraw is used only to change the display mode.
  - That's why DDrawCompat, which captures GDI output, works and cnc-ddraw doesn't.
  - Wiki note: DirectDraw wrappers that focus on surfaces (cnc-ddraw, and probably dgVoodoo) won't display this game properly.
- **New renderer design:**
  1. A minimal `ddraw.dll` stub that fakes mode changes. No real mode change means no black flash.
  2. Intercept the GDI calls that copy frames to the game window.
  3. Present the game's own 8-bit frame and palette on the GPU, with world zoom and fixed-scale HUD compositing.
- **GDI trace** (`[Debug] GdiTrace=1`, `src/gditrace.cpp`):
  - The game presents with exactly **one `StretchBlt` per frame**, from `0x4dc87e`. It copies a full-screen 8-bit DIB section (640x480 in menus, 1920x1080 in missions) 1:1 to the window, at about 64 fps.
  - No other GDI path draws to the window, and the HUD is already composited into that DIB.
  - The exe's import table is already bound to gdi32 when our DllMain runs, so IAT hooks installed at load time see every call.
- **Patch v0.4.0: own renderer.** `[Render] Renderer=d3d11` (default) / `gdi` / `off`.
  - `src/render/ddfake.cpp`: hooks the exe's `LoadLibraryA`/`GetProcAddress`/`FreeLibrary` imports so `DDRAW.DLL` resolves to a fake `IDirectDraw`/`IDirectDraw2` inside our DLL. It lists the modes and "sets" them instantly (2–3 ms against 70–100 ms through real DirectDraw), never changing the real display. **GOG's `ddraw.dll` is no longer loaded** and can stay in place.
  - `src/render/render.cpp`:
    - Intercepts the frame `StretchBlt`/`BitBlt` and copies the DIB pixels and colour table.
    - Keeps the window borderless at monitor size.
    - Maps the mouse between window and game space (`WM_MOUSE*`, `GetCursorPos`, `SetCursorPos`, `ClientToScreen`, `GetClientRect`, `GetSystemMetrics(SM_CX/CYSCREEN)`).
  - `src/render/d3d11.cpp`: palette lookup to RGBA at game resolution, then aspect-fit scaling with sharp-bilinear, bilinear or nearest filtering. Shaders are compiled at runtime with `d3dcompiler_47`.
  - First run: main menu renders correctly (palette, 4:3 pillarbox, sharp scaling).
  - Seen at startup: the game sets 2560x1440x32 (the desktop mode) once, then 640x480x8. Cutscene scenes request depth `0xffff`, which arrives as 16 bpp.
- **Frame pipeline (mapped from the disassembly; replaces the after-the-fact pixel-diff attempts):**
  - Compose tick `0x46f400` draws every scene bottom to top. SGame::Draw `0x47b770`:
    1. a record-only pass `0x47c000(0)` that marks 64 px world tiles dirty;
    2. the world (dirty tiles) and sprites;
    3. **interface overlay `0x47c000` (call at `0x47b9a7`)**: HUD widgets, HUD frame shape, info panel (pre-rendered in scratch port `[0x5509bc]`, then copied in), mission cursor `0x47c3d0` → `0x47c950`;
    4. Scene::Draw `0x495090`, SGame's widgets.
  - Then overlay scenes (the Esc menu SMenu `0x579118`, pushed by `0x471580` and popped by `0x471690`), the top scene's cursor (`0x495800`), tooltip text `0x499cb0`, and frame-ready `0x46f54e`.
  - Present `0x4693a0` copies the back buffer `[0x5509a4]` to the window port `[0x55099c]`. That ends in the one StretchBlt.
  - All drawing addresses "the back buffer" through the global handle `[0x5509a4]`:
    - `0x469140` selects it;
    - `0x469180` refreshes the blitters' pixmap cache at `0x550820`;
    - `0x469250` clears it.
  - Ports are created with `0x4d83a0(w, h, 8, 0)` and disposed with `0x4d9285` (both stdcall); a handle is the port object pointer.
  - Missions redraw fully every tick (`[0x51d9a8]=0`, no cursor save-under). Menus use dirty redraw with a save-under cursor.
  - Index 0 is never used by the game's art.
- **Patch v0.8.0, engine-level layers (`src/layers.cpp`):** in missions the back-buffer handle is swapped for whole phases.
  - At `0x47b9a7`: an engine port I (interface), cleared.
  - Around the cursor draws `0x47c950` / `0x495800`: port C (cursor).
  - At `0x46f54e`: copy I and C out, restore the back buffer.

  The back buffer then only ever contains battlefield, and I/C are rebuilt every frame (no stale pixels, trails or ghosts). The renderer finds interface elements automatically (connected 16 px tiles of I) and draws them, and the cursor, at a fixed interface scale (default = Max zoom). Menus are untouched.
- v0.8 implementation notes:
  - `SetPort` (`0x4d85fa`) silently refuses a port whose pixels aren't locked (port +0x68), so new ports need `LockPort` `0x4d863f` straight after `CreatePort`, and `UnlockPort` `0x4d92d3` before `DisposePort`. Without that, the first build drew everything into the back buffer and showed a black screen.
  - The first mission frame presents straight through `0x4693a0` without reaching `0x46f54e`, so the present entry also closes an open interface phase.
- **Automated, invisible testing (for the patch's own development):**
  - `[Debug] Autopilot=1` (`src/autopilot.cpp`) drives main menu → Skirmish → Start, selects the starting Shaman, zooms out and in (capturing mid-animation too), opens and closes the Esc menu, saves the screen and each layer at every step, and quits. Input is posted to the window with a virtual cursor, so the real mouse and keyboard are untouched.
  - `[Debug] Headless=1` (`src/headless.cpp`) creates the game window hidden, off-screen and non-activating, and reports no audio devices.
- **v0.8 verified headless (2026-10-10):**
  - The game window was never visible or foreground.
  - The interface layer held the panel at exactly the same game-pixel size and offset at every render size, from 1280x720 to 3358x1888, including two mid-animation frames in each direction. On screen it is the same size and position in every capture.
  - The Esc menu is drawn at the fixed scale with a visible cursor, and leaves nothing behind when closed.
  - The battlefield layer contains no interface.
  - Re-run with `NoInterfaceLayers=1` for comparison: **the stock game also hides the info panel while the Esc menu is open**, and the camera ends at the same spot after a zoom round trip near the map edge. Centre-anchored zoom loses position when the camera is clamped at the edge; anchoring zoom on the cursor is a candidate improvement.
- First interactive v0.8 test: clicks in missions missed. Mouse mapping checked the on-screen interface boxes first, and the cursor layer (always under the mouse) counted as one. Every position was therefore mapped relative to the previous game cursor at the interface scale, and the game cursor drifted away from the drawn one. Fixed by excluding the cursor layer from mapping. The autopilot now feeds its virtual mouse through the same client→game mapping as the real mouse and glides it in small steps before clicking; that reproduced the bug (game cursor 583,306 against 405,212) and confirms the fix. Run everything with `tools/autotest.sh`.
- **Order markers drawn in the wrong place.** The tester saw clicks "land" away from the cursor, though the orders themselves went to the right place. The overlay function `0x47c000` starts with world-space drawing:
  - the effects list `[0x579280]` → `0x423c20`, which includes the click/order markers (found with the write-breakpoint tracer on the interface port);
  - building placement and drag box `0x47bc90`;
  - world markers `0x47bdc0`.

  These ran inside our interface phase, so they were scaled and anchored like HUD elements. The interface phase now begins at `0x47c072`, after that world-space part (armed by the drawing call at `0x47b9a7`, so the record-only call from `0x47ce2e` is ignored). Markers now land exactly under the cursor at every zoom and scale with the battlefield. The minimap (`0x5517b0` → `0x432eb0`) stays in the interface.
- **FPS counter:** `[Render] ShowFPS=1` draws the composed-frame rate top-left (GDI text into a texture). The log also records it every 10 s. `[Debug] ClickLog=1` logs how each click is mapped.
- **Zoom performance.** The tester saw ~20 fps while zooming. `[Debug] Profile=1` (`src/prof.cpp`: per-stage timings every 250 ms, plus an instruction-pointer sampler) showed that every animation frame called the game's SetResolution, at 40–50 ms each, and the following redraw cost up to 110 ms per frame zoomed out. Steady play at any zoom is ~64 fps with a few ms of our own work.
  - The sampler put the time in GDI syscalls: the window resize at `0x46faf6` and the engine's surface rebuild via `0x469633`.
  - Fix: the animation no longer resizes every frame. The game renders a frame big enough for the widest view still to come, and the renderer shows the current magnification's part of it (`render::SetWorldCrop`, fractional, so the motion stays smooth; mouse mapping follows the crop). Zooming out resizes once at the start, zooming in once at the end.
  - Redundant window work on mode changes is skipped when the window already covers the monitor.
  - Result: one ~35–45 ms resize per zoom gesture instead of every frame.
  - **The remaining 35–45 ms was a hidden display-mode change.** Besides DirectDraw, the engine's mode switch (`0x4695f0` → `0x4d8943` → `0x4d71dd`) calls `ChangeDisplaySettingsA(mode, CDS_FULLSCREEN)` directly. The import slot is `0x59599c`.
    - It does this on every resolution change, including 640x480 8 bpp for each menu screen and 2560x1440x32 at startup.
    - Modern Windows rejects 8 bpp modes, so nothing visible happened, but each driver round-trip cost ~25 ms or more.
    - With our renderer the call is hooked to report success without touching the display. Resizes now take <2 ms, and zooming stays at 60–70 fps (worst frame ~25 ms).
    - Wiki-relevant: this is also why menu transitions were slow.
  - Profiling notes: the sampler thread must run at time-critical priority, because the game is pinned to one CPU, and it needs a 1 ms timer period. Time spent inside kernel calls shows up only at the syscall return. `tools/samples.py` maps samples to game functions.
- **F2** toggles the FPS counter and saves it to `[Render] ShowFPS`.
- **Main menu research** (from the disassembly):
  - The main menu is the **SMissionMenu** scene (vtable `0x508304`, scene id 0x13, MSMENU.MHK). SHeadquarters only draws the cave and orb, and pushes it with `0x4709a0(0x1f, 0x13, 5)`.
  - Items are added in Init (`0x48fcf0`, slot 5) with `Scene::AddTextButton` `0x495cb0(text, cmd, hotkey)`, styled with `0x49a500(0x10, 0x11)` (font WBLg24).
  - Item rects: rows 28 px apart from (18,17). Strings come from the exe string table: 104 Load Game … 112 Quit. Unused string 107 "Options" suggests an Options item was cut.
  - Clicks go to OnCommand (`0x48ff40`, slot 27), which switches on the button's command id at `+0x20` (0x64–0x6b).
  - Scene changes are queued with `0x4709a0(type, sceneId, delay)`.
  - **SOption** (scene 0xd, "Game Options": resolution and game-speed sliders) *is* reachable, from the in-mission Esc menu. It isn't suitable for our settings.
- **Settings page (v0.9, `src/menu.cpp`):** two SMissionMenu vtable slots are replaced; there are no code patches.
  - **Init** adds a "Settings" item between Credits and Quit (Quit moves down a row), plus the settings page's buttons, hidden.
  - **OnCommand** handles three cases: Settings swaps the pages; clicking a setting cycles its value, writes the ini and applies it live (interface size, zoom limits, key scrolling, WASD, scroll speed, scaling filter, VSync, FPS counter, Alt+F4); Back returns to the main page.
  - The text-button fields used: `+0x0c` rect, `+0x14` hotkey, `+0x1a` needs-redraw, `+0x20` command, `+0x2c` text (SetText `0x49a470`), `+0x74` visible.
  - Hit-testing and hotkeys ignore `+0x74`, so hidden buttons are also moved off-screen and lose their hotkey.
- **Hover highlight:** the game only highlights a text button (yellow, style `+0x70`) while it is pressed; its draw `0x49a3a0` checks `+0x16`/`+0x18`. Text buttons in the top scene now highlight under the mouse: a per-tick hover check plus a draw wrapper (vtable `0x509260`) that uses the highlight style for the hovered button, without the pressed 1 px nudge.
- **Autopilot mode 2** (`[Debug] Autopilot=2`) is a headless main-menu test: it opens Settings, changes two values and goes Back, capturing the screen at each step.
- **Settings page additions:** right-click steps a setting backwards (WndProc `WM_RBUTTONDOWN` → `menu::OnRightClick`, which uses the hovered button). There is now a hint line in style 25 (WBMd14). Button text is limited to 59 characters.
- **v0.9.0 additions:**
  - **Reset to Defaults** on the Settings page asks for a second click ("Click again to reset all settings"), then writes every setting's default back to the ini and applies it.
  - **"WarBreeds HD v0.9.0"** shows bottom right on the main menu (and the Settings page) in WBMd14, so it's clear the mod is running. The version now lives in `src/version.h`.
  - `[Compat] CPU=n` pins the game to the n-th allowed CPU instead of the first. The test copy uses it so it doesn't share a core with a copy being played.
- **Test setup:** automated tests run from `testrun/`, a separate copy of the game folder whose ini is always headless. `testgame/` is for playing. `tools/autotest.sh [mode] [seconds]`: mode 1 is the mission test, mode 2 the main-menu Settings test; `PROFILE=1` adds profiling. `tools/smoke.ps1` refuses to start a copy that is already running.
- **Widescreen menus (v0.9):**
  - **Backgrounds:** a 4:3 menu screen can have a wider background image, `Menus\<Scene>.png` (`.jpg`/`.bmp` also work; the main menu is `Menus\SHeadquarters.png`, named after the base scene class). It's loaded through WIC when the scene changes and drawn by the renderer to fill the screen height, under the game's frame. `[Display] MenuAlign=centre|left` places the 4:3 frame, and the background's original part, in the middle or at the left edge. With no image the sides stay black.
  - **Clickable areas never move:** the game still draws and hit-tests its own 640x480 menu.
  - **Free cursor in menus:**
    - `Scene::UpdateCursor` (`0x4950e0`) restores the saved-under pixels, saves under the new position and calls the scene's cursor draw (vtable `+0xc`). There are 11 variants, e.g. `0x495800`, `0x494530` (SMissionMenu), `0x487e50` (SHeadquarters), all drawing a cursor-sheet frame at `[0x5794d8]`/`[0x5794dc]`.
    - The call at `0x49512f` is redirected: the cursor is drawn into a private 128x128 port at its centre, and the renderer draws it at the real mouse position, at the menu's scale.
    - The menu image never contains the cursor, the save-under logic only ever sees clean pixels, and the cursor follows the mouse over the widescreen sides (clicks there land at the frame edge).
    - Menus are re-presented every tick so the cursor never lags.
  - **Art:** `art/menu/` has the clean main-menu background (captured with `[Debug] Autopilot=3`, which hides all menu text), centre/left canvases at 16:9 and 21:9 for AI outpainting, and the prompts (`art/menu/README.md`).
- The settings hint line now shares the version label's baseline (y 452–468).
- Flaky test note: once in several mission runs, the autopilot's first Esc keypress didn't open the Esc menu (the next run was fine). It's probably timing, with the key arriving mid-order; watch for it.
- **Menu backgrounds per screen:**
  - The background is looked up top scene first, then down the scene stack. Dialogs and screens that sit on the main-menu cave art (Campaign `SSinglePlayer`, Load Game `SOpenDialog`, mission briefing `SMissionBrief`, error `SDialog`) therefore use `Menus\SHeadquarters.png` automatically.
  - Screens with their own art: title `SWarBreedsScreen`, skirmish setup `SMultiSetup`, Gene Lab `SGeneLabScene`, Device Lab `SDeviceLab`.
  - Credits (`SCredits`) cycle through concept-art slides, so they keep black bars.
  - `[Debug] Autopilot=4` with `AutopilotTarget=0..7` opens a main-menu item (7 = Campaign → Basic Training briefing) and captures `auto-screen-<TopScene>`.
  - `tools/menuart.py` builds `art/menus/<nn>-<screen>/`: the capture, a 4x upscale, canvases (centre/left x 16:9/21:9) and a tailored Gemini prompt per screen.
  - **`art/` contains derivatives of the game's copyrighted art: keep it out of the public repo** (.gitignore). The release ships only the code, and users make or obtain their own images.
- **Widescreen menu art made with Gemini (driven through a browser):** main menu, title, skirmish setup, Gene Lab and Device Lab. Each has a centred 21:9 version (3440x1440) and a 16:9 one (2560x1440).
  - The renderer prefers `Menus\<Scene>-21x9.png` on screens wider than 2:1 and `Menus\<Scene>.png` otherwise, falling back to the other.
  - Pipeline (`tools/menuwide.py`, `tools/fitpart.py`), one Gemini image per screen:
    1. The original is placed, scaled, in the middle of a 16:9 canvas with the proportions of a 21:9 band, grey all round. Gemini (Create image, 16:9) fills the grey.
    2. The original is located in the result (scale and offset search) and the 21:9 band is cropped and scaled to 3440x1440.
    3. The untouched original is pasted back. Each new side is colour-graded to the original strip beside it, and the colours are eased to meet the original's edge.
    4. 16:9 is the middle 2560 px.
    5. Where joins show, a second Gemini pass repaints 160 px bands just outside both edges of the original (both fit in one 16:9 window); these are feathered in.
  - Gemini web only offers aspect ratios up to 16:9; generating 21:9 as one band avoids the seam you get from extending twice.
  - The grading strip is per screen: 32 px by default, so a UI panel near the edge doesn't skew it; 240 px for skirmish.
  - Left-aligned attempts are kept in `art/menus/<screen>/archive/left-aligned/`.
  - Upload trick for Gemini's file picker: patch `HTMLInputElement.prototype.click` so the file input is attached to the page instead of opening the OS dialog, then set the file directly.
- **Menu Art setting (tester; for players who'd rather not have AI-generated art):** `[Display] MenuArt=widescreen` (default) or `original`, the first item on the Settings page, applied live.
  - **Original:** no generated images; 4:3 menu screens at the left edge, black beside them.
  - **Widescreen:** the backgrounds, menus centred.
  - Replaces the earlier `MenuAlign` option.
  - Wiki/README note: the widescreen menu art is AI-assisted (Gemini outpainting of the original screens). It's optional, and the original art is one click away.
- **Cutscenes, investigation (2026-10-10):**
  - The GOG copy ships all eight movies in `MOVIES\` as QuickTime files: Cinepak video at 12 fps and IMA4 ADPCM 22050 Hz stereo audio.

    | Movie | Size | Length |
    |---|---|---|
    | OPENING | 640x220 | 168 s |
    | ENDING1 | 640x200 | 127 s |
    | ENDING2 | 640x200 | 84 s |
    | MAGHA1/2, TANU1/2 | 640x200 | 23–30 s |
    | REDORB | 320x240 | 16 s |

  - **Why they don't play:** the game uses QuickTime for Windows 2.x through static glue. At `0x4eb420` it does `LoadLibraryA("QTIM32.DLL")` and `GetProcAddress("_EntryPoint")` into `[0x520f0c]`. About 100 stubs from `0x4eae6a` call it as `push ebx; mov bx, <selector>; call [0x520f0c]` with cdecl args.
    - QTIM32.DLL isn't shipped, so every call goes to the fallback `0x4eae3c` (returns dx:ax = 0x8000:0x8001), and the cutscene scenes end at once.
    - Mohawk also loads qtim32.dll (`0x4e274b`) to route its waveOut calls through the engine's audio proxy.
  - **Our own decoder** (`src/movie/mov.cpp`): a QuickTime atom parser plus Cinepak and QT IMA4 decoders, no dependencies. It's bit-exact against ffmpeg on all eight movies (audio within ±127/32768), and `tools/movtest.cpp` is the checker.
  - Plan: serve a replacement `_EntryPoint` implementing the QuickTime calls the game uses, so the movies play inside the game's own cutscene scenes.
- **Cutscenes fixed (v0.9):** a QuickTime for Windows 2.x stand-in (`src/movie/qt.cpp`).
  - The loader `0x4eb3b0` is patched to install our own QTIM32 and CMGR32 `_EntryPoint` dispatchers, reporting version 2.1.3; the unloader `0x4eb700` returns 0.
  - The selectors the game uses are implemented, as mapped from the disassembly: movie open from the game's file handle, box, duration and time scale, track types, movie controller, MCDoAction, MCIsPlayerEvent, the action filter, and so on. The game's own cutscene scenes (SCut/SIntroCut/SRedOrbCut) drive playback unchanged.
    - Their modal GetMessage loop is kept running by a 5 ms window timer.
    - Video follows the audio clock (waveOut).
    - End of movie is signalled through the game's action filter (mcActionPlay, rate 0), as QuickTime did; re-entrancy is guarded.
  - Space skips, as in the original. The movie is read through a duplicate of the game's handle, because the game holds the file without read sharing.
  - Frames are presented by our renderer, fitted to the screen with their aspect kept.
  - Verified headless: REDORB plays to its end (15.9 s) and the game continues to the title; OPENING plays and Space skips it to the main menu; menus are normal afterwards.
  - Still to test: sound on real hardware, the campaign story movies (TANU/MAGHA/ENDING via SIntroCut), and alt-tab during a movie (the game closes and reopens the movie and seeks).
  - Test runs: the autopilot gets a per-tick callback during movies, because the frame tick is stopped then. It skips each movie after 1 s (mode 5 captures frames, and lets the first movie play to its end).
- **Resolution setting (tester request):** `[Display] Resolution=auto` (default: the desktop resolution) or a standard size (1280x720 … 3840x2160), on the main-menu Settings page.
  - The game runs as if the screen were that size: missions render at that size divided by the zoom, and the interface scale grows with desktop/base height, so it looks like a real screen of that size.
  - The result is scaled to fill the monitor; a different shape gets black bars, e.g. 16:9 on ultrawide.
  - It changes from the main menu only and takes effect from the next mission (zoom is re-initialised). It replaces `BaseWidth`/`BaseHeight`, which are still read if `Resolution` is absent.
  - Verified headless: 1920x1080 on a 1440p desktop renders 1440x810 at the default zoom and fills the screen.
- **In-mission Game Options:** its Resolution slider (a fixed table of 640x480–1280x1024 at `0x579494`; ignored in missions by our SetResolution hook, and it misleadingly showed "640 x 480") is removed.
  - SOption's Init (vtable `0x50850c` slot 5, `0x491750`) is wrapped: the row of the first slider (vtable `0x509134`, with its label and value text) is moved off-screen and the Game Speed row moved up, using the widgets' common Offset method (slot 5, `0x49a760`). The dialog frame is left alone.
  - The autopilot's mode 6 opens Game Options from the Esc menu and captures it.
- **Windowed mode (tester request):** `[Display] DisplayMode=fullscreen|windowed`, the Settings Video tab, and Alt+Enter to toggle.
  - Windowed is a normal titled, movable, resizable window, centred when switching. Its client size is the Resolution setting, or 3/4 of the desktop for Auto. After that the player's placement is kept: the game's own SetWindowPos calls are reduced to z-order/show changes in both modes.
  - The renderer scales to whatever the client size is.
  - The interface scale is now given relative to the base resolution's height (`render::SetHudScale(scale, baseH)`) and applied against the current client height, so the interface keeps its proportions in a window.
- **Settings tabs:** Video (Display Mode, Resolution, Scaling, VSync, FPS Counter), Controls (Smooth Key Scrolling, WASD, Scroll Speed, Zoom Out/In Limit, Alt+F4) and Interface (Menu Art, Interface Size).
  - The tab headers are text buttons on row 0; the open tab keeps the highlight style.
  - Settings start on row 2; Reset is on row 13, Back on row 14.
  - The main menu's two picture hotspots (cmd 1/2, Gene/Device Lab art) are moved out of reach while Settings is open; they used to glow and stayed clickable under the page.
- **Cutscene end fix:** with sound on, the movie clock came from waveOut and stopped where the soundtrack ended, but several soundtracks are shorter than their video (REDORB by under a millisecond, OPENING by 0.23 s, ENDING1 by 1 s). Movies sat on their last frame until Space. The clock now carries on in real time once the sound has played out. (Headless runs are silent and use a wall clock, which is why the test missed it.)
- **Clean menu art from the game data (`tools/mhkimg.py`, `tools/cleanbg.py`):** Mohawk bitmap sets (tBMH) decoded.
  - **tBMH:** u16 count, u16 ?, u16 count, u16 flags. With flags 0x0002 a table of u32 offsets follows; with 0x0102: u32 unpacked size, u32 packed size, u16 1024, then LZ data that unpacks to the offset table and the images, offsets counted from the 8-byte header.
  - **LZ:** LZSS with a 1024-byte ring starting at 958; flag bits LSB first, 1 = literal; a match is a BE u16 with the position in the low 10 bits and length = high 6 bits + 3.
  - **Images:** u16 w, h, bytesPerRow, format; format & 0x10 = RLE8 rows (u16 row length; b & 0x80 = run of (b & 0x7f) + 1, else b + 1 literals).
  - **tPAL:** u16 first, u16 count, then (r, g, b, 0) entries.
  - Frame 0 of MULTI.MHK, GENESCN.MHK and DEVLAB.MHK is the bare 640x480 screen background, with no panels, text, creature or device. Its colours are mapped to the game's on-screen palette, learned from a capture, because the game remaps art to its shared palette.
  - Earlier attempts gave Gemini captures with the UI on them, and it copied panel shapes and other parts of the picture into the sides.
- **Menu art redone (skirmish, Gene Lab, Device Lab):** from the clean backgrounds, with prompts that require every structure to carry straight across the edge and forbid copying or mirroring.
  - Seam pass: the repair image is used across the whole 16:9 window, colour-corrected per row to meet the original exactly at the frame edge (fading over 240 px) and crossfaded into the 21:9 sides over 96 px.
  - `menuwide.py patchcanvas/patch` handles a single leftover band (used once, on the Device Lab).
  - Earlier versions are in `art/menus/<screen>/archive/attempt*`.
- **Hover glow at the frame edge (main menu):** the top-right picture hotspot's lit image (MSMENU.MHK frame 2, 111x214 at x 529, y 0) reaches the 4:3 frame's right edge, so its pink haze stopped dead against the widescreen art.
  - Backgrounds can now have a `-glow` variant (`Menus\<Scene>[-21x9]-glow.png`), which the renderer shows while the game's frame differs clearly from the background along its edge columns (more than 20 of 240 sampled rows off by more than 40 levels).
  - `tools/menuglow.py` makes the variant from a lit capture (`[Debug] Autopilot=3` with `AutopilotTarget=1` hovers the hotspot). It blends the side towards the lit edge colour of each row, plus a fitted tint, on the rows the highlight reaches the edge, fading out over 260 frame px.
  - The cave-mouth hotspot (frame 1) doesn't reach an edge and needs nothing.
- **Full-screen inventory (every 640x480 image in DATA\*.MHK), mapped to scenes via each scene's Init resource id:**

  | Image | Scene | Widescreen treatment |
  |---|---|---|
  | HEADQT | SHeadquarters (main menu) | done |
  | WARBRD | SWarBreedsScreen (title) | done |
  | MSETUP | SMultiSetup (skirmish setup) | done |
  | MULTI | SMultiDialog | identical image to MSETUP: reuses the skirmish art |
  | JOIN | SJoinCreate | identical image to MSETUP: reuses the skirmish art |
  | GENESCN | SGeneLabScene | done |
  | DEVLAB | SDeviceLab | done |
  | ORBFLAT | SOrbFlat (logo, shown when the intro movie can't play) | plain white sides |
  | PROGRESS frames 0 / 10 | SProgress (loading) | done: `SProgress` and `SProgress~2`, chosen by edge match |
  | REWARD frames 0 / 5 | SReward (victory) | done: `SReward` plus `SReward~2` (darkened copy, colour-fitted); in-game colours not verified yet |
  | CREDITS 0–7 | SCredits | concept art: left as is (tester request) |

- **Several backgrounds per scene:** `Menus\<Scene>~2.png` (…`~8`, each with `-21x9` and `-glow` forms). The renderer picks the one whose columns at the frame edges best match the game's frame.
  - Background loading now also runs from each presented frame, using a scene-stack provider from main.cpp, because loading screens present without the frame tick.
  - `[Debug] CaptureScenes=1` screenshots every new screen, including loading screens.
  - `[Debug] Autopilot=7` with `AutopilotTarget=<scene id>` opens that scene from the main menu: 4 SReward, 0x25 SProgress, 0x26 SOrbFlat, 0xa SMultiDialog, 0xb SJoinCreate, 0x12 SCredits.
- **Single-instance check:** the game quits if a WarBreeds window exists (FindWindowA at `0x47449d`, after a CreateMutex check). In `[Debug] Headless` that one call is answered "none", so automated tests can run while the player's copy is open.
- **Gemini through Chrome needs a visible Chrome window:** when it's minimised (document hidden), Gemini's upload flow doesn't attach files.
- **Loading screens are shown dimmed:** the game displays SProgress at about 86% brightness (a 3x4 colour fit from data to screen, error <1 level), steady rather than a fade. The widescreen sides got the same transform, otherwise a line shows at the 4:3 edge. The victory screen may be dimmed similarly; it can't be checked by forcing the scene, because the palette isn't set up then.
- **Every widescreen image is generated as one 21:9 band; the 16:9 version is its centre crop**, so both formats share identical art.
- **Movie Scaling (Settings → Video):**
  - Enhanced runs three GPU passes:
    1. at movie size, soften Cinepak's 4x4/2x2 block edges where the step across them is small, and deband near-flat areas;
    2. Lanczos-2 upscale to the output size, clamped to the 2x2 neighbourhood so there are no halos;
    3. contrast-adaptive sharpening (after AMD FidelityFX CAS, MIT) with a little dither.

    Intermediate textures are float16. If the shaders can't compile, it falls back to Smooth.
  - Smooth and Sharp reuse the frame scaler (bilinear / sharp bilinear).
  - On the OPENING movie, Enhanced visibly removes the block striping in the red sky that Smooth shows.
  - Bugs fixed on the way:
    - the movie options were only applied when changed in Settings, never at startup, so a test saying "all three modes look the same" was really comparing Enhanced three times;
    - the deblock texture kept the first movie's size (the 320x240 red orb), so the 640x220 opening showed only its left half.
- **Zoom anchor:**
  - The mouse wheel zooms on the map point under the cursor; numpad +/- zoom on the screen centre.
  - Verified headless (the zoom anchor now follows the autopilot's virtual mouse). Zooming in to 2x with the cursor at 30%,70% of the screen kept map point 1008,1322 under it exactly.
  - Zooming out near a map edge can't keep the point, because the camera is clamped to the map; on 4 Corners, full zoom-out shows the whole map.
- **Fonts:**
  - The game draws all its text with GDI: `AddFontResourceA` on its own bitmap `.FON` files, `CreateFontIndirectA` by face name, then `TextOutA`/`DrawTextA` into its 8-bit frame. Layout uses `GetTextExtentPointA`/`GetCharABCWidthsA`/`GetTextMetricsA`.
  - Faces:
    - menu: WBLg24, WBMd24/18/14, WBSm12 (Copperplate-like small caps);
    - interface: IntBld10/12, IntReg10/12;
    - Darwin18: a hand-lettered face.
  - A font option can substitute faces at `CreateFontIndirectA`, using a privately loaded OFL TrueType font at a matching cell height. It must be non-antialiased, because the frame is 8-bit palettised; layout adapts through the metric calls.
  - Smooth high-resolution text would need our own text layer (intercepting `TextOutA` on the frame), which is much more work.
  - **Font option done (the tester picked Orbitron):** Settings → Interface → `Font` (`[Display] Font=original|orbitron`). It changes live.
    - Source: `Orbitron[wght].ttf` from github.com/google/fonts (`ofl/orbitron`), SIL OFL 1.1. It ships unmodified in `Fonts\` with `OFL.txt` (kept in `third_party/fonts/orbitron/`). The release must include the licence; OFL forbids selling the font on its own, which doesn't apply here.
    - GDI shows the variable font's named instances as their own faces ("Orbitron Medium", "Orbitron SemiBold"; Bold as weight 700 of "Orbitron"), so no static copies are needed. A modified copy would have to be renamed: "Orbitron" is a Reserved Font Name.
    - Mapping, at each bitmap face's cell height:
      - WBLg24/WBMd24/WBMd18 and Darwin18 → SemiBold;
      - WBMd14/WBSm12/IntBld12/IntBld10/IntReg10 → Medium with 1 px extra letter spacing;
      - IntReg12 → Regular with 1 px.
      Heavier weights lose the dots on i/j at 11–13 px. Without the extra spacing, words run together at 11 px. GDI's synthesised bold smears.
    - `src/fonts.cpp` hooks the exe's `CreateFontIndirectA` (one stand-in per face; the game recreates its fonts every frame on some screens), `SelectObject` (swaps the stand-in in while the option is on, sets `SetTextCharacterExtra`, and hands the game back its own handles), `DeleteObject`, and `GetCharABCWidthsA`. That last one fails for stand-ins, as it always did for the bitmap faces, so the game keeps measuring with text extents, which include the extra spacing.
    - Checked headless: main menu, Settings, the mission panel (unit name, build/plant/upgrade/repair) and the Esc menu all use Orbitron, and the layout adapts.
    - The Settings tab headers and the version label are placed from measured text widths (`fonts::TextWidth`, in the font being shown), so the spacing is even in both fonts and follows a live font change.
- Also fixed: intermittent startup hangs and crashes. The game's 1998 timer/thread code deadlocks on multi-core CPUs. GOG's DDrawCompat pins it to one core; once our renderer replaced DDrawCompat it ran on all cores. Patch now pins it (`[Compat] SingleCPU=1`). **Wiki note:** anyone running WarBreeds without DDrawCompat should set single-CPU affinity.
- **Possible gameplay side effect (to investigate before release):**
  - `0x4427d0` and `0x442900` build a search rectangle from the *screen size* (`0x5508fc`/`0x5508fe`, shifted by a scale) and query the world object `0x579278` (`0x44bf00`) for units inside it.
  - At 1920x1080 that rectangle covers 6.75x the area it did at 640x480.
  - Need to find out which feature uses it (on-screen selection? unit auto-acquire? AI?). If it affects balance, pin it to the original 640x480 size.
  - Update: the shift amount is the Z-zoom flag, so this rectangle is "the world area visible on screen". A bigger screen means more visible area, which is an inherent widescreen trade-off rather than a hidden balance change. Still to confirm which feature uses it.
- **Loader choice:** `WSOCK32.dll` is statically imported by the exe and is not a KnownDLL. A proxy `wsock32.dll` in the game folder is therefore loaded at process start, so the patch needs no edits to `WARBREED.EXE` and doesn't collide with GOG's `ddraw.dll` (DDrawCompat), `WARBREED.DLL` or `_inmm.dll`. Under Wine/Proton this will need `WINEDLLOVERRIDES="wsock32=n,b"`.

### 2026-10-11
- **Small maps: terrain only in the top-left of the frame, and clicks there ignored (fixed; `re/tutorial-terrain.md`).**
  - Seen by the tester in the Base Building tutorial (96x76 tiles, 3072x2432 px) when zoomed out. Units still showed in the black part; skirmish maps were fine.
  - Cause: SGame::SetViewSize `0x47b520` calls SetZoomedOut(0) `0x47d430` (at `0x47b581`) whenever the view is at least as wide or tall as the map. That function halves the view even when the 2x zoom-out is off.
    - A 1998 screen never reached a map's size, so this only ever undid a zoom-out.
    - An HD frame can reach the map's size: at 0.5x on 1920x1080 the view became 1920x1080 inside a 3840x2160 frame.
  - Terrain cells (`0x42deb0`) are drawn only for camera..camera+view, and `World::Click 0x44a860` picks only inside that rectangle. The drag box (`World::BoxSelect`) was not affected in the headless test.
  - Fix (`camera::InstallViewFix`): that call now leaves an active zoom-out as before, then limits the view to the scroll extent so the camera can't go negative. It applies in every zoom mode.
  - Smooth zoom also stops zooming out once the frame covers the map's width or height (extent `Max - Min`).
  - Verified headless (`Autopilot=11`, `AutopilotTarget=1`):
    - zoomed out, black 64 px blocks went from 74% to 0%, and a click on a unit in the formerly black part now selects it (0 before, 1 after);
    - a resolution-zoom frame of 3200x1800 (wider than the map) shows the whole map, black only beyond its edge;
    - the skirmish mission test is unchanged.
  - Wiki note: on small maps with a large render size, the original engine shows only part of the battlefield.
- **Group build (player request; `[Input] GroupBuild=1`, default on; Settings → Controls → Group Build).**
  - Stock rule: a builder's panel buttons (build, plant, upgrade, repair) and B/P/R/U exist only with exactly one own Shaman selected.
    - World::DrawInfoPanel `0x449410` (called at `0x47c1a9`) draws the thing's own panel (AShaman slot +0x14, `0x442f10`) only when the selection count `World+0x84` is 1; with more it shows just "Squad (n)" (string 51).
    - World::PanelClickDown `0x44f620` (called at `0x47dad6`, `0x47eec9`, `0x47d97b`) gives the click to the thing (slot +0x18) only for one selected thing. That presses the button and captures the mouse, so the button-up (slot +0x1c, `0x442230`) and every later placement click go straight to that Shaman.
    - The keys test SelectionKind `0x44a1c0` == 3, then call SetMode `0x442720` on the first selected thing (`0x452b60`).
  - What counts as a group: two or more selected things, all own builders. A mixed selection keeps the stock army panel (its stance page and group title), and a building order means nothing to soldiers. The first selected builder "leads": its panel, lists and modes are used, as if it were selected alone.
  - Hooks (`src/controls.cpp`; addresses in `src/game.h`):
    - the three PanelClickDown calls, the DrawInfoPanel call and the cursor-pick call `0x47c26b` (`0x47c3d0`, only while the lead places a building, so the ghost is the pointer) run with the selection count reading 1. The panel's second page (`World+0x6c`, the I key) keeps the group's stance page;
    - the lead's panel with no mode open is titled "Squad (n)" like the stock group panel (AShaman slot `0x504a40`);
    - B/P/R/U set the lead's mode; quick-build letters use the lead's list;
    - AShaman click-down `0x504a44` and click-up `0x504a48` for the group cases below.
  - **Placing a building** (mode 3): the stock click is repeated (CanPlace `0x4517e0` == 0, release the build button, remove the ghost, click sound, reset the mode), but the Build order (`Cmd+0x5c`) goes to the selected builder **nearest the site**.
    - The others get a Move to just outside the footprint (`0x54e370[type]`, plus 56 px, on their own side).
    - When the building exists (the founder's `+0x18e` holds its id; the build tick `0x4436b0` creates it 0x40 ticks after arriving), each helper that is idle or still walking near the site gets the game's Repair order (`Cmd+0x88`), which finishes construction. A helper the player has given another order is left alone.
    - They can't join earlier: there is nothing to repair yet, and a second Build order on the same spot only fails once the first building stands. Helpers must also stay off the footprint, because creation (`0x446db0`) fails on any object there except the founder.
    - The founder's own failed attempts are expected: it goes idle with `+0x1b4` = 1, steps aside and repeats the Build (`0x443f10`, at most 4 tries). The same happens with one Shaman. The group build waits through it, and is dropped when the founder stops, dies or gets another order (helpers stay where they are), or after 5 minutes.
  - **Invalid spot, cancel, Shift:** a refused spot goes to the stock handler (the mode stays; nobody moves). Back, the panel's cancel or a right-click leave the mode without orders. The stock placement ignores Shift, and so does the group: one click, one building.
  - **Pods:** the nearest builder plants the pod and the rest do nothing. A pod is one quick action with nothing to join, and planting one each would cost each. Fog planting (BuildInFog) works the same way.
  - **Repair** mode sends every selected builder; the panel's **cancel** while the lead works stops them all (`Cmd+0x2c`). **Upgrade** and **Get Genes** act on the lead only.
  - Not covered: Tab-queued planting of several pods (`0x452b80` wants one Shaman) still moves the group instead.
  - The Settings Controls tab is now full (rows 2–12; Reset is row 13).
  - **Headless test `[Debug] Autopilot=12`:**
    - A skirmish starts with one Shaman, so the test creates two more of its type beside it with the engine's own cheat-console placement (`0x4472d0`, as `0x47d6d3` calls it). `AutopilotTarget` = how many; -1 = none, the stock one-Shaman flow for comparison.
    - E selects all three: the panel reads "Squad (3)" with the builder buttons.
    - The panel's plant button and the first pod (clicked where the renderer shows them), then a click on pod soil: the nearest Shaman planted.
    - B, then R (quick-build Refinery). A click with no pod in reach was refused: the mode stayed and nobody moved. Then a click beside the pod: builder 7 founded it, builders 12 and 13 walked to its edge, and when building 25 stood (about 12 s later, after one stock step-aside retry) both got Repair (activity 0x39, work target 25).
    - The panel's cancel stopped all three. Repair through the panel and a click on the building sent all three; the Refinery completed.
    - Progress over 5 s: 214 with one Shaman, about 640 with three.
    - Mode 1 (mission), mode 8 (controls) and mode 2 (Settings) still pass.
- **Capture Mouse** (`[Input] CaptureMouse=0`, Settings > Controls): once the player clicks in the game window, the pointer is confined to its client area (ClipCursor). It is released when the game loses focus, is minimised or the window is being moved/sized, and re-armed by the next click. Confirmed working by the tester in windowed mode.
- **Scrolling settings tabs:** a tab with more settings than the page's 11 rows scrolls. The mouse wheel moves the list a row per notch while the Settings page is open (it doesn't reach the zoom), and small "More above"/"More below" labels at the right are clickable (a page at a time). `[Debug] SettingsRows=N` shows fewer rows. The headless test (`TARGET=2 tools/autotest.sh 2`, with SettingsRows=6) wheels down and up, checks the indicators, and clicks a scrolled row, which cycles the right setting.
- **v0.11.0:** small-map view fix and zoom limit, Group Build, Capture Mouse, scrolling settings tabs.
- **Map editor: smart terrain painter** (`src/editor/terrain_paint.*`, notes in `re/terrain-painter.md`).
  - A rebuild of the 1998 editor's Smart Palette. Painting a terrain sets the four corner types of the cell, the neighbours' touching corners follow, and each cell becomes the tile kind with its corners (the 15^4 table). Saddles spread outwards, canyons get their far wall, cliffs their plateau edges, and cropland its interior. The original's rules come from `MapEditor.exe` 0x43c450–0x441439.
  - The tables (TRID, TRST, TRVA, TRPL) are read from the player's own `DATA\TERRAIN.MHK` at run time, opened read-only and shared. Nothing from the game is shipped.
  - Checked against all 54 shipped maps:
    - each tile's corners give back its own kind for 99.988% of tiles; the 30 others are one cliff piece whose corners another kind shares, so it can only be placed by hand;
    - corners recovered from the neighbours give the right tile for 99.985% (98.92% if open cliff lines count as misses);
    - the designers' variant mix matches the random weights.
  - `tests/terrain_paint/build.sh`: paint scenarios on test grids and on four shipped maps all end without a visible seam. The before/after pictures go to `build/terrain_paint/`.
  - Each brush cell is a transaction. A paint that would leave a visible seam (the original's "Couldn't find tile!") is undone and refused.
- **Map editor, Stage 1 shell** (`src/editor/editor.*`, `mapfile.*`, `raw_paint.*`, the map pages in `src/menu.cpp`; design, hooks and addresses in `re/editor-shell.md`).
  - Main menu > **Map Editor** (under Settings) lists New Map and every `DATA\MAPS\*.WBM` by its header name; the list scrolls. **New Map** asks for the size (32–96 cells per side, presets 32/48/64/96), base terrain and 2–8 players.
  - A map opens through the stock single-player load path (`SGame::SetParam` 0 and 2, `Game_StartCommandModel(0x32)`), with `SGame+0x460` ("update pause", `0x485360`) set first: no turn ever runs, so no AI, movement or tile animation. Fog is off for the local player and the minimap is forced on (`0x5517b0 +0x962c`). Camera, edge scrolling, smooth zoom and minimap clicks work as in a mission; game hotkeys and unit orders are off.
  - The tools panel is the game's own text buttons added to SGame, so it lands in the interface layer, keeps its size at every zoom and gets click mapping for free. Palette, brush 1/2/3/5, Undo/Redo (Ctrl+Z/Ctrl+Y), Save As, Test Map, Exit, status line. Left-drag paints, Shift+drag fills a rectangle, right-click picks.
  - Painting goes through a small interface (`src/editor/painter.h`: Paint, Fill, Pick, palette names; changed cells reported back). The editor uses the smart painter (below); a raw stamper (the lowest tile id of each uniform terrain class) remains as the fallback if the tables can't be read.
  - **Save** runs the game's own map processing (`AITerrainAnalysis::Rebuild 0x4a8030`, then `AIMovementGuide::ConnectStep 0x4a5200` in time slices) and then the game's own saver (`SGame+0x88`/`+0x86` → `SGame::SaveGame 0x480be0`). Shipped map names are refused, so a built-in map is always saved under a new name. Two hooks keep the save clean: the fog flag goes back for the write, and `StoreCameraInPlayer` (call `0x480da9`) is skipped so the editor's view isn't saved as player 0's start.
  - Found on the way: at load the game converts the 1998 editor's player colours (238, 230, …) to the retail numbering (`0x471a80`: +9 unless `(c−0xb7)` is a multiple of 8); triggers are converted the same way. A map re-saved by the game stores 247, 239, …, which loads unchanged.
  - **Test Map** saves, returns to the hub, opens the skirmish setup the way the Skirmish item does, selects the map (`SMultiSetup::SetParam` 0) and presses Start.
  - **New Map** writes a blank `.WBM` from the format notes (one tile everywhere, pathing format 4 with no regions, N player slots, AI state marker 0 so the AI starts fresh, no things), opens it, fills it with the chosen terrain and gives every player a Shaman at its start cell (`PlaceCreature 0x4472d0`). The first Save As writes a complete map through the game's saver.
  - **Headless test `[Debug] Autopilot=13`** (target 0: edit 4 Corners; 1: New Map 48×48; 2: Exit with unsaved changes), checked by `tools/editor_check.py`:
    - 4 Corners: three strokes (Lava, Sand brush 3, Crystal), Ctrl+Z, a pick (Lava), Save As HDTEST. Processing took about 60 ms (318 guide points). The file parses and round-trips in `tools/wbm.py`, and its terrain is the original plus exactly the two kept strokes (24 cells). Test Map started a skirmish on it (176×184 tiles, not paused).
    - New Map: 48×48, 4 players, a Lava stroke, Save As HDNEW: 4 slots (slot 0 human), 4 Shamans, AI state for the 3 computer players, processed pathing. Test Map started a skirmish on it.
    - Exit asked once and then returned to the map list.
    - Mode 1 (mission) and mode 2 (Settings) still pass. Mode 2's Reset/Back clicks and its Interface-tab click were updated: they predated Reset and Back moving half a row down, and the tab position depends on the font.
- **Map editor: smart painter in the editor.**
  - The game opens `DATA\TERRAIN.MHK` without sharing (a shared read while it runs fails with error 32), so `editor::Install` reads the file into memory at DLL load, before the game opens it, and the tables are parsed when the editor first opens.
  - The original's rules refuse a lot (about 80% of random paints in the painter tests): a terrain only goes on or next to terrain it has transition tiles for (Lava with Tar; Sand with Crystal, Tar, Roots or Hot Mud; Cropland with Crushed Crystal, Hardened Tar or Roots). Instead of the original's bare "You cannot paint that there", a refused stroke names the first terrain in or next to the brush that it may not go on ("Lava can't go next to Crushed Crystal"; a border class is named by its offending terrain). The status line wraps onto two rows.
  - The headless test now picks its terrains by dry-running each stroke on a copy of the grid (`editor::StrokeWorks`), so it paints legal strokes on any map with either painter. It also clicks Lava once where it is refused and checks the message. `tools/editor_check.py` has a smart mode (`painter smart` in the save record): the diff matches the file, every changed cell is on or next to a kept stroke, cells inside a stroke hold that terrain's tiles, the undone stroke left nothing, and New Map's base is all base-terrain variants.
  - Results: on 4 Corners, Crushed Crystal and Crystal (brush 3) strokes, then a Sand stroke undone, gave 38 changed cells, all checks pass, and the transitions look like the shipped maps. New Map plus Tar passes. An all-open new map saves with no guide points, which is correct: guide points only sit at obstacle corners. Exit, mode 1 and mode 2 pass.
- **Map editor: panel during smooth zoom, and Test Map asks first.**
  - The tester saw the panel's texts drawn in the middle of the screen while zooming out. The panel buttons were placed in the frame-start tick, but each wheel notch changes the game's resolution before the frame draws. The dark box (drawn at draw time) and the texts then sat in different places, and the interface layer anchored the stray texts as a separate element. Each button is now placed again as it draws (`Place`), from the same frame size as the box. A headless wheel zoom (6 notches) counted 168 such late moves in one zoom-out (28 items over about 6 frames), each one previously a frame of misplaced text. The mid-zoom shots show the panel intact.
  - Test Map leaves the editor and drives the skirmish setup, which surprised the tester after a misclick next to Save As. It now asks first, like Exit: the first click shows "Test Map?" and says what it will do, a second click goes ahead, and any other click or a stroke cancels it. The name prompt says when a skirmish follows the save. Every panel click is logged (`editor: clicked ...`).
- **Interface sized to fit the window.** The tester's 1080 window cut off the bottom of the Gene Lab+'s Select Components (all 15 components). The panel reaches 585 game px down from the top of the screen, so the interface scale is capped at (window height - 8) / 585; the configured size is kept wherever it fits. Elements that fit are also kept inside the window. A scrolling version was tried first and dropped in favour of one size that fits the worst case (the tester's call).
- **Orbitron narrowed to the original's width.** The game measures and centres text with the selected font, so a string wider in Orbitron than in the original face ran out of its box ("Select Components" showed as "ielect Components"). `GetTextExtentPointA`, `TextOutA` and `DrawTextA` (IAT 0x5956ac, 0x595670, 0x5959b8) now measure and draw such a string with a horizontally narrowed Orbitron, so it takes the original's room. Few strings need it: the version label by 1 px, "Skirmish Setup" and "Gene Lab+" by about a quarter.
- **The game's frame is never less than 600 px tall.** The game lays its panels out in its own frame. At full zoom-in a 1080 window gave a 540 px frame, too short for the 585 px Gene Lab+ panel, and the game pushed the panel's header off the top. Zooming in past that now shows the middle part of a frame that is at least 600 px tall (the crop the zoom animation already used, kept at rest). The mouse at the window edge maps to the frame edge, so the game's own edge scrolling still works.
- **Interface elements anchor to their nearest edge.** Elements used to be pinned by the third of the screen their centre fell in. The Gene Lab panel's animated preview changes its box every frame, so at one zoom its centre crossed a third's line and the panel jumped between top and centre. They now pin to the nearer edge, or the centre when about equally far from both.
- **Full redraw.** The game redraws only the 64 px cells something marked as changed, and fast flyers (the Tanu Jubjub) left trails of old copies where nothing did. This also happens with the interface layers off, so it is the original redraw logic. `[Render] FullRedraw=1` (default) marks every cell each mission frame (`AGrid::MarkAllCellsDirty 0x42f210` on `g_Map`). The game still runs at its 64 frames/s at every frame size tested, up to 1974x1110. Autopilot mode 19 flies air units across the screen at each zoom step; mode 16 opens the Gene Lab+ panel (CreateThing with the checks skipped, kept powered, gene levels 5).
- **v0.12.0:** the map editor (terrain, smart painting, New Map, Save As, Test Map), the interface fitting the window, the Gene Lab+ panel fixes, Orbitron fitting, and no more flyer trails.
