// Campaign progress kept between sessions.
//
// The 1998 game keeps campaign progress only in memory (and inside a saved
// game): quitting means starting the campaign again. Nothing carries over
// between missions (each map loads fresh), so the progress is just where the
// campaign is: ACampaign (g_Campaign 0x57928c; +4 u16 chapter, +0xc chapters,
// +0x10 count, +0x18 id) and the chapter's current mission set (AChapter +0
// u16; +0xc set count).
//   - Record: whenever a mission briefing opens (SMissionBrief::OnStart,
//     vtable slot 0x507d4c), that place is written to WarBreedsHD-progress.ini
//     ([Tanu] / [Magha]; Basic Training loops and isn't kept). Separate from
//     WarBreedsHD.ini, so resetting the settings never loses it.
//   - Completion: when the campaign's last mission is won (SReward's Continue,
//     through OnCommand 0x493720 or KeyUp 0x4935d0, sets g_CampaignComplete
//     0x5792ec), the record is cleared.
//   - Resume: on the campaign screen (SSinglePlayer, vtable 0x5088e4), clicking
//     a campaign with a record past its first mission swaps the three campaign
//     items for "Continue: mission N of M", "Start Over" and "Back" (the
//     engine's own text buttons, added hidden after Init). Continue runs the
//     stock start (ACampaign::Load, then the briefing is queued) and moves the
//     campaign to the recorded chapter and set before the briefing opens; Start
//     Over is the stock start.
#include "campaign.h"
#include "lang.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>

namespace campaign {
namespace {

void (*g_log)(const char*, ...);
std::string g_path;  // WarBreedsHD-progress.ini

constexpr uintptr_t kCampaign = 0x57928c, kCampaignComplete = 0x5792ec;
constexpr uintptr_t kBriefSlotStart = 0x507d4c, kBriefStart = 0x489d80;
constexpr uintptr_t kRewardSlotCommand = 0x5087e8, kRewardCommand = 0x493720;
constexpr uintptr_t kRewardSlotKeyUp = 0x5087b4, kRewardKeyUp = 0x4935d0;
constexpr uintptr_t kSingleVtbl = 0x5088e4;
constexpr uintptr_t kSingleSlotInit = kSingleVtbl + 5 * 4, kSingleInit = 0x494250;
constexpr uintptr_t kSingleSlotCommand = kSingleVtbl + 27 * 4, kSingleCommand = 0x494430;
constexpr uintptr_t kTextButtonVtbl = 0x50925c;
constexpr uintptr_t kAddTextButton = 0x495cb0, kSetStyle = 0x49a500, kSetText = 0x49a470;
constexpr uintptr_t kPlaySound = 0x464000, kSoundPlayer = 0x55aeb8;
constexpr int kSelectSound = 0x68;
constexpr int kStyleMenu = 0x10, kStyleMenuHover = 0x11;

// The campaign screen's items (command ids) and ours.
constexpr short kCmdTanu = 100, kCmdMagha = 0x65, kCmdTraining = 0x66, kCmdMainMenu = 0x67;
constexpr short kCmdTitle = 0x90, kCmdContinue = 0x91, kCmdStartOver = 0x92, kCmdBack = 0x93;

struct Info {
    short id;
    const char* section;
    const char* name;
};
const Info kCampaigns[] = {{1000, "Tanu", "Tanu Ascendancy"}, {1001, "Magha", "Magha Uprising"}};

const Info* ById(int id) {
    for (const Info& c : kCampaigns)
        if (c.id == id) return &c;
    return nullptr;
}
const Info* ByCommand(short cmd) {
    return cmd == kCmdTanu ? &kCampaigns[0] : cmd == kCmdMagha ? &kCampaigns[1] : nullptr;
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

// --- the campaign object ----------------------------------------------------

char* Campaign() { return *(char**)kCampaign; }
int Chapters(char* c) { return *(int*)(c + 0x10); }
char* Chapter(char* c, int i) { return (*(char***)(c + 0xc))[i]; }
int Sets(char* chapter) { return *(int*)(chapter + 0xc); }

// 1-based mission number of (chapter, set), and the campaign's mission count.
int MissionNumber(char* c, int ch, int set) {
    int n = 0;
    for (int k = 0; k < ch && k < Chapters(c); ++k) n += Sets(Chapter(c, k));
    return n + set + 1;
}
int MissionCount(char* c) { return MissionNumber(c, Chapters(c), -1); }

// --- the record -------------------------------------------------------------

struct Record {
    int chapter = -1, set = -1, mission = 0, missions = 0;
};

Record Read(const Info& c) {
    Record r;
    r.chapter = GetPrivateProfileIntA(c.section, "Chapter", -1, g_path.c_str());
    r.set = GetPrivateProfileIntA(c.section, "Set", -1, g_path.c_str());
    r.mission = GetPrivateProfileIntA(c.section, "Mission", 0, g_path.c_str());
    r.missions = GetPrivateProfileIntA(c.section, "Missions", 0, g_path.c_str());
    return r;
}

void WriteInt(const char* section, const char* key, int v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", v);
    WritePrivateProfileStringA(section, key, buf, g_path.c_str());
}

void __thiscall HookBriefStart(void* scene) {
    ((void(__thiscall*)(void*))kBriefStart)(scene);
    char* c = Campaign();
    if (!c) return;
    const Info* info = ById(*(unsigned short*)(c + 0x18));
    int ch = *(unsigned short*)(c + 4);
    if (!info || ch >= Chapters(c)) return;
    int set = *(unsigned short*)Chapter(c, ch);
    int mission = MissionNumber(c, ch, set);
    WriteInt(info->section, "Chapter", ch);
    WriteInt(info->section, "Set", set);
    WriteInt(info->section, "Mission", mission);
    WriteInt(info->section, "Missions", MissionCount(c));
    g_log("campaign: %s briefing: chapter %d, set %d (mission %d of %d) recorded", info->name, ch, set, mission,
          MissionCount(c));
}

void CheckComplete(int before) {
    char* c = Campaign();
    if (before || !*(int*)kCampaignComplete || !c) return;
    const Info* info = ById(*(unsigned short*)(c + 0x18));
    if (!info) return;
    WritePrivateProfileStringA(info->section, nullptr, nullptr, g_path.c_str());
    g_log("campaign: %s complete: progress cleared", info->name);
}

int __thiscall HookRewardCommand(void* scene, void* button) {
    int before = *(int*)kCampaignComplete;
    int r = ((int(__thiscall*)(void*, void*))kRewardCommand)(scene, button);
    CheckComplete(before);
    return r;
}

int __thiscall HookRewardKeyUp(void* scene, int a, int b, int c) {
    int before = *(int*)kCampaignComplete;
    int r = ((int(__thiscall*)(void*, int, int, int))kRewardKeyUp)(scene, a, b, c);
    CheckComplete(before);
    return r;
}

// --- the campaign screen ----------------------------------------------------

short& Cmd(void* b) { return *(short*)((char*)b + 0x20); }
short& Visible(void* b) { return *(short*)((char*)b + 0x74); }
short& Dirty(void* b) { return *(short*)((char*)b + 0x1a); }
char& Hotkey(void* b) { return *((char*)b + 0x14); }
short* Rect(void* b) { return (short*)((char*)b + 0xc); }

void* Find(void* scene, short cmd) {
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count < 0 || count > 256) return nullptr;
    for (int i = 0; i < count; ++i)
        if (list[i] && *(uintptr_t*)list[i] == kTextButtonVtbl && Cmd(list[i]) == cmd) return list[i];
    return nullptr;
}

struct Home {
    short rect[4];
    char hotkey;
};
std::map<void*, Home> g_home;

void Show(void* b, bool on) {
    if (!b) return;
    auto it = g_home.find(b);
    if (it == g_home.end()) {
        Home h;
        memcpy(h.rect, Rect(b), sizeof h.rect);
        h.hotkey = Hotkey(b);
        it = g_home.emplace(b, h).first;
    }
    Visible(b) = on ? 1 : 0;
    if (on) {
        memcpy(Rect(b), it->second.rect, sizeof it->second.rect);
        Hotkey(b) = it->second.hotkey;
    } else {
        short off[4] = {-1000, -1000, -1000, -1000};
        memcpy(Rect(b), off, sizeof off);
        Hotkey(b) = 0;
    }
    Dirty(b) = 1;
}

// The campaign items' rows: (0x12, 0x11 + 0x1c*row) to (0x12a, +0x13).
void* AddItem(void* scene, const char* text, short cmd, char hotkey, int row, bool title) {
    void* b = ((void*(__thiscall*)(void*, const char*, int, int))kAddTextButton)(scene, text, cmd, (unsigned char)hotkey);
    if (!b) return nullptr;
    ((void(__thiscall*)(void*, int, int))kSetStyle)(b, title ? kStyleMenuHover : kStyleMenu, kStyleMenuHover);
    short r[4] = {0x12, (short)(0x11 + 0x1c * row), 0x12a, (short)(0x24 + 0x1c * row)};
    memcpy(Rect(b), r, sizeof r);
    Show(b, false);
    return b;
}

void* g_pending;  // the campaign item clicked, while the prompt shows
std::string g_texts[2];

void ShowPrompt(void* scene, bool on) {
    for (short cmd : {kCmdTanu, kCmdMagha, kCmdTraining, kCmdMainMenu}) Show(Find(scene, cmd), !on);
    for (short cmd : {kCmdTitle, kCmdContinue, kCmdStartOver, kCmdBack}) Show(Find(scene, cmd), on);
}

short __thiscall HookSingleInit(void* scene) {
    short r = ((short(__thiscall*)(void*))kSingleInit)(scene);
    g_home.clear();
    g_pending = nullptr;
    AddItem(scene, "", kCmdTitle, 0, 0, true);
    AddItem(scene, "", kCmdContinue, 'C', 1, false);
    AddItem(scene, lang::tr("Start Over"), kCmdStartOver, 'S', 2, false);
    AddItem(scene, lang::tr("Back"), kCmdBack, 'B', 3, false);
    return r;
}

void PlaySelect() { ((int(__thiscall*)(void*, int))kPlaySound)((void*)kSoundPlayer, kSelectSound); }

// Move the freshly loaded campaign to the record; false if the record doesn't fit.
bool Resume(const Info& info) {
    char* c = Campaign();
    Record r = Read(info);
    if (!c || *(unsigned short*)(c + 0x18) != info.id || r.chapter < 0 || r.chapter >= Chapters(c) || r.set < 0 ||
        r.set >= Sets(Chapter(c, r.chapter)))
        return false;
    *(unsigned short*)(c + 4) = (unsigned short)r.chapter;
    *(unsigned short*)Chapter(c, r.chapter) = (unsigned short)r.set;
    g_log("campaign: %s resumed at chapter %d, set %d (mission %d)", info.name, r.chapter, r.set,
          MissionNumber(c, r.chapter, r.set));
    return true;
}

int __thiscall HookSingleCommand(void* scene, void* button) {
    auto original = [&](void* b) { return ((int(__thiscall*)(void*, void*))kSingleCommand)(scene, b); };
    short cmd = button ? Cmd(button) : 0;
    if (const Info* info = ByCommand(cmd)) {
        Record r = Read(*info);
        void* title = Find(scene, kCmdTitle);
        void* cont = Find(scene, kCmdContinue);
        if (r.mission > 1 && r.chapter >= 0 && r.set >= 0 && title && cont) {
            g_texts[0] = lang::tr(info->name);
            if (r.missions >= r.mission)
                g_texts[1] = lang::trf("Continue: mission {1} of {2}",
                                       {std::to_string(r.mission), std::to_string(r.missions)});
            else
                g_texts[1] = lang::trf("Continue: mission {1}", {std::to_string(r.mission)});
            ((void(__thiscall*)(void*, const char*))kSetText)(title, g_texts[0].c_str());
            ((void(__thiscall*)(void*, const char*))kSetText)(cont, g_texts[1].c_str());
            g_pending = button;
            PlaySelect();
            ShowPrompt(scene, true);
            g_log("campaign: %s has progress (mission %d): asking", info->name, r.mission);
            return 1;
        }
        return original(button);
    }
    if (cmd == kCmdTitle) return 1;
    if (cmd == kCmdBack) {
        PlaySelect();
        ShowPrompt(scene, false);
        g_pending = nullptr;
        return 1;
    }
    if ((cmd == kCmdContinue || cmd == kCmdStartOver) && g_pending) {
        void* item = g_pending;
        const Info* info = ByCommand(Cmd(item));
        g_pending = nullptr;
        ShowPrompt(scene, false);
        int r = original(item);  // sound, ACampaign::Load, the briefing queued
        if (info && cmd == kCmdContinue && !Resume(*info))
            g_log("campaign: %s: the recorded place doesn't fit the campaign; starting over", info->name);
        if (info && cmd == kCmdStartOver) g_log("campaign: %s: starting over", info->name);
        return r;
    }
    return original(button);
}

}  // namespace

void Install(const std::string& dir, void (*log)(const char*, ...)) {
    g_log = log;
    g_path = dir + "WarBreedsHD-progress.ini";
    bool ok = PatchSlot(kBriefSlotStart, kBriefStart, (void*)&HookBriefStart) &&
              PatchSlot(kRewardSlotCommand, kRewardCommand, (void*)&HookRewardCommand) &&
              PatchSlot(kRewardSlotKeyUp, kRewardKeyUp, (void*)&HookRewardKeyUp) &&
              PatchSlot(kSingleSlotInit, kSingleInit, (void*)&HookSingleInit) &&
              PatchSlot(kSingleSlotCommand, kSingleCommand, (void*)&HookSingleCommand);
    log("campaign: progress kept between sessions %s", ok ? "(installed)" : "FAILED (unexpected vtable)");
}

}  // namespace campaign
