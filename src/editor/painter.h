// Map editor: the painting interface between the editor shell (editor.cpp)
// and a terrain painter.
//
// The shell owns the map, the mouse, undo/redo, redraw and saving; a painter
// only decides which tile ids go where. Stage 1 ships RawPainter (stamps one
// chosen tile id, raw_paint.cpp); the smart painter (automatic transitions,
// terrain_paint.cpp) implements the same interface.
//
// Units: a cell is one 64 px terrain square, the unit of the engine's terrain
// array (AGrid 0x55dd60 +8: u16 tile id per cell, 96 x 96, row-major). Only
// cells inside (Width, Height) belong to the map. A tile id is a frame of
// TERRAIN.MHK tBMH 9000 (0..417 in the shipped maps).
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace editor {

constexpr int kGridSize = 96;  // the engine's terrain array is always 96 x 96 cells

// The terrain being edited. In game it reads and writes the engine's array;
// a test can back it with a plain array.
class Grid {
public:
    virtual ~Grid() {}
    virtual int Width() const = 0;   // map size in cells (1..96)
    virtual int Height() const = 0;
    virtual uint16_t Get(int x, int y) const = 0;  // only for Inside(x, y)
    virtual void Set(int x, int y, uint16_t tile) = 0;
    bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < Width() && y < Height(); }
};

// One cell a painter wrote.
struct CellChange {
    int16_t x, y;
    uint16_t before, after;
};
using Changes = std::vector<CellChange>;

// A brush of `size` cells (1, 2, 3 or 5) on the cell (x, y) under the cursor
// covers x - (size-1)/2 .. x + size/2, the same in y: centred for odd sizes,
// the cursor cell and the cells right of and below it for size 2.
inline int BrushFirst(int at, int size) { return at - (size - 1) / 2; }
inline int BrushLast(int at, int size) { return at + size / 2; }

class Painter {
public:
    virtual ~Painter() {}

    // Palette: the terrain types this painter offers, in the order shown.
    virtual int TypeCount() const = 0;
    virtual std::string TypeName(int type) const = 0;

    // Paint `type` with a square brush of `size` cells at cell (x, y) (see
    // BrushFirst/BrushLast; cells outside the map are skipped). A painter
    // may also rewrite neighbouring cells (transitions). Every cell written is
    // appended to `changed` (duplicates allowed). Returns false, with a reason
    // in Message(), if the paint was refused; it may still have changed cells.
    virtual bool Paint(Grid& grid, int type, int size, int x, int y, Changes& changed) = 0;

    // Fill the rectangle of cells (x0, y0)..(x1, y1), inclusive, in any
    // corner order, clipped to the map. Same reporting as Paint.
    virtual bool Fill(Grid& grid, int type, int x0, int y0, int x1, int y1, Changes& changed) = 0;

    // Right-click picker: the palette type at cell (x, y), or -1. A painter
    // may add an entry for it (the raw painter adds "Tile n" for a tile that
    // is no palette type), so TypeCount() can grow.
    virtual int Pick(const Grid& grid, int x, int y) = 0;

    // Why the last Paint/Fill was refused (empty if it wasn't).
    virtual std::string Message() const { return std::string(); }
};

}  // namespace editor
