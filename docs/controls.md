# WarBreeds Controls

*WarBreeds · 1998 · Red Orb Entertainment*

Every keyboard and mouse control in a mission, as the original game handles them, followed by what the WarBreeds HD patch adds. The list comes from reading the game's code (the manual's own list is incomplete), so a few rarely used keys are marked *(likely)* where the exact effect still needs confirming in play.

1. [Mouse](#mouse)
2. [Selection and groups](#selection-and-groups)
3. [Camera](#camera)
4. [Orders](#orders)
5. [Formations](#formations)
6. [Hold a key and click](#hold-a-key-and-click)
7. [Shamans (builders)](#shamans-builders)
8. [Interface and game](#interface-and-game)
9. [Multiplayer](#multiplayer)
10. [Keys the game doesn't use](#keys-the-game-doesnt-use)
11. [WarBreeds HD](#warbreeds-hd)

## Mouse

The original scheme uses the left button for everything: it selects, and with your units selected it gives orders. The right button clears the selection. The cursor shape always shows what a left click will do: an animated arrow for move, red for attack or target.

| Action | Result |
|---|---|
| Left-click your unit | Select it (replaces the selection). Never gives an order. |
| <kbd>Shift</kbd>+left-click your unit | Add it to the selection, or remove it if it's already selected. |
| Left-click your building | Select the building, even with units selected. Units clicked onto a building they can enter enter it instead. |
| Left-click an enemy | With your units selected: attack it. With nothing selected: select it to see its details. |
| Left-click a resource or pod | With units selected: target it (the units decide what to do, such as harvest). |
| Left-click the ground | With your units selected: move there. Fogged ground works too. |
| Left-drag a box | Select your units inside it (replaces the selection). A drag shorter than about 32 pixels counts as a click. |
| Double-click your building | Switch it on or off (it must be finished and selected). |
| Double-click an alert icon | The icons down the left edge: centre the camera on that event. |
| Right-click | Clear the selection. This also cancels a Shaman's build, plant, repair or upgrade mode. |
| Right-click the minimap | Move the selected units to that point. |
| Left-click or drag the minimap | Move the camera there (it follows while you hold the button). |
| Screen edges | Scroll the camera: 35 pixels deep at the top and bottom, 30 at the sides. Hold <kbd>Shift</kbd> to scroll twice as fast. |

> The original game ignores a quick second right-click (Windows reports it as a double-click), and it doesn't use the middle button or the mouse wheel.

## Selection and groups

| Key | Action |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>0</kbd>–<kbd>9</kbd> | Make the selected units group 0–9. Each unit shows its group number, and groups are kept in saved games. A unit belongs to one group at a time. |
| <kbd>0</kbd>–<kbd>9</kbd> | Select that group (units that have died drop out). |
| <kbd>Alt</kbd>+<kbd>0</kbd>–<kbd>9</kbd> | Select that group and centre the camera on it. |
| <kbd>E</kbd> | Select all your creatures on screen. With <kbd>Shift</kbd>: add them. |
| <kbd>Ctrl</kbd>+<kbd>E</kbd> | Select every creature on screen of the same type(s) as the selection. |
| <kbd>Ctrl</kbd>+<kbd>I</kbd> | Invert the selection within the screen. |
| <kbd>N</kbd> | Select your next creature and centre on it (cycles). |
| <kbd>D</kbd> | Select your next building and centre on it (cycles). |
| <kbd>Page Up</kbd> (hold) | Box selection and <kbd>E</kbd> pick flying units only. |
| <kbd>Page Down</kbd> (hold) | Box selection and <kbd>E</kbd> pick ground units only. |

## Camera

| Key | Action |
|---|---|
| <kbd>←</kbd> <kbd>→</kbd> <kbd>↑</kbd> <kbd>↓</kbd> | Scroll one step. |
| <kbd>Home</kbd> | Centre on the selection. |
| <kbd>L</kbd> | Lock the camera to the selection (it follows them; a lock icon shows). Press again to unlock. |
| <kbd>Z</kbd> | Toggle the 2× zoomed-out view. Works while paused. |

## Orders

| Key | Action |
|---|---|
| <kbd>S</kbd> | Stop: drop orders and stand still. |
| <kbd>X</kbd> | Scatter: stop, then spread out a little. |
| <kbd>Ctrl</kbd>+<kbd>X</kbd> | Hold position: guard the current spot. |
| <kbd>J</kbd> | Special ability of the K5 Leviathan only *(likely)* |
| <kbd>Delete</kbd> | Delete the saved patrol route under the cursor. |

> **<p><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>K</kbd> destroys everything you have selected. It is easy to hit by accident.</p>**

## Formations

> These act on a selection of several units.

| Key | Action |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>F1</kbd>–<kbd>F8</kbd> | Save the selection's current arrangement as formation 1–8. |
| <kbd>F1</kbd>–<kbd>F8</kbd> | Arrange the selection in formation 1–8. |
| <kbd>Alt</kbd>+<kbd>F1</kbd>–<kbd>F8</kbd> | Arrange in formation 1–8 and centre the camera. |
| <kbd>H</kbd> | Line up horizontally. |
| <kbd>V</kbd> | Line up vertically. |
| <kbd>/</kbd> and <kbd>\</kbd> | Line up diagonally (either way). Numpad <kbd>/</kbd> works too. |
| <kbd>[</kbd> and <kbd>]</kbd> | Rotate the formation 90° one way or the other. |

## Hold a key and click

Hold the key, then left-click.

| Hold | Then left-click |
|---|---|
| <kbd>A</kbd> | An enemy: attack it (the order goes on button-down). |
| <kbd>Space</kbd> | The ground: move there keeping the formation. |
| <kbd>Tab</kbd> | Up to 16 waypoints; the selection moves through them when you let go of Tab. End on an earlier point to patrol. Also sets a Gene Lab's rally route. |
| <kbd>Ctrl</kbd>+<kbd>Tab</kbd> | Draw a saved patrol route. Later, select units and click the route to send them along it. |
| <kbd>G</kbd> | One of your units: follow or escort it. |
| <kbd>C</kbd> | The ground: a group move, probably closing ranks *(likely)* |
| <kbd>Shift</kbd> | Units: add to the selection (also doubles edge-scroll speed). |
| <kbd>T</kbd> | Changes what the cursor can pick, so a click orders instead of selecting *(likely)* |
| <kbd>F</kbd> | (No click needed.) Makes your units blink so you can find them *(likely)* |

## Shamans (builders)

These keys work with exactly one of your Shamans selected. They open the same pages as the info-panel buttons.

| Key | Action |
|---|---|
| <kbd>B</kbd> | Build: opens the list of buildings. Pick one, then click where it goes. |
| <kbd>P</kbd> | Plant: opens the list of pods. Pick one, then click pod soil (the pale lavender ground). |
| <kbd>R</kbd> | Repair: click one of your buildings. This is also the only way to finish a building whose builder was killed. |
| <kbd>U</kbd> | Upgrade: click one of your buildings. |
| <kbd>G</kbd> | Get Genes (only once you have genes to collect). |

### Leaving a mode

> Press the panel's cancel button, right-click (this also clears the selection), or select something else. <kbd>Esc</kbd> opens the game menu instead.

### Placement rules

> Buildings need power in range; a Refinery needs a pod planted nearby. A refused spot just ignores the click. Repair also refuses buildings that are unpowered or switched off.

## Interface and game

| Key | Action |
|---|---|
| <kbd>Esc</kbd> | Open the game menu (options, sound, load and save, in a skirmish too, quit). |
| <kbd>Pause</kbd> | Pause and resume. While paused, only Pause and <kbd>Z</kbd> work. |
| <kbd>I</kbd> | Switch the info panel to its second page (for creatures, their stance). |
| <kbd>+</kbd> / <kbd>=</kbd> and <kbd>-</kbd> | Zoom the minimap in and out. |
| <kbd>O</kbd> | Toggle a visibility view *(likely)* |
| <kbd>F10</kbd> (hold) + drag | Move the info panel or the minimap. |

## Multiplayer

| Key | Action |
|---|---|
| <kbd>Enter</kbd> | Type a chat message; <kbd>Enter</kbd> again sends it. |
| <kbd>M</kbd> (hold) + left-click | Place a marker the other players can see. |
| <kbd>Q</kbd> | Only while the game waits for a player's connection *(likely)* |

> Developer leftovers: typing <kbd>SPAMSPAMSPAMHUMBUG</kbd> in a mission unlocks a debug console on <kbd>F9</kbd>.

## Keys the game doesn't use

> Useful when choosing new shortcuts. <kbd>Shift</kbd>+letter behaves like the plain letter.

<kbd>K</kbd> <kbd>W</kbd> <kbd>Y</kbd> <kbd>Q</kbd> <kbd>F11</kbd> <kbd>F12</kbd> <kbd>Alt + any letter</kbd> <kbd>Ctrl + most letters</kbd> <kbd>Numpad 0–9</kbd> <kbd>Numpad *</kbd> <kbd>Insert</kbd> <kbd>End</kbd> <kbd>Backspace</kbd> <kbd>`</kbd> <kbd>;</kbd> <kbd>'</kbd> <kbd>,</kbd> <kbd>.</kbd>

*Added by the patch*

## WarBreeds HD

Everything above still works the same unless noted. Most additions can be switched off on the main menu's Settings page.

### Everywhere

| Control | Action |
|---|---|
| <kbd>Alt</kbd>+<kbd>Enter</kbd> | Switch between fullscreen and a window. |
| <kbd>Alt</kbd>+<kbd>F4</kbd> | Quit the game. |
| <kbd>F2</kbd> | Show or hide the frame-rate counter. |
| Settings page | Click a setting for its next value, right-click for the previous one. Changes save at once. |
| Movies | <kbd>Space</kbd> skips, as before. Switching away from the game also skips the movie. |

### Skirmish setup and campaigns

| Control | Action |
|---|---|
| Click a row's team badge | Cycle its team: none, 1, 2, 3, 4. Players on the same team are allied and win together. |
| Click a row's colour | Choose the next free colour. |
| Right-click a row's colour | Back to the automatic colour. |
| Click a campaign you've started | Continue from the mission you reached, Start Over, or Back. |
| Esc menu > Objectives, in a skirmish | Lists every enemy to defeat, with a green check beside each one you have beaten. |
| Settings > Interface > Language | Show the game in another language, at once (files in the `Lang` folder). |

### Camera

| Control | Action |
|---|---|
| Mouse wheel | Zoom smoothly in and out, towards the point under the cursor. |
| Numpad <kbd>+</kbd> / <kbd>-</kbd> | Zoom in and out on the screen centre. These replace the minimap zoom on the numpad; the main-keyboard <kbd>+</kbd> and <kbd>-</kbd> still zoom the minimap. |
| Arrow keys | Scroll smoothly instead of in steps (speed set in Settings). |
| <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> | Optional scrolling, off by default because A, S and D are game keys. |

### Groups and selection

| Control | Action |
|---|---|
| <kbd>0</kbd>–<kbd>9</kbd> twice quickly | Select the group and centre the camera on it. |
| <kbd>Shift</kbd>+<kbd>0</kbd>–<kbd>9</kbd> | Add the group to the current selection. |
| <kbd>Shift</kbd>+drag | Add the units in the box to the selection (the original treated it as a click). |
| Double-click one of your units | Select every unit of yours of that type on screen. |

### Quick-build

> With one or more Shamans selected, press <kbd>B</kbd> to open the build list, then the building's letter. With the pod list open (<kbd>P</kbd>), <kbd>1</kbd>–<kbd>3</kbd> pick a pod. Letters for buildings you can't build yet keep their usual meaning.

| Key | Building | Key | Building |
|---|---|---|---|
| <kbd>R</kbd> | Refinery | <kbd>H</kbd> | Healer |
| <kbd>G</kbd> | Gene Lab | <kbd>W</kbd> | Warp |
| <kbd>L</kbd> | Relay | <kbd>S</kbd> | Summoner |
| <kbd>P</kbd> | Psi Tower | <kbd>C</kbd> | Collector |
| <kbd>M</kbd> | Mine | <kbd>Shift</kbd>+<kbd>G</kbd> | Gene Lab+ |
| <kbd>T</kbd> | Turret | <kbd>Shift</kbd>+<kbd>L</kbd> | Relay+ |
| <kbd>E</kbd> | Electric | <kbd>Shift</kbd>+<kbd>T</kbd> | Turret+ |
| <kbd>N</kbd> | Listener | <kbd>Shift</kbd>+<kbd>P</kbd> | Psi Tower+ |

### Builders and fog

| Control | Action |
|---|---|
| Click a Shaman onto your unfinished or damaged building | It goes and finishes or repairs it (the game's Repair order). The building must be powered and switched on. |
| Plant mode, click fogged ground | The Shaman walks there and plants. Buildings could always be placed in fog. In build or plant mode the cursor over fog shows that the Shaman will walk. |
| Several Shamans selected (and nothing else) | The panel shows the build, plant, upgrade and repair buttons, and <kbd>B</kbd>, <kbd>P</kbd>, <kbd>R</kbd>, <kbd>U</kbd> and quick-build work. The original offered them only with exactly one Shaman. Settings → Controls → Group Build. |
| Place a building with several Shamans | With Pathfinding: Improved, they all walk to the site, each works at its own spot round it, and they start it together (four Shamans start it in about a quarter of the time), then all build it on. With Original, the nearest Shaman starts it and the others walk to its edge and help build it as soon as it stands. If the building can't go there, nothing happens and you stay in build mode, as with one Shaman. |
| Place a Refinery, Relay, Relay+, Turret, Plasma Cannon or Electric Tower | Its range ring shows at the cursor, in the same running sparks as the rings of the field buildings already built. A Refinery collects every energy pod inside it (300 px); a Relay powers buildings within 275 px, a Relay+ 400 px. A Turret picks targets by itself within about 272 px and a Plasma Cannon about 336 px (both keep firing at a target out to 400 px); an Electric Tower zaps within 226 px. Settings → Controls → Placement Range. |
| Plant, repair or cancel with several Shamans | The nearest Shaman plants the pod. Repair sends them all. The panel's cancel stops them all. Upgrade and Get Genes act on the first Shaman only. |

### Buildings

| Control | Action |
|---|---|
| A working Listener | With Settings → Controls → Listener Finds Cloaked on (off by default), cloaked enemy Shadows and Reavers within about 480 px of it blink on the minimap. They stay invisible on the battlefield. |
| A Gene Lab or Gene Lab+ selected | Its rally point: left-click the ground (Classic), right-click (Modern), or Tab and clicks for a route. |

### Pathfinding

> Settings → Controls → Pathfinding. Improved is the default; Original is the 1998 behaviour. It takes effect from the next mission. In multiplayer everyone uses the host's choice.

| Order | Improved |
|---|---|
| Move anywhere | The unit plans its whole route around rocks, cliffs, water and buildings, instead of heading straight for the goal and getting stuck in dead ends. It replans when it sees a building in its way. |
| Move to a spot it can't reach | It goes to the nearest spot it can reach and stops there. |
| Move a group | Each unit gets its own spot in a loose cluster around the click. Formation moves (<kbd>Space</kbd>) are unchanged. |
| Group build (several builders) | Every builder walks to the site and works at its own spot round it; they start the building together, then all build it on. |
| A building site | Once a builder is on its way, your other units keep off the site; an idle one standing on it steps off. |
| A unit in the way | An idle friend walks off the path (out of a corridor, if it has to). Following a friend going the same way: wait behind it. A jam: wait about a second, then go another way if it isn't much longer. Two units meeting head-on in a narrow pass: one goes round, or backs out and then carries on. No closer to the goal for 15 seconds: the unit stops. |

### Modern mouse scheme

> Settings → Controls → Mouse Scheme. Classic (the original, described at the top) is the default.

| Control | Action |
|---|---|
| Left-click | Select only. Empty ground clears the selection; <kbd>Shift</kbd> adds or removes your units; box selection as before. |
| Right-click | Give the order the cursor shows: move, attack, harvest, enter. Builders onto your unfinished or damaged building repair it. |
| Right-click in a mode | Cancel build, plant, repair or upgrade, keeping the Shaman selected. |
| Right-click with a Gene Lab or Gene Lab+ selected | Set its rally point (with Classic, a left-click does, as in the original). |
| Right-click the minimap | Move there, as before. |
| Hold a key and left-click | Unchanged: <kbd>A</kbd>, <kbd>Tab</kbd>, <kbd>Space</kbd>, <kbd>G</kbd> and the others still use the left button. |

> In both schemes a quick second right-click now counts, where the original dropped it.

### Map editor

> Main menu → Map Editor. The panel sits in the top-right corner, with tabs: Terrain, Things, Units, Players, Map, Events and AI. Painting works like the original editor's smart palette: terrain blends into its neighbours, and a terrain only goes next to the terrains it has transitions for (Lava next to Tar, for example). When a stroke or a placement can't go somewhere, the panel says why. An outline at the cursor shows the cells a brush will paint, the rectangle a fill will cover, or the ground a thing will take.

| Control | Action |
|---|---|
| <kbd>Tab</kbd>, or Hide / Show (Tab) | Fold the panel to a one-line strip and open it again. The chosen terrain, brush or thing stays in use, so you can paint and place where the panel was. |
| Terrain: left-drag | Paint the chosen terrain with the chosen brush (1, 2, 3 or 5 cells). |
| Terrain: <kbd>Shift</kbd>+left-drag | Fill a rectangle. |
| Terrain: right-click | Pick the terrain under the cursor. |
| Things: left-click or drag | Place trees, rocks, ruins or crops (one per tile). Trees and rocks go on their own subtype (crushed crystal, roots, hardened tar), ruins on open ground, crops on cropland. |
| Things: right-click | Remove the crop, tree, rock or ruin under the cursor. |
| Things: Fill Pods / Spores / Weeds, Clear Crops | Every empty cropland tile gets a crop; or every crop goes. |
| Units: Creatures / Devices, then left-click | Place one for the chosen player (<kbd>&lt;</kbd> <kbd>&gt;</kbd> on the panel). Devices are placed complete, with or without power. |
| Units: Select, then left-click / drag | Select a thing; drag it to move it. The panel then sets hit points (<kbd>-</kbd> <kbd>+</kbd>; right-click for single points), rank and gizmos (click for the next, right-click for the previous), or deletes it (<kbd>Del</kbd> works too). Right-click deselects. |
| Players: the panel | Type (Computer, Human, Either, None; player 1 is always Human), race, CGK level per clan (click +1, right-click -1) and available clans. |
| Players: left-click on the map | Put the chosen player's start point there (a numbered ring in its colour). |
| Map: the panel | Name (click, type, <kbd>Enter</kbd>), fog (fog and shroud, fog only, none), players supported and recommended. The size is shown only. |
| Events tab | Opens the event (trigger) editor over the map, the game's own screen for it. Its buttons, or the keys below, step through and edit the map's events. |
| Events: <kbd>Shift</kbd> / <kbd>Ctrl</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd> with <kbd>←</kbd> <kbd>→</kbd> <kbd>Home</kbd> <kbd>End</kbd> | Previous / next / first / last event, condition or action. |
| Events: the same modifier with <kbd>A</kbd> / <kbd>R</kbd> | Add / remove an event, condition or action. |
| Events: <kbd>*</kbd>, <kbd>Esc</kbd> | Make the event persistent (it can fire again); close the event editor. |
| AI tab | Each computer player's squad, base, scouting and influence settings (1 to 5). A skirmish on a map you made uses them (the original game always replaced them with the clan defaults; the game's own maps still do, so they play as they always have). An AI AGGRESSION event changes them during play. |
| Check | List what may be wrong with the map: missing start points or Shamans, devices without power, start points cut off from each other, cropland leaving a narrow gap at the edge and more. Saving runs the same check and only warns. |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd> | Undo / redo: strokes, placements, moves, deletions and settings. |
| Arrow keys, mouse wheel, minimap | Scroll and zoom, as in a mission. |
| Save As, then type a name | Up to 8 letters or digits; <kbd>Enter</kbd> saves, <kbd>Esc</kbd> cancels. Built-in maps are never overwritten; an existing map of yours asks first. |
| Test Map, twice | Save, then start a skirmish on the map. The first click only asks; anything else you do in between cancels it. |
| Exit | Back to the map list. With unsaved changes, click it twice. |

---

Compiled for the WarBreeds HD patch from the GOG release of WarBreeds (1998). Reverse-engineering notes: `re/hotkeys.md` and `re/mouse.md` in the project.
