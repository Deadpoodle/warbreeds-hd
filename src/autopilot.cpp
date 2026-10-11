// Debug autopilot ([Debug] Autopilot=1): drives the game from the main menu
// into a skirmish and through a fixed test script, saving the layers and the
// displayed image at each step, then quits. Input is posted to the game
// window and the cursor is virtual, so the real mouse and keyboard are never
// touched (combine with [Debug] Headless=1 to keep the window off-screen).
#include "autopilot.h"

#include <windows.h>

#include <algorithm>
#include <string.h>

#include <string>

#include "diag.h"
#include "game.h"
#include "menu.h"
#include "prof.h"
#include "render/render.h"
#include "camera.h"

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
    if (g_h.mode == 8 || g_h.mode == 9 || g_h.mode == 12) {
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
           ClickPanelButton, GlideToOwnBuilding };
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
        enum Kind { SaveShot, LeftClick, Hover, RightClick, Quit };
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
            {6000, SaveShot, {}, "m3-controls"}, {6300, LeftClick, {360, 26}, nullptr},   // Interface tab
            {7000, SaveShot, {}, "m4-interface"}, {7300, LeftClick, {60, 82}, nullptr},   // Menu Art -> Original
            {8000, SaveShot, {}, "m5-original"}, {8200, Hover, {60, 82}, nullptr},
            {8500, RightClick, {60, 82}, nullptr},                                      // Menu Art back
            {9200, SaveShot, {}, "m6-widescreen"}, {9500, LeftClick, {60, 390}, nullptr},  // Reset (arm)
            {10000, LeftClick, {60, 390}, nullptr},                                      // Reset (confirm)
            {10700, SaveShot, {}, "m7-reset"},   {11000, LeftClick, {60, 418}, nullptr},  // Back
            {12000, SaveShot, {}, "m8-back"},    {12300, Quit, {}, nullptr},
        };
        if (g_step < (int)(sizeof kMenu / sizeof kMenu[0]) && t > kMenu[g_step].at) {
            const MenuStep& m = kMenu[g_step++];
            switch (m.kind) {
            case SaveShot: Save(m.tag); break;
            case LeftClick: Click(m.p); break;
            case Hover: render::SetFakeCursor(true, m.p); Post(WM_MOUSEMOVE, 0, m.p); break;
            case RightClick:
                Post(WM_RBUTTONDOWN, MK_RBUTTON, m.p);
                Post(WM_RBUTTONUP, 0, m.p);
                g_h.log("autopilot: right-click %ld,%ld", m.p.x, m.p.y);
                break;
            case Quit: g_h.log("autopilot: done, quitting"); PostQuitMessage(0); break;
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
                                              : kMission;
        int steps = g_h.mode == 8    ? (int)(sizeof kControls / sizeof kControls[0])
                    : g_h.mode == 9  ? (int)(sizeof kModern / sizeof kModern[0])
                    : g_h.mode == 10 ? (int)(sizeof kRecord / sizeof kRecord[0])
                    : g_h.mode == 12 ? (int)(sizeof kGroup / sizeof kGroup[0])
                                     : (int)(sizeof kMission / sizeof kMission[0]);
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
