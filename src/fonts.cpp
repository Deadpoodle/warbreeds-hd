#include "fonts.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>

#include "lang.h"
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
// Our TrueType fonts (stand-ins and narrowed ones). Translated text is Mac Roman
// (the order the game's bitmap faces hold accented letters in), so with these
// selected, text with accents is measured and drawn through the W functions.
std::map<HFONT, bool> g_trueType;
bool TrueTypeIn(HDC dc) { return g_trueType.count((HFONT)GetCurrentObject(dc, OBJ_FONT)) != 0; }
std::map<HFONT, int> g_faceOf;  // every game font handle -> kSpecs index
LOGFONTA g_lf[sizeof kSpecs / sizeof kSpecs[0]];

// A translation marked ~ (a tight spot) is fitted into the room its English took:
// it may run up to kFitSlack wider than its English original; past that it is
// squeezed, within legibility (Orbitron at most kMaxNarrow narrower; the
// bitmap faces 1 px tighter, then the same family's next smaller face), and
// past that it overflows: tools/lang_check.py reports it so the wording can be
// shortened instead.
constexpr double kFitSlack = 1.10, kMaxNarrow = 0.80;

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
        if (g_standIn[i]) g_trueType[g_standIn[i]] = true;
        g_log("fonts: %s -> %s %d px%s", s.face, s.sub, s.height, g_standIn[i] ? "" : " FAILED");
    }
    return g_standIn[i];
}

// The game's bitmap faces keep their accented letters in Mac Roman order, and
// translated text is Mac Roman. Through an ANSI font, GDI drops the bytes
// Windows-1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D: Å ç è ê ù).
// A private copy of each face marked SYMBOL_CHARSET passes every byte to its
// glyph, in TextOutA, DrawTextA and the measuring calls alike, so the game's
// faces are always created from those copies (made from the player's own
// .FON files in memory; same glyphs, so English is unchanged).
bool g_symbolCopies;

// [Debug] TextTrace=1: every distinct string the game draws, with the code that
// drew it (finds text that doesn't come from the string table).
bool g_textTrace;
void Trace(const char* text, int n, void* from) {
    if (!g_textTrace || !text) return;
    static std::map<std::string, bool> seen;
    std::string t = n < 0 ? std::string(text) : std::string(text, n);
    if (seen.size() > 5000 || !seen.emplace(t, true).second) return;
    g_log("text: \"%s\" from %p", t.c_str(), from);
}

void LoadSymbolCopies(const char* dir) {
    int loaded = 0;
    for (const Spec& sp : kSpecs) {
        std::string name = sp.face;
        for (char& c : name) c = (char)toupper((unsigned char)c);
        std::string path = std::string(dir) + name + ".FON";
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        std::string data;
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        fclose(f);
        // NE resource table: type 0x8008 (RT_FONT) entries hold the FNT resources.
        if (data.size() < 0x40) continue;
        uint32_t ne = *(uint32_t*)&data[0x3c];
        if (ne + 0x40 > data.size() || data.compare(ne, 2, "NE") != 0) continue;
        size_t p = ne + *(uint16_t*)&data[ne + 0x24];
        int shift = *(uint16_t*)&data[p];
        p += 2;
        int patched = 0;
        while (p + 8 <= data.size()) {
            uint16_t type = *(uint16_t*)&data[p], count = *(uint16_t*)&data[p + 2];
            if (type == 0) break;
            p += 8;
            for (int i = 0; i < count && p + 12 <= data.size(); ++i, p += 12) {
                size_t fnt = (size_t)(*(uint16_t*)&data[p]) << shift;
                if (type == 0x8008 && fnt + 0x56 <= data.size()) {
                    data[fnt + 0x55] = 2;  // dfCharSet = SYMBOL_CHARSET
                    ++patched;
                }
            }
        }
        DWORD fonts = 0;
        if (patched && AddFontMemResourceEx(&data[0], (DWORD)data.size(), nullptr, &fonts)) ++loaded;
    }
    g_symbolCopies = loaded > 0;
    g_log("fonts: %d of the game's faces copied for translated text (every byte reaches its glyph)", loaded);
}

HFONT WINAPI h_CreateFontIndirectA(const LOGFONTA* lf) {
    LOGFONTA symbol;
    if (lf && g_symbolCopies && SpecFor(lf->lfFaceName) >= 0) {
        symbol = *lf;
        symbol.lfCharSet = SYMBOL_CHARSET;
        lf = &symbol;
    }
    HFONT f = o_CreateFontIndirectA(lf);
    if (!f || !lf) return f;
    int i = SpecFor(lf->lfFaceName);
    if (i >= 0) {
        if (!g_original[i]) {
            g_original[i] = o_CreateFontIndirectA(lf);
            g_lf[i] = *lf;
        }
        g_faceOf[f] = i;
        if (g_loaded && StandIn(i)) {
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
    if (!f || !text) return 0;
    if (n < 0) n = (int)strlen(text);
    if (!g_scratch) g_scratch = CreateCompatibleDC(nullptr);
    HGDIOBJ old = o_SelectObject(g_scratch, f);
    SetTextCharacterExtra(g_scratch, extra);
    SIZE sz{};
    if (lang::HasHigh(text, n) && g_trueType.count(f)) {
        std::wstring w = lang::Wide(text, n);
        GetTextExtentPoint32W(g_scratch, w.c_str(), (int)w.size(), &sz);
    } else {
        o_GetTextExtentPointA(g_scratch, text, n, &sz);
    }
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
    int original = Width(g_original[i], 0, text, n), have = Width(g_standIn[i], extra, text, n);
    int want = original;  // Orbitron always takes the original face's room, however much it must narrow
    if (const char* en = lang::EnglishOf(text, n))
        want = std::min(want, (int)(kFitSlack * Width(g_original[i], 0, en, -1)));
    HFONT use = g_standIn[i];
    if (have > want && want > 0) {
        TEXTMETRICA tm{};
        HGDIOBJ old = o_SelectObject(g_scratch, g_standIn[i]);
        GetTextMetricsA(g_scratch, &tm);
        o_SelectObject(g_scratch, old);
        // A translation squeezed into its English's room narrows at most kMaxNarrow past
        // the original face's width; the plain fit to the original has no floor.
        int least = want < original && have > 0
                        ? std::max(1, (int)(tm.tmAveCharWidth * (double)original / have * kMaxNarrow + 0.5))
                        : 1;
        for (int ave = std::max(least, (int)(tm.tmAveCharWidth * (double)want / have)); ave >= least; --ave) {
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
                if (f) g_trueType[f] = true;
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

// The bitmap faces: the font (and letter spacing, and vertical shift that keeps
// the baseline) to measure and draw a translated string with when it would run
// past its English original's room. False: draw it as it is.
struct Squeeze {
    HFONT font;
    int extra, dy;
};
std::map<std::string, Squeeze> g_squeezeOf;  // spec + text

int SmallerFace(int i) {
    switch (i) {
    case 0: case 1: return 2;  // WBLg24, WBMd24 -> WBMd18
    case 2: return 3;          // WBMd18 -> WBMd14
    case 5: return 7;          // IntBld12 -> IntBld10
    case 6: return 8;          // IntReg12 -> IntReg10
    default: return -1;
    }
}

HFONT OriginalFace(int i) {
    if (!g_original[i]) {
        LOGFONTA lf = g_lf[i];
        if (!lf.lfFaceName[0]) strncpy(lf.lfFaceName, kSpecs[i].face, LF_FACESIZE - 1);
        if (g_symbolCopies) lf.lfCharSet = SYMBOL_CHARSET;
        g_original[i] = o_CreateFontIndirectA(&lf);
    }
    return g_original[i];
}

int Ascent(HFONT f) {
    if (!g_scratch) g_scratch = CreateCompatibleDC(nullptr);
    HGDIOBJ old = o_SelectObject(g_scratch, f);
    TEXTMETRICA tm{};
    GetTextMetricsA(g_scratch, &tm);
    o_SelectObject(g_scratch, old);
    return tm.tmAscent;
}

bool SqueezeFor(HDC dc, const char* text, int n, Squeeze& out) {
    if (g_modern || !text) return false;
    auto face = g_faceOf.find((HFONT)GetCurrentObject(dc, OBJ_FONT));
    if (face == g_faceOf.end()) return false;
    const char* en = lang::EnglishOf(text, n);
    if (!en) return false;
    int i = face->second;
    if (n < 0) n = (int)strlen(text);
    std::string key = std::string(1, (char)('A' + i)) + std::string(text, n);
    auto hit = g_squeezeOf.find(key);
    if (hit != g_squeezeOf.end()) {
        out = hit->second;
        return out.font != nullptr;
    }
    HFONT cur = OriginalFace(i);
    int room = (int)(kFitSlack * Width(cur, 0, en, -1));
    Squeeze sq{nullptr, 0, 0};
    if (cur && Width(cur, 0, text, n) > room) {
        sq = {cur, -1, 0};
        for (int j = SmallerFace(i); Width(sq.font, sq.extra, text, n) > room && j >= 0; j = SmallerFace(j)) {
            HFONT small = OriginalFace(j);
            if (!small) break;
            sq = {small, 0, Ascent(cur) - Ascent(small)};
            if (Width(small, 0, text, n) > room) sq.extra = -1;
        }
        bool fits = Width(sq.font, sq.extra, text, n) <= room;
        g_log("fonts: \"%.*s\" (%s) squeezed to %d px for its English's room (%d px)%s", n, text, kSpecs[i].face,
              Width(sq.font, sq.extra, text, n), room, fits ? "" : ": STILL TOO WIDE");
    }
    g_squeezeOf[key] = sq;
    out = sq;
    return sq.font != nullptr;
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
    if (const char* t = lang::DrawnTranslation(text, n)) text = t, n = (int)strlen(t);
    Squeeze sq;
    if (SqueezeFor(dc, text, n, sq)) {
        HGDIOBJ cur = o_SelectObject(dc, sq.font);
        int was = SetTextCharacterExtra(dc, sq.extra);
        BOOL r = o_GetTextExtentPointA(dc, text, n, size);
        SetTextCharacterExtra(dc, was);
        o_SelectObject(dc, cur);
        return r;
    }
    return WithFit(dc, text, n, [&] {
        if (lang::HasHigh(text, n) && TrueTypeIn(dc)) {
            std::wstring w = lang::Wide(text, n);
            return GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), size);
        }
        return o_GetTextExtentPointA(dc, text, n, size);
    });
}

BOOL WINAPI h_TextOutA(HDC dc, int x, int y, LPCSTR text, int n) {
    Trace(text, n, __builtin_return_address(0));
    if (const char* t = lang::DrawnTranslation(text, n)) text = t, n = (int)strlen(t);
    Squeeze sq;
    if (SqueezeFor(dc, text, n, sq)) {
        HGDIOBJ cur = o_SelectObject(dc, sq.font);
        int was = SetTextCharacterExtra(dc, sq.extra);
        BOOL r = o_TextOutA(dc, x, y + sq.dy, text, n);
        SetTextCharacterExtra(dc, was);
        o_SelectObject(dc, cur);
        return r;
    }
    return WithFit(dc, text, n, [&] {
        if (lang::HasHigh(text, n) && TrueTypeIn(dc)) {
            std::wstring w = lang::Wide(text, n);
            return TextOutW(dc, x, y, w.c_str(), (int)w.size());
        }
        return o_TextOutA(dc, x, y, text, n);
    });
}

int WINAPI h_DrawTextA(HDC dc, LPCSTR text, int n, LPRECT r, UINT format) {
    Trace(text, n, __builtin_return_address(0));
    if (const char* t = lang::DrawnTranslation(text, n)) text = t, n = (int)strlen(t);
    return WithFit(dc, text, n, [&] {
        if (lang::HasHigh(text, n) && TrueTypeIn(dc)) {
            std::wstring w = lang::Wide(text, n);
            return DrawTextW(dc, w.c_str(), (int)w.size(), r, format);
        }
        return o_DrawTextA(dc, text, n, r, format);
    });
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
    LoadSymbolCopies(dir);
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
    if (g_symbolCopies && SpecFor(face) >= 0) lf.lfCharSet = SYMBOL_CHARSET;  // as the game's (above)
    HFONT own = CreateFontIndirectA(&lf);  // the game's bitmap face (not through the hooks)
    HFONT use = own;
    int i = SpecFor(face);
    if (g_modern && i >= 0 && StandIn(i)) {
        use = g_standIn[i];
        SetTextCharacterExtra(dc, kSpecs[i].extra);
    }
    HGDIOBJ old = SelectObject(dc, use);
    SIZE sz{};
    if (use != own && lang::HasHigh(text, -1)) {
        std::wstring w = lang::Wide(text, -1);
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
    } else {
        GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
    }
    SelectObject(dc, old);
    DeleteObject(own);
    DeleteDC(dc);
    return sz.cx;
}

void SetTextTrace(bool on) { g_textTrace = on; }

void SetModern(bool on) {
    if (on && !g_loaded) on = false;
    if (on != g_modern && g_log) g_log("fonts: %s", on ? "Orbitron" : "original");
    g_modern = on;
}

}  // namespace fonts
