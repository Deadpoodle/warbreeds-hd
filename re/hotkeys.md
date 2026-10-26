# WarBreeds mission hotkeys (reverse-engineered)

Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000). Nothing was run.
Confidence: **H** = read straight from the code; **M** = the mechanism is clear but the gameplay name is inferred; **L** = a best guess.

---

## 1. Unit groups (Ctrl+N / N): already in the game

**Summary:** the game already has RTS control groups 0–9.
- **Ctrl+N** assigns the selection to group N.
- **N** reselects the group. Dead units are dropped automatically.
- **Alt+N** reselects the group and centres the camera on it.

Missing:
- Double-tap N to centre the camera.
- Shift+N to add the group to the current selection. Shift+N acts the same as N.
- Adding to a group. A unit has one group byte, so assigning a unit to a new group removes it from its old one.

Groups are stored on the creatures. They persist in save games, and each creature draws its group number next to its sprite.

### 1.1 How keys and modifiers reach the handler (H)
- WndProc message table at `0x47399c`: `WM_KEYDOWN` and `WM_SYSKEYDOWN` → `0x473b90`; `WM_KEYUP` and `WM_SYSKEYUP` → `0x473d20`. `WM_CHAR` is not used.
- `0x4e47b8` builds the key event `{u8 vk, u8 mods, u8 ascii, u8 repeat}`.
  - It calls `GetKeyboardState` (IAT `0x595a28`, the only call to it), then `ToAscii` (IAT `0x595a24`).
  - The game does not import `GetKeyState` or `GetAsyncKeyState`.
  - `mods` = **bit0 Ctrl, bit1 Alt, bit2 Shift** (bit 7 of the VK_CONTROL/VK_MENU/VK_SHIFT state bytes). So 1 = Ctrl, 2 = Alt, 3 = Ctrl+Alt (also AltGr), 4 = Shift, 5 = Ctrl+Shift.
  - `ascii` is the `ToAscii` result, forced to 0 while Alt is held.
  - A press of Ctrl or Alt alone becomes vk 0. In the dispatcher, Shift alone (`wParam == 0x10`) is rewritten to **vk 4**.
- `0x473b90` calls `scenes[top]->vtbl[0x34](vk, mods, ascii)`. Key-up calls `vtbl[0x38]` with the same arguments.
  - Special case: Alt+F4 is sent as `(0x73, 2)`.
  - Esc key-downs with a modifier are dropped.
  - The key-up dispatcher `0x473d20` drops a key-up before any scene sees it when: it is Esc with a modifier down in the thread's keyboard state (`GetKeyboardState`, which can still show Alt down after an Alt+Tab), no scene is up (`0x51bee8 < 0`), or a scene change is pending (`0x5792dc`). (`0x46ddf0`, the journal mode, only decides whether the key is recorded.) With a scene over the base (top > 0), Esc goes to the scene under the top one. `QueueSceneChange 0x4709a0` also ignores a change while another is pending, and `SGame::OnKeyUp` opens the Esc menu only while `+0x5c` (input off: pause, a multiplayer wait) is 0. WarBreeds HD logs every Esc press with these (`keys: Esc ...`). That found why the menu sometimes needed several presses: `Scenes_ProcessCommand 0x4717e0` puts every queued scene change off while the sound player (`0x55aeb8`, busy test `0x463fb0` at 0x4717f2) plays, 2 ticks at a time up to 80 times, then stops the sound; meanwhile the change stays pending and further Esc presses are dropped. With a voice line playing the menu came seconds late, or seemed not to come. Since 0.21.0 the Esc menu (0x1f, 7) and leaving it (0x20) skip the wait (`voices.cpp`).
- SGame vtable `0x507534`:
  - slot 0x34 = **`SGame::OnKeyDown` `0x47f000`** (`thiscall`, `ret 0xc`)
  - slot 0x38 = **`SGame::OnKeyUp` `0x47fd90`**
  - Both call the base `Scene::OnKeyDown` `0x495530` / `Scene::OnKeyUp` `0x495610` first.
  - The base class fires scene-widget hotkeys only when `mods` is 0 or 4, via `0x4960e0` → `widget->vtbl[8]` = `0x49a660`, which compares `widget+0x14`.

### 1.2 Digit handling in `SGame::OnKeyDown` (H)
| Keys | Path | Effect |
|---|---|---|
| **Ctrl+0..9** (mods==1) | `0x47f2e2`: needs `World::SelectionIsMine 0x44a1e0`==1 and `World::SelectionKind 0x44a1c0` ∈ {1,2,3}, i.e. creatures | `World::AssignGroup 0x44d8a0(n)` |
| **Alt+0..9** (mods==2) | `0x47f487` | `World::RecallGroup 0x44b6d0(n)`; if it returned 1, `SGame::CentreOnSelection 0x4802d0` |
| **0..9** with mods 0, Shift (4), Ctrl+Alt (3) or Ctrl+Shift (5) | switch case 13, `0x47fb8a` | `World::RecallGroup 0x44b6d0(n)` only |

When `mods==0` and a console macro is bound to the digit's character, the macro runs first (`0x47f23a`; see §3).

- **`World::AssignGroup 0x44d8a0(n)`**
  - Walks the whole thing table (`world+0x10` data, `+0x14` count).
  - Every creature (type 0..0x2b) with `+0x8d == n` gets `+0x8d = 0xff`. There is no owner check, but the byte is UI-only and is not sent over the network.
  - Then every selected creature gets `+0x8d = n`, via `0x404980` (`mov [ecx+0x8d], al`).
- **`World::RecallGroup 0x44b6d0(n)`**
  - First pass: is there any thing with owner `+0x20 == localPlayer` (`0x46f8e0` = `[0x579288]`), creature type, and `+0x8d == n`? If not, it returns 0 and leaves the selection unchanged.
  - Otherwise it calls `World::ClearSelection 0x44b680(0)`, then `World::Select 0x44b5d0(thing)` for each match, and returns 1.
- **Dead units:**
  - When a thing's HP (`+0x24`, short) drops below 1, the code calls `vtbl[0x38]` (die), then `World::RemoveThing 0x4481e0(id, 0)` (e.g. `0x445c70`–`0x445c8c`).
  - RemoveThing deselects the thing (`thing->vtbl[0x34](0)`), removes it from the selection list (`+0x7c`) and the saved-selection list (`+0x2c`), and clears the table slot `world+0x10[id] = 0`.
  - So a recall never finds dead units, and the selection never holds them.
  - Alive check for a stored id: `id < world+0x14 && world+0x10[id] != 0`, and optionally `thing+0x24 >= 1`.
- **Display and persistence:**
  - Creature draw code shows the group badge when `+0x8d != 0xff` (`0x40266e`, Shaman `0x440af4`).
  - The info-panel title shows `"%s %ld (%ld)"` when all selected units share a group (`0x449770` / `0x4495ed`).
  - The byte is serialized at `0x406138` and initialized to 0xff in the constructor at `0x4022fe`.
- **What's missing or quirky:**
  - No double-tap detection.
  - Shift+digit does not add.
  - A unit can't be in two groups.
  - Ctrl+digit with a single enemy or structure selected does nothing.
  - Alt+digit (WM_SYSKEYDOWN) centres the camera, but it's undiscoverable.
  - `World::Deselect 0x44b620` decrements the count even when the thing isn't in the list. It is only reached from ClearSelection, so this is harmless today.

### 1.3 Selection structure (H)
World object = `*(0x579278)`.

| Field | Meaning |
|---|---|
| `+0x10` / `+0x14` | thing table `AThing**` indexed by thing id / table size |
| `+0x2c` | `IList<AThing*>` "previous selection" (filled by `ClearSelection(1)`, restored by `0x44a060`) |
| `+0x7c` | `IList<AThing*>` **selection**: `+0x7c` vtable (slot 0 = Append(&item)), **`+0x80` data `AThing**`**, **`+0x84` count** |
| `+0x10c` | 2-layer thing grid, 32 px cells (layer 0 ground, layer 1 air) |

Selection API (all `thiscall` on the world):
- **`0x44b5d0 Select(AThing*)`**: append if absent, then `thing->vtbl[0x34](1)` (selected flag).
- **`0x44b620 Deselect(AThing*)`**: remove, then `vtbl[0x34](0)`.
- **`0x44b680 ClearSelection(short saveToPrev)`**
- **`0x44a1c0 SelectionKind()`**: 0 none, 1 = more than one thing, 2 = one non-Shaman creature, 3 = one Shaman (types 0/9/0x12/0x1b), 4 = one device (0x2c..0x3e), 5 = one pod (0x3f..0x41).
- **`0x44a1e0 SelectionIsMine()`**: count>1 → 1; count==1 → owner == local player.
- **`0x44b8e0 SelectionCentroid()`**: returns `x | y<<16`, world px, from `+0x14/+0x16`.
- `0x44afe0 SelectInRect(rect*, inclGround, inclAir, addToSelection)`

Thing fields:

| Field | Meaning |
|---|---|
| `+0x14/+0x16` | world position (px) |
| `+0x18` | type (0..0x2b creatures, 0x2c..0x3b devices, 0x3f..0x41 pods) |
| `+0x1c` | id (short) |
| `+0x20` | owner `APlayer*` |
| `+0x24` | HP |
| `+0x8d` | group (u8, 0xff = none) |

Camera functions:
- **`SGame::CentreOnSelection 0x4802d0`**
- **`SGame::CentreOn 0x480340(x, y)`** (`ret 8`; clamps via `0x47d340`)

### 1.4 Implementing double-tap
Hook switch case 13 at `0x47fb8a` (or wrap `0x44b6d0` at its two call sites, `0x47f4a3` and `0x47fb94`). Remember the last digit and a timestamp (GetTickCount). On a second press within about 300 ms, call `0x4802d0` on the SGame `this` (it is in `ebp` at the case). Shift+N add-to-selection would go in the same spot: mods are in `[esp+0x40]` at the case, and the value is 4 for Shift. Call `0x44b5d0` for each matching creature without the clear.

---

## 2. Full key table

Key-down handler `0x47f000`, args `(vk, mods, ascii)`. Order of tests:
1. `0x47f000`: cheat-code typing.
2. `Scene::OnKeyDown`.
3. **Z** (any mods).
4. Paused check: if `SGame+0x5c==1`, only Pause gets through.
5. Console macro (mods 0).
6. Ctrl+Alt+K.
7. **mods==1 (Ctrl) block** (`0x47f2d7`–`0x47f444`): only digits, F1–F8, X, E, I and Tab; every other Ctrl+key does nothing.
8. F1–F8 for mods ≠ 1.
9. **mods==2 (Alt) block**: digits only.
10. Char tests (`] [ \ / + = -`).
11. VK switch `0x47f55b` (table bytes `0x47fd14`, targets `0x47fc7c`).

The switch default `0x47fc6d` returns 1: **SGame swallows every key it gets**, including unhandled ones. Shift+letter and Ctrl+Alt+letter reach the switch and behave like the plain letter.

Key-up handler `0x47fd90`: switch at `0x47fe26` (bytes `0x480200`, targets `0x4801c8`). It mostly clears "held" flags.

Corrections to the earlier notes:
- Switch case 34 is **X** (0x58) → `0x47fbdf`.
- Case 35 is **F9** (0x78) → `0x47fc1d`.
- Case 36 is **F10** (0x79) → `0x47fa72`.

"Kind" in the table means `SelectionKind`: 1 = multiple selected, 3 = single Shaman.

| Key | What it does | Evidence | Conf |
|---|---|---|---|
| **0–9** | Recall control group | §1 | H |
| **Ctrl+0–9** | Assign control group | §1 | H |
| **Alt+0–9** | Recall group + centre camera | §1 | H |
| **F1–F8** | Apply saved custom formation 1–8 to the selection (kind 1 only) | `0x47f447` → `0x451e40(n)` → `0x451f10(world+0x8c+n*16)` | M |
| **Ctrl+F1–F8** | Save the selection's shape (offsets from centroid) as formation 1–8 (kind 1) | `0x47f352` → `0x451da0(n)` stores into `world+0x8c+n*16` | H |
| **Alt+F1–F8** | Apply formation + centre camera. Alt+F4 also arrives here as `(0x73,2)` | `0x47f472` → `0x4802d0` | H |
| **H** | Arrange selection in a horizontal line (kind 1) | case 21 `0x47fa9b` → `0x452890(0)` | H |
| **V** | Vertical line (kind 1) | case 33 `0x47faca` → `0x452890(1)` | H |
| **/** (char) | Diagonal line `/` (kind 1). Numpad / sends the same char | `0x47f51a` → `0x452890(2)` | H |
| **\\** (char) | Diagonal line `\` (kind 1) | `0x47f505` → `0x452890(3)` | H |
| **]** / **[** (char) | Rotate formation 90° one way / the other around the centroid (kind 1) | `0x47f4db`/`0x47f4f0` → `0x452630(0/1)`, issues moves via command model +0x34 | H |
| **+** or **=** (char) | Radar (minimap) zoom in (scale `radar+4`, min 8). **Numpad + sends this too** | `0x47fc63` → `ARadar 0x433ca0` on `0x5517b0` | H |
| **-** (char) | Radar zoom out (max 16). **Numpad - too** | `0x47f543` → `0x433cd0` | H |
| **Z** | Toggle 2x zoom-out (any modifiers; also works while paused) | `0x47f207` → `0x4802a0` | H |
| **Arrows** | Scroll the camera one step (`SGame+0x40` = step) | cases 8–11 → `0x47d160(dir 1/2/4/8, step)` | H |
| **Home** | Centre on selection | case 7 `0x47fa86` → `0x4802d0` | H |
| **L** | Toggle camera lock (follow selection every frame; draws a lock icon, sprites 0x30d–0x311; edge-scroll is off while locked) | case 24 → `SGame+0x66 ^= 1`; used in `0x47b784`, `0x47c1ae`, `0x47c481` | H |
| **N** | Select the next own creature (cycles) + centre | case 26 → `0x44d450(0x14,1)` + `0x4802d0` | M |
| **D** | Select the next own device (structure) + centre | case 17 → `0x44d450(0x15,1)` + `0x4802d0` | M |
| **E** | Select all own creatures on screen (Shift: add) | case 18 → `0x44afe0(viewRect, !PgUp, !PgDn, shift)` | H |
| **Ctrl+E** | On screen, select all creatures of the same type(s) as the selection; with nothing selected, clear | `0x47f3c0` → `0x44b330` / `0x44b680(0)` | H |
| **Ctrl+I** | Invert the selection within the screen | `0x47f408` → `0x44b4b0` | H |
| **I** | Toggle the info panel's alternate page (same as the panel's page button; the stance page for creatures, set via command model +0xa0 → creature `0x49f8c0`) | case 22 → `0x4497f0` (`world+0x6c ^= 1`) + click sound `0x46fa70` | M |
| **S** | Stop: clear orders and go idle (state 0x28) | case 30 → `0x44a330` → model+0x2c → creature vtbl+0xb8 `0x49f600` | M |
| **X** | Scatter: stop, then move to a random nearby offset (non-Shaman creatures) | case 34 → `0x452830` → model+0x30 → `0x49f7a0` → `0x4a0ce0(0xc01)` | M |
| **Ctrl+X** | Hold position / guard here (state 0x35, stores home point `+0x104`) | `0x47f38d` → `0x451eb0` → model+0x78 → vtbl+0xbc `0x49f710` | M |
| **J** | Special action for **K5 Leviathan** (type 0x16) only; sets `+0x72` = 1 or 2, probably its spinning attack | case 23 → `0x451e60` → model+0x74 → vtbl+0xb0 `0x49f640` | L |
| **G** | Shaman (kind 3): **Get Genes** mode (1). Any selection: also *hold G + left-click own unit* = follow/escort it (model+0x80 → `0x49f7e0`; string "Select creature to follow.") | case 20 → `0x452b60(1)`; flag `SGame+0x78` used at `0x47e537` | H / M |
| **B** | Shaman: **Build** (opens the "Select a Device" list) | case 15 → `0x452b60(2)` | H |
| **P** | Shaman: **Plant** (pod list: "Select a Pod Type") | case 28 → `0x452b60(4)` | H |
| **R** | Shaman: **Repair** | case 29 → `0x452b60(6)` | H |
| **U** | Shaman: **Upgrade** | case 32 → `0x452b60(7)` | H |
| **A** (hold) | Attack modifier: A + left-click an enemy = attack it (`0x44ca70`). Also re-picks the thing under the cursor (`0x47deb0`) | case 14 → `SGame+0x5a`; `0x47d7f9`; key-up resets hover `+0x50=0xc01` | M |
| **C** (hold) | C + left-click ground = special group move (model+0x3c via `0x44cf20`; probably a converge/"cluster" move) | case 16 → `SGame+0x7c`; used at `0x47e5d7` | L |
| **Space** (hold) | Space + left-click = move keeping formation (model+0x40 → `0x462aa0`, uses the centroid); also stops box-drag starting | case 4 → `SGame+0x62`; `0x47e503` → `0x44a3a0` | M |
| **Shift** (hold) | Add to selection (click, box, E) | vk 4 → `SGame+0x64` | H |
| **Tab** (hold) | Waypoints: Tab + left-clicks add up to 16 points; on release, the selection moves through them (`0x44cc80`), or patrols if you end on an earlier point (`0x44d290`). Works for own creatures, or a Gene Lab / Gene Lab+ (rally) | case 1 → `0x451c90` gate → `SGame+0x68`; release `0x47fe47` | M |
| **Ctrl+Tab** (hold) | Draw a saved patrol route (list `SGame+0x1a8`); left-click a route later to assign the selection to it (`0x47e1d0` → `0x44d290`) | `0x47f42b` → `SGame+0x6a` | M |
| **Delete** | Delete the saved route under the cursor (`0x47bf20`, `0x42b760` dtor); otherwise post something to the effects list at the cursor (`0x424820` on `[0x579280]`) | case 12 `0x47f9a0` | M / L |
| **PageUp** (hold) | Box/E selection picks **air units only** | case 5 → `SGame+0x72`; `0x47e34f`, layer loop in `0x44afe0` | H |
| **PageDown** (hold) | Box/E selection picks **ground units only** | case 6 → `SGame+0x70` | H |
| **F** (hold) | Blink sprite colour (sets the global `0x550858` to 0/0x28 every 4 ticks; probably a "find my units" flash) | case 19 → `SGame+0x76`; `0x47d01f`; key-up `0x467880(0)` | M |
| **T** (hold) | Sets `world+0x6e`, the flag passed to the under-cursor pick `0x450870`/`0x4502d0` (changes which categories the cursor can pick) | case 31 → `0x452c60(1)`, key-up `(0)` | L |
| **O** | Toggle `world+0x08` display mode (`0x449300`; rebuilds local visibility via `0x449350`/`0x44edd0` when set to 1; likely a shroud/visibility view) | case 27 → `0x480280` | L |
| **M** (hold, multiplayer only) | M + left-click = place an indicator/ping for other players (model+0x9c; string 615 "This player has placed an indicator") | case 25 → `SGame+0x7e`; `0x47d9c9` | M |
| **Enter** | Multiplayer: open chat entry (`SGame+0x400` widget); Enter again sends it (`0x47f0a7`). With the cheat console open, runs the command | case 2 `0x47f6c1` | M |
| **Pause** | Toggle pause (model+0x90); the only key accepted while paused | case 3 `0x47f5a5` | H |
| **Esc** (key-up) | Open the in-game menu (`0x4709a0(0x1f,7,1)` or `(0x20,0,5)`) | key-up `0x48012d` | H |
| **Q** (key-up, MP) | When the multiplayer net-wait state (`CMultiPlayer+0x308`) is active, `0x45ca10` (resume/drop) | key-up `0x47fdc2` | L |
| **F9** | Cheat-only (after typing `SPAMSPAMSPAMHUMBUG`, which sets `SGame+0x408`): open the debug console `SGame+0x41c` | case 35 `0x47fc1d` | H |
| **F10** (hold) | Debug drag: with F10 held, clicks on the info panel or minimap drag that window (`SGame+0x1cc/0x1f2`) | case 36 → `SGame+0x74`; `0x47d96d`, `0x47db1f` | M |
| **Ctrl+Alt+K** | Destroy all selected own things (damage = their HP) | `0x47f290` → `0x44d960` → model+0x7c → `0x446580` | H |

Cheat code: typing `SPAMSPAMSPAMHUMBUG` (string `0x51c690`, checked at `0x47f015`) prints the message at `0x51c64c` and enables F9. Console commands include `bind <char> <cmd>` (`0x482a10`). This fills the per-character macro table at `SGame+0x464 + ch*256`, which runs on an unmodified keypress (`0x47f23a`). Normally the table is empty.

Mouse handlers, for reference:

| VA | Handler |
|---|---|
| `0x47d500` | LButtonDown |
| `0x47e430` | LButtonUp |
| `0x47e6a0` | slot 0x24 |
| `0x47e6f0` | RButton |
| `0x47e1d0` | box-select end |

The order-giving right-click ignores all the held key flags.

---

## 3. Free keys (in a mission)

These go through the switch default or are never compared:
- **Letters:** **K** (only Ctrl+Alt+K is used), **W**, **Y**, and **Q** (only a multiplayer key-up in a rare net-wait state). **Z** is used.
- **Shaman-only letters:** B, P, R, U do nothing unless exactly one own Shaman is selected. G is also the follow modifier.
- **Ctrl+letter:** everything except **Ctrl+E, Ctrl+I, Ctrl+X** (and Ctrl+Tab, Ctrl+digits, Ctrl+F1–F8). Ctrl+arrows also do nothing.
- **Alt+letter:** all free (Alt only uses digits and F1–F8). These are WM_SYSKEYDOWN, which the game routes the same way.
- **Shift+letter** acts as the plain letter, so it isn't really free unless you intercept it.
- **F11, F12** (the HD patch uses F10–F12 for debug; note the game binds F9 and F10).
- **Numpad:** numpad 0–9 (VK 0x60–0x69; not the top-row digits), numpad `*`, numpad `.`/Del.
  - Numpad `+`/`-` zoom the radar.
  - Numpad `/` is the `/` diagonal formation.
- **Other keys:** Insert, End, Backspace, `` ` ``, `;`, `'`, `,`, `.`, Caps Lock.

---

## 4. Info-panel command buttons: no hotkeys

- The Shaman's panel buttons are static globals added to the info box `0x5476e8` by `WInfoBox::AddButton 0x4979f0(btn, x, y)` (in `0x443154`–`0x4431bd`). They are not SGame scene widgets, so `Scene::OnKeyDown`'s hotkey search (`0x4960e0`, over scene `+0x0c/+0x10`) never sees them.
- Their hotkey byte (`+0x14`) is only set by `0x49a7f0`, which is called only from the Scene `Add*` helpers (`0x4958a1`, `0x495bb4`, `0x495c7c`, `0x495d01`, `0x495eed`). SGame calls none of these.
- Button setup is at `0x4446e0`. Images come from `0x49a800`; labels come from the string table `[0x55dd54]+4*(id-1)`, i.e. the .rsrc STRINGTABLE.
- Visible flag: word `btn+0x16`.

| Button global | Label (string id) | Shaman mode set | Existing key |
|---|---|---|---|
| `0x5449a0` | "Get Genes" (44); shown only if the player's `+0x4844 > 0` | 1 | G |
| `0x5452b0` | "build" (17) | 2 | B |
| `0x544e20` | "plant" (45) | 4 | P |
| `0x546928` | "repair" (47) | 6 | R |
| `0x53f8e0` | "upgrade" (46) | 7 | U |
| `0x546df8` | "cancel" (625) | → `0x442c00` / model+0x2c | none |

Shaman panel click handler: `AShaman` vtbl+0x18 → `0x442230` (switch on mode at `0x4426f8`). Mode setter: `AShaman::SetMode 0x442720(mode)`, which writes `shaman+0x1ac` only when it is 0.

**Build list (mode 2, `0x442517`):**
- 16 buttons at `0x5401e0 + i*0x47c`; visible word at `+0x16` (`0x5401f6 + i*0x47c`).
- Clicking button *i*:
  - sets mode 3 ("Select a Location")
  - sets `shaman+0x188 = 0x2c + i`
  - stores the click position in `+0x194/+0x196`
  - calls `0x442c90` to start placement
- Two toggles (`0x5464a8`, `0x53fd60`) set `shaman+0x18c`.

**Device types (from the name switch at `0x410df0`):**

| i | Device | i | Device |
|---|---|---|---|
| 0 | Refinery | 8 | Healer |
| 1 | Gene Lab | 9 | Gene Lab+ |
| 2 | Relay | 10 | Relay+ |
| 3 | Psi Tower | 11 | Turret+ |
| 4 | Mine | 12 | Psi Tower+ |
| 5 | Turret | 13 | Warp |
| 6 | Electric | 14 | Summoner |
| 7 | Listener | 15 | Collector |

Which of these appear depends on the visible flags.

**Plant list (mode 4, `0x44241c`):** 3 buttons at `0x545730 + i*0x47c`. Clicking sets mode 5 and `+0x188 = 0x3f + i` (pod type). `0x452b80` returns that pod type while placing.

Quick-build therefore needs new code. With exactly one own Shaman selected (kind 3 and SelectionIsMine), either:
- **Option 1:** set the mode and device, then call `0x442c90` the way the button path does. Check that button *i* is visible first.
- **Option 2:** simulate a click on the build button and then the list button.

Good key candidates: Alt+letter, Ctrl+letter (except E/I/X), or digits/letters only while the Shaman is in mode 2. Digits are group recall, so intercept them only in that mode.

---

## 5. Named functions

| VA | Name / purpose |
|---|---|
| `0x473b90` / `0x473d20` | Key-down / key-up dispatchers (WndProc) |
| `0x4e47b8` | BuildKeyEvent (GetKeyboardState + ToAscii) |
| `0x47f000` / `0x47fd90` | SGame::OnKeyDown / OnKeyUp |
| `0x495530` / `0x495610` | Scene::OnKeyDown / OnKeyUp |
| `0x4960e0` | Scene::FindWidgetByHotkey |
| `0x49a660` | Widget::MatchesHotkey (`+0x14`) |
| `0x49a7f0` | Widget::SetHotkey |
| `0x4979f0` | WInfoBox::AddButton |
| `0x44a1c0` | World::SelectionKind |
| `0x44a1e0` | World::SelectionIsMine |
| `0x44b5d0` / `0x44b620` / `0x44b680` | World::Select / Deselect / ClearSelection |
| `0x44a060` | World::RestorePrevSelection |
| `0x44b8e0` | World::SelectionCentroid |
| `0x44afe0` | World::SelectInRect |
| `0x44b330` | World::SelectSameTypeInRect |
| `0x44b4b0` | World::InvertSelectionInRect |
| `0x44d8a0` / `0x44b6d0` | World::AssignGroup / RecallGroup |
| `0x404980` | ACreature::SetGroup |
| `0x4481e0` | World::RemoveThing(id, flag) |
| `0x44d450` | World::SelectNextOfCategory |
| `0x451da0` / `0x451e40` / `0x451f10` | Formation save / apply / apply-impl |
| `0x452630` | Formation rotate 90° |
| `0x452890` | Formation line (0 H, 1 V, 2 /, 3 \\) |
| `0x452b60` | World::SetShamanMode → `0x442720` |
| `0x452c60` | World set `+0x6e` |
| `0x44a330` | World::StopSelection |
| `0x452830` | World::ScatterSelection |
| `0x451eb0` | World::HoldSelection |
| `0x451e60` | World::LeviathanSpecial |
| `0x44d960` | World::KillSelection |
| `0x44ca70` / `0x44ca90` | Attack target / follow target |
| `0x44a3a0` | Formation move to point |
| `0x44cc80` / `0x44d290` | Waypoint move / patrol route |
| `0x4802d0` | SGame::CentreOnSelection |
| `0x480340` | SGame::CentreOn(x, y) |
| `0x4802a0` | SGame::ToggleZoom |
| `0x480280` | SGame::ToggleWorldMode8 (O) |
| `0x47d160` | SGame::ScrollStep |
| `0x47deb0` | SGame::UpdateHoverThing (`+0x50`) |
| `0x47bf20` | SGame::RouteUnderCursor |
| `0x433ca0` / `0x433cd0` | ARadar::ZoomIn / ZoomOut |
| `0x46f8e0` | GetLocalPlayer (`[0x579288]`) |
| `0x46fa70` | PlayClickSound |
| `0x442720` | AShaman::SetMode |
| `0x442230` | AShaman::OnPanelClick |
| `0x442c00` | AShaman::CancelMode |
| `0x4446e0` | Shaman panel button setup |
| `0x410df0` | ThingTypeName(type) |
| `0x504ff4` | CSinglePlayer vtable (command model `[0x579260]`; `+0x18` returns 0x32 for single player, 0x33 for multiplayer) |
| `0x509314` / `0x504a2c` | ACreature / AShaman vtables |
