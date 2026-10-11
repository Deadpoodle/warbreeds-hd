// Map editor: the smart terrain painter. See terrain_paint.h and
// re/terrain-painter.md. Routine comments give the MapEditor.exe address of
// the original each one rebuilds.
#include "terrain_paint.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor {
namespace {

const char* const kCannotPaint = "You cannot paint that there";
const char* const kInvalidCombination = "Invalid combination";
const char* const kNoTile = "Couldn't find tile!";
const char* const kNoFertile = "Couldn't find fertile ground tile";
const char* const kNotImplemented = "Tile not implemented yet";

const uint16_t kOutsideTile = 0x13e;  // what the original reads outside the 96 x 96 array (a lava tile)
const int kMaxDepth = 4000;           // recursion guard (the original has none)

inline int CornerIndex(const Corners& c) { return ((c.c[0] * 15 + c.c[1]) * 15 + c.c[2]) * 15 + c.c[3]; }
inline Corners C4(int a, int b, int c, int d) { return Corners{{(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d}}; }

uint16_t Be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

std::vector<uint16_t> Words(const std::vector<uint8_t>& b) {
    std::vector<uint16_t> w(b.size() / 2);
    for (size_t i = 0; i < w.size(); ++i) w[i] = Be16(&b[2 * i]);
    return w;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
#ifdef _WIN32
    // Read-only and fully shared: the running game may hold the archive open.
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot open " + path + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = n > 0 && n < (64 << 20);
    if (ok) {
        out.resize((size_t)n);
        ok = fread(out.data(), 1, out.size(), f) == out.size();
    }
    fclose(f);
#endif
    if (!ok && error) *error = "cannot read " + path;
    return ok;
}

// The Smart Palette and terrain palette names (MapEditor.exe strings 342-357),
// in TRPL 1000 order.
const char* const kPaletteNames[] = {
    "Smart Palette", "Roots",        "Sand Cropland", "Hot Mud",  "Crushed Crystal", "Crystal Cropland",
    "Crystal Canyons", "Hardened Tar", "Tar Cropland", "Lava",    "Cropland",        "Sand/Crystal",
    "Tar/Crystal",   "Sand/Tar",     "Sand/Crystal/Tar", "Crystal Cliffs",
};

// World thing types named by TRTH items (numbering as tools/wbm.py).
const char* ThingName(int type) {
    switch (type) {
    case 0: return "Tanu Shaman";
    case 1: return "Scout";
    case 2: return "Raptor";
    case 3: return "T-Rex";
    case 4: return "Tanu Dragon";
    case 9: return "Sen-Soth Shaman";
    case 10: return "Flea";
    case 11: return "Myrmidon";
    case 12: return "Beetle";
    case 13: return "Dragonfly";
    case 18: return "Kelika Shaman";
    case 19: return "Tribble";
    case 20: return "Sasquatch";
    case 21: return "Behemoth";
    case 22: return "Leviathan";
    case 27: return "Magha Shaman";
    case 28: return "Shadow";
    case 29: return "Dervish";
    case 30: return "Heavy";
    case 31: return "Wombat";
    case 35: return "Yedda";
    case 44: return "Refinery";
    case 45: return "Gene Lab";
    case 46: return "Relay";
    case 47: return "Psi Tower";
    case 48: return "Mine";
    case 49: return "Turret";
    case 50: return "Electric Tower";
    case 51: return "Listener";
    case 52: return "Revitalizer";
    case 53: return "Advanced Gene Lab";
    case 54: return "Advanced Relay";
    case 55: return "Advanced Turret";
    case 56: return "Advanced Psi Tower";
    case 57: return "Warp";
    case 58: return "Summoner";
    case 59: return "Collector";
    case 63: return "Pods";
    case 64: return "Spores";
    case 65: return "Weeds";
    case 67: return "Yedda Ruins";
    case 68: return "Crystal Tree";
    case 69: return "Crystal Rock";
    case 70: return "Sand Tree";
    case 71: return "Sand Rock";
    case 72: return "Tar Tree";
    case 73: return "Tar Rock";
    default: return "Thing";
    }
}

}  // namespace

bool ReadArchive(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    return ReadWholeFile(path, out, error);
}

// ---------------------------------------------------------------------------
// Names and the compatibility table

const char* TerrainPainter::TerrainName(int t) {
    static const char* const n[] = {"Crystal", "Crushed Crystal", "Crystal Canyon", "Tar",  "Hardened Tar",
                                    "Lava",    "Sand",            "Roots",          "Hot Mud", "Cropland",
                                    "Cropland Interior", "Canyon Wall", "Crystal Cliff", "Cliff Detail"};
    return t >= 0 && t < 14 ? n[t] : "?";
}

const char* TerrainPainter::ClassName(int cls) {
    static const char* const n[] = {
        "Crystal", "Crushed Crystal", "Crystal Canyon", "Tar", "Hardened Tar", "Lava", "Sand", "Roots",
        "Hot Mud", "Cropland", "Cropland Interior", "Canyon Wall", "Crystal Cliff", "Cliff Detail",
        "Sand/Crystal/Tar", "Crystal/Crushed Crystal", "Crystal/Canyon", "Tar/Hardened Tar", "Tar/Lava",
        "Sand/Roots", "Sand/Hot Mud", "Crushed Crystal/Cropland", "Hardened Tar/Cropland", "Roots/Cropland"};
    return cls >= 0 && cls < 24 ? n[cls] : "?";
}

// MapEditor 0x440aa0: may `paint` (a terrain or a class) go on a cell of class `existing`?
bool TerrainPainter::Allowed(int paint, int existing) {
    static const uint32_t B = 1;
    static const uint32_t ok[24] = {
        /* 0 */ B << 0 | B << 1 | B << 2 | B << 3 | B << 6 | B << 11 | B << 12 | B << 14 | B << 15 | B << 16,
        /* 1 */ B << 0 | B << 1 | B << 9 | B << 15 | B << 21,
        /* 2 */ B << 0 | B << 2 | B << 11 | B << 16,
        /* 3 */ B << 0 | B << 3 | B << 4 | B << 5 | B << 6 | B << 14 | B << 17 | B << 18,
        /* 4 */ B << 3 | B << 4 | B << 9 | B << 17 | B << 22,
        /* 5 */ B << 3 | B << 5 | B << 18,
        /* 6 */ B << 0 | B << 3 | B << 6 | B << 7 | B << 8 | B << 14 | B << 19 | B << 20,
        /* 7 */ B << 6 | B << 7 | B << 9 | B << 19 | B << 23,
        /* 8 */ B << 6 | B << 8 | B << 20,
        /* 9 */ B << 1 | B << 4 | B << 7 | B << 9 | B << 10 | B << 21 | B << 22 | B << 23,
        /* 10 */ B << 9 | B << 10,
        /* 11 */ B << 0 | B << 2 | B << 11,
        /* 12 */ B << 12,
        /* 13 */ 0,
        /* 14 */ B << 0 | B << 3 | B << 6 | B << 14,
        /* 15 */ B << 0 | B << 1 | B << 15,
        /* 16 */ B << 0 | B << 2 | B << 16,
        /* 17 */ B << 3 | B << 4 | B << 17,
        /* 18 */ B << 3 | B << 5 | B << 18,
        /* 19 */ B << 6 | B << 7 | B << 19,
        /* 20 */ B << 6 | B << 8 | B << 20,
        /* 21 */ B << 1 | B << 9 | B << 21,
        /* 22 */ B << 4 | B << 9 | B << 22,
        /* 23 */ B << 7 | B << 9 | B << 23,
    };
    if (existing < 0 || existing >= 24 || paint < 0 || paint >= 24) return false;
    return (ok[existing] >> paint) & 1;
}

// ---------------------------------------------------------------------------
// Loading (TERRAIN.MHK, a Mohawk archive)

bool TerrainPainter::Load(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data, error)) return false;
    return LoadFromMemory(data.data(), data.size(), error);
}

bool TerrainPainter::LoadFirst(const std::vector<std::string>& paths, std::string* error) {
    std::string first;
    for (const std::string& p : paths) {
        std::string e;
        if (Load(p, &e)) return true;
        if (first.empty()) first = e;
    }
    if (error) *error = first.empty() ? "no path given" : first;
    return false;
}

bool TerrainPainter::LoadFromMemory(const uint8_t* data, size_t size, std::string* error) {
    loaded_ = false;
    if (!Parse(data, size, error)) return false;
    loaded_ = true;
    return true;
}

bool TerrainPainter::Parse(const uint8_t* d, size_t n, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = std::string("TERRAIN.MHK: ") + why;
        return false;
    };
    // Header: "MHWK", u32 size, "RSRC", u16 version, u16 compaction, u32 size,
    // u32 directory offset, u16 file table offset (from the directory), u16 size.
    if (n < 28 || memcmp(d, "MHWK", 4) != 0 || memcmp(d + 8, "RSRC", 4) != 0) return fail("not a Mohawk archive");
    uint32_t dir = Be32(d + 20);
    uint32_t fto = Be16(d + 24);
    if (dir + 4 > n || dir + fto + 4 > n) return fail("bad directory");
    uint16_t typeCount = Be16(d + dir + 2);
    uint32_t ft = dir + fto;
    uint32_t fileCount = Be32(d + ft);
    if (ft + 4 + (uint64_t)fileCount * 10 > n) return fail("bad file table");
    std::map<std::string, std::map<uint16_t, std::vector<uint8_t>>> res;
    for (uint16_t t = 0; t < typeCount; ++t) {
        uint32_t te = dir + 4 + 8u * t;
        if (te + 8 > n) return fail("bad type table");
        std::string tag((const char*)d + te, 4);
        if (tag != "TRID" && tag != "TRST" && tag != "TRVA" && tag != "TRPL" && tag != "TRTH") continue;
        uint32_t rt = dir + Be16(d + te + 4);
        if (rt + 2 > n) return fail("bad resource table");
        uint16_t rc = Be16(d + rt);
        if (rt + 2 + 4u * rc > n) return fail("bad resource table");
        for (uint16_t k = 0; k < rc; ++k) {
            uint16_t id = Be16(d + rt + 2 + 4 * k), idx = Be16(d + rt + 4 + 4 * k);
            if (idx < 1 || idx > fileCount) return fail("bad file index");
            const uint8_t* fe = d + ft + 4 + 10 * (idx - 1);
            uint32_t off = Be32(fe), len = Be16(fe + 4) | (uint32_t)fe[6] << 16;
            if ((uint64_t)off + len > n) return fail("resource out of range");
            res[tag][id].assign(d + off, d + off + len);
        }
    }

    // Kinds: TRID/TRST/TRVA 1000, 1001, ... (222 in the game).
    kinds_.clear();
    for (uint16_t id = 1000;; ++id) {
        auto a = res["TRID"].find(id), b = res["TRST"].find(id), c = res["TRVA"].find(id);
        if (a == res["TRID"].end() || b == res["TRST"].end() || c == res["TRVA"].end()) break;
        if (a->second.size() < 2 || b->second.size() < 8) return fail("short TRID/TRST");
        Kind k;
        k.cls = Be16(a->second.data());
        for (int i = 0; i < 4; ++i) {
            uint16_t v = Be16(b->second.data() + 2 * i);
            if (v >= 15) return fail("TRST corner out of range");
            k.corners.c[i] = (uint8_t)v;
        }
        // TRVA: thumbnail, count n, then n x (cumulative percent, tile id) and a
        // final 100. Variant i spans percents up to the next pair's value
        // (MapEditor 0x452b60 reads exactly n pairs; two kinds list a stray
        // extra pair, which the original ignores).
        std::vector<uint16_t> v = Words(c->second);
        if (v.size() < 2) return fail("short TRVA");
        k.thumb = v[0];
        int count = v[1];
        if (v.size() < (size_t)(3 + 2 * count)) return fail("short TRVA");
        for (int i = 0; i < count; ++i) {
            k.tiles.push_back(v[3 + 2 * i]);
            k.upper.push_back(v[4 + 2 * i]);
        }
        kinds_.push_back(k);
        if (id == 0xffff) break;
    }
    if (kinds_.size() < 2) return fail("no terrain kinds");

    // The 15^4 corners table (0x43c600): every kind, the later one winning.
    table_.assign(15 * 15 * 15 * 15, -1);
    for (size_t k = 0; k < kinds_.size(); ++k) table_[CornerIndex(kinds_[k].corners)] = (int16_t)k;
    // Tile id -> kind (0x43c600/0x43c7b0): every kind but the last (1221, the
    // plateau, which reuses crystal tiles), the later one winning.
    int maxTile = 0;
    for (const Kind& k : kinds_)
        for (uint16_t t : k.tiles) maxTile = std::max<int>(maxTile, t);
    tileKind_.assign(maxTile + 1, -1);
    plateauArt_.assign(maxTile + 1, 0);
    for (size_t k = 0; k + 1 < kinds_.size(); ++k)
        for (uint16_t t : kinds_[k].tiles) tileKind_[t] = (int16_t)k;
    for (uint16_t t : kinds_.back().tiles) plateauArt_[t] = 1;

    std::vector<std::pair<uint16_t, std::vector<uint16_t>>> trpl, trth;
    for (auto& kv : res["TRPL"]) trpl.push_back({kv.first, Words(kv.second)});
    for (auto& kv : res["TRTH"]) trth.push_back({kv.first, Words(kv.second)});
    BuildPalettes(trpl, trth);
    return true;
}

void TerrainPainter::BuildPalettes(const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& trpl,
                                   const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& trth) {
    auto find = [&](uint16_t id) -> const std::vector<uint16_t>* {
        for (auto& p : trpl)
            if (p.first == id) return &p.second;
        return nullptr;
    };
    auto thing = [&](uint16_t id) -> int {
        for (auto& p : trth)
            if (p.first == id && p.second.size() >= 2) return p.second[1];
        return -1;
    };
    // TRPL: u16 count, u16 0, then count x (default flag, id, category).
    auto items = [&](const std::vector<uint16_t>& v) {
        std::vector<PaletteItem> out;
        int count = v.empty() ? 0 : v[0];
        for (int i = 0; i < count && 4 + 3 * i < (int)v.size(); ++i) {
            PaletteItem it;
            it.isDefault = v[2 + 3 * i] != 0;
            it.id = v[3 + 3 * i];
            it.category = (uint8_t)v[4 + 3 * i];
            it.kind = -1;
            it.thingType = -1;
            it.preview = 0;
            if (it.category == PaletteItem::kTile) {
                int k = it.id - 1000;
                if (k >= 0 && k < KindCount()) {
                    it.kind = k;
                    const Kind& kk = kinds_[k];
                    it.preview = kk.tiles.empty() ? 0 : kk.tiles[0];
                    const Corners& c = kk.corners;
                    bool uniform = c.c[0] == c.c[1] && c.c[1] == c.c[2] && c.c[2] == c.c[3];
                    it.name = uniform ? TerrainName(c.c[0]) : std::string(ClassName(kk.cls)) + " border";
                }
            } else if (it.category >= PaletteItem::kObstacle) {
                it.thingType = thing(it.id);
                it.name = ThingName(it.thingType);
            }
            out.push_back(it);
        }
        return out;
    };
    // A drop-down entry (category 1) names a TRPL whose single link (category
    // 0) is the palette itself.
    auto resolve = [&](uint16_t menuId) -> uint16_t {
        const std::vector<uint16_t>* m = find(menuId);
        if (!m) return 0;
        for (const PaletteItem& it : items(*m))
            if (it.category == PaletteItem::kLink) return it.id;
        return 0;
    };
    palettes_.clear();
    smart_.clear();
    const char* const objNames[] = {"Creatures", "Devices"};
    for (uint16_t menu : {1000, 2000}) {
        const std::vector<uint16_t>* m = find(menu);
        if (!m) continue;
        int i = 0;
        for (const PaletteItem& entry : items(*m)) {
            uint16_t pid = resolve(entry.id);
            const std::vector<uint16_t>* p = pid ? find(pid) : nullptr;
            if (!p) continue;
            Palette pal;
            pal.id = pid;
            if (menu == 1000)
                pal.name = i < 16 ? kPaletteNames[i] : "Terrain Palette";
            else
                pal.name = i < 2 ? objNames[i] : "Objects";
            pal.items = items(*p);
            palettes_.push_back(pal);
            ++i;
        }
    }
    // The Smart Palette's terrains: its uniform tile kinds.
    if (!palettes_.empty())
        for (const PaletteItem& it : palettes_[0].items)
            if (it.kind >= 0) {
                const Corners& c = kinds_[it.kind].corners;
                if (c.c[0] == c.c[1] && c.c[1] == c.c[2] && c.c[2] == c.c[3]) smart_.push_back((Terrain)c.c[0]);
            }
}

// ---------------------------------------------------------------------------
// Table queries

int TerrainPainter::KindClass(int kind) const { return kind >= 0 && kind < KindCount() ? kinds_[kind].cls : kNoClass; }

Corners TerrainPainter::KindCorners(int kind) const {
    return kind >= 0 && kind < KindCount() ? kinds_[kind].corners : C4(255, 255, 255, 255);
}

const std::vector<uint16_t>& TerrainPainter::KindTiles(int kind) const {
    static const std::vector<uint16_t> none;
    return kind >= 0 && kind < KindCount() ? kinds_[kind].tiles : none;
}

int TerrainPainter::KindForCorners(const Corners& c) const {
    for (int i = 0; i < 4; ++i)
        if (c.c[i] >= 15) return kNoKind;
    return table_.empty() ? kNoKind : table_[CornerIndex(c)];
}

int TerrainPainter::UniformKind(Terrain t) const { return KindForCorners(C4(t, t, t, t)); }

int TerrainPainter::KindOfTile(uint16_t tile) const { return tile < tileKind_.size() ? tileKind_[tile] : kNoKind; }

uint16_t TerrainPainter::ClassOfTile(uint16_t tile) const { return KindClass(KindOfTile(tile)); }

bool TerrainPainter::CornersOfTile(uint16_t tile, Corners& out) const {
    int k = KindOfTile(tile);
    if (k < 0) return false;
    out = kinds_[k].corners;
    return true;
}

bool TerrainPainter::IsVariantOf(uint16_t tile, int kind) const {
    if (kind < 0 || kind >= KindCount()) return false;
    for (uint16_t t : kinds_[kind].tiles)
        if (t == tile) return true;
    return false;
}

bool TerrainPainter::IsPlateauArt(uint16_t tile) const { return tile < plateauArt_.size() && plateauArt_[tile]; }

int TerrainPainter::Rand() {
    rng_ = rng_ * 214013u + 2531011u;  // MSVC rand()
    return (rng_ >> 16) & 0x7fff;
}

// ATerrainTile::PickVariant (game 0x455a30, editor 0x452fa0).
uint16_t TerrainPainter::PickTile(int kind) {
    if (kind < 0 || kind >= KindCount() || kinds_[kind].tiles.empty()) return 0;
    const Kind& k = kinds_[kind];
    int r = Rand() % 101, i = 0;
    while (i < (int)k.tiles.size() - 1 && r > k.upper[i]) ++i;
    return k.tiles[i];
}

uint16_t TerrainPainter::MostLikelyTile(int kind) const {
    if (kind < 0 || kind >= KindCount() || kinds_[kind].tiles.empty()) return 0;
    const Kind& k = kinds_[kind];
    std::vector<int> hits(k.tiles.size(), 0);
    for (int r = 0; r <= 100; ++r) {
        int i = 0;
        while (i < (int)k.tiles.size() - 1 && r > k.upper[i]) ++i;
        ++hits[i];
    }
    return k.tiles[std::max_element(hits.begin(), hits.end()) - hits.begin()];
}

// ---------------------------------------------------------------------------
// Grid access during an operation

uint16_t TerrainPainter::TileAt(int x, int y) const {
    return x >= 0 && y >= 0 && x < W() && y < H() ? g_->Get(x, y) : kOutsideTile;
}

Corners TerrainPainter::Cor(int x, int y) const {
    Corners c;
    if (!CornersOfTile(TileAt(x, y), c)) c = C4(255, 255, 255, 255);
    return c;
}

void TerrainPainter::Write(int x, int y, uint16_t tile) {
    if (!g_->Inside(x, y)) return;
    uint16_t old = g_->Get(x, y);
    if (old == tile) return;
    journal_.push_back({(int16_t)x, (int16_t)y, old});
    g_->Set(x, y, tile);
}

// ATerrainTile::PaintAt (game 0x4559e0). A cell that already shows a variant
// of the kind keeps it (the original rolls a new variant every time).
void TerrainPainter::Stamp(int kind, int x, int y) {
    if (kind < 0 || !g_->Inside(x, y)) return;
    if (IsVariantOf(g_->Get(x, y), kind)) return;
    Write(x, y, PickTile(kind));
}

// ---------------------------------------------------------------------------
// The original's routines

// 0x440720 (rows y-1..y+1) and 0x440980 (mode 1: rows y-2..y+1, mode 0: y-1..y+2):
// every map cell of columns x-1..x+1, rows y0..y1 must accept `t`.
bool TerrainPainter::Area(int t, int x, int y, int y0, int y1) const {
    (void)y;
    for (int cy = y0; cy <= y1; ++cy)
        for (int cx = x - 1; cx <= x + 1; ++cx)
            if (g_->Inside(cx, cy) && !Allowed(t, Cls(cx, cy))) return false;
    return true;
}

// 0x440840: the 3x3 check, but cropland interior (class 10) cells don't count.
bool TerrainPainter::Area10(int t, int x, int y) const {
    for (int cy = y - 1; cy <= y + 1; ++cy)
        for (int cx = x - 1; cx <= x + 1; ++cx) {
            if (!g_->Inside(cx, cy)) continue;
            uint16_t c = Cls(cx, cy);
            if (!Allowed(t, c) && c != 10) return false;
        }
    return true;
}

namespace {
// The four diagonal neighbours: offset, the corner they share with the centre
// cell, the far corner, the two side corners and, for each side, the cell
// beyond it that the propagation checks (offset from the centre) and the
// bound for it.
struct Diag {
    int dx, dy, shared, opp, s1, s2;
    int b1x, b1y, b2x, b2y;
};
const Diag kDiag[4] = {
    {-1, -1, 3, 0, 1, 2, 0, -2, -2, 0},  // above-left
    {+1, -1, 2, 1, 0, 3, 0, -2, +2, 0},  // above-right
    {-1, +1, 1, 2, 3, 0, 0, +2, -2, 0},  // below-left
    {+1, +1, 0, 3, 2, 1, 0, +2, +2, 0},  // below-right
};
const Diag* DiagOf(int dx, int dy) {
    for (const Diag& d : kDiag)
        if (d.dx == dx && d.dy == dy) return &d;
    return nullptr;
}
// Border classes reduced to the terrain they spread (0x43f850).
int ReduceNew(int t) {
    switch (t) {
    case 15: case 16: return 0;
    case 17: case 18: return 3;
    case 19: case 20: return 6;
    case 21: return 1;
    case 22: return 4;
    case 23: return 7;
    default: return t;
    }
}
}  // namespace

// 0x440510: after the propagation, a diagonal neighbour whose far corner is
// `t` while both side corners aren't (a saddle) must itself accept `t` all round.
bool TerrainPainter::Revalidate(int t, int x, int y) const {
    for (const Diag& d : kDiag) {
        int nx = x + d.dx, ny = y + d.dy;
        if (!g_->Inside(nx, ny)) continue;
        Corners c = Cor(nx, ny);
        if (c.c[d.opp] == t && c.c[d.s1] != t && c.c[d.s2] != t && !Area(t, nx, ny, ny - 1, ny + 1)) return false;
    }
    return true;
}

// 0x43f730: propagate to the four diagonal neighbours.
void TerrainPainter::Propagate(int t, int x, int y) {
    for (int ny = y - 1; ny <= y + 1; ++ny)
        for (int nx = x - 1; nx <= x + 1; ++nx) {
            if (aborted_) return;
            if (nx != x && ny != y && g_->Inside(nx, ny)) PropagateTo(t, nx, ny, x, y);
        }
}

// 0x43f850: the diagonal neighbour (nx, ny) of the painted cell (px, py) is
// about to get `t` at the corner they share. If that would leave a saddle
// (the same terrain on two diagonal corners, something else on the others),
// for which no tile exists, its two side corners become `t` too, where the
// cell beyond each one accepts `t`; that tile is placed and the change spreads
// from there. Neighbours that then can't follow are painted `t` whole.
void TerrainPainter::PropagateTo(int t, int nx, int ny, int px, int py) {
    if (aborted_) return;
    if (++depth_ > kMaxDepth) {
        aborted_ = true;
        return;
    }
    struct Leave {
        int& d;
        ~Leave() { --d; }
    } leave{depth_};
    const Diag* d = DiagOf(nx - px, ny - py);
    if (!d) return;
    int T = ReduceNew(t);
    Corners c = Cor(nx, ny);
    if (c.c[0] == 255) return;
    if (c.c[d->shared] == T) return;
    c.c[d->shared] = (uint8_t)T;
    int fv = c.c[d->opp], s1 = c.c[d->s1], s2 = c.c[d->s2];
    if (!(fv != s1 && ((fv != s2 && fv == T) || s1 == s2))) return;
    bool changed = false;
    int b1x = px + d->b1x, b1y = py + d->b1y, b2x = px + d->b2x, b2y = py + d->b2y;
    if (g_->Inside(b1x, b1y) && Allowed(T, Cls(b1x, b1y))) {
        c.c[d->s1] = (uint8_t)T;
        changed = true;
    }
    if (g_->Inside(b2x, b2y) && Allowed(T, Cls(b2x, b2y))) {
        c.c[d->s2] = (uint8_t)T;
        changed = true;
    }
    if (!changed) return;
    int k = KindForCorners(c);
    if (k < 0) return;
    retry_.clear();
    if (T == kCliff) {
        if (CliffAt(k, nx, ny, false) != 1) {
            aborted_ = true;  // the original throws here
            return;
        }
    } else {
        Place(k, nx, ny);
    }
    if (!retry_.empty()) {
        // The original visits the list while the call below empties it, so
        // only its first match is painted; every match is painted here.
        std::vector<int16_t> list = retry_;
        int u = UniformKind((Terrain)T);
        for (size_t i = 0; i + 1 < list.size() && !aborted_; i += 2) {
            int rx = list[i], ry = list[i + 1];
            if (rx != nx && ry != ny) continue;
            if (T == kCliff)
                CliffAt(u, rx, ry, true);
            else
                Generic(u, rx, ry);
        }
    }
    Propagate(T, nx, ny);
}

// 0x4403c0: give cell (x, y) the corners `c` (kKeep keeps one). If they
// differ from its tile's, the kind with those corners is stamped; with no
// such kind the cell goes on the retry list.
void TerrainPainter::SetCorners(Corners c, int x, int y) {
    if (!g_->Inside(x, y)) return;
    Corners cur = Cor(x, y);
    if (cur.c[0] == 255) return;  // unknown tile: leave it
    for (int i = 0; i < 4; ++i)
        if (c.c[i] == kKeep) c.c[i] = cur.c[i];
    if (c == cur) return;
    int k = KindForCorners(c);
    if (k >= 0) {
        Stamp(k, x, y);
        return;
    }
    msg_ = kNoTile;
    retry_.push_back((int16_t)x);
    retry_.push_back((int16_t)y);
}

// 0x440030: stamp `kind` and give the 8 neighbours its corners where they touch.
void TerrainPainter::Place(int kind, int x, int y) {
    Stamp(kind, x, y);
    Corners k = KindCorners(kind);
    const int K = kKeep;
    bool l = x > 0, r = x < W() - 1, u = y > 0, dn = y < H() - 1;
    if (l) SetCorners(C4(K, k.c[0], K, k.c[2]), x - 1, y);
    if (l && u) SetCorners(C4(K, K, K, k.c[0]), x - 1, y - 1);
    if (u) SetCorners(C4(K, K, k.c[0], k.c[1]), x, y - 1);
    if (l && dn) SetCorners(C4(K, k.c[2], K, K), x - 1, y + 1);
    if (dn) SetCorners(C4(k.c[2], k.c[3], K, K), x, y + 1);
    if (r && u) SetCorners(C4(K, K, k.c[1], K), x + 1, y - 1);
    if (r) SetCorners(C4(k.c[1], K, k.c[3], K), x + 1, y);
    if (r && dn) SetCorners(C4(k.c[3], K, K, K), x + 1, y + 1);
}

// 0x43cc70: the generic painter.
bool TerrainPainter::Generic(int kind, int x, int y) {
    if (aborted_) return false;
    if (++depth_ > kMaxDepth) {
        aborted_ = true;
        return false;
    }
    struct Leave {
        int& d;
        ~Leave() { --d; }
    } leave{depth_};
    int t = KindClass(kind);
    bool ok = Area(t, x, y, y - 1, y + 1);
    if (ok) {
        Propagate(t, x, y);
        ok = !aborted_ && Revalidate(t, x, y);
        if (ok) {
            retry_.clear();
            Place(kind, x, y);
            retry_.clear();
        }
    }
    if (!ok) msg_ = kCannotPaint;
    return ok;
}

// 0x43dab0: crushed crystal, hardened tar, roots. Next to cropland interior,
// the interior corners around the cell first turn back into cropland edge.
bool TerrainPainter::PaintSubtype(int kind, int x, int y) {
    int t = KindClass(kind);
    if (Area(t, x, y, y - 1, y + 1)) return Generic(kind, x, y);
    if (!Area10(t, x, y) || !Revalidate(t, x, y)) {
        msg_ = kCannotPaint;
        return false;
    }
    DemoteInterior(x, y);
    return Generic(kind, x, y);
}

// 0x43f530: corners 10 -> 9 in the 3x3 around (x, y). (The original repaints
// every cell of the 3x3; cells without an interior corner are left alone here.)
void TerrainPainter::DemoteInterior(int x, int y) {
    for (int cy = y - 1; cy <= y + 1; ++cy)
        for (int cx = x - 1; cx <= x + 1; ++cx) {
            if (!g_->Inside(cx, cy) || aborted_) continue;
            Corners c = Cor(cx, cy);
            if (c.c[0] == 255) continue;
            bool any = false;
            for (int i = 0; i < 4; ++i)
                if (c.c[i] == kCroplandInterior) {
                    c.c[i] = kCropland;
                    any = true;
                }
            if (!any) continue;
            int k = KindForCorners(c);
            if (k < 0)
                msg_ = kNoFertile;
            else
                Generic(k, cx, cy);
        }
}

// 0x43eb90 with 0x43ed30/0x43ef30/0x43f130/0x43f330: a corner of (x, y)
// becomes cropland interior when all four cells around it are cropland and
// none of them would be left with interior on two diagonal corners only.
bool TerrainPainter::PromoteCorners(int x, int y, Corners& c) const {
    Corners cur = Cor(x, y);
    c = cur;
    for (int i = 0; i < 4; ++i) {
        int vx = x + (i & 1), vy = y + (i >> 1);  // the vertex
        if (vx <= 0 || vy <= 0 || vx >= W() || vy >= H()) continue;  // needs all four cells
        bool ok = true;
        for (int j = 0; j < 4 && ok; ++j) {
            int cx = vx - 1 + (j & 1), cy = vy - 1 + (j >> 1);
            int sh = 3 - j;  // that cell's corner at the vertex
            uint16_t cls = Cls(cx, cy);
            if (cls != 9 && cls != 10) {
                ok = false;
                break;
            }
            Corners n = Cor(cx, cy);
            int opp = 3 - sh, o1 = sh ^ 1, o2 = sh ^ 2;
            if (n.c[opp] == kCroplandInterior && n.c[o1] != kCroplandInterior && n.c[o2] != kCroplandInterior) ok = false;
        }
        if (ok) c.c[i] = kCroplandInterior;
    }
    // Two promoted diagonal corners alone have no tile (the original would
    // place nothing there); keep the cell as it is then.
    if (KindForCorners(c) < 0) c = cur;
    return c != cur;
}

// 0x43ea20: cropland.
bool TerrainPainter::PaintCropland(int kind, int x, int y) {
    uint16_t old = Cls(x, y);
    if (!Allowed(kCropland, old)) {
        msg_ = kInvalidCombination;
        return false;
    }
    if (!Area(kCropland, x, y, y - 1, y + 1)) {
        msg_ = kCannotPaint;
        return false;
    }
    int k = kind;
    if (old == 10) {
        int kk = KindForCorners(Cor(x, y));
        if (kk >= 0) k = kk;
    }
    // A cell already cropland (interior) is repainted only if the 3x3 accepts
    // the interior kind; the promotion below still runs.
    bool ok = Generic(k, x, y) || old == 10;
    Corners c;
    if (PromoteCorners(x, y, c)) {
        retry_.clear();
        Place(KindForCorners(c), x, y);
        ok = true;
    }
    return ok;
}

// 0x43cd70: crystal cliff on (x, y).
int TerrainPainter::CliffAt(int kind, int x, int y, bool propagate) {
    if (aborted_) return 0;
    if (propagate) Propagate(kCliff, x, y);
    if (aborted_) return 0;
    uint16_t cls = Cls(x, y);
    if (cls == 0) return CliffOverCrystal(kind, x, y);
    if (cls == 12) return CliffOverCliff(kind, x, y) ? 1 : 0;
    msg_ = kCannotPaint;
    return 0;
}

// 0x43ceb0: cliff on a crystal cell. A crystal cell touching a cliff tile's
// plateau corner is already on the plateau: nothing to do (-1; the original
// returns 0 without a message).
int TerrainPainter::CliffOverCrystal(int kind, int x, int y) {
    Stamp(kind, x, y);
    static const struct {
        int dx, dy;
        int8_t a, b;  // corners of that neighbour touching (x, y); -1 = none
    } n[8] = {{-1, -1, 3, -1}, {0, -1, 2, 3}, {1, -1, 2, -1}, {-1, 0, 1, 3},
              {1, 0, 0, 2},    {-1, 1, 1, -1}, {0, 1, 0, 1},  {1, 1, 0, -1}};
    for (const auto& e : n) {
        int cx = x + e.dx, cy = y + e.dy;
        if (!g_->Inside(cx, cy) || Cls(cx, cy) != 12) continue;
        Corners c = Cor(cx, cy);
        if (c.c[e.a] == kCliff || (e.b >= 0 && c.c[e.b] == kCliff)) return -1;
    }
    // Deeper inside a plateau no neighbour shows a cliff corner and the
    // original raises a second plateau on the first; a brush hits such cells
    // all the time. Here the uniform cliff isn't painted where OnPlateau says
    // the cell already is plateau.
    if (kind == UniformKind(kCliff) && OnPlateau(x, y)) return -1;
    return Generic(kind, x, y) ? 1 : 0;
}

// Is crystal-art cell (x, y) on a plateau? Looking along the row and the
// column through crystal-art cells, the first other tile met in each
// direction votes by the two corners facing (x, y): both cliff = plateau,
// anything else = lowland. Plateau wins with at least two votes and more
// than lowland. Exact for the closed plateaus the painter makes.
bool TerrainPainter::OnPlateau(int x, int y) const {
    static const int dir[4][4] = {{-1, 0, 1, 3}, {1, 0, 0, 2}, {0, -1, 2, 3}, {0, 1, 0, 1}};  // dx, dy, facing corners
    int up = 0, down = 0;
    for (const auto& d : dir) {
        int cx = x + d[0], cy = y + d[1];
        while (g_->Inside(cx, cy) && IsPlateauArt(TileAt(cx, cy))) cx += d[0], cy += d[1];
        if (!g_->Inside(cx, cy)) continue;
        Corners c = Cor(cx, cy);
        if (c.c[d[2]] == kCliff && c.c[d[3]] == kCliff)
            ++up;
        else
            ++down;
    }
    return up >= 2 && up > down;
}

// 0x43d2e0 with the check 0x43d640: cliff on a cliff edge tile. The cell
// becomes plateau and its lowland corners' neighbours get cliff corners.
bool TerrainPainter::CliffOverCliff(int kind, int x, int y) {
    Corners c = Cor(x, y);
    bool l = x > 0, r = x < W() - 1, u = y > 0, dn = y < H() - 1;
    bool UL = false, UR = false, DL = false, DR = false, U = false, L = false, R = false, D = false;
    if (c.c[0] == 0) {
        L = l;
        if (u) UL = l, U = true;
    }
    if (c.c[1] == 0) {
        R = r;
        if (u) UR = r, U = true;
    }
    if (c.c[2] == 0) {
        if (l) L = true;
        if (dn) DL = l, D = true;
    }
    if (c.c[3] == 0) {
        if (r) R = true;
        if (dn) DR = r, D = true;
    }
    // 0x43d640: refuse if a neighbour that would get cliff corners already
    // has them (and more): the map disagrees with this cell there.
    auto cliff12 = [&](int cx, int cy, int a, int b, int e) {
        if (!g_->Inside(cx, cy) || Cls(cx, cy) != 12) return false;
        Corners n = Cor(cx, cy);
        return n.c[a] == kCliff && n.c[b] == kCliff && (e < 0 || n.c[e] == kCliff);
    };
    bool bad = (UL && cliff12(x - 1, y - 1, 3, 2, 1)) || (UR && cliff12(x + 1, y - 1, 2, 3, 0)) ||
               (U && cliff12(x, y - 1, 2, 3, -1)) || (L && cliff12(x - 1, y, 1, 3, -1)) ||
               (R && cliff12(x + 1, y, 0, 2, -1)) || (DL && cliff12(x - 1, y + 1, 1, 0, 3)) ||
               (DR && cliff12(x + 1, y + 1, 0, 1, 2)) || (D && cliff12(x, y + 1, 0, 1, -1));
    if (bad) {
        msg_ = kCannotPaint;
        return false;
    }
    const int K = kKeep, C = kCliff;
    if (UL) SetCorners(C4(K, K, K, C), x - 1, y - 1);
    if (U) SetCorners(C4(K, K, C, C), x, y - 1);
    if (UR) SetCorners(C4(K, K, C, K), x + 1, y - 1);
    if (L) SetCorners(C4(K, C, K, C), x - 1, y);
    if (R) SetCorners(C4(C, K, C, K), x + 1, y);
    if (DL) SetCorners(C4(K, C, K, K), x - 1, y + 1);
    if (D) SetCorners(C4(C, C, K, K), x, y + 1);
    if (DR) SetCorners(C4(C, K, K, K), x + 1, y + 1);
    Stamp(kind, x, y);
    return true;
}

// 0x43deb0: the canyon area check, repeated upwards past canyon walls.
bool TerrainPainter::CanyonUp(int x, int y) const {
    if (!Area(kCanyon, x, y, y - 2, y + 1)) return false;
    bool r = true;
    if (y > 0) {
        bool flag = (x > 0 && Cor(x - 1, y).c[0] == kCanyonWall) || (x < W() - 1 && Cor(x + 1, y).c[1] == kCanyonWall);
        if (flag) r = CanyonUp(x, y - 1);
    }
    if (r && y > 1) {
        bool flag = (x > 0 && Cor(x - 1, y - 2).c[0] == kCanyonWall) ||
                    (x < W() - 1 && Cor(x + 1, y - 2).c[1] == kCanyonWall);
        Corners c = Cor(x, y - 2);
        if ((c.c[1] == kCanyon && c.c[3] == 0) || (c.c[0] == kCanyon && c.c[2] == 0)) flag = true;
        if (flag) r = CanyonUp(x, y - 1);
    }
    return r;
}

// 0x43e140: the same downwards, while the cells below hold wall corners.
bool TerrainPainter::CanyonDown(int x, int y) const {
    if (!Area(kCanyon, x, y, y - 2, y + 1)) return false;
    if (y >= H() - 1) return true;
    bool flag = false;
    if (x > 0) {
        Corners c = Cor(x - 1, y + 1);
        if (c.c[2] == kCanyonWall && c.c[3] != kCanyonWall) flag = true;
    }
    if (x < W() - 1) {
        Corners c = Cor(x + 1, y + 1);
        if (c.c[3] == kCanyonWall && c.c[2] != kCanyonWall) flag = true;
    }
    Corners c = Cor(x, y + 1);
    if (c.c[3] == kCanyonWall || c.c[2] == kCanyonWall) flag = true;
    return flag ? CanyonDown(x, y + 1) : true;
}

// 0x43dbc0: crystal canyon. The cell becomes canyon and the cell above it
// the canyon's far wall (top corners 11). Cells that can't follow extend the
// canyon down or up.
bool TerrainPainter::PaintCanyon(int kind, int x, int y) {
    if (aborted_) return false;
    if (++depth_ > kMaxDepth) {
        aborted_ = true;
        return false;
    }
    struct Leave {
        int& d;
        ~Leave() { --d; }
    } leave{depth_};
    // 0x43de60
    if (!(Area(kCanyon, x, y, y - 2, y + 1) && CanyonUp(x, y) && CanyonDown(x, y))) {
        msg_ = kCannotPaint;
        return false;
    }
    retry_.clear();
    Place(kind, x, y);
    if (y < H() - 1) {
        bool below = false;
        for (size_t i = 0; i + 1 < retry_.size(); i += 2)
            if (retry_[i + 1] > y) below = true;
        if (below) PaintCanyon(kind, x, y + 1);
    }
    if (y > 0) {
        Corners a = Cor(x, y - 1);
        int c0 = a.c[0] == kCanyon ? kCanyon : kCanyonWall, c1 = a.c[1] == kCanyon ? kCanyon : kCanyonWall;
        if (y > 1) {
            Corners b = Cor(x, y - 2);
            if (b.c[0] == kCanyon && b.c[1] == kCanyon) c0 = c1 = kCanyon;
        }
        int wall = KindForCorners(C4(c0, c1, kCanyon, kCanyon));
        retry_.clear();
        Place(wall, x, y - 1);
        CanyonWallSides(x, y);
    }
    if (!retry_.empty()) PaintCanyon(kind, x, y - 1);
    CanyonBelow(x, y);
    return true;
}

// 0x43e300: after the wall went in at (x, y-1), cells beside the wall's top
// that still show a canyon corner (where the canyon can't reach) are trimmed.
void TerrainPainter::CanyonWallSides(int x, int y) {
    if (retry_.empty() || y <= 1) return;
    int yy = y - 2;
    if (Area(kCanyon, x, yy, yy - 2, yy + 1)) return;
    int crystal = UniformKind(kCrystal);
    if (x < W() - 1 && Cor(x + 1, yy) == C4(0, 2, 0, 0)) {
        int k = KindForCorners(C4(0, 0, 11, 0));
        if (k >= 0) {
            Place(k, x + 1, yy);
            if (y > 2 && Cor(x + 1, y - 3).c[1] == kCanyonWall && crystal >= 0) Place(crystal, x + 1, y - 3);
        }
    }
    if (x > 0 && Cor(x - 1, yy) == C4(2, 0, 0, 0)) {
        int k = KindForCorners(C4(0, 0, 0, 11));
        if (k >= 0) {
            Place(k, x - 1, yy);
            if (y > 2 && Cor(x - 1, y - 3).c[0] == kCanyonWall && crystal >= 0) Place(crystal, x - 1, y - 3);
        }
    }
}

// 0x43e5c0: a canyon cell right above another canyon's wall joins them.
void TerrainPainter::CanyonBelow(int x, int y) {
    if (y >= H() - 1) return;
    Corners c = Cor(x, y + 1);
    auto ok = [](int v) { return v == kCanyon || v == 0 || v == kCanyonWall; };
    if ((c.c[3] == kCanyonWall && ok(c.c[2])) || (c.c[2] == kCanyonWall && ok(c.c[3]))) {
        retry_.clear();
        Place(UniformKind(kCanyon), x, y + 1);
    }
}

// 0x43e6c0: crystal over a canyon, canyon wall or canyon border cell.
bool TerrainPainter::CrystalOverCanyon(int kind, int x, int y) {
    if (aborted_) return false;
    if (++depth_ > kMaxDepth) {
        aborted_ = true;
        return false;
    }
    struct Leave {
        int& d;
        ~Leave() { --d; }
    } leave{depth_};
    bool first = true;
    for (;;) {
        uint16_t old = Cls(x, y);
        bool ok;
        if (!Allowed(0, old)) {
            msg_ = kInvalidCombination;
            ok = false;
        } else if (old == 2 || old == 11 || old == 16) {
            ok = Area(0, x, y, y - 1, y + 2);
        } else {
            ok = Area(0, x, y, y - 1, y + 1);
        }
        if (!ok) {
            if (msg_.empty()) msg_ = kCannotPaint;
            return first ? false : true;
        }
        first = false;
        retry_.clear();
        Place(kind, x, y);
        if (y > 0) {
            bool above = false;
            for (size_t i = 0; i + 1 < retry_.size(); i += 2)
                if (retry_[i + 1] < y) above = true;
            if (above) CrystalOverCanyon(kind, x, y - 1);
        }
        if (aborted_) return false;
        if (y < H() - 1) {
            // The cell below: its top is now crystal and canyon below it
            // shows the wall. (The original turns every non-crystal bottom
            // corner into wall, even sand or tar; only canyon and wall are
            // turned here.)
            Corners b = Cor(x, y + 1);
            auto wallOf = [](int v) { return v == kCanyon || v == kCanyonWall ? (int)kCanyonWall : v; };
            int k = KindForCorners(C4(0, 0, wallOf(b.c[2]), wallOf(b.c[3])));
            retry_.clear();
            if (k >= 0) Place(k, x, y + 1);
            if (!retry_.empty()) CrystalOverCanyon(kind, x, y + 1);
        }
        if (aborted_) return false;
        if (y == 0) return true;
        Corners a = Cor(x, y - 1);
        auto open = [](int v) { return v == kCanyonWall || v == 0 || v == kCanyon; };
        if (!open(a.c[0]) || !open(a.c[1])) return true;
        if (a.c[0] == kCanyon && a.c[1] == kCanyon) return true;
        retry_.clear();
        --y;
        Place(UniformKind(kCrystal), x, y);
        if (retry_.empty()) return true;
    }
}

// 0x43ca70: the Smart Palette dispatcher for one cell. True if the cell was
// painted (or already was that terrain).
bool TerrainPainter::Dispatch(int kind, int x, int y) {
    switch (KindClass(kind)) {
    case 0: {
        uint16_t c = Cls(x, y);
        if (c == 2 || c == 11 || c == 16) return CrystalOverCanyon(kind, x, y);
        if (c == 12 || c == 13) return true;  // 0x43daa0: crystal over a cliff edge does nothing
        return Generic(kind, x, y);
    }
    case 1: case 4: case 7: return PaintSubtype(kind, x, y);
    case 2: return PaintCanyon(kind, x, y);
    case 3: case 5: case 6: case 8: return Generic(kind, x, y);
    case 9: return PaintCropland(kind, x, y);
    case 12: return CliffAt(kind, x, y, true) != 0;
    case 14: msg_ = kNotImplemented; return false;
    default: msg_ = kCannotPaint; return false;
    }
}

// ---------------------------------------------------------------------------
// Operations

namespace {
// Corners drawn with crystal texture: crystal, cliff plateau (its tiles are
// crystal art) and the cliff detail pieces. A disagreement among these only
// ends a cliff line (as the designers' open terraces do); any other is a seam.
inline bool CrystalTextured(int v) { return v == kCrystal || v == kCliff || v == kCliffDetail; }
}  // namespace

// A plateau-art tile (crystal art that is also the plateau top) is judged from
// its own side: all its other neighbours must give crystal at every vertex, or
// all give plateau; a mix only ends a cliff line (1). Its neighbours don't
// judge it.
template <class TileFn>
int TerrainPainter::MismatchWith(TileFn tileAt, int w, int h, int x, int y) const {
    Corners c;
    uint16_t t = tileAt(x, y);
    if (!CornersOfTile(t, c)) return 0;
    bool art = IsPlateauArt(t);
    bool saw0 = false, saw12 = false;
    int worst = 0;
    for (int i = 0; i < 4; ++i) {
        int vx = x + (i & 1), vy = y + (i >> 1);
        for (int j = 0; j < 4; ++j) {
            int cx = vx - 1 + (j & 1), cy = vy - 1 + (j >> 1);
            if ((cx == x && cy == y) || cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
            uint16_t t2 = tileAt(cx, cy);
            Corners c2;
            if (!CornersOfTile(t2, c2)) continue;
            int v = c2.c[3 - j];
            if (IsPlateauArt(t2)) {
                if (art || c.c[i] == kCrystal || c.c[i] == kCliff) continue;
                if (!CrystalTextured(c.c[i])) return 2;
                worst = 1;
                continue;
            }
            if (art) {
                if (v == kCrystal)
                    saw0 = true;
                else if (v == kCliff)
                    saw12 = true;
                else if (CrystalTextured(v))
                    worst = 1;
                else
                    return 2;
                continue;
            }
            if (v == c.c[i]) continue;
            if (!(CrystalTextured(v) && CrystalTextured(c.c[i]))) return 2;
            worst = 1;
        }
    }
    if (saw0 && saw12) worst = 1;
    return worst;
}

int TerrainPainter::CellMismatch(const Grid& g, int x, int y) const {
    if (!g.Inside(x, y)) return 0;
    return MismatchWith([&](int cx, int cy) { return g.Get(cx, cy); }, g.Width(), g.Height(), x, y);
}

int TerrainPainter::CountInconsistent(const Grid& g, bool soft) const {
    int n = 0;
    for (int y = 0; y < g.Height(); ++y)
        for (int x = 0; x < g.Width(); ++x) n += CellMismatch(g, x, y) >= (soft ? 1 : 2);
    return n;
}

int TerrainPainter::TerrainAt(const Grid& g, int x, int y) const {
    if (!g.Inside(x, y)) return -1;
    Corners c;
    uint16_t t = g.Get(x, y);
    if (!CornersOfTile(t, c)) return -1;
    if (IsPlateauArt(t)) return kCrystal;
    int count[15] = {0};
    for (int i = 0; i < 4; ++i) {
        int v = c.c[i];
        if (v == kCroplandInterior) v = kCropland;
        if (v == kCanyonWall) v = kCanyon;
        if (v == kCliffDetail) v = kCliff;
        ++count[v];
    }
    // Most corners wins; on a tie the subtype (the higher number but the bases' order).
    int best = -1;
    for (int v = 0; v < 15; ++v)
        if (count[v] && (best < 0 || count[v] > count[best] || (count[v] == count[best] && v != 0 && v != 3 && v != 6)))
            best = v;
    return best;
}

TerrainPainter::Result TerrainPainter::Run(Grid& g, Terrain t, const std::vector<std::pair<int, int>>& cells) {
    Result res;
    int kind = loaded_ ? UniformKind(t) : kNoKind;
    if (kind < 0) {
        res.refused = (int)cells.size();
        res.message = loaded_ ? "No tile for that terrain" : "Terrain tables not loaded";
        return res;
    }
    g_ = &g;
    int w = g.Width(), h = g.Height();
    std::vector<int> slot(w * h, -1);    // cell -> index in res.changes
    std::vector<int> before(w * h, -1);  // per brush cell: original value of written cells
    for (const auto& p : cells) {
        int x = p.first, y = p.second;
        if (!g.Inside(x, y)) continue;
        journal_.clear();
        retry_.clear();
        msg_.clear();
        aborted_ = false;
        depth_ = 0;
        bool ok = Dispatch(kind, x, y) && !aborted_;
        if (!ok && (aborted_ || msg_.empty())) msg_ = kCannotPaint;
        // "Couldn't find tile!" is how the original leaves a neighbour whose
        // corners now disagree; with the guard such a paint is undone.
        if (ok && guard_ && !journal_.empty()) {
            for (const Undo1& u : journal_)
                if (before[u.y * w + u.x] < 0) before[u.y * w + u.x] = u.before;
            auto beforeAt = [&](int cx, int cy) {
                int b = before[cy * w + cx];
                return b >= 0 ? (uint16_t)b : g.Get(cx, cy);
            };
            auto afterAt = [&](int cx, int cy) { return g.Get(cx, cy); };
            for (const Undo1& u : journal_) {
                for (int cy = u.y - 1; cy <= u.y + 1 && ok; ++cy)
                    for (int cx = u.x - 1; cx <= u.x + 1 && ok; ++cx) {
                        if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
                        if (MismatchWith(afterAt, w, h, cx, cy) == 2 && MismatchWith(beforeAt, w, h, cx, cy) < 2) ok = false;
                    }
                if (!ok) break;
            }
            for (const Undo1& u : journal_) before[u.y * w + u.x] = -1;
            if (!ok) msg_ = kNoTile;
        }
        if (!ok) {
            for (size_t i = journal_.size(); i-- > 0;) g.Set(journal_[i].x, journal_[i].y, journal_[i].before);
            ++res.refused;
            res.message = msg_;
            continue;
        }
        ++res.painted;
        for (const Undo1& u : journal_) {
            int& s = slot[u.y * w + u.x];
            if (s < 0) {
                s = (int)res.changes.size();
                res.changes.push_back({u.x, u.y, u.before, u.before});
            }
        }
    }
    // Final values; cells that ended where they started drop out.
    Changes out;
    for (CellChange& c : res.changes) {
        c.after = g.Get(c.x, c.y);
        if (c.after != c.before) out.push_back(c);
    }
    res.changes.swap(out);
    journal_.clear();
    g_ = nullptr;
    return res;
}

TerrainPainter::Result TerrainPainter::PaintCell(Grid& g, Terrain t, int x, int y) { return Run(g, t, {{x, y}}); }

TerrainPainter::Result TerrainPainter::PaintCircle(Grid& g, Terrain t, int radius, int x, int y) {
    std::vector<std::pair<int, int>> cells;
    radius = std::max(0, radius);
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            if (dx * dx + dy * dy <= radius * radius + radius) cells.push_back({x + dx, y + dy});
    return Run(g, t, cells);
}

TerrainPainter::Result TerrainPainter::PaintSquare(Grid& g, Terrain t, int size, int x, int y) {
    std::vector<std::pair<int, int>> cells;
    size = std::max(1, size);
    for (int cy = BrushFirst(y, size); cy <= BrushLast(y, size); ++cy)
        for (int cx = BrushFirst(x, size); cx <= BrushLast(x, size); ++cx) cells.push_back({cx, cy});
    return Run(g, t, cells);
}

TerrainPainter::Result TerrainPainter::FillRect(Grid& g, Terrain t, int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> cells;
    for (int cy = std::min(y0, y1); cy <= std::max(y0, y1); ++cy)
        for (int cx = std::min(x0, x1); cx <= std::max(x0, x1); ++cx)
            if (g.Inside(cx, cy)) cells.push_back({cx, cy});
    return Run(g, t, cells);
}

TerrainPainter::Result TerrainPainter::StampKind(Grid& g, int kind, int x, int y) {
    Result res;
    if (!loaded_ || kind < 0 || kind >= KindCount() || !g.Inside(x, y)) {
        res.refused = 1;
        res.message = kCannotPaint;
        return res;
    }
    g_ = &g;
    journal_.clear();
    Stamp(kind, x, y);
    res.painted = 1;
    for (const Undo1& u : journal_) res.changes.push_back({u.x, u.y, u.before, g.Get(u.x, u.y)});
    journal_.clear();
    g_ = nullptr;
    return res;
}

TerrainPainter::Result TerrainPainter::FillAll(Grid& g, Terrain t) {
    Result res;
    int kind = loaded_ ? UniformKind(t) : kNoKind;
    if (kind < 0) {
        res.refused = 1;
        res.message = "Terrain tables not loaded";
        return res;
    }
    g_ = &g;
    journal_.clear();
    for (int y = 0; y < g.Height(); ++y)
        for (int x = 0; x < g.Width(); ++x) Stamp(kind, x, y);
    res.painted = g.Width() * g.Height();
    for (const Undo1& u : journal_) res.changes.push_back({u.x, u.y, u.before, g.Get(u.x, u.y)});
    journal_.clear();
    g_ = nullptr;
    return res;
}

void TerrainPainter::Undo(Grid& g, const Result& r) {
    for (size_t i = r.changes.size(); i-- > 0;) {
        const CellChange& c = r.changes[i];
        if (g.Inside(c.x, c.y)) g.Set(c.x, c.y, c.before);
    }
}

// ---------------------------------------------------------------------------
// SmartPainter

int SmartPainter::TypeCount() const { return (int)tp_.SmartTerrains().size(); }

std::string SmartPainter::TypeName(int type) const {
    return type >= 0 && type < TypeCount() ? TerrainPainter::TerrainName(tp_.SmartTerrains()[type]) : std::string("?");
}

bool SmartPainter::Finish(const TerrainPainter::Result& r, Changes& changed) {
    changed.insert(changed.end(), r.changes.begin(), r.changes.end());
    message_ = r.refused ? r.message : std::string();
    return r.refused == 0;
}

// After a refusal: the first terrain in or next to the brush area that `t`
// may not go on ("Lava can't go next to Sand"), else the original's message.
// A border class ("Tar/Lava") is named by its first terrain `t` may not touch.
void SmartPainter::Explain(const Grid& grid, Terrain t, int x0, int y0, int x1, int y1) {
    for (int y = std::max(0, y0 - 1); y <= std::min(grid.Height() - 1, y1 + 1); ++y)
        for (int x = std::max(0, x0 - 1); x <= std::min(grid.Width() - 1, x1 + 1); ++x) {
            int cls = tp_.ClassOfTile(grid.Get(x, y));
            if (cls == TerrainPainter::kNoClass || TerrainPainter::Allowed(t, cls)) continue;
            std::string what = TerrainPainter::ClassName(cls);
            if (cls > 13)  // a border: name the first of its terrains that t may not touch
                for (int c = 0; c < 14; ++c) {
                    std::string part = TerrainPainter::TerrainName(c);
                    std::string padded = "/" + what + "/";
                    if (padded.find("/" + part + "/") != std::string::npos && !TerrainPainter::Allowed(t, c)) {
                        what = part;
                        break;
                    }
                }
            message_ = std::string(TerrainPainter::TerrainName(t)) + " can't go next to " + what;
            return;
        }
}

bool SmartPainter::Paint(Grid& grid, int type, int size, int x, int y, Changes& changed) {
    if (type < 0 || type >= TypeCount()) {
        message_ = "No terrain chosen";
        return false;
    }
    Terrain t = tp_.SmartTerrains()[type];
    if (Finish(tp_.PaintSquare(grid, t, size, x, y), changed)) return true;
    Explain(grid, t, BrushFirst(x, size), BrushFirst(y, size), BrushLast(x, size), BrushLast(y, size));
    return false;
}

bool SmartPainter::Fill(Grid& grid, int type, int x0, int y0, int x1, int y1, Changes& changed) {
    if (type < 0 || type >= TypeCount()) {
        message_ = "No terrain chosen";
        return false;
    }
    Terrain t = tp_.SmartTerrains()[type];
    if (Finish(tp_.FillRect(grid, t, x0, y0, x1, y1), changed)) return true;
    Explain(grid, t, std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1));
    return false;
}

int SmartPainter::Pick(const Grid& grid, int x, int y) {
    int t = tp_.TerrainAt(grid, x, y);
    const std::vector<Terrain>& s = tp_.SmartTerrains();
    for (int i = 0; i < (int)s.size(); ++i)
        if (s[i] == t) return i;
    return -1;
}

}  // namespace editor
