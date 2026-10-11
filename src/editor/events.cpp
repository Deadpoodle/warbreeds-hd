// In-game map editor, Stage 3: the event editor (re/editor-events.md).
//
// The retail game still contains the 1998 editor's engine-drawn event editor:
// the scene SEvent (static object 0x5790d8, scene id 0x16), its panel (the tan
// picture 0x150 of the sheet at [0x5507e8]), every condition and action class
// with its own boxes, the type menus and the numeric, text, cycling and point
// entry. Nothing in the game ever pushes it. Pushed over the mission scene the
// editor uses (QueueSceneChange 0x1f, 0x16, as the 1998 editor's Tools > Event
// Editor did with its own scene id 3), it works as it did there: it shows
// g_Events (AEventRegistry [0x579284]), the map's live trigger list, which the
// game's saver writes ("ER" section).
//
// The 1998 editor's toolbar buttons called separate SEvent methods; in the
// retail build those are inlined into SEvent::KeyDown 0x478850, keyed by the
// modifiers: Shift = event, Ctrl = condition, Ctrl+Shift = action; with Left /
// Right (previous / next), Home / End, A (add), R (remove); '*' toggles the
// persistent flag; Esc pops the scene. Our toolbar is drawn next to the panel
// and calls KeyDown with those keys, so real keyboards get the same keys free.
//
// We patch four of SEvent's vtable slots (the class's own, nothing shared):
// Draw (guards and our toolbar), LButtonDown (toolbar clicks), KeyDown (panels,
// jump to an added item), AcceptsMouse (the editor's tools panel and the
// minimap stay usable underneath).
#include "events.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../game.h"
#include "../layers.h"
#include "../render/render.h"
#include "editor.h"

namespace editor {
namespace events {
namespace {

void (*g_log)(const char*, ...);

// --- engine addresses ----------------------------------------------------------

constexpr uintptr_t kGame = 0x567ef8;        // SGame static object (scene id 2)
constexpr uintptr_t kSEvent = 0x5790d8;      // SEvent static object (scene id 0x16)
constexpr int kSceneId = 0x16;
constexpr uintptr_t kVtblSEvent = 0x50653c;
constexpr uintptr_t kSlotDraw = kVtblSEvent + 0x04, kSlotLDown = kVtblSEvent + 0x1c,
                    kSlotKey = kVtblSEvent + 0x34, kSlotAccepts = kVtblSEvent + 0x3c;
constexpr uintptr_t kEvents = 0x579284;      // AEventRegistry*: +8 AEvent**, +0xc count
constexpr uintptr_t kQueueScene = 0x4709a0;  // cdecl (cmd, id, delay)
constexpr uintptr_t kSceneCmdPending = 0x5792dc;
constexpr uintptr_t kClickSound = 0x46fa70;  // cdecl ()
constexpr uintptr_t kPictures = 0x5507e8;    // picture sheet*: frame 0x150 = the tan event panel
constexpr uintptr_t kDrawPicture = 0x4675f0; // thiscall sheet (frame, x, y, colour, flip, dx, dy)
constexpr uintptr_t kTextObject = 0x551558;
constexpr uintptr_t kDrawText = 0x4696e0;    // thiscall 0x551558 (text, x, y, style)
constexpr uintptr_t kRadar = 0x5517b0;       // minimap
constexpr uintptr_t kRadarShown = 0x433e10, kRadarHit = 0x4335b0;  // Minimap short(), short(x, y)

// SEvent fields (after the 0x38-byte Scene).
constexpr uintptr_t kEventIndex = 0x38;  // short
constexpr uintptr_t kCondIndex = 0x3a;   // u8
constexpr uintptr_t kActIndex = 0x3b;    // u8
constexpr uintptr_t kColumn = 0x3c;      // u8: 0 none, 1 condition, 2 action
constexpr uintptr_t kBox = 0x3d;         // u8: box in the column (0 = the type)
// AEvent: +4 ACondition**, +8 count, +0x14 AAction**, +0x18 count, +0x20 short persistent.
// Elements (ACondition / AAction): +4/+6 panel position, +8 type, +0xc first field
// (AAction: the delay).

// Element sizes by type (the factories' allocations, ConditionFactory 0x4123d0 and
// ActionFactory 0x412600), for comparing the registry with its saved state.
constexpr int kCondSize[9] = {0x10, 0x1c, 0x10, 0x18, 0x18, 0x10, 0x10, 0x28, 0x14};
constexpr int kActSize[28] = {0x34, 0x24, 0x18, 0x1c, 0x14, 0x14, 0x34, 0x14, 0x1c, 0x1c, 0x14, 0x14, 100, 0x60,
                              0x18, 0x1c, 0x20, 0x20, 0x20, 0x14, 0x28, 0x18, 0x10, 0x24, 0x24, 0x28, 0x24, 0x1c};
// The action type menu's order (its "next" function 0x412280; alphabetical labels).
constexpr int kActMenu[28] = {0, 3, 5, 4, 0x1b, 0xc, 0x14, 1, 2, 9, 0x16, 0x18, 0x15, 0xb,
                              7, 0xe, 0x17, 0xd, 0x10, 0x11, 0xf, 0x12, 0x1a, 0x13, 0xa, 8, 6, 0x19};

template <typename T>
T& F(uintptr_t p, uintptr_t off) {
    return *(T*)(p + off);
}

uintptr_t TopScene() {
    short top = game::at<short>(game::SceneTop);
    return top < 0 || top > 8 ? 0 : (&game::at<uintptr_t>(game::Scenes))[top];
}

uintptr_t Registry() { return game::at<uintptr_t>(kEvents); }
uint32_t Count() { return Registry() ? F<uint32_t>(Registry(), 0xc) : 0; }
uintptr_t EventAt(uint32_t i) { return i < Count() ? F<uintptr_t*>(Registry(), 8)[i] : 0; }
uint32_t CondCount(uintptr_t e) { return F<uint32_t>(e, 8); }
uint32_t ActCount(uintptr_t e) { return F<uint32_t>(e, 0x18); }
uintptr_t Cond(uintptr_t e, uint32_t i) { return i < CondCount(e) ? F<uintptr_t*>(e, 4)[i] : 0; }
uintptr_t Act(uintptr_t e, uint32_t i) { return i < ActCount(e) ? F<uintptr_t*>(e, 0x14)[i] : 0; }

// --- state --------------------------------------------------------------------------

typedef void(__thiscall* Draw_t)(void*, int);
typedef short(__thiscall* Mouse_t)(void*, int, int);
typedef short(__thiscall* Key_t)(void*, int, int, int);
Draw_t o_Draw;
Mouse_t o_LDown, o_Accepts;
Key_t o_Key;

bool g_opening, g_open;
DWORD g_openAt;
Panel* g_panel;
bool g_session;        // the editor was active at the last tick
int g_saves = -1;      // the editor's save count when the saved state was taken
uint32_t g_savedPrint;
std::string g_hint;
int g_tan = -1, g_dark = -1;

// The registry's content (not the panel positions), for "unsaved changes".
uint32_t Fingerprint() {
    uint32_t h = 2166136261u;
    auto mix = [&h](const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) h = (h ^ ((const uint8_t*)p)[i]) * 16777619u;
    };
    uint32_t n = Count();
    mix(&n, 4);
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t e = EventAt(i);
        mix((void*)(e + 0x20), 2);
        uint32_t nc = CondCount(e), na = ActCount(e);
        mix(&nc, 4);
        mix(&na, 4);
        for (uint32_t k = 0; k < nc; ++k) {
            uintptr_t c = Cond(e, k);
            uint32_t t = F<uint32_t>(c, 8);
            mix((void*)(c + 8), (t < 9 ? kCondSize[t] : 0x10) - 8);
        }
        for (uint32_t k = 0; k < na; ++k) {
            uintptr_t a = Act(e, k);
            uint32_t t = F<uint32_t>(a, 8);
            mix((void*)(a + 8), (t < 28 ? kActSize[t] : 0x10) - 8);
        }
    }
    return h;
}

// The current event's indexes inside its lists (the stock code indexes without
// checks); false if there is nothing safe to draw (an event without a condition
// or an action: the panel would read a null element).
bool Clamp() {
    uint32_t n = Count();
    if (n == 0) return true;
    short& ei = F<short>(kSEvent, kEventIndex);
    if (ei < 0 || (uint32_t)ei >= n) ei = (short)(n - 1);
    uintptr_t e = EventAt(ei);
    if (!e || CondCount(e) == 0 || ActCount(e) == 0) return false;
    uint8_t& ci = F<uint8_t>(kSEvent, kCondIndex);
    uint8_t& ai = F<uint8_t>(kSEvent, kActIndex);
    if (ci >= CondCount(e)) ci = (uint8_t)(CondCount(e) - 1);
    if (ai >= ActCount(e)) ai = (uint8_t)(ActCount(e) - 1);
    return true;
}

// --- drawing ---------------------------------------------------------------------

void Text(const char* s, int x, int y, int style) {
    ((void(__thiscall*)(uintptr_t, const char*, int, int, int))kDrawText)(kTextObject, s, x, y, style);
}

void FillRect(int x0, int y0, int x1, int y1, int index) {
    if (!layers::Active() || index < 0) return;
    for (int y = y0; y < y1; ++y) {
        uintptr_t a = layers::InterfacePixelAddress(x0, y), b = layers::InterfacePixelAddress(x1 - 1, y);
        if (a && b && b >= a) memset((void*)a, index, b - a + 1);
    }
}

void FindColours() {
    // The panel's own tan (sampled where the picture is plain) and the darkest colour.
    uintptr_t p = layers::InterfacePixelAddress(8, 8);
    if (p) g_tan = *(uint8_t*)p;
    const render::Frame& f = render::CurrentFrame();
    int best = -1, sum = 1 << 30;
    for (int i = 1; i < 256; ++i) {
        const RGBQUAD& c = f.palette[i];
        int s = c.rgbRed + c.rgbGreen + c.rgbBlue;
        if (s < sum) sum = s, best = i;
    }
    g_dark = best;
}

// The toolbar, right of the panel.
constexpr int kBarX = 252, kBarY = 12, kBarW = 196, kRowH = 15, kCharW = 6;
constexpr int kStyle = 5, kStyleHover = 4, kStyleHint = 2;

struct Button {
    const char* label;
    Command cmd;
    int row, x;  // x relative to the bar
};
const Button kButtons[] = {
    {"Close", kClose, 0, 156},
    {"<", kPrevEvent, 1, 70},  {">", kNextEvent, 1, 86},  {"Add", kAddEvent, 1, 104},  {"Remove", kRemoveEvent, 1, 134},
    {"Persistent", kPersistent, 2, 70},
    {"<", kPrevCondition, 3, 70}, {">", kNextCondition, 3, 86}, {"Add", kAddCondition, 3, 104},
    {"Remove", kRemoveCondition, 3, 134},
    {"<", kPrevAction, 4, 70}, {">", kNextAction, 4, 86}, {"Add", kAddAction, 4, 104}, {"Remove", kRemoveAction, 4, 134},
};
constexpr int kBarRows = 9;

RECT ButtonRect(const Button& b) {
    int x = kBarX + b.x, y = kBarY + b.row * kRowH;
    return {x - 3, y - 11, x + (int)strlen(b.label) * kCharW + 3, y + 3};  // text at y is its baseline
}

bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

bool OnBar(int x, int y) { return x >= kBarX && x < kBarX + kBarW && y >= 0 && y < kBarY + kBarRows * kRowH + 4; }

const Button* ButtonAt(int x, int y) {
    for (const Button& b : kButtons)
        if (In(ButtonRect(b), x, y)) return &b;
    return nullptr;
}

std::vector<std::string> Wrap(const std::string& s, size_t width) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        size_t n = std::min(width, s.size() - at);
        if (at + n < s.size()) {
            size_t cut = s.rfind(' ', at + n);
            if (cut != std::string::npos && cut > at) n = cut - at;
        }
        out.push_back(s.substr(at, n));
        at += n;
        while (at < s.size() && s[at] == ' ') ++at;
    }
    return out;
}

std::string MakeHint() {
    if (Count() == 0) return "No events yet: Add makes one (a timed condition and an action).";
    switch (F<uint8_t>(kSEvent, kColumn)) {
    case 1:
    case 2:
        if (F<uint8_t>(kSEvent, kBox) == 0) return "Pick the type in the list on the right.";
        return "Type a number or text, click the box to cycle it, or click the map for a point.";
    default: return "Click a box to change it. Keys: Shift/Ctrl/Ctrl+Shift with arrows, A, R; * persistent; Esc closes.";
    }
}

void DrawBar() {
    if (g_tan < 0) FindColours();
    int y1 = kBarY + kBarRows * kRowH + 4;
    FillRect(kBarX, 0, kBarX + kBarW, y1, g_tan);
    FillRect(kBarX, y1 - 1, kBarX + kBarW, y1, g_dark);
    FillRect(kBarX + kBarW - 1, 0, kBarX + kBarW, y1, g_dark);
    Text("EVENTS", kBarX + 6, kBarY, kStyle);
    Text("Event", kBarX + 6, kBarY + kRowH, kStyle);
    Text("Condition", kBarX + 6, kBarY + 3 * kRowH, kStyle);
    Text("Action", kBarX + 6, kBarY + 4 * kRowH, kStyle);
    uintptr_t e = EventAt(F<short>(kSEvent, kEventIndex));
    bool persistent = e && F<short>(e, 0x20) == 1;
    short cx = game::at<short>(game::CursorX), cy = game::at<short>(game::CursorY);
    for (const Button& b : kButtons) {
        RECT r = ButtonRect(b);
        bool on = b.cmd == kPersistent && persistent;
        if (on) FillRect(r.left, r.top, r.left + 15 * kCharW + 6, r.bottom, g_dark);
        Text(b.cmd == kPersistent ? (persistent ? "Persistent: on" : "Persistent: off") : b.label, r.left + 3, r.bottom - 3,
             on ? kStyleHint : In(r, cx, cy) ? kStyleHover : kStyle);
    }
    g_hint = MakeHint();
    std::vector<std::string> lines = Wrap(g_hint, 30);
    for (size_t i = 0; i < lines.size() && i < 4; ++i)
        Text(lines[i].c_str(), kBarX + 6, kBarY + (5 + (int)i) * kRowH + 2, kStyleHint);
}

// --- SEvent hooks -------------------------------------------------------------

void __thiscall HookDraw(void* s, int full) {
    if (g_panel) {
        DrawPanelBackground();
        g_panel->Draw();
        return;
    }
    if (Clamp()) {
        o_Draw(s, full);
    } else {
        DrawPanelBackground();
        Text("This event has no condition or no action.", 20, 40, kStyle);
        Text("Remove it (Shift+R) or add one.", 20, 56, kStyle);
    }
    DrawBar();
}

short __thiscall HookLDown(void* s, int x, int y) {
    x = (short)x;
    y = (short)y;
    if (!g_panel)
        if (const Button* b = ButtonAt(x, y)) {
            ((void(__cdecl*)())kClickSound)();
            Do(b->cmd);
            return 1;
        }
    if (g_panel) {
        g_panel->Click(x, y);
        return 1;
    }
    if (OnBar(x, y)) return 1;
    if (!Clamp()) return 1;
    return o_LDown(s, x, y);
}

bool OnMinimap(int x, int y) {
    return ((short(__thiscall*)(uintptr_t))kRadarShown)(kRadar) == 1 &&
           ((short(__thiscall*)(uintptr_t, int, int))kRadarHit)(kRadar, x, y) == 1;
}

// The left button goes to the first scene from the top that accepts it: the
// editor's tools panel and the minimap belong to the mission scene below.
short __thiscall HookAccepts(void* s, int x, int y) {
    x = (short)x;
    y = (short)y;
    bool ours = (x < 250 && y < 414) || OnBar(x, y);
    if (!ours && (editor::PanelContains(x, y) || OnMinimap(x, y))) return 0;
    return o_Accepts(s, x, y);
}

short __thiscall HookKey(void* s, int vk, int mods, int ascii) {
    int k = vk & 0xff, m = mods & 0xff;
    if (g_panel) {
        if (k == VK_ESCAPE) {
            Close();
            return 1;
        }
        g_panel->Key(k, m, ascii & 0xff);
        return 1;
    }
    if (!Clamp() && k != VK_ESCAPE && !(m == 4 && (k == 'R' || k == 'A' || k == VK_LEFT || k == VK_RIGHT))) return 1;
    short r = o_Key(s, vk, mods, ascii);
    // Show what was just added (the stock code stays on the current item).
    if (k == 'A' && (m == 1 || m == 4 || m == 5)) o_Key(s, VK_END, m, 0);
    return r;
}

bool PatchSlot(uintptr_t slot, void* hook, void** orig) {
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *orig = *(void**)slot;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

// --- names (logs and tests) ------------------------------------------------------

const char* const kCondNames[9] = {"TIMED", "STEP ON POINT", "PLAYER DEATH", "PLAYER CGK", "PLAYER DEVICES",
                                   "ENEMY SEEN", "TRIGGER", "STEP IN RECT", "THING MISSING"};

std::string DescribeElement(uintptr_t el, bool action) {
    char buf[160];
    uint32_t t = F<uint32_t>(el, 8);
    if (!action) {
        if (t == 0)
            snprintf(buf, sizeof buf, "C0{%d}", F<int>(el, 0xc));
        else
            snprintf(buf, sizeof buf, "C%u", t);
        return buf;
    }
    int delay = F<int>(el, 0xc);
    if (t == 12)
        snprintf(buf, sizeof buf, "A12{%d %u '%.40s'}", delay, F<uint8_t>(el, 0x10), (const char*)(el + 0x11));
    else if (t == 11)
        snprintf(buf, sizeof buf, "A11{%d %u %d}", delay, F<uint8_t>(el, 0x10), F<short>(el, 0x12));
    else if (t == 21)
        snprintf(buf, sizeof buf, "A21{%d %u %u %u}", delay, F<uint16_t>(el, 0x10), F<uint16_t>(el, 0x12), F<uint16_t>(el, 0x14));
    else if (t == 7)
        snprintf(buf, sizeof buf, "A7{%d %u}", delay, F<uint8_t>(el, 0x10));
    else
        snprintf(buf, sizeof buf, "A%u{%d}", t, delay);
    return buf;
}

}  // namespace

// --- public ------------------------------------------------------------------------

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchSlot(kSlotDraw, (void*)&HookDraw, (void**)&o_Draw) &&
              PatchSlot(kSlotLDown, (void*)&HookLDown, (void**)&o_LDown) &&
              PatchSlot(kSlotKey, (void*)&HookKey, (void**)&o_Key) &&
              PatchSlot(kSlotAccepts, (void*)&HookAccepts, (void**)&o_Accepts);
    g_log("events: %s", ok ? "installed" : "FAILED");
}

bool Open() {
    if (Active() || !editor::Active() || game::at<short>(kSceneCmdPending) || TopScene() != kGame) return false;
    ((void(__cdecl*)(int, int, int))kQueueScene)(0x1f, kSceneId, 5);
    g_opening = true;
    g_openAt = GetTickCount();
    F<uint8_t>(kSEvent, kColumn) = 0;
    g_tan = -1;
    g_log("events: opening %s (%u events)", g_panel ? g_panel->Name() : "the event editor", Count());
    return true;
}

bool OpenPanel(Panel* p) {
    if (TopScene() == kSEvent) {
        g_panel = p;
        g_log("events: showing %s", p ? p->Name() : "the events");
        return true;
    }
    Panel* was = g_panel;
    g_panel = p;
    if (Open()) return true;
    g_panel = was;
    return false;
}

Panel* CurrentPanel() { return Active() ? g_panel : nullptr; }

void Close() {
    if (TopScene() != kSEvent || game::at<short>(kSceneCmdPending)) return;
    ((void(__cdecl*)(int, int, int))kQueueScene)(0x23, 0, 5);
    g_log("events: closing");
}

bool Active() { return TopScene() == kSEvent || g_opening; }
bool Showing() { return TopScene() == kSEvent && !g_panel; }

void Tick() {
    bool editing = editor::Active();
    if (editing && (!g_session || editor::SaveCount() != g_saves)) {
        // A map was opened, or the editor saved it: this is the saved state.
        g_savedPrint = Fingerprint();
        g_saves = editor::SaveCount();
    }
    g_session = editing;
    bool top = TopScene() == kSEvent;
    if (g_opening && (top || GetTickCount() - g_openAt > 3000)) {
        g_opening = false;
        g_log("events: %s %s", g_panel ? g_panel->Name() : "event editor", top ? "open" : "did NOT open");
    }
    if (top) g_open = true;
    if (g_open && !top && !g_opening) {
        g_open = false;
        g_panel = nullptr;
        g_log("events: closed (%u events)", Count());
    }
}

bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    // Keys reach the scene through the game's own key handler (it reads the
    // modifiers from the keyboard state).
    (void)msg;
    (void)wp;
    (void)lp;
    return false;
}

void Do(Command c) {
    if (TopScene() != kSEvent) return;
    if (c == kClose) {
        Close();
        return;
    }
    static const struct { int vk, mods; } kKeys[kCommandCount] = {
        {VK_LEFT, 4}, {VK_RIGHT, 4}, {'A', 4}, {'R', 4}, {0, 0},
        {VK_LEFT, 1}, {VK_RIGHT, 1}, {'A', 1}, {'R', 1},
        {VK_LEFT, 5}, {VK_RIGHT, 5}, {'A', 5}, {'R', 5},
        {0, 0},
    };
    static const char* const kNames[kCommandCount] = {
        "previous event", "next event", "add event", "remove event", "persistent",
        "previous condition", "next condition", "add condition", "remove condition",
        "previous action", "next action", "add action", "remove action", "close"};
    if (c == kPersistent)
        HookKey((void*)kSEvent, '8', 4, '*');
    else
        HookKey((void*)kSEvent, kKeys[c].vk, kKeys[c].mods, 0);
    g_log("events: %s -> event %d of %u", kNames[c], F<short>(kSEvent, kEventIndex) + 1, Count());
}

bool Dirty() { return g_session && Fingerprint() != g_savedPrint; }

void DrawPanelBackground() {
    void* sheet = game::at<void*>(kPictures);
    if (!sheet) return;
    ((void(__thiscall*)(void*, int, int, int, int, int, int, int))kDrawPicture)(sheet, 0x150, 100, 100, 0xf7, 0, 0, 0);
}

void DrawText(const char* s, int x, int y, int style) { Text(s, x, y, style); }

void Highlight(int x0, int y0, int x1, int y1) {
    if (g_dark < 0) FindColours();
    FillRect(x0, y0, x1, y1, g_dark);
}

bool Hover(int x0, int y0, int x1, int y1) {
    short x = game::at<short>(game::CursorX), y = game::at<short>(game::CursorY);
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

int EventCount() { return (int)Count(); }

std::string Describe() {
    std::string s;
    for (uint32_t i = 0; i < Count(); ++i) {
        uintptr_t e = EventAt(i);
        s += "E" + std::to_string(i) + (F<short>(e, 0x20) == 1 ? " P" : "");
        for (uint32_t k = 0; k < CondCount(e); ++k) s += " " + DescribeElement(Cond(e, k), false);
        s += " ->";
        for (uint32_t k = 0; k < ActCount(e); ++k) s += " " + DescribeElement(Act(e, k), true);
        s += "\n";
    }
    return s;
}

bool CommandCentre(Command c, POINT& frame) {
    for (const Button& b : kButtons)
        if (b.cmd == c) {
            RECT r = ButtonRect(b);
            frame = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            return true;
        }
    return false;
}

bool BoxCentre(int column, int box, POINT& frame) {
    if (column != 1 && column != 2) return false;
    int x = column == 1 ? 20 : 134, y = 100 + 32 * box;  // as SEvent::Draw places the columns
    frame = {x + 38, y + 2};
    return true;
}

bool MenuEntryCentre(int column, int type, POINT& frame) {
    int row = -1;
    if (column == 1 && type >= 0 && type < 9) row = type;
    for (int i = 0; column == 2 && i < 28; ++i)
        if (kActMenu[i] == type) row = i;
    if (row < 0) return false;
    frame = {0x218 + 42, 16 * (row + 1) + 6};  // 0x4118f0: x 0x218..0x26c, rows of 16 from y 16
    return true;
}

bool ColourEntryCentre(int colour, POINT& frame) {
    // 0x411b30: 247, 239 ... 191 from y 16 in rows of 32; hit test 0x411a20: y - 10 .. y + 14.
    if (colour > 247 || colour < 191 || (247 - colour) % 8) return false;
    frame = {0x218 + 42, 16 + 32 * ((247 - colour) / 8) + 2};
    return true;
}

int SelectedEvent() { return F<short>(kSEvent, kEventIndex); }
std::string Hint() { return g_hint; }

}  // namespace events
}  // namespace editor
