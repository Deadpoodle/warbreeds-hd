#include "fonts.h"

#include <windows.h>
#include <string.h>

#include <algorithm>
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
// Orbitron is wider than the game's faces, and the game lays text out in boxes
// sized for them ("Select Components" ran out of its title bar). A string that
// would come out wider than the original face draws it is measured and drawn
// with a horizontally narrowed stand-in instead, so it takes the original's room.
HFONT g_original[sizeof kSpecs / sizeof kSpecs[0]];  // our own copy of each original face
std::map<std::pair<int, int>, HFONT> g_narrow;       // (spec, average char width) -> font
std::map<std::string, HFONT> g_fitOf;                // spec + text -> font to use (stand-in if it fits)
HDC g_scratch;

decltype(&CreateFontIndirectA) o_CreateFontIndirectA;
decltype(&SelectObject) o_SelectObject;
decltype(&DeleteObject) o_DeleteObject;
decltype(&GetCharABCWidthsA) o_GetCharABCWidthsA;
decltype(&GetTextExtentPointA) o_GetTextExtentPointA;
decltype(&TextOutA) o_TextOutA;
decltype(&DrawTextA) o_DrawTextA;

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
        if (!g_original[i]) g_original[i] = o_CreateFontIndirectA(lf);
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

int SpecOfStandIn(HFONT f) {
    for (int i = 0; i < (int)(sizeof kSpecs / sizeof kSpecs[0]); ++i)
        if (g_standIn[i] && g_standIn[i] == f) return i;
    return -1;
}

int Width(HFONT f, int extra, const char* text, int n) {
    if (!g_scratch) g_scratch = CreateCompatibleDC(nullptr);
    HGDIOBJ old = o_SelectObject(g_scratch, f);
    SetTextCharacterExtra(g_scratch, extra);
    SIZE sz{};
    o_GetTextExtentPointA(g_scratch, text, n, &sz);
    o_SelectObject(g_scratch, old);
    return sz.cx;
}

// The font to measure and draw this string with in a DC holding a stand-in:
// the stand-in itself, or a narrowed one if the stand-in runs wider than the
// original face. nullptr: not a stand-in, nothing to do.
HFONT FitFont(HDC dc, const char* text, int n) {
    if (!g_modern || !text) return nullptr;
    int i = SpecOfStandIn((HFONT)GetCurrentObject(dc, OBJ_FONT));
    if (i < 0 || !g_original[i]) return nullptr;
    if (n < 0) n = (int)strlen(text);
    std::string key = std::string(1, (char)('A' + i)) + std::string(text, n);
    auto hit = g_fitOf.find(key);
    if (hit != g_fitOf.end()) return hit->second;
    if (g_fitOf.size() > 4000) g_fitOf.clear();  // the game's strings are few; just a safety cap
    int extra = kSpecs[i].extra;
    int want = Width(g_original[i], 0, text, n), have = Width(g_standIn[i], extra, text, n);
    HFONT use = g_standIn[i];
    if (have > want && want > 0) {
        TEXTMETRICA tm{};
        HGDIOBJ old = o_SelectObject(g_scratch, g_standIn[i]);
        GetTextMetricsA(g_scratch, &tm);
        o_SelectObject(g_scratch, old);
        for (int ave = std::max(1, (int)(tm.tmAveCharWidth * (double)want / have)); ave >= 1; --ave) {
            HFONT& f = g_narrow[{i, ave}];
            if (!f) {
                LOGFONTA m{};
                m.lfHeight = kSpecs[i].height;
                m.lfWidth = ave;
                m.lfWeight = kSpecs[i].weight;
                m.lfCharSet = DEFAULT_CHARSET;
                m.lfQuality = NONANTIALIASED_QUALITY;
                strncpy(m.lfFaceName, kSpecs[i].sub, LF_FACESIZE - 1);
                f = o_CreateFontIndirectA(&m);
            }
            if (!f) break;
            use = f;
            if (Width(f, extra, text, n) <= want) break;
        }
        g_log("fonts: \"%.*s\" (%s) narrowed to fit %d px (Orbitron %d px)", n, text, kSpecs[i].face, want, have);
    }
    g_fitOf[key] = use;
    return use;
}

// Measure or draw with the fitted font selected, then put the stand-in back.
template <class F>
auto WithFit(HDC dc, const char* text, int n, F call) {
    HFONT fit = FitFont(dc, text, n);
    HGDIOBJ cur = GetCurrentObject(dc, OBJ_FONT);
    if (!fit || fit == (HFONT)cur) return call();
    o_SelectObject(dc, fit);
    auto r = call();
    o_SelectObject(dc, cur);
    return r;
}

BOOL WINAPI h_GetTextExtentPointA(HDC dc, LPCSTR text, int n, LPSIZE size) {
    return WithFit(dc, text, n, [&] { return o_GetTextExtentPointA(dc, text, n, size); });
}

BOOL WINAPI h_TextOutA(HDC dc, int x, int y, LPCSTR text, int n) {
    return WithFit(dc, text, n, [&] { return o_TextOutA(dc, x, y, text, n); });
}

int WINAPI h_DrawTextA(HDC dc, LPCSTR text, int n, LPRECT r, UINT format) {
    return WithFit(dc, text, n, [&] { return o_DrawTextA(dc, text, n, r, format); });
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
              render::HookImport(0x59572c, &h_GetCharABCWidthsA, &o_GetCharABCWidthsA) &&
              render::HookImport(0x5956ac, &h_GetTextExtentPointA, &o_GetTextExtentPointA) &&
              render::HookImport(0x595670, &h_TextOutA, &o_TextOutA) &&
              render::HookImport(0x5959b8, &h_DrawTextA, &o_DrawTextA);
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
