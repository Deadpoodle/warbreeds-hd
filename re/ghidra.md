# Ghidra project and exports

A persistent Ghidra project of `WARBREED.EXE`, the names and types we have learned, and the
grep-able export that research agents read. Everything under `re/` except `*.md`, `names.tsv`
and `types.tsv` is git-ignored, so the project and the exports stay local; the knowledge lives in
the two TSV files and is re-applied to a fresh project in minutes.

| Path | What |
|---|---|
| `re/ghidra-project/WarBreeds.gpr` | the Ghidra project (open it in the Ghidra GUI, or drive it headless) |
| `re/names.tsv` | every identified function, global, label and vtable: address, name, note, optional type |
| `re/types.tsv` | struct layouts: struct, offset, C type, field, note |
| `re/ghidra-export-v2/` | the current export (see below) |
| `re/ghidra-export/` | the first export (no RTTI, 20 names); kept for agents that still read it |
| `tools/ghidra/MarkVisualStudio.java` | pre-script: tells Ghidra the exe is MSVC so RTTI analysis runs |
| `tools/ghidra/ApplyNames.java` | applies `names.tsv` + `types.tsv`; re-runnable |
| `tools/ghidra/ExportCaches.java` | writes the export |

Tools: Ghidra 12.1.x with a JDK 21 (point `support/launch.properties` → `JAVA_HOME_OVERRIDE` at it).
Below, `GHIDRA` is the Ghidra folder and every path is relative to the project root.

## Daily use: add names, re-apply, re-export

1. Add lines to `re/names.tsv` and/or `re/types.tsv` (formats are documented in each file's header).
2. Apply and export in one headless run (about 3 minutes):

   ```
   GHIDRA/support/analyzeHeadless re/ghidra-project WarBreeds -process WARBREED.EXE -noanalysis \
     -scriptPath tools/ghidra \
     -postScript ApplyNames.java re/names.tsv re/types.tsv \
     -postScript ExportCaches.java re/ghidra-export-v2
   ```

   `ApplyNames` prints how many names/fields it applied and a `WARN` line for anything it could not
   (unknown type, address not in memory, conflicting data). Re-running is safe: names.tsv owns the
   user-defined labels at the addresses it lists, functions are renamed/moved in place, struct fields
   replace whatever overlapped them.
3. Close the GUI first if it has the project open (headless needs the project lock), or apply from the
   GUI instead: Script Manager → add `tools/ghidra` to the script directories → run `ApplyNames.java`
   (it asks for no input when run headless; in the GUI pass the arguments via the script's
   "Run with arguments" or edit the defaults).

## The export (`re/ghidra-export-v2/`)

| File | Content |
|---|---|
| `functions.tsv` | entry, name (`Class::Method` when known), size, callers, callees, signature |
| `decompiled.c` | every function, each headed `// ==== <entry> <name>` |
| `by-class/<Class>.c` | the same functions grouped by class/namespace; unnamespaced code in `range_<start>-<end>.c` (64 KB slices) |
| `classes.tsv` | every RTTI class: vtable, this-offset, bases, slot count, `name@address` per virtual slot |
| `types.h` | C definitions of the project structs and the RTTI class structs |
| `xrefs.tsv` | from, to, type, from-function, to-symbol |
| `strings.tsv` | address, referencing functions, string |
| `symbols.tsv` | every non-default symbol with its namespace |

Grep tips: addresses are 8-digit lowercase hex without `0x` (`0047f000`); `grep -n "// ==== 0047f000"
decompiled.c` finds a function; `grep -P "\t0055dd60\t" xrefs.tsv` lists users of a global.

## Rebuilding the project from scratch

Only needed for a new exe build or a corrupted project (about 5 minutes):

```
GHIDRA/support/analyzeHeadless re/ghidra-project WarBreeds -import testgame/WARBREED.EXE \
  -scriptPath tools/ghidra -preScript MarkVisualStudio.java \
  -postScript RecoverClassesFromRTTIScript.java
```

then the "apply and export" command above. `MarkVisualStudio.java` matters: MSVC 4/5 executables
have no Rich header, so Ghidra reports the compiler as `unknown` and its RTTI analyzer and
`RecoverClassesFromRTTIScript` refuse to run ("TypeDescriptor data type model is only valid for
Visual Studio windows PE"). With the mark, auto-analysis creates the RTTI structures and the script
recovers class namespaces, class structs and vtable-based virtual functions.

## Conventions

- Names: `Class::Method` for `__thiscall` methods (puts the function in the class namespace, so the
  decompiler types `this` as `Class *`), `g_Name` for globals, `Subsystem_Name` for free functions.
  Mid-function patch sites are labels (`Function_What`), shown as pre-comments in the listing.
- Notes start with a confidence letter: **H** read from the code, **M** mechanism clear but purpose
  inferred, **L** a guess to test in play.
- Struct names: an RTTI class's struct is the class itself (`SGame`, `APlayer`, `AShaman`...);
  non-polymorphic objects (`World`, ...) get a class namespace from their methods; plain data structs
  live in the `/WarBreeds` category.
- Never put raw decompiled code in the `re/*.md` reports; cite addresses.
