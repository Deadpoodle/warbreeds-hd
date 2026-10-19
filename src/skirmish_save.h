#pragma once
// Skirmish: Load Game and Save Game on the Esc menu; a skirmish save loads as a
// skirmish (its human player and setup in <name>.wbs.hd beside the save).

namespace skirmish_save {

void Install(void (*log)(const char*, ...));

}  // namespace skirmish_save
