// In-game map editor, Stage 1 shell (re/editor-shell.md).
//
// A map is opened the way a saved game is: the mission scene SGame (static
// object 0x567ef8) gets the path (SetParam 0, 0x4803e0), skirmish off
// (SetParam 2) and the single-player command model starts (0x470780), which
// runs the stock loading screen. Before that, SGame +0x460 (the console's
// "update pause", 0x485360) is set: only the SGame constructor clears it, so
// no simulation turn ever runs while editing (no AI, no tile animation, no
// pathing work). The local player's fog flag (+0x484c) is cleared while
// editing and restored around every save, and the minimap is kept on.
//
// The tools panel is made of the game's own text buttons (WTextButton2),
// added to SGame's widget list: SGame draws them inside the interface phase,
// so they land in WarBreeds HD's interface layer and keep one size at every
// zoom, like the info panel. They get their own copy of the button vtable
// with a Draw that first paints a dark band behind the row and does the hover
// highlight. Mouse input goes through SGame's own handlers (vtable slots),
// so coordinates are game frame pixels whatever the zoom: panel clicks are
// ours, the minimap keeps its stock behaviour, everything else paints.
//
// Painting goes through the Painter interface (painter.h); Stage 1 uses
// RawPainter (MakePainter). Each stroke is an undo record (the cells whose tile changed,
// before and after).
//
// Save: the guide graph is rebuilt with the game's own map processing
// (AITerrainAnalysis::Rebuild 0x4a8030, then AIMovementGuide::ConnectStep
// 0x4a5200 run here in time slices instead of 200 pairs per turn), then the
// stock deferred save writes the file (SGame +0x88 path, +0x86 pending ->
// SGame::SaveGame 0x480be0 in the next SGame tick).
#include "editor.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "../game.h"
#include "../layers.h"
#include "../render/render.h"
#include "raw_paint.h"
#include "terrain_paint.h"

namespace editor {
namespace {

Hooks g_h;

// --- engine addresses --------------------------------------------------------

constexpr uintptr_t kGame = 0x567ef8;            // SGame static object (scene id 2)
constexpr uintptr_t kHub = 0x551738;             // SHeadquarters static object (scene id 0x10)
constexpr uintptr_t kGridCells = 0x55dd68;       // AGrid +8: u16[96][96] tile ids, row-major
constexpr uintptr_t kGrid = 0x55dd60;            // AGrid
constexpr uintptr_t kTileClass = 0x566dbc;       // AGrid +0x905c: u16 TRID class per tile id
constexpr int kTileIds = 418;
constexpr uintptr_t kTerrainAnalysis = 0x55dd64;  // AGrid +4: AITerrainAnalysis*
constexpr uintptr_t kGuidePointCount = 0x592350;  // u32
constexpr uintptr_t kTileAnimator = 0x562568;    // AGrid +0x4808
constexpr uintptr_t kRadar = 0x5517b0;           // minimap
constexpr uintptr_t kWorld = 0x579278;           // AThingRegistry*
constexpr uintptr_t kPlayers = 0x57927c;         // APlayerRegistry*: +4 APlayer*[8]
constexpr uintptr_t kLocalPlayer = 0x579288;     // APlayer*
constexpr uintptr_t kSceneCmdPending = 0x5792dc;  // short: a scene change is queued

// Functions.
constexpr uintptr_t kMarkAllDirty = 0x42f210;    // AGrid (u8 value, short both)
constexpr uintptr_t kMinimapRefresh = 0x433820;  // Minimap ()
constexpr uintptr_t kAnimStop = 0x455420;        // TileAnimator (): restore base ids, off
constexpr uintptr_t kBuildAnimList = 0x42ff30;   // AGrid (): animated (lava) cells from the tiles
constexpr uintptr_t kGuideRebuild = 0x4a8030;    // AITerrainAnalysis (): obstacle map + guide points
constexpr uintptr_t kGuideConnect = 0x4a5200;    // AIMovementGuide (): link up to 200 point pairs
constexpr uintptr_t kGuidePrune = 0x4a5990;      // fastcall AIMovementGuide: end of processing
constexpr uintptr_t kGuideFill = 0x4a5f00;       // fastcall AIMovementGuide: end of processing
constexpr uintptr_t kSaveGame = 0x480be0;        // SGame (char* path)
constexpr uintptr_t kSaveGameCall = 0x47cfe4;    // in SGame::Tick: call SaveGame(+0x88)
constexpr uintptr_t kStoreCamera = 0x481260;     // SGame (): local player's start cell = view centre
constexpr uintptr_t kStoreCameraCall = 0x480da9;  // in SaveGame
constexpr uintptr_t kQueueScene = 0x4709a0;      // cdecl (cmd, id, delay)
constexpr uintptr_t kSceneSend = 0x471560;       // cdecl (scene id, what, value)
constexpr uintptr_t kStartModel = 0x470780;      // cdecl (0x32 single player)
constexpr uintptr_t kAddTextButton = 0x495cb0;   // Scene (text, cmd, hotkey) -> WTextButton2*
constexpr uintptr_t kSetStyle = 0x49a500;        // WTextButton2 (normal, hover)
constexpr uintptr_t kSetText = 0x49a470;         // WTextButton2 (text)
constexpr uintptr_t kButtonDraw = 0x49a3a0;      // WTextButton2::Draw, no args
constexpr uintptr_t kButtonVtbl = 0x50925c;
constexpr uintptr_t kClickSound = 0x46fa70;      // cdecl ()
constexpr uintptr_t kClearSelection = 0x44b680;  // AThingRegistry (short saveToPrevious)
constexpr uintptr_t kPlaceCreature = 0x4472d0;   // AThingRegistry (player, type, x, y, 4 genes) -> id
constexpr uintptr_t kRadarShown = 0x433e10, kRadarHit = 0x4335b0;  // Minimap short(), short(x, y)
constexpr uintptr_t kToWorldX = 0x47ec60, kToWorldY = 0x47ec80;    // SGame int(screen coord)
constexpr uintptr_t kInPanel = 0x4497d0;         // AThingRegistry short(sx, sy): on the info panel
constexpr uintptr_t kMultiSetupStart = 0x48ce30; // SMultiSetup::OnCommand(widget)

// SGame vtable slots (input).
constexpr uintptr_t kSlotLDown = game::VtblSGame + 0x1c, kSlotLUp = game::VtblSGame + 0x20,
                    kSlotRDown = game::VtblSGame + 0x24, kSlotRUp = game::VtblSGame + 0x28,
                    kSlotMove = game::VtblSGame + 0x2c, kSlotDbl = game::VtblSGame + 0x30;

// SGame fields.
constexpr uintptr_t kUpdateMode = 0x460;  // int: 1 = paused
constexpr uintptr_t kSavePending = 0x86, kSavePath = 0x88;  // short; char[256]
constexpr uintptr_t kMinimapDrag = 0x6c;
// Minimap fields: +0x962c counts ticks without a radar building (shown while < 20), +0x9624 on.
constexpr uintptr_t kRadarAbsent = 0x962c, kRadarOn = 0x9624;
// APlayer fields.
constexpr uintptr_t kFog = 0x484c, kRace = 0x4878, kIndex = 0x4;

// Scene vtables.
constexpr uintptr_t kVtblMainMenu = 0x508304, kVtblMultiSetup = 0x508214;
constexpr uintptr_t kMultiSetupStartButton = 0x28fc;

template <typename T>
T& F(uintptr_t p, uintptr_t off) {
    return *(T*)(p + off);
}

// --- the terrain ---------------------------------------------------------------

class EngineGrid : public Grid {
public:
    int Width() const override { return std::min(kGridSize, game::at<uint8_t>(game::MapTilesW) / 2); }
    int Height() const override { return std::min(kGridSize, game::at<uint8_t>(game::MapTilesH) / 2); }
    uint16_t Get(int x, int y) const override { return ((uint16_t*)kGridCells)[y * kGridSize + x]; }
    void Set(int x, int y, uint16_t t) override { ((uint16_t*)kGridCells)[y * kGridSize + x] = t; }
};
EngineGrid g_grid;

// A copy of the map's cells, for trying a stroke without touching the map (tests).
class CopyGrid : public Grid {
public:
    explicit CopyGrid(const Grid& g) : w_(g.Width()), h_(g.Height()), cells_(w_ * h_) {
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) cells_[y * w_ + x] = g.Get(x, y);
    }
    int Width() const override { return w_; }
    int Height() const override { return h_; }
    uint16_t Get(int x, int y) const override { return cells_[y * w_ + x]; }
    void Set(int x, int y, uint16_t t) override { cells_[y * w_ + x] = t; }

private:
    int w_, h_;
    std::vector<uint16_t> cells_;
};

using Snapshot = std::vector<uint16_t>;
Snapshot TakeSnapshot() { return Snapshot((uint16_t*)kGridCells, (uint16_t*)kGridCells + kGridSize * kGridSize); }

// Net change between a snapshot and now, inside the map.
Changes Diff(const Snapshot& before) {
    Changes c;
    for (int y = 0; y < g_grid.Height(); ++y)
        for (int x = 0; x < g_grid.Width(); ++x) {
            uint16_t a = before[y * kGridSize + x], b = g_grid.Get(x, y);
            if (a != b) c.push_back({(int16_t)x, (int16_t)y, a, b});
        }
    return c;
}

void Redraw() {
    ((void(__thiscall*)(uintptr_t, int, int))kMarkAllDirty)(kGrid, 1, 0);
    ((void(__thiscall*)(uintptr_t))kMinimapRefresh)(kRadar);
}

// --- state -----------------------------------------------------------------------

enum Phase { kIdle, kNaming, kProcessing, kSaving };

struct State {
    bool pending = false;  // a map is loading for the editor
    bool active = false;
    bool wePaused = false;
    bool isNew = false;
    NewMapSpec spec;       // New Map
    MapEntry map;          // what was opened
    std::string title;
    std::unique_ptr<Painter> painter;
    int type = 0, brush = 1, page = 0;
    int baseType = -1;  // New Map's base terrain (tests)
    Phase phase = kIdle;
    std::string name;      // Save As name being typed
    bool testAfter = false, replaceArmed = false, exitArmed = false;
    std::string savePath, lastSaved;
    int saves = 0;
    bool saveDone = false, inSave = false;
    DWORD phaseStart = 0;
    // Strokes and undo.
    bool stroking = false;
    int lastX = -1, lastY = -1;
    bool fill = false;
    int fillX = 0, fillY = 0;
    Snapshot strokeBefore, entry;
    std::vector<Changes> undo, redo;
    size_t savedAt = 0;  // undo depth when last saved
    bool dirtySinceSave = false;
    std::string status;
    short fogWas = 1;
    bool ctrl = false, shift = false;
    DWORD openAt = 0;
} g;

// Test Map, after the editor has gone.
struct TestRun {
    int stage = 0;
    std::string file;
    DWORD at = 0;
} g_test;
bool g_returnToList;

uintptr_t Game() { return kGame; }
uintptr_t World() { return *(uintptr_t*)kWorld; }

void SetStatus(const std::string& s, bool log = true) {
    if (s != g.status && log) g_h.log("editor: %s", s.c_str());
    g.status = s;
}

bool Dirty() { return g.dirtySinceSave || g.undo.size() != g.savedAt; }

// --- panel ---------------------------------------------------------------------

enum Kind { kTitle, kLabel, kPal, kPrev, kNext, kBrushLabel, kBrush, kUndo, kRedo, kSaveAs, kTest, kExit, kStatus };
struct Item {
    void* b;
    Kind kind;
    int arg;    // palette row / brush size
    int row;
    int x;      // left, panel relative
    bool clickable;
};
std::vector<Item> g_items;
uintptr_t g_vtbl[40];  // our copy of the WTextButton2 vtable
constexpr int kPanelX = 10, kPanelW = 250, kRowH = 16, kRows = 19, kPalRows = 12, kBottomMargin = 10;
constexpr int kStatusChars = 32;  // per status row (the panel is about 35 of the game's capitals wide)
constexpr int kStyle = 25, kStyleLit = 0x1b;  // WBMd14 white / yellow
int g_dark = -1;    // darkest palette index (the panel's background)
int g_yellow = -1;  // the mission palette's nearest to the menus' highlight yellow
// Text colours: 32 colour objects at 0x550910, +4 = 0x80000000 | palette index. Style
// 0x1b (WBMd14, colour 1) is yellow in the menus' palette but not in a mission's, so a lit
// item draws with colour 1 pointed at g_yellow for the moment.
constexpr uintptr_t kTextColours = 0x550910;

int PanelTop() { return game::at<short>(game::ScreenBottom) - kBottomMargin - kRows * kRowH; }

short* Rect(void* b) { return (short*)((char*)b + 0xc); }

bool InItem(const Item& it, int x, int y) {
    short* r = Rect(it.b);
    return x >= r[0] && x < std::max<int>(r[2], r[0] + 8) && y >= r[1] - 1 && y < r[3] + 1;
}

const Item* ItemAt(int x, int y) {
    for (const Item& it : g_items)
        if (it.clickable && InItem(it, x, y)) return &it;
    return nullptr;
}

bool OnPanel(int x, int y) {
    if (g_items.empty()) return false;
    int top = PanelTop();
    return x >= kPanelX - 6 && x < kPanelX + kPanelW && y >= top - 4 && y < top + kRows * kRowH + 4;
}

void FindColours() {
    const render::Frame& f = render::CurrentFrame();
    int best = 1, bestSum = 1 << 30, yellow = -1, yellowD = 1 << 30;
    for (int i = 1; i < 256; ++i) {
        const RGBQUAD& c = f.palette[i];
        int s = c.rgbRed + c.rgbGreen + c.rgbBlue;
        if (s < bestSum) bestSum = s, best = i;
        int dr = c.rgbRed - 255, dg = c.rgbGreen - 210, db = c.rgbBlue - 40, d = dr * dr + dg * dg + db * db;
        if (d < yellowD) yellowD = d, yellow = i;
    }
    g_dark = best;
    g_yellow = yellow;
}

// The panel's background, straight into the interface layer's port (only
// while it is being drawn), when the title (the first of the panel's widgets)
// draws, so every text lands on top of it.
void Background() {
    if (!layers::Active() || g_dark < 0) return;
    int y0 = PanelTop() - 6, y1 = PanelTop() + kRows * kRowH + 2;
    for (int y = y0; y < y1; ++y) {
        uintptr_t a = layers::InterfacePixelAddress(kPanelX - 6, y);
        uintptr_t b = layers::InterfacePixelAddress(kPanelX + kPanelW - 1, y);
        if (a && b && b > a) memset((void*)a, g_dark, b - a + 1);
    }
}

void __thiscall ItemDraw(void* b) {
    const Item* me = nullptr;
    for (const Item& it : g_items)
        if (it.b == b) me = &it;
    if (me && me->kind == kTitle) Background();
    bool hover = me && me->clickable && InItem(*me, game::at<short>(game::CursorX), game::at<short>(game::CursorY));
    int* normal = (int*)((char*)b + 0x6c);
    int saved = *normal;
    if (hover) *normal = *(int*)((char*)b + 0x70);
    uint32_t* lit = *normal == kStyleLit && g_yellow > 0 ? ((uint32_t**)kTextColours)[1] : nullptr;
    uint32_t was = lit ? lit[1] : 0;
    if (lit) lit[1] = 0x80000000u | (uint32_t)g_yellow;
    ((void(__thiscall*)(void*))kButtonDraw)(b);
    if (lit) lit[1] = was;
    *normal = saved;
}

void SetText(void* b, const std::string& s) {
    char buf[60];
    snprintf(buf, sizeof buf, "%s", s.c_str());
    if (strcmp((char*)b + 0x2c, buf) != 0) ((void(__thiscall*)(void*, const char*))kSetText)(b, buf);
}

void SetLit(void* b, bool lit) {
    if (*(int*)((char*)b + 0x6c) != (lit ? kStyleLit : kStyle))
        ((void(__thiscall*)(void*, int, int))kSetStyle)(b, lit ? kStyleLit : kStyle, kStyleLit);
}

void AddItem(Kind kind, int arg, int row, int x, const char* text, bool clickable) {
    void* b = ((void*(__thiscall*)(uintptr_t, const char*, int, int))kAddTextButton)(Game(), text, 0x300 + (int)g_items.size(), 0);
    if (!b) return;
    ((void(__thiscall*)(void*, int, int))kSetStyle)(b, kStyle, clickable ? kStyleLit : kStyle);
    *(uintptr_t*)b = (uintptr_t)g_vtbl;
    g_items.push_back({b, kind, arg, row, x, clickable});
}

void BuildPanel() {
    g_items.clear();
    memcpy(g_vtbl, (void*)kButtonVtbl, sizeof g_vtbl);
    g_vtbl[1] = (uintptr_t)&ItemDraw;
    AddItem(kTitle, 0, 0, 0, "Map Editor", false);
    AddItem(kLabel, 0, 1, 0, "Terrain", false);
    AddItem(kPrev, 0, 1, 150, "< Prev", true);
    AddItem(kNext, 0, 1, 205, "Next >", true);
    for (int i = 0; i < kPalRows; ++i) AddItem(kPal, i, 2 + i, 12, "", true);
    constexpr int kTools = 2 + kPalRows;  // the rows under the palette
    AddItem(kBrushLabel, 0, kTools, 0, "Brush", false);
    static const int kSizes[] = {1, 2, 3, 5};
    for (int i = 0; i < 4; ++i) AddItem(kBrush, kSizes[i], kTools, 70 + i * 30, std::to_string(kSizes[i]).c_str(), true);
    AddItem(kUndo, 0, kTools + 1, 0, "Undo", true);
    AddItem(kRedo, 0, kTools + 1, 70, "Redo", true);
    AddItem(kSaveAs, 0, kTools + 2, 0, "Save As", true);
    AddItem(kTest, 0, kTools + 2, 85, "Test Map", true);
    AddItem(kExit, 0, kTools + 2, 180, "Exit", true);
    AddItem(kStatus, 0, kTools + 3, 0, "", false);
    AddItem(kStatus, 1, kTools + 4, 0, "", false);
    g_h.log("editor: panel built (%zu items)", g_items.size());
}

// Status row `i` (0 or 1): the status word-wrapped onto two rows.
std::string StatusRow(int i) {
    const std::string& s = g.status;
    if ((int)s.size() <= kStatusChars) return i == 0 ? s : std::string();
    size_t cut = s.rfind(' ', kStatusChars);
    if (cut == std::string::npos) cut = kStatusChars;
    return i == 0 ? s.substr(0, cut) : s.substr(s[cut] == ' ' ? cut + 1 : cut);
}

// Every frame: positions (the frame size changes with the zoom; the panel
// keeps to the bottom-left corner) and texts.
void UpdatePanel() {
    int top = PanelTop();
    int pages = std::max(1, (g.painter->TypeCount() + kPalRows - 1) / kPalRows);
    g.page = std::clamp(g.page, 0, pages - 1);
    for (Item& it : g_items) {
        short* r = Rect(it.b);
        short want[2] = {(short)(kPanelX + it.x), (short)(top + it.row * kRowH)};
        if (r[0] != want[0] || r[1] != want[1]) {
            int w = r[2] - r[0];
            r[0] = want[0];
            r[1] = want[1];
            r[2] = (short)(want[0] + std::max(w, 8));
            r[3] = (short)(want[1] + kRowH - 2);
        }
        switch (it.kind) {
        case kTitle: SetText(it.b, "Map Editor: " + g.title + (Dirty() ? " *" : "")); break;
        case kLabel: SetText(it.b, pages > 1 ? "Terrain " + std::to_string(g.page + 1) + "/" + std::to_string(pages) : "Terrain"); break;
        case kPal: {
            int t = g.page * kPalRows + it.arg;
            bool shown = t < g.painter->TypeCount();
            SetText(it.b, shown ? g.painter->TypeName(t) : "");
            it.clickable = shown;
            SetLit(it.b, shown && t == g.type);
            break;
        }
        case kPrev: it.clickable = g.page > 0; SetText(it.b, it.clickable ? "< Prev" : ""); break;
        case kNext: it.clickable = g.page + 1 < pages; SetText(it.b, it.clickable ? "Next >" : ""); break;
        case kBrush: SetLit(it.b, g.brush == it.arg); break;
        case kUndo: SetLit(it.b, false); SetText(it.b, g.undo.empty() ? "" : "Undo"); it.clickable = !g.undo.empty(); break;
        case kRedo: SetText(it.b, g.redo.empty() ? "" : "Redo"); it.clickable = !g.redo.empty(); break;
        case kSaveAs: SetText(it.b, g.phase == kNaming ? "Save" : "Save As"); SetLit(it.b, g.phase == kNaming); break;
        case kTest: SetText(it.b, g.phase == kNaming ? "Cancel" : "Test Map"); break;
        case kExit: SetText(it.b, g.exitArmed ? "Exit?" : "Exit"); SetLit(it.b, g.exitArmed); break;
        case kStatus: SetText(it.b, StatusRow(it.arg)); break;
        default: break;
        }
        *(short*)((char*)it.b + 0x1a) = 1;  // redraw
    }
}

// --- painting --------------------------------------------------------------------

bool CellAt(int sx, int sy, int& cx, int& cy) {
    int wx = (short)((int(__thiscall*)(uintptr_t, int))kToWorldX)(Game(), (short)sx);
    int wy = (short)((int(__thiscall*)(uintptr_t, int))kToWorldY)(Game(), (short)sy);
    cx = wx >> 6;
    cy = wy >> 6;
    return g_grid.Inside(cx, cy);
}

void PaintAt(int cx, int cy) {
    Changes ch;
    if (!g.painter->Paint(g_grid, g.type, g.brush, cx, cy, ch)) SetStatus(g.painter->Message());
    if (!ch.empty()) Redraw();
}

// Paint along the line from the last painted cell, so a fast drag leaves no gaps.
void PaintTo(int cx, int cy) {
    if (g.lastX < 0) {
        PaintAt(cx, cy);
    } else {
        int dx = abs(cx - g.lastX), dy = abs(cy - g.lastY), n = std::max(dx, dy);
        for (int i = 1; i <= n; ++i)
            PaintAt(g.lastX + (cx - g.lastX) * i / n, g.lastY + (cy - g.lastY) * i / n);
    }
    g.lastX = cx;
    g.lastY = cy;
}

void BeginStroke() {
    g.strokeBefore = TakeSnapshot();
    g.stroking = true;
    g.lastX = g.lastY = -1;
}

void EndStroke(const char* what) {
    g.stroking = false;
    Changes net = Diff(g.strokeBefore);
    if (net.empty()) return;
    g.undo.push_back(net);
    if (g.undo.size() > 200) {
        g.undo.erase(g.undo.begin());
        g.dirtySinceSave = true;  // the saved state can no longer be reached by undo
    }
    g.redo.clear();
    SetStatus(std::string(what) + ": " + std::to_string(net.size()) + " cells");
    g.exitArmed = false;
}

void Apply(const Changes& c, bool forward) {
    for (const CellChange& x : c) g_grid.Set(x.x, x.y, forward ? x.after : x.before);
    Redraw();
}

void Undo() {
    if (g.undo.empty() || g.stroking) return;
    Apply(g.undo.back(), false);
    g.redo.push_back(g.undo.back());
    g.undo.pop_back();
    SetStatus("Undone (" + std::to_string(g.redo.back().size()) + " cells)");
}

void Redo() {
    if (g.redo.empty() || g.stroking) return;
    Apply(g.redo.back(), true);
    g.undo.push_back(g.redo.back());
    g.redo.pop_back();
    SetStatus("Redone (" + std::to_string(g.undo.back().size()) + " cells)");
}

// --- saving --------------------------------------------------------------------

std::string BaseName(const std::string& file) {
    std::string s = file.substr(file.find_last_of("\\/") + 1);
    return s.substr(0, s.find_last_of('.'));
}

bool FileExists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string SuggestName() {
    if (!g.lastSaved.empty()) return BaseName(g.lastSaved);
    if (!g.isNew && !g.map.shipped && !g.map.file.empty()) return BaseName(g.map.file);
    const char* stem = g.isNew ? "NEWMAP" : "MYMAP";
    for (int i = 1; i < 100; ++i) {
        std::string n = stem + std::to_string(i);
        if (!FileExists(g_h.dir + "DATA\\MAPS\\" + n + ".WBM")) return n;
    }
    return stem;
}

void ShowNaming() { SetStatus("Name: " + g.name + "_   Enter saves"); }

void StartNaming(bool test) {
    g.phase = kNaming;
    g.testAfter = test;
    g.replaceArmed = false;
    g.name = SuggestName();
    ShowNaming();
}

// Run the guide build and start connecting its points.
void StartProcessing() {
    uintptr_t ita = *(uintptr_t*)kTerrainAnalysis;
    if (!ita) {
        SetStatus("No terrain analysis; can't save");
        g.phase = kIdle;
        return;
    }
    // The tile animator keeps cell positions of the lava it animates and restores their
    // ids when stopped (the guide build and the saver both stop it): re-read them first.
    ((void(__thiscall*)(uintptr_t))kBuildAnimList)(kGrid);
    ((void(__thiscall*)(uintptr_t))kAnimStop)(kTileAnimator);
    ((void(__thiscall*)(uintptr_t))kGuideRebuild)(ita);
    g.phase = kProcessing;
    g.phaseStart = GetTickCount();
    g_h.log("editor: processing map, %u guide points", game::at<uint32_t>(kGuidePointCount));
}

void StepProcessing() {
    uintptr_t guide = *(uintptr_t*)kTerrainAnalysis + 0x1c;
    uint32_t n = game::at<uint32_t>(kGuidePointCount);
    DWORD t0 = GetTickCount();
    while (F<uint16_t>(guide, 0x24) < n && GetTickCount() - t0 < 40)
        ((void(__thiscall*)(uintptr_t))kGuideConnect)(guide);
    if (F<uint16_t>(guide, 0x24) < n) {
        SetStatus("Processing map " + std::to_string(F<uint16_t>(guide, 0x24) * 100 / n) + "%", false);
        return;
    }
    // ConnectStep finishes (prune, per-tile lookup) on the call after the last pair, unless
    // the last pair was the 200th of a call: then that call never comes.
    if (n > 0 && ((uint64_t)n * n) % 200 == 0) {
        ((void(__fastcall*)(uintptr_t))kGuidePrune)(guide);
        ((void(__fastcall*)(uintptr_t))kGuideFill)(guide);
    }
    g_h.log("editor: map processed in %lu ms (%u guide points)", GetTickCount() - g.phaseStart, n);
    // The stock deferred save: SGame::Tick calls SaveGame(+0x88) when +0x86 is set.
    char* path = (char*)(Game() + kSavePath);
    snprintf(path, 256, "%s", g.savePath.c_str());
    g.saveDone = false;
    F<short>(Game(), kSavePending) = 1;
    g.phase = kSaving;
    SetStatus("Saving...");
}

bool Smart();
std::vector<int> TilesOf(int t);
std::string Joined(const std::vector<int>& v);

void WriteDebugDiff() {
    FILE* f = fopen((g_h.dir + "WarBreedsHD-editor-save.txt").c_str(), "w");
    if (!f) return;
    fprintf(f, "file %s\nsize %d %d\nnew %d\npainter %s\nbase %s\n", g.savePath.c_str(), g_grid.Width(),
            g_grid.Height(), g.isNew ? 1 : 0, Smart() ? "smart" : "raw", Joined(TilesOf(g.baseType)).c_str());
    for (const CellChange& c : Diff(g.entry)) fprintf(f, "%d %d %u %u\n", c.x, c.y, c.before, c.after);
    fclose(f);
}

void StartTest(const std::string& path);

void FinishSave() {
    g.phase = kIdle;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(g.savePath.c_str(), GetFileExInfoStandard, &fa) || fa.nFileSizeLow == 0) {
        SetStatus("Save failed");
        return;
    }
    ++g.saves;
    g.lastSaved = g.savePath;
    g.savedAt = g.undo.size();
    g.dirtySinceSave = false;
    g.title = BaseName(g.savePath);
    g_h.log("editor: saved %s (%lu bytes)", g.savePath.c_str(), fa.nFileSizeLow);
    SetStatus("Saved " + BaseName(g.savePath) + ".WBM");
    if (g_h.debug) WriteDebugDiff();
    if (g.testAfter) StartTest(g.savePath);
}

// Name typed: check it and start processing.
void ConfirmName() {
    std::string n = g.name;
    if (n.empty()) return;
    if (IsShippedMapName(n)) {
        SetStatus(n + " is a built-in map; pick another name");
        return;
    }
    std::string path = g_h.dir + "DATA\\MAPS\\" + n + ".WBM";
    bool own = !g.lastSaved.empty() ? _stricmp(path.c_str(), g.lastSaved.c_str()) == 0
                                    : !g.isNew && !g.map.shipped && _stricmp(path.c_str(), g.map.path.c_str()) == 0;
    if (FileExists(path) && !own && !g.replaceArmed) {
        g.replaceArmed = true;
        SetStatus(n + " exists: Enter replaces it");
        return;
    }
    g.savePath = path;
    StartProcessing();
}

// --- the editor's lifetime ---------------------------------------------------------

void Teardown() {
    g.active = false;
    g.phase = kIdle;
    g.stroking = false;
    g_items.clear();  // the widgets go with SGame's own (Scene::Close)
    g.painter.reset();
    g.undo.clear();
    g.redo.clear();
}

void Leave(bool toList) {
    g_returnToList = toList;
    ((void(__cdecl*)(int, int, int))kQueueScene)(0x21, 0x10, 5);  // replace the root with the hub
    g_h.log("editor: leaving%s", toList ? " (back to the map list)" : "");
    Teardown();
}

void StartTest(const std::string& path) {
    g_test.stage = 1;
    g_test.file = path.substr(path.find_last_of("\\/") + 1);
    g_test.at = GetTickCount();
    g_h.log("editor: Test Map %s: to the skirmish setup", g_test.file.c_str());
    Leave(false);
}

// The painter behind the palette: the smart painter (terrain_paint.h, the
// original editor's transitions from DATA\TERRAIN.MHK), or the raw stamper if
// the tables can't be read.
// The game opens TERRAIN.MHK without sharing, so Install reads it at startup,
// before the game does; the tables are parsed when the editor first opens.
TerrainPainter g_terrain;
std::vector<uint8_t> g_terrainFile;
std::string g_terrainError;

std::unique_ptr<Painter> MakePainter() {
    if (!g_terrain.Loaded() && !g_terrainFile.empty()) {
        std::string error;
        if (g_terrain.LoadFromMemory(g_terrainFile.data(), g_terrainFile.size(), &error)) {
            g_terrainFile = std::vector<uint8_t>();
            g_terrain.Seed(GetTickCount());
            g_h.log("editor: smart painter: %d terrain kinds, %d smart terrains", g_terrain.KindCount(),
                    (int)g_terrain.SmartTerrains().size());
        } else {
            g_terrainError = error;
        }
        g_terrainFile = std::vector<uint8_t>();
    }
    if (g_terrain.Loaded()) return std::unique_ptr<Painter>(new SmartPainter(g_terrain));
    g_h.log("editor: smart painter unavailable (%s); using the raw painter", g_terrainError.c_str());
    return std::unique_ptr<Painter>(new RawPainter((const uint16_t*)kTileClass, kTileIds));
}

bool Smart() { return dynamic_cast<SmartPainter*>(g.painter.get()) != nullptr; }

// Palette entry by name (New Map's base terrain), or -1.
int TypeNamed(const char* name) {
    for (int t = 0; t < g.painter->TypeCount(); ++t)
        if (_stricmp(g.painter->TypeName(t).c_str(), name) == 0) return t;
    return -1;
}

// The tile a raw palette entry stamps (log and tests), -1 for other painters.
int StampTile(int t) {
    RawPainter* raw = dynamic_cast<RawPainter*>(g.painter.get());
    return raw ? raw->TileOf(t) : -1;
}

// Every tile a palette entry may leave in the middle of a stroke (tests): the
// raw stamp, or the smart painter's uniform variants.
std::vector<int> TilesOf(int t) {
    std::vector<int> out;
    if (!Smart()) {
        if (StampTile(t) >= 0) out.push_back(StampTile(t));
    } else if (t >= 0 && t < (int)g_terrain.SmartTerrains().size()) {
        int kind = g_terrain.UniformKind(g_terrain.SmartTerrains()[t]);
        if (kind != TerrainPainter::kNoKind)
            for (uint16_t id : g_terrain.KindTiles(kind)) out.push_back(id);
    }
    return out;
}

std::string Joined(const std::vector<int>& v) {
    std::string s;
    for (int id : v) s += (s.empty() ? "" : ",") + std::to_string(id);
    return s.empty() ? "-" : s;
}

void PlaceStartShamans() {
    uintptr_t reg = *(uintptr_t*)kPlayers;
    if (!reg || !World()) return;
    int made = 0;
    for (int i = 0; i < 8; ++i) {
        uintptr_t p = F<uintptr_t>(reg, 4 + i * 4);
        if (!p) continue;
        short cx = F<short>(p, 0x4a1c), cy = F<short>(p, 0x4a1e);  // start cell
        int type = F<uint16_t>(p, kRace) * 9;  // the race's Shaman
        unsigned short id = ((unsigned short(__thiscall*)(uintptr_t, int, int, int, int, int, int, int, int))kPlaceCreature)(
            World(), F<short>(p, kIndex), type, cx * 64 + 16, cy * 64 + 16, 100, 100, 100, 100);
        if (id == 0xc01)
            g_h.log("editor: no room for player %d's Shaman at cell %d,%d", i, cx, cy);
        else
            ++made;
    }
    g_h.log("editor: new map: %d starting Shamans placed", made);
}

void Enter() {
    g.pending = false;
    g.active = true;
    g.phase = kIdle;
    g.undo.clear();
    g.redo.clear();
    g.savedAt = 0;
    g.dirtySinceSave = g.isNew;
    g.saves = 0;
    g.lastSaved.clear();
    g.exitArmed = false;
    F<int>(Game(), kUpdateMode) = 1;  // (already set; the console's "update pause")
    if (World()) ((void(__thiscall*)(uintptr_t, int))kClearSelection)(World(), 0);
    ((void(__thiscall*)(uintptr_t))kAnimStop)(kTileAnimator);  // lava back to its base tiles
    g.painter = MakePainter();
    g.baseType = -1;
    g.type = 0;
    g.brush = 1;
    g.page = 0;
    if (char* me = *(char**)kLocalPlayer) {
        g.fogWas = *(short*)(me + kFog);
        *(short*)(me + kFog) = 0;
    }
    if (g.isNew) {
        int t = TypeNamed(RawPainter::BaseName(g.spec.terrain));
        Changes ch;
        if (t >= 0) {
            g.painter->Fill(g_grid, t, 0, 0, g_grid.Width() - 1, g_grid.Height() - 1, ch);
            g.type = t;
            g.baseType = t;
        }
        PlaceStartShamans();
        DeleteFileA((g_h.dir + "WarBreedsHD-newmap.wbm").c_str());
    }
    g.entry = TakeSnapshot();
    Redraw();
    FindColours();
    BuildPanel();
    std::string types;
    for (int i = 0; i < g.painter->TypeCount(); ++i)
        types += g.painter->TypeName(i) + "=" + std::to_string(StampTile(i)) + " ";
    g_h.log("editor: editing %s (%dx%d cells); palette %s", g.title.c_str(), g_grid.Width(), g_grid.Height(), types.c_str());
    SetStatus(g.isNew ? "New map" : "Left-drag paints, right-click picks");
}

void Open(const std::string& path) {
    // The single-player load path a saved game takes from the main menu.
    F<int>(Game(), kUpdateMode) = 1;  // paused from the first turn on
    g.wePaused = true;
    g.pending = true;
    g.openAt = GetTickCount();
    g.active = false;
    g_test.stage = 0;
    g_returnToList = false;
    ((void(__cdecl*)(int, int, int))kSceneSend)(2, 0, (int)(uintptr_t)path.c_str());  // map path (+0x1fe)
    ((void(__cdecl*)(int, int, int))kSceneSend)(2, 2, 0);  // not a skirmish
    ((void(__cdecl*)(int))kStartModel)(0x32);              // loading screen, then SGame
    g_h.log("editor: opening %s", path.c_str());
}

// --- input -----------------------------------------------------------------------

typedef short(__thiscall* Mouse_t)(void* sg, int x, int y);
Mouse_t o_LDown, o_LUp, o_RDown, o_RUp, o_Move, o_Dbl;

bool OnMinimap(int x, int y) {
    return ((short(__thiscall*)(uintptr_t))kRadarShown)(kRadar) == 1 &&
           ((short(__thiscall*)(uintptr_t, int, int))kRadarHit)(kRadar, x, y) == 1;
}

void Click(const Item& it) {
    ((void(__cdecl*)())kClickSound)();
    if (g.phase == kProcessing || g.phase == kSaving) return;
    if (it.kind != kExit) g.exitArmed = false;
    if (g.phase == kNaming && it.kind != kSaveAs && it.kind != kTest) return;
    switch (it.kind) {
    case kPal: g.type = g.page * kPalRows + it.arg; SetStatus("Terrain: " + g.painter->TypeName(g.type)); break;
    case kPrev: --g.page; break;
    case kNext: ++g.page; break;
    case kBrush: g.brush = it.arg; SetStatus("Brush " + std::to_string(it.arg)); break;
    case kUndo: Undo(); break;
    case kRedo: Redo(); break;
    case kSaveAs:
        if (g.phase == kNaming)
            ConfirmName();
        else
            StartNaming(false);
        break;
    case kTest:
        if (g.phase == kNaming) {  // "Cancel"
            g.phase = kIdle;
            SetStatus("Not saved");
        } else if (!g.lastSaved.empty() && !Dirty()) {
            StartTest(g.lastSaved);
        } else {
            StartNaming(true);
        }
        break;
    case kExit:
        if (Dirty() && !g.exitArmed) {
            g.exitArmed = true;
            SetStatus("Unsaved changes: Exit again to discard");
        } else {
            Leave(true);
        }
        break;
    default: break;
    }
}

bool Editing() { return g.active && (uintptr_t)game::at<void*>(game::Scenes) == Game(); }

short __thiscall HookLDown(void* sg, int x, int y) {
    if (!Editing()) return o_LDown(sg, x, y);
    x = (short)x;
    y = (short)y;
    if (const Item* it = ItemAt(x, y)) {
        Click(*it);
        return 1;
    }
    if (OnPanel(x, y)) return 1;
    if (OnMinimap(x, y)) return o_LDown(sg, x, y);  // the camera jumps there
    if (((short(__thiscall*)(uintptr_t, int, int))kInPanel)(World(), x, y) == 1) return 1;
    if (g.phase != kIdle) return 1;
    int cx, cy;
    if (!CellAt(x, y, cx, cy)) return 1;
    g.exitArmed = false;
    BeginStroke();
    if (g.shift || GetKeyState(VK_SHIFT) < 0) {  // Shift: drag a rectangle to fill
        g.fill = true;
        g.fillX = cx;
        g.fillY = cy;
        SetStatus("Fill: drag the rectangle");
    } else {
        g.fill = false;
        PaintTo(cx, cy);
    }
    return 1;
}

short __thiscall HookMove(void* sg, int x, int y) {
    short r = o_Move(sg, x, y);  // edge scrolling, minimap drag
    if (Editing() && g.stroking && !g.fill) {
        int cx, cy;
        if (CellAt((short)x, (short)y, cx, cy)) PaintTo(cx, cy);
    }
    return r;
}

short __thiscall HookLUp(void* sg, int x, int y) {
    if (!Editing()) return o_LUp(sg, x, y);
    if (g.stroking) {
        if (g.fill) {
            int cx, cy;
            CellAt((short)x, (short)y, cx, cy);
            cx = std::clamp(cx, 0, g_grid.Width() - 1);
            cy = std::clamp(cy, 0, g_grid.Height() - 1);
            Changes ch;
            if (!g.painter->Fill(g_grid, g.type, g.fillX, g.fillY, cx, cy, ch)) SetStatus(g.painter->Message());
            if (!ch.empty()) Redraw();
            EndStroke("Filled");
        } else {
            EndStroke("Painted");
        }
        return 1;
    }
    if (F<short>((uintptr_t)sg, kMinimapDrag)) return o_LUp(sg, x, y);
    return 1;
}

short __thiscall HookDbl(void* sg, int x, int y) {
    if (!Editing()) return o_Dbl(sg, x, y);
    return HookLDown(sg, x, y);
}

short __thiscall HookRDown(void* sg, int x, int y) {
    if (!Editing()) return o_RDown(sg, x, y);
    x = (short)x;
    y = (short)y;
    int cx, cy;
    if (g.phase == kIdle && !g.stroking && !OnPanel(x, y) && !OnMinimap(x, y) && CellAt(x, y, cx, cy)) {
        int t = g.painter->Pick(g_grid, cx, cy);
        if (t >= 0) {
            g.type = t;
            g.page = t / kPalRows;
            SetStatus("Picked " + g.painter->TypeName(t) + " (tile " + std::to_string(g_grid.Get(cx, cy)) + ")");
        }
    }
    return 1;  // no orders in the editor
}

short __thiscall HookRUp(void* sg, int x, int y) {
    if (!Editing()) return o_RUp(sg, x, y);
    return 1;
}

// SGame::Tick's call to SaveGame: the fog flag goes back to the map's value for
// the write, and SaveGame's ForceRedraw runs a frame tick inside: the editor's
// tick must stay out of the way until the file is written.
void __thiscall HookSaveGame(void* sg, char* path) {
    bool ours = g.active && g.phase == kSaving;
    char* me = *(char**)kLocalPlayer;
    if (ours && me) *(short*)(me + kFog) = g.fogWas;
    g.inSave = true;
    ((void(__thiscall*)(void*, char*))kSaveGame)(sg, path);
    g.inSave = false;
    if (ours) {
        if (me) *(short*)(me + kFog) = 0;
        g.saveDone = true;
    }
}

// SaveGame stores the view centre as the local player's start cell; an editor
// save keeps the map's own start cell.
void __thiscall HookStoreCamera(void* sg) {
    if (g.active) return;
    ((void(__thiscall*)(void*))kStoreCamera)(sg);
}

bool PatchSlot(uintptr_t slot, void* hook, Mouse_t* orig) {
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *orig = *(Mouse_t*)slot;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

bool PatchCall(uintptr_t site, uintptr_t expect, void* hook) {
    uint8_t* p = (uint8_t*)site;
    if (p[0] != 0xe8 || site + 5 + *(int32_t*)(p + 1) != expect) return false;
    DWORD old;
    if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)hook - (site + 5));
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// --- Test Map: from the hub to a started skirmish ----------------------------------

uintptr_t TopScene() {
    short top = game::at<short>(game::SceneTop);
    return top < 0 || top > 8 ? 0 : (&game::at<uintptr_t>(game::Scenes))[top];
}

void TickTest() {
    DWORD now = GetTickCount();
    uintptr_t top = TopScene();
    uintptr_t vt = top ? *(uintptr_t*)top : 0;
    bool idle = game::at<short>(kSceneCmdPending) == 0;
    if (g_test.stage == 1 && vt == kVtblMainMenu && idle) {
        // What the main menu's Skirmish item does (0x48ff40, item 0x65).
        ((void(__cdecl*)(int, int, int))kSceneSend)(5, 1, 0);
        ((void(__cdecl*)(int, int, int))kQueueScene)(0x21, 5, 5);
        g_test.stage = 2;
        g_test.at = now;
        g_h.log("editor: Test Map: opening the skirmish setup");
    } else if (g_test.stage == 2 && vt == kVtblMultiSetup && idle && now - g_test.at > 1500) {
        ((void(__cdecl*)(int, int, int))kSceneSend)(5, 0, (int)(uintptr_t)g_test.file.c_str());  // the map
        g_test.stage = 3;
        g_test.at = now;
        g_h.log("editor: Test Map: map %s chosen", g_test.file.c_str());
    } else if (g_test.stage == 3 && vt == kVtblMultiSetup && idle && now - g_test.at > 1500) {
        void* start = *(void**)(top + kMultiSetupStartButton);
        g_h.log("editor: Test Map: Start");
        g_test.stage = 0;
        if (start) ((void(__thiscall*)(uintptr_t, void*))kMultiSetupStart)(top, start);
    } else if (g_test.stage && now - g_test.at > 30000) {
        g_h.log("editor: Test Map: gave up (stage %d)", g_test.stage);
        g_test.stage = 0;
    }
}

}  // namespace

// --- public ----------------------------------------------------------------------------

void Install(const Hooks& h) {
    g_h = h;
    bool ok = PatchSlot(kSlotLDown, (void*)&HookLDown, &o_LDown) && PatchSlot(kSlotLUp, (void*)&HookLUp, &o_LUp) &&
              PatchSlot(kSlotRDown, (void*)&HookRDown, &o_RDown) && PatchSlot(kSlotRUp, (void*)&HookRUp, &o_RUp) &&
              PatchSlot(kSlotMove, (void*)&HookMove, &o_Move) && PatchSlot(kSlotDbl, (void*)&HookDbl, &o_Dbl);
    bool save = PatchCall(kSaveGameCall, kSaveGame, (void*)&HookSaveGame) &&
                PatchCall(kStoreCameraCall, kStoreCamera, (void*)&HookStoreCamera);
    h.log("editor: %s, save hooks %s", ok ? "installed" : "FAILED", save ? "installed" : "FAILED (unexpected code)");
    if (!ReadArchive(h.dir + "DATA\\TERRAIN.MHK", g_terrainFile, &g_terrainError)) g_terrainFile.clear();
}

void OpenMap(const MapEntry& map) {
    g.isNew = false;
    g.map = map;
    g.title = BaseName(map.file);
    Open(map.path);
}

void NewMap(const NewMapSpec& spec) {
    g.isNew = true;
    g.spec = spec;
    g.map = MapEntry();
    std::string path = g_h.dir + "WarBreedsHD-newmap.wbm";
    if (path.size() > 250) path = g_h.dir + "DATA\\MAPS\\~NEWMAP.WBM";
    // Tile 0 for now; the editor fills the map with the chosen terrain when it opens.
    if (!WriteBlankMap(path, spec, 0, "New Map")) {
        g_h.log("editor: cannot write %s", path.c_str());
        return;
    }
    g.title = "New " + std::to_string(spec.width) + "x" + std::to_string(spec.height);
    g_h.log("editor: new map %dx%d, %d players, %s", spec.width, spec.height, spec.players,
            RawPainter::BaseName(spec.terrain));
    Open(path);
}

void Tick() {
    if (g.inSave) return;  // inside SaveGame's own redraw
    uintptr_t base = game::at<short>(game::SceneTop) >= 0 ? game::at<uintptr_t>(game::Scenes) : 0;
    bool sgameRoot = base == Game();
    if (g.pending && sgameRoot && game::at<short>(game::SceneTop) == 0 && *(uintptr_t*)kLocalPlayer) Enter();
    if (g.pending && base == kHub && GetTickCount() - g.openAt > 5000) {  // the load never started
        g_h.log("editor: the map did not load");
        g.pending = false;
    }
    if (g.active && !sgameRoot) {
        g_h.log("editor: the mission scene has gone");
        Teardown();
    }
    if (!g.active && !g.pending && g.wePaused && !sgameRoot) {
        F<int>(Game(), kUpdateMode) = 0;  // the next mission runs normally
        g.wePaused = false;
    }
    if (g_test.stage) TickTest();
    if (!g.active) return;
    F<int>(Game(), kUpdateMode) = 1;
    F<int>(kRadar, kRadarAbsent) = 0;  // the minimap stays on (in a mission it needs a radar building)
    F<short>(kRadar, kRadarOn) = 1;
    if (g.phase == kProcessing) StepProcessing();
    if (g.phase == kSaving && g.saveDone) FinishSave();
    if (g.active && !g_items.empty()) UpdatePanel();
}

bool Active() { return g.active; }

bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (!Editing()) return false;
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        if (wp == VK_CONTROL) g.ctrl = msg == WM_KEYDOWN;
        if (wp == VK_SHIFT) g.shift = msg == WM_KEYDOWN;
        if (wp >= VK_LEFT && wp <= VK_DOWN) return false;  // camera
    }
    if (msg == WM_KEYDOWN) {
        bool ctrl = g.ctrl || GetKeyState(VK_CONTROL) < 0;
        if (g.phase == kNaming) {
            if (wp == VK_RETURN) ConfirmName();
            else if (wp == VK_ESCAPE) { g.phase = kIdle; SetStatus("Not saved"); }
            else if (wp == VK_BACK && !g.name.empty()) { g.name.pop_back(); g.replaceArmed = false; ShowNaming(); }
        } else if (ctrl && wp == 'Z') {
            Undo();
        } else if (ctrl && wp == 'Y') {
            Redo();
        }
        return true;
    }
    if (msg == WM_CHAR) {
        if (g.phase == kNaming) {
            char c = (char)toupper((int)wp);
            if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') && g.name.size() < 8) {
                g.name += c;
                g.replaceArmed = false;
                ShowNaming();
            }
        }
        return true;
    }
    return msg == WM_KEYUP || msg == WM_SYSCHAR;
}

bool TakeReturnToMapList() {
    bool r = g_returnToList;
    g_returnToList = false;
    return r;
}

std::string Status() { return g.status; }
bool Idle() { return Editing() && g.phase == kIdle && !g.stroking; }
int SaveCount() { return g.saves; }
int TestStage() { return g_test.stage; }

int TypeTile(const char* name) {
    if (!g.painter) return -1;
    for (int t = 0; t < g.painter->TypeCount(); ++t)
        if (g.painter->TypeName(t) == name) return StampTile(t);
    return -1;
}

bool StrokeWorks(const char* name, int size, int x0, int y0, int x1, int y1, std::string* why) {
    if (!g.painter) return false;
    int t = -1;
    for (int i = 0; i < g.painter->TypeCount(); ++i)
        if (g.painter->TypeName(i) == name) t = i;
    if (t < 0) return false;
    CopyGrid grid(g_grid);
    Changes ch;
    int n = std::max(abs(x1 - x0), abs(y1 - y0));
    for (int i = 0; i <= n; ++i)
        if (!g.painter->Paint(grid, t, size, n ? x0 + (x1 - x0) * i / n : x0, n ? y0 + (y1 - y0) * i / n : y0, ch)) {
            if (why) *why = g.painter->Message();
            return false;
        }
    return !ch.empty();
}

std::string TypeTiles(const char* name) {
    if (!g.painter) return "-";
    for (int t = 0; t < g.painter->TypeCount(); ++t)
        if (g.painter->TypeName(t) == name) return Joined(TilesOf(t));
    return "-";
}

std::string TypeName() { return g.painter ? g.painter->TypeName(g.type) : std::string(); }

bool CellCentre(int cx, int cy, POINT& out) {
    if (!Editing()) return false;
    int f = F<short>(Game(), game::SGameZoomedOut) == 1 ? 2 : 1;
    out.x = (cx * 64 + 32 - F<short>(Game(), game::SGameCameraX)) / f;
    out.y = (cy * 64 + 32 - F<short>(Game(), game::SGameCameraY)) / f;
    return out.x >= 0 && out.y >= 0 && out.x < game::at<short>(game::ScreenRight) &&
           out.y < game::at<short>(game::ScreenBottom);
}

}  // namespace editor
