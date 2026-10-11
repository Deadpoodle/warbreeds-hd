// In-game map editor: the shell (re/editor-shell.md) and, from Stage 2, the
// things, units, players and map settings (re/editor-stage2.md).
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
// The tools panel is a pool of the game's own text buttons (WTextButton2),
// added to SGame's widget list: SGame draws them inside the interface phase,
// so they land in WarBreeds HD's interface layer and keep one size at every
// zoom, like the info panel. They get their own copy of the button vtable
// with a Draw that first paints a dark band behind the panel and does the
// hover highlight. Every frame the current tab lists the items it shows
// (tabs.h) and the pool takes them. Mouse input goes through SGame's own
// handlers (vtable slots), so coordinates are game frame pixels whatever the
// zoom: panel clicks are ours, the minimap keeps its stock behaviour, the map
// takes the current tab's tool.
//
// Tabs: Terrain (the Painter interface, painter.h: the smart painter, or the
// raw stamper), Things (trees, rocks, ruins, crops), Units (creatures and
// devices; select, move, delete, attributes), Players (type, race, CGK,
// clans, start point) and Map (name, fog, players). Things, players and the
// map header are the engine's own (world.h). Every change is one undo record
// of operations (terrain cells, a thing by value, a player, the map header)
// that apply forwards or backwards.
//
// Save: the guide graph is rebuilt with the game's own map processing
// (AITerrainAnalysis::Rebuild 0x4a8030, then AIMovementGuide::ConnectStep
// 0x4a5200 run here in time slices instead of 200 pairs per turn), the map
// is checked (validate.h, warnings only), then the stock deferred save writes
// the file (SGame +0x88 path, +0x86 pending -> SGame::SaveGame 0x480be0 in the
// next SGame tick).
#include "editor.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "../game.h"
#include "../layers.h"
#include "../render/render.h"
#include "raw_paint.h"
#include "tabs.h"
#include "terrain_paint.h"
#include "validate.h"
#include "world.h"
#include "ai_settings.h"  // Stage 3
#include "events.h"       // Stage 3

namespace editor {
namespace {

using world::kNoThing;
using world::MapRec;
using world::PlayerRec;
using world::ThingRec;

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
constexpr uintptr_t kMapInfo = 0x567578;         // the map header

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
constexpr uintptr_t kSetMapName = 0x432150;      // MapInfo (char*)
constexpr uintptr_t kSetMapNameCall = 0x480cd2;  // in SaveGame: the map is named after its file
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

// --- undo records -------------------------------------------------------------------

// One operation of an undo record. A thing is kept by value (ids change when a
// thing is put back) and named by a handle (below).
struct Op {
    enum Kind { kCells, kThing, kPlayer, kMap, kCustom } kind = kCells;
    Changes cells;                         // kCells
    int handle = 0;                        // kThing
    bool had = false, has = false;         // kThing: existed before / after
    ThingRec tb, ta;
    int player = 0;                        // kPlayer
    PlayerRec pb, pa;
    MapRec mb, ma;                         // kMap
    std::function<void(bool)> custom;      // kCustom
};

struct Edit {
    std::string what;      // "Painted: 12 cells", for the status line
    std::vector<Op> ops;   // applied in order; undone in reverse
    std::string merge;     // consecutive edits with the same key fold into one (hit point clicks)
};

// --- state -----------------------------------------------------------------------

enum Phase { kIdle, kNaming, kMapNaming, kProcessing, kSaving };
enum UnitMode { kModeCreatures, kModeDevices, kModeSelect };
enum Drag { kNoDrag, kDragStroke, kDragFill, kDragThings, kDragMove };

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
    std::string mapNameEdit;  // map name being typed
    bool testAfter = false, testArmed = false, replaceArmed = false, exitArmed = false;
    std::string savePath, lastSaved;
    int saves = 0;
    bool saveDone = false, inSave = false;
    DWORD phaseStart = 0;
    // Tabs.
    int tab = kTabTerrain;
    bool collapsed = false;     // the panel folded to a strip (Tab)
    bool showFindings = false;
    std::vector<Finding> findings;
    int thingItem = 0;          // Things tab: palette entry
    int unitMode = kModeCreatures, unitItem = 0, unitPage = 0;
    int player = 0;             // the player new units belong to (Units, Players)
    int selHandle = 0;          // Units: the selected thing (0 none)
    std::string mapName;        // "" = named after its file when saved (a new map)
    int fogMode = 0;
    // Mouse drags and undo.
    Drag drag = kNoDrag;
    int lastX = -1, lastY = -1;     // stroke: last cell; things: last tile
    int fillX = 0, fillY = 0;
    int downWX = 0, downWY = 0;     // move: where the button went down (world px)
    Snapshot strokeBefore, entry;
    Edit building;                  // the record a drag is filling
    std::string dragNote;           // the last refusal during a drag
    int dragCount = 0;
    std::vector<Edit> undo, redo;
    size_t savedAt = 0;  // undo depth when last saved
    bool dirtySinceSave = false;
    std::string status;
    short fogWas = 1;
    bool ctrl = false, shift = false;
    DWORD openAt = 0;
} g;

// The test skirmish (autopilot), after the editor has gone.
struct TestRun {
    int stage = 0;
    std::string file;
    DWORD at = 0;
} g_test;
bool g_returnToList;

TabPage* g_pages[kTabCount];  // modules' tabs (Events, AI)

uintptr_t Game() { return kGame; }
uintptr_t World() { return *(uintptr_t*)kWorld; }

void SetStatus(const std::string& s, bool log = true) {
    if (s != g.status && log) g_h.log("editor: %s", s.c_str());
    g.status = s;
}

bool Dirty() { return g.dirtySinceSave || g.undo.size() != g.savedAt || events::Dirty() || ai::Dirty(); }

// --- thing handles ---------------------------------------------------------------
// A handle names one thing for the undo records, whatever id the engine gives it
// when it is put back.

std::map<int, int> g_handleId, g_idHandle;
int g_nextHandle = 1;

int HandleOf(int id) {
    auto it = g_idHandle.find(id);
    if (it != g_idHandle.end()) return it->second;
    int h = g_nextHandle++;
    g_idHandle[id] = h;
    g_handleId[h] = id;
    return h;
}

int IdOf(int handle) {
    auto it = g_handleId.find(handle);
    return it == g_handleId.end() || !world::Thing(it->second) ? kNoThing : it->second;
}

void Bind(int handle, int id) {
    auto old = g_handleId.find(handle);
    if (old != g_handleId.end()) g_idHandle.erase(old->second);
    g_handleId[handle] = id;
    g_idHandle[id] = handle;
}

void Unbind(int handle) {
    auto it = g_handleId.find(handle);
    if (it == g_handleId.end()) return;
    g_idHandle.erase(it->second);
    g_handleId.erase(it);
}

// --- applying records --------------------------------------------------------------

std::string MapNameShown() { return g.mapName.empty() ? std::string("(the file name)") : g.mapName; }

MapRec CaptureMap() {
    MapRec m;
    m.name = g.mapName;
    m.supported = world::Supported();
    m.recommended = world::Recommended();
    m.fog = g.fogMode;
    return m;
}

void ApplyMap(const MapRec& m) {
    if (m.name != g.mapName) {
        g.mapName = m.name;
        if (!m.name.empty()) world::SetMapName(m.name);
    }
    world::SetSupported(m.supported);
    world::SetRecommended(m.recommended);
    if (m.fog != g.fogMode) {
        world::SetFogMode(m.fog, g.fogWas);
        g.fogMode = m.fog;
    }
}

void ApplyOp(const Op& op, bool forward) {
    switch (op.kind) {
    case Op::kCells:
        for (const CellChange& x : op.cells) g_grid.Set(x.x, x.y, forward ? x.after : x.before);
        break;
    case Op::kThing: {
        bool had = forward ? op.had : op.has, has = forward ? op.has : op.had;
        if (had) {
            world::Remove(IdOf(op.handle));
            Unbind(op.handle);
        }
        if (has) {
            const ThingRec& r = forward ? op.ta : op.tb;
            int id = world::Restore(r);
            if (id == kNoThing)
                g_h.log("editor: could not put back a %s at %d,%d", world::Name(r.type).c_str(), r.x, r.y);
            else
                Bind(op.handle, id);
        }
        break;
    }
    case Op::kPlayer: world::ApplyPlayer(op.player, forward ? op.pa : op.pb, g.fogMode); break;
    case Op::kMap: ApplyMap(forward ? op.ma : op.mb); break;
    case Op::kCustom:
        if (op.custom) op.custom(forward);
        break;
    }
}

// The game's selection stays empty in the editor (its info panel would cover
// the editor's in the top-right corner); the overlay marks the selected thing.
void ShowSelection() { world::Select(kNoThing); }

void Apply(const Edit& e, bool forward) {
    if (forward) {
        for (const Op& op : e.ops) ApplyOp(op, true);
    } else {
        for (auto it = e.ops.rbegin(); it != e.ops.rend(); ++it) ApplyOp(*it, false);
    }
    Redraw();
    ShowSelection();
}

void Push(Edit e) {
    if (e.ops.empty()) return;
    if (g_h.debug)
        if (int n = world::StaleGridEntries()) g_h.log("editor: DEBUG %d stale thing-grid entries after \"%s\"", n, e.what.c_str());
    g.exitArmed = g.testArmed = false;
    if (!e.merge.empty() && g.redo.empty() && !g.undo.empty() && g.undo.back().merge == e.merge &&
        g.undo.size() != g.savedAt) {
        // Fold into the last record: its "before" stays, the "after" is the new one.
        Edit& last = g.undo.back();
        if (last.ops.size() == e.ops.size()) {
            for (size_t i = 0; i < e.ops.size(); ++i) {
                last.ops[i].ta = e.ops[i].ta;
                last.ops[i].pa = e.ops[i].pa;
                last.ops[i].ma = e.ops[i].ma;
                last.ops[i].has = e.ops[i].has;
            }
            last.what = e.what;
            return;
        }
    }
    g.undo.push_back(std::move(e));
    if (g.undo.size() > 200) {
        g.undo.erase(g.undo.begin());
        g.dirtySinceSave = true;  // the saved state can no longer be reached by undo
        if (g.savedAt > 0) --g.savedAt;
    }
    g.redo.clear();
}

void Undo() {
    if (g.undo.empty() || g.drag != kNoDrag) return;
    Apply(g.undo.back(), false);
    g.redo.push_back(std::move(g.undo.back()));
    g.undo.pop_back();
    SetStatus("Undone: " + g.redo.back().what);
}

void Redo() {
    if (g.redo.empty() || g.drag != kNoDrag) return;
    Apply(g.redo.back(), true);
    g.undo.push_back(std::move(g.redo.back()));
    g.redo.pop_back();
    SetStatus("Redone: " + g.undo.back().what);
}

// Thing operations that go into a record.
int PutRecorded(Edit& e, int type, int owner, int wx, int wy, const ThingRec* attrs, std::string& why) {
    int id = world::Put(type, owner, wx, wy, attrs, why);
    if (id == kNoThing) return kNoThing;
    Op op;
    op.kind = Op::kThing;
    op.handle = HandleOf(id);
    op.has = true;
    world::Capture(id, op.ta);
    e.ops.push_back(op);
    return id;
}

bool RemoveRecorded(Edit& e, int id) {
    Op op;
    op.kind = Op::kThing;
    if (!world::Capture(id, op.tb)) return false;
    op.handle = HandleOf(id);
    op.had = true;
    world::Remove(id);
    Unbind(op.handle);
    e.ops.push_back(op);
    return true;
}

// The thing `id` becomes `to` (other attributes, or another place without the
// placement rules); false if it couldn't, and nothing changed.
bool ReplaceRecorded(Edit& e, int id, const ThingRec& to) {
    Op op;
    op.kind = Op::kThing;
    if (!world::Capture(id, op.tb)) return false;
    op.handle = HandleOf(id);
    world::Remove(id);
    int nid = world::Restore(to);
    if (nid == kNoThing) {
        Bind(op.handle, world::Restore(op.tb));
        return false;
    }
    Bind(op.handle, nid);
    op.had = op.has = true;
    world::Capture(nid, op.ta);
    e.ops.push_back(op);
    return true;
}

// --- panel ---------------------------------------------------------------------
// A pool of the game's text buttons; every frame the items of the title, the
// tab rows, the current page and the common rows are dealt out to it.

constexpr int kPanelW = kPanelWidth, kRowH = 16, kRows = 22, kMargin = 10;
constexpr int kStripW = 200;  // the collapsed panel: one row
constexpr int kPoolSize = 80;
constexpr int kCommonRow = kPageFirstRow + kPageRows;  // Undo/Redo/Check; then Save As..., then 2 status rows
constexpr int kStatusChars = 32;  // per status row (the panel is about 35 of the game's capitals wide)
constexpr int kStyle = 25, kStyleLit = 0x1b;  // WBMd14 white / yellow

// Item ids of the built-in rows: action * 100 + argument.
enum Action {
    kActNone = 0, kActTab, kActPal, kActPrev, kActNext, kActBrush, kActUndo, kActRedo, kActCheck, kActSaveAs,
    kActTest, kActExit, kActCloseCheck, kActThing, kActFill, kActClearCrops, kActUnitMode, kActUnit,
    kActUnitPrev, kActUnitNext, kActPlayerPrev, kActPlayerNext, kActHpDown, kActHpUp, kActRank, kActGizmo,
    kActDelete, kActType, kActRace, kActCgk, kActAvail, kActName, kActFog, kActSupDown, kActSupUp, kActRecDown,
    kActRecUp, kActCollapse,
};
int Id(Action a, int arg = 0) { return a * 100 + arg; }

struct Slot {
    void* b = nullptr;
    PanelItem it;       // row is absolute here
    bool used = false;
    bool page = false;  // a module page's item (its id goes to the page)
    bool clickable = false;
};
std::vector<Slot> g_pool;
uintptr_t g_vtbl[40];  // our copy of the WTextButton2 vtable
int g_lateMoves = 0;  // items the draw had to move (the frame size changed since the tick)
int g_dark = -1;    // darkest palette index (the panel's background)
int g_yellow = -1;  // the mission palette's nearest to the menus' highlight yellow
// Text colours: 32 colour objects at 0x550910, +4 = 0x80000000 | palette index. Style
// 0x1b (WBMd14, colour 1) is yellow in the menus' palette but not in a mission's, so a lit
// item draws with colour 1 pointed at g_yellow for the moment.
constexpr uintptr_t kTextColours = 0x550910;

// The panel sits in the top-right corner (the minimap has the top left); folded,
// it is one row there. The game's info panel, also top right in missions, stays
// empty: the editor keeps the game's selection clear and marks its own.
int PanelW() { return g.collapsed ? kStripW : kPanelW; }
int PanelRows() { return g.collapsed ? 1 : kRows; }
int PanelLeft() { return game::at<short>(game::ScreenRight) - kMargin - PanelW(); }
int PanelTop() { return kMargin + 6; }

short* Rect(void* b) { return (short*)((char*)b + 0xc); }

bool InItem(const Slot& s, int x, int y) {
    short* r = Rect(s.b);
    return x >= r[0] && x < std::max<int>(r[2], r[0] + 8) && y >= r[1] - 1 && y < r[3] + 1;
}

const Slot* SlotAt(int x, int y) {
    for (const Slot& s : g_pool)
        if (s.used && s.clickable && InItem(s, x, y)) return &s;
    return nullptr;
}

bool OnPanel(int x, int y) {
    if (g_pool.empty()) return false;
    int top = PanelTop(), left = PanelLeft();
    return x >= left - 6 && x < left + PanelW() && y >= top - 6 && y < top + PanelRows() * kRowH + 4;
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

void FillInterface(int x0, int y0, int x1, int y1, int colour) {
    for (int y = y0; y < y1; ++y) {
        uintptr_t a = layers::InterfacePixelAddress(x0, y);
        uintptr_t b = layers::InterfacePixelAddress(x1 - 1, y);
        if (a && b && b >= a) memset((void*)a, colour, b - a + 1);
    }
}

// The panel's background, straight into the interface layer's port (only
// while it is being drawn), when the title (the first of the panel's widgets)
// draws, so every text lands on top of it.
void Background() {
    if (!layers::Active() || g_dark < 0) return;
    FillInterface(PanelLeft() - 6, PanelTop() - 6, PanelLeft() + PanelW(), PanelTop() + PanelRows() * kRowH + 2, g_dark);
}

// An item's place in the top-right corner of the current frame. Smooth zoom
// changes the frame size between the frame-start tick and the draw, so this
// runs again as each item draws, from the same frame size as Background().
bool Place(const Slot& s) {
    short* r = Rect(s.b);
    int indent = s.it.swatch >= 0 ? 14 : 0;
    short want[2] = {(short)(PanelLeft() + s.it.x + indent), (short)(PanelTop() + s.it.row * kRowH)};
    if (r[0] == want[0] && r[1] == want[1]) return false;
    int w = r[2] - r[0];
    r[0] = want[0];
    r[1] = want[1];
    r[2] = (short)(want[0] + std::max(w, 8));
    r[3] = (short)(want[1] + kRowH - 2);
    return true;
}

void __thiscall ItemDraw(void* b) {
    Slot* me = nullptr;
    for (Slot& s : g_pool)
        if (s.b == b) me = &s;
    if (me && Place(*me)) ++g_lateMoves;
    if (me == &g_pool[0]) Background();
    if (!me || !me->used) return;
    if (me->it.swatch >= 0 && layers::Active()) {
        short* r = Rect(b);
        FillInterface(r[0] - 13, r[1] + 2, r[0] - 3, r[1] + 12, me->it.swatch);
    }
    bool hover = me->clickable && InItem(*me, game::at<short>(game::CursorX), game::at<short>(game::CursorY));
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

void BuildPanel() {
    g_pool.clear();
    memcpy(g_vtbl, (void*)kButtonVtbl, sizeof g_vtbl);
    g_vtbl[1] = (uintptr_t)&ItemDraw;
    for (int i = 0; i < kPoolSize; ++i) {
        void* b = ((void*(__thiscall*)(uintptr_t, const char*, int, int))kAddTextButton)(Game(), "", 0x300 + i, 0);
        if (!b) break;
        ((void(__thiscall*)(void*, int, int))kSetStyle)(b, kStyle, kStyleLit);
        *(uintptr_t*)b = (uintptr_t)g_vtbl;
        Slot s;
        s.b = b;
        g_pool.push_back(s);
    }
    g_h.log("editor: panel built (%zu buttons)", g_pool.size());
}

// Status text word-wrapped onto rows of kStatusChars.
std::vector<std::string> Wrap(const std::string& s, int rows) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size() && (int)out.size() < rows) {
        if (s.size() - at <= (size_t)kStatusChars) {
            out.push_back(s.substr(at));
            break;
        }
        size_t cut = s.rfind(' ', at + kStatusChars);
        if (cut == std::string::npos || cut <= at) cut = at + kStatusChars;
        out.push_back(s.substr(at, cut - at));
        at = s[cut] == ' ' ? cut + 1 : cut;
    }
    return out;
}

void Add(std::vector<PanelItem>& v, int row, int x, const std::string& text, int id = -1, bool lit = false,
         int swatch = -1) {
    PanelItem it;
    it.row = row;
    it.x = x;
    it.text = text;
    it.id = id;
    it.lit = lit;
    it.swatch = swatch;
    v.push_back(it);
}

std::string Num(int n) { return std::to_string(n); }

// --- pages -------------------------------------------------------------------------

const char* const kTabLabels[] = {"Terrain", "Things", "Units", "Players", "Map", "Events", "AI"};
const int kTabRow[] = {1, 1, 1, 2, 2, 2, 2};
const int kTabX[] = {0, 82, 154, 0, 82, 128, 196};

TerrainPainter g_terrain;
std::vector<uint8_t> g_terrainFile;
std::string g_terrainError;

// The Things palette: the Smart Palette's obstacles, ruins and crops (TRPL 1022).
struct Entry {
    int type;
    std::string name;
};
std::vector<Entry> g_thingList, g_creatureList, g_deviceList;

void BuildLists() {
    g_thingList.clear();
    g_creatureList.clear();
    g_deviceList.clear();
    for (const Palette& p : g_terrain.Palettes())
        for (const PaletteItem& it : p.items) {
            if (it.thingType < 0) continue;
            Entry e{it.thingType, world::Name(it.thingType)};
            if (it.category == PaletteItem::kObstacle || it.category == PaletteItem::kCrop) g_thingList.push_back(e);
            if (it.category == PaletteItem::kCreature) g_creatureList.push_back(e);
            if (it.category == PaletteItem::kDevice) g_deviceList.push_back(e);
        }
    // Without TERRAIN.MHK's palettes: the game's own type tables.
    if (g_thingList.empty())
        for (int t : {0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x43, 0x3f, 0x40, 0x41}) g_thingList.push_back({t, world::Name(t)});
    if (g_creatureList.empty())
        for (int clan = 0; clan < 4; ++clan)
            for (int k = 0; k < 5; ++k) g_creatureList.push_back({clan * 9 + k, world::Name(clan * 9 + k)});
    if (g_deviceList.empty())
        for (int t = 0x2c; t <= 0x3b; ++t) g_deviceList.push_back({t, world::Name(t)});
}

const std::vector<Entry>& UnitList() { return g.unitMode == kModeDevices ? g_deviceList : g_creatureList; }
constexpr int kUnitRows = 11;
constexpr int kPalRows = 12;

int PlayerSwatch(int i) {
    PlayerRec r = world::CapturePlayer(i);
    return r.type == world::kNone ? world::ColourOf(i) : r.colour;
}

void PlayerSelector(std::vector<PanelItem>& v, int row) {
    PlayerRec r = world::CapturePlayer(g.player);
    std::string s = "Player " + Num(g.player + 1) + ": " +
                    (r.type == world::kNone ? std::string("None") : std::string(world::RaceName(r.race)));
    Add(v, row, 0, "<", Id(kActPlayerPrev));
    Add(v, row, 22, s, -1, false, PlayerSwatch(g.player));
    Add(v, row, 236, ">", Id(kActPlayerNext));
}

void TerrainPage(std::vector<PanelItem>& v) {
    int pages = std::max(1, (g.painter->TypeCount() + kPalRows - 1) / kPalRows);
    g.page = std::clamp(g.page, 0, pages - 1);
    Add(v, 0, 0, pages > 1 ? "Terrain " + Num(g.page + 1) + "/" + Num(pages) : "Terrain");
    if (g.page > 0) Add(v, 0, 150, "< Prev", Id(kActPrev));
    if (g.page + 1 < pages) Add(v, 0, 205, "Next >", Id(kActNext));
    for (int i = 0; i < kPalRows; ++i) {
        int t = g.page * kPalRows + i;
        if (t < g.painter->TypeCount()) Add(v, 1 + i, 12, g.painter->TypeName(t), Id(kActPal, i), t == g.type);
    }
    Add(v, 1 + kPalRows, 0, "Brush");
    static const int kSizes[] = {1, 2, 3, 5};
    for (int i = 0; i < 4; ++i) Add(v, 1 + kPalRows, 70 + i * 30, Num(kSizes[i]), Id(kActBrush, kSizes[i]), g.brush == kSizes[i]);
}

void ThingsPage(std::vector<PanelItem>& v) {
    Add(v, 0, 0, "Trees, rocks, ruins, crops");
    for (size_t i = 0; i < g_thingList.size() && i < 10; ++i)
        Add(v, 1 + (int)i, 12, g_thingList[i].name, Id(kActThing, (int)i), (int)i == g.thingItem);
    Add(v, 11, 0, "Fill Pods", Id(kActFill, world::kPod));
    Add(v, 11, 120, "Fill Spores", Id(kActFill, world::kSpore));
    Add(v, 12, 0, "Fill Weeds", Id(kActFill, world::kWeed));
    Add(v, 12, 120, "Clear Crops", Id(kActClearCrops));
    Add(v, 14, 0, "Left: place   Right: remove");
}

std::string Rank(int vet) {
    static const char* const kRanks[] = {"Rookie", "Squaddie", "Veteran", "Hero"};  // MapEditor strings 365-368
    return vet >= 0 && vet < 4 ? kRanks[vet] : Num(vet);
}

void UnitsPage(std::vector<PanelItem>& v) {
    PlayerSelector(v, 0);
    Add(v, 1, 0, "Creatures", Id(kActUnitMode, kModeCreatures), g.unitMode == kModeCreatures);
    Add(v, 1, 90, "Devices", Id(kActUnitMode, kModeDevices), g.unitMode == kModeDevices);
    Add(v, 1, 168, "Select", Id(kActUnitMode, kModeSelect), g.unitMode == kModeSelect);
    if (g.unitMode != kModeSelect) {
        const std::vector<Entry>& list = UnitList();
        int pages = std::max(1, ((int)list.size() + kUnitRows - 1) / kUnitRows);
        g.unitPage = std::clamp(g.unitPage, 0, pages - 1);
        if (pages > 1) {
            Add(v, 2, 0, "Page " + Num(g.unitPage + 1) + "/" + Num(pages));
            if (g.unitPage > 0) Add(v, 2, 150, "< Prev", Id(kActUnitPrev));
            if (g.unitPage + 1 < pages) Add(v, 2, 205, "Next >", Id(kActUnitNext));
        }
        for (int i = 0; i < kUnitRows; ++i) {
            int k = g.unitPage * kUnitRows + i;
            if (k < (int)list.size()) Add(v, 3 + i, 12, list[k].name, Id(kActUnit, i), k == g.unitItem);
        }
        Add(v, 14, 0, "Click the map to place");
        return;
    }
    int id = g.selHandle ? IdOf(g.selHandle) : kNoThing;
    ThingRec r;
    if (id == kNoThing || !world::Capture(id, r)) {
        Add(v, 3, 0, "Click a thing to select it,");
        Add(v, 4, 0, "drag it to move it");
        return;
    }
    std::string owner = r.owner < 8 ? " (P" + Num(r.owner + 1) + ")" : "";
    Add(v, 2, 0, world::Name(r.type) + owner, -1, false, r.owner < 8 ? PlayerSwatch(r.owner) : -1);
    if (world::IsCreature(r.type) || world::IsDevice(r.type)) {
        Add(v, 3, 0, "Hit points " + Num(r.hp) + "/" + Num(world::MaxHp(id)));
        Add(v, 3, 190, "-", Id(kActHpDown));
        Add(v, 3, 215, "+", Id(kActHpUp));
    }
    if (world::IsCreature(r.type)) {
        Add(v, 4, 0, "Rank: " + Rank(r.vet), Id(kActRank));
        int row = 5;
        static const char* const kSlot[] = {"Arm 1", "Head", "Arm 2", "Slot 4"};
        for (int s = 0; s < 4; ++s)
            if (world::SlotUsed(r.type, s)) {
                std::string name = s == 1 && world::SlotGroup(r.type, s) == 0 ? "Arm 3" : kSlot[s];
                Add(v, row++, 0, name + ": " + world::GizmoName(r.gizmo[s]), Id(kActGizmo, s));
            }
    }
    Add(v, 11, 0, "Delete", Id(kActDelete));
    Add(v, 13, 0, "Right-click: deselect");
}

void PlayersPage(std::vector<PanelItem>& v) {
    PlayerSelector(v, 0);
    PlayerRec r = world::CapturePlayer(g.player);
    bool fixed = g.player == 0 || g.player == world::LocalPlayerIndex();
    Add(v, 1, 0, std::string("Type: ") + world::TypeName(r.type), fixed ? -1 : Id(kActType));
    if (r.type == world::kNone) {
        Add(v, 3, 0, "Set a type to add this player");
        if (g.player >= world::Supported()) Add(v, 4, 0, "(beyond Players supported)");
        return;
    }
    Add(v, 2, 0, std::string("Race: ") + world::RaceName(r.race), Id(kActRace));
    Add(v, 3, 0, "Start: " + Num(r.startX) + "," + Num(r.startY));
    Add(v, 5, 0, "Clan");
    Add(v, 5, 110, "CGK");
    Add(v, 5, 165, "Available");
    for (int c = 0; c < 4; ++c) {
        Add(v, 6 + c, 12, world::RaceName(c));
        Add(v, 6 + c, 116, Num(r.cgk[c]), Id(kActCgk, c));
        Add(v, 6 + c, 175, r.avail[c] ? "Yes" : "No", Id(kActAvail, c), r.race == c);
    }
    Add(v, 11, 0, "Click the map: start point");
    Add(v, 12, 0, "CGK: click +1, right-click -1");
}

void MapPage(std::vector<PanelItem>& v) {
    if (g.phase == kMapNaming)
        Add(v, 0, 0, "Name: " + g.mapNameEdit + "_", Id(kActName), true);
    else
        Add(v, 0, 0, "Name: " + MapNameShown(), Id(kActName));
    Add(v, 1, 0, "Size: " + Num(g_grid.Width()) + " x " + Num(g_grid.Height()) + " cells");
    Add(v, 2, 0, std::string("Fog: ") + world::FogName(g.fogMode), Id(kActFog));
    Add(v, 3, 0, "Players supported: " + Num(world::Supported()));
    Add(v, 3, 190, "-", Id(kActSupDown));
    Add(v, 3, 215, "+", Id(kActSupUp));
    Add(v, 4, 0, "Recommended: " + Num(world::Recommended()));
    Add(v, 4, 190, "-", Id(kActRecDown));
    Add(v, 4, 215, "+", Id(kActRecUp));
    Add(v, 6, 0, "Name: click, type, Enter");
    Add(v, 7, 0, "Fog: click to change");
}

void FindingsPage(std::vector<PanelItem>& v) {
    Add(v, 0, 0, g.findings.empty() ? "Map check: no problems found"
                                     : "Map check: " + Num((int)g.findings.size()) + " warning" +
                                           (g.findings.size() == 1 ? "" : "s"));
    int row = 1;
    for (size_t i = 0; i < g.findings.size() && row < kPageRows - 1; ++i)
        for (const std::string& line : Wrap(g.findings[i].text, 2))
            if (row < kPageRows - 1) Add(v, row++, 0, line);
    Add(v, kPageRows - 1, 0, "Close", Id(kActCloseCheck));
}

// The current tool, for the folded panel ("Sand 3").
std::string ToolText() {
    switch (g.tab) {
    case kTabTerrain: return g.painter->TypeName(g.type) + " " + Num(g.brush);
    case kTabThings:
        return g_thingList.empty() ? "" : g_thingList[std::clamp(g.thingItem, 0, (int)g_thingList.size() - 1)].name;
    case kTabUnits:
        if (g.unitMode == kModeSelect || UnitList().empty()) return "Select";
        return UnitList()[std::clamp(g.unitItem, 0, (int)UnitList().size() - 1)].name;
    case kTabPlayers: return "P" + Num(g.player + 1) + " start";
    default: return g_pages[g.tab] ? g_pages[g.tab]->Label() : kTabLabels[g.tab];
    }
}

// Every frame: what the panel shows, dealt to the pool. Positions follow the
// frame size (the zoom); the panel keeps to the bottom-left corner.
void UpdatePanel() {
    std::vector<PanelItem> page, common;
    if (g.collapsed) {
        Add(common, 0, 0, "Show (Tab)", Id(kActCollapse));
        Add(common, 0, 96, ToolText().substr(0, 13));
    } else {
        std::string title = "Map Editor: " + g.title;
        if (title.size() > 22) title = title.substr(0, 21) + ".";
        Add(common, 0, 0, title + (Dirty() ? " *" : ""));
        Add(common, 0, 212, "Hide", Id(kActCollapse));
    }
    for (int t = 0; t < kTabCount && !g.collapsed; ++t) {
        bool shown = t < kTabEvents || g_pages[t];
        if (shown)
            Add(common, kTabRow[t], kTabX[t], g_pages[t] ? g_pages[t]->Label() : kTabLabels[t], Id(kActTab, t),
                t == g.tab && !g.showFindings);
    }
    bool external = false;
    if (g.collapsed)
        ;  // just the strip
    else if (g.showFindings)
        FindingsPage(page);
    else if (g.tab == kTabTerrain)
        TerrainPage(page);
    else if (g.tab == kTabThings)
        ThingsPage(page);
    else if (g.tab == kTabUnits)
        UnitsPage(page);
    else if (g.tab == kTabPlayers)
        PlayersPage(page);
    else if (g.tab == kTabMap)
        MapPage(page);
    else if (g_pages[g.tab])
        g_pages[g.tab]->Items(page), external = true;
    if (!g.collapsed) {
        if (!g.undo.empty()) Add(common, kCommonRow, 0, "Undo", Id(kActUndo));
        if (!g.redo.empty()) Add(common, kCommonRow, 70, "Redo", Id(kActRedo));
        Add(common, kCommonRow, 140, "Check", Id(kActCheck), g.showFindings);
        Add(common, kCommonRow + 1, 0, g.phase == kNaming ? "Save" : "Save As", Id(kActSaveAs), g.phase == kNaming);
        Add(common, kCommonRow + 1, 85, g.phase == kNaming ? "Cancel" : g.testArmed ? "Test Map?" : "Test Map",
            Id(kActTest), g.phase != kNaming && g.testArmed);
        Add(common, kCommonRow + 1, 180, g.exitArmed ? "Exit?" : "Exit", Id(kActExit), g.exitArmed);
        std::vector<std::string> status = Wrap(g.status, 2);
        for (size_t i = 0; i < status.size(); ++i) Add(common, kCommonRow + 2 + (int)i, 0, status[i]);
    }

    size_t n = 0;
    auto deal = [&](const PanelItem& it, int rowOffset, bool fromModule) {
        if (n >= g_pool.size()) return;
        Slot& s = g_pool[n++];
        s.it = it;
        s.it.row = it.row + rowOffset;
        s.used = true;
        s.page = fromModule;
        s.clickable = it.id >= 0;
        SetText(s.b, it.text);
        SetLit(s.b, it.lit);
        Place(s);
    };
    deal(common[0], 0, false);  // the title draws first: it paints the background
    for (size_t i = 1; i < common.size(); ++i) deal(common[i], 0, false);
    for (PanelItem& it : page) {
        it.row = std::clamp(it.row, 0, kPageRows - 1);
        deal(it, kPageFirstRow, external);
    }
    for (; n < g_pool.size(); ++n) {
        Slot& s = g_pool[n];
        s.used = s.clickable = false;
        s.it = PanelItem();
        SetText(s.b, "");
        SetLit(s.b, false);
    }
    for (Slot& s : g_pool) *(short*)((char*)s.b + 0x1a) = 1;  // redraw
}

// --- painting --------------------------------------------------------------------

bool WorldAt(int sx, int sy, int& wx, int& wy) {
    wx = (short)((int(__thiscall*)(uintptr_t, int))kToWorldX)(Game(), (short)sx);
    wy = (short)((int(__thiscall*)(uintptr_t, int))kToWorldY)(Game(), (short)sy);
    return g_grid.Inside(wx >> 6, wy >> 6);
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

void BeginStroke(bool fill, int cx, int cy) {
    g.strokeBefore = TakeSnapshot();
    g.drag = fill ? kDragFill : kDragStroke;
    g.lastX = g.lastY = -1;
    g.fillX = cx;
    g.fillY = cy;
}

void EndStroke(const char* what) {
    g.drag = kNoDrag;
    Changes net = Diff(g.strokeBefore);
    if (net.empty()) return;
    Edit e;
    e.what = std::string(what) + ": " + Num((int)net.size()) + " cells";
    Op op;
    op.kind = Op::kCells;
    op.cells = std::move(net);
    e.ops.push_back(std::move(op));
    SetStatus(e.what);
    Push(std::move(e));
}

// --- things -------------------------------------------------------------------------

// The corner terrain of a 32 px tile, for the obstacle rules (smart painter's tables).
int CornerAt(int tx, int ty) {
    if (!g_terrain.Loaded() || !g_grid.Inside(tx >> 1, ty >> 1)) return -1;
    Corners c;
    if (!g_terrain.CornersOfTile(g_grid.Get(tx >> 1, ty >> 1), c)) return -1;
    return c.c[(ty & 1) * 2 + (tx & 1)];
}

// Things tab: place the chosen thing at a world point (once per 32 px tile in a drag).
void PlaceThingAt(int wx, int wy) {
    if (g_thingList.empty()) return;
    int tx = wx >> 5, ty = wy >> 5;
    if (tx == g.lastX && ty == g.lastY) return;
    g.lastX = tx;
    g.lastY = ty;
    const Entry& e = g_thingList[std::clamp(g.thingItem, 0, (int)g_thingList.size() - 1)];
    std::string why;
    int id = PutRecorded(g.building, e.type, world::kNeutral, wx, wy, nullptr, why);
    if (id == kNoThing)
        g.dragNote = why;
    else
        ++g.dragCount;
}

void EndThingsDrag() {
    g.drag = kNoDrag;
    std::string name = g_thingList.empty() ? std::string() : g_thingList[g.thingItem].name;
    if (g.dragCount) {
        g.building.what = "Placed " + Num(g.dragCount) + " " + name;
        SetStatus(g.building.what + (g.dragNote.empty() ? "" : "; " + g.dragNote));
        Push(std::move(g.building));
    } else {
        SetStatus(g.dragNote.empty() ? "Nothing placed" : g.dragNote);
    }
    g.building = Edit();
}

// Things tab, right-click: remove the crop, tree, rock or ruin there.
void RemoveThingAt(int wx, int wy) {
    int id = world::PickAt(wx, wy);
    uintptr_t t = world::Thing(id);
    if (!t || !(world::IsCrop(world::TypeOf(t)) || world::IsObstacle(world::TypeOf(t)))) {
        SetStatus("No crop or obstacle there (units: Units tab)");
        return;
    }
    Edit e;
    std::string name = world::Name(world::TypeOf(t));
    RemoveRecorded(e, id);
    e.what = "Removed a " + name;
    SetStatus(e.what);
    Push(std::move(e));
}

void FillCrops(int type) {
    // As the 1998 editor's Fill Cropland (0x402fd0) and the console's "crop":
    // every 32 px tile of pod soil gets a crop, where nothing is in the way.
    Edit e;
    int n = 0;
    std::string why;
    for (int ty = 0; ty < world::TilesH(); ++ty)
        for (int tx = 0; tx < world::TilesW(); ++tx)
            if (world::TerrainType(tx, ty) == 2 && PutRecorded(e, type, world::kNeutral, tx * 32 + 16, ty * 32 + 16, nullptr, why) != kNoThing)
                ++n;
    e.what = n ? "Filled cropland: " + Num(n) + " " + world::Name(type) : "No empty cropland";
    SetStatus(e.what);
    Push(std::move(e));
}

void ClearCrops() {
    Edit e;
    int n = 0;
    for (int id = 0; id < world::ThingCount(); ++id)
        if (uintptr_t t = world::Thing(id))
            if (world::IsCrop(world::TypeOf(t)) && RemoveRecorded(e, id)) ++n;
    e.what = "Cleared " + Num(n) + " crops";
    SetStatus(e.what);
    Push(std::move(e));
}

// Units tab: a new creature or device of the current player.
void PlaceUnitAt(int wx, int wy) {
    const std::vector<Entry>& list = UnitList();
    if (list.empty()) return;
    const Entry& en = list[std::clamp(g.unitItem, 0, (int)list.size() - 1)];
    Edit e;
    std::string why;
    int id = PutRecorded(e, en.type, g.player, wx, wy, nullptr, why);
    if (id == kNoThing) {
        SetStatus(why);
        return;
    }
    e.what = "Placed a " + en.name + " for player " + Num(g.player + 1);
    SetStatus(e.what + (why.empty() ? "" : " (" + why + ")"));
    Push(std::move(e));
}

void SelectThing(int id) {
    g.selHandle = id == kNoThing ? 0 : HandleOf(id);
    ShowSelection();
    if (id != kNoThing) {
        ThingRec r;
        world::Capture(id, r);
        SetStatus("Selected " + world::Name(r.type) + " at " + Num((r.x + 16) / 64) + "," + Num((r.y + 16) / 64), false);
    }
}

// Units tab: the selected thing moves to world (wx, wy), with the placement rules.
void MoveSelected(int wx, int wy) {
    int id = IdOf(g.selHandle);
    ThingRec old;
    if (id == kNoThing || !world::Capture(id, old)) return;
    world::Remove(id);
    std::string why;
    int nid = world::Put(old.type, old.owner, wx, wy, &old, why);
    if (nid == kNoThing) {
        Bind(g.selHandle, world::Restore(old));
        ShowSelection();
        SetStatus("Can't move it there: " + why);
        return;
    }
    if (world::IsCrop(old.type) && old.extra > 0) F<int>(world::Thing(nid), 0x28) = old.extra;
    Bind(g.selHandle, nid);
    Edit e;
    Op op;
    op.kind = Op::kThing;
    op.handle = g.selHandle;
    op.had = op.has = true;
    op.tb = old;
    world::Capture(nid, op.ta);
    e.ops.push_back(op);
    e.what = "Moved the " + world::Name(old.type);
    SetStatus(e.what);
    Push(std::move(e));
    ShowSelection();
}

void DeleteSelected() {
    int id = g.selHandle ? IdOf(g.selHandle) : kNoThing;
    if (id == kNoThing) {
        SetStatus("Nothing selected");
        return;
    }
    Edit e;
    std::string name = world::Name(world::TypeOf(world::Thing(id)));
    RemoveRecorded(e, id);
    e.what = "Deleted the " + name;
    g.selHandle = 0;
    ShowSelection();
    SetStatus(e.what);
    Push(std::move(e));
}

// The selected thing with changed attributes (hit points, rank, gizmos).
void ChangeSelected(const std::function<void(ThingRec&)>& change, const std::string& what, const std::string& merge) {
    int id = g.selHandle ? IdOf(g.selHandle) : kNoThing;
    ThingRec r;
    if (id == kNoThing || !world::Capture(id, r)) return;
    ThingRec to = r;
    change(to);
    if (memcmp(&to, &r, sizeof r) == 0) return;
    Edit e;
    ThingRec hpOnly = r;
    hpOnly.hp = to.hp;
    if (memcmp(&hpOnly, &to, sizeof to) == 0) {  // hit points alone change in place
        world::SetHitPoints(id, to.hp);
        Op op;
        op.kind = Op::kThing;
        op.handle = g.selHandle;
        op.had = op.has = true;
        op.tb = r;
        world::Capture(id, op.ta);
        e.ops.push_back(op);
    } else if (!ReplaceRecorded(e, id, to)) {
        SetStatus("Couldn't change it");
        return;
    }
    e.what = what;
    e.merge = merge;
    ShowSelection();
    SetStatus(what + " (" + world::Name(r.type) + ")");
    Push(std::move(e));
}

// --- players and map ---------------------------------------------------------------

void ChangePlayer(const std::function<void(PlayerRec&)>& change, const std::string& what) {
    PlayerRec before = world::CapturePlayer(g.player), after = before;
    change(after);
    if (memcmp(&before, &after, sizeof before) == 0) return;
    Edit e;
    e.what = what;
    // A new player starts from the defaults; one that goes loses its things first.
    if (before.type == world::kNone) {
        PlayerRec d = world::DefaultPlayer(g.player, after.type, world::Supported());
        after = d;
    }
    if (after.type == world::kNone)
        for (int id = 0; id < world::ThingCount(); ++id)
            if (uintptr_t t = world::Thing(id))
                if (world::OwnerOf(t) == g.player) RemoveRecorded(e, id);
    // A new race: the player's Shamans become the new race's.
    if (before.type != world::kNone && after.type != world::kNone && after.race != before.race) {
        after.avail[after.race] = 1;
        std::vector<int> shamans;  // first: the new ones get new ids at the end of the table
        for (int id = 0; id < world::ThingCount(); ++id)
            if (uintptr_t t = world::Thing(id))
                if (world::OwnerOf(t) == g.player && world::IsShaman(world::TypeOf(t))) shamans.push_back(id);
        for (int id : shamans) {
            ThingRec r;
            world::Capture(id, r);
            r.type = after.race * 9;
            ReplaceRecorded(e, id, r);
        }
    }
    Op op;
    op.kind = Op::kPlayer;
    op.player = g.player;
    op.pb = before;
    op.pa = after;
    world::ApplyPlayer(g.player, after, g.fogMode);
    e.ops.push_back(op);
    SetStatus(what);
    Push(std::move(e));
    ShowSelection();
}

void ChangeMap(const std::function<void(MapRec&)>& change, const std::string& what) {
    MapRec before = CaptureMap(), after = before;
    change(after);
    if (after.name == before.name && after.supported == before.supported && after.recommended == before.recommended &&
        after.fog == before.fog)
        return;
    Edit e;
    e.what = what;
    Op op;
    op.kind = Op::kMap;
    op.mb = before;
    op.ma = after;
    ApplyMap(after);
    e.ops.push_back(op);
    SetStatus(what);
    Push(std::move(e));
}

void SetStartPoint(int cx, int cy) {
    if (!world::Player(g.player)) {
        SetStatus("Player " + Num(g.player + 1) + " is None: set its type first");
        return;
    }
    ChangePlayer([&](PlayerRec& r) { r.startX = cx, r.startY = cy; },
                 "Player " + Num(g.player + 1) + "'s start: " + Num(cx) + "," + Num(cy));
}

// --- checking ---------------------------------------------------------------------

void RunCheck(bool show) {
    g.findings = Validate();
    g_h.log("editor: map check: %zu finding(s)", g.findings.size());
    for (const Finding& f : g.findings) g_h.log("editor:   %s", f.text.c_str());
    if (show) {
        g.showFindings = true;
        SetStatus(g.findings.empty() ? "Map check: no problems found"
                                     : "Map check: " + Num((int)g.findings.size()) + " warning(s)");
    }
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

void ShowNaming() {
    SetStatus("Name: " + g.name + "_   Enter saves" + (g.testAfter ? ", then starts a test skirmish" : ""));
}

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
    RunCheck(false);  // warnings only: they never stop the save
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
    // The engine's things, players and map header as saved (tools/editor_check.py
    // compares them with the file).
    f = fopen((g_h.dir + "WarBreedsHD-editor-world.txt").c_str(), "w");
    if (!f) return;
    std::string name = world::MapName();
    fprintf(f, "map %d %d %d %s\n", world::Supported(), world::Recommended(), g.fogMode, name.c_str());
    for (int i = 0; i < 8; ++i) {
        PlayerRec r = world::CapturePlayer(i);
        if (r.type == world::kNone) continue;
        fprintf(f, "player %d %d %d %d %d,%d,%d,%d %d,%d,%d,%d %d %d\n", i, r.type, r.race, r.colour, r.cgk[0], r.cgk[1],
                r.cgk[2], r.cgk[3], r.avail[0], r.avail[1], r.avail[2], r.avail[3], r.startX, r.startY);
    }
    for (int id = 0; id < world::ThingCount(); ++id) {
        ThingRec r;
        if (!world::Capture(id, r)) continue;
        fprintf(f, "thing %d %d %d %d %d %d %d %d,%d,%d,%d\n", id, r.type, r.owner, r.x, r.y, r.hp, r.vet, r.gizmo[0],
                r.gizmo[1], r.gizmo[2], r.gizmo[3]);
    }
    for (const Finding& x : g.findings) fprintf(f, "finding %s\n", x.text.c_str());
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
    std::string warn = g.findings.empty() ? "" : "; " + Num((int)g.findings.size()) + " warning(s): Check";
    SetStatus("Saved " + BaseName(g.savePath) + ".WBM" + warn);
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

void ConfirmMapName() {
    g.phase = kIdle;
    std::string n = g.mapNameEdit;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.empty()) {
        SetStatus("Name unchanged");
        return;
    }
    ChangeMap([&](MapRec& m) { m.name = n; }, "Map name: " + n);
}

// --- the editor's lifetime ---------------------------------------------------------

void Teardown() {
    g.active = false;
    g.phase = kIdle;
    g.drag = kNoDrag;
    g.building = Edit();
    g_pool.clear();  // the widgets go with SGame's own (Scene::Close)
    g.painter.reset();
    g.undo.clear();
    g.redo.clear();
    g_handleId.clear();
    g_idHandle.clear();
    g.selHandle = 0;
    for (TabPage* p : g_pages)
        if (p) p->OnClose();
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
    g_h.log("editor: test skirmish %s: to the skirmish setup", g_test.file.c_str());
    Leave(false);
}

// The painter behind the palette: the smart painter (terrain_paint.h, the
// original editor's transitions from DATA\TERRAIN.MHK), or the raw stamper if
// the tables can't be read.
// The game opens TERRAIN.MHK without sharing, so Install reads it at startup,
// before the game does; the tables are parsed when the editor first opens.
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
        Terrain tt = g_terrain.SmartTerrains()[t];
        int kind = g_terrain.UniformKind(tt);
        if (kind != TerrainPainter::kNoKind)
            for (uint16_t id : g_terrain.KindTiles(kind)) out.push_back(id);
        // Cropland inside cropland has interior corners (10): those kinds count too.
        if (tt == kCropland)
            for (int k = 0; k < g_terrain.KindCount(); ++k) {
                Corners c = g_terrain.KindCorners(k);
                bool crop = true;
                for (int i = 0; i < 4; ++i) crop = crop && (c.c[i] == kCropland || c.c[i] == kCroplandInterior);
                if (crop && k != kind)
                    for (uint16_t id : g_terrain.KindTiles(k)) out.push_back(id);
            }
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
    g.drag = kNoDrag;
    g.tab = kTabTerrain;
    g.collapsed = false;
    g.showFindings = false;
    g.findings.clear();
    g.selHandle = 0;
    g.player = 0;
    g.unitMode = kModeCreatures;
    g.unitItem = g.unitPage = g.thingItem = 0;
    g_handleId.clear();
    g_idHandle.clear();
    F<int>(Game(), kUpdateMode) = 1;  // (already set; the console's "update pause")
    if (World()) ((void(__thiscall*)(uintptr_t, int))kClearSelection)(World(), 0);
    ((void(__thiscall*)(uintptr_t))kAnimStop)(kTileAnimator);  // lava back to its base tiles
    g.painter = MakePainter();
    BuildLists();
    world::SetCornerQuery(g_terrain.Loaded() ? &CornerAt : nullptr);
    g.baseType = -1;
    g.type = 0;
    g.brush = 1;
    g.page = 0;
    if (char* me = *(char**)kLocalPlayer) {
        g.fogWas = *(short*)(me + kFog);
        *(short*)(me + kFog) = 0;
    }
    g.fogMode = world::FogModeOf(g.fogWas);
    g.mapName = g.isNew ? std::string() : world::MapName();
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
    g_h.log("editor: map \"%s\", %d supported, %d recommended, fog mode %d, %d things; lists: %zu things, %zu creatures, "
            "%zu devices",
            world::MapName().c_str(), world::Supported(), world::Recommended(), g.fogMode, world::ThingCount(),
            g_thingList.size(), g_creatureList.size(), g_deviceList.size());
    std::string names;
    for (const Entry& e : g_creatureList) names += e.name + "=" + Num(e.type) + " ";
    for (const Entry& e : g_deviceList) names += e.name + "=" + Num(e.type) + " ";
    for (const Entry& e : g_thingList) names += e.name + "=" + Num(e.type) + " ";
    g_h.log("editor: things %s", names.c_str());
    if (g_h.debug) g_h.log("editor: DEBUG %d stale thing-grid entries at entry", world::StaleGridEntries());
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

// --- clicks on the panel ------------------------------------------------------------

const char* const kTabHints[] = {
    "Left-drag paints, right-click picks; Tab hides", "Pick a thing; left places, right removes",
    "Pick a unit and click the map, or Select", "Pick a player; click the map for its start",
    "Name, fog and players of the map", "", ""};

void CycleGizmo(int slot, bool back) {
    int id = IdOf(g.selHandle);
    ThingRec r;
    if (!world::Capture(id, r)) return;
    std::vector<int> c = world::GizmoChoices(r.type, slot, r.owner);
    int cur = r.gizmo[slot] == 0x15 ? 0x13 : r.gizmo[slot];
    size_t at = std::find(c.begin(), c.end(), cur) - c.begin();
    int next = c[at >= c.size() ? 0 : (at + (back ? c.size() - 1 : 1)) % c.size()];
    ChangeSelected(
        [&](ThingRec& t) {
            // The Screamer fills a pair of arm slots (ACreatureBody::SetGizmo 0x405230).
            int base = slot & 1;
            if (t.gizmo[base] == 0x13 || t.gizmo[base + 2] == 0x15) t.gizmo[base] = t.gizmo[base + 2] = 100;
            t.gizmo[slot] = next;
            if (next == 0x13) {
                t.gizmo[base] = 0x13;
                t.gizmo[base + 2] = 0x15;
            }
        },
        "Gizmo: " + world::GizmoName(next), "");
}

// Fold the panel to its strip, or open it again; the tool stays as it was.
void ToggleCollapsed() {
    g.collapsed = !g.collapsed;
    SetStatus(g.collapsed ? "Panel hidden (Tab shows it)" : "Panel shown (Tab hides it)", false);
    g_h.log("editor: panel %s", g.collapsed ? "hidden" : "shown");
}

void DoAction(int id, bool right) {
    int act = id / 100, arg = id % 100;
    switch (act) {
    case kActTab:
        g.showFindings = false;
        if (g.tab != arg) {
            g.tab = arg;
            if (kTabHints[arg][0]) SetStatus(kTabHints[arg], false);
        }
        // A module's tab opens its editor on every click, so it reopens after Esc.
        if (g_pages[arg]) g_pages[arg]->OnShow();
        break;
    case kActPal: g.type = g.page * kPalRows + arg; SetStatus("Terrain: " + g.painter->TypeName(g.type)); break;
    case kActPrev: --g.page; break;
    case kActNext: ++g.page; break;
    case kActBrush: g.brush = arg; SetStatus("Brush " + std::to_string(arg)); break;
    case kActUndo: Undo(); break;
    case kActRedo: Redo(); break;
    case kActCheck: RunCheck(true); break;
    case kActCloseCheck: g.showFindings = false; break;
    case kActCollapse: ToggleCollapsed(); break;
    case kActThing:
        g.thingItem = arg;
        SetStatus(g_thingList[arg].name + ": click the map to place");
        break;
    case kActFill: FillCrops(arg); break;
    case kActClearCrops: ClearCrops(); break;
    case kActUnitMode:
        g.unitMode = arg;
        g.unitItem = g.unitPage = 0;
        if (arg != kModeSelect) {
            g.selHandle = 0;
            ShowSelection();
        }
        SetStatus(arg == kModeSelect ? "Click a thing to select it" : "Pick one and click the map", false);
        break;
    case kActUnit:
        g.unitItem = g.unitPage * kUnitRows + arg;
        SetStatus(UnitList()[g.unitItem].name + " for player " + Num(g.player + 1) + ": click the map");
        break;
    case kActUnitPrev: --g.unitPage; break;
    case kActUnitNext: ++g.unitPage; break;
    case kActPlayerPrev: g.player = (g.player + 7) % 8; break;
    case kActPlayerNext: g.player = (g.player + 1) % 8; break;
    case kActHpDown:
    case kActHpUp: {
        int maxHp = world::MaxHp(IdOf(g.selHandle));
        int step = right ? 1 : std::max(1, maxHp / 10);
        ChangeSelected([&](ThingRec& r) { r.hp = std::clamp(r.hp + (act == kActHpUp ? step : -step), 1, std::max(1, maxHp)); },
                       "Hit points", "hp" + Num(g.selHandle));
        break;
    }
    case kActRank:
        ChangeSelected([&](ThingRec& r) { r.vet = (r.vet + (right ? 3 : 1)) % 4; }, "Rank changed", "");
        break;
    case kActGizmo: CycleGizmo(arg, right); break;
    case kActDelete: DeleteSelected(); break;
    case kActType: {
        int cur = world::CapturePlayer(g.player).type;
        int next = (cur + (right ? 3 : 1)) % 4;
        ChangePlayer([&](PlayerRec& r) { r.type = next; },
                     "Player " + Num(g.player + 1) + ": " + world::TypeName(next));
        break;
    }
    case kActRace: {
        int next = (world::CapturePlayer(g.player).race + (right ? 3 : 1)) % 4;
        ChangePlayer([&](PlayerRec& r) { r.race = next; },
                     "Player " + Num(g.player + 1) + ": " + world::RaceName(next));
        break;
    }
    case kActCgk:
        ChangePlayer([&](PlayerRec& r) { r.cgk[arg] = (r.cgk[arg] + (right ? 5 : 1)) % 6; },
                     std::string("CGK ") + world::RaceName(arg) + ": " +
                         Num((world::CapturePlayer(g.player).cgk[arg] + (right ? 5 : 1)) % 6));
        break;
    case kActAvail:
        if (world::CapturePlayer(g.player).race == arg) {
            SetStatus("A player's own clan is always available");
            break;
        }
        ChangePlayer([&](PlayerRec& r) { r.avail[arg] = !r.avail[arg]; },
                     std::string(world::RaceName(arg)) + (world::CapturePlayer(g.player).avail[arg] ? " not available" : " available"));
        break;
    case kActName:
        if (g.phase == kMapNaming) {
            ConfirmMapName();
        } else {
            g.phase = kMapNaming;
            g.mapNameEdit = g.mapName;
            SetStatus("Type the map's name, Enter to keep it", false);
        }
        break;
    case kActFog: {
        int next = (g.fogMode + (right ? 2 : 1)) % 3;
        ChangeMap([&](MapRec& m) { m.fog = next; }, std::string("Fog: ") + world::FogName(next));
        break;
    }
    case kActSupDown:
    case kActSupUp: {
        int n = std::clamp(world::Supported() + (act == kActSupUp ? 1 : -1), 2, 8);
        if (n < world::Supported() && world::Player(n)) {
            SetStatus("Set player " + Num(n + 1) + " to None first");
            break;
        }
        ChangeMap([&](MapRec& m) { m.supported = n, m.recommended = std::min(m.recommended, n); },
                  "Players supported: " + Num(n));
        break;
    }
    case kActRecDown:
    case kActRecUp: {
        int n = std::clamp(world::Recommended() + (act == kActRecUp ? 1 : -1), 2, world::Supported());
        ChangeMap([&](MapRec& m) { m.recommended = n; }, "Players recommended: " + Num(n));
        break;
    }
    default: break;
    }
}

void Click(const Slot& s, bool right) {
    ((void(__cdecl*)())kClickSound)();
    if (g.phase == kProcessing || g.phase == kSaving) return;
    int act = s.page ? -1 : s.it.id / 100;
    if (act != kActExit) g.exitArmed = false;
    if (act != kActTest) g.testArmed = false;
    g_h.log("editor: clicked %s%s", s.it.text.c_str(), right ? " (right)" : "");
    if (g.phase == kMapNaming && act != kActName) ConfirmMapName();
    if (g.phase == kNaming && act != kActSaveAs && act != kActTest) return;
    if (s.page) {
        if (g_pages[g.tab]) g_pages[g.tab]->OnClick(s.it.id, right);
        return;
    }
    switch (act) {
    case kActSaveAs:
        if (g.phase == kNaming)
            ConfirmName();
        else
            StartNaming(false);
        break;
    case kActTest:
        // Leaves the editor for a skirmish, so it asks first, as Exit does.
        if (g.phase == kNaming) {  // "Cancel"
            g.phase = kIdle;
            g.testAfter = false;
            SetStatus("Not saved");
        } else if (!g.testArmed) {
            g.testArmed = true;
            SetStatus("Test Map saves and starts a skirmish on this map: click again");
        } else {
            g.testArmed = false;
            if (!g.lastSaved.empty() && !Dirty())
                StartTest(g.lastSaved);
            else
                StartNaming(true);
        }
        break;
    case kActExit:
        if (Dirty() && !g.exitArmed) {
            g.exitArmed = true;
            SetStatus("Unsaved changes: Exit again to discard");
        } else {
            Leave(true);
        }
        break;
    default: DoAction(s.it.id, right); break;
    }
}

// --- the world overlay: start points ------------------------------------------------

// A 3x5 digit font for the player numbers.
const uint16_t kDigits[10] = {0x7b6f, 0x2c97, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf};

// The overlay's extent this frame (frame pixels): those terrain cells are drawn
// again next frame, so nothing is left behind when the cursor moves (the
// terrain only redraws cells marked dirty).
int g_drawn[4];

void Plot(int x, int y, int c) {
    if (uintptr_t a = layers::WorldPixelAddress(x, y)) {
        *(uint8_t*)a = (uint8_t)c;
        g_drawn[0] = std::min(g_drawn[0], x), g_drawn[1] = std::min(g_drawn[1], y);
        g_drawn[2] = std::max(g_drawn[2], x), g_drawn[3] = std::max(g_drawn[3], y);
    }
}

// AGrid +0x4848: two u8[96*96] dirty buffers; a cell is drawn when either is set.
constexpr uintptr_t kGridDirty = kGrid + 0x4848;

void MarkDrawnDirty(int f, int camX, int camY) {
    if (g_drawn[0] > g_drawn[2]) return;
    int x0 = std::max(0, (g_drawn[0] * f + camX) / 64 - 1), y0 = std::max(0, (g_drawn[1] * f + camY) / 64 - 1);
    int x1 = std::min(95, (g_drawn[2] * f + camX) / 64 + 1), y1 = std::min(95, (g_drawn[3] * f + camY) / 64 + 1);
    uint8_t* d = (uint8_t*)kGridDirty;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int b = 0; b < 2; ++b) d[b * 9216 + y * 96 + x] = d[b * 9216 + x * 96 + y] = 1;
}

void Ring(int cx, int cy, int r0, int r1, int c) {
    for (int dy = -r1; dy <= r1; ++dy)
        for (int dx = -r1; dx <= r1; ++dx) {
            int d = dx * dx + dy * dy;
            if (d >= r0 * r0 && d <= r1 * r1) Plot(cx + dx, cy + dy, c);
        }
}

void Digit(int cx, int cy, int n, int scale, int c) {
    uint16_t bits = kDigits[n % 10];
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 3; ++x)
            if (bits & (1 << (14 - (y * 3 + x))))
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx)
                        Plot(cx + (x - 1) * scale - scale / 2 + sx, cy + (y - 2) * scale - scale / 2 + sy, c);
}

bool OnMinimap(int x, int y);

// An outline of world rectangle [x0, x1) x [y0, y1): two pixels of `c` inside a dark one.
void Outline(int x0, int y0, int x1, int y1, int c, int f, int camX, int camY) {
    int a = (x0 - camX) / f, b = (y0 - camY) / f, r = (x1 - camX) / f - 1, d = (y1 - camY) / f - 1;
    for (int k = 0; k < 3; ++k) {
        int col = k == 0 ? g_dark : c;
        int ax = a + k - 1, bx = b + k - 1, rx = r - k + 1, dx = d - k + 1;
        for (int x = ax; x <= rx; ++x) Plot(x, bx, col), Plot(x, dx, col);
        for (int y = bx; y <= dx; ++y) Plot(ax, y, col), Plot(rx, y, col);
    }
}

// The last ghost drawn (tests): kind (0 none, 1 brush, 2 fill, 3 footprint,
// 4 start point) and its world rectangle.
int g_ghost[5];

// What the mouse would do on the map, before the click: the cells the brush
// paints, the rectangle a Shift+drag fills, the footprint a thing takes.
// Computed from the cursor and the camera as they are at draw time (smooth
// zoom changes the frame size after the tick).
void DrawGhost(int f, int camX, int camY) {
    int sx = game::at<short>(game::CursorX), sy = game::at<short>(game::CursorY);
    g_ghost[0] = 0;
    if (g.phase != kIdle || g.showFindings || OnPanel(sx, sy) || OnMinimap(sx, sy)) return;
    int wx, wy;
    bool in = WorldAt(sx, sy, wx, wy);
    int c = g_yellow > 0 ? g_yellow : 255;
    int cx = std::clamp(wx >> 6, 0, g_grid.Width() - 1), cy = std::clamp(wy >> 6, 0, g_grid.Height() - 1);
    if (g.tab == kTabTerrain) {
        if (g.drag == kDragFill) {
            int x0 = std::min(g.fillX, cx), x1 = std::max(g.fillX, cx), y0 = std::min(g.fillY, cy), y1 = std::max(g.fillY, cy);
            Outline(x0 * 64, y0 * 64, (x1 + 1) * 64, (y1 + 1) * 64, c, f, camX, camY);
            g_ghost[0] = 2, g_ghost[1] = x0 * 64, g_ghost[2] = y0 * 64, g_ghost[3] = (x1 + 1) * 64, g_ghost[4] = (y1 + 1) * 64;
        } else if (in) {
            int x0 = std::max(0, BrushFirst(cx, g.brush)), x1 = std::min(g_grid.Width() - 1, BrushLast(cx, g.brush));
            int y0 = std::max(0, BrushFirst(cy, g.brush)), y1 = std::min(g_grid.Height() - 1, BrushLast(cy, g.brush));
            Outline(x0 * 64, y0 * 64, (x1 + 1) * 64, (y1 + 1) * 64, c, f, camX, camY);
            g_ghost[0] = 1, g_ghost[1] = x0 * 64, g_ghost[2] = y0 * 64, g_ghost[3] = (x1 + 1) * 64, g_ghost[4] = (y1 + 1) * 64;
        }
        return;
    }
    int type = -1;
    if (g.tab == kTabThings && !g_thingList.empty())
        type = g_thingList[std::clamp(g.thingItem, 0, (int)g_thingList.size() - 1)].type;
    else if (g.tab == kTabUnits && g.unitMode != kModeSelect && !UnitList().empty())
        type = UnitList()[std::clamp(g.unitItem, 0, (int)UnitList().size() - 1)].type;
    if (type >= 0 && in) {
        int x0, y0, x1, y1;
        world::PlacementRect(type, wx, wy, x0, y0, x1, y1);
        Outline(x0, y0, x1, y1, c, f, camX, camY);
        g_ghost[0] = 3, g_ghost[1] = x0, g_ghost[2] = y0, g_ghost[3] = x1, g_ghost[4] = y1;
    } else if (g.tab == kTabPlayers && in) {  // the start point the click would set
        Ring((cx * 64 + 32 - camX) / f, (cy * 64 + 32 - camY) / f, 24 / f, 26 / f, c);
    }
}

// Each player's start point as a ring in its colour with its number (the 1998
// editor drew PLYRSTRT.MHK, which retail doesn't have), a box round the
// selected thing, and the ghost of the current tool at the cursor.
void __cdecl DrawOverlay() {
    if (!g.active || g_dark < 0) return;
    uintptr_t sg = Game();
    int f = F<short>(sg, game::SGameZoomedOut) == 1 ? 2 : 1;
    int camX = F<short>(sg, game::SGameCameraX), camY = F<short>(sg, game::SGameCameraY);
    g_drawn[0] = g_drawn[1] = 1 << 30;
    g_drawn[2] = g_drawn[3] = -(1 << 30);
    DrawGhost(f, camX, camY);
    for (int i = 0; i < 8; ++i) {
        PlayerRec r = world::CapturePlayer(i);
        if (r.type == world::kNone) continue;
        int x = (r.startX * 64 + 32 - camX) / f, y = (r.startY * 64 + 32 - camY) / f;
        int rr = 26 / f;
        bool chosen = g.tab == kTabPlayers && i == g.player;
        Ring(x, y, rr - 4, rr + 1, g_dark);
        Ring(x, y, rr - 3, rr, r.colour);
        if (chosen) Ring(x, y, rr + 3, rr + 4, g_yellow > 0 ? g_yellow : r.colour);
        Digit(x + 1, y + 1, i + 1, 4 / f + 1, g_dark);
        Digit(x, y, i + 1, 4 / f + 1, r.colour);
    }
    int id = g.selHandle ? IdOf(g.selHandle) : kNoThing;
    if (uintptr_t t = world::Thing(id)) {
        int x = (world::CentreX(t) - camX) / f, y = (world::CentreY(t) - camY) / f, h = 22 / f;
        for (int k = -h; k <= h; ++k) {
            int c = g_yellow > 0 ? g_yellow : 255;
            Plot(x + k, y - h, c), Plot(x + k, y + h, c), Plot(x - h, y + k, c), Plot(x + h, y + k, c);
        }
    }
    MarkDrawnDirty(f, camX, camY);
}

// --- input -----------------------------------------------------------------------

typedef short(__thiscall* Mouse_t)(void* sg, int x, int y);
Mouse_t o_LDown, o_LUp, o_RDown, o_RUp, o_Move, o_Dbl;

bool OnMinimap(int x, int y) {
    return ((short(__thiscall*)(uintptr_t))kRadarShown)(kRadar) == 1 &&
           ((short(__thiscall*)(uintptr_t, int, int))kRadarHit)(kRadar, x, y) == 1;
}

bool Editing() { return g.active && (uintptr_t)game::at<void*>(game::Scenes) == Game(); }

// A left press on the map, by tab.
void MapDown(int wx, int wy) {
    int cx = wx >> 6, cy = wy >> 6;
    g.exitArmed = g.testArmed = false;
    if (g.showFindings) g.showFindings = false;
    switch (g.tab) {
    case kTabTerrain:
        if (g.shift || GetKeyState(VK_SHIFT) < 0) {  // Shift: drag a rectangle to fill
            BeginStroke(true, cx, cy);
            SetStatus("Fill: drag the rectangle");
        } else {
            BeginStroke(false, cx, cy);
            PaintTo(cx, cy);
        }
        break;
    case kTabThings:
        g.drag = kDragThings;
        g.building = Edit();
        g.dragNote.clear();
        g.dragCount = 0;
        g.lastX = g.lastY = -1;
        PlaceThingAt(wx, wy);
        break;
    case kTabUnits:
        if (g.unitMode != kModeSelect) {
            PlaceUnitAt(wx, wy);
        } else {
            int id = world::PickAt(wx, wy);
            SelectThing(id);
            if (id != kNoThing) {
                g.drag = kDragMove;
                g.downWX = wx;
                g.downWY = wy;
            } else {
                SetStatus("Nothing there", false);
            }
        }
        break;
    case kTabPlayers: SetStartPoint(cx, cy); break;
    default:
        if (g_pages[g.tab]) g_pages[g.tab]->OnMapClick(cx, cy, wx, wy, false);
        break;
    }
}

short __thiscall HookLDown(void* sg, int x, int y) {
    if (!Editing()) return o_LDown(sg, x, y);
    x = (short)x;
    y = (short)y;
    if (const Slot* s = SlotAt(x, y)) {
        Click(*s, false);
        return 1;
    }
    if (OnPanel(x, y)) return 1;
    if (OnMinimap(x, y)) return o_LDown(sg, x, y);  // the camera jumps there
    if (g.phase == kMapNaming) ConfirmMapName();
    if (g.phase != kIdle || g.drag != kNoDrag) return 1;
    int wx, wy;
    if (!WorldAt(x, y, wx, wy)) return 1;
    MapDown(wx, wy);
    return 1;
}

short __thiscall HookMove(void* sg, int x, int y) {
    short r = o_Move(sg, x, y);  // edge scrolling, minimap drag
    if (Editing() && (g.drag == kDragStroke || g.drag == kDragThings)) {
        int wx, wy;
        if (WorldAt((short)x, (short)y, wx, wy)) {
            if (g.drag == kDragStroke)
                PaintTo(wx >> 6, wy >> 6);
            else
                PlaceThingAt(wx, wy);
        }
    }
    return r;
}

short __thiscall HookLUp(void* sg, int x, int y) {
    if (!Editing()) return o_LUp(sg, x, y);
    int wx, wy;
    WorldAt((short)x, (short)y, wx, wy);
    switch (g.drag) {
    case kDragFill: {
        int cx = std::clamp(wx >> 6, 0, g_grid.Width() - 1), cy = std::clamp(wy >> 6, 0, g_grid.Height() - 1);
        Changes ch;
        if (!g.painter->Fill(g_grid, g.type, g.fillX, g.fillY, cx, cy, ch)) SetStatus(g.painter->Message());
        if (!ch.empty()) Redraw();
        EndStroke("Filled");
        return 1;
    }
    case kDragStroke: EndStroke("Painted"); return 1;
    case kDragThings: EndThingsDrag(); return 1;
    case kDragMove:
        g.drag = kNoDrag;
        if (abs(wx - g.downWX) > 12 || abs(wy - g.downWY) > 12) MoveSelected(wx, wy);
        return 1;
    default: break;
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
    if (const Slot* s = SlotAt(x, y)) {
        Click(*s, true);
        return 1;
    }
    int wx, wy;
    if (g.phase != kIdle || g.drag != kNoDrag || OnPanel(x, y) || OnMinimap(x, y) || !WorldAt(x, y, wx, wy)) return 1;
    int cx = wx >> 6, cy = wy >> 6;
    switch (g.tab) {
    case kTabTerrain: {
        int t = g.painter->Pick(g_grid, cx, cy);
        if (t >= 0) {
            g.type = t;
            g.page = t / kPalRows;
            SetStatus("Picked " + g.painter->TypeName(t) + " (tile " + std::to_string(g_grid.Get(cx, cy)) + ")");
        }
        break;
    }
    case kTabThings: RemoveThingAt(wx, wy); break;
    case kTabUnits:
        g.selHandle = 0;
        ShowSelection();
        break;
    default:
        if (g.tab >= kTabEvents && g_pages[g.tab]) g_pages[g.tab]->OnMapClick(cx, cy, wx, wy, true);
        break;
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
    // The map's fog mode as the Map tab shows it, for every player: the paused
    // world has still marked the cells round each unit explored.
    if (ours) world::SetFogMode(g.fogMode, g.fogWas);
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

// SaveGame names the map after its file; an editor save keeps the name set in
// the Map tab (a new map without one still takes the file's).
void __thiscall HookSetMapName(void* info, const char* fileName) {
    const char* name = g.active && !g.mapName.empty() ? g.mapName.c_str() : fileName;
    ((void(__thiscall*)(void*, const char*))kSetMapName)(info, name);
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

// --- Test skirmish (autopilot only): from the hub to a started skirmish ----------------------------------

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
        g_h.log("editor: test skirmish: opening the skirmish setup");
    } else if (g_test.stage == 2 && vt == kVtblMultiSetup && idle && now - g_test.at > 1500) {
        ((void(__cdecl*)(int, int, int))kSceneSend)(5, 0, (int)(uintptr_t)g_test.file.c_str());  // the map
        g_test.stage = 3;
        g_test.at = now;
        g_h.log("editor: test skirmish: map %s chosen", g_test.file.c_str());
    } else if (g_test.stage == 3 && vt == kVtblMultiSetup && idle && now - g_test.at > 1500) {
        void* start = *(void**)(top + kMultiSetupStartButton);
        g_h.log("editor: test skirmish: Start");
        g_test.stage = 0;
        if (start) ((void(__thiscall*)(uintptr_t, void*))kMultiSetupStart)(top, start);
    } else if (g_test.stage && now - g_test.at > 30000) {
        g_h.log("editor: test skirmish: gave up (stage %d)", g_test.stage);
        g_test.stage = 0;
    }
}

}  // namespace

// --- tabs.h -----------------------------------------------------------------------------

void RegisterTab(Tab tab, TabPage* page) {
    if (tab >= kTabEvents && tab < kTabCount) g_pages[tab] = page;
}

void PushCustomEdit(const std::string& what, std::function<void(bool forward)> apply) {
    Edit e;
    e.what = what;
    Op op;
    op.kind = Op::kCustom;
    op.custom = std::move(apply);
    e.ops.push_back(std::move(op));
    Push(std::move(e));
}

void SetPanelStatus(const std::string& text) { SetStatus(text); }

// --- public ----------------------------------------------------------------------------

void Install(const Hooks& h) {
    g_h = h;
    bool ok = PatchSlot(kSlotLDown, (void*)&HookLDown, &o_LDown) && PatchSlot(kSlotLUp, (void*)&HookLUp, &o_LUp) &&
              PatchSlot(kSlotRDown, (void*)&HookRDown, &o_RDown) && PatchSlot(kSlotRUp, (void*)&HookRUp, &o_RUp) &&
              PatchSlot(kSlotMove, (void*)&HookMove, &o_Move) && PatchSlot(kSlotDbl, (void*)&HookDbl, &o_Dbl);
    bool save = PatchCall(kSaveGameCall, kSaveGame, (void*)&HookSaveGame) &&
                PatchCall(kStoreCameraCall, kStoreCamera, (void*)&HookStoreCamera) &&
                PatchCall(kSetMapNameCall, kSetMapName, (void*)&HookSetMapName);
    h.log("editor: %s, save hooks %s", ok ? "installed" : "FAILED", save ? "installed" : "FAILED (unexpected code)");
    if (!ReadArchive(h.dir + "DATA\\TERRAIN.MHK", g_terrainFile, &g_terrainError)) g_terrainFile.clear();
    layers::SetWorldOverlay(&DrawOverlay);
    events::Install(h.log);
    // Stage 3's editors on the panel's Events and AI tabs. Choosing a tab opens
    // its editor (a scene over the map); the tab's page can close and reopen it.
    struct EventsTab : TabPage {
        const char* Label() const override { return "Events"; }
        void OnShow() override {
            if (!events::Active()) events::Open();
        }
        void Items(std::vector<PanelItem>& v) override {
            bool on = events::Showing();
            PanelItem open;
            open.row = 0;
            open.text = on ? "Close event editor" : "Open event editor";
            open.id = 0;
            open.lit = on;
            v.push_back(open);
            const char* help[] = {"The map's triggers: conditions", "and the actions they start.",
                                  "Shift: event, Ctrl: condition,", "Ctrl+Shift: action; with Left /",
                                  "Right, A add, R remove.", "* persistent, Esc closes."};
            for (int i = 0; i < 6; ++i) {
                PanelItem t;
                t.row = 2 + i;
                t.text = help[i];
                v.push_back(t);
            }
        }
        void OnClick(int id, bool) override {
            if (id != 0) return;
            if (events::Showing())
                events::Close();
            else if (events::Active())
                events::OpenPanel(nullptr);
            else
                events::Open();
        }
    };
    struct AiTab : TabPage {
        const char* Label() const override { return "AI"; }
        void OnShow() override {
            if (!ai::Active()) ai::Open();
        }
        void Items(std::vector<PanelItem>& v) override {
            bool on = ai::Active();
            PanelItem open;
            open.row = 0;
            open.text = on ? "Close Global AI" : "Open Global AI";
            open.id = 0;
            open.lit = on;
            v.push_back(open);
            const char* help[] = {"Each computer player's squad,", "base, scouting and influence.",
                                  "A skirmish resets these to the", "clan's defaults; they apply when",
                                  "the map is played without the", "skirmish setup, or through an",
                                  "AI AGGRESSION event."};
            for (int i = 0; i < 7; ++i) {
                PanelItem t;
                t.row = 2 + i;
                t.text = help[i];
                v.push_back(t);
            }
        }
        void OnClick(int id, bool) override {
            if (id != 0) return;
            if (ai::Active())
                ai::Close();
            else
                ai::Open();
        }
    };
    static EventsTab eventsTab;
    static AiTab aiTab;
    RegisterTab(kTabEvents, &eventsTab);
    RegisterTab(kTabAi, &aiTab);
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
    if (g.active && !g_pool.empty()) UpdatePanel();
    events::Tick();
    ai::Tick();
}

bool Active() { return g.active; }
bool PanelContains(int x, int y) { return g.active && OnPanel(x, y); }

bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (!Editing()) return false;
    if (g.phase != kNaming && events::Active()) return events::OnMessage(msg, wp, lp);  // Stage 3
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        if (wp == VK_CONTROL) g.ctrl = msg == WM_KEYDOWN;
        if (wp == VK_SHIFT) g.shift = msg == WM_KEYDOWN;
        if (wp >= VK_LEFT && wp <= VK_DOWN) return false;  // camera
    }
    if (msg == WM_KEYDOWN) {
        bool ctrl = g.ctrl || GetKeyState(VK_CONTROL) < 0;
        if (g.phase == kNaming) {
            if (wp == VK_RETURN) ConfirmName();
            else if (wp == VK_ESCAPE) { g.phase = kIdle; g.testAfter = false; SetStatus("Not saved"); }
            else if (wp == VK_BACK && !g.name.empty()) { g.name.pop_back(); g.replaceArmed = false; ShowNaming(); }
        } else if (g.phase == kMapNaming) {
            if (wp == VK_RETURN) ConfirmMapName();
            else if (wp == VK_ESCAPE) { g.phase = kIdle; SetStatus("Name unchanged"); }
            else if (wp == VK_BACK && !g.mapNameEdit.empty()) g.mapNameEdit.pop_back();
        } else if (wp == VK_TAB) {
            if (!(lp & 0x40000000)) ToggleCollapsed();  // not auto-repeat
        } else if (ctrl && wp == 'Z') {
            Undo();
        } else if (ctrl && wp == 'Y') {
            Redo();
        } else if (wp == VK_DELETE && g.tab == kTabUnits && g.selHandle) {
            DeleteSelected();
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
        } else if (g.phase == kMapNaming) {
            char c = (char)wp;
            if (c >= 32 && c < 127 && g.mapNameEdit.size() < 30) g.mapNameEdit += c;
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
bool Idle() { return Editing() && g.phase == kIdle && g.drag == kNoDrag; }
int SaveCount() { return g.saves; }
int TestStage() { return g_test.stage; }
int LateMoves() { return g_lateMoves; }

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
    return WorldPoint(cx * 64 + 32, cy * 64 + 32, out);
}

bool WorldPoint(int wx, int wy, POINT& out) {
    if (!Editing()) return false;
    int f = F<short>(Game(), game::SGameZoomedOut) == 1 ? 2 : 1;
    out.x = (wx - F<short>(Game(), game::SGameCameraX)) / f;
    out.y = (wy - F<short>(Game(), game::SGameCameraY)) / f;
    return out.x >= 0 && out.y >= 0 && out.x < game::at<short>(game::ScreenRight) &&
           out.y < game::at<short>(game::ScreenBottom);
}

bool FindItem(const char* text, long& x, long& y) {
    for (const Slot& s : g_pool)
        if (s.used && s.it.text == text) {
            short* r = Rect(s.b);
            x = (r[0] + r[2]) / 2;
            y = (r[1] + r[3]) / 2;
            return true;
        }
    return false;
}

int CurrentTab() { return g.tab; }
std::string GhostInfo() {
    char buf[80];
    snprintf(buf, sizeof buf, "%d %d %d %d %d", g_ghost[0], g_ghost[1], g_ghost[2], g_ghost[3], g_ghost[4]);
    return buf;
}
bool Collapsed() { return g.collapsed; }
bool UnderInterface(POINT frame);
bool Hidden(POINT frame) { return OnPanel(frame.x, frame.y) || UnderInterface(frame); }

std::vector<std::string> Findings() {
    std::vector<std::string> out;
    for (const Finding& f : g.findings) out.push_back(f.text);
    return out;
}

// A frame point whose battlefield pixel is hidden on screen by the panel or the
// minimap (they are drawn at the interface scale, not the world's, so a frame
// test isn't enough).
bool UnderInterface(POINT frame) {
    // Corners taken just inside each element (outside one, the mapping is the world's).
    POINT c = render::GamePointToClient(frame);
    const RECT boxes[] = {
        {PanelLeft() - 5, PanelTop() - 5, PanelLeft() + PanelW() - 1, PanelTop() + PanelRows() * kRowH + 1},
        {40, 40, 140, 140}};
    for (const RECT& b : boxes) {
        POINT a = render::InterfacePointToClient({b.left, b.top}), z = render::InterfacePointToClient({b.right, b.bottom});
        int m = &b == &boxes[0] ? 4 : (z.x - a.x) / 4 + 24;  // the minimap is larger than its sampled box
        if (c.x >= a.x - m && c.x < z.x + m && c.y >= a.y - m && c.y < z.y + m) return true;
    }
    return false;
}

bool FindCell(int what, int nearX, int nearY, int& cx, int& cy, int clearPx) {
    // Cells near (nearX, nearY), nearest first, that are on screen and whose four
    // tiles suit `what`, with nothing within two cells.
    int bestD = 1 << 30;
    for (int y = 1; y < g_grid.Height() - 1; ++y)
        for (int x = 1; x < g_grid.Width() - 1; ++x) {
            POINT p;
            if (!CellCentre(x, y, p) || OnPanel(p.x, p.y) || p.x < 60 || p.y < 60 || OnMinimap(p.x, p.y) ||
                UnderInterface(p))
                continue;
            int d = (x - nearX) * (x - nearX) + (y - nearY) * (y - nearY);
            if (d >= bestD) continue;
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
                int tx = 2 * x + (k & 1), ty = 2 * y + (k >> 1);
                if (what == kCellOpen) ok = world::TerrainType(tx, ty) == 0;
                else if (what == kCellPodSoil) ok = world::TerrainType(tx, ty) == 2;
                else ok = CornerAt(tx, ty) == (what == kCellRoots ? 7 : what == kCellCrushed ? 1 : 4);
            }
            for (int id = 0; id < world::ThingCount() && ok; ++id)
                if (uintptr_t t = world::Thing(id))
                    ok = abs(world::CentreX(t) - (x * 64 + 32)) > clearPx || abs(world::CentreY(t) - (y * 64 + 32)) > clearPx;
            if (ok) {
                bestD = d;
                cx = x;
                cy = y;
            }
        }
    return bestD < (1 << 30);
}

int CountThings(int type, int owner) {
    int n = 0;
    for (int id = 0; id < world::ThingCount(); ++id)
        if (uintptr_t t = world::Thing(id))
            if ((type < 0 || world::TypeOf(t) == type) && (owner < 0 || world::OwnerOf(t) == owner)) ++n;
    return n;
}

bool ThingPoint(int type, int owner, POINT& out) {
    int wx, wy;
    return ThingWorld(type, owner, wx, wy) && WorldPoint(wx, wy, out);
}

std::string Selected() {
    int id = g.selHandle ? IdOf(g.selHandle) : kNoThing;
    ThingRec r;
    if (!world::Capture(id, r)) return "none";
    char buf[160];
    snprintf(buf, sizeof buf, "%d %d %d %d %d %d %d %d,%d,%d,%d", id, r.type, r.owner, r.x, r.y, r.hp, r.vet, r.gizmo[0],
             r.gizmo[1], r.gizmo[2], r.gizmo[3]);
    return buf;
}

int UndoDepth() { return (int)g.undo.size(); }

bool FindItemOnRow(int pageRow, int index, long& x, long& y) {
    int k = 0;
    for (const Slot& s : g_pool)
        if (s.used && s.clickable && s.it.row == kPageFirstRow + pageRow && k++ == index) {
            short* r = Rect(s.b);
            x = (r[0] + r[2]) / 2;
            y = (r[1] + r[3]) / 2;
            return true;
        }
    return false;
}

std::string ThingName(int type) { return world::Name(type); }

std::string PlayerInfo(int i) {
    PlayerRec r = world::CapturePlayer(i);
    char buf[120];
    snprintf(buf, sizeof buf, "%d %d %d %d,%d,%d,%d %d,%d,%d,%d %d %d", r.type, r.race, r.colour, r.cgk[0], r.cgk[1],
             r.cgk[2], r.cgk[3], r.avail[0], r.avail[1], r.avail[2], r.avail[3], r.startX, r.startY);
    return buf;
}

std::string MapInfoText() {
    return Num(world::Supported()) + " " + Num(world::Recommended()) + " " + Num(g.fogMode) + " " + g.mapName;
}

bool ThingWorld(int type, int owner, int& wx, int& wy) {
    for (int id = world::ThingCount() - 1; id >= 0; --id)
        if (uintptr_t t = world::Thing(id)) {
            int tt = world::TypeOf(t);
            if ((type == -2 ? world::IsShaman(tt) : tt == type) && (owner < 0 || world::OwnerOf(t) == owner)) {
                wx = world::CentreX(t);
                wy = world::CentreY(t);
                return true;
            }
        }
    return false;
}

void LookAt(int wx, int wy) {
    uintptr_t sg = Game();
    int f = F<short>(sg, game::SGameZoomedOut) == 1 ? 2 : 1;
    (void)f;
    F<short>(sg, game::SGameCameraX) = (short)(wx - F<short>(sg, game::SGameViewW) / 2);
    F<short>(sg, game::SGameCameraY) = (short)(wy - F<short>(sg, game::SGameViewH) / 2);
    ((void(__thiscall*)(uintptr_t))game::SGameClampCamera)(sg);
    Redraw();
}

}  // namespace editor
