# WarBreeds mission mouse input: reverse-engineering notes

Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000), using `re/text.asm` plus capstone for code the linear disassembly mis-decodes after jump tables. Nothing was run.

Confidence: **H** means read directly from code; **M** means inferred from code and naming or behaviour that looks consistent; **L** means a guess, which should be checked in game.

Naming conventions in this file:
- `SGame` is the mission scene (vtable `0x507534`).
- `World` is the object at `[0x579278]`.
- `Cmd` is the command object at `[0x579260]` (`CSinglePlayer` vtable `0x504ff4`, `CMultiPlayer` `0x504f04`). Every gameplay order goes through it.
- `Panel` is the info panel `WInfoBox` at `0x5476e8`.
- Object ids are 16-bit; `0xc01` means "none". `World+0x10` is the object table and `World+0x14` its count.

---

## 1. From the Windows message to the scene

### WndProc `0x4731a0` (registered at `0x474300`)
- The window class style is `0x2008`, which **includes `CS_DBLCLKS`**. The second click of a fast double click therefore arrives as a `*DBLCLK` message instead of `*DOWN`.
- If `0x46ddf0()` (returns `[0x51bea4]`, the input-journal playback mode) is 1, all keyboard and mouse messages go to `DefWindowProc` and live input is ignored during playback.
- Mouse messages are dispatched as follows. `[0x579268]` is set to 1 around each call as a re-entrancy flag that the frame tick checks.

| msg | handler | args |
|---|---|---|
| `WM_LBUTTONDOWN` 0x201 | `0x473e40(lParam, HIWORD(lParam))` | cdecl |
| `WM_LBUTTONUP` 0x202 | `0x473f00` | same |
| `WM_LBUTTONDBLCLK` 0x203 | `0x473fc0` | same |
| `WM_RBUTTONDOWN` 0x204 | `0x474080` | same |
| `WM_RBUTTONUP` 0x205 | `0x474120` | same |
| `WM_MOUSEMOVE` 0x200 | **not handled** (`DefWindowProc`) | |
| `WM_RBUTTONDBLCLK` 0x206, `WM_MBUTTON*` 0x207–0x209, `WM_MOUSEWHEEL` 0x20a | **not handled**. A fast second right click is **lost** (it arrives as 0x206). | |
| `WM_KEYDOWN`/`WM_SYSKEYDOWN` | `0x473b90` | |
| `WM_KEYUP`/`WM_SYSKEYUP` | `0x473d20` | |

The game uses neither the middle button nor the wheel. Our `WM_MOUSEWHEEL` zoom in `src/main.cpp` therefore conflicts with nothing. **H**

### Button dispatch (`0x473e40` and the other four)
1. If `[0x5792dc]` (an input-blocked flag) is non-zero, the event is ignored.
2. **Left button events (down, up, double click):** the dispatcher walks the scene stack `0x55aea8[]` from the top index `[0x51bee8]` downwards. It calls vtable slot `+0x3c` (`AcceptsMouse(x, y)`; the base `0x495520` always returns 1) and stops at the first scene that accepts. In practice that is the top scene.
3. **Right button events** go straight to the top scene.
4. Unless the journal is in playback, an event record is written to the input journal: `0x46d9f0` builds it as `{int type; int [0x57929c]; int timer; ...; short x @+0x10; short y @+0x12}` and `0x46db70` writes it. The type is 0 = L down, 1 = L up, 2 = R down, 3 = R up, 4 = move, 5 = L double click, 6 = key. Replay goes through `0x46da70`.
5. The handler is called as `scene->vtbl[slot](short x, short y)`. It is `thiscall`, takes 2 stack arguments (`ret 8`) and returns `short`. The x and y values are **client coordinates of the 640x480-style game frame**, after our `render::FilterMessage` remap.

### Mouse move (polled, not message-driven)
`FrameTick 0x46f400` runs `GetCursorPos` (hooked by `render.cpp`), then `0x474230(x, y)`:
- it records a type-4 journal event;
- it calls `scene->vtbl+0x2c(x, y)` on the first accepting scene **and** on `scenes[0]`.

The base handler `Scene::MouseMove 0x495360` writes the **game cursor `[0x5794d8]` / `[0x5794dc]`** and updates widget hover. **H**

Consequence: hover, cursor shape and some click paths read `[0x5794d8/dc]`, which is updated once per tick from `GetCursorPos`. Button handlers use the message's lParam.

### Keys and modifiers
`0x4e47b8` builds the Mohawk key event `{u8 vk; u8 mods; u8 ascii; u8 repeat}`:
- `mods` bit0 = Ctrl, bit1 = Alt, bit2 = Shift, read from `GetKeyboardState`;
- for VK 0x10–0x12 (the modifier keys themselves) the event is zeroed;
- `0x473b90` then turns a **Shift** press into `vk = 4`.

Scene slots are `+0x34 KeyDown(vk, mods, ascii)` and `+0x38 KeyUp(...)`.

**Mouse events carry no modifiers.** The only modifier a click sees is the SGame "key held" flags below: Shift is tracked, Ctrl and Alt are not. **H**

### SGame vtable `0x507534`: input slots

| slot | addr | slot VA (to patch) | role |
|---|---|---|---|
| +0x0c | `0x495800` | 0x507540 | scene cursor draw (SGame draws its own at `0x47c3d0`) |
| +0x10 | `0x47ce80` | 0x507544 | tick (contains edge scrolling `0x47cfe9`) |
| +0x1c | **`0x47d500`** | 0x507550 | **LButtonDown** |
| +0x20 | **`0x47e430`** | 0x507554 | **LButtonUp** |
| +0x24 | **`0x47e6a0`** | 0x507558 | **RButtonDown** |
| +0x28 | **`0x47e6f0`** | 0x50755c | **RButtonUp** |
| +0x2c | **`0x47e910`** | 0x507560 | **MouseMove** |
| +0x30 | **`0x47eea0`** | 0x507564 | **LButtonDblClk** |
| +0x34 | `0x47f000` | 0x507568 | KeyDown |
| +0x38 | `0x47fd90` | 0x50756c | KeyUp |
| +0x3c | `0x495520` | | AcceptsMouse (always 1) |
| +0x5c | `0x480520` | | SetMouseCapture(objId) → `[+0x1f0]`; called via `0x470970`, used by `GameObject::Capture 0x445a90` |

### SGame fields used by mouse code (all `short` unless noted)

| off | meaning | set by |
|---|---|---|
| +0x38/+0x3a | camera x/y (world px) | |
| +0x3c (byte) | edge-scroll direction bits: 1 = left, 2 = right, 4 = up, 8 = down | MouseMove |
| +0x3e | edge-scroll speed (0x28 default) | ctor `0x47abb6` |
| +0x42 (byte) | edge-scroll ramp, 1..6 | tick |
| +0x4c/+0x4e | view size in world px | |
| +0x50 | hovered object id (only maintained while A is held) | `0x47deb0` |
| +0x52..+0x58 | drag box: world x0, y0, x1, y1 | LDown / MouseMove |
| +0x5a | **A held** (attack mode) | KeyDown 'A' |
| +0x5c | input disabled (game over / paused) | `0x47d070/80` |
| +0x5e | 2x zoom-out active | |
| +0x62 | **Space held** (no drag box; Space-click order) | |
| +0x64 | **Shift held** | KeyDown vk 4 |
| +0x66 | 'L' toggle: no edge scroll while something is selected | |
| +0x68 | **Tab held** (waypoint mode; needs an own commandable selection) | |
| +0x6a | **Ctrl+Tab** (waypoint mode, looped/patrol variant) **M** | |
| +0x6c | dragging on the minimap (camera follows) | LDown |
| +0x6e | **left click pending** (LDown on the battlefield; resolved at LUp) | |
| +0x70 / +0x72 | **PgDn / PgUp held**: pick/box-select layer filter (two map layers, see §2.4) **M** | |
| +0x74 | **F10 held**: HUD move mode (drag the info panel or minimap) | |
| +0x76 | 'F' held | |
| +0x78 | **'G' held**: G-click on an own object (guard/follow **L**) | |
| +0x7a | unknown; changes the default cursor **L** | `0x47ce1c` |
| +0x7c | **'C' held**: C-click order | |
| +0x7e | **'M' held** (multiplayer only): map flare/ping | |
| +0x198 / +0x19c / +0x1a0 | waypoint list (max 16) built during Tab mode | |
| +0x1ac / +0x1b0 | on-map marker list (objective/nav markers) | |
| +0x1cc (int) | HUD drag: 1 = info panel, 2 = minimap | |
| +0x1d0, +0x1d8, +0x1e0, +0x1e8 | edge-scroll rects (§6) | `0x47b520` |
| +0x1f0 | **mouse-captured object id** (`0xc01` = none). A pending command mode lives here. | `0x480520` |
| +0x43c / +0x454 | cheat-console object placement (`[0x454]` type, `0x4a` = none). Not used in normal play. | `0x483590` |

---

## 2. Left button

### 2.1 LButtonDown `0x47d500(x, y)`
The handler runs the following checks in order and stops at the first that applies. The return value is `short` (1 = handled).

1. `Scene::LDown 0x4951f0` handles the scene's own widgets. Its return value is **ignored**.
2. Input disabled `[+0x5c]` → return 0.
3. Cheat placement `[+0x43c]` → place object `[+0x454]` (`World 0x447410` / `0x4472d0`), then leave the mode.
4. **A held, hovered object visible (`0x47e100`) and hostile (`0x47e040`):** if the selection is commandable (`World 0x44a1e0`) → `World::TargetSelectionAt(id) 0x44ca70`, which is an **attack order issued on button down**. Otherwise the click does nothing.
5. **Same A-mode conditions with a friendly or neutral target:** with no selection → select it at once (`0x44b680(0)`, then `0x44b5d0`). With a selection → nothing on down; the click resolves at LUp.
6. **F10 HUD mode** and the click is on the info panel (`World 0x44f620`) → start dragging the panel (`[+0x1cc]=1`).
7. **M (multiplayer flare)** → `Cmd->vtbl+0x9c` (flare at the world point).
8. **Captured object `[+0x1f0]`**, and not in Tab mode → `obj->vtbl+0x18(x, y, wx, wy, zoom)`. If that returns 1, the click is consumed. **This is how build placement, planting, repair and upgrade targeting receive their click** (§5).
9. Something is selected → `World::PanelClick 0x44f620(x, y)`:
   - the panel's toggle button flips `World+0x6c`;
   - with a single selection, the object's own panel handler `obj->vtbl+0x18(x, y, 0, 0, 0)` runs;
   - otherwise `Panel 0x49a630`.
   - If handled, return 1.
10. **Minimap** (`ARadar 0x5517b0`: visible `0x433e10`, hit `0x4335b0`):
    - in F10 mode, drag the minimap (`[+0x1cc]=2`);
    - otherwise jump the camera to the clicked point and set `[+0x6c]=1`, so the camera keeps following while the button is held.
11. Otherwise, unless Space is held: **start a drag box** (`[+0x6e]=1`, `+0x52..+0x58` = world point).
12. Tab or Ctrl+Tab held → add a waypoint to `+0x198` (max 16). If a builder is in placement state, `World 0x452c20` validates the point first.

### 2.2 LButtonUp `0x47e430(x, y)`
1. `Scene::LUp 0x495290`.
2. Input disabled → 0. Clear minimap drag (`+0x6c`) and HUD drag (`+0x1cc`). Cheat placement → 0.
3. **Captured object** → `obj->vtbl+0x1c(x, y, wx, wy)`; return 1. Panel buttons fire here: the object captures the mouse on button press so that it receives the release.
4. Tab or Ctrl+Tab → 1 (waypoints are issued on Tab release, `KeyUp 0x47fe47` → `World 0x44cc80`).
5. **Space** → `World 0x44a3a0(wx, wy)`, which issues `Cmd+0x40` with a one-point path. This is a move variant, possibly aggressive or attack-move. **L**
6. **G** with own units selected (kind 1 or 2), on an own object → `World 0x44ca90(id)` (`Cmd+0x80` per unit; guard/follow **L**).
7. **C** with own units selected → `World 0x44d260(point at game cursor)` (`Cmd` order via `0x44cf20`; meaning **L**).
8. If a click is pending (`[+0x6e]`) → **`SGame::ResolveClick 0x47e1d0(x, y)`**. Then clear `[+0x6e]`.

### 2.3 `SGame::ResolveClick 0x47e1d0`: the core of the left-click scheme
- **Drag box?** `0x4804c0` is true only when there is no capture, Shift is **not** held, there is no Tab mode, and the box exceeds ±32 world px on either axis. Note that Shift disables the box test.
  - **Box:** normalise the rect, then `World::BoxSelect 0x44afe0(rect, layerFrom, layerTo, shift)`. It selects **own units only** (types 0..0x2b) inside the rect and replaces the selection. If nothing qualifies, fall back to a single click. `0x44afe0` has an "add" (Shift) path, but it is unreachable from the mouse, because Shift makes `0x4804c0` return "not a drag".
- **Single click:**
  - If an on-map marker is within ±20 px (`0x47bf20`) and own units are selected → send them along the marker's path (`0x44cc80` / `0x44d290`).
  - Otherwise → **`World::Click 0x44a860(viewRect*, x, y, zoom, pgdn, pgup, shift)`**.

### 2.4 `World::Click 0x44a860`: how one click becomes "select" or "order"
Object classes (`obj+0x18` = type):

| type range | class |
|---|---|
| 0..0x2b | **units** (creatures; builders = types 0, 9, 0x12, 0x1b = `AShaman`) |
| 0x2c..0x3e | **devices / buildings** |
| 0x3f..0x41 | **pods** (planted seeds) |
| > 0x41 | other (e.g. mushrooms / resources, obstacles); not selectable |

Ownership: `obj+0x20` is the owner player. "Hostile" means `(player+0x18 & table[0x53e0e8 + owner->id]) == 0`.

Selection "kind" (`World 0x44a1c0` → `0x44a220`): 0 = none, 1 = several, 2 = one unit, 3 = one builder, 4 = one building, 5 = one pod. "Commandable" (`0x44a1e0`) means more than one selected, or exactly one that the local player owns.

The function works through these steps:
1. Snapshot the current selection, used later to decide whether to play the "selected" voice (`0x4465a0`).
2. **Fog or unexplored tile under the click, own units selected:** if the effects list has something remembered there (`0x426920`) → **move there directly** (`0x44ccd0`). **M**
3. Pick the object under the point:
   - PgDn → `0x44bf00(..., 0, 0)`;
   - PgUp → `0x44bf00(..., 1, 0)`;
   - otherwise `World::PickAt 0x44c530(rect, x, y, zoom)`.
4. **Object found and T not held** (`World+0x6e`, set by holding T):
   - **Allied object** (own or allied), or own pod →
     - **unit:** if it is already selected and Shift is held with more than one selected → **toggle it off**. Otherwise **select it**, replacing the selection unless Shift is held, in which case it is **added**. Selecting a unit **never orders**.
     - **building:** if own units are selected and the building type is **0x34 or 0x39**, PgUp/PgDn are not held and kind is 1..3 → **order: enter/use the building** (`Cmd+0x48(idList, buildingId)`). Otherwise **select the building**, replacing the selection unless Shift is held.
   - **Hostile or neutral object:**
     - with a selection → **order** (`0x44c6d0`, below);
     - with no selection → **select it** to view its info (classes 0x14..0x16 only).
5. **Nothing picked, or T held:** issue an order with `0x44c6d0` unless PgUp/PgDn is held.

**`World::OrderAt 0x44c6d0(rect*, x, y, zoom, wx, wy)`** issues the order. It does nothing if the selection is empty. It calls `World::CursorAction 0x450d40(rect, x, y, zoom)` and acts on the result:
- **0x29 move:** all selected must be units → `World::MoveSelection 0x44ccd0(&sel, path[1], 0)`, ack sound `0x451cd0(0)` and a click marker (`effects 0x423e20`). For a building (type 0x2d/0x35) this sets the rally point via `Cmd+0x4c`.
- **0x2a target:** `target = 0x44c530(...)`, then `World 0x44caf0(&sel, targetId)` (`Cmd`, units only), ack `0x451cd0(1)` and the marker. Attack, harvest and so on are decided by the units, not by the UI.
- **0x5b:** no order (selection not commandable).

`CursorAction 0x450d40` decides as follows:
- Not commandable → 0x5b.
- A single defence building of type 0x31 or 0x37 → 0x2a over a visible hostile, otherwise 0x5b.
- Units:
  - object under the cursor, visible, **not own** → **0x2a**;
  - **own** pod (0x3f..0x41) → 0x2a;
  - own unit or building → **0x29** (move onto it);
  - with T held → 0x2a only for hostile objects, otherwise 0x29;
  - nothing → 0x29.

The game does no passability test: impassable ground still gives "move" and the pathfinder copes. **H/M**

### 2.5 Left double click `0x47eea0` (it replaces the second LDown)
- Info panel click (`0x44f620`).
- If a single own building is selected and the double click lands on it → `World 0x44b920`: **switch the building on/off** (`Cmd+0x50(id, on)`, sounds 0x4c/0x4d). This applies only when it is complete, not upgrading, `+0x2e == 0`, and the type is not 0x2f, 0x38 or 0x3b.
- If the double click hits one of the **alert icons on the left edge** (`APlayer 0x4374f0`) → **centre the camera on that event** (`0x480340`).

The double click does **not** run the LDown path, so no selection or order happens on the second click. **H**

---

## 3. Right button
- **RButtonDown `0x47e6a0`:**
  - cancels cheat placement (`[+0x43c]=0`, `[+0x454]=0x4a`, clears the status line `0x470a10("")`);
  - **cancels a pending left click or drag box** (`[+0x6e]=0`);
  - returns 1. Nothing else happens on down.
- **RButtonUp `0x47e6f0`:**
  - **On the minimap** → **move order to that map point** (`ARadar 0x433ec0` converts the point, then `World 0x44cc80(path, 0)` with a marker).
  - **Anywhere else** → **deselect all** (`World::DeselectAll 0x44b680(0)`). Deselecting calls each object's `vtbl+0x34(0)`. For a builder that is `AShaman::SetSelected 0x443430`, which runs `0x442c00`, resets its pending command state and releases the mouse capture. **So in Classic, right-click is also the "cancel mode" key.**
- **Right drag:** not handled; there is no right-drag behaviour.
- A fast second right click (`WM_RBUTTONDBLCLK`) is **dropped** by the WndProc. **H**

---

## 4. Modifiers on clicks

| modifier | effect |
|---|---|
| **Shift** (`+0x64`) | click on an own unit adds it, or toggles it off if already selected and more than one is selected; click on a building with Shift adds without clearing; **Shift turns off box select** (`0x4804c0`), so Shift+drag acts as a Shift-click at the release point; Shift with edge scroll doubles the speed |
| **Ctrl / Alt** | **no effect on mouse clicks** (not tracked). Ctrl works only on keys: Ctrl+0..9 assigns a group (`0x44d8a0`), Ctrl+F1..F8 (`0x451da0`), Ctrl+X, Ctrl+E (select on screen, `0x44b330`), Ctrl+I (`0x44b4b0`), Ctrl+Tab = waypoint loop |
| A, G, C, T, Space, Tab, PgUp, PgDn, F10, M | held-key "mouse modes", see the field table and §2 |

---

## 5. Command modes ("press a panel button, then click a target")

There is no global mode variable. **The mode lives in the selected object** and is armed through **mouse capture**:
- `GameObject::Capture 0x445a90(1)` sets `obj+8=1` and `SGame[+0x1f0]=id`;
- SGame then sends every click to `obj->vtbl+0x18` (down), `+0x1c` (up), `+0x20` (move) and `+0x24` (cursor id).

The builder `AShaman` (vtable `0x504a2c`; down `0x4419c0`, up `0x442230`, move `0x442d60`, cursor `0x442db0`) has a state at `+0x1ac` (the "panel page"):

| state | entered by panel button | next click does | valid / invalid cursor id |
|---|---|---|---|
| 0 | (default) | panel page shown | |
| 1 | `0x5449a0` (only if `player+0x4844 > 0`) | click on an effect or gizmo → `Cmd+0x60(shaman, path)` (**unidentified special**) | 0x11 / 0 |
| 2 | `0x5452b0` **Build** | build list page (`0x5401e0[16]`, filtered by tech `0x437b50`) → choosing one enters state 3 | |
| 3 | build list item | **building placement**: the ghost follows the mouse (`+0x194/+0x196`, `0x441170`); click → `0x4517e0` validates → `Cmd+0x5c(shaman, type, x, y)`. On an invalid spot the click falls through to the panel and **the mode stays active**. | arrow plus ghost |
| 4 | `0x544e20` **Plant** | pod list page (`0x545730[3]`, types 0x3f..0x41) → choosing one enters state 5 | |
| 5 | pod item | **plant**: `0x442b10` validates → `Cmd+0x58(shaman, podType, path)` | 0xc/0xe/0x10 (valid), 0xb/0xd/0xf (invalid) |
| 6 | `0x546928` **Repair** | target an own building that passes `0x442780` → `Cmd+0x88(shaman, target)` | 8 / 7 |
| 7 | `0x53f8e0` **Upgrade** | target an own building that passes `0x4428b0` → `Cmd+0x84(shaman, target)` | 0xa / 9 |
| — | `0x546df8` | panel **Back/Cancel** (in state 0 it sends `Cmd+0x2c(shaman)`, **unidentified**) | |

**Cancelling a mode:**
- the panel Back button;
- **right-click** (deselects everything, which resets the state through `SetSelected(0)`);
- selecting something else (same reset).

`0x442c00` is the reset: `state=0`, `+0x18c=0`, release capture. Esc does not cancel a mode; Esc opens the game menu (`KeyUp 0x48012d`). **H/M**

Cheat placement `[+0x43c]` is a separate debug mode, entered only from the chat-command parser `0x483590` ("crystal tree", "spore", "psi"…).

---

## 6. Interface, edge scrolling, hover and cursor

- **Info panel** (`0x5476e8`):
  - clicks are routed by `World 0x44f620` (and by a captured object first);
  - panel buttons act on **release**, because the object captures the mouse on press;
  - F10 plus a drag moves the panel (`0x54be40/44`).
- **Minimap** (`ARadar 0x5517b0`):
  - left click or drag scrolls the camera;
  - **right click issues a move order to that point**;
  - F10 plus a drag moves the minimap (`0x55adc6/8`).
- **Alert icons** on the left edge: a double click centres the camera on the alert.
- **Esc menu (SMenu)** is pushed on top. Left events go to the top accepting scene, right events to the top scene, so SGame receives nothing while the menu is open.
- **Edge scrolling:**
  - The **zones** are computed in `0x47b520` at each resolution change, in game-frame pixels:
    - **top band 35 px**, flag 4;
    - **bottom band 35 px**, flag 8;
    - **left band 30 px**, flag 1;
    - **right band 30 px**, flag 2.
  - `MouseMove` sets the flags in `+0x3c`.
  - The **scroll itself** runs once per tick in `SGame::Tick 0x47cfe9` → `0x47d1e0`:
    - base speed `+0x3e` = 40 world px per tick;
    - **Shift doubles it**;
    - it ramps up over the first 6 ticks (`+0x42`);
    - it is scaled by screen width `SGame+0x28`: 640 → 1x, 800 → 1.5x, 1024/1152 → 2x, 1280 → 2.5x, other widths 1x;
    - it is doubled in 2x zoom-out (`0x47d160`).
  - Scrolling is skipped while dragging on the minimap or the HUD, and with 'L' when something is selected.
  - Arrow keys use the same `0x47d160` with step `+0x40`.
- **Tooltips:** button tooltips come from the scene widget code (`0x499cb0`), not from SGame.

### Cursor shape: the best index of what a click will do
`SGame::UpdateMissionCursor 0x47c3d0` picks an id and `0x47c950(id)` draws the frame from sheet `0x550770`. The rules below are taken in order:

| priority | condition | id | frame(s) |
|---|---|---|---|
| 1 | input disabled | 0 | arrow (frame 0) |
| 2 | Ctrl+Tab | 0x14 | 0x33+anim (waypoint) |
| 3 | over the info panel (`World 0x4497d0`) | 0 | arrow |
| 4 | in an edge zone and able to scroll | edge id = `+0x3c` (1..10) | arrows: 0x13.., 0x1b.., 0x23.., 0x27.., 0x2b.., 0x2f..; Shift draws a second, offset arrow |
| 5 | builder in placement (state 3) over unexplored terrain | — | no cursor |
| 6 | G held | 0x12 | frame 0x58 |
| 7 | M flare (multiplayer) | 0x13 | flare sheet `0x54e4d0` 0x308/0x309+colour |
| 8 | `World 0x452fa0` (one own object, special state) | 4 | 0x37..0x3e animated |
| 9 | over the minimap | 0 | arrow |
| 10 | captured object | `obj->vtbl+0x24(...)` | builder modes, see the §5 table (ids > 0x14 are raw frame numbers) |
| 11 | A held, over a visible hostile | 2 | 0xb..0xe animated, red (attack) |
| 12 | PgUp or PgDn held | 5 | 0xb.. plus a layer indicator |
| 13 | Tab held | 0x14 | waypoint |
| 14 | over an on-map marker with own units | 0x14 | |
| 15 | own unit or builder under the cursor (`World 0x44a690`) | 5 | "select" |
| 16 | `+0x7a` set and own units selected | 1 | |
| 17 | otherwise `World::CursorIdAt 0x44f4b0` | 0, 1 or 2 | 0 = arrow (not commandable), **1 = move** (0xb.. animated), **2 = target/attack** (red). Over a remembered object in fog → 2. |

The last row is the same `0x450d40` decision that a left click (Classic) uses to pick move or target, so the cursor predicts the order exactly. **H** for structure, **M** for the visual descriptions of frames.

---

## 7. Complete action map (Classic)

| action | context / mode | modifiers | result | evidence | conf |
|---|---|---|---|---|---|
| L click | own unit | — | select it, replacing the selection; never orders | 0x44a860 → 0x44ac54 | H |
| L click | own unit | Shift | add it; toggles off if already selected and more than one selected | 0x44adb9 | H |
| L click | own building | — | select it (even with units selected), except type 0x34/0x39 with own units → "enter/use" order `Cmd+0x48` | 0x44ade4..0x44af9d | H |
| L click | own building, unfinished or damaged, builder selected | — | **just selects the building**; no build or repair order | same | H |
| L click | enemy unit or building | nothing selected | select it (info only) | 0x44aca0 | H |
| L click | enemy unit or building | own units selected | **target order** (attack), with marker and ack | 0x44c6d0 → 0x44caf0 | H |
| L click | neutral (mushroom/resource, type > 0x41) | units selected | target order (units decide: harvest etc.) | 0x450ef4 | M |
| L click | own pod | units selected | target order | 0x450f00 | M |
| L click | empty ground, also impassable | own units selected | move order (pathfinder handles passability) | 0x44c6d0 0x29 | H |
| L click | empty ground | nothing or enemy selected | nothing | 0x44c6d0 returns 0 | H |
| L click | fog / unexplored | own units | move there (if something is remembered there it goes directly; otherwise normal) | 0x44a9d7..0x44ab05 | M |
| L click | own building selected | — | ground: rally point for types 0x2d/0x35; defence types 0x31/0x37 target a visible hostile | 0x44ccd0, 0x450d40 | M |
| L drag (>32 px) | battlefield | — | box-select own units only, replacing the selection | 0x47e1d0 → 0x44afe0 | H |
| L drag | — | Shift | Shift disables the drag test, so a Shift-drag acts as a click at the release point | 0x4804c0 | H |
| L down | A held, visible hostile | — | immediate attack order | 0x47d844 | H |
| L click | G / C / Space / Tab / Ctrl+Tab / M held | — | guard(?) / C-order / move variant / add waypoint / loop waypoints / flare | §2.2 | M/L |
| L click | PgUp / PgDn held | — | pick or box only from layer 1+0 / no box; never orders | 0x44a860 | M |
| L click | builder in Build/Plant/Repair/Upgrade mode | — | the mode's action, or stays in the mode if the target is invalid | §5 | H |
| L click / drag | minimap | — | camera jumps there and follows while held | 0x47db66 | H |
| L drag | info panel or minimap | F10 held | move the HUD element | 0x47d985, 0x47db25 | H |
| L click | info panel buttons | — | object or panel action, on release | 0x44f620 | H |
| L dbl-click | own complete building (already selected) | — | switch it on/off | 0x44b920 | H |
| L dbl-click | alert icon at the left edge | — | centre the camera on the alert | 0x47ef3e..0x480340 | M |
| R down | any | — | cancel cheat placement and a pending click or drag box | 0x47e6a0 | H |
| R up | minimap | — | move order to that point | 0x47e765.. | H |
| R up | elsewhere | — | **deselect all**, which also cancels any builder mode | 0x47e8e9 | H |
| R dbl-click | — | — | **lost** (WndProc ignores `WM_RBUTTONDBLCLK`) | 0x4731a0 | H |
| R drag | — | — | nothing | | H |
| Middle button, wheel | — | — | not used by the game (the wheel is ours: zoom) | 0x4731a0 | H |
| hover | anywhere | — | cursor per §6; edge zones scroll | 0x47c3d0 | H |

---

## 8. Recommendation: "Modern" (AoE2-style) mouse scheme

### Option A: remap at message level (not recommended)
Translating messages, for example turning a right-click into a left-click, fails for these reasons:
- **left-click on an own unit selects it**, so a right-click "move onto an own unit" would change the selection;
- with a selection, **a left-click on an enemy always orders**, so there is no message sequence for "left-click selects the enemy" short of a synthetic right-click (deselect) followed by a left-click;
- a left-click on ground with a selection always moves, so "left-click on ground deselects" also needs a synthetic right-click, and we can only tell "ground" from "object" by calling the engine pick anyway;
- the Classic right-click always deselects, so the order would still need extra handling;
- synthetic clicks also go into the input journal, hit the 32 px drag threshold and double-click timing (`CS_DBLCLKS`), and depend on the per-tick cursor position.

### Option B (recommended): replace four SGame vtable slots
This uses the same technique as `menu.cpp` (`PatchSlot`), with no code patches. Each hook calls the original when Modern is off. The hooks are `short __thiscall Hook(void* sgame, short x, short y)`.

```
slots: 0x507554 (LUp 0x47e430), 0x507558 (RDown 0x47e6a0), 0x50755c (RUp 0x47e6f0); leave LDown as is.
WndProc (main.cpp): when Modern, treat WM_RBUTTONDBLCLK as WM_RBUTTONDOWN (and post/handle the UP normally).
```

**Modern LUp** (select only):
1. Pass through to the original `0x47e430` if any of these hold:
   - Modern is off;
   - `[+0x5c]` (input disabled), or cheat placement `[+0x43c]`;
   - capture `[+0x1f0] != 0xc01` (a builder mode, so the left-click places or targets as in AoE2);
   - Tab/Ctrl+Tab `[+0x68]`/`[+0x6a]`, Space `[+0x62]`, G `[+0x78]`, C `[+0x7c]`;
   - no pending click `[+0x6e] == 0` (minimap, panel, A-attack, which already acted on down);
   - drag box: `0x4804c0(sg)` is true, so box select stays unchanged.
2. Otherwise run the start of the original yourself:
   - `Scene::LUp 0x495290(sg, x, y)`;
   - clear `[+0x6c]` and `[+0x1cc]`;
   - clear `[+0x6e]` (do this **before** calling into World).
3. Then:
   - `rect = SGame::ViewRect 0x47dc70(sg, &rect)`;
   - `obj = World::PickAt 0x44c530(world, &rect, x, y, zoom=[+0x5e])`, or `0x44bf00` with layers when PgUp/PgDn are held.
4. Act on the result:
   - **no object** → deselect unless Shift: `World::DeselectAll 0x44b680(world, 0)`;
   - **Shift**, object is an own unit → `World::Click 0x44a860(world, &rect, x, y, zoom, [+0x70], [+0x72], 1)`, the stock add/toggle. Any other object with Shift → ignore.
   - **otherwise** → `0x44b680(world, 0)` then `0x44a860(world, &rect, x, y, zoom, [+0x70], [+0x72], 0)`. With an empty selection `World::Click` **only selects** (own or enemy units and buildings) and never orders. Its order path `0x44c6d0` returns at once on an empty selection, and the fog branch needs a selection. Verified in §2.4.

**Modern RDown:** call the original `0x47e6a0`, which cancels the pending click and cheat placement. Return 1.

**Modern RUp** (context order):
1. Call `0x435a70(sg, x, y)` (the base that the original calls). Return 0 if `[+0x5c]`.
2. **Minimap** (`0x433e10` and `0x4335b0` on `0x5517b0`) → call the original `0x47e6f0`, which moves there.
3. **Capture active** → **cancel the mode but keep the selection.** Look up `obj` for `[+0x1f0]`, then `World::Deselect 0x44b620(world, obj)` followed by `World::Select 0x44b5d0(world, obj)`. `SetSelected(0)` resets the builder state and releases the capture through each class's own virtual. Return 1.
4. **Selection not commandable** (`World 0x44a1e0`) → return 1. AoE2 does nothing here; the Classic behaviour (deselect) is optional.
5. Compute `rect`, `wx = 0x47ec60(sg, x)` and `wy = 0x47ec80(sg, y)`, then `obj = 0x44c530(...)`.
6. Pick the order:
   - **own building type 0x34/0x39, units selected** (kind 1..3) → `0x44a860(world, &rect, x, y, zoom, 0, 0, 0)`, the stock "enter" order;
   - **own building that is unfinished or damaged, selection contains builders** → repair/resume order (§9.4);
   - **otherwise** → `World::OrderAt 0x44c6d0(world, &rect, x, y, zoom, wx, wy)`. This gives move or target exactly as the cursor shows, plus ack and marker. A right-click on an own unit gives "move to it" (close to AoE2's follow).

**Notes and risks:**
- All of these run on the game thread inside the game's own dispatch, so calling engine functions there is safe. Orders still go through `Cmd` (`[0x579260]`), so multiplayer and lockstep stay consistent: we change only which UI action fires.
- The input journal (`0x46db70`) records raw button events. A replay recorded in Modern would replay in whichever scheme is active. It is probably a debug feature; leave it as is.
- `World::Click` plays the "selected" voice when the selection changed. Deselect-then-select makes a re-click on the same unit play it again, which matches AoE2.
- The cursor (§6) already shows the right-click outcome (move or attack) whenever a commandable selection exists. In Modern you may want arrow/select over own units, which the game already does (id 5).
- Optional AoE2 behaviour: **Shift+drag adds a box to the selection.** The stock code never box-selects with Shift held. The Modern LUp hook can do the ±32 px test itself (`+0x52..+0x58`, with `+0x56/+0x58` updated by MouseMove) and call `World::BoxSelect 0x44afe0(world, &rect, [+0x72]!=1, [+0x70]!=1, 1)`.
- Keep Tab, A, G, C, Space, T and PgUp/PgDn on left-click as in the original. AoE2 also uses "press a key, then left-click".
- The calling conventions are `thiscall` on `World` (`ecx = [0x579278]`). `0x44a860` takes 7 stack args (`ret 0x1c`), `0x44c6d0` 6 (`ret 0x18`), `0x44c530` 4, `0x44b680` 1, `0x44b5d0`/`0x44b620` 1. The zoom argument is `SGame[+0x5e]`.

---

## 9. Builders and unfinished buildings (extra question)

### 9.1 What a builder gets when clicked onto an own building
**Nothing.** In Classic, a left-click on any own building other than type 0x34/0x39, with any selection, **selects the building** (`World::Click 0x44a860`, path `0x44ade4` → `0x44ac54`). No order is sent. This is the same whether the building is unfinished, damaged or healthy. Holding T (order-only) gives "move next to it". There is **no click path that issues build or repair**. **H**

This is almost certainly what the user saw: clicking a new builder onto the unfinished building just selected the building.

### 9.2 How construction is stored (fields on `ADevice`, vtable `0x5033ec`; all `short`)

| field | meaning | evidence |
|---|---|---|
| +0x2c | **complete** flag (0 = under construction or upgrading) | `BuildStep 0x40c952` sets it to 1 |
| +0x32 | **construction progress** in work ticks | `0x40c9fc inc` |
| vtbl+0x70 → `0x54c670[type]` | **build time** (also the builder's work timer, `0x443d10`) | `0x446470` |
| +0x24 | **hit points**; they grow in steps during construction (`0x54d378` thresholds, `+0x54de90[type]`) | `0x40c926`, repair step `0x40e340` |
| vtbl+0x50 → `0x54cdb0[type]` | max HP | `0x446010` |
| +0x2a | switched **on** (1) or off; set on placement (`ADevice vtbl+0x88 0x40e1d0`); toggled by a double click | `0x44b920` |
| +0x28 | **unpowered** (1 = no power provider or not enough power) | `0x40e020`, `0x40e130`, `0x40dfe0` |
| +0x34 | power provider id | `0x40e130` |
| +0x3a | upgrade in progress | `0x40cbc0` |
| +0x2e | if > 0, BuildStep does nothing (paused/disabled **L**) | `0x40c8ba` |

**Construction is not separate from progress, and buildings do not build themselves.** `ADevice::BuildStep 0x40c8a0` advances progress by one tick. Its only callers are builder work ticks:
- **build** (activity 0x2e, `0x443aeb`): creates the device after 0x40 ticks via `World 0x447410` and remembers it in `shaman+0x18e`;
- **repair** (0x39, `0x443806`);
- **upgrade** (0x38, `0x443968`);
- the cheat placer.

`BuildStep` returns `+0x2c`. Completion is reached **only** when `progress >= buildTime`, which sets `+0x2c=1`, tops up HP and calls `vtbl+0xa0`. Full HP alone does not complete a building. **H**

When the builder dies, the device stays with `+0x2c = 0` and keeps its progress.

### 9.3 Does Repair finish an unfinished building?
**Yes, if the building is powered and switched on.**
- The repair tick `0x443806` runs while `target+0x2a != 0`. While `+0x2c == 0` it calls **`BuildStep(target)` every tick**, so construction resumes and completes.
- After completion it switches to HP repair (`0x40e340`) and stops at full HP.
- The "Build" command cannot resume: it always places a new building, and its resume pointer `+0x18e` belongs to the original builder. **Repair is the only resume path.**

Conditions that leave the building **stuck**:
1. **Unpowered.** The **target validator for the Repair button `0x442780` rejects buildings with `+0x28 == 1`** (except type 0x2c, presumably the power source). The cursor stays at the static id 7 instead of the animated id 8, and the click does nothing. Even if the order got through, the repair tick and the original build tick both raise a stall counter (`shaman+0x170`) each tick while unpowered and give up after 16 ticks (`0x443792`). So **unpowered buildings cannot be (re)built at all**. That looks like a deliberate game rule: power the site first.
2. **Switched off** (`+0x2a == 0`). Both `Shaman::Repair 0x441660` and the validator require `+0x2a == 1`; double-click the building to switch it back on.
3. A low "energy/resource" check (`0x40f5f0(target) < 20` and `owner->vtbl+0x7c() == 0`) stops the work (`0x443b57`). Meaning **L**.

The validator `0x442780` accepts an own building with `+0x2a==1`, not unpowered (unless type 0x2c), and either **progress < build time** (unfinished) or **HP < max** (damaged). `Shaman::Repair 0x441660`, the executor behind `Cmd+0x88` (`0x460e30` → `0x4621b0`), checks the same and sets activity 0x39 with the target in `+0x18e`. **H**

**To check in game:**
1. Select a builder, press **Repair**, and hover over the unfinished building.
2. An animated cursor means the click will resume construction.
3. A static cursor means the building is unpowered or switched off. Fix the power or double-click the building.

### 9.4 Making "builder onto own unfinished or damaged building" mean Repair
- **Hook point:** the same Modern RUp hook (or, for Classic, a LUp hook placed before `World::Click`). This is UI-only, so it needs no code patch and stays lockstep-safe.
- **Condition:**
  - `obj = World::PickAt 0x44c530(...)`;
  - `obj+0x20 == localPlayer` (`0x46f8e0()`);
  - the type is in 0x2c..0x3e;
  - `obj+0x2a == 1`;
  - `obj+0x28 != 1 || type == 0x2c`;
  - and either `obj+0x32 < obj->vtbl[0x70/4]()` (unfinished) or `obj+0x24 < obj->vtbl[0x50/4]()` (damaged).

  This mirrors `0x442780` exactly.
- **Action:** for every selected object (`World+0x80` array, count `World+0x84`) whose type is 0, 9, 0x12 or 0x1b (builders), call `Cmd->vtbl[0x88/4](Cmd, shaman+0x1c id, target+0x1c id)`, a `thiscall` with 2 shorts. Then call `World::Ack 0x451cd0(world, 0)` and add the marker the same way `0x44c6d0` does, via `effects 0x423e20`, or skip the marker.
  - If the selection mixes builders with other units: send the builders to repair and use `0x44c6d0` (move) for the rest, or keep it simple and only handle builder-only selections.
  - When the condition fails because the building is unpowered or off, fall back to the normal path (selection in Classic, move in Modern) so the player notices.
- **Classic option:** the same check can be added to Classic as "left-click with builders selected on an unfinished building" before `0x44a860`. It changes the original rule (that click used to select the building), so put it behind a setting.
- **Risk:** low. It issues the same command the Repair button sends, and the engine re-validates it in `Shaman::Repair`. It does not lift the power rule; lifting it would mean patching `0x442780`, `0x441660` and the stall checks at `0x4438fe` / `0x443b7c`, and it would change gameplay.
