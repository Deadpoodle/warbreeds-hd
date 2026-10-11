#include "raw_paint.h"

#include <stdio.h>

#include <algorithm>

namespace editor {
namespace {

// The uniform terrains (re/editor-original.md §5.1, TRID classes), in palette
// order: base types first.
struct Base {
    const char* name;
    uint16_t cls;
};
const Base kBases[] = {
    {"Sand", 6},          {"Crystal", 0},        {"Tar", 3},     {"Roots", 7},
    {"Crushed Crystal", 1}, {"Hardened Tar", 4}, {"Cropland", 10}, {"Hot Mud", 8},
    {"Lava", 5},          {"Crystal Canyon", 2}, {"Crystal Cliff", 12},
};
constexpr int kBaseCount = sizeof kBases / sizeof kBases[0];

}  // namespace

int RawPainter::BaseCount() { return kBaseCount; }
const char* RawPainter::BaseName(int i) { return i >= 0 && i < kBaseCount ? kBases[i].name : "?"; }

RawPainter::RawPainter(const uint16_t* tileClass, int count) : classOf_(tileClass, tileClass + std::max(0, count)) {
    // Each terrain stamps the lowest tile id of its class.
    for (int b = 0; b < kBaseCount; ++b)
        for (int id = 0; id < count; ++id)
            if (classOf_[id] == kBases[b].cls) {
                entries_.push_back({kBases[b].name, (uint16_t)id, b});
                break;
            }
}

std::string RawPainter::TypeName(int type) const {
    return type >= 0 && type < TypeCount() ? entries_[type].name : std::string("?");
}

int RawPainter::TypeOfBase(int i) const {
    for (int t = 0; t < TypeCount(); ++t)
        if (entries_[t].base == i) return t;
    return -1;
}

void RawPainter::Stamp(Grid& grid, int x, int y, uint16_t tile, Changes& changed) {
    if (!grid.Inside(x, y)) return;
    uint16_t old = grid.Get(x, y);
    if (old == tile) return;
    grid.Set(x, y, tile);
    changed.push_back({(int16_t)x, (int16_t)y, old, tile});
}

bool RawPainter::Paint(Grid& grid, int type, int size, int x, int y, Changes& changed) {
    message_.clear();
    if (type < 0 || type >= TypeCount()) {
        message_ = "No terrain chosen";
        return false;
    }
    size = std::max(1, size);
    uint16_t tile = entries_[type].tile;
    for (int cy = BrushFirst(y, size); cy <= BrushLast(y, size); ++cy)
        for (int cx = BrushFirst(x, size); cx <= BrushLast(x, size); ++cx) Stamp(grid, cx, cy, tile, changed);
    return true;
}

bool RawPainter::Fill(Grid& grid, int type, int x0, int y0, int x1, int y1, Changes& changed) {
    message_.clear();
    if (type < 0 || type >= TypeCount()) {
        message_ = "No terrain chosen";
        return false;
    }
    uint16_t tile = entries_[type].tile;
    for (int cy = std::min(y0, y1); cy <= std::max(y0, y1); ++cy)
        for (int cx = std::min(x0, x1); cx <= std::max(x0, x1); ++cx) Stamp(grid, cx, cy, tile, changed);
    return true;
}

int RawPainter::Pick(const Grid& grid, int x, int y) {
    if (!grid.Inside(x, y)) return -1;
    uint16_t id = grid.Get(x, y);
    for (int t = 0; t < TypeCount(); ++t)
        if (entries_[t].tile == id) return t;
    // A uniform terrain's other variants pick that terrain.
    if (id < classOf_.size())
        for (int t = 0; t < TypeCount(); ++t)
            if (entries_[t].base >= 0 && kBases[entries_[t].base].cls == classOf_[id]) return t;
    // Anything else (a border piece...) becomes its own entry, replacing an earlier pick.
    char name[32];
    snprintf(name, sizeof name, "Tile %u", (unsigned)id);
    if (!entries_.empty() && entries_.back().base < 0) entries_.pop_back();
    entries_.push_back({name, id, -1});
    return TypeCount() - 1;
}

}  // namespace editor
