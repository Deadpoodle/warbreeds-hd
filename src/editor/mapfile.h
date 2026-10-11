// Map editor: .WBM files outside the engine (re/wbm-format.md): the map list
// with each map's header, and the blank map that "New Map" starts from.
#pragma once
#include <string>
#include <vector>

namespace editor {

struct MapEntry {
    std::string file;  // e.g. "MPMAP1.WBM"
    std::string path;  // full path
    std::string name;  // from the header ("4 Corners")
    int width = 0, height = 0, players = 0;  // cells, players supported
    bool shipped = false;  // one of the game's own maps: never overwritten
};

// *.WBM in <gameDir>DATA\MAPS, shipped maps first, then by file name. Files
// whose header doesn't parse are left out.
std::vector<MapEntry> ListMaps(const std::string& gameDir);
bool ReadHeader(const std::string& path, MapEntry& out);
bool IsShippedMapName(const std::string& baseName);  // "MPMAP1" (any case)

struct NewMapSpec {
    int width = 64, height = 64;  // cells, 32..96
    int players = 4;              // supported, 2..8
    int terrain = 0;              // RawPainter::BaseName index
};

// Start cell (64 px cells) of player i of n on a w x h map: corners first,
// then the edge midpoints, inset from the edge.
void StartCell(int i, int n, int w, int h, int& x, int& y);

// Writes a blank map: the header, terrain (all `tile`), no pathing regions
// (format 4, every tile "no region"; the editor processes the map before it
// saves), `players` slots (slot 0 human, the rest computer, with the AI state
// left for the engine to create), no things, no triggers. False on error.
bool WriteBlankMap(const std::string& path, const NewMapSpec& spec, unsigned short tile, const std::string& name);

}  // namespace editor
