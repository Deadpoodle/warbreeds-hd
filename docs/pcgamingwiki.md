# PCGamingWiki edits for WarBreeds

Ready-to-paste wikitext for <https://www.pcgamingwiki.com/wiki/WarBreeds>. These sections follow the wiki's standard game-page templates. The page is a stub, so most of this is new; where a template already exists on the page (the video settings table is the usual one), replace its fields rather than adding a second copy.

Everything here is verified on the GOG.com release (Windows 10). The Steam release hasn't been checked.

Replace `YOURNAME` in the `{{Refcheck}}` tags with your PCGamingWiki username. Refchecks note who checked a claim, and when.

---

## Essential improvements

Goes under `==Essential improvements==`, which usually sits after `==Availability==`.

```
==Essential improvements==
===WarBreeds HD===
{{++}} Unofficial patch: native widescreen and ultrawide resolutions, smooth zoom, windowed and borderless modes, working cutscenes, a settings menu and modern controls.
{{Fixbox|description=Install WarBreeds HD|ref=<ref>{{Refcheck|user=YOURNAME|date=2026-10-11}}</ref>|fix=
# Download the latest release from [https://github.com/Deadpoodle/warbreeds-hd/releases GitHub].
# Copy {{file|wsock32.dll}} and the {{file|Fonts}} and {{file|Menus}} folders to {{folder|{{p|game}}}}, next to {{file|WARBREED.EXE}}.
# Launch the game. A '''Settings''' item appears on the main menu.

'''Notes'''
{{ii}} No game files are modified; delete the added files to uninstall.
{{ii}} Tested with the GOG.com release. An unrecognised executable is left unpatched.
{{ii}} The widescreen menu art is AI-assisted outpainting of the original art; set ''Menu Art'' to ''Original'' in Settings to use the original 4:3 art.
}}
```

## Game data

```
==Game data==
===Configuration file(s) location===
{{Game data|
{{Game data/config|Windows|{{p|game}}\WARBREED.CFG|{{p|game}}\DATA\prefs.dbg}}
}}
{{ii}} {{file|prefs.dbg}} stores the player preferences, including the resolution, in Mohawk resource format.

===Save game data location===
{{Game data|
{{Game data/saves|Windows|{{p|game}}\DATA\Saves\}}
}}
{{ii}} Skirmish games can't be saved.
```

## Video

Replace the existing `{{Video}}` block's fields with these (keep the field names the page already uses if they differ slightly).

```
==Video==
{{Video
|wsgf link                  =
|widescreen wsgf award      =
|multimonitor wsgf award    =
|ultrawidescreen wsgf award =
|4k ultra hd wsgf award     =
|widescreen resolution      = hackable
|widescreen resolution notes= 4:3 only (640x480 by default; the in-mission Game Options has a slider for larger 4:3 sizes). Use [[#WarBreeds HD|WarBreeds HD]] for any resolution; menus are shown at 4:3 with widescreen side art.
|multimonitor               = unknown
|multimonitor notes         =
|ultrawidescreen            = hackable
|ultrawidescreen notes      = Use [[#WarBreeds HD|WarBreeds HD]] (includes 21:9 menu art).
|4k ultra hd                = hackable
|4k ultra hd notes          = Use [[#WarBreeds HD|WarBreeds HD]].
|fov                        = n/a
|fov notes                  =
|windowed                   = hackable
|windowed notes             = Use [[#WarBreeds HD|WarBreeds HD]]; {{key|Alt|Enter}} toggles.
|borderless windowed        = hackable
|borderless windowed notes  = Use [[#WarBreeds HD|WarBreeds HD]] (its default fullscreen mode).
|anisotropic                = n/a
|anisotropic notes          =
|antialiasing               = false
|antialiasing notes         =
|vsync                      = hackable
|vsync notes                = Use [[#WarBreeds HD|WarBreeds HD]].
|60 fps                     = true
|60 fps notes               = The game composes about 64 frames per second, one per Windows timer tick.
|120 fps                    = false
|120 fps notes              = Fixed at about 64 FPS.
|hdr                        = false
|hdr notes                  =
|color blind                = false
|color blind notes          =
}}
```

## Issues fixed

```
==Issues fixed==
===Cutscenes don't play===
{{--}} The movies are QuickTime 2 files, and the game needs QuickTime for Windows, which modern systems lack, so the cutscenes are skipped.
{{Fixbox|description=Use WarBreeds HD|ref=<ref>{{Refcheck|user=YOURNAME|date=2026-10-11}}</ref>|fix=
Install [[#WarBreeds HD|WarBreeds HD]]; it replaces the QuickTime calls with its own Cinepak/IMA4 player.
}}

===Hangs or crashes on start-up===
{{--}} The 1998 timer and thread code can deadlock on multi-core CPUs.
{{Fixbox|description=Run the game on one CPU core|ref=<ref>{{Refcheck|user=YOURNAME|date=2026-10-11}}</ref>|fix=
The GOG.com release (through its bundled DDrawCompat) and [[#WarBreeds HD|WarBreeds HD]] both do this automatically. With other wrappers, set the process affinity of {{file|WARBREED.EXE}} to a single core.
}}
```

## Other information

```
==Other information==
===API===
{{API
|direct3d versions      =
|direct3d notes         =
|directdraw versions    = 
|directdraw notes       = Loaded at run time. The GOG.com release bundles [[DDrawCompat]] v0.6.0 as {{file|ddraw.dll}}.
|wrapper                = 
|wrapper notes          =
|windows 32-bit exe     = true
|windows 64-bit exe     = false
|windows arm app        = unknown
|windows exe notes      =
|mac os x powerpc app   = unknown
|macos intel 32-bit app = unknown
|macos intel 64-bit app = unknown
|macos arm app          = unknown
|macos app notes        =
|linux 32-bit executable= unknown
|linux 64-bit executable= unknown
|linux arm app          = unknown
|linux executable notes =
}}

===Middleware===
{{Middleware
|cutscenes        = QuickTime
|cutscenes notes  = QuickTime 2 movies (Cinepak video, IMA4 audio).
}}
{{ii}} The game uses Broderbund's Mohawk engine ({{file|.MHK}} archives).

===Keyboard shortcuts===
{{ii}} A full list of the game's keyboard and mouse controls, including several that the manual doesn't mention (formations on {{key|F1}}–{{key|F8}}, waypoints with {{key|Tab}}, camera lock on {{key|L}}), is in the [https://github.com/Deadpoodle/warbreeds-hd/blob/main/docs/controls.html WarBreeds HD controls reference].
```

## Notes for the edit summary

> Added WarBreeds HD (widescreen, windowed, cutscene fix), game data locations, video settings, cutscene and multi-core fixes, API/middleware. Verified on the GOG.com release.

Things deliberately left as `unknown` because they weren't tested: multi-monitor, Steam differences, Linux/macOS, controller support, audio settings. Fill them in only after checking.
