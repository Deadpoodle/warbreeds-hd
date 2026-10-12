// Map editor: the smart terrain painter, a re-implementation of the 1998 map
// editor's automatic terrain transitions (MapEditor.exe 0x43c450-0x441439;
// re/editor-original.md §5.1, re/terrain-painter.md).
//
// Model. Every tile id (a frame of TERRAIN.MHK tBMH 9000) belongs to a tile
// "kind" (222 kinds, TRVA lists each kind's random variants). A kind has four
// corner terrain types (TRST: top-left, top-right, bottom-left, bottom-right)
// and a class (TRID, 0-23). Painting a terrain T on a cell sets the cell's
// four corners to T; each neighbour's corners that touch the cell follow, and
// the neighbour becomes the kind with those corners (a 15^4 lookup table).
// Where no kind exists, the change spreads outwards; canyons, cliffs and
// cropland have extra rules.
//
// The tables are read at run time from the game's own DATA\TERRAIN.MHK
// (TRID, TRST, TRVA, TRPL; nothing is shipped). Apart from opening that file
// the code is plain C++ and works on any editor::Grid.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include "painter.h"

namespace editor {

// Corner terrain types (TRST values).
enum Terrain : uint8_t {
    kCrystal = 0,
    kCrushedCrystal = 1,
    kCanyon = 2,
    kTar = 3,
    kHardenedTar = 4,
    kLava = 5,
    kSand = 6,
    kRoots = 7,
    kHotMud = 8,
    kCropland = 9,          // cropland edge
    kCroplandInterior = 10, // cropland corner surrounded by cropland
    kCanyonWall = 11,       // the vertex row above a canyon (its far wall)
    kCliff = 12,            // crystal cliff plateau
    kCliffDetail = 13,      // hand-placed cliff detail pieces only
    kKeep = 14,             // lookups only: keep the existing corner
};

struct Corners {
    uint8_t c[4];  // top-left, top-right, bottom-left, bottom-right
    bool operator==(const Corners& o) const { return c[0] == o.c[0] && c[1] == o.c[1] && c[2] == o.c[2] && c[3] == o.c[3]; }
    bool operator!=(const Corners& o) const { return !(*this == o); }
};

// One entry of a TRPL palette.
struct PaletteItem {
    enum Category : uint8_t { kLink = 0, kMenu = 1, kTile = 2, kObstacle = 3, kCreature = 4, kDevice = 5, kCrop = 6 };
    uint8_t category;
    bool isDefault;     // preselected item
    uint16_t id;        // kTile: kind resource id (1000+kind); 3-6: TRTH id
    int kind;           // kTile: kind index 0..221; else -1
    int thingType;      // 3-6: world thing type (wbm.py TYPE_NAMES numbering); else -1
    uint16_t preview;   // kTile: a tile id to draw as its thumbnail (first variant)
    std::string name;
};

struct Palette {
    uint16_t id;        // TRPL id
    std::string name;   // the original's drop-down names ("Smart Palette", "Roots", ...)
    std::vector<PaletteItem> items;
};

// The whole of a file, opened read-only and shared. The game opens TERRAIN.MHK
// without sharing, so the patch reads it before the game starts (editor::Install).
bool ReadArchive(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr);

class TerrainPainter {
public:
    // --- tables -------------------------------------------------------------
    // Reads TERRAIN.MHK (read-only, shared: a running game may hold it open).
    bool Load(const std::string& path, std::string* error = nullptr);
    // The first of `paths` that loads (e.g. "DATA\\TERRAIN.MHK", then a test copy).
    bool LoadFirst(const std::vector<std::string>& paths, std::string* error = nullptr);
    bool LoadFromMemory(const uint8_t* data, size_t size, std::string* error = nullptr);
    bool Loaded() const { return loaded_; }

    static constexpr int kNoKind = -1;
    static constexpr uint16_t kNoClass = 0xffff;
    int KindCount() const { return (int)kinds_.size(); }        // 222
    int KindClass(int kind) const;                              // TRID, kNoClass if out of range
    Corners KindCorners(int kind) const;                        // TRST
    const std::vector<uint16_t>& KindTiles(int kind) const;     // TRVA tile ids
    int KindForCorners(const Corners& c) const;                 // the 15^4 table; kNoKind if none
    int UniformKind(Terrain t) const;                           // KindForCorners(t,t,t,t)
    // Per tile id, as the original reads the map (the last kind listing an id
    // wins; kind 1221, the cliff plateau, is left out, so its crystal-looking
    // tiles read as crystal).
    int KindOfTile(uint16_t tile) const;                        // kNoKind if unknown
    uint16_t ClassOfTile(uint16_t tile) const;                  // kNoClass if unknown
    bool CornersOfTile(uint16_t tile, Corners& out) const;      // false if unknown
    bool IsVariantOf(uint16_t tile, int kind) const;
    // Tiles that look like crystal but may sit on a cliff plateau (kind 1221's).
    bool IsPlateauArt(uint16_t tile) const;
    // TRID compatibility (MapEditor 0x440aa0): may terrain/class `paint` go on a
    // cell of class `existing`?
    static bool Allowed(int paint, int existing);
    static const char* ClassName(int cls);
    static const char* TerrainName(int t);

    // Variants: weighted random choice as the original (TRVA cumulative
    // percentages, rand() % 101). The generator is MSVC's rand().
    void Seed(uint32_t seed) { rng_ = seed; }
    uint16_t PickTile(int kind);
    uint16_t MostLikelyTile(int kind) const;  // the deterministic choice used by the tests

    // The 16 terrain palettes of the drop-down, then Creatures and Devices.
    const std::vector<Palette>& Palettes() const { return palettes_; }
    // The Smart Palette's paintable terrains, in its order (Crystal, Crystal
    // Canyon, Crushed Crystal, Sand, Hot Mud, Roots, Tar, Crystal Cliff, Lava,
    // Hardened Tar, Cropland).
    const std::vector<Terrain>& SmartTerrains() const { return smart_; }

    // --- painting -----------------------------------------------------------
    struct Result {
        int painted = 0;      // brush cells accepted
        int refused = 0;      // brush cells refused (message says why)
        std::string message;  // last refusal, in the original's words
        Changes changes;      // every cell written: x, y, value before, value after (one entry per cell)
    };

    // Smart paint of terrain `t` on every cell of a brush, cell by cell in
    // row order. Radius 0 = one cell; radius r = cells with dx*dx+dy*dy <= r*r+r.
    Result PaintCircle(Grid& g, Terrain t, int radius, int x, int y);
    // Square brush of `size` cells as editor::BrushFirst/BrushLast.
    Result PaintSquare(Grid& g, Terrain t, int size, int x, int y);
    // Auto Fill: the rectangle (any corner order), row by row, clipped.
    Result FillRect(Grid& g, Terrain t, int x0, int y0, int x1, int y1);
    // One cell, the original's dispatcher (0x43ca70) for a smart-palette item.
    Result PaintCell(Grid& g, Terrain t, int x, int y);
    // Terrain-specific palettes: stamp a kind as is, no transitions (a random variant).
    Result StampKind(Grid& g, int kind, int x, int y);
    // New map: every cell of the map a uniform `t` (no transitions needed).
    Result FillAll(Grid& g, Terrain t);
    // Put the "before" values back (in reverse order).
    static void Undo(Grid& g, const Result& r);

    // With the guard on (default), a brush cell whose paint would leave a
    // visible seam (CellMismatch 2) where there was none, which the original
    // could do ("Couldn't find tile!"), is undone and counted as refused.
    void SetGuard(bool on) { guard_ = on; }

    // --- validation ---------------------------------------------------------
    // How well cell (x, y) agrees with its neighbours: each of its corners
    // against the corner the neighbouring tiles give the same vertex.
    //   0  all agree;
    //   1  only crystal against cliff plateau (or cliff detail) disagree: all
    //      three are drawn with crystal texture, so this just ends a cliff line
    //      (the shipped maps do it everywhere, and the original painter reads
    //      plateau tops as crystal);
    //   2  a visible seam (e.g. sand meeting tar with no border tile).
    // Unknown tile ids are not judged.
    int CellMismatch(const Grid& g, int x, int y) const;
    bool CellConsistent(const Grid& g, int x, int y) const { return CellMismatch(g, x, y) < 2; }
    int CountInconsistent(const Grid& g, bool soft = false) const;  // cells with a mismatch >= 2 (or >= 1)

    // The smart terrain a cell shows (for the right-click picker); -1 if unknown.
    int TerrainAt(const Grid& g, int x, int y) const;

private:
    struct Kind {
        uint16_t cls = kNoClass;
        Corners corners{{0, 0, 0, 0}};
        uint16_t thumb = 0;
        std::vector<uint16_t> tiles;   // variants
        std::vector<uint16_t> upper;   // cumulative percent bound of each variant
    };
    bool loaded_ = false;
    std::vector<Kind> kinds_;
    std::vector<int16_t> table_;       // 15^4 corners -> kind
    std::vector<int16_t> tileKind_;    // tile id -> kind (original's image maps)
    std::vector<uint8_t> plateauArt_;  // tile id -> listed by kind 1221
    std::vector<Palette> palettes_;
    std::vector<Terrain> smart_;
    uint32_t rng_ = 1;
    bool guard_ = true;

    // State of one operation.
    Grid* g_ = nullptr;
    struct Undo1 { int16_t x, y; uint16_t before; };
    std::vector<Undo1> journal_;       // writes of the current brush cell
    std::vector<int16_t> retry_;       // the original's retry list (x, y pairs)
    std::string msg_;
    int depth_ = 0;
    bool aborted_ = false;

    bool Parse(const uint8_t* data, size_t size, std::string* error);
    void BuildPalettes(const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& trpl,
                       const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& trth);
    int Rand();

    // Grid access as the original (outside the map reads as tile 0x13e, lava).
    int W() const { return g_->Width(); }
    int H() const { return g_->Height(); }
    uint16_t TileAt(int x, int y) const;
    uint16_t Cls(int x, int y) const { return ClassOfTile(TileAt(x, y)); }
    Corners Cor(int x, int y) const;
    void Write(int x, int y, uint16_t tile);
    void Stamp(int kind, int x, int y);   // ATerrainTile::PaintAt with a fresh variant

    // The original's routines (MapEditor.exe addresses).
    bool Dispatch(int kind, int x, int y);                         // 0x43ca70
    bool Generic(int kind, int x, int y);                          // 0x43cc70
    bool Area(int t, int x, int y, int y0, int y1) const;          // 0x440720 / 0x440980
    bool Area10(int t, int x, int y) const;                        // 0x440840
    bool Revalidate(int t, int x, int y) const;                    // 0x440510
    void Propagate(int t, int x, int y);                           // 0x43f730
    void PropagateTo(int t, int nx, int ny, int px, int py);       // 0x43f850
    void Place(int kind, int x, int y);                            // 0x440030
    void SetCorners(Corners c, int x, int y);                      // 0x4403c0
    bool PaintSubtype(int kind, int x, int y);                     // 0x43dab0
    void DemoteInterior(int x, int y);                             // 0x43f530
    bool PaintCropland(int kind, int x, int y);                    // 0x43ea20
    bool PromoteCorners(int x, int y, Corners& c) const;           // 0x43eb90
    int CliffAt(int kind, int x, int y, bool propagate);           // 0x43cd70: 1 done, 0 refused, -1 already plateau
    int CliffOverCrystal(int kind, int x, int y);                  // 0x43ceb0
    bool OnPlateau(int x, int y) const;
    bool CliffOverCliff(int kind, int x, int y);                   // 0x43d2e0
    bool PaintCanyon(int kind, int x, int y);                      // 0x43dbc0
    bool CanyonUp(int x, int y) const;                             // 0x43deb0
    bool CanyonDown(int x, int y) const;                           // 0x43e140
    void CanyonWallSides(int x, int y);                            // 0x43e300
    void CanyonBelow(int x, int y);                                // 0x43e5c0
    bool CrystalOverCanyon(int kind, int x, int y);                // 0x43e6c0

    Result Run(Grid& g, Terrain t, const std::vector<std::pair<int, int>>& cells);
    template <class TileFn>
    int MismatchWith(TileFn tileAt, int w, int h, int x, int y) const;
};

// The smart painter behind the shell's Painter interface (painter.h). Types
// are the Smart Palette terrains; the brush is the shell's square brush.
class SmartPainter : public Painter {
public:
    explicit SmartPainter(TerrainPainter& tp) : tp_(tp) {}
    int TypeCount() const override;
    std::string TypeName(int type) const override;
    bool Paint(Grid& grid, int type, int size, int x, int y, Changes& changed) override;
    bool Fill(Grid& grid, int type, int x0, int y0, int x1, int y1, Changes& changed) override;
    int Pick(const Grid& grid, int x, int y) override;
    std::string Message() const override { return message_; }
    std::vector<std::string> MessageParts() const override { return parts_; }

private:
    TerrainPainter& tp_;
    std::string message_;
    std::vector<std::string> parts_;  // message_ as a pattern and its terrain names
    bool Finish(const TerrainPainter::Result& r, Changes& changed);
    void Explain(const Grid& grid, Terrain t, int x0, int y0, int x1, int y1);
};

}  // namespace editor
