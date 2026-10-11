# Ghidra project and exports

A persistent Ghidra project of `WARBREED.EXE`, the names and types learned so far, and the grep-able
export that research agents read. The project and the exports are local (git-ignored); the knowledge
lives in two text files and can be re-applied to a fresh project in a few minutes.

| Path | What |
|---|---|
| `re/ghidra-project/WarBreeds.gpr` | the Ghidra project (Ghidra GUI, or headless) |
| `re/names.tsv` | every identified function, global, label and vtable: address, name, note, optional type |
| `re/types.tsv` | struct layouts: struct, offset, C type, field, note |
| `re/ghidra-export-v2/` | the current export (below) |
| `re/ghidra-export/` | the first export (no RTTI, 20 names), kept for readers that still use it |
| `re/subsystems.md` | the subsystem index built on all of this |
| `tools/ghidra/MarkVisualStudio.java` | pre-script: tells Ghidra the exe is MSVC so RTTI analysis can run |
| `tools/ghidra/ApplyNames.java` | RTTI class recovery + applies `names.tsv` and `types.tsv`; re-runnable |
| `tools/ghidra/ExportCaches.java` | writes the export |

Tools: Ghidra 12.1.x and a JDK 21 (point `support/launch.properties` → `JAVA_HOME_OVERRIDE` at it).
Below, `GHIDRA` is the Ghidra folder; other paths are relative to the project root.

## Add names, re-apply, re-export

1. Add lines to `re/names.tsv` and/or `re/types.tsv` (formats in each file's header).
2. Apply and export in one headless run (about 4 minutes; close the GUI first, it holds the project lock):

   ```
   GHIDRA/support/analyzeHeadless re/ghidra-project WarBreeds -process WARBREED.EXE -noanalysis \
     -scriptPath tools/ghidra \
     -postScript ApplyNames.java re/names.tsv re/types.tsv \
     -postScript ExportCaches.java re/ghidra-export-v2
   ```

   `ApplyNames` prints counts and a `WARN` line for anything it could not apply (unknown type, address
   not in memory, a field past a struct's size). Re-running is safe: the TSVs own the names at their
   addresses and the structs they define (GUI edits to those structs are overwritten; GUI renames of
   other things survive). In the GUI, run `ApplyNames.java` from the Script Manager (add `tools/ghidra`
   to the script directories); it asks for the two files.

What `ApplyNames` does, in order:
1. **RTTI.** Walks the MSVC RTTI itself (type descriptor ← complete object locator ← vftable, class
   hierarchy descriptor for bases); labels `Class::vftable`; moves each virtual function into the
   most-base class whose vtable holds it (unnamed ones become `Class::vfXX`, XX = slot offset) and
   makes it `__thiscall`; names functions that store a vtable into `this` `Class::Class` /
   `Class::~Class` (heuristic: a destructor is a vtable slot or is called from one).
2. **Structs.** One struct per RTTI class (vftable pointer, then the primary base's fields flattened,
   then `types.tsv`), plus the plain structs in `types.tsv`.
3. **Names** from `names.tsv`: functions are created where none exists; `Class::Method` methods get
   `__thiscall` (Ghidra's `__fastcall` guess is converted), so the decompiler types `this`; globals get
   their C type.
4. **Vtable structs** (`Class_vftable`, one typed function pointer per slot) applied at each vftable,
   so virtual calls read `this->vftable->Invalidate(...)`. Slot names: `Class_vftable` lines in
   `types.tsv`, else the function's name, else the base class's name for that slot.

## The export (`re/ghidra-export-v2/`)

| File | Content |
|---|---|
| `functions.tsv` | entry, name (`Class::Method` when known), size, callers, callees, signature with calling convention |
| `decompiled.c` | every function, each headed `// ==== <entry> <name>` |
| `by-class/<Class>.c` | the same, one file per class/namespace (`A::B` → `A__B.c`); functions without one in `range_<start>-<end>.c` (64 KB slices) |
| `classes.tsv` | every RTTI class: vtable, this-offset, bases, slot count, `name@address` per slot |
| `types.h` | C definitions of the project structs, class structs and vtable structs |
| `xrefs.tsv` | from, to, type, from-function, to-symbol |
| `strings.tsv` | address, referencing functions, string |
| `symbols.tsv` | every non-default symbol with its namespace |

Grep tips: addresses are 8-digit lowercase hex without `0x` (`0047f000`). `grep -n "// ==== 0047f000"
decompiled.c` finds a function; `grep -P "\t0055dd60\t" xrefs.tsv` lists users of a global.

## Rebuilding the project from scratch

For a new exe build or a broken project (about 5 minutes):

```
GHIDRA/support/analyzeHeadless re/ghidra-project WarBreeds -import testgame/WARBREED.EXE \
  -scriptPath tools/ghidra -preScript MarkVisualStudio.java \
  -postScript RunRttiAnalyzerScript.java
```

then the apply-and-export command above. Why the two extra scripts: MSVC 4/5 executables have no Rich
header, so Ghidra reports the compiler as `unknown`, and its RTTI analyzer refuses to create anything
("TypeDescriptor data type model is only valid for Visual Studio windows PE"). `MarkVisualStudio` sets
the compiler; `RunRttiAnalyzerScript` (ships with Ghidra) then creates the type descriptors and a
class namespace per type with proper demangled names (`IList<AThing*>`). Ghidra's analyzer still
cannot parse MSVC 5's object locators, and `RecoverClassesFromRTTIScript` reports "No new classes
recovered", which is why `ApplyNames` does the vtable work itself.

Library signatures: Ghidra's Function ID databases (VS2012-2019 and "vsOlder") match only a few
dozen CRT functions in this MSVC 5 build; the `windows_vs12_32` type archive is applied (and `ApplyNames` pulls
Windows types such as `RECT` from it on demand). Ghidra ships no DirectX types; DirectDraw is reached
through COM (no ddraw import), so it is not typed.

## Conventions

- Names: `Class::Method` for methods, `g_Name` for globals, `Subsystem_Name` for free functions,
  `Function_What` for labels inside a function (patch and call sites; shown as pre-comments).
- Notes start with a confidence letter: **H** read from the code, **M** mechanism clear but purpose
  inferred, **L** a guess to test in play.
- `World` (in `src/game.h`) is the RTTI class `AThingRegistry` at `[0x579278]`; 0x55dd60 is `AGrid`.
- Reports in `re/*.md` cite addresses and summarise; no raw decompiled code.
