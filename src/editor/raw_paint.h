// Map editor, Stage 1 stand-in painter: stamps one chosen tile id, with no
// transitions. Its palette is the uniform terrains (one tile each); the
// right-click picker adds any other tile as "Tile n".
#pragma once
#include "painter.h"

namespace editor {

class RawPainter : public Painter {
public:
    // tileClass[id]: the TRID class of each tile id (AGrid +0x905c, filled
    // from TERRAIN.MHK when a map loads); count = number of ids.
    RawPainter(const uint16_t* tileClass, int count);

    // The palette names, in order, before any map is loaded (the New Map page).
    static int BaseCount();
    static const char* BaseName(int i);

    int TypeCount() const override { return (int)entries_.size(); }
    std::string TypeName(int type) const override;
    bool Paint(Grid& grid, int type, int size, int x, int y, Changes& changed) override;
    bool Fill(Grid& grid, int type, int x0, int y0, int x1, int y1, Changes& changed) override;
    int Pick(const Grid& grid, int x, int y) override;
    std::string Message() const override { return message_; }

    // Palette index of base entry i (BaseName order), or -1 if the terrain
    // set has no tile for it.
    int TypeOfBase(int i) const;
    uint16_t TileOf(int type) const { return type >= 0 && type < TypeCount() ? entries_[type].tile : 0; }

private:
    struct Entry {
        std::string name;
        uint16_t tile;
        int base;  // BaseName index, -1 for a picked tile
    };
    std::vector<Entry> entries_;
    std::vector<uint16_t> classOf_;
    std::string message_;
    void Stamp(Grid& grid, int x, int y, uint16_t tile, Changes& changed);
};

}  // namespace editor
