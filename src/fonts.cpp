#include "fonts.h"

#include <windows.h>
#include <string.h>

#include <map>
#include <string>

#include "render/render.h"

namespace fonts {
namespace {

void (*g_log)(const char*, ...);
bool g_modern;
bool g_loaded;  // the TrueType file is available

// Stand-in for each of the game's faces, at the same cell height so line
// spacing and boxes stay as they were. Small sizes use Medium with 1 px of
// extra letter spacing (Orbitron runs its letters together below ~15 px);
// heavier weights lose the dots on i/j there.
struct Spec {
    const char* face;  // original
    const char* sub;   // GDI face of the named instance
    int weight, height, extra;
};
const Spec kSpecs[] = {
    {"WBLg24", "Orbitron SemiBold", 600, 26, 0},
    {"WBMd24", "Orbitron SemiBold", 600, 26, 0},
    {"WBMd18", "Orbitron SemiBold", 600, 19, 0},
    {"WBMd14", "Orbitron Medium", 500, 15, 1},
    {"WBSm12", "Orbitron Medium", 500, 13, 1},
    {"IntBld12", "Orbitron Medium", 500, 13, 1},
    {"IntReg12", "Orbitron", 400, 13, 1},
    {"IntBld10", "Orbitron Medium", 500, 11, 1},
    {"IntReg10", "Orbitron Medium", 500, 11, 1},
    {"Darwin18", "Orbitron SemiBold", 600, 27, 0},  // headings (unit names, menu titles)
};

// The game creates and deletes its fonts over and over (every frame on some
// screens), so each face gets one stand-in, made on first use and kept.
HFONT g_standIn[sizeof kSpecs / sizeof kSpecs[0]];
std::map<HFONT, int> g_specOf;        // game font -> kSpecs index
std::map<HFONT, HFONT> g_originalOf;  // stand-in -> a game font of that face

decltype(&CreateFontIndirectA) o_CreateFontIndirectA;
decltype(&SelectObject) o_SelectObject;
decltype(&DeleteObject) o_DeleteObject;
decltype(&GetCharABCWidthsA) o_GetCharABCWidthsA;

int SpecFor(const char* face) {
    for (int i = 0; i < (int)(sizeof kSpecs / sizeof kSpecs[0]); ++i)
        if (_stricmp(face, kSpecs[i].face) == 0) return i;
    return -1;
}

HFONT StandIn(int i) {
    if (!g_standIn[i]) {
        const Spec& s = kSpecs[i];
        LOGFONTA m{};
        m.lfHeight = s.height;  // cell height, like the bitmap face's
        m.lfWeight = s.weight;
        m.lfCharSet = DEFAULT_CHARSET;
        m.lfQuality = NONANTIALIASED_QUALITY;  // the game draws into an 8-bit frame
        strncpy(m.lfFaceName, s.sub, LF_FACESIZE - 1);
        g_standIn[i] = (o_CreateFontIndirectA ? o_CreateFontIndirectA : CreateFontIndirectA)(&m);
        g_log("fonts: %s -> %s %d px%s", s.face, s.sub, s.height, g_standIn[i] ? "" : " FAILED");
    }
    return g_standIn[i];
}

HFONT WINAPI h_CreateFontIndirectA(const LOGFONTA* lf) {
    HFONT f = o_CreateFontIndirectA(lf);
    if (!f || !lf || !g_loaded) return f;
    int i = SpecFor(lf->lfFaceName);
    if (i >= 0) {
        if (StandIn(i)) {
            g_specOf[f] = i;
            g_originalOf[g_standIn[i]] = f;
        }
    }
    return f;
}

// The game selects its own font; while the option is on, the stand-in goes
// into the DC instead. The game only ever sees its own handles.
HGDIOBJ WINAPI h_SelectObject(HDC dc, HGDIOBJ obj) {
    if (!obj || GetObjectType(obj) != OBJ_FONT) return o_SelectObject(dc, obj);
    HGDIOBJ want = obj;
    int extra = 0;
    auto o = g_originalOf.find((HFONT)obj);  // a stand-in handed back to us
    if (o != g_originalOf.end()) want = o->second;
    auto s = g_specOf.find((HFONT)want);
    if (g_modern && s != g_specOf.end()) {
        extra = kSpecs[s->second].extra;
        want = g_standIn[s->second];
    }
    HGDIOBJ prev = o_SelectObject(dc, want);
    SetTextCharacterExtra(dc, extra);  // the game never sets it itself
    auto po = g_originalOf.find((HFONT)prev);
    return po != g_originalOf.end() ? po->second : prev;
}

BOOL WINAPI h_DeleteObject(HGDIOBJ obj) {
    g_specOf.erase((HFONT)obj);
    return o_DeleteObject(obj);
}

// ABC widths only exist for TrueType faces, so with its bitmap fonts the game
// always took its fallback (text extents, which include the extra spacing).
// Keep it on that path.
BOOL WINAPI h_GetCharABCWidthsA(HDC dc, UINT first, UINT last, LPABC abc) {
    if (g_originalOf.count((HFONT)GetCurrentObject(dc, OBJ_FONT))) return FALSE;
    return o_GetCharABCWidthsA(dc, first, last, abc);
}

}  // namespace

void Install(const char* dir, void (*log)(const char*, ...)) {
    g_log = log;
    std::string path = std::string(dir) + "Fonts\\Orbitron[wght].ttf";
    g_loaded = AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr) > 0;
    if (!g_loaded) log("fonts: %s not found; Font=orbitron unavailable", path.c_str());
    // GDI32 import slots in WARBREED.EXE.
    bool ok = render::HookImport(0x5956ec, &h_CreateFontIndirectA, &o_CreateFontIndirectA) &&
              render::HookImport(0x5956f8, &h_SelectObject, &o_SelectObject) &&
              render::HookImport(0x59562c, &h_DeleteObject, &o_DeleteObject) &&
              render::HookImport(0x59572c, &h_GetCharABCWidthsA, &o_GetCharABCWidthsA);
    if (!ok) log("fonts: hooks FAILED");
}

int TextWidth(const char* face, const char* text) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0;
    LOGFONTA lf{};
    strncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    HFONT own = CreateFontIndirectA(&lf);  // the game's bitmap face (not through the hooks)
    HFONT use = own;
    int i = SpecFor(face);
    if (g_modern && i >= 0 && StandIn(i)) {
        use = g_standIn[i];
        SetTextCharacterExtra(dc, kSpecs[i].extra);
    }
    HGDIOBJ old = SelectObject(dc, use);
    SIZE sz{};
    GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
    SelectObject(dc, old);
    DeleteObject(own);
    DeleteDC(dc);
    return sz.cx;
}

void SetModern(bool on) {
    if (on && !g_loaded) on = false;
    if (on != g_modern && g_log) g_log("fonts: %s", on ? "Orbitron" : "original");
    g_modern = on;
}

}  // namespace fonts
