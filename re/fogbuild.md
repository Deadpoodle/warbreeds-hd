# Building and planting on fogged ground: reverse-engineering notes

Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000) using `re/text.asm`, plus capstone for jump tables. Nothing was run.

Confidence: **H** means read directly from code. **M** means inferred from code plus naming or consistent behaviour. **L** means a guess.

Naming follows `re/mouse.md`:
- `World` is `[0x579278]`.
- `Cmd` is `[0x579260]` (SP vtable `0x504ff4`, MP vtable `0x504f04`).
- `Panel` is `0x5476e8`.
- The local player is `[0x579288]`, returned by `0x46f8e0()`.
- "Builder" means an `AShaman` (vtable `0x504a2c`). Types 0, 9, 0x12 and 0x1b all use this one class: there is a single ctor at `0x440845`.

---

## 0. Headline: what actually blocks in fog

| mode | click handler checks visibility? | what the player sees over fog | what really refuses the click |
|---|---|---|---|
| **Plant** (state 5) | **Yes.** `0x442b10` requires the 64 px cell to be *currently visible* | invalid pod cursor (0xb/0xd/0xf) | the visibility test inside `0x442b10` |
| **Build** (state 3) | **No.** Neither `0x4517e0` nor the handler nor the executor reads the fog maps | **no ghost** (fog-culled) and a **plain arrow** cursor | only the omniscient validator: collision with any object (seen or not), footprint terrain, power range (or "pod nearby" for the Refinery) |

So on code alone (**H**), a build click on fogged or unexplored ground **is accepted** whenever `0x4517e0` returns 0. The Shaman then walks into the fog and builds. In fog the player gets no feedback at all: the ghost is hidden, the cursor is an arrow, and nothing shows why a click failed. The most likely reason for the observed "refusal" is **code 2: no Relay/Refinery power range** (or, for a Refinery, no pod within ~4 tiles). Unexplored ground is usually outside your power network, and walking there does not change that result.

Worth confirming in game before building the build half:
1. Pick a spot that is inside a Relay's power range but is black or fogged (for example at the edge of the explored area).
2. Select a Shaman, press Build → Gene Lab, and click the spot.
3. By the code, the Shaman walks there and builds.

The mouse.md §6 cursor table, row 5, is inverted. The actual rule:
- `0x47c566..0x47c641`: when `0x452bc0` (one own builder in state 3) holds and the cursor's world cell is **visible**, and the cursor is neither edge-scrolling nor over the panel, **no cursor is drawn**, because the ghost is the pointer.
- Over a **non-visible** cell the normal rules continue down to the captured object's cursor `0x442db0`. For state 3 that returns id 0 (the arrow).

---

## 1. Validators

### 1.1 `World::CanPlace 0x4517e0`
- **Signature (H):** `int __thiscall CanPlace(World*, short playerIdx, short x, short y, int type, short ignoreUnits, short skipSiteCheck, int a6)`. It has 7 stack arguments (`ret 0x1c`).
- **Coordinates:** `x` and `y` are the **centre** of the building in world pixels. The callees subtract half of the footprint from `0x54e370[type]`.
- **How the UI calls it:** both the ghost and the click call it as `(owner+4, sh+0x194, sh+0x196, sh+0x188, 1, 0, 0)`.

The checks run in this order:

| step | call | result if it hits |
|---|---|---|
| 0 | if `ignoreUnits==1`: `player = [0x57927c]->0x43ad10(playerIdx)` (players by index) | – |
| 1 | `0x44e270(type, 0xc02, x, y, ignoreUnits, player, a6)`: **object collision**. It walks the object grid `World+0x10c` (32 px tiles, 192 per row) over the footprint (`0x528264/0x528268`, and `0x527d94/0x527d98` for type 0x34 or when `a6`). With `ignoreUnits=1`, mobile objects (`vtbl+0x60()==1`) are skipped, and so are own or allied units that can step aside (`vtbl+0x68`). **It sees every object, including ones hidden in fog.** | **1** |
| 2 | `0x44e5c0(type, x, y)`: **footprint terrain**, using `0x55dd60::0x42ecf0` on each footprint cell (tile passability masks) | **1** |
| 3 | `skipSiteCheck==1` | **0** (valid) |
| 4a | type `0x2c` (Refinery): `0x4518c0(x>>5, y>>5)`. This scans tiles from x−3..x+4 to y−3..y+4 (clipped by `0x55dd60::0x42f170`) for a **pod** (`0x44e9d0` returns a non-zero value only when the object in the cell has type 0x3f..0x41) | found → **0**, else **2** |
| 4b | other types: `0x44ed20(playerIdx, x, y, need=0x54cfb8[type])`, a **power** check. `0x44eac0` finds an own type 0x2c/0x2e/0x36 device whose radius covers the point. The radius is `0x54de68[type]` if the device is complete or upgrading, else 1. The check takes its provider `+0x34` and requires `prov+0x42 − prov+0x44 + prov+0x40 >= need` | ok → **0**, else **2** |

Return codes (**H**):
- **0** = valid.
- **1** = blocked: an object is in the way, or the footprint terrain is impassable.
- **2** = site requirement not met: no power in range or not enough capacity, or for a Refinery no pod nearby.

**There is no "fogged" code.** The ghost draws these codes as colours (`0x4208a0`, `+0x28`): 0 = valid palette, 1 = blocked, 2 = blinking "needs power/pod".

`World::CreateAt 0x446db0` (reached through `0x447410` by the build work tick, and through `0x448100` for pods) runs the same checks again when the object is actually created: `0x44e270`, `0x44db90`, `0x42ea40`, `0x4518c0`, `0x44ed20`, `0x44e5c0`, `0x42eb50`. It also has no fog test. **H**

### 1.2 `AShaman::CanPlantAt 0x442b10(short wx, short wy)`
- **Signature (H):** thiscall, 2 arguments (`ret 8`), returns a `short` (1 = ok).
- **Checks, in order:**
  1. `0x55dd60::0x42ea40(&Pt{vtbl 0x5030ec, wx>>5, wy>>5}) == 2`: terrain type 2, which is pod soil. **M** for "soil".
  2. `World::0x44db90(0xc01, wx, wy, 16, 16, 0) == 0`: nothing in a 16×16 box.
  3. If `sh+0x20` (the owner) is non-null: **the owner's cell must be visible**, using exactly the inline test from §2.
- **Answer to the question:** yes, `player+0x484c` and the arrays at `+0x26` and `+0x2426` are the fog state. **H**

---

## 2. Fog state per player (`APlayer`, H)

| field | type | meaning |
|---|---|---|
| `+0x484c` | short | **fog of war enabled** for this player. The ctor sets it to 1 (`0x435c19`). `0x43ce6f` can clear it per player (reveal-map setup or cheat). When it is 0, everything counts as visible. |
| `+0x26 [96*96]` | u8 | **visibility count**: how many of the player's sight sources currently see the cell. Init 0. `APlayer::SetSeen 0x4360c0(row, col, add)` does inc/dec. On 0→1 it calls `World::0x44c570` (reveal objects); on 1→0 it calls `Effects::0x424170` (remember objects as fog ghosts). |
| `+0x2426 [96*96]` | u8 | **unexplored / black shroud**. Init `0x01` (memset at `0x435e95`). `SetSeen` sets it to 0 the first time the cell is seen, and nothing sets it back. |

- **Cell size:** 64×64 world px.
- **Index:** `((u8)(wy>>5) >> 1) * 96 + ((u8)(wx>>5) >> 1)`.
- **Map size:** `[0x566db4]` and `[0x566db5]` (bytes, in 32 px tiles; that is `0x55dd60+0x9054/0x9055`).

The game distinguishes three states:
1. **unexplored** (`+0x2426 == 1`);
2. **explored but fogged** (`+0x2426 == 0`, `+0x26 == 0`);
3. **visible** (`+0x26 > 0`).

Every gameplay test uses the same predicate:

```
visible(p, cell) = p+0x484c == 0 || (p+0x2426[cell] != 1 && p+0x26[cell] != 0)
```

Because a visible cell is always explored, this reduces to "**currently visible**". **Both unexplored and explored-but-fogged cells block pod planting**, and both hide the build ghost.

There is an out-of-line version, **`APlayer::IsTileVisible 0x453cc0`**:
- `short __thiscall(APlayer*, int tileX, int tileY)`, where tile = world>>5, `ret 8`;
- it is used at `0x450453`, `0x450df9` and `0x4b3eb0`.

A safe C++ equivalent:

```cpp
bool VisibleLocal(int wx, int wy) {                 // world px
    char* p = *(char**)0x579288;                    // local APlayer
    if (!p) return true;
    if (*(short*)(p + 0x484c) == 0) return true;
    int tx = wx >> 5, ty = wy >> 5;
    if (wx < 0 || wy < 0 || tx >= *(uint8_t*)0x566db4 || ty >= *(uint8_t*)0x566db5) return false;
    int cell = ((uint8_t)ty >> 1) * 96 + ((uint8_t)tx >> 1);
    return p[0x2426 + cell] != 1 && p[0x26 + cell] != 0;
    // or: ((short(__thiscall*)(void*,int,int))0x453cc0)(p, tx, ty) == 1
}
bool ExploredLocal(...)  // same but only p[0x2426+cell] != 1 (not used by the engine for placement)
```

---

## 3. Where the UI rejects or hides fogged placement

The SGame `LButtonDown 0x47d500` forwards to the captured builder at `0x47da3a..0x47da9c` **before** it checks the panel or the minimap, and it has no fog test. The call is `obj->vtbl+0x18(sx, sy, wx, wy, zoom)`, where:
- `wx = 0x47ec60(sg, sx)` and `wy = 0x47ec80(sg, sy)` (camera plus offset, ×2 in zoom-out);
- if the call returns 1, the click is consumed.

### AShaman down handler `0x4419c0`
`short __thiscall(sh, sx, sy, wx, wy, zoom)`, `ret 0x14`. The jump table at `0x442210` maps states 0..7 to `0x441a52, 0x441f75, 0x441c0b, 0x441d2e, 0x441b53, 0x441dbf, 0x44214f, 0x44219d`.

- **State 3 (`0x441d2e`):**
  1. If the click is on the panel Back button (`0x546df8`, through `0x497c60(&Panel, sx, sy)`), go to Back.
  2. Otherwise `r = 0x4517e0(owner+4, sh+0x194, sh+0x196, sh+0x188, 1, 0, 0)`. **It uses the ghost position, not wx/wy.**
  3. `r != 0` → fall through to `0x445a70` (base handler). The mode stays active.
  4. `r == 0` → run these steps in order:
     - `0x49a6f0(&BuildBtn 0x5452b0)`, to release the button;
     - `0x442d20(sh)`, which removes the ghost: `World::0x4492b0(0,1)`, `Effects::0x424290(ghost)`, `sh+0x1b0=0`, release the capture;
     - `0x46fa70()`, the UI click sound (`0x55aeb8::0x464000(0x32)`);
     - `0x442c00(sh)`, the mode reset;
     - `Cmd->vtbl[0x5c/4](sh+0x1c, sh+0x188, sh+0x194, sh+0x196)`, the build order.
     - Return 1.
  5. **No fog check anywhere.** **H**
- **State 5 (`0x441dbf`):**
  1. Back button, as in state 3.
  2. If `wx==0 && wy==0`, fall through.
  3. If `0x442b10(wx, wy) != 1`, fall through (**this is the fog rejection**).
  4. Otherwise:
     - build a one-point path `{vtbl 0x5033d4, data, count=1, cap=4}` whose point is `Pt{vtbl 0x5031c4, wx, wy}`;
     - `0x46fa70()`;
     - `Cmd->vtbl[0x58/4](sh+0x1c, sh+0x188, &path)`;
     - `0x442c00(sh)`;
     - free the data;
     - return 1. **H**

### Cursor `AShaman::CursorId 0x442db0(sx, sy, wx, wy, ?)`
`ret 0x14`. The table at `0x442ee8` covers states 1..7:
- state 3 → `0x442ee0` → **0** (arrow), but it is only reached over non-visible cells (see §0).
- state 5: `0x442b10(wx, wy)` ok → 0xc/0xe/0x10 for pod types 0x3f/0x40/0x41; failed → 0xb/0xd/0xf.

### Ghost
- Created by `0x442c90`: `Effects[0x579280]::0x423d90(&sh+0x190)` makes an effect of kind 0x4c (**M**), class vtable `0x503fd4`.
- It is updated per mouse move by `0x441170`, which calls `0x4517e0` and stores the code in `+0x28`.
- Every effect draw goes through **`Effect::DrawIfVisible 0x41e0b0`**. That function skips the effect when its cell is not visible to the local player (`0x41e0f4..0x41e16f`). Only kinds 0x2b and 0x45 are always drawn. **So the ghost disappears over fog.** **H** for the cull, **M** that the ghost is kind 0x4c.

---

## 4. Ordering one unit to walk to a point (lockstep-safe)

**Use `Cmd->vtbl[0x34/4]`, `MoveUnits(IdList*, PtList*, short flag)`.** It is thiscall with 3 arguments (`ret 0xc`). **H**
- In SP, `0x460430` executes immediately through `0x461b00` and then journals.
- In MP, `0x45dfc0` serialises the order. It runs later, on the lockstep turn.
- `World::MoveSelection 0x44ccd0(&World+0x7c, &path, 0)` is just this call with the selection. Calling Cmd+0x34 directly with your own one-id list **leaves the selection untouched**.
- The executor `0x461b00` handles counts below 3 one by one: `obj->vtbl[0xb4/4](&path, flag)`, which is `Creature::Move 0x49f0c0`. It ignores non-creatures (types outside 0..0x2b).

Layouts (**H**; nothing keeps or frees the caller's buffers, so static or stack memory is fine):

```cpp
struct Pt     { uint32_t vtbl /*0x5031c4*/; short x, y; };                 // 8 bytes
struct PtList { uint32_t vtbl /*0x5033d4*/; Pt* data; uint32_t count, cap; }; // cap >= count (0x42d1d0 copies cap and allocates cap*8)
struct IdList { uint32_t vtbl /*0x5030dc*/; short* data; uint32_t count, cap; };

void WalkTo(void* sh, short x, short y) {
    static Pt pt; static PtList path; static short id; static IdList ids;
    pt = {0x5031c4, x, y};
    path = {0x5033d4, &pt, 1, 1};
    ((void(__thiscall*)(void*, PtList*))0x44a570)(*(void**)0x579278, &path);  // World::ClampPath: 40 px inside the map edges (optional)
    id = *(short*)((char*)sh + 0x1c);
    ids = {0x5030dc, &id, 1, 1};
    void* cmd = *(void**)0x579260;
    ((void(__thiscall*)(void*, IdList*, PtList*, int))(*(void***)cmd)[0x34 / 4])(cmd, &ids, &path, 0);
}
```

`flag` is the formation/queue word that `0x44ceb0` computes. It is 0 for a single unit. `Creature::Move` stores it in `+0xc4`.

What `Creature::Move 0x49f0c0` does:
1. If `0x407170()` is set (`+0x79` bit 7 while `owner->vtbl+0x7c() != 1`; meaning unknown, **L**), it does nothing.
2. If the unit is already moving to the same final point, it does nothing.
3. Otherwise it calls `vtbl+0xc8` (cancel current work; for a Shaman this is `0x441250`).
4. It copies the path **reversed** into the list at `+0xac` (`data +0xb0`, `count +0xb4`; **goal = `data[0]`**) and in order into `+0xc8`.
5. It sets `+0xc0 = 0x29` (move) and `+0xc4 = flag`.

---

## 5. Issuing build and plant from a tick

| slot | SP → executor | args (all pushed as 32-bit; shorts sign/zero-extended) | ret |
|---|---|---|---|
| `Cmd+0x5c` **Build** | `0x4607f0` → `0x461df0` → `AShaman::Build 0x441400(type, x, y)` | `(short shamanId, int type 0x2c..0x3e, short x, short y)` (centre, world px) | `ret 0x10` |
| `Cmd+0x58` **Plant** | `0x460760` → `0x461da0` → `AShaman::Plant 0x4412f0(type, PtList*)` | `(short shamanId, int podType 0x3f..0x41, PtList* path)` (one point per pod; the click uses 1) | `ret 0xc` |
| `Cmd+0x34` Move | see §4 | `(IdList*, PtList*, short flag)` | `ret 0xc` |
| MP | `0x45ede0`, `0x45ed60` and `0x45dfc0` serialise the same arguments | | same |

The executors re-check that the id is a builder (types 0, 9, 0x12 or 0x1b) and ignore anything else.

- **`Build 0x441400`:**
  - cancels current work (`vtbl+0xc8`) and sets `+0xc0=0x2e`, `+0x18e=0xc01` and `+0x184=type`;
  - `World::0x447460` asks units to clear the site, then `0x441750(&pt)` walks to it;
  - **no validation and no fog check**;
  - the work tick `0x443aeb` creates the building after 0x40 ticks through `World::0x447410` → `0x446db0`. That call re-validates; on failure (0xc01) it retries up to 4 times and then gives up.
- **`Plant 0x4412f0`:**
  - sets `+0xc0=0x2d`, `+0x184=type`, copies the path to `+0x19c`, clears the site and walks;
  - the plant tick (`0x4437cc`) calls `World::0x448100(playerIdx, type, x, y)`, which tries the point and then a ±16 px spiral through `0x446db0`. **The result is ignored**, so an invalid spot simply yields no pod. No fog check.

**What else the click path does, and whether a later issue needs it** (**H**):
- `0x49a6f0(0x5452b0)` releases the Build button, `0x442d20` removes the ghost and the capture, and `0x442c00` resets the mode (`+0x1ac=0`, `+0x18c=0`, release capture). These are **UI state only**. Do them at **click time** in your hook, not at issue time.
- `0x46fa70` is only the click sound.
- `+0x188` is only the UI's chosen type. The executor uses the argument.
- `+0x18e` is set by `Build` itself.

So a deferred issue needs **only the Cmd call**. Issue it on the game thread from an SGame callback (tick), not from another thread. The SP journal (`0x45fe00` check) records tick-issued orders exactly like clicked ones.

Before issuing, re-run the same checks the click would:
- **build:** `0x4517e0(owner+4, x, y, type, 1, 0, 0) == 0`, and optionally `owner->0x437b50(type) == 1` (tech still available);
- **plant:** `0x442b10(sh, x, y) == 1`, which now passes the fog test because the cell is visible.

Affordability is checked by the engine at work time (`0x443bcf owner->vtbl+0x54(type)`), not at order time.

---

## 6. Liveness and "has the player redirected it?"

Liveness (**H**):
- Objects are removed by setting `World+0x10[id] = 0` (`0x4482f5`).
- Ids are **recycled** through the free list at `World+0x40/+0x44` (`0x447267`).
- So remember both the id and the pointer, and require all of the following:
  - `id < World+0x14`;
  - `World+0x10[id] == ptr`;
  - `ptr+0x20 == [0x579288]` (still ours);
  - `ptr+0x18` is in {0, 9, 0x12, 0x1b}.

Order and activity state on the creature (**H** unless marked):

| field | meaning |
|---|---|
| `+0xc0` int | **activity**: `0x28` idle (`0x441790` finish, arrival at `0x49c598`), `0x29` move, `0x2d` plant, `0x2e` build, `0x2f` (`Cmd+0x60` special), `0x35` hold/guard (`vtbl+0xbc 0x49f710`), `0x38` upgrade, `0x39` repair, `0x2a` (other order, **L**) |
| `+0xac` list | remaining path: `data +0xb0` (Pt[]), `count +0xb4`; **`data[0]` = final goal** while `count > 0`; on arrival `count → 0` and activity → idle (`0x49c573..0x49c598`) |
| `+0x14/+0x16` short | current world position |
| `+0x184` | type being built or planted (work order) |
| `+0x18e` | building under construction (resume id) |
| `+0x1ac` | UI panel state (2 = build list, 3 = placing, 4 = pod list, 5 = planting) |

Cancel rules that follow from this:
1. **Arming.** After sending the move, wait until `+0xc0 == 0x29` with the goal (`+0xb0[0]`) equal to the clamped target. In SP this happens immediately. In MP it happens after the lockstep delay, so allow a timeout of about 3 s, then drop.
2. **Once armed:**
   - `+0xc0 == 0x29` with a different goal → the player redirected the Shaman → drop.
   - `+0xc0` not in {0x28, 0x29} → another order (build, plant, repair, upgrade, hold, attack, or death) → drop.
   - `+0xc0 == 0x28` → it arrived or was stopped. Issue if the spot is visible now; otherwise drop.
3. Also drop when the object is gone, has changed owner, or has stopped being a builder.

---

## 7. Recommended implementation

### Hooks (vtable slot swaps, same `PatchSlot` as controls.cpp; no code patches)
1. **`AShaman` down**, slot VA **`0x504a44`** (vtable `0x504a2c`+0x18; expected value `0x4419c0`):
   ```cpp
   short __thiscall HookShamanDown(void* sh, int sx, int sy, int wx, int wy, int zoom)
   ```
   Intercept only when all of these hold:
   - `state = sh+0x1ac` is 3 or 5;
   - the click is **not** on a panel widget (`0x497c60(&Panel 0x5476e8, sx, sy) == 0`) **and** not inside the panel (`World::0x4497d0(sx, sy) != 1`; ecx is ignored, `ret 8`);
   - the click is **not** on the minimap (`0x5517b0::0x433e10() == 1 && 0x5517b0::0x4335b0(sx, sy) == 1` means it is on the minimap).

   Note: vanilla forwards minimap and panel clicks to the builder first.

   Then:
   - **State 3:** `x,y = sh+0x194/+0x196` (the ghost; vanilla uses this, not wx/wy).
     - If `VisibleLocal(x, y)` → call the original.
     - Otherwise:
       - optional pre-filter: for type ≠ 0x2c, if `0x44ed20(owner+4, x, y, 0x54cfb8[type]) != 1` → call the original (it falls through as usual). This saves a pointless walk; power is information the player already has.
       - Record `Pending{id, ptr, BUILD, type=sh+0x188, x, y}`.
       - Run the vanilla accept UI: `0x49a6f0(0x5452b0)`, `0x442d20(sh)`, `0x46fa70()`, `0x442c00(sh)`.
       - `WalkTo(sh, x, y)`; return 1.
   - **State 5:**
     - If `(wx|wy) == 0` or `VisibleLocal(wx, wy)` → call the original.
     - Otherwise:
       - record `Pending{…, PLANT, type=sh+0x188, wx, wy}`;
       - `0x46fa70()`, `0x442c00(sh)`;
       - `WalkTo(sh, wx, wy)`; return 1.
     - Do **not** pre-check terrain or collision. That would leak unexplored terrain.
   - Keep one pending entry per Shaman. A new fog click replaces the old entry.
2. **Per tick:** SGame slot VA **`0x507544`** (expected `0x47ce80`, thiscall, no args, plain `ret`). Call the original first, then run the pending logic. Skip the pending logic when `sg+0x5c == 1` (input disabled or game over).

   For each pending entry:
   - check liveness (§6);
   - apply the arming and cancel rules (§6);
   - if `VisibleLocal(x, y)` (or the Shaman is idle and the spot is visible):
     - **BUILD:** if `0x4517e0(World, owner+4, x, y, type, 1, 0, 0) == 0` (plus tech `0x437b50`), call `Cmd+0x5c(id, type, x, y)`;
     - **PLANT:** if `0x442b10(sh, x, y) == 1`, call `Cmd+0x58(id, type, &PtList{1 point})`;
     - otherwise drop. The game has no error sound for an invalid placement (vanilla just ignores the click); use the 0x32 click or nothing, and log it.
   - Remove the entry after issuing. From then on the engine owns the order, with its own walk, re-validation at creation and give-up.
3. **Optional UI:** hook the `AShaman` cursor slot **`0x504a50`** (`0x442db0`, `ret 0x14`, args `(sx, sy, wx, wy, ?)`). For state 3 or 5 over a non-visible cell, return 1 (the move cursor), so the player sees "will walk there". The build ghost stays hidden in fog because of the `0x41e0b0` cull; drawing it there would mean showing the omniscient result, so leave it.

### Simpler alternative for pods
`Plant 0x4412f0` and its tick have **no fog check**, so the hook could issue `Cmd+0x58` straight away for a fogged click. The Shaman walks there and plants if `0x446db0` allows it, and otherwise silently does nothing. It is less code (no tick and no tracking), but the order is not validated "on reveal", nothing reports an invalid spot, and the Shaman always walks all the way. For builds, vanilla already behaves like this whenever the validator passes.

### Risks and notes
- **MP:**
  - The orders go through `Cmd`, so they are lockstep-safe.
  - The decision uses local data only: the local player's fog, and object state, which is simulated identically everywhere.
  - Only the owning machine issues orders, and only for its own Shamans.
  - Orders take effect after the lockstep delay, which is why arming waits for activity 0x29 with our goal.
  - **M:** `0x45dfc0` reads the path's data pointer, so the move should serialise fine; test in a 2-player LAN game.
- **Info leak:** the build validator is omniscient. With the defer-until-visible hook, a fog click no longer reveals hidden objects or power results (apart from the optional power pre-filter, which uses own data). Vanilla already leaks this through accepted or refused fog clicks.
- **Reveal before arrival:** the Shaman usually sees the spot before reaching it. The order is then issued from wherever it is, and `Build/Plant` walk the rest. A spot that is visible but now invalid (an enemy built there, or the power was lost) is dropped.
- **Idle short of the target** (unreachable destination, or the player pressed Stop): if the spot is not visible, drop it. A Stop by the player produces the same idle state, so it is treated as a cancel. That is correct behaviour.
- **Pathfinder goal rewrite (M):** if the engine ever changes `+0xb0[0]` (for an unreachable goal, for example), the goal comparison would cancel early. To guard against it, snapshot the goal at arming time and compare against the snapshot.
- **Panel and minimap clicks** while in state 3/5 must go to the original handler, as above. Otherwise scrolling by minimap would send the Shaman walking.
- **Quick-build** (`controls.cpp QuickBuild`) seeds `+0x194/+0x196` with **screen** coordinates (`0x5794d8/dc`). They are corrected on the next polled MouseMove, but a click in that same tick would use the wrong spot. Seed them with `0x47ec60(sg, cx)` / `0x47ec80(sg, cy)` instead.
- **Tables** `0x54cfb8`, `0x54de68`, `0x54e370` and `0x54cf08` are in `.data` beyond the raw data, so they are filled at runtime. Read them live; do not hard-code them.

### VA cheat-sheet

| what | VA | convention |
|---|---|---|
| local player | `[0x579288]` / `0x46f8e0()` | cdecl |
| `APlayer::IsTileVisible` | `0x453cc0` | thiscall (tileX, tileY) → short, ret 8 |
| fog flag / vis count / shroud | `+0x484c` / `+0x26[96*96]` / `+0x2426[96*96]` | 64 px cells |
| map size (tiles) | `0x566db4` / `0x566db5` (u8) | |
| `World::CanPlace` | `0x4517e0` | thiscall 7 args, ret 0x1c; 0 ok, 1 blocked, 2 power/pod |
| power check | `0x44ed20(playerIdx, x, y, need)` | thiscall, ret 0x10, 1 = ok |
| `AShaman::CanPlantAt` | `0x442b10(wx, wy)` | thiscall, ret 8, 1 = ok |
| `AShaman` down / up / move / cursor slots | `0x504a44` / `0x504a48` / `0x504a4c` / `0x504a50` | → `0x4419c0` / `0x442230` / `0x442d60` / `0x442db0` |
| remove ghost / reset mode / click sound | `0x442d20` / `0x442c00` / `0x46fa70` | thiscall sh / thiscall sh / cdecl |
| release Build button | `0x49a6f0` with ecx = `0x5452b0` | thiscall |
| `Cmd` Move / Plant / Build | vtbl `+0x34` / `+0x58` / `+0x5c` | see §4/§5 |
| `World::ClampPath` | `0x44a570(PtList*)` | thiscall, ret 4 |
| SGame tick slot | `0x507544` → `0x47ce80` | thiscall, no args |
| panel hit / panel contains / minimap hit | `0x497c60` (ecx `0x5476e8`) / `0x4497d0` / `0x433e10` + `0x4335b0` (ecx `0x5517b0`) | thiscall (sx, sy) |
