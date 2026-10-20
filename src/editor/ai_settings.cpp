// In-game map editor, Stage 3: the Global AI settings (re/editor-events.md).
//
// The 1998 editor's dialog 132 (CGlobalAIDlg, handler 0x404190) reads four
// floats of each computer player, shows them as trackbars of five steps (value
// x 4) and writes them back x 0.25: squad aggression, base defence and
// scouting through AIComputerPlayer::SetAggression (vtable +0x60: base, squad,
// explore), AI influence directly. In the retail game these are
// AIComputerPlayer +0x4d84 squad, +0x4d88 base, +0x4d8c explore (scouting) and
// +0x4d90 (the weight AIComputerPlayer 0x4b85a0 gives the settings against the
// AI's own estimate). All four are in the map's AI state (0x4b7280 writes
// +0x4d80..+0x4d90 as five floats), which the editor's save writes.
#include "ai_settings.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "../fonts.h"
#include "../game.h"
#include "../lang.h"
#include "editor.h"
#include "events.h"

namespace editor {
namespace ai {
namespace {

constexpr uintptr_t kPlayers = 0x57927c;    // APlayerRegistry*: +4 APlayer*[8]
constexpr uintptr_t kVtblAI = 0x509734;     // AIComputerPlayer
constexpr uintptr_t kClickSound = 0x46fa70;
constexpr uintptr_t kField[kSettingCount] = {0x4d84, 0x4d88, 0x4d8c, 0x4d90};
constexpr uintptr_t kRace = 0x4878, kColour = 0x19;

const char* const kLabels[kSettingCount] = {"Squad aggression", "Base", "Scouting", "AI influence"};
const char* const kLow[kSettingCount] = {"Low", "Defensive", "Low", "More"};
const char* const kHigh[kSettingCount] = {"High", "Offensive", "High", "Less"};
const char* const kRaces[4] = {"Tanu", "Sen-Soth", "Kelika", "Magha"};

uintptr_t Player(int slot) {
    uintptr_t reg = game::at<uintptr_t>(kPlayers);
    if (!reg || slot < 0 || slot >= 8) return 0;
    return *(uintptr_t*)(reg + 4 + slot * 4);
}

uintptr_t Computer(int slot) {
    uintptr_t p = Player(slot);
    return p && *(uintptr_t*)p == kVtblAI ? p : 0;
}

float& Value(uintptr_t p, Setting s) { return *(float*)(p + kField[s]); }

int LevelOf(float v) { return std::clamp((int)(v * 4.0f + 0.5f), 0, 4); }

uint32_t Fingerprint() {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8; ++i) {
        uintptr_t p = Computer(i);
        if (!p) continue;
        for (int s = 0; s < kSettingCount; ++s) {
            uint32_t bits;
            memcpy(&bits, &Value(p, (Setting)s), 4);
            h = (h ^ bits ^ (uint32_t)(i << 24)) * 16777619u;
        }
    }
    return h;
}

bool g_session;
int g_saves = -1;
uint32_t g_savedPrint;
int g_slot = -1;  // the player shown

// --- the panel (frame pixels inside the game's tan event panel) --------------------

// On the page's black background, without the styles' offset outline (it blurred thin letters).
constexpr int kStyle = fonts::kNoOutline + 5, kStyleLit = fonts::kNoOutline + 4, kStyleSmall = 2;
constexpr int kLeftX = 20, kStepX = 92, kStepW = 16, kRightX = 176, kTop = 100, kRowGap = 44;

RECT StepRect(Setting s, int level) {
    int x = kStepX + level * kStepW, y = kTop + kRowGap * s + 16;
    return {x - 4, y - 11, x + 10, y + 3};  // text at y is its baseline
}
RECT PlayerRect(int next) {
    int x = next ? 200 : kLeftX;
    return {x - 4, 52, x + 14, 66};
}
RECT CloseRect() { return {kLeftX - 4, 290, kLeftX + 34, 304}; }

bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

void Step(int dir) {
    for (int n = 1; n <= 8; ++n) {
        int s = ((g_slot < 0 ? 0 : g_slot) + dir * n + 16) % 8;
        if (Computer(s)) {
            g_slot = s;
            return;
        }
    }
}

std::string PlayerName(int slot) {
    uintptr_t p = Player(slot);
    if (!p) return lang::tr("No computer players");
    uint16_t race = *(uint16_t*)(p + kRace);
    return lang::trf("Player {1}: {2}", {std::to_string(slot + 1), lang::tr(race < 4 ? kRaces[race] : "?")});
}

class AIPanel : public events::Panel {
public:
    const char* Name() const override { return "the Global AI settings"; }

    void Draw() override {
        if (!Computer(g_slot)) Step(1);
        uintptr_t p = Computer(g_slot);
        events::DrawText(lang::tr("GLOBAL AI"), 60, 40, kStyle);
        RECT prev = PlayerRect(0), next = PlayerRect(1);
        if (p) {
            events::DrawText("<", prev.left + 4, prev.bottom - 3, Hover(prev) ? kStyleLit : kStyle);
            events::DrawText(">", next.left + 4, next.bottom - 3, Hover(next) ? kStyleLit : kStyle);
        }
        events::DrawText(PlayerName(p ? g_slot : -1).c_str(), kLeftX + 18, 63, kStyle);
        for (int s = 0; p && s < kSettingCount; ++s) {
            int y = kTop + kRowGap * s;
            events::DrawText(lang::tr(kLabels[s]), kLeftX, y, kStyle);
            events::DrawText(lang::tr(kLow[s]), kLeftX, y + 16, kStyleSmall);
            events::DrawText(lang::tr(kHigh[s]), kRightX, y + 16, kStyleSmall);
            int level = LevelOf(Value(p, (Setting)s));
            for (int l = 0; l < 5; ++l) {
                RECT r = StepRect((Setting)s, l);
                char digit[2] = {(char)('1' + l), 0};
                if (l == level) events::Highlight(r.left, r.top, r.right, r.bottom);
                events::DrawText(digit, r.left + 4, r.bottom - 3, l == level ? kStyleSmall : Hover(r) ? kStyleLit : kStyle);
            }
        }
        RECT c = CloseRect();
        events::DrawText(lang::tr("Close"), c.left + 4, c.bottom - 3, Hover(c) ? kStyleLit : kStyle);
        // Up to six lines of 34 characters (the panel's bottom edge).
        std::vector<std::string> hint = events::Wrap(
            lang::tr("Saved with the map, and kept in a skirmish on it (the game's own maps use the clan defaults). "
                     "An AI AGGRESSION event changes them during play."),
            34);
        for (size_t i = 0; i < hint.size() && i < 6; ++i)
            events::DrawText(hint[i].c_str(), kLeftX, 318 + 14 * (int)i, kStyleSmall);
    }

    bool Click(int x, int y) override {
        RECT c = CloseRect();
        if (In(c, x, y)) {
            ((void(__cdecl*)())kClickSound)();
            events::Close();
            return true;
        }
        for (int next = 0; next < 2; ++next)
            if (In(PlayerRect(next), x, y)) {
                ((void(__cdecl*)())kClickSound)();
                Step(next ? 1 : -1);
                return true;
            }
        uintptr_t p = Computer(g_slot);
        for (int s = 0; p && s < kSettingCount; ++s)
            for (int l = 0; l < 5; ++l)
                if (In(StepRect((Setting)s, l), x, y)) {
                    ((void(__cdecl*)())kClickSound)();
                    Value(p, (Setting)s) = l * 0.25f;
                    return true;
                }
        return false;
    }

    bool Key(int vk, int mods, int ascii) override {
        (void)mods;
        (void)ascii;
        if (vk == VK_LEFT || vk == VK_RIGHT) {
            Step(vk == VK_RIGHT ? 1 : -1);
            return true;
        }
        return false;
    }

private:
    static bool Hover(const RECT& r) { return events::Hover(r.left, r.top, r.right, r.bottom); }
} g_panel;

}  // namespace

bool Open() {
    if (Active()) return false;
    if (!Computer(g_slot)) Step(1);
    return events::OpenPanel(&g_panel);
}

void Close() {
    if (Active()) events::Close();
}

bool Active() { return events::Active() && events::CurrentPanel() == &g_panel; }

void Tick() {
    bool editing = editor::Active();
    if (editing && (!g_session || editor::SaveCount() != g_saves)) {
        g_savedPrint = Fingerprint();
        g_saves = editor::SaveCount();
        if (!g_session) g_slot = -1;
    }
    g_session = editing;
}

bool Dirty() { return g_session && Fingerprint() != g_savedPrint; }

int ComputerCount() {
    int n = 0;
    for (int i = 0; i < 8; ++i) n += Computer(i) != 0;
    return n;
}

int PlayerSlot(int computer) {
    for (int i = 0; i < 8; ++i)
        if (Computer(i) && computer-- == 0) return i;
    return -1;
}

int Level(int slot, Setting s) {
    uintptr_t p = Computer(slot);
    return p ? LevelOf(Value(p, s)) : -1;
}

std::string Describe() {
    std::string out;
    for (int i = 0; i < 8; ++i) {
        uintptr_t p = Computer(i);
        if (!p) continue;
        char buf[96];
        snprintf(buf, sizeof buf, "slot %d: squad %.2f base %.2f scouting %.2f influence %.2f\n", i,
                 Value(p, kSquad), Value(p, kBase), Value(p, kScouting), Value(p, kInfluence));
        out += buf;
    }
    return out;
}

bool StepCentre(Setting s, int level, POINT& frame) {
    if (s < 0 || s >= kSettingCount || level < 0 || level > 4) return false;
    RECT r = StepRect(s, level);
    frame = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    return true;
}

bool PlayerButtonCentre(int next, POINT& frame) {
    RECT r = PlayerRect(next);
    frame = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    return true;
}

bool CloseCentre(POINT& frame) {
    RECT r = CloseRect();
    frame = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    return true;
}

int ShownSlot() { return g_slot; }

}  // namespace ai
}  // namespace editor
