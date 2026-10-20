#include "fonts.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "lang.h"
#include "render/render.h"

namespace fonts {
namespace {

void (*g_log)(const char*, ...);
std::string g_dir;
bool g_loaded;  // the Orbitron file is available

// What the game's faces are drawn in: its own bitmap faces; Orbitron (an English
// option); or a language's release fonts (Lang\WarBreedsHD_<code>.fonts: the faces
// that language's 1998 release used, or free stand-ins for them).
enum Mode { kBitmap, kOrbitron, kRelease };
Mode g_mode = kBitmap;
std::string g_release;  // the language whose release fonts are loaded
bool g_releaseReal;     // the player has the release's own fonts installed: drawn as in 1998

// Orbitron for each of the game's faces, at the same cell height so line
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

constexpr int kFaces = sizeof kSpecs / sizeof kSpecs[0];

// The current mode's stand-in for each face (kSpecs order). height is GDI's
// lfHeight: positive a cell height, negative a character height. room: in release
// mode, the advance widths (Windows-1252 0x20..0xFF) of the release's own face; a
// string the stand-in draws wider than that is narrowed to it, so text fits
// wherever the release's did.
struct Stand {
    std::string face;
    int weight = 400, height = 0, extra = 0;
    std::vector<int> room;
};
Stand g_stand[kFaces];

// The game creates and deletes its fonts over and over (every frame on some
// screens), so each face gets one stand-in, made on first use and kept until
// the mode changes.
HFONT g_standIn[kFaces];
std::map<HFONT, HFONT> g_originalOf;  // stand-in -> the game font it last stood in for
// Orbitron is wider than the game's faces, and the game lays text out in boxes
// sized for them ("Select Components" ran out of its title bar). A string that
// would come out wider than the original face draws it is measured and drawn
// with a horizontally narrowed stand-in instead, so it takes the original's room.
// In release mode the room is the release face's (Stand::room) instead.
HFONT g_original[kFaces];                       // our own copy of each original face
std::map<std::pair<int, int>, HFONT> g_narrow;  // (spec, average char width) -> font
std::map<std::string, HFONT> g_fitOf;           // spec + text -> font to use (stand-in if it fits)
HDC g_scratch;
// Our TrueType fonts (stand-ins and narrowed ones). Translated text is Mac Roman
// (the order the game's bitmap faces hold accented letters in), so with these
// selected, text with accents is measured and drawn through the W functions.
std::map<HFONT, bool> g_trueType;
bool TrueTypeIn(HDC dc) { return g_trueType.count((HFONT)GetCurrentObject(dc, OBJ_FONT)) != 0; }
std::map<HFONT, int> g_faceOf;  // every live game font handle -> kSpecs index
LOGFONTA g_lf[kFaces];

// A translation marked ~ (a tight spot) is fitted into the room its English took:
// it may run up to kFitSlack wider than its English original; past that it is
// squeezed, within legibility (Orbitron at most kMaxNarrow narrower; the
// bitmap faces 1 px tighter, then the same family's next smaller face), and
// past that it overflows: tools/lang_check.py reports it so the wording can be
// shortened instead. Release fonts narrow to the release's room, at most
// kMaxNarrowRelease (their room is exact, so any string the release fitted fits).
constexpr double kFitSlack = 1.10, kMaxNarrow = 0.80, kMaxNarrowRelease = 0.60;

decltype(&CreateFontIndirectA) o_CreateFontIndirectA;
decltype(&SelectObject) o_SelectObject;
decltype(&DeleteObject) o_DeleteObject;
decltype(&GetCharABCWidthsA) o_GetCharABCWidthsA;
decltype(&GetTextExtentPointA) o_GetTextExtentPointA;
decltype(&TextOutA) o_TextOutA;
decltype(&DrawTextA) o_DrawTextA;

int SpecFor(const char* face) {
    for (int i = 0; i < kFaces; ++i)
        if (_stricmp(face, kSpecs[i].face) == 0) return i;
    return -1;
}

HFONT MakeFont(const Stand& s, int ave) {
    LOGFONTA m{};
    m.lfHeight = s.height;
    m.lfWidth = ave;
    m.lfWeight = s.weight;
    m.lfCharSet = DEFAULT_CHARSET;
    m.lfQuality = NONANTIALIASED_QUALITY;  // the game draws into an 8-bit frame
    strncpy(m.lfFaceName, s.face.c_str(), LF_FACESIZE - 1);
    HFONT f = (o_CreateFontIndirectA ? o_CreateFontIndirectA : CreateFontIndirectA)(&m);
    if (f) g_trueType[f] = true;
    return f;
}

HFONT StandIn(int i) {
    if (g_mode == kBitmap || g_stand[i].face.empty()) return nullptr;
    if (!g_standIn[i]) {
        g_standIn[i] = MakeFont(g_stand[i], 0);
        g_log("fonts: %s -> %s %d px%s", kSpecs[i].face, g_stand[i].face.c_str(), g_stand[i].height,
              g_standIn[i] ? "" : " FAILED");
    }
    return g_standIn[i];
}

// Retires every stand-in and fitted font (the mode or its fonts changed); the next
// SelectObject swaps new ones in. They aren't deleted: one may still be selected in
// a DC, and the game may get it back from SelectObject (mapped to its own font, as
// before). A few fonts per language change.
void DropStandIns() {
    for (HFONT& f : g_standIn) f = nullptr;
    g_narrow.clear();
    g_fitOf.clear();
}

// A text box the release resized for its fonts: its .rdata rect (left, top, right,
// bottom) as the release had it and as the English exe has it. Scenes copy these
// rects when they open, so the release's boxes are written in while its own fonts
// are drawn (their lines are shorter than the bitmap faces' and the boxes were made
// for them), and the English ones back otherwise.
struct Layout {
    uintptr_t at;
    short release[4], english[4];
};
std::vector<Layout> g_layouts;  // in use: the current language's, written in

bool WriteRect(uintptr_t at, const short (&from)[4], const short (&to)[4]) {
    if (memcmp((void*)at, from, sizeof from) != 0) return false;  // not the exe we know
    DWORD old;
    if (!VirtualProtect((void*)at, sizeof to, PAGE_READWRITE, &old)) return false;
    memcpy((void*)at, to, sizeof to);
    VirtualProtect((void*)at, sizeof to, old, &old);
    return true;
}

// Lang\WarBreedsHD_<code>.fonts (tools/font_metrics.py): one line per game face,
//   WBMd14 = BellGothic BT, 14, 400 | Roboto Condensed, 400, 16 | 4 5 4 ...
// the release's face, size and weight | the stand-in ("-": the game's own face),
// weight, cell height | the room; then the boxes the release resized,
//   Layout 0x5063f0 = 19 128 132 144 | 19 122 132 144
// the release's rect | the English exe's.
bool LoadRelease(const std::string& code, Stand (&out)[kFaces], bool& real, std::vector<Layout>& layouts) {
    FILE* f = fopen((g_dir + "Lang\\WarBreedsHD_" + code + ".fonts").c_str(), "rb");
    if (!f) return false;
    Stand own[kFaces];  // the release's own faces, as the release asked for them
    int got = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        Layout l{};
        unsigned at;
        if (sscanf(line, " Layout %x = %hd %hd %hd %hd | %hd %hd %hd %hd", &at, &l.release[0], &l.release[1],
                   &l.release[2], &l.release[3], &l.english[0], &l.english[1], &l.english[2], &l.english[3]) == 9) {
            l.at = at;
            layouts.push_back(l);
            continue;
        }
        char game[32], face[64], sub[64];
        int size, weight, sweight, cell, used = 0;
        if (sscanf(line, " %31[^ =] = %63[^,], %d, %d | %63[^,], %d, %d |%n", game, face, &size, &weight, sub, &sweight,
                   &cell, &used) < 7 || !used)
            continue;
        int i = SpecFor(game);
        if (i < 0) continue;
        own[i].face = face;
        own[i].weight = weight;
        own[i].height = -size;
        out[i].face = strcmp(sub, "-") == 0 ? "" : sub;  // "-": the game's own face
        out[i].weight = sweight;
        out[i].height = cell;
        out[i].room.clear();
        for (char *p = line + used, *e; out[i].room.size() < 224; p = e) {
            long w = strtol(p, &e, 10);
            if (e == p) break;
            out[i].room.push_back((int)w);
        }
        if (out[i].room.size() != 224) out[i].room.clear();
        ++got;
    }
    fclose(f);
    // The release's own fonts, if the player has them installed (the file names the
    // face Windows drew in 1998, fallbacks included, so each must be found as named).
    real = got == kFaces;
    HDC dc = CreateCompatibleDC(nullptr);
    for (int i = 0; i < kFaces && real; ++i) {
        HFONT probe = MakeFont(own[i], 0);
        HGDIOBJ old = SelectObject(dc, probe);
        char name[LF_FACESIZE] = "";
        GetTextFaceA(dc, sizeof name, name);
        SelectObject(dc, old);
        g_trueType.erase(probe);
        DeleteObject(probe);
        real = _stricmp(name, own[i].face.c_str()) == 0;
    }
    DeleteDC(dc);
    if (real)
        for (int i = 0; i < kFaces; ++i) out[i] = own[i];
    return got > 0;
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
int g_lastStyle = -1;  // the text style last resolved, for the trace

// Text styles (ResolveTextStyle 0x4698c0: style -> font slot, colours). The French
// and German releases made style 2 (the bold interface face, IntBld10's slot) the
// regular face, and moved the places meant to stay bold to a new style 47 with style
// 2's old look: a text at 0x401810, the AI's debug lines (0x453120), the stats rows
// (0x4547b0), three in SAllies::Init, and the selected row of the file and map lists
// (SOpenDialog::Init, set as a variable). The same places here use style 47 (resolved
// as style 2 always was), and with the release's own fonts style 2 is the regular
// face, as the 1998 game drew it: the build and pod lists' labels in Geometric 231
// (a player's 1998 screenshots), while the map list (style 8) stays bold.
constexpr uintptr_t kResolveStyle = 0x4698c0;
const uintptr_t kResolveStyleCalls[] = {0x469732, 0x4697db, 0x469899};
const uintptr_t kBoldStylePushes[] = {0x401822, 0x453138, 0x4547d1, 0x474cd4,
                                      0x474d07, 0x474d3a, 0x4907de};  // push 2
constexpr int kStyleReleaseBold = 47;

// A thiscall that pops its five arguments (ret 0x14): taken as fastcall, edx unused.
void __fastcall ResolveStyle(void* self, void*, int style, int* slot, int* colour, int* outline, short* flag) {
    g_lastStyle = style;
    if (g_textTrace && style == 2) {  // debug: who draws in style 2 (return addresses on the stack)
        static std::map<std::string, bool> seenChains;
        uintptr_t* sp = (uintptr_t*)&flag;
        std::string chain;
        for (int i = 0, n = 0; i < 96 && n < 8; ++i)
            if (sp[i] > 0x401000 && sp[i] < 0x502000) {
                char b[12];
                snprintf(b, sizeof b, " %06x", (unsigned)sp[i]);
                chain += b;
                ++n;
            }
        if (seenChains.size() < 200 && seenChains.emplace(chain, true).second) g_log("style 2 from%s", chain.c_str());
    }
    bool plain = style >= kNoOutline;
    if (plain) style -= kNoOutline;
    ((void(__thiscall*)(void*, int, int*, int*, int*, short*))kResolveStyle)(
        self, style == kStyleReleaseBold ? 2 : style, slot, colour, outline, flag);
    if (style == 2 && g_mode == kRelease && g_releaseReal) *slot = 0;  // IntReg10's slot
    if (plain) *flag = 0;
}

bool InstallStyles() {
    for (uintptr_t site : kResolveStyleCalls) {
        uint8_t* p = (uint8_t*)site;
        if (p[0] != 0xe8 || site + 5 + *(int32_t*)(p + 1) != kResolveStyle) return false;
    }
    for (uintptr_t site : kBoldStylePushes)
        if (((uint8_t*)site)[0] != 0x6a || ((uint8_t*)site)[1] != 2) return false;
    for (uintptr_t site : kResolveStyleCalls) {
        uint8_t* p = (uint8_t*)site;
        DWORD old;
        if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
        *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&ResolveStyle - (site + 5));
        VirtualProtect(p + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
    }
    for (uintptr_t site : kBoldStylePushes) {
        uint8_t* p = (uint8_t*)site;
        DWORD old;
        if (!VirtualProtect(p + 1, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
        p[1] = kStyleReleaseBold;
        VirtualProtect(p + 1, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 2);
    }
    return true;
}

void Trace(HDC dc, const char* text, int n, void* from, const RECT* box = nullptr, UINT format = 0) {
    if (!g_textTrace || !text) return;
    static std::map<std::string, bool> seen;
    std::string t = n < 0 ? std::string(text) : std::string(text, n);
    if (seen.size() > 5000 || !seen.emplace(t + " | style " + std::to_string(g_lastStyle), true).second) return;
    char face[64] = "";
    GetTextFaceA(dc, sizeof face, face);
    TEXTMETRICA tm{};
    GetTextMetricsA(dc, &tm);
    LOGFONTA lf{};
    GetObjectA(GetCurrentObject(dc, OBJ_FONT), sizeof lf, &lf);
    SIZE sz{};
    o_GetTextExtentPointA(dc, text, n < 0 ? (int)strlen(text) : n, &sz);
    g_log("text: \"%s\" from %p style %d: %s h%ld ave%ld lfWidth %ld extra %d width %ld rect %ld-%ld format 0x%x", t.c_str(),
          from, g_lastStyle, face, tm.tmHeight, tm.tmAveCharWidth, lf.lfWidth, GetTextCharacterExtra(dc), sz.cx,
          box ? box->left : 0, box ? box->right : 0, format);
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
    }
    return f;
}

// The game selects its own font; in Orbitron or release mode the stand-in goes
// into the DC instead. The game only ever sees its own handles.
HGDIOBJ WINAPI h_SelectObject(HDC dc, HGDIOBJ obj) {
    if (!obj || GetObjectType(obj) != OBJ_FONT) return o_SelectObject(dc, obj);
    HGDIOBJ want = obj;
    int extra = 0;
    auto o = g_originalOf.find((HFONT)obj);  // a stand-in handed back to us
    if (o != g_originalOf.end()) want = o->second;
    auto s = g_faceOf.find((HFONT)want);
    if (s != g_faceOf.end()) {
        if (HFONT stand = StandIn(s->second)) {
            extra = g_stand[s->second].extra;
            g_originalOf[stand] = (HFONT)want;
            want = stand;
        }
    }
    HGDIOBJ prev = o_SelectObject(dc, want);
    SetTextCharacterExtra(dc, extra);  // the game never sets it itself
    auto po = g_originalOf.find((HFONT)prev);
    return po != g_originalOf.end() ? po->second : prev;
}

int SpecOfStandIn(HFONT f) {
    for (int i = 0; i < kFaces; ++i)
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

// The width this text took in the release's own face (the sum of its advance
// widths, as GDI lays a line out); -1 if face i has no room.
int RoomWidth(int i, const char* text, int n) {
    const std::vector<int>& room = g_stand[i].room;
    if (room.size() != 224) return -1;
    if (n < 0) n = (int)strlen(text);
    std::wstring w = lang::Wide(text, n);
    std::string ansi(w.size() * 2 + 1, '\0');
    int len = WideCharToMultiByte(1252, 0, w.c_str(), (int)w.size(), &ansi[0], (int)ansi.size(), nullptr, nullptr);
    int sum = 0;
    for (int k = 0; k < len; ++k) {
        uint8_t b = (uint8_t)ansi[k];
        if (b >= 0x20) sum += room[b - 0x20];
    }
    return sum;
}

bool HasBracket(const char* text, int n) {
    for (int k = 0; k < n; ++k)
        if (strchr("()[]", text[k]) && text[k]) return true;
    return false;
}

// Face i's stand-in at this average character width.
HFONT Narrow(int i, int ave) {
    HFONT& f = g_narrow[{i, ave}];
    if (!f) f = MakeFont(g_stand[i], ave);
    return f;
}

int AveWidth(HFONT f) {
    TEXTMETRICA tm{};
    HGDIOBJ old = o_SelectObject(g_scratch, f);
    GetTextMetricsA(g_scratch, &tm);
    o_SelectObject(g_scratch, old);
    return tm.tmAveCharWidth;
}

// The font to measure and draw this string with in a DC holding a stand-in:
// the stand-in itself, or a narrowed one if the stand-in runs wider than the
// original face (Orbitron) or the release's own face (release mode). nullptr:
// not a stand-in, nothing to do.
HFONT FitFont(HDC dc, const char* text, int n) {
    if (g_mode == kBitmap || !text) return nullptr;
    int i = SpecOfStandIn((HFONT)GetCurrentObject(dc, OBJ_FONT));
    if (i < 0) return nullptr;
    if (n < 0) n = (int)strlen(text);
    std::string key = std::string(1, (char)('A' + i)) + std::string(text, n);
    auto hit = g_fitOf.find(key);
    if (hit != g_fitOf.end()) return hit->second;
    if (g_fitOf.size() > 4000) g_fitOf.clear();  // the game's strings are few; just a safety cap
    int extra = g_stand[i].extra;
    int have = Width(g_standIn[i], extra, text, n), want, least;
    HFONT use = g_standIn[i];
    if (!g_scratch) g_scratch = CreateCompatibleDC(nullptr);
    int natural = AveWidth(use);
    if (g_mode == kRelease) {
        want = RoomWidth(i, text, n);  // -1: the release's own fonts, drawn as they are
        least = std::max(1, (int)(natural * kMaxNarrowRelease + 0.5));
    } else {
        if (!g_original[i]) return nullptr;
        int original = Width(g_original[i], 0, text, n);
        want = original;  // Orbitron always takes the original face's room, however much it must narrow
        if (const char* en = lang::EnglishOf(text, n))
            want = std::min(want, (int)(kFitSlack * Width(g_original[i], 0, en, -1)));
        // A translation squeezed into its English's room narrows at most kMaxNarrow past
        // the original face's width; the plain fit to the original has no floor.
        least = want < original && have > 0 ? std::max(1, (int)(natural * (double)original / have * kMaxNarrow + 0.5))
                                            : 1;
    }
    if (have > want && want > 0) {
        bool fits = false;
        for (int ave = std::max(least, std::min(natural - 1, (int)(natural * (double)want / have))); ave >= least;
             --ave) {
            HFONT f = Narrow(i, ave);
            if (!f) break;
            use = f;
            if ((fits = Width(f, extra, text, n) <= want)) break;
        }
        g_log("fonts: \"%.*s\" (%s) narrowed to fit %d px (%s %d px)%s", n, text, kSpecs[i].face, want,
              g_stand[i].face.c_str(), have, fits ? "" : ": STILL TOO WIDE");
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
    if (!text) return false;
    auto face = g_faceOf.find((HFONT)GetCurrentObject(dc, OBJ_FONT));
    if (face == g_faceOf.end() || StandIn(face->second)) return false;  // drawn in a stand-in
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

// Orbitron's brackets are tall bars with hooks under half a pixel long at the
// small sizes, and the rasteriser keeps or drops the hooks by the width:
// "(AI)" drew as "|AI|", or as "(AI]" with the two ends unlike. Up to this
// height the stand-in's brackets are clipped out and drawn here as a matched
// pixel pair instead: ( ) with rounded ends, [ ] square.
constexpr int kOwnBracketsUpTo = 15;

bool OwnBrackets(HDC dc, const char* text, int n) {
    if (!TrueTypeIn(dc) || !HasBracket(text, n)) return false;
    TEXTMETRICA tm{};
    GetTextMetricsA(dc, &tm);
    return tm.tmHeight <= kOwnBracketsUpTo;
}

BOOL TextOutOwnBrackets(HDC dc, int x, int y, const char* text, int n) {
    std::wstring w = lang::Wide(text, n);
    int len = (int)w.size();
    UINT align = GetTextAlign(dc);
    POINT ref{x, y};
    if (align & TA_UPDATECP) GetCurrentPositionEx(dc, &ref);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SIZE all{};
    GetTextExtentPoint32W(dc, w.c_str(), len, &all);
    int left = ref.x;
    if ((align & TA_CENTER) == TA_CENTER) left -= all.cx / 2;
    else if (align & TA_RIGHT) left -= all.cx;
    int base = ref.y;
    if ((align & TA_BASELINE) == TA_BASELINE) {
    } else if (align & TA_BOTTOM) {
        base -= tm.tmDescent;
    } else {
        base += tm.tmAscent;
    }

    struct Mark {
        wchar_t ch;
        int l, r, top, h;  // our pair's columns l..r; the font's glyph box
        RECT box;          // all it covers, ours and the font's
    };
    std::vector<Mark> marks;
    const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    for (int k = 0; k < len; ++k) {
        wchar_t c = w[k];
        if (c != L'(' && c != L')' && c != L'[' && c != L']') continue;
        SIZE before{};
        GetTextExtentPoint32W(dc, w.c_str(), k, &before);
        GLYPHMETRICS gm{};
        if (GetGlyphOutlineW(dc, c, GGO_METRICS, &gm, 0, nullptr, &identity) == GDI_ERROR) continue;
        int bx = left + before.cx + gm.gmptGlyphOrigin.x, bw = std::max(1, (int)gm.gmBlackBoxX);
        Mark m{c, 0, 0, base - gm.gmptGlyphOrigin.y, (int)gm.gmBlackBoxY, {}};
        if (c == L'(' || c == L'[') m.l = bx, m.r = bx + std::max(bw, 2) - 1;
        else m.r = bx + bw - 1, m.l = m.r - std::max(bw, 2) + 1;
        m.box = {std::min(m.l, bx), m.top, std::max(m.r, bx + bw - 1) + 1, m.top + m.h};
        marks.push_back(m);
    }

    HRGN saved = CreateRectRgn(0, 0, 0, 0);
    int had = GetClipRgn(dc, saved);
    for (const Mark& m : marks) ExcludeClipRect(dc, m.box.left, m.box.top, m.box.right, m.box.bottom);
    BOOL ok = TextOutW(dc, x, y, w.c_str(), len);
    SelectClipRgn(dc, had == 1 ? saved : nullptr);
    DeleteObject(saved);

    HBRUSH ink = CreateSolidBrush(GetTextColor(dc));
    HBRUSH paper = GetBkMode(dc) == OPAQUE ? CreateSolidBrush(GetBkColor(dc)) : nullptr;
    auto fill = [&](int l, int t, int r, int b) {
        RECT rc{l, t, r, b};
        if (r > l && b > t) FillRect(dc, &rc, ink);
    };
    for (const Mark& m : marks) {
        if (paper) FillRect(dc, &m.box, paper);
        bool round = m.ch == L'(' || m.ch == L')', opens = m.ch == L'(' || m.ch == L'[';
        int bar = opens ? m.l : m.r, bottom = m.top + m.h;
        fill(bar, m.top + round, bar + 1, bottom - round);  // the upright
        int hl = opens ? m.l + 1 : m.l, hr = opens ? m.r + 1 : m.r;
        fill(hl, m.top, hr, m.top + 1);  // the ends
        fill(hl, bottom - 1, hr, bottom);
    }
    DeleteObject(ink);
    if (paper) DeleteObject(paper);
    return ok;
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
    Trace(dc, text, n, __builtin_return_address(0));
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
        if (OwnBrackets(dc, text, n)) return TextOutOwnBrackets(dc, x, y, text, n);
        if (lang::HasHigh(text, n) && TrueTypeIn(dc)) {
            std::wstring w = lang::Wide(text, n);
            return TextOutW(dc, x, y, w.c_str(), (int)w.size());
        }
        return o_TextOutA(dc, x, y, text, n);
    });
}

int WINAPI h_DrawTextA(HDC dc, LPCSTR text, int n, LPRECT r, UINT format) {
    Trace(dc, text, n, __builtin_return_address(0), r, format);
    if (const char* t = lang::DrawnTranslation(text, n)) text = t, n = (int)strlen(t);
    return WithFit(dc, text, n, [&] {
        // Word-wrapping doesn't count the stand-in's extra letter spacing, so wrapped
        // lines (mission briefings) came out wider than their box and were cut off:
        // wrap and draw them without it.
        bool wrap = (format & DT_WORDBREAK) && TrueTypeIn(dc);
        int extra = wrap ? SetTextCharacterExtra(dc, 0) : 0;
        int result;
        if (lang::HasHigh(text, n) && TrueTypeIn(dc)) {
            std::wstring w = lang::Wide(text, n);
            result = DrawTextW(dc, w.c_str(), (int)w.size(), r, format);
        } else {
            result = o_DrawTextA(dc, text, n, r, format);
        }
        if (wrap) SetTextCharacterExtra(dc, extra);
        return result;
    });
}

BOOL WINAPI h_DeleteObject(HGDIOBJ obj) {
    g_faceOf.erase((HFONT)obj);  // GDI may hand the handle out again for another font
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
    g_dir = dir;
    LoadSymbolCopies(dir);
    // Every font in Fonts\ (Orbitron, and the release fonts' stand-ins), private to the game.
    std::string fontDir = std::string(dir) + "Fonts\\";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((fontDir + "*.ttf").c_str(), &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            bool ok = AddFontResourceExA((fontDir + fd.cFileName).c_str(), FR_PRIVATE, nullptr) > 0;
            if (_stricmp(fd.cFileName, "Orbitron[wght].ttf") == 0) g_loaded = ok;
            n += ok;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    log("fonts: %d fonts loaded from %s", n, fontDir.c_str());
    if (!g_loaded) log("fonts: Orbitron[wght].ttf not found; Font=orbitron unavailable");
    // GDI32 import slots in WARBREED.EXE.
    bool ok = render::HookImport(0x5956ec, &h_CreateFontIndirectA, &o_CreateFontIndirectA) &&
              render::HookImport(0x5956f8, &h_SelectObject, &o_SelectObject) &&
              render::HookImport(0x59562c, &h_DeleteObject, &o_DeleteObject) &&
              render::HookImport(0x59572c, &h_GetCharABCWidthsA, &o_GetCharABCWidthsA) &&
              render::HookImport(0x5956ac, &h_GetTextExtentPointA, &o_GetTextExtentPointA) &&
              render::HookImport(0x595670, &h_TextOutA, &o_TextOutA) &&
              render::HookImport(0x5959b8, &h_DrawTextA, &o_DrawTextA);
    if (!ok) log("fonts: hooks FAILED");
    log("fonts: text styles %s", InstallStyles() ? "hooked (the releases' style 47)" : "FAILED (unexpected code)");
}

int TextWidth(const char* face, const char* text) {
    int i = SpecFor(face);
    if (i >= 0 && StandIn(i)) {
        int w = Width(g_standIn[i], g_stand[i].extra, text, -1);
        int room = g_mode == kRelease ? RoomWidth(i, text, -1) : -1;
        return room > 0 && w > room ? room : w;  // drawn narrowed to the release's room
    }
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0;
    LOGFONTA lf{};
    strncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    if (g_symbolCopies && i >= 0) lf.lfCharSet = SYMBOL_CHARSET;  // as the game's (above)
    HFONT own = CreateFontIndirectA(&lf);  // the game's bitmap face (not through the hooks)
    HGDIOBJ old = SelectObject(dc, own);
    SIZE sz{};
    GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
    SelectObject(dc, old);
    DeleteObject(own);
    DeleteDC(dc);
    return sz.cx;
}

void SetTextTrace(bool on) { g_textTrace = on; }

void SetFor(const std::string& language, bool orbitron) {
    Mode mode = kBitmap;
    Stand stand[kFaces];
    bool real = false;
    std::vector<Layout> layouts;
    if (_stricmp(language.c_str(), "en") != 0 && LoadRelease(language, stand, real, layouts)) {
        mode = kRelease;
    } else if (orbitron && g_loaded && _stricmp(language.c_str(), "en") == 0) {
        mode = kOrbitron;
        for (int i = 0; i < kFaces; ++i) stand[i] = {kSpecs[i].sub, kSpecs[i].weight, kSpecs[i].height, kSpecs[i].extra, {}};
    }
    if (mode == g_mode && (mode != kRelease || (language == g_release && real == g_releaseReal))) return;
    DropStandIns();
    for (int i = 0; i < kFaces; ++i) g_stand[i] = stand[i];
    g_mode = mode;
    g_release = mode == kRelease ? language : "";
    g_releaseReal = real;
    // The release's boxes with its own fonts only; the English ones otherwise.
    for (const Layout& l : g_layouts) WriteRect(l.at, l.release, l.english);
    g_layouts.clear();
    if (real) {
        int done = 0;
        for (const Layout& l : layouts)
            if (WriteRect(l.at, l.english, l.release)) g_layouts.push_back(l), ++done;
        if (g_log && !layouts.empty()) g_log("fonts: %d of %d of the release's text boxes", done, (int)layouts.size());
    }
    if (g_log)
        g_log("fonts: %s", mode == kOrbitron ? "Orbitron"
                           : mode == kBitmap ? "the game's own"
                           : real            ? "the release's own (installed)"
                                             : "the release's, by free stand-ins");
}

}  // namespace fonts
