// Apply the project's reverse-engineering knowledge to a Ghidra program. Re-runnable: run it
// again whenever re/names.tsv or re/types.tsv grows, then re-export.
//
//   analyzeHeadless <projDir> <projName> -process <program> -noanalysis
//       -scriptPath <this folder> -postScript ApplyNames.java <names.tsv> [types.tsv]
// (In the GUI, run it from the Script Manager; it asks for the two files.)
//
// Phases:
// 1. MSVC RTTI. Ghidra 12's RTTI analyzer creates the type descriptors (and a class namespace per
//    type) but cannot parse MSVC 4/5 object locators, so this script walks them itself: type
//    descriptor <- complete object locator <- vftable, plus the class hierarchy descriptor for
//    base classes. It labels Class::vftable, puts every virtual function into the most-base class
//    whose vtable holds it (default-named ones become Class::vfXX, XX = slot offset, and are made
//    __thiscall), and names constructors/destructors (functions storing the vtable) Class::Class
//    and Class::~Class.
// 2. Structs from types.tsv (see below) and every RTTI class. A class's struct is rebuilt on each
//    run: vftable pointer(s), then the primary base class's fields (flattened, so SGame shows the
//    Scene fields), then its own types.tsv fields. types.tsv is the source of truth: edits made to
//    these structs in the GUI are overwritten.
// 3. names.tsv: names, comments, prototypes, global types.
// 4. One vtable struct per vftable (Class_vftable: one function pointer per slot, named after the
//    function) applied at the vftable, so virtual calls decompile as this->vftable->Method(...).
//
// types.tsv: struct definitions, one field per line
//   Struct <TAB> 0xOFFSET <TAB> ctype <TAB> field [<TAB> note]
//   Struct <TAB> size     <TAB> 0xSIZE                 (minimum size of the struct)
//   Struct <TAB> base     <TAB> BaseStruct             (flatten BaseStruct's fields in first; RTTI
//                                                       classes get their primary base automatically)
//   Struct <TAB> desc     <TAB> <TAB> <TAB> description (struct comment)
// A struct named like a class namespace (RTTI class, or a Class:: used in names.tsv) is that
// class's structure, so its __thiscall methods see `this` as Class *. Other structs live in the
// /WarBreeds category. ctype: C basic types (char, short, int, long, unsigned ..., float, double,
// byte, word, dword, bool, void), struct/class names and Windows SDK types (RECT, HWND, ...), with
// '*' and [N] suffixes: "Scene *[16]", "short[4]", "APlayer *".
//
// names.tsv: address <TAB> name [<TAB> note [<TAB> type]]
//   name   "Class::Method" puts a function or label into that class namespace (created as a class
//          if needed); plain names stay global. Lines starting with # are comments.
//   note   plate comment (functions, globals) or pre comment (labels inside a function).
//   type   a function prototype "ret [callconv] Name(type a, type b, ...)" (the name in it is
//          ignored; __thiscall adds `this` itself), or a C type for a global (e.g. "World").
// An address inside no function in an executable block becomes a function; an address inside a
// function body (not its entry) stays a label.
//@category WarBreeds

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import generic.jar.ResourceFile;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class ApplyNames extends GhidraScript {

    private static final CategoryPath CAT = new CategoryPath("/WarBreeds");
    private static final CategoryPath VT_CAT = new CategoryPath("/WarBreeds/vtables");
    private DataTypeManager dtm;
    private FileDataTypeManager winArchive;
    private final Map<String, Structure> structs = new HashMap<>();
    private int warnings = 0;

    /** One vftable of a class (a class with multiple inheritance has several). */
    private static class VTable {
        long addr, col;
        int thisOffset;
        List<Long> slots = new ArrayList<>();
    }

    /** One RTTI class. */
    private static class RClass {
        String name;            // Ghidra namespace path, e.g. "AIMovementGuide::tGuide"
        GhidraClass ns;
        List<String> bases = new ArrayList<>(); // all bases, CHD order (primary base first)
        List<VTable> vtables = new ArrayList<>();
    }

    private final Map<String, RClass> classes = new LinkedHashMap<>();
    private final Map<String, Map<Integer, String[]>> vtNames = new HashMap<>();

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File namesFile, typesFile = null;
        if (args.length >= 1) {
            namesFile = new File(args[0]);
            if (args.length > 1) typesFile = new File(args[1]);
        } else if (!isRunningHeadless()) {
            namesFile = askFile("names.tsv", "Apply");
            typesFile = askFile("types.tsv", "Apply");
        } else {
            printerr("usage: ApplyNames.java <names.tsv> [types.tsv]");
            return;
        }
        dtm = currentProgram.getDataTypeManager();
        List<String[]> names = readTsv(namesFile);
        List<String[]> types = typesFile != null ? readTsv(typesFile) : new ArrayList<>();

        // ---- 1. RTTI
        recoverRtti();

        // ---- 2. structs
        for (String[] p : names) {
            if (p.length >= 2 && p[1].contains("::")) classNamespace(p[1].substring(0, p[1].lastIndexOf("::")));
        }
        Map<String, List<String[]>> byStruct = new LinkedHashMap<>();
        for (RClass c : classes.values()) byStruct.put(c.name, new ArrayList<>());
        for (String[] p : types) {
            if (p.length < 3) continue;
            if (p[0].trim().endsWith("_vftable")) { // slot names for a generated vtable struct
                if (p.length > 3) vtNames.computeIfAbsent(p[0].trim(), k -> new HashMap<>()).put((int) parseNum(p[1]), p);
                continue;
            }
            byStruct.computeIfAbsent(p[0].trim(), k -> new ArrayList<>()).add(p);
        }
        for (String s : byStruct.keySet()) structs.put(s, structFor(s));
        int fields = 0;
        Set<String> done = new HashSet<>();
        for (String s : byStruct.keySet()) fields += buildStruct(s, byStruct, done, new HashSet<>());
        println("structs: " + structs.size() + ", fields applied: " + fields);

        // ---- 3. names
        int fn = 0, lab = 0, typed = 0;
        for (String[] p : names) {
            if (p.length < 2 || p[1].isBlank()) continue;
            try {
                Address a = toAddr(parseNum(p[0]));
                if (!currentProgram.getMemory().contains(a)) { warn(p[0] + " not in memory"); continue; }
                String full = p[1].trim();
                String note = p.length > 2 ? p[2].trim() : "";
                String type = p.length > 3 ? p[3].trim() : "";
                Namespace ns = currentProgram.getGlobalNamespace();
                String simple = full;
                if (full.contains("::")) {
                    ns = classNamespace(full.substring(0, full.lastIndexOf("::")));
                    simple = full.substring(full.lastIndexOf("::") + 2);
                }
                Function f = functionFor(a);
                if (f != null && !f.getEntryPoint().equals(a)) f = null;
                if (f != null) {
                    if (f.getParentNamespace() != ns) f.setParentNamespace(ns);
                    if (ns instanceof GhidraClass && !type.contains("(")) toThiscall(f, false);
                    if (!f.getName().equals(simple)) f.setName(simple, SourceType.USER_DEFINED);
                    else if (f.getSymbol().getSource() != SourceType.USER_DEFINED) f.getSymbol().setName(simple, SourceType.USER_DEFINED);
                    if (!note.isEmpty()) setPlateComment(a, note);
                    if (type.contains("(") && applyPrototype(f, type)) typed++;
                    fn++;
                } else {
                    label(a, simple, ns);
                    boolean inFunc = getFunctionContaining(a) != null;
                    if (!note.isEmpty()) {
                        if (inFunc) setPreComment(a, note); else setPlateComment(a, note);
                    }
                    if (!type.isEmpty() && !type.contains("(") && !inFunc) {
                        DataType dt = parseType(type);
                        if (dt == null) warn(p[0] + ": unknown type '" + type + "'");
                        else {
                            DataUtilities.createData(currentProgram, a, dt, -1, false,
                                    DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
                            typed++;
                        }
                    }
                    lab++;
                }
            } catch (Exception ex) {
                warn(String.join("|", p) + ": " + ex.getMessage());
            }
        }
        println("names applied: " + fn + " functions, " + lab + " labels, " + typed + " typed");

        // ---- 4. vtable structs
        println("vtable structs: " + buildVtableStructs());

        if (winArchive != null) winArchive.close();
        println("warnings: " + warnings);
    }

    // =========================================================================================
    // 1. RTTI
    // =========================================================================================

    private void recoverRtti() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock text = mem.getBlock(".text");
        Map<Long, String> tds = new HashMap<>();   // type descriptor -> class path
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            for (int i = 8; i + 4 < buf.length; i++) {
                if (buf[i] == '.' && buf[i + 1] == '?' && buf[i + 2] == 'A' && (buf[i + 3] == 'V' || buf[i + 3] == 'U')) {
                    int j = i;
                    while (j < buf.length && buf[j] != 0) j++;
                    long td = b.getStart().getOffset() + i - 8;
                    tds.put(td, tdClassName(td, new String(buf, i, j - i, StandardCharsets.US_ASCII)));
                }
            }
        }
        Map<Long, Long> colTd = new HashMap<>();
        Map<Long, long[]> colInfo = new HashMap<>(); // offset, chd
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address s = b.getStart();
            for (long o = 12; o + 8 <= b.getSize(); o += 4) {
                long v = u32(s.add(o));
                if (!tds.containsKey(v) || u32(s.add(o - 12)) != 0) continue;
                long chd = u32(s.add(o + 4));
                if (!mem.contains(toAddr(chd))) continue;
                long col = s.getOffset() + o - 12;
                colTd.put(col, v);
                colInfo.put(col, new long[] {u32(s.add(o - 8)), chd});
            }
        }
        TreeMap<Long, Long> vtCol = new TreeMap<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address s = b.getStart();
            for (long o = 0; o + 8 <= b.getSize(); o += 4) {
                long v = u32(s.add(o));
                if (colTd.containsKey(v)) vtCol.put(s.getOffset() + o + 4, v);
            }
        }
        for (Map.Entry<Long, Long> e : vtCol.entrySet()) {
            long vt = e.getKey(), col = e.getValue();
            String name = tds.get(colTd.get(col));
            RClass c = classes.get(name);
            if (c == null) {
                c = new RClass();
                c.name = name;
                c.ns = (GhidraClass) classNamespace(name);
                long chd = colInfo.get(col)[1];
                long nb = u32(toAddr(chd + 8)), bca = u32(toAddr(chd + 12));
                for (int i = 1; i < nb && i < 64; i++) {
                    long bcd = u32(toAddr(bca + 4L * i));
                    String bn = tds.get(u32(toAddr(bcd)));
                    if (bn != null) c.bases.add(bn);
                }
                classes.put(name, c);
            }
            VTable v = new VTable();
            v.addr = vt;
            v.col = col;
            v.thisOffset = (int) colInfo.get(col)[0];
            for (long a = vt;; a += 4) {
                if (a != vt && vtCol.containsKey(a + 4)) break; // the next vftable's COL pointer
                long f = u32(toAddr(a));
                if (!text.contains(toAddr(f))) break;
                v.slots.add(f);
            }
            c.vtables.add(v);
            label(toAddr(vt), v.thisOffset == 0 ? "vftable" : "vftable_" + v.thisOffset, c.ns, SourceType.ANALYSIS);
            label(toAddr(col), "RTTI_Complete_Object_Locator" + (v.thisOffset == 0 ? "" : "_" + v.thisOffset), c.ns, SourceType.ANALYSIS);
        }

        // Forget the names this phase gave on earlier runs, so a better heuristic can rename them.
        int reset = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getSymbol().getSource() != SourceType.ANALYSIS || f.getParentNamespace().isGlobal()) continue;
            if (!isDefaultName(f)) continue;
            f.setParentNamespace(currentProgram.getGlobalNamespace());
            f.setName(SymbolUtilities.getDefaultFunctionName(f.getEntryPoint()), SourceType.DEFAULT);
            reset++;
        }
        if (reset > 0) println("RTTI: reset " + reset + " generated names");
        // Virtual function ownership: the most-base class among the vtables holding the function.
        Map<Long, Set<String>> holders = new HashMap<>();
        Map<Long, Integer> slotOf = new HashMap<>();
        for (RClass c : classes.values()) {
            for (VTable v : c.vtables) {
                for (int i = 0; i < v.slots.size(); i++) {
                    holders.computeIfAbsent(v.slots.get(i), k -> new HashSet<>()).add(c.name);
                    slotOf.putIfAbsent(v.slots.get(i), i * 4);
                }
            }
        }
        int vf = 0, shared = 0;
        for (Map.Entry<Long, Set<String>> e : holders.entrySet()) {
            String owner = mostBase(e.getValue());
            Function f = functionFor(toAddr(e.getKey()));
            if (f == null || f.isThunk()) continue;
            if (owner == null) { shared++; continue; }
            if (isDefaultName(f)) {
                f.setParentNamespace(classes.get(owner).ns);
                f.setName(String.format("vf%02x", slotOf.get(e.getKey())), SourceType.ANALYSIS);
            }
            makeThiscall(f);
            vf++;
        }
        // Constructors and destructors: functions that store a vtable.
        int ctors = 0;
        Map<Function, Set<String>> writers = new HashMap<>();
        Set<Long> slotFns = holders.keySet();
        for (RClass c : classes.values()) {
            for (VTable v : c.vtables) {
                for (Reference r : getReferencesTo(toAddr(v.addr))) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f == null || !r.getReferenceType().isData()) continue;
                    Instruction ins = getInstructionAt(r.getFromAddress());
                    if (ins == null || !ins.getMnemonicString().startsWith("MOV") || !storesToThis(ins)) continue;
                    writers.computeIfAbsent(f, k -> new HashSet<>()).add(c.name);
                }
            }
        }
        for (Map.Entry<Function, Set<String>> e : writers.entrySet()) {
            Function f = e.getKey();
            String cls = mostDerived(e.getValue());
            if (cls == null || !isDefaultName(f)) continue;
            RClass c = classes.get(cls);
            String simple = c.ns.getName();
            boolean dtor = slotFns.contains(f.getEntryPoint().getOffset());
            if (!dtor) {
                for (Function caller : f.getCallingFunctions(monitor)) {
                    if (slotFns.contains(caller.getEntryPoint().getOffset())) { dtor = true; break; }
                }
            }
            f.setParentNamespace(c.ns);
            f.setName(dtor ? "~" + simple : simple, SourceType.ANALYSIS);
            makeThiscall(f);
            ctors++;
        }
        println("RTTI: " + classes.size() + " classes, " + vtCol.size() + " vftables, " + vf
                + " virtual functions placed (" + shared + " shared by unrelated classes), " + ctors + " ctors/dtors");
    }

    private String tdClassName(long td, String mangled) {
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(toAddr(td))) {
            Namespace p = s.getParentNamespace();
            if (!p.isGlobal()) return p.getName(true);
        }
        String body = mangled.substring(4, mangled.length() - 2);
        String[] parts = body.split("@");
        StringBuilder sb = new StringBuilder();
        for (int i = parts.length - 1; i >= 0; i--) {
            if (parts[i].isEmpty()) continue;
            if (sb.length() > 0) sb.append("::");
            sb.append(parts[i].replaceAll("[^A-Za-z0-9_]", "_"));
        }
        return sb.toString();
    }

    private boolean derives(String cls, String base) {
        RClass c = classes.get(cls);
        return c != null && c.bases.contains(base);
    }

    private String mostBase(Set<String> set) {
        for (String c : set) {
            boolean ok = true;
            for (String d : set) if (!d.equals(c) && !derives(d, c)) { ok = false; break; }
            if (ok) return c;
        }
        return null;
    }

    private String mostDerived(Set<String> set) {
        for (String c : set) {
            boolean ok = true;
            for (String d : set) if (!d.equals(c) && !derives(c, d)) { ok = false; break; }
            if (ok) return c;
        }
        return null;
    }

    private boolean isDefaultName(Function f) {
        SourceType s = f.getSymbol().getSource();
        return s == SourceType.DEFAULT || (s == SourceType.ANALYSIS && (f.getName().startsWith("vf") || f.getName().startsWith("FUN_")
                || f.getName().equals(f.getParentNamespace().getName()) || f.getName().startsWith("~")));
    }

    // "mov dword ptr [reg], vftable" with no displacement and reg not ESP/EBP: the object at this.
    // (Stack locals and embedded members are constructed in place by other functions.)
    private boolean storesToThis(Instruction ins) {
        Object[] objs = ins.getOpObjects(0);
        if (objs.length != 1 || !(objs[0] instanceof ghidra.program.model.lang.Register)) return false;
        if ((ins.getOperandType(0) & ghidra.program.model.lang.OperandType.DYNAMIC) == 0) return false;
        String r = ((ghidra.program.model.lang.Register) objs[0]).getName();
        if (r.equals("ESP") || r.equals("EBP")) return false;
        return holdsEntryEcx(ins, r);
    }

    // Does register r still hold the ECX value the function was entered with? A linear scan from
    // the entry: tracks "mov reg, reg" copies, drops registers that are overwritten or clobbered by
    // calls. Separates constructors (vftable stored into this) from functions that fill in a
    // returned or heap object of the class.
    private boolean holdsEntryEcx(Instruction store, String r) {
        Function f = getFunctionContaining(store.getAddress());
        if (f == null) return false;
        Set<String> held = new HashSet<>(Set.of("ECX"));
        Instruction ins = getInstructionAt(f.getEntryPoint());
        int guard = 0;
        while (ins != null && ins.getAddress().compareTo(store.getAddress()) < 0 && guard++ < 400) {
            String m = ins.getMnemonicString();
            if (m.equals("CALL")) {
                held.remove("EAX"); held.remove("ECX"); held.remove("EDX");
            } else if (ins.getNumOperands() >= 1) {
                Object[] dst = ins.getOpObjects(0);
                boolean regDst = dst.length == 1 && dst[0] instanceof ghidra.program.model.lang.Register
                        && (ins.getOperandType(0) & ghidra.program.model.lang.OperandType.DYNAMIC) == 0;
                if (regDst && !m.equals("PUSH") && !m.equals("CMP") && !m.equals("TEST")) {
                    String d = ((ghidra.program.model.lang.Register) dst[0]).getBaseRegister().getName();
                    boolean copy = false;
                    if (m.equals("MOV") && ins.getNumOperands() == 2) {
                        Object[] src = ins.getOpObjects(1);
                        if (src.length == 1 && src[0] instanceof ghidra.program.model.lang.Register
                                && (ins.getOperandType(1) & ghidra.program.model.lang.OperandType.DYNAMIC) == 0) {
                            copy = held.contains(((ghidra.program.model.lang.Register) src[0]).getBaseRegister().getName());
                        }
                    }
                    if (copy) held.add(d); else held.remove(d);
                }
            }
            ins = ins.getNext();
        }
        return held.contains(r);
    }

    // Methods are __thiscall in MSVC. Ghidra often guesses __fastcall for a method without stack
    // arguments (ECX in use): convert that, dropping the ECX parameter, which becomes `this`.
    // virtualOrCtor: also convert __stdcall/__cdecl/unknown (a vtable slot is always a method).
    private void makeThiscall(Function f) {
        toThiscall(f, true);
    }

    private void toThiscall(Function f, boolean virtualOrCtor) {
        try {
            String cc = f.getCallingConventionName();
            if (f.hasVarArgs() || "__thiscall".equals(cc)) return;
            if (f.getSignatureSource() == SourceType.USER_DEFINED) return;
            if ("__fastcall".equals(cc)) {
                List<Variable> keep = new ArrayList<>();
                for (Parameter p : f.getParameters()) {
                    if (p.isRegisterVariable()) {
                        if (p.getRegister().getName().equals("EDX")) return; // a real fastcall
                        continue;
                    }
                    keep.add(new ParameterImpl(p.getName(), p.getDataType(), currentProgram));
                }
                f.updateFunction("__thiscall", f.getReturn(), keep, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                        true, f.getSignatureSource());
            } else if (virtualOrCtor) {
                f.setCallingConvention("__thiscall");
            }
        } catch (Exception ex) {
            warn(f.getName(true) + ": thiscall: " + ex.getMessage());
        }
    }

    // =========================================================================================
    // 2. structs
    // =========================================================================================

    private int buildStruct(String name, Map<String, List<String[]>> defs, Set<String> done, Set<String> visiting) {
        if (done.contains(name) || visiting.contains(name)) return 0;
        visiting.add(name);
        Structure st = structs.get(name);
        List<String[]> lines = defs.getOrDefault(name, new ArrayList<>());
        String base = null;
        RClass rc = classes.get(name);
        if (rc != null && !rc.bases.isEmpty()) base = rc.bases.get(0);
        for (String[] p : lines) if (p[1].trim().equalsIgnoreCase("base")) base = p[2].trim();
        int n = 0;
        if (base != null && structs.containsKey(base)) n += buildStruct(base, defs, done, visiting);
        st.deleteAll();
        // vftable pointers
        if (rc != null) {
            for (VTable v : rc.vtables) {
                Structure vs = vtableStruct(rc, v);
                setField(st, v.thisOffset, new PointerDataType(vs, 4, dtm), v.thisOffset == 0 ? "vftable" : "vftable_" + v.thisOffset, null);
            }
        }
        // flattened base fields
        if (base != null && structs.containsKey(base)) {
            Structure bs = structs.get(base);
            if (slen(st) < slen(bs)) st.growStructure(slen(bs) - slen(st));
            for (DataTypeComponent c : bs.getDefinedComponents()) {
                if (c.getFieldName() != null && c.getFieldName().startsWith("vftable")) continue;
                setField(st, c.getOffset(), c.getDataType(), c.getFieldName() == null ? "" : c.getFieldName(), c.getComment());
            }
        }
        for (String[] p : lines) {
            String key = p[1].trim();
            try {
                if (key.equalsIgnoreCase("size")) {
                    int size = (int) parseNum(p[2]);
                    if (slen(st) < size) st.growStructure(size - slen(st));
                    else if (slen(st) > size) warn(name + ": fields reach 0x" + Integer.toHexString(st.getLength()) + " beyond size 0x" + Integer.toHexString(size));
                } else if (key.equalsIgnoreCase("desc")) {
                    st.setDescription(p.length > 4 ? p[4] : "");
                } else if (key.equalsIgnoreCase("base")) {
                    // handled above
                } else {
                    if (p.length < 4) continue;
                    int off = (int) parseNum(key);
                    if (rc != null && rc.vtables.stream().anyMatch(v -> v.thisOffset == off)) continue; // vftable pointer
                    DataType dt = parseType(p[2]);
                    if (dt == null) { warn(name + "+" + key + ": unknown type '" + p[2] + "'"); continue; }
                    setField(st, off, dt, p[3].trim(), p.length > 4 ? p[4] : null);
                    n++;
                }
            } catch (Exception ex) {
                warn(name + " " + String.join("|", p) + ": " + ex.getMessage());
            }
        }
        if (rc != null && st.getDescription().isEmpty()) {
            st.setDescription("RTTI class" + (rc.bases.isEmpty() ? "" : "; bases: " + String.join(", ", rc.bases)));
        }
        visiting.remove(name);
        done.add(name);
        return n;
    }

    private Structure structFor(String name) throws Exception {
        Namespace ns = null;
        try {
            for (Namespace n : NamespaceUtils.getNamespaceByPath(currentProgram, null, name)) if (n instanceof GhidraClass) ns = n;
        } catch (Exception ignored) {
        }
        if (ns != null) {
            Structure s = VariableUtilities.findOrCreateClassStruct((GhidraClass) ns, dtm);
            return (Structure) dtm.resolve(s, DataTypeConflictHandler.KEEP_HANDLER);
        }
        String simple = name.replace("::", "_");
        DataType existing = dtm.getDataType(CAT, simple);
        if (existing instanceof Structure) return (Structure) existing;
        Structure s = new StructureDataType(CAT, simple, 0, dtm);
        return (Structure) dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
    }

    private void setField(Structure st, int off, DataType dt, String name, String comment) {
        try {
            setField0(st, off, dt, name, comment);
        } catch (Exception ex) {
            warn(st.getName() + "+0x" + Integer.toHexString(off) + " " + name + ": " + ex.getMessage());
        }
    }

    private void setField0(Structure st, int off, DataType dt, String name, String comment) {
        int len = dt.getLength();
        if (len <= 0) { warn(st.getName() + "+0x" + Integer.toHexString(off) + ": zero-length type " + dt.getName()); return; }
        if (off + len > slen(st)) st.growStructure(off + len - slen(st));
        for (int o = off; o < off + len; o++) {
            for (DataTypeComponent c : st.getComponentsContaining(o)) {
                if (c.getDataType() != DataType.DEFAULT) st.clearComponent(c.getOrdinal());
            }
        }
        st.replaceAtOffset(off, dt, len, name.isEmpty() ? null : name, comment == null || comment.isBlank() ? null : comment);
    }

    // =========================================================================================
    // 4. vtable structs
    // =========================================================================================

    // f lives in c's namespace or in one of c's base classes
    private boolean isAncestorNs(RClass c, Function f) {
        String ns = f.getParentNamespace().getName(true);
        return ns.equals(c.name) || c.bases.contains(ns);
    }

    private Structure vtableStruct(RClass c, VTable v) {
        String n = c.name.replace("::", "_").replaceAll("[^A-Za-z0-9_]", "_") + "_vftable" + (v.thisOffset == 0 ? "" : "_" + v.thisOffset);
        DataType existing = dtm.getDataType(VT_CAT, n);
        if (existing instanceof Structure) return (Structure) existing;
        Structure s = new StructureDataType(VT_CAT, n, 0, dtm);
        return (Structure) dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
    }

    // Field names: Class_vftable lines in types.tsv win; then the slot function's own name; a slot
    // whose function has no real name (vfXX, FUN_, _purecall, a shared stub) takes the primary
    // base class's field name for that slot.
    private int buildVtableStructs() {
        int n = 0;
        List<RClass> order = new ArrayList<>(classes.values());
        order.sort(Comparator.comparingInt(c -> c.bases.size()));
        for (RClass c : order) {
            for (VTable v : c.vtables) {
                try {
                    Structure s = vtableStruct(c, v);
                    s.deleteAll();
                    Map<Integer, String[]> over = vtNames.getOrDefault(s.getName(), Map.of());
                    Structure baseVt = null;
                    if (v.thisOffset == 0 && !c.bases.isEmpty() && classes.containsKey(c.bases.get(0))) {
                        RClass b = classes.get(c.bases.get(0));
                        for (VTable bv : b.vtables) if (bv.thisOffset == 0) baseVt = vtableStruct(b, bv);
                    }
                    Set<String> used = new HashSet<>();
                    for (int i = 0; i < v.slots.size(); i++) {
                        Function f = getFunctionAt(toAddr(v.slots.get(i)));
                        String fname = f == null ? String.format("vf%02x", i * 4) : f.getName().replaceAll("[^A-Za-z0-9_]", "_");
                        boolean weak = f == null || fname.matches("vf[0-9a-f]+") || fname.startsWith("FUN_") || fname.contains("purecall")
                                || !f.getParentNamespace().getName(true).equals(c.name) && !isAncestorNs(c, f);
                        if (over.containsKey(i * 4)) fname = over.get(i * 4)[3].trim();
                        else if (weak && baseVt != null && !baseVt.isZeroLength() && i * 4 < baseVt.getLength()) {
                            DataTypeComponent bc = baseVt.getComponentAt(i * 4);
                            if (bc != null && bc.getFieldName() != null && !bc.getFieldName().matches("vf[0-9a-f]+(_[0-9a-f]+)?")) {
                                fname = bc.getFieldName().replaceAll("_[0-9a-f]{2}$", "");
                            }
                        }
                        if (!used.add(fname)) fname = fname + String.format("_%02x", i * 4);
                        DataType dt = PointerDataType.dataType;
                        if (f != null) {
                            String fdName = f.getName(true).replace("::", "__").replaceAll("[^A-Za-z0-9_]", "_");
                            FunctionDefinitionDataType fd = new FunctionDefinitionDataType(VT_CAT.extend("functions"), fdName, f.getSignature(true), dtm);
                            DataType r = dtm.resolve(fd, DataTypeConflictHandler.REPLACE_HANDLER);
                            dt = new PointerDataType(r, 4, dtm);
                        }
                        s.add(dt, 4, fname, f == null ? null : f.getName(true) + " @ " + f.getEntryPoint());
                    }
                    DataUtilities.createData(currentProgram, toAddr(v.addr), s, -1, false,
                            DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
                    n++;
                } catch (Exception ex) {
                    warn(c.name + " vtable " + Long.toHexString(v.addr) + ": " + ex.getMessage());
                }
            }
        }
        return n;
    }

    // =========================================================================================
    // 3. names
    // =========================================================================================

    private Function functionFor(Address a) throws Exception {
        Function f = getFunctionAt(a);
        if (f != null) return f;
        if (getFunctionContaining(a) != null) return null; // a label inside a function
        MemoryBlock b = currentProgram.getMemory().getBlock(a);
        if (b == null || !b.isExecute()) return null;
        if (getInstructionAt(a) == null) {
            Data d = getDataAt(a);
            if (d != null && d.isDefined()) return null;
            disassemble(a);
        }
        if (getInstructionAt(a) == null) return null;
        return createFunction(a, null);
    }

    private Symbol label(Address a, String name, Namespace ns) throws Exception {
        return label(a, name, ns, SourceType.USER_DEFINED);
    }

    private Symbol label(Address a, String name, Namespace ns, SourceType src) throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        Symbol keep = null;
        for (Symbol s : st.getSymbols(a)) {
            if (s.getSymbolType() != SymbolType.LABEL) continue;
            if (s.getName().equals(name) && s.getParentNamespace().equals(ns)) keep = s;
            else if (src == SourceType.USER_DEFINED && s.getSource() == SourceType.USER_DEFINED) s.delete(); // names.tsv owns it
        }
        if (keep == null) keep = st.createLabel(a, name, ns, src);
        if (src == SourceType.USER_DEFINED && !keep.isPrimary()) keep.setPrimary();
        return keep;
    }

    private Namespace classNamespace(String path) throws Exception {
        Namespace ns = currentProgram.getGlobalNamespace();
        for (String part : splitPath(path)) {
            Namespace next = currentProgram.getSymbolTable().getNamespace(part, ns);
            if (next == null) next = currentProgram.getSymbolTable().createClass(ns, part, SourceType.USER_DEFINED);
            ns = next;
        }
        if (!(ns instanceof GhidraClass)) ns = NamespaceUtils.convertNamespaceToClass(ns);
        return ns;
    }

    // split "A::B<C::D>::E" on top-level "::" only
    private List<String> splitPath(String path) {
        List<String> out = new ArrayList<>();
        int depth = 0, start = 0;
        for (int i = 0; i < path.length(); i++) {
            char c = path.charAt(i);
            if (c == '<') depth++;
            else if (c == '>') depth--;
            else if (c == ':' && depth == 0 && i + 1 < path.length() && path.charAt(i + 1) == ':') {
                out.add(path.substring(start, i));
                start = i + 2;
                i++;
            }
        }
        out.add(path.substring(start));
        return out;
    }

    private boolean applyPrototype(Function f, String proto) {
        try {
            int open = proto.indexOf('('), close = proto.lastIndexOf(')');
            String head = proto.substring(0, open).trim();
            String plist = proto.substring(open + 1, close).trim();
            int sp = Math.max(head.lastIndexOf(' '), head.lastIndexOf('*'));
            String retAndCc = head.substring(0, sp + 1).trim();
            String cc = null;
            for (String c : new String[] {"__thiscall", "__stdcall", "__cdecl", "__fastcall"}) {
                if (retAndCc.contains(c)) { cc = c; retAndCc = retAndCc.replace(c, " ").trim(); }
            }
            DataType ret = parseType(retAndCc);
            if (ret == null) { warn(f.getName() + ": unknown return type '" + retAndCc + "'"); return false; }
            List<Variable> params = new ArrayList<>();
            boolean varargs = false;
            if (!plist.isEmpty() && !plist.equals("void")) {
                int i = 0;
                for (String raw : splitParams(plist)) {
                    String q = raw.trim();
                    if (q.equals("...")) { varargs = true; continue; }
                    String pname = "param_" + (++i);
                    String ptype = q;
                    Matcher m = Pattern.compile("^(.*?[\\s\\*])([A-Za-z_][A-Za-z0-9_]*)(\\s*(\\[[^\\]]*\\])*)$").matcher(q);
                    if (m.matches() && !isTypeWord(m.group(2))) {
                        pname = m.group(2);
                        ptype = m.group(1).trim() + m.group(3).trim();
                    }
                    DataType dt = parseType(ptype);
                    if (dt == null) { warn(f.getName() + ": unknown param type '" + ptype + "'"); return false; }
                    params.add(new ParameterImpl(pname, dt, currentProgram));
                }
            }
            ReturnParameterImpl rv = new ReturnParameterImpl(ret, currentProgram);
            f.updateFunction(cc != null ? cc : f.getCallingConventionName(), rv, params,
                    Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
            f.setVarArgs(varargs);
            return true;
        } catch (Exception ex) {
            warn(f.getName() + ": prototype '" + proto + "': " + ex.getMessage());
            return false;
        }
    }

    private List<String> splitParams(String s) {
        List<String> out = new ArrayList<>();
        int depth = 0, start = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '(' || c == '[' || c == '<') depth++;
            else if (c == ')' || c == ']' || c == '>') depth--;
            else if (c == ',' && depth == 0) { out.add(s.substring(start, i)); start = i + 1; }
        }
        out.add(s.substring(start));
        return out;
    }

    private boolean isTypeWord(String w) {
        return Set.of("int", "short", "char", "long", "unsigned", "signed", "void", "float", "double", "const", "byte",
                "word", "dword", "bool").contains(w);
    }

    // =========================================================================================
    // types
    // =========================================================================================

    private DataType parseType(String spec) {
        String s = spec.trim().replaceAll("\\bconst\\b", "").replaceAll("\\bstruct\\b", "").replaceAll("\\bclass\\b", "").trim();
        List<Integer> dims = new ArrayList<>();
        Pattern arr = Pattern.compile("\\[\\s*(0x[0-9a-fA-F]+|\\d+)\\s*\\]\\s*$");
        Matcher m = arr.matcher(s);
        while (m.find()) {
            dims.add(0, (int) parseNum(m.group(1)));
            s = s.substring(0, m.start()).trim();
            m = arr.matcher(s);
        }
        int ptrs = 0;
        while (s.endsWith("*")) { ptrs++; s = s.substring(0, s.length() - 1).trim(); }
        DataType dt = baseType(s.replaceAll("\\s+", " "));
        if (dt == null) {
            if (ptrs == 0) return null;
            warn("unknown pointee '" + s + "', using void *");
            dt = VoidDataType.dataType;
        }
        for (int i = 0; i < ptrs; i++) dt = new PointerDataType(dt, 4, dtm);
        for (int i = dims.size() - 1; i >= 0; i--) dt = new ArrayDataType(dt, dims.get(i), dt.getLength(), dtm);
        return dt;
    }

    private DataType baseType(String s) {
        switch (s) {
            case "void": return VoidDataType.dataType;
            case "char": case "signed char": case "int8_t": return CharDataType.dataType;
            case "unsigned char": case "uchar": case "uint8_t": return UnsignedCharDataType.dataType;
            case "byte": case "BYTE": return ByteDataType.dataType;
            case "short": case "signed short": case "short int": case "int16_t": return ShortDataType.dataType;
            case "unsigned short": case "ushort": case "uint16_t": return UnsignedShortDataType.dataType;
            case "word": case "WORD": return WordDataType.dataType;
            case "int": case "signed int": case "signed": case "int32_t": return IntegerDataType.dataType;
            case "unsigned int": case "unsigned": case "uint": case "uint32_t": return UnsignedIntegerDataType.dataType;
            case "long": case "signed long": case "long int": return LongDataType.dataType;
            case "unsigned long": case "ulong": return UnsignedLongDataType.dataType;
            case "dword": case "DWORD": return DWordDataType.dataType;
            case "float": return FloatDataType.dataType;
            case "double": return DoubleDataType.dataType;
            case "bool": return BooleanDataType.dataType;
            case "pointer": return PointerDataType.dataType;
            case "undefined": case "undefined1": return Undefined1DataType.dataType;
            case "undefined2": return Undefined2DataType.dataType;
            case "undefined4": return Undefined4DataType.dataType;
        }
        if (structs.containsKey(s)) return structs.get(s);
        try {
            for (Namespace n : NamespaceUtils.getNamespaceByPath(currentProgram, null, s)) {
                if (n instanceof GhidraClass) return VariableUtilities.findOrCreateClassStruct((GhidraClass) n, dtm);
            }
        } catch (Exception ignored) {
        }
        DataType d = findByName(dtm, s.replace("::", "_"));
        if (d != null) return d;
        try {
            if (winArchive == null) {
                ResourceFile rf = Application.findDataFileInAnyModule("typeinfo/win32/windows_vs12_32.gdt");
                if (rf != null) winArchive = FileDataTypeManager.openFileArchive(rf, false);
            }
            if (winArchive != null) {
                d = findByName(winArchive, s);
                if (d != null) return dtm.resolve(d, DataTypeConflictHandler.DEFAULT_HANDLER);
            }
        } catch (Exception ex) {
            warn("windows archive: " + ex.getMessage());
        }
        return null;
    }

    private DataType findByName(DataTypeManager m, String name) {
        List<DataType> found = new ArrayList<>();
        m.findDataTypes(name, found);
        DataType best = null;
        for (DataType d : found) {
            if (d instanceof Pointer || d instanceof Array) continue;
            if (best == null || d.getCategoryPath().equals(CAT)) best = d;
        }
        return best;
    }

    // =========================================================================================
    // helpers
    // =========================================================================================

    private List<String[]> readTsv(File f) throws Exception {
        List<String[]> out = new ArrayList<>();
        if (!f.exists()) { warn("not found: " + f); return out; }
        for (String line : Files.readAllLines(f.toPath(), StandardCharsets.UTF_8)) {
            if (line.isBlank() || line.startsWith("#")) continue;
            out.add(line.split("\t", -1));
        }
        return out;
    }

    private long parseNum(String s) {
        s = s.trim();
        if (s.startsWith("0x") || s.startsWith("0X")) return Long.parseLong(s.substring(2), 16);
        if (s.matches("[0-9a-fA-F]{6,8}")) return Long.parseLong(s, 16);
        return Long.parseLong(s);
    }

    private static int slen(Structure s) {
        return s.isZeroLength() ? 0 : s.getLength();
    }

    private long u32(Address a) throws Exception {
        return currentProgram.getMemory().getInt(a) & 0xffffffffL;
    }

    private void warn(String msg) {
        warnings++;
        println("WARN " + msg);
    }
}
