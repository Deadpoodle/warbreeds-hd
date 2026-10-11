// Debug autopilot ([Debug] Autopilot=1): drives the game from the main menu
// into a skirmish and through a fixed test script, saving the layers and the
// displayed image at each step, then quits. Input is posted to the game
// window and the cursor is virtual, so the real mouse and keyboard are never
// touched (combine with [Debug] Headless=1 to keep the window off-screen).
#include "autopilot.h"

#include <windows.h>

#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "diag.h"
#include "game.h"
#include "menu.h"
#include "prof.h"
#include "render/render.h"
#include "camera.h"
#include "editor/editor.h"

namespace autopilot {
namespace {

Hooks g_h;
std::string g_scene;
DWORD g_sceneStart;
int g_step;
bool g_buttonDown;
DWORD g_releaseAt;
POINT g_clickAt;
// A mouse glide in small steps, the way a real mouse moves.
bool g_gliding;
POINT g_glideFrom, g_glideTo;
POINT g_shiftDragTo;
DWORD g_shiftDragUpAt;  // release the Shift+drag (button, then Shift) at this time
DWORD g_glideStart;
constexpr DWORD kGlideMs = 1000;

void Post(UINT msg, WPARAM wp, POINT game) {
    POINT c = render::GamePointToClient(game);
    PostMessageA(render::GameWindow(), msg, wp, MAKELPARAM(c.x, c.y));
}

// Camera state of the base scene, if it is the mission scene.
void LogView() {
    uintptr_t* scenes = (uintptr_t*)game::Scenes;
    uintptr_t g = scenes[0];
    if (!g || *(uintptr_t*)g != game::VtblSGame) return;
    auto sh = [g](uintptr_t off) { return (int)*(short*)(g + off); };
    g_h.log("autopilot:   camera %d,%d view %dx%d zoomedOut %d screen %dx%d", sh(game::SGameCameraX),
            sh(game::SGameCameraY), sh(game::SGameViewW), sh(game::SGameViewH), sh(game::SGameZoomedOut),
            game::at<short>(game::ScreenRight), game::at<short>(game::ScreenBottom));
    // Mode 12 (group build): every own builder, selected or not.
    if (g_h.mode == 12) {
        char* w = *(char**)0x579278;
        void* me = ((void* (*)())0x46f8e0)();
        char** sel = *(char***)(w + 0x80);
        int n = *(int*)(w + 0x84);
        char** things = *(char***)(w + 0x10);
        int count = *(int*)(w + 0x14);
        g_h.log("autopilot:   selected %d", n);
        for (int i = 0; i < count; ++i) {
            char* t = things[i];
            int ty = t ? *(int*)(t + 0x18) : -1;
            if (!t || *(void**)(t + 0x20) != me || !(ty == 0 || ty == 9 || ty == 0x12 || ty == 0x1b)) continue;
            bool s = false;
            for (int k = 0; k < n; ++k) s |= sel[k] == t;
            short gx = -1, gy = -1;
            if (*(int*)(t + 0xb4) > 0) gx = *(short*)(*(char**)(t + 0xb0) + 4), gy = *(short*)(*(char**)(t + 0xb0) + 6);
            g_h.log("autopilot:   builder id %d%s state %d activity %#x workTarget %d pos %d,%d goal %d,%d",
                    *(short*)(t + 0x1c), s ? " (selected)" : "", *(int*)(t + 0x1ac), *(int*)(t + 0xc0),
                    *(short*)(t + 0x18e), *(short*)(t + 0x14), *(short*)(t + 0x16), gx, gy);
        }
    }
    // Selection and own buildings (mode 8, the controls test; 12, group build).
    if (g_h.mode == 8 || g_h.mode == 9 || g_h.mode == 12 || g_h.mode == 16) {
        char* w = *(char**)0x579278;
        void* me = ((void* (*)())0x46f8e0)();
        int n = *(int*)(w + 0x84);
        char** sel = *(char***)(w + 0x80);
        if (n > 0 && sel && sel[0])
            g_h.log("autopilot:   selected %d, first type %#x state %d buildType %#x captured %d/%#x at %d,%d activity %#x pos %d,%d", n,
                    *(int*)(sel[0] + 0x18), *(int*)(sel[0] + 0x1ac), *(int*)(sel[0] + 0x188), *(short*)(sel[0] + 8),
                    *(unsigned short*)(g + 0x1f0), *(short*)(sel[0] + 0x194), *(short*)(sel[0] + 0x196), *(int*)(sel[0] + 0xc0),
                    *(short*)(sel[0] + 0x14), *(short*)(sel[0] + 0x16));
        else
            g_h.log("autopilot:   selected %d", n);
        char** things = *(char***)(w + 0x10);
        int count = *(int*)(w + 0x14);
        for (int i = 0; i < count; ++i) {
            char* t = things[i];
            if (!t || *(void**)(t + 0x20) != me || *(int*)(t + 0x18) < 0x2c || *(int*)(t + 0x18) > 0x3e) continue;
            g_h.log("autopilot:   own building id %d type %#x complete %d progress %d hp %d on %d unpowered %d",
                    *(short*)(t + 0x1c), *(int*)(t + 0x18), *(short*)(t + 0x2c), *(short*)(t + 0x32),
                    *(short*)(t + 0x24), *(short*)(t + 0x2a), *(short*)(t + 0x28));
        }
    }
}

int SelectedCount() {
    char* w = *(char**)0x579278;
    return w ? *(int*)(w + 0x84) : 0;
}

// Mode 11: the own unit (creature types 0..0x2b, as World::BoxSelect selects) whose frame
// position is furthest right/down, as a frame point; x = -1 if none is in view.
POINT g_target, g_dragTo;
POINT FarOwnUnit(int frameW, int frameH) {
    POINT best{-1, -1};
    uintptr_t g = ((uintptr_t*)game::Scenes)[0];
    char* w = *(char**)0x579278;
    void* me = ((void* (*)())0x46f8e0)();
    if (!g || !w || !me) return best;
    int f = *(short*)(g + game::SGameZoomedOut) == 1 ? 2 : 1;
    int camX = *(short*)(g + game::SGameCameraX), camY = *(short*)(g + game::SGameCameraY);
    char** things = *(char***)(w + 0x10);
    int count = *(int*)(w + 0x14);
    double bestScore = -1;
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t || *(void**)(t + 0x20) != me || *(int*)(t + 0x18) < 0 || *(int*)(t + 0x18) >= 0x2c) continue;
        int x = (*(short*)(t + 0x14) - camX) / f, y = (*(short*)(t + 0x16) - camY) / f;
        g_h.log("autopilot:   own unit id %d type %#x at world %d,%d, frame %d,%d", *(short*)(t + 0x1c),
                *(int*)(t + 0x18), *(short*)(t + 0x14), *(short*)(t + 0x16), x, y);
        if (x < 100 || y < 100 || x > frameW - 100 || y > frameH - 100) continue;
        double score = std::max((double)x / frameW, (double)y / frameH);
        if (score > bestScore) {
            bestScore = score;
            best = {x, y};
        }
    }
    return best;
}

void Click(POINT game) {
    render::SetFakeCursor(true, game);
    Post(WM_MOUSEMOVE, 0, game);
    Post(WM_LBUTTONDOWN, MK_LBUTTON, game);
    g_buttonDown = true;
    g_clickAt = game;
    g_releaseAt = GetTickCount() + 120;
    g_h.log("autopilot: click %ld,%ld in %s (game cursor %d,%d)", game.x, game.y, g_scene.c_str(),
            game::at<short>(game::CursorX), game::at<short>(game::CursorY));
    LogView();
}

void Key(WPARAM vk) {
    PostMessageA(render::GameWindow(), WM_KEYDOWN, vk, 1);
    PostMessageA(render::GameWindow(), WM_KEYUP, vk, 0xC0000001);
    g_h.log("autopilot: key %#x", (unsigned)vk);
}

// The mission test: glide the mouse to the starting Shaman (4 Corners map,
// Tanu) and click it, order it to a spot on the ground at the starting zoom,
// fully zoomed out and fully zoomed in (capturing the order marker against
// the drawn cursor), zoom with captures during and after each animation, then
// open and close the Esc menu. Points are in thousandths of the frame size, so
// they stay on screen at every zoom.
enum Act { SaveShot, ZoomOut, ZoomIn, Esc, Quit, CursorCentre, Glide, ClickHere, KeyTap, RightClickHere, ShiftDrag,
           RecordStart, RecordStop, HoldKey, ReleaseKey, SpawnBuilders, GlideToBuildSpot,
           ClickPanelButton, GlideToOwnBuilding, SpawnThing, CursorClient, WheelNotches, ClickClient, SetCGK, PickSpecies, SpawnAllCreatures, SpawnFlyers };
struct Step {
    DWORD at;
    Act act;
    const char* tag;
    POINT to;
};
const Step kMission[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, Glide, nullptr, {211, 196}},  // the Shaman
    {3200, ClickHere, nullptr, {}},
    {4000, SaveShot, "a-start", {}},
    {4200, Glide, nullptr, {600, 600}},
    {5400, ClickHere, nullptr, {}},
    {5550, SaveShot, "a1-order-150ms", {}},
    {5900, SaveShot, "a2-order-500ms", {}},
    {6200, ZoomOut, nullptr, {}},
    {6330, SaveShot, "b-zoomout-anim1", {}},
    {6480, SaveShot, "c-zoomout-anim2", {}},
    {8000, SaveShot, "d-zoomed-out", {}},
    {8200, Glide, nullptr, {300, 700}},
    {9400, ClickHere, nullptr, {}},
    {9550, SaveShot, "d1-order-150ms", {}},
    {9900, SaveShot, "d2-order-500ms", {}},
    {10200, ZoomIn, nullptr, {}},
    {10330, SaveShot, "e-zoomin-anim1", {}},
    {10480, SaveShot, "f-zoomin-anim2", {}},
    {12500, SaveShot, "g-zoomed-in", {}},
    {12700, Glide, nullptr, {700, 400}},
    {13900, ClickHere, nullptr, {}},
    {14050, SaveShot, "g1-order-150ms", {}},
    {14400, SaveShot, "g2-order-500ms", {}},
    {15000, Esc, nullptr, {}},
    {16000, SaveShot, "h-esc-open", {}},
    {16500, Esc, nullptr, {}},
    {17500, SaveShot, "i-esc-closed", {}},
    {18000, Quit, nullptr, {}},
};

// Mode 8, the controls test: select the Shaman, open the build list (B),
// quick-build (AutopilotTarget = the key, default T), place it, stop the
// Shaman (S) and click it onto the unfinished building (repair), then the
// control groups: Ctrl+1, deselect, double-tap 1, deselect, Shift+1.
// KeyTap: to.x = the key, to.y = a modifier held with it (0 = none).
// Mode 9, the Modern mouse test (needs [Input] MouseScheme=modern): left-click
// the Shaman (select), left-click ground (deselect), select again, right-click
// ground (move, still selected), P 1 (plant mode) then right-click (mode
// cancelled, still selected).
const Step kModern[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, Glide, nullptr, {211, 196}},  // the Shaman
    {3200, ClickHere, nullptr, {}},
    {3700, SaveShot, "m0-selected", {}},
    {3800, Glide, nullptr, {500, 500}},
    {5000, ClickHere, nullptr, {}},  // ground: deselect
    {5500, SaveShot, "m1-deselected", {}},
    {5600, Glide, nullptr, {211, 196}},
    {6800, ClickHere, nullptr, {}},
    {7300, Glide, nullptr, {420, 380}},
    {8500, RightClickHere, nullptr, {}},  // ground: move
    {9500, SaveShot, "m2-moving", {}},
    {9700, KeyTap, nullptr, {'P', 0}},
    {10200, KeyTap, nullptr, {'1', 0}},
    {10800, SaveShot, "m3-plantmode", {}},
    {11000, RightClickHere, nullptr, {}},  // cancel the mode, keep the selection
    {11800, SaveShot, "m4-cancelled", {}},
    // Shift+drag a box round the Shaman (walked to about 417,384) after deselecting.
    {12000, Glide, nullptr, {600, 700}},
    {13200, ClickHere, nullptr, {}},
    {13400, Glide, nullptr, {330, 290}},
    {14600, ShiftDrag, nullptr, {520, 480}},
    {15400, SaveShot, "m5-shift-drag", {}},
    {15800, Quit, nullptr, {}},
};

// Mode 10, frames for a GIF of the smooth zoom and scrolling: select the
// Shaman, record, zoom out and back in with the cursor off-centre, scroll
// right then down with the arrow keys, stop.
const Step kRecord[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, Glide, nullptr, {211, 196}},
    {3200, ClickHere, nullptr, {}},
    {3600, RecordStart, nullptr, {}},
    {3900, ZoomOut, nullptr, {}},                // out, anchored on the Shaman
    {5600, Glide, nullptr, {700, 420}},          // over to the chasm edge
    {6900, ZoomIn, nullptr, {}},                 // in towards it
    {8800, HoldKey, nullptr, {VK_DOWN, 0}},
    {10000, ReleaseKey, nullptr, {VK_DOWN, 0}},
    {10100, HoldKey, nullptr, {VK_LEFT, 0}},
    {11400, ReleaseKey, nullptr, {VK_LEFT, 0}},
    {11900, RecordStop, nullptr, {}},
    {12400, Quit, nullptr, {}},
};

const Step kControls[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, Glide, nullptr, {211, 196}},  // the Shaman
    {3200, ClickHere, nullptr, {}},
    {3700, SaveShot, "c0-selected", {}},
    // Plant a pod (P, 1) near the Shaman: a Refinery needs one nearby.
    {3900, KeyTap, nullptr, {'P', 0}},
    {4400, KeyTap, nullptr, {'1', 0}},
    {4600, Glide, nullptr, {150, 88}},
    {5800, SaveShot, "c2a-planting", {}},
    {6000, ClickHere, nullptr, {}},
    {6800, ClickHere, nullptr, {}},
    {16000, SaveShot, "c2b-planted", {}},
    // Quick-build (B, then AutopilotTarget = the key, default R) beside it.
    {16200, KeyTap, nullptr, {'B', 0}},
    {16900, SaveShot, "c1-buildlist", {}},
    {17100, KeyTap, nullptr, {-1, 0}},
    {17300, Glide, nullptr, {230, 120}},
    {18600, SaveShot, "c2c-placing", {}},
    {18800, ClickHere, nullptr, {}},
    {19600, ClickHere, nullptr, {}},
    {24000, SaveShot, "c3-building", {}},
    {24200, KeyTap, nullptr, {'S', 0}},  // stop: the building stays unfinished
    {25500, SaveShot, "c3b-stopped", {}},
    {25700, ClickHere, nullptr, {}},  // the unfinished building
    {28500, SaveShot, "c4-repair", {}},
    {28700, KeyTap, nullptr, {'1', VK_CONTROL}},
    {29200, RightClickHere, nullptr, {}},
    {29700, KeyTap, nullptr, {'1', 0}},
    {29850, KeyTap, nullptr, {'1', 0}},
    {30500, SaveShot, "c5-group", {}},
    {30700, RightClickHere, nullptr, {}},
    {31200, KeyTap, nullptr, {'1', VK_SHIFT}},
    {31800, SaveShot, "c6-shift-add", {}},
    {32200, Quit, nullptr, {}},
};

// Mode 12, group build ([Input] GroupBuild): a skirmish starts with one Shaman,
// so two more of its type are created beside it with the engine's own
// cheat-console placement (0x4472d0, as SGame::LButtonDown 0x47d6d3 calls it;
// AutopilotTarget = how many, -1 none). E selects all three. Then, with the
// panel's own buttons, plant and pod 1 and a click on pod soil (the nearest
// plants); B and R (quick-build Refinery), first a click where it can't go
// (refused: the mode stays, nobody moves), then a glide to a valid Refinery
// spot beside the pod and a click (the nearest builds, the others join);
// later the panel's cancel (all stop) and repair, then a click on the
// building (all repair, which finishes it). The log lists every builder.
const Step kGroup[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, SpawnBuilders, nullptr, {2, 0}},
    {2600, KeyTap, nullptr, {'E', 0}},  // select all own units on screen
    {3200, SaveShot, "g0-group", {}},
    {3400, ClickPanelButton, nullptr, {0x544e20, 0}},  // plant
    {3900, SaveShot, "g1-podlist", {}},
    {4100, ClickPanelButton, nullptr, {0x545730, 0}},  // the first pod
    {4300, Glide, nullptr, {150, 88}},                // pod soil
    {5500, ClickHere, nullptr, {}},
    {6000, SaveShot, "g2-planting", {}},
    {15000, SaveShot, "g3-planted", {}},
    {15200, KeyTap, nullptr, {'B', 0}},
    {15800, SaveShot, "g4-buildlist", {}},
    {16000, KeyTap, nullptr, {'R', 0}},
    {16200, Glide, nullptr, {500, 600}},  // no pod in reach: refused, the mode stays
    {17400, ClickHere, nullptr, {}},
    {18000, SaveShot, "g5a-refused", {}},
    {18200, GlideToBuildSpot, nullptr, {}},
    {19600, SaveShot, "g5-placing", {}},
    {19800, ClickHere, nullptr, {}},
    {20600, SaveShot, "g6-heading", {}},
    {24000, SaveShot, "g7-founding", {}},
    {28000, SaveShot, "g8-building", {}},
    {33000, SaveShot, "g9-together", {}},
    {33200, ClickPanelButton, nullptr, {0x546df8, 0}},  // cancel: all stop
    {34000, SaveShot, "g10-cancelled", {}},
    {34200, ClickPanelButton, nullptr, {0x546928, 0}},  // repair
    {34600, GlideToOwnBuilding, nullptr, {0x2c, 0}},
    {35900, ClickHere, nullptr, {}},
    {36800, SaveShot, "g11-repair", {}},
    {46000, SaveShot, "g12-later", {}},
    {46500, Quit, nullptr, {}},
};

// Mode 16, the tallest panel: a finished Gene Lab+ is created beside the
// starting Shaman (CreateThing with its checks skipped, kept powered) and
// selected, a species chosen, which opens Select Components with every
// component (gene level 5): the panel that sets the interface size cap. It
// must fit the window; a hover and a click on its bottom button (count 1)
// show the clicks still land where it is drawn.
// CursorClient / ClickClient: thousandths of the window.
const Step kTallPanel[] = {
    {1500, CursorCentre, nullptr, {}},
    {1800, SetCGK, nullptr, {5, 0}},         // every clan's gene level 5: every component
    {2000, SpawnThing, nullptr, {0x35, 0}},  // a Gene Lab+
    {2600, GlideToOwnBuilding, nullptr, {0x35, 0}},
    {3800, ClickHere, nullptr, {}},
    {4600, SaveShot, "t0a-genelab", {}},
    {4800, PickSpecies, nullptr, {1, 0}},  // a scout: Select Components
    {5000, CursorClient, nullptr, {500, 500}},
    {5600, SaveShot, "t0-tall-panel", {}},
    {5800, CursorClient, nullptr, {965, 870}},  // near the bottom of the panel
    {6400, SaveShot, "t1-hover-bottom", {}},
    {6600, ClickClient, nullptr, {965, 870}},
    {7400, SaveShot, "t2-clicked-bottom", {}},
    {7600, ZoomIn, nullptr, {}},
    {7700, ZoomIn, nullptr, {}},
    {9800, SaveShot, "t3-full-in", {}},
    {10000, WheelNotches, nullptr, {-1, 0}},
    {11400, SaveShot, "t4-out1a", {}},
    {11550, SaveShot, "t4-out1b", {}},
    {11800, WheelNotches, nullptr, {-1, 0}},
    {13200, SaveShot, "t4-out2a", {}},
    {13350, SaveShot, "t4-out2b", {}},
    {13600, WheelNotches, nullptr, {-1, 0}},
    {15000, SaveShot, "t4-out3a", {}},
    {15150, SaveShot, "t4-out3b", {}},
    {15400, WheelNotches, nullptr, {-1, 0}},
    {16800, SaveShot, "t4-out4a", {}},
    {16950, SaveShot, "t4-out4b", {}},
    {17200, WheelNotches, nullptr, {-1, 0}},
    {18600, SaveShot, "t4-out5a", {}},
    {18750, SaveShot, "t4-out5b", {}},
    {19000, WheelNotches, nullptr, {-1, 0}},
    {20400, SaveShot, "t4-out6a", {}},
    {20550, SaveShot, "t4-out6b", {}},
    {20800, WheelNotches, nullptr, {-1, 0}},
    {22200, SaveShot, "t4-out7a", {}},
    {22350, SaveShot, "t4-out7b", {}},
    {22600, Quit, nullptr, {}},
};

// Mode 17: what a dead creature leaves behind (two creatures created beside
// the Shaman and killed at once), photographed over 10 seconds.
const Step kCorpse[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, SpawnThing, nullptr, {3, 1}},  // T-Rex, killed
    {2100, SpawnThing, nullptr, {2, 1}},  // raptor, killed
    {3000, SaveShot, "k0-died", {}},
    {6000, SaveShot, "k1-later", {}},
    {12000, SaveShot, "k2-much-later", {}},
    {12500, Quit, nullptr, {}},
};

// Mode 18: every creature type for player 1 (an enemy), in a grid right of
// the Shaman, photographed at the starting zoom, fully zoomed out (the game's
// half-scale view) and back in. The log lists each type and where it stands.
const Step kAllCreatures[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, SpawnAllCreatures, nullptr, {1, 1}},  // armed
    {2600, SaveShot, "z0-normal", {}},
    {2800, ZoomOut, nullptr, {}},
    {2900, ZoomOut, nullptr, {}},
    {5000, SaveShot, "z1-zoomed-out", {}},
    {6000, SaveShot, "z1b-zoomed-out", {}},
    {7000, SaveShot, "z1c-zoomed-out", {}},
    {9000, SaveShot, "z1d-zoomed-out", {}},
    {12000, SaveShot, "z1e-zoomed-out", {}},
    {12200, ZoomIn, nullptr, {}},
    {14500, SaveShot, "z2-back-in", {}},
    {15000, Quit, nullptr, {}},
};

// Mode 19, flying units in flight at every zoom: two of each air unit (Jubjub,
// Aphid, Viper) for the local player beside the Shaman, armed; E selects them,
// then they are ordered back and forth while the zoom steps out a notch at a
// time from fully in, with shots during each flight.
const Step kFlyers[] = {
    {1500, CursorCentre, nullptr, {}},
    {2000, SpawnFlyers, nullptr, {}},
    {2400, ZoomIn, nullptr, {}},
    {2500, ZoomIn, nullptr, {}},
    {4500, KeyTap, nullptr, {'E', 0}},
    {5000, Glide, nullptr, {750, 600}},
    {5900, ClickHere, nullptr, {}},
    {6500, SaveShot, "f0-a", {}},
    {7300, SaveShot, "f0-b", {}},
    {7600, WheelNotches, nullptr, {-1, 0}},
    {9200, Glide, nullptr, {250, 300}},
    {10100, ClickHere, nullptr, {}},
    {10700, SaveShot, "f1-a", {}},
    {11500, SaveShot, "f1-b", {}},
    {11800, WheelNotches, nullptr, {-1, 0}},
    {13400, Glide, nullptr, {750, 600}},
    {14300, ClickHere, nullptr, {}},
    {14900, SaveShot, "f2-a", {}},
    {15700, SaveShot, "f2-b", {}},
    {16000, WheelNotches, nullptr, {-1, 0}},
    {17600, Glide, nullptr, {250, 300}},
    {18500, ClickHere, nullptr, {}},
    {19100, SaveShot, "f3-a", {}},
    {19900, SaveShot, "f3-b", {}},
    {20200, WheelNotches, nullptr, {-1, 0}},
    {21800, Glide, nullptr, {750, 600}},
    {22700, ClickHere, nullptr, {}},
    {23300, SaveShot, "f4-a", {}},
    {24100, SaveShot, "f4-b", {}},
    {24400, WheelNotches, nullptr, {-1, 0}},
    {26000, Glide, nullptr, {250, 300}},
    {26900, ClickHere, nullptr, {}},
    {27500, SaveShot, "f5-a", {}},
    {28300, SaveShot, "f5-b", {}},
    {28600, WheelNotches, nullptr, {-1, 0}},
    {30200, Glide, nullptr, {750, 600}},
    {31100, ClickHere, nullptr, {}},
    {31700, SaveShot, "f6-a", {}},
    {32500, SaveShot, "f6-b", {}},
    {32800, WheelNotches, nullptr, {-1, 0}},
    {34400, Quit, nullptr, {}},
};

// --- mode 13: the map editor ---------------------------------------------------
//
// AutopilotTarget 0: main menu -> Map Editor -> "4 Corners" (MPMAP1); paint
// three strokes (Lava brush 1, Sand brush 3, Crystal brush 1), undo the last
// (Ctrl+Z), right-click pick, Save As HDTEST, a wheel zoom-out, Test Map (asks
// once, then a skirmish on it).
// AutopilotTarget 1: Map Editor -> New Map, 48 x 48, Create; one Lava stroke,
// Save As HDNEW, Test Map.
// AutopilotTarget 2: Map Editor -> "East vs. West" (MPMAP7), one stroke, Exit
// (refused once: unsaved changes), Exit again: back to the map list.
// AutopilotTarget 3 (Stage 2, things and units): New Map 48 x 48; Roots and
// Cropland strokes; Things: a Sand Tree on the roots, Yedda Ruins, Fill Pods,
// right-click removes a pod, Ctrl+Z puts it back; Units: a Refinery by the
// cropland, a Turret in its power field and one far away, a Raptor and a
// Scout; Select: the Raptor's hit points, rank and a gizmo, drag it
// elsewhere; delete the Scout, undo, redo; Check (the far Turret has no
// power); Save As HDTHING; Test Map.
// AutopilotTarget 4 (Stage 2, players and map): "4 Corners"; Players: player
// 2 Computer -> Human -> Either, next race, CGK +1, a clan toggled, a start
// point; player 6 -> None; Map: name "HD Test Map", fog without shroud,
// Supported 6 -> 5 (a second step refused: player 5 exists), Recommended 4 ->
// 3, undo and redo it; Units: delete player 3's Shaman; Check (player 3 has
// no Shaman); Save As HDPLAY; Test Map.
// Targets 3 and 4 log what they did as "x ..." lines in the expect file;
// the editor writes the engine's things, players and map header on save
// (WarBreedsHD-editor-world.txt).
// Every painted cell, in order, goes to WarBreedsHD-editor-expect.txt, and the
// editor writes its own diff on save (WarBreedsHD-editor-save.txt), for
// tools/editor_check.py to compare with the saved file.
FILE* g_expect;

void Expect(const char* fmt, ...) {
    if (!g_expect) g_expect = fopen((g_h.dir + "WarBreedsHD-editor-expect.txt").c_str(), "w");
    if (!g_expect) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_expect, fmt, ap);
    va_end(ap);
    fputc('\n', g_expect);
    fflush(g_expect);
}

// Click a widget of the top scene (found by its text) where the interface layer shows it.
bool ClickWidget(const char* text, bool menu) {
    long x, y;
    if (!menu::FindWidget(text, x, y) || x < 0 || y < 0) {
        g_h.log("autopilot: widget \"%s\" not found", text);
        return false;
    }
    if (menu) {  // menus: the frame is the screen
        Click({x, y});
        return true;
    }
    POINT c = render::InterfacePointToClient({x, y});
    HWND hw = render::GameWindow();
    render::SetFakeCursorClient(c);
    PostMessageA(hw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
    PostMessageA(hw, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
    PostMessageA(hw, WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
    g_h.log("autopilot: click \"%s\" at frame %ld,%ld (client %ld,%ld)", text, x, y, c.x, c.y);
    return true;
}

std::vector<POINT> g_strokePts;  // a drag in progress
size_t g_strokeAt;

// One step of a drag each frame; true while it lasts.
bool StrokeTick() {
    if (g_strokePts.empty()) return false;
    if (g_strokeAt < g_strokePts.size()) {
        POINT p = g_strokePts[g_strokeAt++];
        render::SetFakeCursor(true, p);
        Post(WM_MOUSEMOVE, MK_LBUTTON, p);
    } else {
        Post(WM_LBUTTONUP, 0, g_strokePts.back());
        g_strokePts.clear();
    }
    return true;
}

// A palette name as one word, for the expect file ("Hot Mud" -> Hot_Mud).
std::string OneWord(std::string s) {
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

// The first terrain in `prefer` (other than `skip`) the painter accepts along a
// stroke. The raw painter accepts anything; the smart one only what the
// original editor's rules allow next to the terrain already there.
std::string Choose(std::initializer_list<const char*> prefer, const std::string& skip, int size, int x0, int y0,
                   int x1, int y1) {
    for (const char* t : prefer) {
        std::string why;
        if (t == skip) continue;
        if (editor::StrokeWorks(t, size, x0, y0, x1, y1, &why)) return t;
        g_h.log("autopilot: %s refused here: %s", t, why.empty() ? "(no change)" : why.c_str());
    }
    g_h.log("autopilot: no terrain paints at %d,%d -> %d,%d", x0, y0, x1, y1);
    return *prefer.begin();
}

// A stroke over cells (x0, y0) -> (x1, y1), one cell per frame.
bool Stroke(const char* type, int brush, int x0, int y0, int x1, int y1) {
    int n = std::max(abs(x1 - x0), abs(y1 - y0));
    std::vector<POINT> pts;
    for (int i = 0; i <= n; ++i) {
        POINT p;
        int cx = n ? x0 + (x1 - x0) * i / n : x0, cy = n ? y0 + (y1 - y0) * i / n : y0;
        if (!editor::CellCentre(cx, cy, p)) {
            g_h.log("autopilot: cell %d,%d is off screen", cx, cy);
            return false;
        }
        pts.push_back(p);
    }
    Expect("begin %s %d %s", OneWord(type).c_str(), editor::TypeTile(type), editor::TypeTiles(type).c_str());
    for (int i = 0; i <= n; ++i)
        Expect("paint %d %d %d %d", editor::TypeTile(type), brush, n ? x0 + (x1 - x0) * i / n : x0,
               n ? y0 + (y1 - y0) * i / n : y0);
    Expect("end");
    // The game polls the cursor once a frame (0x46f400), so the drag moves
    // one cell per frame (StrokeTick); the button goes down here.
    render::SetFakeCursor(true, pts[0]);
    Post(WM_MOUSEMOVE, 0, pts[0]);
    Post(WM_LBUTTONDOWN, MK_LBUTTON, pts[0]);
    g_strokePts = pts;
    g_strokeAt = 1;
    g_h.log("autopilot: stroke %s brush %d cells %d,%d -> %d,%d", type, brush, x0, y0, x1, y1);
    return true;
}

void PostKey(UINT msg, WPARAM vk) { PostMessageA(render::GameWindow(), msg, vk, msg == WM_KEYUP ? 0xC0000001 : 1); }

void TypeName(const char* name) {
    for (int i = 0; i < 10; ++i) {
        PostKey(WM_KEYDOWN, VK_BACK);
        PostKey(WM_KEYUP, VK_BACK);
    }
    for (const char* p = name; *p; ++p) PostMessageA(render::GameWindow(), WM_CHAR, (WPARAM)*p, 1);
    PostKey(WM_KEYDOWN, VK_RETURN);
    PostKey(WM_KEYUP, VK_RETURN);
    g_h.log("autopilot: typed %s + Enter", name);
}

// Stage 2 helpers: panel items by exact text or by row, map clicks and drags.
bool ClickPanelAt(long x, long y, const char* what, bool right = false) {
    POINT c = render::InterfacePointToClient({x, y});
    HWND hw = render::GameWindow();
    render::SetFakeCursorClient(c);
    PostMessageA(hw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
    PostMessageA(hw, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(c.x, c.y));
    PostMessageA(hw, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
    g_h.log("autopilot: click%s \"%s\" at frame %ld,%ld", right ? " (right)" : "", what, x, y);
    return true;
}

bool ClickItem(const std::string& text) {
    long x, y;
    if (!editor::FindItem(text.c_str(), x, y)) {
        g_h.log("autopilot: item \"%s\" not shown", text.c_str());
        return false;
    }
    return ClickPanelAt(x, y, text.c_str());
}

bool ClickRow(int row, int index, const char* what) {
    long x, y;
    if (!editor::FindItemOnRow(row, index, x, y)) {
        g_h.log("autopilot: no item %d on page row %d (%s)", index, row, what);
        return false;
    }
    return ClickPanelAt(x, y, what);
}

void ClickMap(POINT p, bool right = false) {
    render::SetFakeCursor(true, p);
    Post(WM_MOUSEMOVE, 0, p);
    Post(right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, p);
    Post(right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, p);
    g_h.log("autopilot: %s map at %ld,%ld", right ? "right-click" : "click", p.x, p.y);
}

bool ClickCell(int cx, int cy) {
    POINT p;
    if (!editor::CellCentre(cx, cy, p)) {
        g_h.log("autopilot: cell %d,%d is off screen", cx, cy);
        return false;
    }
    ClickMap(p);
    return true;
}

// A free cell of the kind near (x, y), clicked; false (logged) if there is none.
bool ClickFreeCell(int what, int x, int y, int clear, int& cx, int& cy) {
    if (!editor::FindCell(what, x, y, cx, cy, clear)) {
        g_h.log("autopilot: no free cell of kind %d near %d,%d", what, x, y);
        return false;
    }
    return ClickCell(cx, cy);
}

void DragMap(POINT a, POINT b) {
    render::SetFakeCursor(true, a);
    Post(WM_MOUSEMOVE, 0, a);
    Post(WM_LBUTTONDOWN, MK_LBUTTON, a);
    for (int i = 1; i <= 6; ++i) Post(WM_MOUSEMOVE, MK_LBUTTON, {a.x + (b.x - a.x) * i / 6, a.y + (b.y - a.y) * i / 6});
    render::SetFakeCursor(true, b);
    Post(WM_LBUTTONUP, 0, b);
    g_h.log("autopilot: drag %ld,%ld -> %ld,%ld", a.x, a.y, b.x, b.y);
}

void CtrlKey(WPARAM k) {
    PostKey(WM_KEYDOWN, VK_CONTROL);
    PostKey(WM_KEYDOWN, k);
    PostKey(WM_KEYUP, k);
    PostKey(WM_KEYUP, VK_CONTROL);
    g_h.log("autopilot: Ctrl+%c", (char)k);
}

void TypeText(const char* text) {
    for (const char* p = text; *p; ++p) PostMessageA(render::GameWindow(), WM_CHAR, (WPARAM)*p, 1);
    PostKey(WM_KEYDOWN, VK_RETURN);
    PostKey(WM_KEYUP, VK_RETURN);
    g_h.log("autopilot: typed \"%s\" + Enter", text);
}

void ExpectFindings() {
    std::vector<std::string> f = editor::Findings();
    Expect("x findings %d", (int)f.size());
    for (const std::string& s : f) Expect("x finding %s", s.c_str());
}

void EditorTest(const std::string& scene, int frameW, int frameH, DWORD now) {
    static int stage;
    static DWORD at;
    static int bx, by;  // the strokes' base cell
    static std::string t1, t2, t3;  // the strokes' terrains
    static int clicks;
    bool fresh = g_h.target == 1, quit = g_h.target == 2, things = g_h.target == 3, players = g_h.target == 4;
    bool newMap = fresh || things;
    bool stage2 = things || players;
    const char* name = fresh ? "HDNEW" : things ? "HDTHING" : players ? "HDPLAY" : "HDTEST";
    static int rx, ry;  // target 3: the Refinery's cell
    static int hx, hy;  // target 3: a cell behind the panel
    auto tag = [&](const char* n, const char* e, const char* s2) {
        static std::string t;
        t = stage2 ? std::string(things ? "t" : "p") + s2 : fresh ? n : e;
        return t.c_str();
    };
    auto next = [&](int s) {
        stage = s;
        at = now;
    };
    if (StrokeTick()) {
        at = now;
        return;
    }
    DWORD dt = now - at;
    switch (stage) {
    case 0:
        if (scene == ".?AVSHeadquarters@@" && g_h.topScene && strstr(g_h.topScene(), "SMissionMenu") && dt > 0 &&
            now - g_sceneStart > 2500) {
            DeleteFileA((g_h.dir + "DATA\\MAPS\\" + name + ".WBM").c_str());  // left from an earlier run
            Save("e0-main-menu");
            ClickWidget("Map Editor", true);
            next(1);
        }
        break;
    case 1:
        if (dt > 1500) {
            Save("e1-map-list");
            if (stage2) Expect("x target %d", g_h.target);
            ClickWidget(newMap ? "New Map" : quit ? "East vs. West" : "4 Corners", true);
            next(newMap ? 20 : 2);
        }
        break;
    case 20:  // New Map page: the size preset to 48 x 48
        if (dt > 1200) {
            long x, y;
            if (menu::FindWidget("Size: 48 x 48", x, y)) {
                Save("n0-new-map-page");
                ClickWidget("Create Map", true);
                next(2);
            } else if (++clicks > 6) {
                g_h.log("autopilot: could not set the size");
                next(99);
            } else {
                ClickWidget("Size:", true);
                at = now - 600;
            }
        }
        break;
    case 2:  // the editor opens (loading screen, then the mission scene)
        if (editor::Active() && scene == ".?AVSGame@@" && dt > 4000) {
            render::SetFakeCursor(true, {frameW / 2, frameH / 2});
            Save(tag("n1-editor-new", "e2-editor-open", "0-editor-open"));
            LogView();
            // The base cell: the one at 30% / 45% of the frame (the panel is top right).
            uintptr_t g = ((uintptr_t*)game::Scenes)[0];
            int f = *(short*)(g + game::SGameZoomedOut) == 1 ? 2 : 1;
            bx = (*(short*)(g + game::SGameCameraX) + frameW * 30 / 100 * f) / 64;
            by = (*(short*)(g + game::SGameCameraY) + frameH * 45 / 100 * f) / 64;
            g_h.log("autopilot: editor open: %s; base cell %d,%d", editor::Status().c_str(), bx, by);
            if (things) {
                ClickItem("Roots");
                next(300);
                break;
            }
            if (players) {
                ClickItem("Players");
                next(400);
                break;
            }
            t1 = Choose({"Lava", "Tar", "Crystal", "Sand", "Cropland", "Roots", "Hot Mud", "Crushed Crystal"}, "", 1, bx,
                        by, bx + 5, by);
            // Lava refused here (smart painter): try it anyway, for the message.
            ClickWidget(t1 == "Lava" ? t1.c_str() : "Lava", false);
            next(t1 == "Lava" ? 3 : 21);
        }
        break;
    case 21:  // one click of Lava where it may not go
        if (dt > 600) {
            POINT p;
            if (editor::CellCentre(bx, by, p)) {
                render::SetFakeCursor(true, p);
                Post(WM_MOUSEMOVE, 0, p);
                Post(WM_LBUTTONDOWN, MK_LBUTTON, p);
                Post(WM_LBUTTONUP, 0, p);
            }
            next(22);
        }
        break;
    case 22:
        if (dt > 800) {
            Save("e2b-refused");
            g_h.log("autopilot: refused Lava: %s", editor::Status().c_str());
            Expect("refused %s", OneWord(editor::Status()).c_str());
            ClickWidget(t1.c_str(), false);
            next(3);
        }
        break;
    case 3:
        if (dt > 600) {
            g_h.log("autopilot: chosen %s", editor::TypeName().c_str());
            Stroke(t1.c_str(), 1, bx, by, bx + 5, by);
            next(fresh ? 9 : quit ? 30 : 4);
        }
        break;
    case 4:
        if (dt > 800) {
            Save("e3-stroke1");
            t2 = Choose({"Sand", "Cropland", "Crystal", "Tar", "Roots", "Hot Mud", "Crushed Crystal", "Lava"}, t1, 3,
                        bx + 2, by + 2, bx + 2, by + 5);
            ClickWidget(t2.c_str(), false);
            next(5);
        }
        break;
    case 5:
        if (dt > 500) {
            ClickWidget("3", false);  // brush 3
            next(6);
        }
        break;
    case 6:
        if (dt > 500) {
            Stroke(t2.c_str(), 3, bx + 2, by + 2, bx + 2, by + 5);
            next(7);
        }
        break;
    case 7:
        if (dt > 800) {
            Save("e4-stroke2");
            t3 = Choose({"Crystal", "Cropland", "Sand", "Tar", "Roots", "Hot Mud", "Crushed Crystal", "Lava"}, t2, 1,
                        bx + 8, by + 1, bx + 10, by + 1);
            ClickWidget(t3.c_str(), false);
            next(70);
        }
        break;
    case 70:
        if (dt > 500) {
            ClickWidget("1", false);  // brush 1
            next(71);
        }
        break;
    case 71:
        if (dt > 500) {
            Stroke(t3.c_str(), 1, bx + 8, by + 1, bx + 10, by + 1);
            next(8);
        }
        break;
    case 8:
        if (dt > 800) {
            Save("e5-stroke3");
            PostKey(WM_KEYDOWN, VK_CONTROL);
            PostKey(WM_KEYDOWN, 'Z');
            PostKey(WM_KEYUP, 'Z');
            PostKey(WM_KEYUP, VK_CONTROL);
            Expect("undo");
            g_h.log("autopilot: Ctrl+Z");
            next(80);
        }
        break;
    case 80:
        if (dt > 800) {
            Save("e6-undone");
            g_h.log("autopilot: after undo: %s", editor::Status().c_str());
            POINT p;
            if (editor::CellCentre(bx + 1, by, p)) {  // right-click pick on the first stroke
                ClickWidget(t2.c_str(), false);  // something else first
                render::SetFakeCursor(true, p);
                Post(WM_MOUSEMOVE, 0, p);
                Post(WM_RBUTTONDOWN, MK_RBUTTON, p);
                Post(WM_RBUTTONUP, 0, p);
            }
            next(81);
        }
        break;
    case 81:
        if (dt > 800) {
            g_h.log("autopilot: picked: %s (%s)", editor::TypeName().c_str(), editor::Status().c_str());
            Expect("picked %s", OneWord(editor::TypeName()).c_str());
            next(9);
        }
        break;
    // --- target 3: things and units (Stage 2) ---------------------------------------
    case 300:
        if (dt > 500) {
            ClickItem("5");
            next(350);
        }
        break;
    case 350:  // the brush ghost: the cursor over the map, no button
        if (dt > 500) {
            POINT p;
            if (editor::CellCentre(bx + 2, by - 5, p)) {
                render::SetFakeCursor(true, p);
                Post(WM_MOUSEMOVE, 0, p);
            }
            next(351);
        }
        break;
    case 351:  // (a screenshot is of the next frame: nothing else is posted until it is taken)
        if (dt > 500) {
            Save("t0b-brush-ghost");
            Expect("x ghost-brush %d %d %s", bx + 2, by - 5, editor::GhostInfo().c_str());
            next(355);
        }
        break;
    case 355:
        if (dt > 300) {
            ClickItem("Sand");
            next(352);
        }
        break;
    case 352:  // a Shift+drag fill, captured mid-drag (Sand on sand: no change)
        if (dt > 500) {
            POINT a, b;
            if (editor::CellCentre(bx - 3, by - 6, a) && editor::CellCentre(bx + 1, by - 4, b)) {
                PostKey(WM_KEYDOWN, VK_SHIFT);
                render::SetFakeCursor(true, a);
                Post(WM_MOUSEMOVE, MK_SHIFT, a);
                Post(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, a);
                Post(WM_MOUSEMOVE, MK_LBUTTON | MK_SHIFT, b);
                render::SetFakeCursor(true, b);
                g_shiftDragTo = b;
            }
            next(353);
        }
        break;
    case 353:
        if (dt > 500) {
            Save("t0c-fill-ghost");
            Expect("x ghost-fill %d %d %d %d %s", bx - 3, by - 6, bx + 1, by - 4, editor::GhostInfo().c_str());
            Post(WM_LBUTTONUP, MK_SHIFT, g_shiftDragTo);
            PostKey(WM_KEYUP, VK_SHIFT);
            g_h.log("autopilot: fill ghost: %s", editor::Status().c_str());
            next(354);
        }
        break;
    case 354:
        if (dt > 500) {
            ClickItem("Roots");
            next(301);
        }
        break;
    case 301:
        if (dt > 500) {
            std::string why;
            if (!editor::StrokeWorks("Roots", 5, bx, by, bx + 4, by, &why)) g_h.log("autopilot: Roots refused: %s", why.c_str());
            Stroke("Roots", 5, bx, by, bx + 4, by);
            next(302);
        }
        break;
    case 302:
        if (dt > 800) {
            Save("t1-roots");
            next(370);
        }
        break;
    // The panel folds away (Tab) and the tool stays: paint where it was, open it
    // with the strip's button, then Tab twice more.
    case 370:
        if (dt > 300) {
            uintptr_t g = ((uintptr_t*)game::Scenes)[0];
            int f = *(short*)(g + game::SGameZoomedOut) == 1 ? 2 : 1;
            hx = (*(short*)(g + game::SGameCameraX) + (frameW - 150) * f) / 64;
            hy = (*(short*)(g + game::SGameCameraY) + 180 * f) / 64;
            POINT p;
            Expect("x hidden-expanded %d", editor::CellCentre(hx, hy, p) && editor::Hidden(p) ? 1 : 0);
            Expect("x behind-cell %d %d", hx, hy);
            PostKey(WM_KEYDOWN, VK_TAB);
            PostKey(WM_KEYUP, VK_TAB);
            g_h.log("autopilot: Tab (cell %d,%d behind the panel)", hx, hy);
            next(371);
        }
        break;
    case 371:
        if (dt > 600) {
            POINT p;
            Expect("x collapsed %d", editor::Collapsed() ? 1 : 0);
            Expect("x hidden-collapsed %d", editor::CellCentre(hx, hy, p) && editor::Hidden(p) ? 1 : 0);
            Save("t1b-collapsed");
            std::string why;
            if (!editor::StrokeWorks("Roots", 5, hx, hy, hx, hy + 1, &why)) g_h.log("autopilot: Roots refused there: %s", why.c_str());
            Stroke("Roots", 5, hx, hy, hx, hy + 1);
            next(372);
        }
        break;
    case 372:
        if (dt > 800) {
            Save("t1c-painted-behind");
            g_h.log("autopilot: painted behind the panel: %s", editor::Status().c_str());
            ClickItem("Show (Tab)");
            next(373);
        }
        break;
    case 373:
        if (dt > 600) {
            Expect("x expanded-by-click %d", editor::Collapsed() ? 0 : 1);
            Save("t1d-expanded");
            PostKey(WM_KEYDOWN, VK_TAB);
            PostKey(WM_KEYUP, VK_TAB);
            next(374);
        }
        break;
    case 374:
        if (dt > 600) {
            Expect("x collapsed-by-tab %d", editor::Collapsed() ? 1 : 0);
            PostKey(WM_KEYDOWN, VK_TAB);
            PostKey(WM_KEYUP, VK_TAB);
            next(375);
        }
        break;
    case 375:
        if (dt > 600) {
            Expect("x expanded-by-tab %d", editor::Collapsed() ? 0 : 1);
            ClickItem("Cropland");
            next(303);
        }
        break;
    case 303:
        if (dt > 500) {
            ClickItem("3");
            next(304);
        }
        break;
    case 304:
        if (dt > 500) {
            std::string why;
            if (!editor::StrokeWorks("Cropland", 3, bx + 1, by, bx + 3, by, &why))
                g_h.log("autopilot: Cropland refused: %s", why.c_str());
            Stroke("Cropland", 3, bx + 1, by, bx + 3, by);
            next(305);
        }
        break;
    case 305:
        if (dt > 800) {
            Save("t2-cropland");
            ClickItem("Things");
            next(306);
        }
        break;
    case 306:
        if (dt > 600) {
            ClickItem(editor::ThingName(0x46));  // Sand Tree
            next(307);
        }
        break;
    case 307:  // the footprint ghost at the cursor, then the click
        if (dt > 500) {
            static int cx, cy;
            POINT p;
            if (editor::FindCell(editor::kCellRoots, bx - 2, by, cx, cy) && editor::CellCentre(cx, cy, p)) {
                render::SetFakeCursor(true, p);
                Post(WM_MOUSEMOVE, 0, p);
                rx = cx, ry = cy;
            } else {
                g_h.log("autopilot: no roots cell for the tree");
            }
            next(360);
        }
        break;
    case 360:
        if (dt > 500) {
            Save("t2b-thing-ghost");
            Expect("x ghost-thing %d %d %s", rx, ry, editor::GhostInfo().c_str());
            next(361);
        }
        break;
    case 361:
        if (dt > 300) {
            if (ClickCell(rx, ry)) Expect("x tree-cell %d %d", rx, ry);
            next(308);
        }
        break;
    case 308:
        if (dt > 600) {
            g_h.log("autopilot: after the tree: %s", editor::Status().c_str());
            Expect("x count 70 %d", editor::CountThings(0x46, -1));
            ClickItem(editor::ThingName(0x43));  // Yedda Ruins
            next(309);
        }
        break;
    case 309:
        if (dt > 500) {
            int cx, cy;
            if (ClickFreeCell(editor::kCellOpen, bx + 9, by + 3, 128, cx, cy)) Expect("x ruins-cell %d %d", cx, cy);
            next(310);
        }
        break;
    case 310:
        if (dt > 600) {
            g_h.log("autopilot: after the ruins: %s", editor::Status().c_str());
            Expect("x count 67 %d", editor::CountThings(0x43, -1));
            Save("t3-tree-ruins");
            ClickItem("Fill Pods");
            next(311);
        }
        break;
    case 311:
        if (dt > 800) {
            Expect("x pods %d", editor::CountThings(0x3f, -1));
            g_h.log("autopilot: after Fill Pods: %s", editor::Status().c_str());
            Save("t4-pods");
            POINT p;
            if (editor::ThingPoint(0x3f, -1, p)) ClickMap(p, true);
            next(312);
        }
        break;
    case 312:
        if (dt > 600) {
            Expect("x pods-removed %d", editor::CountThings(0x3f, -1));
            CtrlKey('Z');
            next(313);
        }
        break;
    case 313:
        if (dt > 600) {
            Expect("x pods-undone %d", editor::CountThings(0x3f, -1));
            ClickItem("Units");
            next(314);
        }
        break;
    case 314:
        if (dt > 500) {
            ClickItem("Devices");
            next(315);
        }
        break;
    case 315:
        if (dt > 500) {
            ClickItem(editor::ThingName(0x2c));  // Refinery
            next(316);
        }
        break;
    case 316:
        if (dt > 500) {
            if (ClickFreeCell(editor::kCellOpen, bx + 2, by + 3, 40, rx, ry)) Expect("x refinery-cell %d %d", rx, ry);
            next(317);
        }
        break;
    case 317:
        if (dt > 600) {
            g_h.log("autopilot: after the Refinery: %s", editor::Status().c_str());
            Expect("x count 44 %d", editor::CountThings(0x2c, 0));
            ClickItem("Next >");
            next(318);
        }
        break;
    case 318:
        if (dt > 500) {
            ClickItem(editor::ThingName(0x31));  // Turret
            next(319);
        }
        break;
    case 319:
        if (dt > 500) {
            int cx, cy;
            if (ClickFreeCell(editor::kCellOpen, rx + 2, ry + 1, 40, cx, cy)) Expect("x turret-near %d %d", cx, cy);
            next(320);
        }
        break;
    case 320:
        if (dt > 600) {
            int cx, cy;
            if (ClickFreeCell(editor::kCellOpen, bx + 16, by + 8, 128, cx, cy)) Expect("x turret-far %d %d", cx, cy);
            next(321);
        }
        break;
    case 321:
        if (dt > 600) {
            Expect("x count 49 %d", editor::CountThings(0x31, 0));
            Save("t5-devices");
            ClickItem("Creatures");
            next(322);
        }
        break;
    case 322:
        if (dt > 500) {
            ClickItem(editor::ThingName(2));  // Raptor
            next(323);
        }
        break;
    case 323:
        if (dt > 500) {
            int cx, cy;
            ClickFreeCell(editor::kCellOpen, bx - 6, by + 5, 128, cx, cy);
            next(324);
        }
        break;
    case 324:
        if (dt > 600) {
            Expect("x count 2 %d", editor::CountThings(2, 0));
            ClickItem(editor::ThingName(1));  // Scout
            next(325);
        }
        break;
    case 325:
        if (dt > 500) {
            int cx, cy;
            ClickFreeCell(editor::kCellOpen, bx - 9, by + 1, 128, cx, cy);
            next(326);
        }
        break;
    case 326:
        if (dt > 600) {
            Expect("x count 1 %d", editor::CountThings(1, 0));
            ClickItem("Select");
            next(327);
        }
        break;
    case 327:
        if (dt > 500) {
            POINT p;
            if (editor::ThingPoint(2, 0, p)) ClickMap(p);
            next(328);
        }
        break;
    case 328:
        if (dt > 600) {
            Expect("x raptor-before %s", editor::Selected().c_str());
            Save("t6-selected");
            ClickItem("-");  // hit points down a tenth
            next(329);
        }
        break;
    case 329:
        if (dt > 500) {
            ClickItem("Rank: Rookie");
            next(330);
        }
        break;
    case 330:
        if (dt > 500) {
            ClickRow(5, 0, "first gizmo slot");
            next(331);
        }
        break;
    case 331:
        if (dt > 600) {
            Expect("x raptor-after %s", editor::Selected().c_str());
            g_h.log("autopilot: raptor after: %s (%s)", editor::Selected().c_str(), editor::Status().c_str());
            Save("t7-attributes");
            POINT a, b;
            int cx, cy;
            if (editor::ThingPoint(2, 0, a) && editor::FindCell(editor::kCellOpen, bx + 8, by + 7, cx, cy) &&
                editor::CellCentre(cx, cy, b)) {
                Expect("x move-to %d %d", cx, cy);
                DragMap(a, b);
            }
            next(332);
        }
        break;
    case 332:
        if (dt > 800) {
            Expect("x raptor-moved %s", editor::Selected().c_str());
            g_h.log("autopilot: after the move: %s", editor::Status().c_str());
            Save("t8-moved");
            POINT p;
            if (editor::ThingPoint(1, 0, p)) ClickMap(p);  // the Scout
            next(333);
        }
        break;
    case 333:
        if (dt > 600) {
            ClickItem("Delete");
            next(334);
        }
        break;
    case 334:
        if (dt > 600) {
            Expect("x scout-deleted %d", editor::CountThings(1, 0));
            CtrlKey('Z');
            next(335);
        }
        break;
    case 335:
        if (dt > 600) {
            Expect("x scout-undone %d", editor::CountThings(1, 0));
            CtrlKey('Y');
            next(336);
        }
        break;
    case 336:
        if (dt > 600) {
            Expect("x scout-redone %d", editor::CountThings(1, 0));
            ClickItem("Check");
            next(337);
        }
        break;
    case 337:
        if (dt > 800) {
            Save("t9-check");
            ExpectFindings();
            next(9);
        }
        break;

    // --- target 4: players and map settings (Stage 2) -----------------------------------
    case 400:
        if (dt > 600) {
            ClickItem(">");
            next(401);
        }
        break;
    case 401:
        if (dt > 500) {
            Expect("x p2-before %s", editor::PlayerInfo(1).c_str());
            ClickRow(1, 0, "type");  // Computer -> Human
            next(402);
        }
        break;
    case 402:
        if (dt > 600) {
            Expect("x p2-human %s", editor::PlayerInfo(1).c_str());
            ClickRow(1, 0, "type");  // Human -> Either
            next(403);
        }
        break;
    case 403:
        if (dt > 600) {
            ClickRow(2, 0, "race");
            next(404);
        }
        break;
    case 404:
        if (dt > 600) {
            ClickRow(6, 0, "Tanu CGK");
            next(405);
        }
        break;
    case 405:
        if (dt > 500) {
            int race = atoi(editor::PlayerInfo(1).c_str() + 2);
            int clan = (race + 1) % 4;
            Expect("x avail-clan %d", clan);
            ClickRow(6 + clan, 1, "available");
            next(406);
        }
        break;
    case 406:
        if (dt > 500) {
            int cx, cy;
            if (ClickFreeCell(editor::kCellOpen, bx, by, 128, cx, cy)) Expect("x p2-start %d %d", cx, cy);
            next(407);
        }
        break;
    case 407:
        if (dt > 600) {
            Expect("x p2-after %s", editor::PlayerInfo(1).c_str());
            int race = atoi(editor::PlayerInfo(1).c_str() + 2);
            Expect("x p2-race-shamans %d", editor::CountThings(race * 9, 1));
            Save("p1-player2");
            for (int i = 0; i < 4; ++i) ClickItem(">");  // player 6
            next(408);
        }
        break;
    case 408:
        if (dt > 600) {
            Expect("x p6-before %s things %d", editor::PlayerInfo(5).c_str(), editor::CountThings(-1, 5));
            for (int i = 0; i < 3; ++i) ClickRow(1, 0, "type");  // Computer -> Human -> Either -> None
            next(409);
        }
        break;
    case 409:
        if (dt > 800) {
            Expect("x p6-after %s things %d", editor::PlayerInfo(5).c_str(), editor::CountThings(-1, 5));
            Save("p2-player6-none");
            ClickItem("Map");
            next(410);
        }
        break;
    case 410:
        if (dt > 500) {
            ClickRow(0, 0, "name");
            next(411);
        }
        break;
    case 411:
        if (dt > 500) {
            for (int i = 0; i < 12; ++i) {
                PostKey(WM_KEYDOWN, VK_BACK);
                PostKey(WM_KEYUP, VK_BACK);
            }
            TypeText("HD Test Map");
            next(412);
        }
        break;
    case 412:
        if (dt > 600) {
            ClickRow(2, 0, "fog");
            next(413);
        }
        break;
    case 413:
        if (dt > 500) {
            ClickRow(3, 0, "supported -");
            next(414);
        }
        break;
    case 414:
        if (dt > 500) {
            Expect("x supported-once %s", editor::MapInfoText().c_str());
            ClickRow(3, 0, "supported -");  // refused: player 5 exists
            next(415);
        }
        break;
    case 415:
        if (dt > 500) {
            Expect("x supported-refused %s", OneWord(editor::Status()).c_str());
            ClickRow(4, 0, "recommended -");
            next(416);
        }
        break;
    case 416:
        if (dt > 500) {
            Expect("x recommended %s", editor::MapInfoText().c_str());
            CtrlKey('Z');
            next(417);
        }
        break;
    case 417:
        if (dt > 500) {
            Expect("x recommended-undone %s", editor::MapInfoText().c_str());
            CtrlKey('Y');
            next(418);
        }
        break;
    case 418:
        if (dt > 600) {
            Expect("x map %s", editor::MapInfoText().c_str());
            Save("p3-map");
            int wx, wy;
            if (editor::ThingWorld(-2, 2, wx, wy)) editor::LookAt(wx, wy);  // player 3's Shaman
            ClickItem("Units");
            next(419);
        }
        break;
    case 419:
        if (dt > 600) {
            ClickItem("Select");
            next(420);
        }
        break;
    case 420:
        if (dt > 600) {
            POINT p;
            if (editor::ThingPoint(-2, 2, p)) ClickMap(p);
            next(421);
        }
        break;
    case 421:
        if (dt > 600) {
            Save("p4-shaman-selected");
            g_h.log("autopilot: selected %s", editor::Selected().c_str());
            ClickItem("Delete");
            next(422);
        }
        break;
    case 422:
        if (dt > 600) {
            Expect("x p3-things %d", editor::CountThings(-1, 2));
            ClickItem("Check");
            next(423);
        }
        break;
    case 423:
        if (dt > 800) {
            Save("p5-check");
            ExpectFindings();
            next(9);
        }
        break;
    case 30:  // Exit with unsaved changes: asks once
        if (dt > 800) {
            ClickWidget("Exit", false);
            next(31);
        }
        break;
    case 31:
        if (dt > 800) {
            Save("x1-exit-asks");
            g_h.log("autopilot: after Exit: %s", editor::Status().c_str());
            ClickWidget("Exit", false);
            next(32);
        }
        break;
    case 32:
        if (scene == ".?AVSHeadquarters@@" && !editor::Active() && g_h.topScene && strstr(g_h.topScene(), "SMissionMenu") &&
            now - g_sceneStart > 2500) {
            long x, y;
            g_h.log("autopilot: back in the menu; map list %s", menu::FindWidget("New Map", x, y) && x > 0 ? "shown" : "NOT shown");
            Save("x2-back-to-list");
            next(99);
        } else if (dt > 30000) {
            next(99);
        }
        break;
    case 9:
        if (dt > 800) {
            if (fresh) Save("n2-stroke");
            ClickWidget("Save As", false);
            next(10);
        }
        break;
    case 10:
        if (dt > 600) {
            Save(tag("n3-name-prompt", "e7-name-prompt", "20-name-prompt"));
            TypeName(name);
            next(11);
        }
        break;
    case 11:  // processing and saving
        if (editor::SaveCount() > 0 && editor::Idle() && dt > 500) {
            Save(tag("n4-saved", "e8-saved", "21-saved"));
            g_h.log("autopilot: saved: %s", editor::Status().c_str());
            Expect("saved %s", name);
            if (g_expect) fclose(g_expect), g_expect = nullptr;
            next(110);  // zoom out with the wheel: the panel keeps its size and place
        } else if (dt > 60000) {
            g_h.log("autopilot: no save after 60 s (%s)", editor::Status().c_str());
            next(99);
        }
        break;
    case 110:
        // Mid-animation: the frame grows every frame, and the panel (box and
        // texts) must stay together in the bottom-left corner.
        {
            static bool notch[6], shot[3];
            static int lateBefore;
            if (dt == 0) {
                for (bool& b : notch) b = false;
                for (bool& b : shot) b = false;
                lateBefore = editor::LateMoves();
            }
            for (int i = 0; i < 6; ++i)
                if (!notch[i] && dt > 60u + 90u * i) {  // a notch every few frames, as a player wheels
                    notch[i] = true;
                    Post(WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), {frameW / 2, frameH / 2});
                }
            for (int i = 0; i < 3; ++i)
                if (!shot[i] && dt > 150u + 180u * i) {
                    shot[i] = true;
                    char zt[32];
                    snprintf(zt, sizeof zt, "%s-zooming%d", tag("n4a", "e8a", "22"), i);
                    Save(zt);
                }
            if (dt > 2000)
                g_h.log("autopilot: wheel zoom: panel items re-placed at draw time %d times",
                        editor::LateMoves() - lateBefore);
        }
        if (dt > 2000) {
            Save(tag("n4b-zoomed-out", "e8b-zoomed-out", "23-zoomed-out"));
            LogView();
            for (int i = 0; i < 4; ++i) g_h.zoomStep(-1);
            next(111);
        }
        break;
    case 111:  // Test Map asks first
        if (dt > 2000) {
            ClickWidget("Test Map", false);
            next(112);
        }
        break;
    case 112:
        if (dt > 800) {
            Save(tag("n4c-test-asks", "e8c-test-asks", "24-test-asks"));
            g_h.log("autopilot: after Test Map: %s (test stage %d)", editor::Status().c_str(), editor::TestStage());
            ClickWidget("Test Map", false);  // "Test Map?"
            next(12);
        }
        break;
    case 12:  // Test Map: the hub, the skirmish setup, then the skirmish
        if (scene == ".?AVSMultiSetup@@" && !strcmp(g_h.topScene(), ".?AVSMultiSetup@@") && now - g_sceneStart > 2600 &&
            !(clicks & 0x100)) {
            Save(tag("n5-skirmish-setup", "e9-skirmish-setup", "25-skirmish-setup"));
            clicks |= 0x100;
        }
        if (scene == ".?AVSGame@@" && !editor::Active() && ((short (*)())0x46f960)() == 1 && now - g_sceneStart > 5000) {
            render::SetFakeCursor(true, {frameW / 2, frameH / 2});
            Save(tag("n6-test-skirmish", "e10-test-skirmish", "26-test-skirmish"));
            LogView();
            char* w = *(char**)0x579278;
            int count = 0;
            for (int i = 0; w && i < *(int*)(w + 0x14); ++i) count += (*(char***)(w + 0x10))[i] != nullptr;
            g_h.log("autopilot: Test Map skirmish running: map tiles %dx%d, %d things, turn period %d, paused %d",
                    game::at<uint8_t>(game::MapTilesW), game::at<uint8_t>(game::MapTilesH), count,
                    *(int*)(0x567ef8 + 0x188), *(int*)(0x567ef8 + 0x460));
            std::string types;
            for (int t = 0; t < 0x4a; ++t) {
                int n = 0;
                for (int i = 0; w && i < *(int*)(w + 0x14); ++i)
                    if (char* th = (*(char***)(w + 0x10))[i]) n += *(int*)(th + 0x18) == t;
                if (n) types += " " + std::to_string(t) + "=" + std::to_string(n);
            }
            g_h.log("autopilot: skirmish things by type:%s", types.c_str());
            next(99);
        } else if (dt > 60000) {
            g_h.log("autopilot: the Test Map skirmish did not start");
            next(99);
        }
        break;
    case 99:
        if (dt > 500) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
            next(100);
        }
        break;
    }
}

}  // namespace

void Init(const Hooks& h) {
    g_h = h;
    g_h.log("autopilot: enabled");
}

void Save(const char* tag) {
    std::string base = std::string("auto-") + tag;
    render::d3d11::Screenshot(g_h.dir + base + "-screen.bmp");
    diag::SaveFrameAs((base + ".bmp").c_str());
    g_h.log("autopilot: saved %s, game cursor %d,%d (last click %ld,%ld)", base.c_str(), game::at<short>(game::CursorX),
            game::at<short>(game::CursorY), g_clickAt.x, g_clickAt.y);
}

void MovieTick(double t) {
    // Skip every cutscene (Space) so the tests reach the menus; mode 5
    // captures two frames of each first.
    static double last = 1e9;
    static int stage, movies;
    if (t < last - 0.5) {
        stage = 0;
        ++movies;
    }
    last = t;
    if (g_h.mode == 5) {
        if (stage == 0 && t > 2) {
            Save(("movie" + std::to_string(movies) + "-2s").c_str());
            ++stage;
        } else if (stage == 1 && t > 5) {
            Save(("movie" + std::to_string(movies) + "-5s").c_str());
            ++stage;
        } else if (stage == 2 && t > 6 && movies > 1) {  // the first plays to its end
            Key(VK_SPACE);
            ++stage;
        }
    } else if (stage == 0 && t > 1) {
        Key(VK_SPACE);
        ++stage;
    }
}

void Tick(const std::string& scene, int frameW, int frameH) {
    DWORD now = GetTickCount();
    if (g_buttonDown && now >= g_releaseAt) {
        Post(WM_LBUTTONUP, 0, g_clickAt);
        g_buttonDown = false;
    }
    if (g_shiftDragUpAt && now >= g_shiftDragUpAt) {
        Post(WM_LBUTTONUP, MK_SHIFT, g_shiftDragTo);
        PostMessageA(render::GameWindow(), WM_KEYUP, VK_SHIFT, 0xC0000001);
        g_shiftDragUpAt = 0;
    }
    if (scene != g_scene) {
        g_scene = scene;
        g_sceneStart = now;
        g_step = 0;
    }
    DWORD t = now - g_sceneStart;
    // Loading screens are short: capture each as soon as it's drawn.
    if (scene == ".?AVSProgress@@") {
        static int loads;
        static DWORD savedAt;
        if (g_sceneStart != savedAt && t > 60) {
            savedAt = g_sceneStart;
            Save(("progress-" + std::to_string(++loads)).c_str());
        }
    }
    if (g_gliding) {
        double f = std::min(1.0, (now - g_glideStart) / (double)kGlideMs);
        POINT p{(LONG)(g_glideFrom.x + (g_glideTo.x - g_glideFrom.x) * f),
                (LONG)(g_glideFrom.y + (g_glideTo.y - g_glideFrom.y) * f)};
        render::SetFakeCursor(true, p);
        Post(WM_MOUSEMOVE, 0, p);
        g_gliding = f < 1.0;
    }
    if (g_h.mode == 6 && scene == ".?AVSGame@@") {
        // Game Options dialog: Esc, click "Game Options", capture, quit.
        static int stage;
        static DWORD at;
        long x, y;
        if (stage == 0 && t > 3000) {
            Key(VK_ESCAPE);
            stage = 1;
            at = now;
        } else if (stage == 1 && now - at > 1500) {
            // The Esc menu is drawn at the interface scale (2x, centred): its
            // "Game Options" button sits at (-130, -54) from the window centre.
            RECT rc;
            GetClientRect(render::GameWindow(), &rc);
            POINT c{rc.right / 2 - 130, rc.bottom / 2 - 54};
            render::SetFakeCursorClient(c);
            PostMessageA(render::GameWindow(), WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
            PostMessageA(render::GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
            PostMessageA(render::GameWindow(), WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
            g_h.log("autopilot: click client %ld,%ld (Game Options)", c.x, c.y);
            (void)x;
            (void)y;
            stage = 2;
            at = now;
        } else if (stage == 2 && now - at > 2000) {
            Save("game-options");
            stage = 3;
            at = now;
        } else if (stage == 3 && now - at > 800) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
            stage = 4;
        }
        return;
    }
    if (g_h.mode == 7) {  // open scene id AutopilotTarget from the main menu, then quit
        static int stage;
        if (stage == 0 && scene == ".?AVSHeadquarters@@" && t > 2500) {
            typedef void(__cdecl * Queue_t)(int type, int id, int delay);
            ((Queue_t)0x4709a0)(0x22, g_h.target, 5);  // replace the top scene
            g_h.log("autopilot: opening scene id %#x", g_h.target);
            render::SetFakeCursor(true, {639, 479});
            stage = 1;
        } else if (stage == 1 && scene != ".?AVSHeadquarters@@") {
            stage = 2;
        } else if (stage >= 2 && ++stage > 400) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
            stage = -1000000;
        }
        return;
    }
    if (g_h.mode == 11) {
        // Small-map zoom test: Campaign -> Basic Training -> mission AutopilotTarget (0 Combat,
        // 1 Base Building, 2 Gene Collecting), accept, then zoom all the way out and capture.
        // Then selection far from the frame's top-left (where the halved view used to end, see
        // camera::InstallViewFix): click an own unit there, right-click to deselect, drag a box
        // round it; the selection count is logged after each.
        static int stage;
        static DWORD at;
        if (stage == 0 && scene == ".?AVSHeadquarters@@" && t > 2500) {
            Click({60, 54});  // Campaign
            stage = 1;
            at = now;
        } else if (stage == 1 && now - at > 3000) {
            Click({60, 26});  // Basic Training
            stage = 2;
            at = now;
        } else if (stage == 2 && now - at > 3000) {
            Click({75, (LONG)(24 + 28 * g_h.target)});  // the training mission
            stage = 3;
            at = now;
        } else if (stage == 3 && now - at > 1500) {
            Click({55, 438});  // Accept
            stage = 4;
            at = now;
        } else if (stage == 4 && scene == ".?AVSGame@@") {
            stage = 5;
            at = now;
        } else if (stage == 5 && now - at > 8000) {
            Save("bb-start");
            for (int i = 0; i < 12; ++i) g_h.zoomStep(+1);  // all the way out
            stage = 6;
            at = now;
        } else if (stage == 6 && now - at > 3000) {
            Save("bb-zoomed-out");
            LogView();
            // Selection in the far part of the frame (the area that used to be black): the own
            // unit furthest right/down in the frame, clicked, deselected, then drag-boxed.
            g_target = FarOwnUnit(frameW, frameH);
            if (g_target.x < 0) {
                g_h.log("autopilot: no own unit in view; skipping the selection test");
                stage = 20;
            } else {
                g_glideFrom = {game::at<short>(game::CursorX), game::at<short>(game::CursorY)};
                g_glideTo = g_target;
                g_glideStart = now;
                g_gliding = true;
                stage = 7;
            }
            at = now;
        } else if (stage == 7 && now - at > 1300) {
            Click(g_target);
            stage = 8;
            at = now;
        } else if (stage == 8 && now - at > 800) {
            g_h.log("autopilot: after the click on %ld,%ld: selected %d", g_target.x, g_target.y, SelectedCount());
            Save("bb-click");
            Post(WM_RBUTTONDOWN, MK_RBUTTON, g_target);  // classic scheme: right-up deselects
            Post(WM_RBUTTONUP, 0, g_target);
            stage = 9;
            at = now;
        } else if (stage == 9 && now - at > 800) {
            g_h.log("autopilot: after the right-click: selected %d", SelectedCount());
            POINT a{g_target.x - 90, g_target.y - 90}, b{g_target.x + 90, g_target.y + 90};
            render::SetFakeCursor(true, a);
            Post(WM_MOUSEMOVE, 0, a);
            Post(WM_LBUTTONDOWN, MK_LBUTTON, a);
            for (int i = 1; i <= 8; ++i)
                Post(WM_MOUSEMOVE, MK_LBUTTON, {a.x + (b.x - a.x) * i / 8, a.y + (b.y - a.y) * i / 8});
            render::SetFakeCursor(true, b);
            g_dragTo = b;
            g_h.log("autopilot: drag %ld,%ld -> %ld,%ld", a.x, a.y, b.x, b.y);
            stage = 10;
            at = now;
        } else if (stage == 10 && now - at > 300) {
            Save("bb-drag-box");
            Post(WM_LBUTTONUP, 0, g_dragTo);
            stage = 11;
            at = now;
        } else if (stage == 11 && now - at > 800) {
            g_h.log("autopilot: after the drag box: selected %d", SelectedCount());
            Save("bb-drag");
            stage = 20;
            at = now;
        } else if (stage == 20 && now - at > 500) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
            stage = 21;
        }
        return;
    }
    if (g_h.mode == 13) {
        EditorTest(scene, frameW, frameH, now);
        return;
    }
    if (g_h.mode == 5) {  // cutscene test: done once the main menu is up
        if (scene == ".?AVSHeadquarters@@" && t > 1000) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
        }
        return;
    }
    if (g_h.mode == 4) {
        // Open one main-menu item, let its screen settle, park the cursor off
        // the frame and save it as auto-screen-<TopScene>.
        static int stage;
        static DWORD at;
        // Targets 0-6: main-menu items. 7: Campaign, then its first mission
        // (Basic Training), for the mission briefing.
        if (stage == 0 && scene == ".?AVSHeadquarters@@" && t > 2500) {
            int item = g_h.target == 7 ? 1 : g_h.target;
            Click({60, (LONG)(26 + 28 * item)});
            stage = g_h.target == 7 ? 10 : 1;
            at = now;
        } else if (stage == 10 && now - at > 3000) {
            Click({60, 26});
            stage = 1;
            at = now;
        } else if (stage == 1 && now - at > 4000) {
            render::SetFakeCursor(true, {639, 479});
            Post(WM_MOUSEMOVE, 0, {639, 479});
            if (g_h.bare) menu::HideAllWidgets();
            stage = 2;
        } else if (stage == 2 && now - at > 5000) {
            std::string top = g_h.topScene ? g_h.topScene() : "unknown";
            if (top.size() > 6 && top.compare(0, 4, ".?AV") == 0) top = top.substr(4, top.size() - 6);
            Save(("screen-" + top).c_str());
            stage = 3;
        } else if (stage == 3 && now - at > 5500) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
            stage = 4;
        }
        return;
    }
    if (scene == ".?AVSHeadquarters@@" && g_h.mode == 3) {
        // Bare main-menu background (no text, cursor parked off-frame).
        // AutopilotTarget=1: with the mouse over the top-right picture hotspot (lit).
        if (g_step == 0 && t > 2500) {
            POINT at = g_h.target == 1 ? POINT{590, 100} : POINT{639, 479};
            render::SetFakeCursor(true, at);
            Post(WM_MOUSEMOVE, 0, at);
            menu::HideAllText();
            ++g_step;
        } else if (g_step == 1 && t > 3500) {
            Save(g_h.target == 1 ? "menu-background-lit" : "menu-background");
            ++g_step;
        } else if (g_step == 2 && t > 4000) {
            g_h.log("autopilot: done, quitting");
            PostQuitMessage(0);
        }
    } else if (scene == ".?AVSHeadquarters@@" && g_h.mode == 2) {
        // Settings page: open it, change two settings, go back.
        enum Kind { SaveShot, LeftClick, Hover, RightClick, Quit, WheelDown, WheelUp, ClickText };
        struct MenuStep {
            DWORD at;
            Kind kind;
            POINT p;
            const char* tag;
        };
        // Settings page: tabs on row 0 (Video x 18, Controls x 150, Interface x 312),
        // settings from row 2 (y 17 + 28 * row), Reset on row 13, Back on row 14.
        static const MenuStep kMenu[] = {
            {2500, SaveShot, {}, "m0-main"},     {3000, LeftClick, {60, 222}, nullptr},   // Settings
            {4000, SaveShot, {}, "m1-video"},    {4300, LeftClick, {60, 194}, nullptr},   // FPS Counter (row 6)
            {5000, SaveShot, {}, "m2-fps"},      {5300, LeftClick, {200, 26}, nullptr},   // Controls tab
            {6000, SaveShot, {}, "m3-controls"}, {6300, ClickText, {}, "Interface"},      // Interface tab
            {7000, SaveShot, {}, "m4-interface"}, {7300, LeftClick, {60, 82}, nullptr},   // Menu Art -> Original
            {8000, SaveShot, {}, "m5-original"}, {8200, Hover, {60, 82}, nullptr},
            {8500, RightClick, {60, 82}, nullptr},                                      // Menu Art back
            {9200, SaveShot, {}, "m6-widescreen"}, {9500, LeftClick, {60, 404}, nullptr},  // Reset (arm)
            {10000, LeftClick, {60, 404}, nullptr},                                      // Reset (confirm)
            {10700, SaveShot, {}, "m7-reset"},   {11000, LeftClick, {60, 432}, nullptr},  // Back
            {12000, SaveShot, {}, "m8-back"},    {12300, Quit, {}, nullptr},
        };
        // AutopilotTarget=2: a long tab scrolling (run with [Debug] SettingsRows=6).
        static const MenuStep kScroll[] = {
            {2500, LeftClick, {60, 222}, nullptr},                                        // Settings
            {3300, LeftClick, {200, 26}, nullptr},                                        // Controls tab
            {4000, SaveShot, {}, "s0-controls-top"},  {4200, Hover, {150, 150}, nullptr},
            {4400, WheelDown, {150, 150}, nullptr},   {4600, WheelDown, {150, 150}, nullptr},
            {5200, SaveShot, {}, "s1-wheel-down2"},   {5400, WheelDown, {150, 150}, nullptr},
            {5500, WheelDown, {150, 150}, nullptr},   {5600, WheelDown, {150, 150}, nullptr},
            {5700, WheelDown, {150, 150}, nullptr},   {5800, WheelDown, {150, 150}, nullptr},
            {6400, SaveShot, {}, "s2-wheel-bottom"},  {6600, WheelUp, {150, 150}, nullptr},
            {7200, SaveShot, {}, "s3-wheel-up1"},     {7400, LeftClick, {60, 222}, nullptr},  // a setting row
            {8000, SaveShot, {}, "s4-row-clicked"},   {8200, RightClick, {60, 222}, nullptr},
            {8800, Quit, {}, nullptr},
        };
        const MenuStep* script = g_h.target == 2 ? kScroll : kMenu;
        int steps = g_h.target == 2 ? (int)(sizeof kScroll / sizeof kScroll[0]) : (int)(sizeof kMenu / sizeof kMenu[0]);
        if (g_step < steps && t > script[g_step].at) {
            const MenuStep& m = script[g_step++];
            switch (m.kind) {
            case SaveShot: Save(m.tag); break;
            case LeftClick: Click(m.p); break;
            case ClickText: {  // a tab, wherever the font puts it
                long x, y;
                if (menu::FindWidget(m.tag, x, y)) Click({x, y});
                break;
            }
            case Hover: render::SetFakeCursor(true, m.p); Post(WM_MOUSEMOVE, 0, m.p); break;
            case RightClick:
                Post(WM_RBUTTONDOWN, MK_RBUTTON, m.p);
                Post(WM_RBUTTONUP, 0, m.p);
                g_h.log("autopilot: right-click %ld,%ld", m.p.x, m.p.y);
                break;
            case Quit: g_h.log("autopilot: done, quitting"); PostQuitMessage(0); break;
            case WheelDown:
            case WheelUp:
                Post(WM_MOUSEWHEEL, MAKEWPARAM(0, m.kind == WheelUp ? WHEEL_DELTA : -WHEEL_DELTA), m.p);
                g_h.log("autopilot: wheel %s", m.kind == WheelUp ? "up" : "down");
                break;
            }
        }
    } else if (scene == ".?AVSHeadquarters@@") {
        if (g_step == 0 && t > 2500) { Click({87, 77}); ++g_step; }  // Skirmish
    } else if (scene == ".?AVSMultiSetup@@") {
        if (g_step == 0 && t > 2000) { Click({437, 457}); ++g_step; }  // Start
    } else if (scene == ".?AVSGame@@") {
        const Step* script = g_h.mode == 8    ? kControls
                             : g_h.mode == 9  ? kModern
                             : g_h.mode == 10 ? kRecord
                             : g_h.mode == 12 ? kGroup
                             : g_h.mode == 16 ? kTallPanel
                             : g_h.mode == 17 ? kCorpse
                             : g_h.mode == 18 ? kAllCreatures
                             : g_h.mode == 19 ? kFlyers
                                              : kMission;
        int steps = g_h.mode == 8    ? (int)(sizeof kControls / sizeof kControls[0])
                    : g_h.mode == 9  ? (int)(sizeof kModern / sizeof kModern[0])
                    : g_h.mode == 10 ? (int)(sizeof kRecord / sizeof kRecord[0])
                    : g_h.mode == 12 ? (int)(sizeof kGroup / sizeof kGroup[0])
                    : g_h.mode == 16 ? (int)(sizeof kTallPanel / sizeof kTallPanel[0])
                    : g_h.mode == 17 ? (int)(sizeof kCorpse / sizeof kCorpse[0])
                    : g_h.mode == 18 ? (int)(sizeof kAllCreatures / sizeof kAllCreatures[0])
                    : g_h.mode == 19 ? (int)(sizeof kFlyers / sizeof kFlyers[0])
                                     : (int)(sizeof kMission / sizeof kMission[0]);
        if (g_h.mode == 16) {  // test only: the Gene Lab stays powered (no power network was built)
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            for (int i = 0; i < *(int*)(w + 0x14); ++i)
                if (things[i] && *(char**)(things[i] + 0x20) == me &&
                    (*(int*)(things[i] + 0x18) == 0x2d || *(int*)(things[i] + 0x18) == 0x35))
                    *(short*)(things[i] + 0x28) = 0;
        }
        if (g_step >= steps || t <= script[g_step].at) return;
        const Step& st = script[g_step++];
        if (st.act != ReleaseKey && st.act != HoldKey)
            for (int vk : {VK_CONTROL, VK_SHIFT, VK_MENU}) camera::ForceHeld(vk, false);
        POINT centre{frameW / 2, frameH / 2};
        switch (st.act) {
        case SaveShot:
            if (!strncmp(st.tag, "c2", 2)) {  // where can the building/pod go? (thousandths of the frame)
                char* w = *(char**)0x579278;
                char** sel = *(char***)(w + 0x80);
                if (*(int*)(w + 0x84) == 1 && sel[0]) {
                    char* sh = sel[0];
                    short player = *(short*)(*(char**)(sh + 0x20) + 4);
                    int type = *(int*)(sh + 0x188);
                    typedef int(__thiscall * Valid_t)(void*, int, int, int, int, int, int, int);
                    std::string line;
                    uintptr_t g = ((uintptr_t*)game::Scenes)[0];
                    int cx = *(short*)(g + game::SGameCameraX), cy = *(short*)(g + game::SGameCameraY);
                    for (int y = cy; y < cy + 1080; y += 48)
                        for (int x = cx; x < cx + 1920; x += 48)
                            if (type >= 0x3f ? ((short(__thiscall*)(void*, int, int))0x442b10)(sh, x, y) == 1
                                             : (((Valid_t)0x4517e0)(w, player, x, y, type, 1, 0, 0) & 0xffff) == 0)
                                line += std::to_string((x - cx) * 1000 / 1920) + "," +
                                        std::to_string((y - cy) * 1000 / 1080) + " ";
                    g_h.log("autopilot:   valid spots for %#x: %s", type, line.substr(0, 1500).c_str());
                }
            }
            if (!strcmp(st.tag, "d-zoomed-out")) prof::StopSampling((g_h.dir + "WarBreedsHD-samples-out.txt").c_str());
            if (!strcmp(st.tag, "g-zoomed-in")) prof::StopSampling((g_h.dir + "WarBreedsHD-samples-in.txt").c_str());
            Save(st.tag);
            LogView();
            break;
        case ZoomOut:
            if (prof::Enabled()) prof::StartSampling(GetCurrentThreadId());
            for (int i = 0; i < 4; ++i) g_h.zoomStep(+1);
            break;
        case ZoomIn:
            if (prof::Enabled()) prof::StartSampling(GetCurrentThreadId());
            for (int i = 0; i < 8; ++i) g_h.zoomStep(-1);
            break;
        case Esc: Key(VK_ESCAPE); break;
        case Quit: g_h.log("autopilot: done, quitting"); PostQuitMessage(0); break;
        case CursorCentre: render::SetFakeCursor(true, centre); Post(WM_MOUSEMOVE, 0, centre); break;
        case Glide:
            g_glideFrom = {game::at<short>(game::CursorX), game::at<short>(game::CursorY)};
            g_glideTo = {(LONG)((int64_t)st.to.x * frameW / 1000), (LONG)((int64_t)st.to.y * frameH / 1000)};
            g_glideStart = now;
            g_gliding = true;
            break;
        case ClickHere:
            if (g_h.mode == 8) {  // debug: would the Shaman's plant check accept this point?
                uintptr_t g = ((uintptr_t*)game::Scenes)[0];
                char* w = *(char**)0x579278;
                char** sel = *(char***)(w + 0x80);
                int wx = ((int(__thiscall*)(void*, int))0x47ec60)((void*)g, g_glideTo.x) & 0xffff;
                int wy = ((int(__thiscall*)(void*, int))0x47ec80)((void*)g, g_glideTo.y) & 0xffff;
                int ok = *(int*)(w + 0x84) == 1 && *(int*)(sel[0] + 0x1ac) == 5
                             ? ((short(__thiscall*)(void*, int, int))0x442b10)(sel[0], wx, wy)
                             : -1;
                g_h.log("autopilot:   click world %d,%d plant check %d", (short)wx, (short)wy, ok);
            }
            Click(g_glideTo);
            break;
        case RightClickHere:
            Post(WM_RBUTTONDOWN, MK_RBUTTON, g_glideTo);
            Post(WM_RBUTTONUP, 0, g_glideTo);
            g_h.log("autopilot: right-click %ld,%ld", g_glideTo.x, g_glideTo.y);
            break;
        case SpawnBuilders: {  // to.x more builders of the first own builder's type, beside it
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            int count = *(int*)(w + 0x14);
            char* first = nullptr;
            for (int i = 0; i < count && !first; ++i) {
                char* t = things[i];
                int ty = t ? *(int*)(t + 0x18) : -1;
                if (t && *(char**)(t + 0x20) == me && (ty == 0 || ty == 9 || ty == 0x12 || ty == 0x1b)) first = t;
            }
            if (!first) {
                g_h.log("autopilot: no own builder to copy");
                break;
            }
            // World::PlaceCreature 0x4472d0(player, type, x, y, 4 gene values; 100 = default).
            typedef unsigned short(__thiscall * Place_t)(void*, int, int, int, int, int, int, int, int);
            static const short off[][2] = {{70, 0}, {0, 70}, {70, 70}, {-70, 0}, {0, -70}, {-70, 70}, {140, 0}, {0, 140}};
            int made = 0;
            for (const auto& o : off) {
                // AutopilotTarget: how many (default to.x; -1 = none, the stock one-Shaman flow).
                if (made == (g_h.target < 0 ? 0 : g_h.target > 0 ? g_h.target : st.to.x)) break;
                unsigned short id = ((Place_t)0x4472d0)(w, *(short*)(me + 4), *(int*)(first + 0x18),
                                                        *(short*)(first + 0x14) + o[0], *(short*)(first + 0x16) + o[1],
                                                        100, 100, 100, 100);
                if (id == 0xc01) continue;
                ++made;
                g_h.log("autopilot: created builder id %d (type %#x) at %d,%d", id, *(int*)(first + 0x18),
                        *(short*)(first + 0x14) + o[0], *(short*)(first + 0x16) + o[1]);
            }
            break;
        }
        case GlideToBuildSpot: {  // to a spot the selected lead's building may go, nearest an own pod
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** sel = *(char***)(w + 0x80);
            if (*(int*)(w + 0x84) < 1 || !sel[0]) break;
            int type = *(int*)(sel[0] + 0x188);
            char** things = *(char***)(w + 0x10);
            int count = *(int*)(w + 0x14);
            char* pod = nullptr;  // the pod nearest the lead (pods may have no owner)
            int podD = 1 << 30;
            for (int i = 0; i < count; ++i) {
                char* t = things[i];
                if (!t || *(int*)(t + 0x18) < 0x3f || *(int*)(t + 0x18) > 0x41) continue;
                int dx = *(short*)(t + 0x14) - *(short*)(sel[0] + 0x14), dy = *(short*)(t + 0x16) - *(short*)(sel[0] + 0x16);
                if (dx * dx + dy * dy < podD) podD = dx * dx + dy * dy, pod = t;
            }
            if (!pod) {
                g_h.log("autopilot: no pod yet");
                break;
            }
            g_h.log("autopilot: pod owner %p (me %p)", *(void**)(pod + 0x20), (void*)me);
            uintptr_t g = ((uintptr_t*)game::Scenes)[0];
            int f = *(short*)(g + game::SGameZoomedOut) == 1 ? 2 : 1;
            int camX = *(short*)(g + game::SGameCameraX), camY = *(short*)(g + game::SGameCameraY);
            int px = *(short*)(pod + 0x14), py = *(short*)(pod + 0x16);
            typedef int(__thiscall * Valid_t)(void*, int, int, int, int, int, int, int);
            int best = 1 << 30;
            POINT spot{-1, -1};
            for (int y = py - 192; y <= py + 192; y += 8)
                for (int x = px - 192; x <= px + 192; x += 8) {
                    int sx = (x - camX) / f, sy = (y - camY) / f;
                    if (sx < 100 || sy < 100 || sx > frameW - 100 || sy > frameH - 100) continue;
                    if (sx > frameW - 400 && sy < 300) continue;  // the info panel
                    if (((Valid_t)0x4517e0)(w, *(short*)(me + 4), x, y, type, 1, 0, 0) != 0) continue;
                    int d = (x - px) * (x - px) + (y - py) * (y - py);
                    if (d < best) best = d, spot = {sx, sy};
                }
            g_h.log("autopilot: pod id %d at %d,%d; nearest valid spot for %#x: frame %ld,%ld", *(short*)(pod + 0x1c), px,
                    py, type, spot.x, spot.y);
            if (spot.x < 0) break;
            g_glideFrom = {game::at<short>(game::CursorX), game::at<short>(game::CursorY)};
            g_glideTo = spot;
            g_glideStart = now;
            g_gliding = true;
            break;
        }
        case ClickPanelButton: {  // the info panel's button at address to.x, clicked where it is shown
            void* btn = (void*)(uintptr_t)st.to.x;
            long sx = 0, sy = 0, hits = 0;
            for (int y = 0; y < std::min(frameH, 400); y += 2)
                for (int x = std::max(0, frameW - 400); x < frameW; x += 2)
                    if (((void*(__thiscall*)(void*, int, int))0x497c60)((void*)game::InfoPanel, x, y) == btn)
                        sx += x, sy += y, ++hits;
            if (!hits) {
                g_h.log("autopilot: panel button %p not shown", btn);
                break;
            }
            POINT g{sx / hits, sy / hits};
            POINT c = render::InterfacePointToClient(g);
            HWND hw = render::GameWindow();
            render::SetFakeCursorClient(c);
            PostMessageA(hw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
            PostMessageA(hw, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
            PostMessageA(hw, WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
            g_h.log("autopilot: click panel button %p at frame %ld,%ld (client %ld,%ld)", btn, g.x, g.y, c.x, c.y);
            break;
        }
        case SpawnThing: {  // a thing of type to.x for the local player, beside its first creature
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            int count = *(int*)(w + 0x14);
            for (int i = 0; i < count; ++i) {
                char* t = things[i];
                if (!t || *(char**)(t + 0x20) != me) continue;
                typedef unsigned short(__thiscall * Place_t)(void*, int, int, int, int, int, int, int, int);
                static const short off[][2] = {{160, 0},    {0, 160},   {-160, 0}, {0, -160}, {224, 224},
                                               {-224, 224}, {224, -224}, {320, 0},  {0, 320},  {-320, 0}};
                bool device = st.to.x >= 0x2c && st.to.x <= 0x3b;
                for (const auto& o : off) {
                    int x = *(short*)(t + 0x14) + o[0], y = *(short*)(t + 0x16) + o[1];
                    if (!device) {
                        unsigned short id = ((Place_t)0x4472d0)(w, *(short*)(me + 4), (int)st.to.x, x, y, 100, 100, 100, 100);
                        g_h.log("autopilot: create thing type %#lx at %d,%d: id %#x", st.to.x, x, y, id);
                        if (id == 0xc01) continue;
                        if (st.to.y == 1) {  // and kill it at once (AThing::Die, vtbl+0x38): what does it leave?
                            char** all = *(char***)(w + 0x10);
                            for (int j = 0; j < *(int*)(w + 0x14); ++j)
                                if (all[j] && *(unsigned short*)(all[j] + 0x1c) == id) {
                                    void** vt = *(void***)all[j];
                                    ((void(__thiscall*)(void*, short))vt[0x38 / 4])(all[j], (short)0xc01);
                                    g_h.log("autopilot:   killed id %#x", id);
                                }
                        }
                        break;
                    }
                    // AThingRegistry::CreateThing 0x446db0(owner, type, x, y, skipChecks, activate, arg)
                    // with the placement checks skipped (a Gene Lab needs power nearby), then
                    // the game's own build step with the progress at its end: a finished building.
                    int before = *(int*)(w + 0x14);
                    ((short(__thiscall*)(void*, short, int, short, short, short, short, short))0x446db0)(
                        w, *(short*)(me + 4), (int)st.to.x, (short)x, (short)y, 1, 1, (short)0xc01);
                    char** all = *(char***)(w + 0x10);
                    char* made = nullptr;
                    for (int j = 0; j < *(int*)(w + 0x14); ++j)
                        if (all[j] && *(char**)(all[j] + 0x20) == me && *(int*)(all[j] + 0x18) == st.to.x) made = all[j];
                    g_h.log("autopilot: create device type %#lx at %d,%d: %s (things %d -> %d)", st.to.x, x, y,
                            made ? "made" : "failed", before, *(int*)(w + 0x14));
                    if (!made) continue;
                    *(short*)(made + 0x32) = 30000;
                    short done = ((short(__thiscall*)(void*))0x40c8a0)(made);
                    ((void(__thiscall*)(void*, short))0x40dfe0)(made, 0);  // ADevice::SetUnpowered(0): powered
                    g_h.log("autopilot:   finished %d, id %d, unpowered %d, on %d", done, *(short*)(made + 0x1c),
                            *(short*)(made + 0x28), *(short*)(made + 0x2a));
                    break;
                }
                break;
            }
            break;
        }
        case SpawnAllCreatures: {  // every creature type for player to.x, 6 per row, 150 px apart
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            char* first = nullptr;
            for (int i = 0; i < *(int*)(w + 0x14) && !first; ++i)
                if (things[i] && *(char**)(things[i] + 0x20) == me) first = things[i];
            if (!first) break;
            typedef unsigned short(__thiscall * Place_t)(void*, int, int, int, int, int, int, int, int);
            // AutopilotTarget: the clan (0 Tanu, 1 Sen-Soth, 2 Kelika, 3 Magha: types clan*9+0..4;
            // 4: Yedda and Dhuer), or -1 for all.
            static const int kTypes[] = {0, 1, 2, 3, 4, 9, 10, 11, 12, 13, 18, 19, 20, 21, 22, 27, 28, 29, 30, 31, 35, 36};
            int n = 0;
            for (int ty : kTypes) {
                int clan = ty >= 35 ? 4 : ty / 9;
                if (g_h.target >= 0 && clan != g_h.target) continue;
                int x = *(short*)(first + 0x14) + 250 + (n % 6) * 150, y = *(short*)(first + 0x16) - 150 + (n / 6) * 150;
                // to.y = 1: armed (gizmo 1 in both arm slots, 4 on the head; 100 = empty).
                int arm = st.to.y == 1 ? 1 : 100, head = st.to.y == 1 ? 4 : 100;
                unsigned short id = ((Place_t)0x4472d0)(w, (int)st.to.x, ty, x, y, arm, head, arm, 100);
                g_h.log("autopilot: creature type %#x (%s) for player %ld at %d,%d: id %#x", ty, "", st.to.x, x, y, id);
                ++n;
            }
            break;
        }
        case SpawnFlyers: {  // two of each air unit for the local player, armed, beside its first thing
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** roster = *(char***)(w + 0x10);
            char* first = nullptr;
            for (int i = 0; i < *(int*)(w + 0x14) && !first; ++i)
                if (roster[i] && *(char**)(roster[i] + 0x20) == me) first = roster[i];
            if (!first) break;
            typedef unsigned short(__thiscall * Place_t)(void*, int, int, int, int, int, int, int, int);
            static const int kAir[] = {4, 0xd, 0x1f, 4, 0xd, 0x1f};
            for (int n = 0; n < 6; ++n) {
                unsigned short id = ((Place_t)0x4472d0)(w, *(short*)(me + 4), kAir[n], *(short*)(first + 0x14) + 120 + (n % 3) * 90,
                                                        *(short*)(first + 0x16) + (n / 3) * 90, 1, 4, 1, 100);
                g_h.log("autopilot: flyer type %#x: id %#x", kAir[n], id);
            }
            break;
        }
        case SetCGK: {  // APlayer +0x4850 int level[5]: the local player's gene level per clan
            char* me = ((char* (*)())0x46f8e0)();
            for (int i = 0; i < 5; ++i) *(int*)(me + 0x4850 + 4 * i) = (int)st.to.x;
            g_h.log("autopilot: gene levels set to %ld", st.to.x);
            break;
        }
        case PickSpecies: {  // the own Gene Lab's species choice (0x42bde0 on AGeneLab+0x40): Select Components
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            for (int i = 0; i < *(int*)(w + 0x14); ++i) {
                char* t = things[i];
                if (!t || *(char**)(t + 0x20) != me || (*(int*)(t + 0x18) != 0x2d && *(int*)(t + 0x18) != 0x35)) continue;
                ((void(__fastcall*)(void*, unsigned, int))0x42bde0)(t + 0x40, 0, (int)st.to.x);
                g_h.log("autopilot: Gene Lab id %d: species %ld chosen; unpowered %d on %d complete %d paused %d, ui state %d type %d",
                        *(short*)(t + 0x1c), st.to.x, *(short*)(t + 0x28), *(short*)(t + 0x2a), *(short*)(t + 0x2c),
                        *(short*)(t + 0x2e), *(int*)(t + 0x40 + 0x1a4), *(int*)(t + 0x40 + 0x2c));
                break;
            }
            break;
        }
        case CursorClient:
        case ClickClient: {
            RECT r;
            GetClientRect(render::GameWindow(), &r);
            POINT c{(LONG)((int64_t)st.to.x * r.right / 1000), (LONG)((int64_t)st.to.y * r.bottom / 1000)};
            render::SetFakeCursorClient(c);
            HWND hw = render::GameWindow();
            PostMessageA(hw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
            if (st.act == ClickClient) {
                PostMessageA(hw, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
                PostMessageA(hw, WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
            }
            g_h.log("autopilot: %s client %ld,%ld (game cursor %d,%d)", st.act == ClickClient ? "click" : "cursor to",
                    c.x, c.y, game::at<short>(game::CursorX), game::at<short>(game::CursorY));
            break;
        }
        case WheelNotches:
            PostMessageA(render::GameWindow(), WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(st.to.x * WHEEL_DELTA)), 0);
            g_h.log("autopilot: wheel %ld notches", st.to.x);
            break;
        case GlideToOwnBuilding: {  // the first own building of type to.x
            char* w = *(char**)0x579278;
            char* me = ((char* (*)())0x46f8e0)();
            char** things = *(char***)(w + 0x10);
            int count = *(int*)(w + 0x14);
            uintptr_t g = ((uintptr_t*)game::Scenes)[0];
            int f = *(short*)(g + game::SGameZoomedOut) == 1 ? 2 : 1;
            for (int i = 0; i < count; ++i) {
                char* t = things[i];
                if (!t || *(char**)(t + 0x20) != me || *(int*)(t + 0x18) != st.to.x) continue;
                g_glideFrom = {game::at<short>(game::CursorX), game::at<short>(game::CursorY)};
                g_glideTo = {(*(short*)(t + 0x14) - *(short*)(g + game::SGameCameraX)) / f,
                             (*(short*)(t + 0x16) - *(short*)(g + game::SGameCameraY)) / f};
                g_glideStart = now;
                g_gliding = true;
                g_h.log("autopilot: to building id %d at frame %ld,%ld", *(short*)(t + 0x1c), g_glideTo.x, g_glideTo.y);
                break;
            }
            break;
        }
        case RecordStart: render::d3d11::Record(g_h.dir + "rec"); break;
        case RecordStop: render::d3d11::Record(""); break;
        case HoldKey:  // held for the smooth scroll (the game itself never sees it)
            camera::ForceHeld((int)st.to.x, true);
            g_h.log("autopilot: holding key %#lx", st.to.x);
            break;
        case ReleaseKey: camera::ForceHeld((int)st.to.x, false); break;
        case ShiftDrag: {  // from the glide point to `to` (thousandths), with Shift held
            POINT to{(LONG)((int64_t)st.to.x * frameW / 1000), (LONG)((int64_t)st.to.y * frameH / 1000)};
            HWND hw = render::GameWindow();
            LogView();  // the selection before
            camera::ForceHeld(VK_SHIFT, true);  // released at the next step
            PostMessageA(hw, WM_KEYDOWN, VK_SHIFT, 1);
            Post(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, g_glideTo);
            render::SetFakeCursor(true, to);
            Post(WM_MOUSEMOVE, MK_LBUTTON | MK_SHIFT, to);
            g_shiftDragTo = to;
            g_shiftDragUpAt = GetTickCount() + 300;
            g_h.log("autopilot: shift-drag %ld,%ld -> %ld,%ld", g_glideTo.x, g_glideTo.y, to.x, to.y);
            break;
        }
        case KeyTap: {
            WPARAM vk = st.to.x >= 0 ? (WPARAM)st.to.x : (WPARAM)(g_h.target ? g_h.target : 'T');
            WPARAM mod = (WPARAM)st.to.y;
            HWND w = render::GameWindow();
            if (mod) {
                camera::ForceHeld((int)mod, true);  // released at the next step
                PostMessageA(w, WM_KEYDOWN, mod, 1);
            }
            PostMessageA(w, WM_KEYDOWN, vk, 1);
            PostMessageA(w, WM_KEYUP, vk, 0xC0000001);
            if (mod) PostMessageA(w, WM_KEYUP, mod, 0xC0000001);
            g_h.log("autopilot: key %#x (modifier %#x)", (unsigned)vk, (unsigned)mod);
            LogView();
            break;
        }
        }
    }
}

}  // namespace autopilot
