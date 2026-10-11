# The in-game map editor, Stage 3: events and Global AI

How WarBreeds HD's map editor edits a map's events (triggers) and its computer players' Global AI settings, with the engine code it uses and what was found on the way. Code: `src/editor/events.*` and `src/editor/ai_settings.*`. Background: `re/editor-original.md` (§2 the manual's event editor, §3 the commands, §5.6, §7 items 15 and 17, §8), `re/wbm-format.md` §3.8 and §3.9, `re/editor-shell.md` (Stage 1).

Confidence marks as in the other notes: **H** = read from the code and confirmed in the running game (headless tests), **M** = from the code only.

---

## 1. Summary

- **The retail game's event editor works.** The scene `SEvent` (static object `0x5790d8`, scene id 0x16, constructor `0x478280`, vtable `0x50653c`) is complete in `WARBREED.EXE` and only unreachable. Pushed over the editor's mission scene with `QueueSceneChange 0x4709a0(0x1f, 0x16, 5)` it draws the 1998 editor's tan panel, shows the map's events from `g_Events`, and edits them with its own boxes, type menus, numeric and text entry, cycling boxes and map points. Esc (its own key handler) pops it with `(0x23, 0, 5)`. Nothing crashed in any test. **H**
- The 1998 editor's toolbar commands are **inlined in `SEvent::KeyDown 0x478850`** in the retail build, keyed by modifiers. WarBreeds HD draws its own toolbar next to the panel and calls that key handler, so the keyboard shortcuts work too. **H**
- Edited events are written by the game's own saver (the "ER" section), round-trip through `tools/wbm.py`, and **fire in a skirmish on the saved map**: an OK DIALOG at turn 61, a CHAT line and a delayed PLAYER END victory at turn 150/190. **H**
- **Global AI settings** are four floats per computer player, saved in the map's AI state. The panel edits them and they are saved, but **a skirmish replaces them with the clan defaults** (`APlayerRegistry::ConfigureAI 0x43c2f0`, called for every computer slot by `ApplySlotToPlayer 0x43c130`). They only last in a game that loads the map without the skirmish setup (single-player load path, as campaign missions do). An AI AGGRESSION event (`AAction_SetAggression`) changes them during play, skirmish included. **H**

## 2. What the player sees

- The editor's tools panel has two buttons, **Events** and **AI** (a temporary entry point, see §7). Each opens a panel over the map; clicking it again closes it, and the other button switches.
- **Events:** the game's tan event panel at the top left: "Event *n* out of *m*" (or "No Events"), "\*PERSISTENT\*", and the two columns "Condn. *i* out of *k*" and "Action *i* out of *k*", each a stack of boxes (the type, then its parameters). Next to it, our toolbar:

  | Row | Buttons |
  |---|---|
  | EVENTS | Close |
  | Event | `<` `>` Add Remove |
  | | Persistent: on/off |
  | Condition | `<` `>` Add Remove |
  | Action | `<` `>` Add Remove |

  and a hint line that follows what is selected. The stock behaviour of the panel:
  - click a box to select it; the type box opens the list of types on the right (conditions 9, actions 28 in alphabetical order), and a click there replaces the condition or action;
  - number boxes take digits and Backspace (each class caps its value), text boxes take typed text (CHAT 79 characters);
  - cycling boxes (victory/defeat, on/off and so on) change with each click;
  - player boxes open a list of the eight colours (247, 239 … 191);
  - point boxes take a click on the map (a grey cross marks a condition's point, a red X an action's).
- **Keys** (the stock ones): Shift = event, Ctrl = condition, Ctrl+Shift = action, each with Left/Right (previous/next), Home/End, A (add), R (remove); `*` toggles persistent; Esc closes. Arrow keys without a modifier do nothing while the panel is open; the camera still edge-scrolls and the minimap still works.
- Add jumps to the new item (the stock code stayed on the current one). A new event is TIMED 100 → EARTHQUAKE, a new condition TIMED 100, a new action ADD CREATURE. The last condition or action of an event can't be removed (stock rule).
- **AI:** the same tan panel shows "GLOBAL AI", the computer player (`<` `>` step through them: "Player 3: Kelika"), and for each of Squad aggression (Low–High), Base (Defensive–Offensive), Scouting (Low–High) and AI influence (More–Less) five steps 1–5; the current one is boxed. A note says that a skirmish resets them.
- Both panels count as edits: the title's `*`, and Exit asks before discarding. Opening and looking changes nothing. Save As, Test Map and Exit on the tools panel work while a panel is open.

## 3. SEvent in the retail game **H**

### 3.1 Layout

`SEvent` is a `Scene` (0x38 bytes) plus:

| Offset | Type | Meaning |
|---|---|---|
| +0x38 | short | current event |
| +0x3a | u8 | current condition |
| +0x3b | u8 | current action |
| +0x3c | u8 | selected column: 0 none, 1 condition, 2 action |
| +0x3d | u8 | selected box in the column (0 = the type) |

`Init 0x478300` clears all of them. It has no widgets and loads nothing: everything it draws is the game's existing art.

### 3.2 Drawing (`SEvent::Draw 0x478330`)

- The panel is frame `0x150` of the picture sheet at `[0x5507e8]` (`0x4675f0(sheet, 0x150, 100, 100, 0xf7, 0, 0, 0)`; the picture's own offset puts it at 0..245 × 0..413 frame pixels).
- Text is `0x4696e0`, `thiscall` on `0x551558` (text, x, baseline y, style): style 5 headings, 2 box texts.
- The columns: conditions at (20, 100), actions at (134, 100); a box is `x−4..x+80`, `y−10..y+14`, one every 32 pixels (`0x4108e0`).
- Type menus: `x 0x218..0x26c` (536–620), rows of 16 from y 16 (`0x4118f0`); conditions in type order (next `0x412270`), actions in the alphabetical order of `0x412280` (0, 3, 5, 4, 0x1b, 0xc, 0x14, 1, 2, 9, 0x16, 0x18, 0x15, 0xb, 7, 0xe, 0x17, 0xd, 0x10, 0x11, 0xf, 0x12, 0x1a, 0x13, 0xa, 8, 6, 0x19).
- Player colour menu: `0x411b30`, 247 down to 191 in rows of 32 from y 16, hit test `0x411a20` (y−10..y+14).
- Drawn after SGame, the panel lands in WarBreeds HD's interface layer like the Esc menu: anchored top left at the interface scale, clicks mapped back by the renderer.
- **Guard (ours):** Draw indexes the current event, condition and action without checks, and an event with no condition or no action would make it read a null element. Before every draw, click and key we clamp the indexes, and skip the stock code (with a note) if an event has an empty list. No shipped map has one; the stock editing keys never leave one.

### 3.3 Input

- `SEvent::LButtonDown 0x478610`: a click on a box selects it (and calls the element's `vf0c(box)`, which cycles a cycling box); with a type box selected, a click on the menu replaces the element (`ConditionFactory 0x4123d0` / `ActionFactory 0x412600`, old one freed); with another box selected, any other click is passed to the element's `vf10(box, world point, frame point)`, which takes a map point (world = camera + frame × 1 or 2) or a menu choice (player colours).
- `SEvent::KeyDown 0x478850(vk, mods, ascii)`; mods bit 0 Ctrl, bit 1 Alt, bit 2 Shift (`re/hotkeys.md` §1.1):

  | Key | mods 4 (Shift): event | 1 (Ctrl): condition | 5 (Ctrl+Shift): action |
  |---|---|---|---|
  | Left / Right | previous / next | | |
  | Home / End | first / last | | |
  | A | add (TIMED 100 → EARTHQUAKE) | add TIMED 100 (max 100) | add ADD CREATURE (max 100) |
  | R | remove (not freed) | remove, if more than one | remove, if more than one |

  - `*` (ascii, any key) toggles the event's persistent flag (`AEvent +0x20`, saved as `AJ`). Esc queues the pop.
  - Any other key goes to the selected element's `vf14(box, vk, ascii)`: numbers through `0x4120d0` (digits append, Backspace divides by 10, capped), text by ascii (`AAction_Message::vf14 0x419360`).
- `Scene::AcceptsMouse` (slot 0x3c) decides which scene of the stack gets a left click; SEvent's is the base (always 1).

### 3.4 The 1998 editor's toolbar mapped to retail

The editor's handlers `0x402ee0..0x402fc0` call `0x47cbb0` (its SEvent, `0x5993b8`) and jump to separate methods, which retail inlined into `KeyDown`:

| Editor handler | Editor method | Does | Retail key |
|---|---|---|---|
| 0x402f70 / 0x402f90 | 0x480610 / 0x4806d0 | previous / next event | Shift+Left / Right |
| 0x402f60 | 0x480380 | add event | Shift+A |
| 0x402f80 | 0x480540 | remove event | Shift+R |
| 0x402fc0 | 0x480710 | toggle persistent | `*` |
| 0x402f30 / 0x402f50 | 0x480590 / 0x480650 | previous / next condition | Ctrl+Left / Right |
| 0x402f20 | 0x480280 | add condition | Ctrl+A |
| 0x402f40 | 0x480460 | remove condition | Ctrl+R |
| 0x402ef0 / 0x402f10 | 0x4805d0 / 0x480690 | previous / next action | Ctrl+Shift+Left / Right |
| 0x402ee0 | 0x480300 | add action | Ctrl+Shift+A |
| 0x402f00 | 0x4804d0 | remove action | Ctrl+Shift+R |

(0x402fa0 is the persistent button's update handler.) The editor's Tools > Event Editor (`0x4028d0`) pushes its scene 3 with `0x47d020(0x1f, 3, 5)` and pops it with `(0x23, 0, 5)`; retail's id is 0x16.

### 3.5 Our hooks (`events.cpp`)

Four slots of SEvent's own vtable (nothing shared with other scenes):

| Slot | Stock | Ours |
|---|---|---|
| 0x04 Draw | 0x478330 | guard (§3.2), stock draw, then our toolbar; or another panel (Global AI) on the same scene |
| 0x1c LButtonDown | 0x478610 | toolbar buttons first, then the stock handler |
| 0x34 KeyDown | 0x478850 | guard; after Add, End with the same modifiers (show the new item); a panel's keys |
| 0x3c AcceptsMouse | 0x495520 | 0 on the editor's tools panel (`editor::PanelContains`) and on the minimap (`0x433e10`/`0x4335b0`), so those clicks reach the mission scene below |

- The toolbar is drawn in the interface layer: a box in the panel's own tan (sampled from the drawn panel) with a dark edge, texts with the game's text function.
- Keys reach the scene through the game's own handler (`editor::OnMessage` passes them on while the scene is open, unless the editor is asking for a file name), which reads the modifiers from the keyboard state. Posted test messages have no modifiers, so the tests use the toolbar.
- **Unsaved changes:** a hash of the registry's content (each element's bytes after its panel position, sized from the factories' allocations: conditions 0x10–0x28, actions 0x10–0x64) taken when the map opens and after each save (`editor::SaveCount`). The AI panel hashes its four floats the same way. `editor.cpp`'s `Dirty()` asks both.
- While the scene is up, `SGame` still ticks (no turns: the update pause) and still draws under it; the camera keys and the zoom are off (`InMission()` wants a lone mission scene), edge scrolling still works (mouse moves also go to `scenes[0]`).

### 3.6 Dead ends and quirks

- First attempt was a plain push from the editor with no hooks: it worked straight away (tan panel, "No Events", Add made TIMED 100 → EARTHQUAKE). Everything else was presentation and input.
- `menu::FindWidget` looks at the top scene only; with SEvent on top the tests find the editor's buttons in `scenes[0]` themselves.
- The text function's y is the **baseline**: glyphs sit about 10 pixels above it. The toolbar's hit boxes are placed accordingly.
- At the smallest zoom (1280×720 frame) the tools panel's top rows (bottom left at that time) sit under the tan panel's last 17 pixels; the scene can't be zoomed while open. The Stage 2 panel moves to the top right.
- The PLAYER boxes show the colour as the player's clan logo sprite, which overflows the box (stock look).
- Remove event doesn't free the event (stock; a few dozen bytes).

## 4. Events in play **H**

- `AEventRegistry::Tick 0x41d7f0` runs once per simulation turn (never in the editor: update pause). An event fires when all its conditions hold; its actions run after their delays; a non-persistent event is removed when every action has run.
- Conditions TIMED compare `GetGameTurn 0x4708b0` with the cycles. In the test skirmish TIMED 60 fired at turn 61 and TIMED 150 with a 40-turn delay was removed at turn 191 (about 2 s and 7 s in).
- Players in actions and conditions are **colours**. In a skirmish the colours come from the setup's slots (the test copy: human 231, AIs 191, 215, 239), not from the map (247, 239, …). An event for "player 247" does nothing when no slot has that colour. A skirmish map's events should therefore use colour-free actions or the colours its players will have.
- OK DIALOG (`AAction_OKDialog 0x41bfb0` → `ShowDialog 0x471950`) pushes the stock message box with three string-table ids (title, first line, last line; retail table, `re/editor-original.md` §1). Turns stop while it is up; Enter or its OK closes it.

## 5. Global AI settings **H**

- The 1998 editor's Tools > Global AI Editor (dialog 132, `CGlobalAIDlg`, handler `0x404190`): for each computer player (`0x439830(i)` and a dynamic cast to `AIComputerPlayer`) it reads four floats (getters `0x45c1e0/f0/200` and `0x45cc20`), shows them ×4 on trackbars 1056 (squad), 1057 (base defence), 1018 (scouting) and 1021 (AI influence), and on OK writes them ×0.25 through `SetAggression` (vtable +0x60: base, squad, explore) and the influence setter `0x45cc30`.
- Retail fields (`AIComputerPlayer`): +0x4d84 squad aggression, +0x4d88 base, +0x4d8c explore (scouting), +0x4d90 influence. +0x4d90 is the weight `0x4b85a0` gives the setting against the AI's own estimate (`(1−w)·own + w·setting`), so "Less" AI influence = 1.0. +0x4d80 is a fifth float, always 0.5.
- Saved in the map's AI state (`0x4b7280`): +0x4d80..+0x4d90 as five floats. `tools/wbm.py` now names them `f4d80, squad, base, scouting, influence` and `--dump` prints them; the 54 shipped maps and the editor's saves still round-trip byte for byte.
- The panel writes the floats directly (levels 0–4 × 0.25), as the original did.
- **Skirmish:** `ApplySlotToPlayer 0x43c130` calls `ConfigureAI 0x43c2f0` for every computer player, which sets base, squad, explore and influence from the clan (influence 0.65 for all). The test skirmish showed exactly those, not the map's. A map's settings therefore matter only on the single-player load path (campaign-style loading, the editor itself). The AI AGGRESSION action (`AAction_SetAggression`, type 16: player colour and base, squad and explore) sets them during play, skirmish included.

## 6. Tests

`tools/autotest.sh 14 <seconds>` (headless; `TESTDIR` for another test copy), then `python tools/events_check.py <game dir>`:

- **TARGET 0** (about 150 s): Map Editor → 4 Corners (no events) → Events. Through the toolbar, box clicks, menu clicks and typed keys:
  - event 1: TIMED 60 → OK DIALOG 72, 73, 74;
  - event 2: TIMED 150 → CHAT from 231 "hd event fired", then Add action → PLAYER END 231, delay 40, victory;
  - add and remove a condition, add an action, previous, next, remove it, persistent on and off, add and remove a third event, previous and next event: the events end as built;
  - AI: first computer player squad 5 and scouting 1, next player base 1; Close; Save As HDEVENT; Test Map.
  - The checker: the file round-trips; its "ER" section is exactly the two events; the AI floats are the edits and everything else is MPMAP1's; terrain and things unchanged; in the log both events fired in the skirmish, the dialog appeared and the game ended (SReward, "Victory!").
  - Screenshots `auto-v*`: v1 no events, v3 the action menu, v5 the colour menu, v8 persistent, v10–v12 the AI panel, v16 the OK DIALOG in the skirmish, v17 the chat line, v19 the victory screen.
- **TARGET 1** (about 70 s): Switchbacks (MPMAP8: TIMED 0 → SET MOBILITY; STEP IN RECT → SET MOBILITY). The events as loaded equal the file's; a point box, the next event and the condition menu are shown; Esc closes; reopening shows the same events, not dirty; the toolbar's Close; Exit leaves without asking.
- Regressions run after the change: mode 13 target 0 (`editor_check.py` all pass), mode 1, mode 2.

## 7. Integration

- `events.h`: `Install`, `Open`, `Close`, `Active`, `Showing`, `Tick`, `OnMessage`, `Do(Command)`, `Dirty`, the `Panel` interface and `OpenPanel` (another modal panel on the same scene), drawing helpers, and test accessors (`Describe`, `CommandCentre`, `BoxCentre`, `MenuEntryCentre`, `ColourEntryCentre`, `Hint`).
- `ai_settings.h`: `Open`, `Close`, `Active`, `Tick`, `Dirty`, and test accessors.
- `editor.cpp` calls: `events::Install` in `Install`; `events::Tick` and `ai::Tick` at the end of `Tick`; `events::OnMessage` first in `OnMessage` while a panel is open (not while naming); `events::Dirty() || ai::Dirty()` in `Dirty()`; new public `editor::PanelContains(x, y)`. **Temporary entry point:** two panel items, Events and AI, on the Undo/Redo row (`kEvents`, `kAI`, marked TEMPORARY), to move into the panel's tabs.

## 8. Open questions

- Most condition and action parameter meanings beyond the ones tested (ADD CREATURE's gizmo slots, EFFECT's layer, COMMAND SCRIPT files) were not exercised in play. **M**
- Should the editor offer to keep a map's Global AI settings in a skirmish (skip `ConfigureAI` for maps that carry them)? That would change the shipped maps' skirmish AI, so it was left alone.
- Skirmish colours: the setup decides them; a way for a skirmish map's events to name "the human" or "slot n" would need new code.
- The panel coordinates are the 1998 editor's 640×480 layout; it stays at the top left at every zoom (interface scale), but isn't scaled up beyond the interface scale.
