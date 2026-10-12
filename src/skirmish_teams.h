#pragma once
// Skirmish: allied teams and player colours on the setup screen, and watching
// on after defeat while an ally still fights.

namespace teams {

void Install(void (*log)(const char*, ...));
// After APlayerRegistry::SetupSkirmish: ally each team (when any team is set).
void Apply(void* registry);
// Tests: a point (640x480 menu coordinates) on a visible row's team (kind 0)
// or colour (kind 1) icon of the skirmish setup scene.
bool HitPoint(void* scene, int row, int kind, long& x, long& y);

}  // namespace teams
