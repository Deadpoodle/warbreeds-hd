// Smooth camera zoom (needs the WarBreeds HD renderer).
//
// The zoom is a magnification m = screen pixels per world pixel. At rest the
// game renders at screen / m, which the renderer scales back up to the screen.
// Beyond a pixel budget the game's own half-scale view (the Z key) takes
// over, halving the render size. The map point under the cursor stays put.
//
// Changing the game's resolution is expensive (40-110 ms: the engine rebuilds
// its full-screen surfaces and resizes the window), so an animation doesn't
// resize every frame. The game renders a frame big enough for the most
// zoomed-out view the animation will show, and the renderer shows the part of
// it the current magnification covers (SetWorldCrop). Zooming out resizes
// once at the start, zooming in once at the end.
#include "zoom.h"

#include <windows.h>
#include <math.h>

#include <algorithm>

#include "game.h"
#include "render/render.h"

namespace zoom {
namespace {

Settings g_s;
void (*g_log)(const char*, ...);
int g_screenW, g_screenH;
double g_target, g_cur;
int g_resW, g_resH;      // resolution missions run at right now
double g_minEffective;   // raised if the map is too small for half-scale view
bool g_halfUnavailable;  // this mission's map is too small for half-scale view
void* g_mission;         // SGame the above applies to
// While animating: the shown world area's top-left (world pixels) and the
// camera position we last left the game with.
bool g_cropping;
bool g_atCursor = true;  // anchor of the current zoom: cursor or screen centre
double g_viewX, g_viewY;
int g_lastCamX, g_lastCamY;
LARGE_INTEGER g_last, g_freq;

typedef short(__cdecl* SetResolution_t)(int w, int h, int depth, int flag);
typedef void(__thiscall* SGameFn)(void* sgame);

struct Plan {
    int w, h;
    int factor;  // world pixels per frame pixel: 1, or 2 with the game's half-scale view
};

Plan PlanFor(double m, int currentFactor) {
    Plan p;
    for (int f : {1, 2}) {
        p.factor = f;
        p.w = std::max(640, (int)lround(g_screenW / (m * f)) & ~1);
        p.h = std::max(480, (int)lround(g_screenH / (m * f)) & ~1);
        // Hysteresis: once half-scale, stay there until comfortably under budget.
        double budget = (double)g_s.maxPixels * (currentFactor == 2 ? 0.8 : 1.0);
        if (!g_s.halfScale || g_halfUnavailable || (double)p.w * p.h <= budget) break;
    }
    return p;
}

}  // namespace

void Init(const Settings& s, int screenW, int screenH, void (*log)(const char*, ...)) {
    g_s = s;
    g_log = log;
    g_screenW = screenW;
    g_screenH = screenH;
    g_target = g_cur = std::clamp(s.defaultMag, s.minMag, s.maxMag);
    g_minEffective = s.minMag;
    Plan p = PlanFor(g_cur, 1);
    g_resW = p.w;
    g_resH = p.h;
    render::AllowMode(g_resW, g_resH);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last);
    log("zoom: smooth, %.3fx (render %dx%d), range %.2f-%.2f, budget %d px", g_cur, g_resW, g_resH, s.minMag,
        s.maxMag, s.maxPixels);
}

void MissionResolution(int& w, int& h) {
    w = g_resW;
    h = g_resH;
}

void Wheel(int notches, bool atCursor) {
    g_atCursor = atCursor;
    g_target = std::clamp(g_target * pow(g_s.step, notches), g_minEffective, g_s.maxMag);
}

void Tick(void* sgame) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = std::min(0.1, (double)(now.QuadPart - g_last.QuadPart) / g_freq.QuadPart);
    g_last = now;
    if (!sgame) {
        if (g_cropping) render::SetWorldCrop(false);
        g_cropping = false;
        return;
    }
    if (sgame != g_mission) {  // new mission: forget the previous map's limits
        g_mission = sgame;
        g_halfUnavailable = false;
        g_minEffective = g_s.minMag;
        g_cropping = false;
        render::SetWorldCrop(false);
    }
    if (!g_cropping && fabs(g_cur - g_target) < 1e-4) return;

    char* g = (char*)sgame;
    short& zoomedOut = *(short*)(g + game::SGameZoomedOut);
    short& camX = *(short*)(g + game::SGameCameraX);
    short& camY = *(short*)(g + game::SGameCameraY);
    int factor = zoomedOut == 1 ? 2 : 1;

    // The shown area: the whole frame at rest; follow camera scrolling made
    // by the game during an animation.
    if (!g_cropping) {
        g_viewX = camX;
        g_viewY = camY;
    } else {
        g_viewX += camX - g_lastCamX;
        g_viewY += camY - g_lastCamY;
    }

    // Anchor: the world point under the cursor (or the screen centre).
    double fx = 0.5, fy = 0.5;
    if (g_atCursor) render::CursorClientFraction(fx, fy);
    double ax = g_viewX + fx * g_screenW / g_cur, ay = g_viewY + fy * g_screenH / g_cur;
    if (!g_cropping) g_log("zoom: %.2fx -> %.2fx at %.3f,%.3f of the screen, map point %.0f,%.0f", g_cur, g_target, fx, fy, ax, ay);

    // Ease towards the target (exponential; ~0.25 s to settle).
    double k = 1.0 - exp(-dt * g_s.speed);
    g_cur += (g_target - g_cur) * k;
    if (fabs(g_cur - g_target) < g_target * 0.004) g_cur = g_target;
    bool done = g_cur == g_target;

    auto setRes = [&](Plan& q) {
        // The game skips a mode change if only one dimension differs; avoid that.
        if ((q.w == g_resW) != (q.h == g_resH)) q.h += 2;
        g_resW = q.w;
        g_resH = q.h;
        render::AllowMode(q.w, q.h);
        ((SetResolution_t)game::SetResolution)(q.w, q.h, 0x100, 1);
    };
    // Switch the game to plan p (resolution and half-scale view).
    auto apply = [&](Plan p) {
        if (p.w == g_resW && p.h == g_resH && p.factor == factor) return;
        if (p.factor == 2 && factor == 1) {
            // Shrink first: the game only allows its half-scale view if twice the
            // current view fits inside the map.
            setRes(p);
            ((SGameFn)game::SGameToggleZoom)(g);
            if (zoomedOut != 1) {
                // Map too small: stay at full scale, limited by the pixel budget.
                g_halfUnavailable = true;
                g_minEffective = std::max(g_s.minMag, sqrt((double)g_screenW * g_screenH / g_s.maxPixels));
                g_target = std::max(g_target, g_minEffective);
                g_cur = std::max(g_cur, g_minEffective);
                g_log("zoom: map too small for half-scale view; zoom-out limited to %.2fx", g_minEffective);
                p = PlanFor(std::min(g_cur, g_target), 1);
                setRes(p);
            }
        } else if (p.factor == 1 && factor == 2) {
            ((SGameFn)game::SGameToggleZoom)(g);
            setRes(p);
        } else {
            setRes(p);
        }
        factor = zoomedOut == 1 ? 2 : 1;
    };

    if (done) {
        // Final view: the frame becomes exactly the shown area again.
        apply(PlanFor(g_cur, factor));
        g_viewX = ax - fx * g_screenW / g_cur;
        g_viewY = ay - fy * g_screenH / g_cur;
        camX = (short)lround(g_viewX);
        camY = (short)lround(g_viewY);
        ((SGameFn)game::SGameClampCamera)(g);
        g_log("zoom: settled at %.2fx, map point under the cursor %.0f,%.0f (camera %d,%d)", g_cur,
              camX + fx * g_screenW / g_cur, camY + fy * g_screenH / g_cur, camX, camY);
        render::SetWorldCrop(false);
        g_cropping = false;
        return;
    }

    // Animating: make sure the frame covers the most zoomed-out view still to
    // come; resize only if it doesn't.
    double widest = std::min(g_cur, g_target);
    if ((double)g_resW * factor < g_screenW / widest - 1 || (double)g_resH * factor < g_screenH / widest - 1) {
        apply(PlanFor(widest, factor));
        // Place the frame so every view anchored at this point fits inside it.
        camX = (short)lround(ax - fx * g_resW * factor);
        camY = (short)lround(ay - fy * g_resH * factor);
        ((SGameFn)game::SGameClampCamera)(g);
    }

    // Show the current view's part of the frame (kept inside it at map edges).
    double vw = g_screenW / g_cur, vh = g_screenH / g_cur;
    double cx = std::clamp(ax - fx * vw - camX, 0.0, std::max(0.0, g_resW * factor - vw));
    double cy = std::clamp(ay - fy * vh - camY, 0.0, std::max(0.0, g_resH * factor - vh));
    g_viewX = camX + cx;
    g_viewY = camY + cy;
    render::SetWorldCrop(true, cx / factor, cy / factor, vw / factor, vh / factor);
    g_cropping = true;
    g_lastCamX = camX;
    g_lastCamY = camY;
}

double Current() { return g_cur; }

void SetRange(double minMag, double maxMag) {
    g_s.minMag = minMag;
    g_s.maxMag = maxMag;
    g_minEffective = g_halfUnavailable ? std::max(minMag, g_minEffective) : minMag;
    g_target = std::clamp(g_target, g_minEffective, g_s.maxMag);
}

}  // namespace zoom
