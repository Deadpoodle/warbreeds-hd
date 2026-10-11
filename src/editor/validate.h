// Map editor, Stage 2: the map checker. The 1998 editor had none (its manual
// only gives advice); this one warns and never blocks a save.
// re/editor-original.md §7 item 13, re/editor-stage2.md.
#pragma once
#include <string>
#include <vector>

namespace editor {

struct Finding {
    std::string text;  // one short sentence for the panel and the log
};

// Checks the map as it is in the engine: players and start points, Shamans
// (or a Gene Lab and a Psi tower), device power, ground reachability between
// start points, Supported/Recommended, ownerless units, cropland a cell or two
// from the map edge, crops off cropland. Regenerates the game's obstacle map
// (the guide graph's input) first.
std::vector<Finding> Validate();

}  // namespace editor
