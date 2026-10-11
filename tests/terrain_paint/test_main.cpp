// Tests for the smart terrain painter (src/editor/terrain_paint.cpp).
//
//   terrain_paint_test <repo root> <output dir>
//
// 1. Re-derivation: every shipped map (testrun/DATA/MAPS and testrun/CAMPAIGN)
//    is read back through the corner model and each tile compared with what
//    the painter would choose.
// 2. Paint scenarios on test grids; every tile must stay consistent with its
//    neighbours. Before/after grids are written as <name>.tpg for
//    tools/terrain_paint_render.py.
// Exit code 0 if every required check passed.
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "terrain_paint.h"

using namespace editor;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            ++g_failures;                        \
            printf("  FAIL %s: ", #cond);        \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while (0)

class VecGrid : public Grid {
public:
    VecGrid(int w, int h, uint16_t fill = 0) : w_(w), h_(h), cells_(w * h, fill) {}
    int Width() const override { return w_; }
    int Height() const override { return h_; }
    uint16_t Get(int x, int y) const override { return cells_[y * w_ + x]; }
    void Set(int x, int y, uint16_t t) override { cells_[y * w_ + x] = t; }
    const std::vector<uint16_t>& Cells() const { return cells_; }
    bool operator==(const VecGrid& o) const { return w_ == o.w_ && h_ == o.h_ && cells_ == o.cells_; }

private:
    int w_, h_;
    std::vector<uint16_t> cells_;
};

// A .WBM's terrain (re/wbm-format.md §3.1-3.2): header, then u8 width and
// height in 32 px tiles, then 96 x 96 u16 cells column by column.
bool LoadMap(const std::string& path, VecGrid*& out, std::string& name) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d(20000 + 512);
    size_t n = fread(d.data(), 1, d.size(), f);
    fclose(f);
    if (n < 12) return false;
    int nameLen = d[10] | d[11] << 8;
    size_t o = 12 + nameLen;
    if (o + 2 + 2 * 9216 > n) return false;
    name.assign((const char*)&d[12], nameLen ? nameLen - 1 : 0);
    int w = d[o] / 2, h = d[o + 1] / 2;
    out = new VecGrid(w, h);
    for (int x = 0; x < 96; ++x)
        for (int y = 0; y < 96; ++y) {
            size_t p = o + 2 + 2 * (x * 96 + y);
            if (x < w && y < h) out->Set(x, y, (uint16_t)(d[p] | d[p + 1] << 8));
        }
    return true;
}

// .tpg: "TPG1", u16 w, u16 h, before tiles, after tiles (row-major u16), then
// one flag byte per cell: 1 = changed, 2 = seam after, 4 = crystal/plateau difference after.
void WriteTpg(const TerrainPainter& tp, const std::string& path, const VecGrid& before, const VecGrid& after) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint16_t wh[2] = {(uint16_t)after.Width(), (uint16_t)after.Height()};
    fwrite("TPG1", 1, 4, f);
    fwrite(wh, 2, 2, f);
    fwrite(before.Cells().data(), 2, before.Cells().size(), f);
    fwrite(after.Cells().data(), 2, after.Cells().size(), f);
    for (int y = 0; y < after.Height(); ++y)
        for (int x = 0; x < after.Width(); ++x) {
            int mm = tp.CellMismatch(after, x, y);
            uint8_t fl = (before.Get(x, y) != after.Get(x, y) ? 1 : 0) | (mm == 2 ? 2 : mm == 1 ? 4 : 0);
            fputc(fl, f);
        }
    fclose(f);
}

std::string CornerStr(const Corners& c) {
    char b[32];
    snprintf(b, sizeof b, "%d,%d,%d,%d", c.c[0], c.c[1], c.c[2], c.c[3]);
    return b;
}

// ---------------------------------------------------------------------------
// 1. Re-derivation

// The shipped maps: MPMAP*.WBM in DATA/MAPS and SP*.WBM in CAMPAIGN, from the
// untouched game copy (testgame/) if present, else testrun/. Other .WBM files
// (the editor's own test maps) are left out.
std::vector<std::string> ShippedMaps(const std::string& root) {
    std::vector<std::string> maps;
    for (const char* game : {"testgame", "testrun"}) {
        for (const char* dir : {"DATA/MAPS", "CAMPAIGN"}) {
            std::vector<std::string> v;
            std::error_code ec;
            for (auto& e : fs::directory_iterator(fs::path(root) / game / dir, ec)) {
                std::string f = e.path().filename().string();
                std::transform(f.begin(), f.end(), f.begin(), ::toupper);
                bool shipped = f.size() > 4 && f.substr(f.size() - 4) == ".WBM" &&
                               (f.rfind("MPMAP", 0) == 0 || f.rfind("SP", 0) == 0);
                if (shipped) v.push_back(e.path().string());
            }
            std::sort(v.begin(), v.end());
            maps.insert(maps.end(), v.begin(), v.end());
        }
        if (!maps.empty()) break;
    }
    return maps;
}

void Rederive(TerrainPainter& tp, const std::string& root) {
    printf("\n== 1. Re-derivation on the shipped maps ==\n");
    std::vector<std::string> maps = ShippedMaps(root);
    printf("maps: %zu (from %s)\n", maps.size(),
           maps.empty() ? "-" : fs::path(maps[0]).parent_path().parent_path().filename().string().c_str());
    CHECK(maps.size() == 54, "expected 54 shipped maps, found %zu", maps.size());

    long tiles = 0, unknown = 0;
    long kindMatch = 0, exactMatch = 0;            // per tile: own corners -> table
    long nbMatch = 0, nbMismatch = 0, nbSoft = 0;  // corners recovered from the neighbours
    std::map<int, long> kindMiss;                  // kind of tiles the table doesn't give back
    std::map<std::string, long> nbMissWhy;
    std::map<int, std::map<uint16_t, long>> variantUse;  // kind -> tile -> count
    long hardCells = 0, softCells = 0;
    std::map<int, long> hardByClass, softByClass;
    struct Example {
        std::string map;
        int x, y;
        std::string what;
    };
    std::vector<Example> examples;
    auto crystalTextured = [](int v) { return v == kCrystal || v == kCliff || v == kCliffDetail; };

    for (const std::string& path : maps) {
        VecGrid* g = nullptr;
        std::string name;
        if (!LoadMap(path, g, name)) {
            CHECK(false, "cannot read %s", path.c_str());
            continue;
        }
        int W = g->Width(), H = g->Height();
        // Vertex votes from every tile that isn't plateau art.
        std::vector<int> vote((W + 1) * (H + 1) * 15, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                Corners c;
                uint16_t t = g->Get(x, y);
                if (!tp.CornersOfTile(t, c) || tp.IsPlateauArt(t)) continue;
                for (int i = 0; i < 4; ++i) ++vote[((y + (i >> 1)) * (W + 1) + x + (i & 1)) * 15 + c.c[i]];
            }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                ++tiles;
                uint16_t t = g->Get(x, y);
                Corners own;
                if (!tp.CornersOfTile(t, own)) {
                    ++unknown;
                    continue;
                }
                int k = tp.KindOfTile(t);
                int k2 = tp.KindForCorners(own);
                variantUse[k2][t]++;
                if (tp.IsVariantOf(t, k2)) {
                    ++kindMatch;
                    if (tp.MostLikelyTile(k2) == t) ++exactMatch;
                } else {
                    ++kindMiss[1000 + k];
                }
                // Corners from the neighbourhood: each vertex takes its
                // majority vote (the tile's own corner breaks ties).
                Corners nb = own;
                bool any = false;
                for (int i = 0; i < 4; ++i) {
                    const int* v = &vote[((y + (i >> 1)) * (W + 1) + x + (i & 1)) * 15];
                    int best = -1;
                    for (int j = 0; j < 15; ++j)
                        if (v[j] && (best < 0 || v[j] > v[best] || (v[j] == v[best] && j == own.c[i]))) best = j;
                    if (best >= 0) {
                        nb.c[i] = (uint8_t)best;
                        any = true;
                    }
                }
                bool ok;
                if (tp.IsPlateauArt(t)) {
                    // Crystal-looking: right as plain crystal or as plateau top.
                    bool all0 = true, all12 = true;
                    for (int i = 0; i < 4; ++i) {
                        all0 &= nb.c[i] == kCrystal;
                        all12 &= nb.c[i] == kCliff;
                    }
                    ok = !any || all0 || all12;
                } else {
                    ok = tp.IsVariantOf(t, tp.KindForCorners(nb));
                }
                // Differences only between crystal-textured corners (crystal,
                // plateau, cliff detail) just end a cliff line.
                bool soft = true;
                for (int i = 0; i < 4; ++i)
                    if (nb.c[i] != own.c[i] && !(crystalTextured(nb.c[i]) && crystalTextured(own.c[i]))) soft = false;
                if (ok) {
                    ++nbMatch;
                } else {
                    ++nbMismatch;
                    int cls = tp.KindClass(k);
                    std::string why;
                    if (tp.KindForCorners(own) != k && !tp.IsVariantOf(t, tp.KindForCorners(own))) {
                        why = "kind shadowed in the table (hand-placed)";
                    } else if (soft) {
                        ++nbSoft;
                        why = cls == 13 ? "cliff detail piece, crystal/plateau corners only"
                                        : "crystal vs plateau only (open cliff lines)";
                    } else if (cls == 13) {
                        why = "cliff detail piece (hand-placed)";
                    } else if (tp.KindForCorners(nb) < 0) {
                        why = "neighbours give a corner combination with no tile";
                    } else {
                        why = std::string("neighbours disagree: ") + TerrainPainter::ClassName(cls);
                    }
                    ++nbMissWhy[why];
                    if (examples.size() < 8 && !soft && cls != 13)
                        examples.push_back({fs::path(path).filename().string(), x, y,
                                            "tile " + std::to_string(t) + " kind " + std::to_string(1000 + k) + " (" +
                                                CornerStr(own) + ") neighbours say (" + CornerStr(nb) + ")"});
                }
                int mm = tp.CellMismatch(*g, x, y);
                if (mm == 2) {
                    ++hardCells;
                    ++hardByClass[tp.KindClass(k)];
                } else if (mm == 1) {
                    ++softCells;
                    ++softByClass[tp.KindClass(k)];
                }
            }
        delete g;
    }
    long known = tiles - unknown;
    printf("tiles: %ld (%ld with an unknown tile id)\n", tiles, unknown);
    printf("A. own corners -> 15^4 table -> kind; tile is one of its variants: %ld / %ld = %.3f%%\n", kindMatch, known,
           100.0 * kindMatch / known);
    for (auto& kv : kindMiss) printf("   not given back: kind %d x %ld\n", kv.first, kv.second);
    printf("   exact tile id with a deterministic choice (most likely variant): %ld / %ld = %.2f%%\n", exactMatch,
           known, 100.0 * exactMatch / known);
    printf("B. corners recovered from the neighbours (vertex majority) -> kind -> variant: %ld / %ld = %.3f%%\n",
           nbMatch, known, 100.0 * nbMatch / known);
    printf("   counting crystal-vs-plateau-only differences as matches: %ld / %ld = %.3f%%\n", nbMatch + nbSoft, known,
           100.0 * (nbMatch + nbSoft) / known);
    for (auto& kv : nbMissWhy) printf("   mismatch: %-52s %ld\n", kv.first.c_str(), kv.second);
    for (auto& e : examples) printf("   e.g. %s (%d,%d): %s\n", e.map.c_str(), e.x, e.y, e.what.c_str());
    printf("C. validator (CellMismatch) on the shipped maps: %ld cells with a seam, %ld with crystal/plateau differences only, of %ld\n",
           hardCells, softCells, known);
    for (auto& kv : hardByClass)
        printf("   seam            class %2d %-26s %ld\n", kv.first, TerrainPainter::ClassName(kv.first), kv.second);
    for (auto& kv : softByClass)
        printf("   crystal/plateau class %2d %-26s %ld\n", kv.first, TerrainPainter::ClassName(kv.first), kv.second);
    CHECK(100.0 * kindMatch / known > 99.9, "per-tile selection should match");
    CHECK(100.0 * (nbMatch + nbSoft) / known > 99.5, "neighbour-derived selection should match");

    // Variants: the designers' maps against the TRVA weights.
    printf("D. variant use in the maps vs. TRVA weights (kinds with 3+ variants, >= 200 tiles):\n");
    for (auto& kv : variantUse) {
        int k = kv.first;
        const std::vector<uint16_t>& tl = tp.KindTiles(k);
        long total = 0;
        for (auto& u : kv.second) total += u.second;
        if (tl.size() < 3 || total < 200) continue;
        printf("   kind %d (%s), %ld tiles:", 1000 + k, TerrainPainter::ClassName(tp.KindClass(k)), total);
        for (uint16_t t : tl) {
            auto it = kv.second.find(t);
            printf(" %u:%.0f%%", t, 100.0 * (it == kv.second.end() ? 0 : it->second) / total);
        }
        printf("\n");
    }
}

// ---------------------------------------------------------------------------
// 2. Paint tests

struct Stats {
    int ops = 0, painted = 0, refused = 0, guard = 0;
};

TerrainPainter::Result Paint(TerrainPainter& tp, VecGrid& g, Terrain t, int r, int x, int y, Stats* st = nullptr) {
    TerrainPainter::Result res = tp.PaintCircle(g, t, r, x, y);
    if (st) {
        ++st->ops;
        st->painted += res.painted;
        st->refused += res.refused;
        if (res.refused && res.message == "Couldn't find tile!") ++st->guard;
    }
    return res;
}

// Every cell consistent and of a known tile.
int Bad(const TerrainPainter& tp, const VecGrid& g) {
    int n = 0;
    for (int y = 0; y < g.Height(); ++y)
        for (int x = 0; x < g.Width(); ++x) {
            Corners c;
            if (!tp.CornersOfTile(g.Get(x, y), c) || !tp.CellConsistent(g, x, y)) ++n;
        }
    return n;
}

int CountKind(const TerrainPainter& tp, const VecGrid& g, int cls) {
    int n = 0;
    for (uint16_t t : g.Cells()) n += tp.ClassOfTile(t) == cls;
    return n;
}

void Scenario(TerrainPainter& tp, const std::string& out, const char* name, VecGrid& g, const VecGrid& before,
              const char* note) {
    int bad = Bad(tp, g);
    int soft = tp.CountInconsistent(g, true) - tp.CountInconsistent(g);
    int changed = 0;
    for (size_t i = 0; i < g.Cells().size(); ++i) changed += g.Cells()[i] != before.Cells()[i];
    printf("  %-40s changed %4d cells, seams %d, crystal/plateau %d  %s\n", name, changed, bad, soft, note);
    CHECK(bad == 0, "%s leaves %d inconsistent cells", name, bad);
    WriteTpg(tp, out + "/" + name + ".tpg", before, g);
}

void PaintTests(TerrainPainter& tp, const std::string& root, const std::string& out) {
    printf("\n== 2. Paint tests ==\n");
    tp.Seed(12345);
    const int N = 32;
    struct Base {
        Terrain base;
        std::vector<Terrain> on;
    };
    // Each terrain on the base it belongs to.
    const Base bases[] = {
        {kCrystal, {kSand, kTar, kCrushedCrystal, kCanyon, kCliff}},
        {kSand, {kCrystal, kTar, kRoots, kHotMud}},
        {kTar, {kCrystal, kSand, kHardenedTar, kLava}},
    };
    for (const Base& b : bases)
        for (Terrain t : b.on) {
            VecGrid g(N, N);
            tp.FillAll(g, b.base);
            VecGrid before = g;
            // Circles of radius 0..3 apart from each other, plus one at the map edge.
            Paint(tp, g, t, 0, 5, 5);
            Paint(tp, g, t, 1, 14, 6);
            Paint(tp, g, t, 2, 24, 7);
            Paint(tp, g, t, 3, 8, 20);
            Paint(tp, g, t, 4, 23, 22);
            Paint(tp, g, t, 2, 0, 31);
            std::string nm = std::string("circle_") + TerrainPainter::TerrainName(t) + "_on_" + TerrainPainter::TerrainName(b.base);
            for (char& c : nm)
                if (c == ' ' || c == '/') c = '_';
            int cls = t;
            char note[64];
            snprintf(note, sizeof note, "(%d uniform %s tiles)", CountKind(tp, g, cls), TerrainPainter::TerrainName(t));
            Scenario(tp, out, nm.c_str(), g, before, note);
            CHECK(CountKind(tp, g, cls) > 0 || t == kCliff, "%s: no %s painted", nm.c_str(), TerrainPainter::TerrainName(t));
        }

    // Refusals the original makes.
    {
        VecGrid g(N, N);
        tp.FillAll(g, kSand);
        VecGrid before = g;
        TerrainPainter::Result r = Paint(tp, g, kCrushedCrystal, 0, 10, 10);
        printf("  crushed crystal on sand: painted %d refused %d \"%s\"\n", r.painted, r.refused, r.message.c_str());
        CHECK(r.refused == 1 && g == before, "crushed crystal on sand must be refused");
        r = Paint(tp, g, kCropland, 0, 10, 10);
        printf("  cropland on sand: painted %d refused %d \"%s\"\n", r.painted, r.refused, r.message.c_str());
        CHECK(r.refused == 1 && g == before, "cropland directly on sand must be refused");
        r = Paint(tp, g, kCanyon, 0, 10, 10);
        printf("  canyon on sand: painted %d refused %d \"%s\"\n", r.painted, r.refused, r.message.c_str());
        CHECK(r.refused == 1 && g == before, "canyon on sand must be refused");
    }

    // Cropland: on a patch of each cropland-capable subtype.
    for (Terrain base : {kSand, kCrystal, kTar}) {
        Terrain sub = base == kSand ? kRoots : base == kCrystal ? kCrushedCrystal : kHardenedTar;
        VecGrid g(N, N);
        tp.FillAll(g, base);
        tp.FillRect(g, sub, 4, 4, 27, 27);
        VecGrid before = g;
        Paint(tp, g, kCropland, 3, 11, 11);
        Paint(tp, g, kCropland, 2, 21, 20);
        Paint(tp, g, kCropland, 0, 20, 8);
        tp.FillRect(g, kCropland, 8, 22, 14, 25);
        std::string nm = std::string("cropland_on_") + TerrainPainter::TerrainName(sub);
        for (char& c : nm)
            if (c == ' ') c = '_';
        char note[64];
        snprintf(note, sizeof note, "(%d interior tiles)", CountKind(tp, g, 10));
        Scenario(tp, out, nm.c_str(), g, before, note);
        CHECK(CountKind(tp, g, 10) > 0, "%s: no cropland interior", nm.c_str());
        // The subtype painted back over part of the field turns interior to edge.
        VecGrid before2 = g;
        Paint(tp, g, sub, 1, 11, 11);
        Scenario(tp, out, (nm + "_then_subtype").c_str(), g, before2, "");
    }

    // A line between two types: sand left, crystal right, tar and canyon lines across.
    {
        VecGrid g(N, N);
        tp.FillAll(g, kSand);
        tp.FillRect(g, kCrystal, 16, 0, 31, 31);
        VecGrid mid = g;
        Scenario(tp, out, "line_sand_crystal", g, VecGrid(N, N, g.Get(0, 0)), "(fill rect)");
        for (int x = 2; x < 30; ++x) Paint(tp, g, kTar, 1, x, 8 + (x * 7) / 30);
        Scenario(tp, out, "line_tar_across_sand_crystal", g, mid, "");
        VecGrid mid2 = g;
        for (int y = 14; y < 30; ++y) Paint(tp, g, kCanyon, 0, 24 + (y % 5 == 0), y);
        Scenario(tp, out, "line_canyon_down_crystal", g, mid2, "");
        VecGrid mid3 = g;
        for (int y = 16; y < 28; ++y) Paint(tp, g, kCrystal, 0, 24, y);
        Scenario(tp, out, "crystal_over_canyon", g, mid3, "");
    }

    // Cliffs: plateaus, extended, merged and cut.
    {
        VecGrid g(N, N);
        tp.FillAll(g, kCrystal);
        VecGrid before = g;
        Paint(tp, g, kCliff, 0, 5, 5);
        Paint(tp, g, kCliff, 2, 14, 8);
        tp.FillRect(g, kCliff, 4, 16, 12, 26);
        for (int x = 18; x < 29; ++x) Paint(tp, g, kCliff, 1, x, 22);
        Scenario(tp, out, "cliffs", g, before, "");
        VecGrid mid = g;
        Paint(tp, g, kCliff, 0, 13, 21);
        Paint(tp, g, kCrystal, 1, 8, 21);
        Paint(tp, g, kSand, 1, 24, 22);
        Scenario(tp, out, "cliffs_edited", g, mid, "");
    }

    // Lava and hot mud pools, then the base painted back over them.
    {
        VecGrid g(N, N);
        tp.FillAll(g, kTar);
        tp.FillRect(g, kSand, 16, 0, 31, 31);
        VecGrid before = g;
        Paint(tp, g, kLava, 3, 8, 8);
        Paint(tp, g, kHotMud, 3, 24, 8);
        Paint(tp, g, kLava, 2, 8, 22);
        Paint(tp, g, kTar, 1, 8, 8);
        Paint(tp, g, kSand, 1, 24, 8);
        Paint(tp, g, kCrystal, 2, 16, 24);
        Scenario(tp, out, "pools_and_repaint", g, before, "");
    }

    // Speed: the shell calls the painter on every mouse move.
    {
        VecGrid g(96, 96);
        tp.FillAll(g, kCrystal);
        auto t0 = std::chrono::steady_clock::now();
        TerrainPainter::Result r = tp.FillRect(g, kSand, 0, 0, 95, 95);
        auto t1 = std::chrono::steady_clock::now();
        int n = 0;
        for (int i = 0; i < 200; ++i) n += tp.PaintCircle(g, i % 2 ? kTar : kCrystal, 4, (i * 37) % 96, (i * 53) % 96).painted;
        auto t2 = std::chrono::steady_clock::now();
        double fill = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double brush = std::chrono::duration<double, std::milli>(t2 - t1).count() / 200;
        printf("  speed: Auto Fill of a whole 96x96 map %.1f ms (%zu cells changed); radius-4 brush %.2f ms each\n", fill,
               r.changes.size(), brush);
        CHECK(Bad(tp, g) == 0, "speed test map has seams");
    }

    // Undo restores the grid exactly.
    {
        VecGrid g(N, N);
        tp.FillAll(g, kCrystal);
        tp.FillRect(g, kSand, 0, 0, 15, 31);
        VecGrid before = g;
        TerrainPainter::Result r = tp.PaintCircle(g, kTar, 4, 15, 15);
        TerrainPainter::Undo(g, r);
        CHECK(g == before, "undo restores the grid");
        printf("  undo of a %zu-cell paint restores the grid: %s\n", r.changes.size(), g == before ? "yes" : "NO");
    }

    // Random strokes over mixed terrain, with and without the guard.
    printf("  random strokes (2000 brush paints of random terrains and radii on a mixed 48x48 map):\n");
    for (int guard = 1; guard >= 0; --guard) {
        tp.SetGuard(guard != 0);
        tp.Seed(777);
        uint32_t r = 4242;
        auto rnd = [&](int n) {
            r = r * 1103515245u + 12345u;
            return (int)((r >> 16) % n);
        };
        VecGrid g(48, 48);
        tp.FillAll(g, kCrystal);
        tp.FillRect(g, kSand, 0, 0, 20, 47);
        tp.FillRect(g, kTar, 30, 0, 47, 47);
        VecGrid before = g;
        Stats st;
        const Terrain all[] = {kCrystal, kSand, kTar, kCrushedCrystal, kRoots, kHardenedTar, kLava,
                               kHotMud, kCanyon, kCliff, kCropland};
        int worst = 0;
        for (int i = 0; i < 2000; ++i) {
            Terrain t = all[rnd(11)];
            Paint(tp, g, t, rnd(4), rnd(48), rnd(48), &st);
            worst = std::max(worst, Bad(tp, g));
        }
        int bad = Bad(tp, g);
        printf("    guard %s: %d paints, %d cells painted, %d refused (%d by the guard); cells with a seam %d (at worst %d), crystal/plateau only %d\n",
               guard ? "on " : "off", st.ops, st.painted, st.refused, st.guard, bad, worst,
               tp.CountInconsistent(g, true) - tp.CountInconsistent(g));
        if (guard) {
            CHECK(bad == 0 && worst == 0, "random strokes with the guard leave %d inconsistent cells", worst);
            WriteTpg(tp, out + "/random_strokes.tpg", before, g);
        } else {
            WriteTpg(tp, out + "/random_strokes_noguard.tpg", before, g);
        }
    }
    tp.SetGuard(true);

    // Painting on shipped maps adds no inconsistency.
    printf("  strokes on shipped maps (no new inconsistent cells):\n");
    std::vector<std::string> shipped = ShippedMaps(root);
    for (const char* want : {"MPMAP1.WBM", "MPMAP10.WBM", "SPTAN5A.WBM", "SPMAG8A.WBM"}) {
        std::string path;
        for (const std::string& p : shipped)
            if (fs::path(p).filename().string() == want) path = p;
        const char* m = want;
        VecGrid* g = nullptr;
        std::string name;
        if (path.empty() || !LoadMap(path, g, name)) {
            CHECK(false, "cannot read %s", m);
            continue;
        }
        VecGrid before = *g;
        int pre = tp.CountInconsistent(*g);
        tp.Seed(99);
        uint32_t r = 99;
        auto rnd = [&](int n) {
            r = r * 1103515245u + 12345u;
            return (int)((r >> 16) % n);
        };
        Stats st;
        const Terrain all[] = {kCrystal, kSand, kTar, kCrushedCrystal, kRoots, kHardenedTar, kLava,
                               kHotMud, kCanyon, kCliff, kCropland};
        for (int i = 0; i < 400; ++i)
            Paint(tp, *g, all[rnd(11)], rnd(3), rnd(g->Width()), rnd(g->Height()), &st);
        // New inconsistencies only: cells inconsistent now that weren't before.
        int added = 0;
        for (int y = 0; y < g->Height(); ++y)
            for (int x = 0; x < g->Width(); ++x)
                added += !tp.CellConsistent(*g, x, y) && tp.CellConsistent(before, x, y);
        printf("    %-14s \"%s\": %d paints, %d cells painted, %d refused; cells with a seam before %d, new %d\n",
               fs::path(m).filename().string().c_str(), name.c_str(), st.ops, st.painted, st.refused, pre, added);
        CHECK(added == 0, "%s: %d new inconsistent cells", m, added);
        std::string nm = "map_" + fs::path(m).stem().string();
        WriteTpg(tp, out + "/" + nm + ".tpg", before, *g);
        delete g;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : ".";
    std::string out = argc > 2 ? argv[2] : "build/terrain_paint";
    fs::create_directories(out);
    TerrainPainter tp;
    std::string err;
    std::vector<std::string> paths = {(fs::path(root) / "testrun/DATA/TERRAIN.MHK").string(),
                                      (fs::path(root) / "testgame/DATA/TERRAIN.MHK").string()};
    if (!tp.LoadFirst(paths, &err)) {
        printf("cannot load the terrain tables: %s\n", err.c_str());
        return 2;
    }
    printf("== 0. Tables ==\n");
    printf("kinds: %d; palettes: %zu; smart terrains:", tp.KindCount(), tp.Palettes().size());
    for (Terrain t : tp.SmartTerrains()) printf(" %s", TerrainPainter::TerrainName(t));
    printf("\n");
    CHECK(tp.KindCount() == 222, "222 kinds");
    CHECK(tp.SmartTerrains().size() == 11, "11 smart terrains");
    for (const Palette& p : tp.Palettes()) {
        int tilesN = 0, things = 0;
        for (const PaletteItem& it : p.items) (it.category == PaletteItem::kTile ? tilesN : things)++;
        printf("  palette %d %-18s %2zu items (%d tiles, %d things)\n", p.id, p.name.c_str(), p.items.size(), tilesN, things);
    }
    // Every corner combination a kind has must come back from the table to a
    // kind with the same corners.
    int dup = 0;
    for (int k = 0; k < tp.KindCount(); ++k)
        if (tp.KindForCorners(tp.KindCorners(k)) != k) {
            ++dup;
            printf("  kind %d (%s) is shadowed by kind %d with the same corners\n", 1000 + k,
                   CornerStr(tp.KindCorners(k)).c_str(), 1000 + tp.KindForCorners(tp.KindCorners(k)));
        }

    Rederive(tp, root);
    PaintTests(tp, root, out);
    printf("\n%s (%d failed checks)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
