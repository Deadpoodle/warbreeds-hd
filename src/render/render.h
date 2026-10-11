// WarBreeds HD renderer: takes over presentation of the game's GDI frames.
//
// WARBREED.EXE renders each frame into an 8-bit DIB section and copies it to
// its window with one StretchBlt; DirectDraw is only used to set display
// modes. The renderer fakes the mode changes (ddfake.cpp), intercepts that
// StretchBlt, keeps the latest frame + palette, and presents it scaled with
// Direct3D 11 (or GDI as a fallback). Mouse coordinates are mapped between
// the real window and the game's mode-sized coordinate space.
#pragma once
#include <windows.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace render {

struct Mode {
    int w, h, bpp;
};

struct Settings {
    bool d3d11 = true;   // false = GDI presenter
    bool vsync = true;
    int filter = 2;      // 0 = nearest, 1 = bilinear, 2 = sharp bilinear
    std::vector<Mode> modes;  // advertised display modes (bpp ignored)
    bool gdiLog = false;      // debug logging of the game's frame DIBs
    bool headless = false;    // [Debug] Headless: keep the window hidden and off-screen
    bool showFps = false;     // [Render] ShowFPS: frame rate in the top-left corner
    bool clickLog = false;    // [Debug] ClickLog: log how each mouse click is mapped
    bool windowed = false;    // [Display] DisplayMode=windowed: a normal window instead of borderless fullscreen
    int windowW = 0, windowH = 0;  // windowed client size
    bool menuArt = true;      // [Display] MenuArt: widescreen backgrounds with centred 4:3 menus,
                              // or original (no art, 4:3 menus at the left edge, black beside)
};

// Setup, from DllMain. Installs every hook the renderer needs.
bool Install(HMODULE self, const Settings& s, void (*log)(const char*, ...));
bool Active();

// Called by ddfake.
const std::vector<Mode>& AdvertisedModes();
Mode CurrentMode();
void OnSetDisplayMode(int w, int h, int bpp);
void OnSetCooperativeLevel(HWND hwnd, DWORD flags);

// Called from the game window's subclassed WndProc before the game sees a
// message; rewrites mouse coordinates into game space.
void FilterMessage(UINT msg, WPARAM wp, LPARAM& lp);

// The latest frame, in game pixels (8-bit indices + palette).
struct Frame {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;  // w*h, top-down: the finished frame
    RGBQUAD palette[256];
    uint32_t serial = 0;          // bumped whenever pixels or palette change
    // In missions (see layers.cpp) `pixels` holds only the battlefield; the
    // interface is in `hud` and the mouse cursor in `cursor` (index 0 = empty).
    bool split = false;
    std::vector<uint8_t> hud, cursor;
    int cursorX = 0, cursorY = 0;  // game cursor position (frame pixels)
    // Menus: the cursor drawn on its own (menuCursorSize^2, hotspot at the
    // centre; see layers.cpp), kept until the next one or a scene change.
    std::vector<uint8_t> menuCursor;
    int menuCursorSize = 0;
    uint32_t menuCursorSerial = 0;
};

// Cutscenes: show a movie frame (top-down BGRA) full-screen now, scaled to fit;
// null ends the movie (the game's own frames are shown again).
void ShowMovieFrame(const uint32_t* bgra, int w, int h);

// Menus: the cursor image from layers.cpp (size^2, hotspot at the centre).
void SetMenuCursor(const uint8_t* px, int size, int gameX, int gameY);
// Every frame tick, with the scene stack's RTTI names, top first: switches the
// widescreen menu background (Menus\<Scene>.png, the first scene that has
// one) and re-presents menus so the cursor follows the mouse even when the
// game has nothing new to show.
void Tick(const std::vector<std::string>& scenes);
// The scene stack's RTTI names, top first, for frames presented outside the
// frame tick (loading screens). captureScenes: [Debug] CaptureScenes.
void SetSceneStackProvider(std::vector<std::string> (*fn)(), int captureScenes);

// Mission layers for the next presented frame (top-down, w*h each).
void SetInterfaceLayers(const uint8_t* ui, const uint8_t* cursor, int w, int h, int cursorX, int cursorY);
// Row order of one of the game's 8-bit DIB sections, looked up by its pixels.
bool DibIsBottomUp(const void* bits, bool fallback);
const uint8_t* FrameBits();   // the game's frame DIB pixels (null if unknown)
HDC FrameDC();                // the memory DC the frame is presented from
uint8_t* FramePixel(int x, int y);  // address of a frame pixel (null if unknown)
POINT GameCursor();           // mouse position in frame pixels (clamped)
const Frame& CurrentFrame();

// Where the frame is drawn inside the window's client area.
RECT Viewport();

// A piece of the frame drawn somewhere on screen: the battlefield into the
// viewport, then each interface element and the cursor at the interface scale.
struct Layer {
    enum Kind { World, InterfaceScaled, Cursor, MenuCursor };
    RECT src;  // frame pixels
    RECT dst;  // client pixels
    Kind kind;
    bool fractional = false;  // use fsrc instead of src (smooth zoom crop)
    double fsrc[4] = {};      // left, top, right, bottom in frame pixels
};

// Size of the mission interface and cursor: screen pixels per game pixel.
// scale = interface px per game px at a window baseHeight px tall (0 = as given).
void SetHudScale(double scale, int baseHeight = 0);
// Borderless fullscreen, or a normal window with this client size (centred
// when switching; afterwards the player may move and resize it).
void SetWindowed(bool on, int clientW, int clientH);

// Cutscenes: scaling 0 Enhanced, 1 Smooth, 2 Sharp; originalSize = sized as in
// the 4:3 game screen instead of filling the width.
void SetMovieOptions(int scaling, bool originalSize);

// Live display settings.
void SetFilter(int filter);  // 0 nearest, 1 bilinear, 2 sharp
void SetVSync(bool on);
void SetMenuArt(bool widescreen);

// FPS counter (F2 / [Render] ShowFPS).
void SetShowFps(bool on);
bool ShowFps();

// Debug autopilot support: a virtual cursor (game coordinates) and helpers
// to post input to the game window.
void SetFakeCursor(bool on, POINT game);
void SetFakeCursorClient(POINT client);  // the virtual mouse at a window (screen-space) position
POINT GamePointToClient(POINT game);
POINT InterfacePointToClient(POINT game);  // where an interface element's frame pixel is shown
HWND GameWindow();

// Allow one extra display mode (used for continuous zoom).
void AllowMode(int w, int h);
// Smooth zoom: show only this part of the next frames of the current mode
// (frame pixels, fractional), scaled to the whole viewport. Off = whole frame.
void SetWorldCrop(bool on, double x = 0, double y = 0, double w = 0, double h = 0);
// Mouse position as a fraction of the client area (false if outside).
bool CursorClientFraction(double& fx, double& fy);

// Client point -> frame pixel, ignoring interface elements (i.e. the battlefield).
POINT ClientToFrame(POINT client);
// Real cursor position in frame pixels; false if it is outside the window.
bool CursorFrame(POINT& out);

// Presenters.
namespace d3d11 {
bool Init(HWND hwnd, const Settings& s, void (*log)(const char*, ...));
void Present(const Frame& f, const std::vector<Layer>& layers, int clientW, int clientH);
void Screenshot(const std::string& path);  // save the next presented image (BMP)
void Record(const std::string& prefix);    // debug: save presented frames as <prefix>-<ms>.bmp; "" stops
void SetOverlayText(const char* text);      // small text box in the top-left corner ("" = none)
void SetFilter(int filter);
void SetVSync(bool on);
// Widescreen menu background (BGRA, drawn under the frame at dst); null = none.
void SetBackground(const uint32_t* px, int w, int h);
// A cutscene movie frame (BGRA), scaled into dst on a black screen, presented now.
void PresentMovie(const uint32_t* px, int w, int h, RECT dst, int clientW, int clientH);
void SetMovieScaling(int mode);  // 0 Enhanced, 1 Smooth, 2 Sharp
void SetBackgroundRect(bool on, RECT dst);
// Optional "-glow" variant (same size; set after SetBackground), shown while on.
void SetBackgroundGlow(const uint32_t* px, int w, int h);
void SetGlow(bool on);
void Shutdown();
}  // namespace d3d11

// Replace a function pointer in WARBREED.EXE's import address table.
template <typename T>
bool HookImport(uintptr_t slot, T hook, T* orig) {
    DWORD old;
    if (!VirtualProtect((void*)slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = *(T*)slot;
    *(T*)slot = hook;
    VirtualProtect((void*)slot, sizeof(void*), old, &old);
    return true;
}

}  // namespace render
