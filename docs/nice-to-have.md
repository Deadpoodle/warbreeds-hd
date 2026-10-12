# Nice to have

Ideas that were considered for WarBreeds HD and left out of the current plan. They are kept here so they aren't lost, with what was found about each.

**Current plan:**
1. Pathfinding overhaul.
2. Allied teams in skirmish.
3. Saving and loading skirmishes.
4. Campaign progress kept between sessions.
5. Last, the remaining bug reports: the long-skirmish crash involving the Blue Leviathan or Yellow Ophidian, gene gathering freezing input, and several Tab rally points on one Gene Pool.

## For a future version

### Map editor extras
The editor matches the original's map content, events and Global AI. Still missing:
- **Terrain-specific palettes:** placing border tiles by hand, as the original editor's palettes allowed. Smart painting covers normal use.
- **Palette thumbnails:** pictures of each tile in the palette instead of names only.
- **Campaign maps:** opening and saving the campaign missions. Built-in skirmish maps are already editable and are always saved under a new name.
- **No Lava as New Map's base terrain:** Lava as the starting terrain makes an unplayable map; take it out of the New Map choices.

### Installer
A download that installs itself on Windows and Linux. It would:
- find the game folder (GOG Galaxy, standalone GOG, Steam, Wine/Proton prefixes) or ask for it;
- copy the patch files and never overwrite an existing `WarBreedsHD.ini`;
- offer an uninstall;
- on Linux, set or print the `WINEDLLOVERRIDES="wsock32=n,b"` launch option.

Today the install is manual: copy three items next to `WARBREED.EXE`.

### Testing on more systems
- **Steam version:** not tested. An unrecognised `WARBREED.EXE` gets a message and the patch changes nothing, so it fails safe.
- **Linux / Steam Deck (Proton or Wine):** not tested. The README gives the expected launch option.

### Multiplayer extras
Multiplayer works again as of v0.13.2 (LAN and Internet over TCP/IP, up to four players). Possible additions:

- **Humans against computer players.** Researched and planned, then shelved. The findings, for picking it up later:
  - The 1998 game nearly supports it. In multiplayer, `AssignPlayers 0x43bd40` turns map players with no human slot into computer players while the session AI count (`settings+0x220`) is above 0.
  - The host's Start routine `0x48d330` zeroes that count at `0x48d424`.
  - The options dialog hides the AI Players button in multiplayer (`jne` at `0x48aed7`; layout shift at `0x48affb`).
  - The count already reaches every client inside the DirectPlay player data block.
  - Computer players aren't allied with each other in multiplayer: the loop at `0x43c06d` is single-player only.
  - Running the AI on every machine looks deterministic. It uses only the synced random generator `0x471320` and game-turn timers. Guards still needed:
    - force the preference byte `[0x53e0f8]` to 0;
    - pin the FPU control word in `SimulateTurn 0x463080`;
    - execute the three AI moves that go through the network command path directly;
    - add a desync check that hashes every random draw (the call at `0x47137d`).
  - Agreed design: computer players are all on one team, humans pick teams in the lobby, and the stock 4-human limit stays.
- **A lobby browser** for Internet games, so players don't have to swap IP addresses: a small registry service, peer-to-peer or through a relay.
- **More than four human players:** the DirectPlay session limit is set at `0x4c6ff5`. The 1998 network code never ran with more than four.

## Dropped

- **A "Modern" interface layout:** an Age of Empires-style bar along the bottom, and a panel that stays on screen (dimmed) when nothing is selected. The original layout stays.
- **Main-menu animation speed:** the globe looked faster than in the untouched game. Measurements found no difference (the same rate through the patch's renderer and through GOG's), so it wasn't pursued.
- **A skirmish difficulty setting of our own:** the game has none. The Esc menu's AI Options only adds armour to the human player's units, and inventing one was left out. Custom maps do keep their own Global AI settings in a skirmish.
- **Floating weapons after Test Map:** spinning weapons seen at the top left of a map after playing it through the editor's Test Map. They never appeared in a normal skirmish, and the investigation was stopped.
