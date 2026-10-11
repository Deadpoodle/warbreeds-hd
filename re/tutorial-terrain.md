# Terrain only in the top-left of the frame on small maps (Base Building tutorial)

Disassembly of `WARBREED.EXE` (GOG build, image base 0x400000) plus headless runs with the patch (`[Debug] Autopilot=11`).

## Symptom (reported by the tester)

- In Campaign → Basic Training → Base Building, zoomed out, the terrain was drawn only in a rectangle at the top-left of the frame. The rest was black, but units and buildings were still drawn there.
- Clicking units in the black part selected nothing. It worked when zoomed fully in.
- Skirmish maps were fine.
- Measurements:
  - The tester, at 1920x1080 and 0.5x zoom (frame 3840x2160): terrain in about 1984x1088 frame pixels.
  - Headless, at 0.83x (frame 3072x1728): terrain in 1536x896, and 74% of the frame's 64 px blocks were black.

## Root cause: SGame::SetViewSize halves a plain view that reaches the map edge

The mission's *view* (`SGame +0x4c/+0x4e`) is the world area the game considers visible. It is normally the frame size, or twice that with the game's own 2x zoom-out (`+0x5e = 1`, the Z key).

`0x47b520` SGame::SetViewSize is called from the resize handler `0x47b2f0` (SGame vtable slot 0x4c), and from `0x47af1d`:

```
view = frame size                    (0x5508fc / 0x5508fe)
if (+0x5e == 1) view *= 2            (2x zoom-out)
if (viewW >= mapTilesW * 32 || viewH >= mapTilesH * 32)    (0x47b567, tiles at 0x566db4/5)
    SGame::SetZoomedOut(0)           (call at 0x47b581 -> 0x47d430)
SGame::ClampCamera()                 (0x47d340)
```

`0x47d430` SetZoomedOut(0) clears `+0x5e`, **halves the view unconditionally** and moves the camera by a quarter view to keep it centred. The check was meant to leave the 2x zoom-out when the doubled view no longer fits the map. The original game's largest mode was 1280x1024, smaller than any map seen so far, so a plain view never reached the map and the halving only ever undid a zoom-out.

At HD frame sizes a plain frame can be as large as a small map. Base Building is 96x76 tiles (3072x2432 px). At 0.5x on 1920x1080 the frame is 3840x2160, and with our zoom-out cap it was 3072x1728 (equal to the map width, and the check is `>=`). In both cases the view was halved: 1920x1080 and 1536x864. Skirmish maps (for example 4 Corners, 176x184 tiles) are larger than any frame, so they never hit this.

Effect of a view smaller than the frame:

- **Terrain:** SGame::Draw `0x47b770` passes camera..camera+view (from `0x47dc70`) to the terrain cell drawer `World(0x55dd60)::0x42deb0`, which draws only the 64 px cells in that rectangle. Cells outside it are never drawn, so they stay black. At camera 575,0 with view 1536x864, cells 8..32 x 0..13 were drawn: exactly the 1536x896 frame pixels measured. For the tester, view 1920x1080 gives 31x17 cells = 1984x1088.
- **Units** were still drawn in the black part, which is why it looked like a terrain-only problem.
- **Click selection:** the left-up handler passes camera..camera+view to `World::Click 0x44a860`, which picks among objects in that rectangle (`World_PickAt`), so objects outside it can't be clicked. Headless before the fix, a click on an own unit at frame 2252,328 (view 1536 wide) selected nothing.
- **Drag box:** it was not affected headless. `World::BoxSelect 0x44afe0` gets the drag rectangle, not the view, and a box round the same unit selected it before the fix too.
- The keyboard scroll speed (`camera.cpp`, a fraction of the view width) was also halved.

### Ruled out on the way

- The incremental redraw path. In SGame::Draw at `0x47b876..0x47b8c2`, every redraw condition jumps to "mark all cells" `0x42f210(1,0)` when it holds:
  - more than one scene;
  - `+0x42e == 1`;
  - `+0x436 == 1`;
  - `[0x567110] != 0`;
  - `0x47ccb0` returns 1 (the cursor is near a rectangle in the `+0x19c` list).

  Only when all of them fail does `0x47cdb0` run, the incremental "record what moved" pass. By the code it is the normal path in any mission, skirmish included. `SGame::ClampCamera` marks all cells on every camera change, and a resize clamps the camera, so dirty tracking always covers the full frame. The limit was the rectangle handed to `0x42deb0`, not which cells were dirty.
- The fog of war: the test copy runs with `[Debug] NoFog=1`.

## Fix (`src/camera.cpp`, `camera::InstallViewFix`)

- The call at `0x47b581` (`E8 AA 1E 00 00`, call 0x47d430) is redirected to `FitViewToMap(sgame, 0)`:
  1. If the 2x zoom-out is on (`+0x5e == 1`), it calls the original `0x47d430(0)`, so the game's intended behaviour is kept.
  2. Then it limits each view dimension to the scroll extent (`+0x48 - +0x44`, `+0x4a - +0x46`). `ClampCamera` keeps camera + view below Max, so a larger view would push the camera negative. The terrain drawer doesn't handle that: it clamps the cell index to 0 but keeps the `-(camX & 63)` pixel offset.
- With a frame larger than the map, the whole map is now drawn and clickable, and only the strip beyond the map edge is black.
- It is installed in every zoom mode, because native and resolution zoom and the Resolution setting can produce frames larger than a small map too.
- Smooth zoom's zoom-out cap (`zoom.cpp` MapLimit) is kept. It now uses the extent `Max - Min`, not `Max - Min + 1`, so the capped frame (3070x1726 here) stays inside what ClampCamera allows and doesn't reach the `>=` check at all.

## Verification (headless, `TARGET=1 tools/autotest.sh 11 50`)

| Run | Frame | View | Black 64 px blocks | Click in the far area | Drag box there |
|---|---|---|---|---|---|
| Before, 0.83x | 3072x1728 | 1536x864 | 74% | selected 0 | selected 1 |
| After, 0.83x (cap) | 3070x1726 | 3070x1726 | 0% | selected 1 | selected 1 |
| After, resolution zoom 0.8 (frame wider than the map) | 3200x1800 | 3071x1800 | 4% (only the 128 px beyond the map edge) | selected 1 | selected 1 |

- At the default zoom (1.333x, frame 1920x1080) the frame was fully drawn both before and after, since it is smaller than the map.
- The skirmish mission test (`Autopilot=1`) is unchanged.
