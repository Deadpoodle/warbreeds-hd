// Apply the project's reverse-engineering knowledge to a Ghidra program. Re-runnable: run it
// again whenever re/names.tsv or re/types.tsv grows, then re-export.
//
//   analyzeHeadless <projDir> <projName> -process <program> -noanalysis
//       -scriptPath <this folder> -postScript ApplyNames.java <names.tsv> [types.tsv]
//
// types.tsv (applied first): struct definitions, one field per line
//   Struct <TAB> 0xOFFSET <TAB> ctype <TAB> field [<TAB> note]
//   Struct <TAB> size     <TAB> 0xSIZE                 (minimum size of the struct)
//   Struct <TAB> desc     <TAB> <TAB> <TAB> description (struct comment)
// A struct whose name is a class namespace (RTTI-recovered, or any Class:: used in names.tsv) is
// that class's own structure, so every __thiscall method of the class sees `this` as Class *.
// Other structs live in the /WarBreeds category. Fields replace whatever overlaps them; the
// struct grows as needed. ctype: C basic types (char, short, int, long, unsigned ..., float,
// double, byte, word, dword, bool, void), struct/class names, Windows SDK types (RECT, HWND, ...)
// with any number of '*' and [N] suffixes, e.g. "Scene *[16]", "short[4]", "APlayer *".
//
// names.tsv: address <TAB> name [<TAB> note [<TAB> type]]
//   name   "Class::Method" puts a function or label into that class namespace (created as a
//          class if needed); plain names stay global. Lines starting with # are comments.
//   note   plate comment (functions, globals) or pre comment (labels inside a function).
//   type   a function prototype "ret [callconv] Name(type a, type b, ...)" (the name in it is
//          ignored; __thiscall adds `this` itself), or a C type for a global (e.g. "World").
// An address inside no function in an executable block becomes a function; an address inside a
// function body (not its entry) stays a label.
//@category WarBreeds

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import generic.jar.ResourceFile;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class ApplyNames extends GhidraScript {

    private static final CategoryPath CAT = new CategoryPath("/WarBreeds");
    private DataTypeManager dtm;
    private FileDataTypeManager winArchive;
    private final Map<String, Structure> structs = new HashMap<>();
    private int warnings = 0;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: ApplyNames.java <names.tsv> [types.tsv]");
            return;
        }
        dtm = currentProgram.getDataTypeManager();
        List<String[]> names = readTsv(new File(args[0]));
        List<String[]> types = args.length > 1 ? readTsv(new File(args[1])) : new ArrayList<>();

        // 1. Namespaces for every Class:: prefix, so class structs bind to them.
        for (String[] p : names) {
            if (p.length >= 2 && p[1].contains("::")) classNamespace(p[1].substring(0, p[1].lastIndexOf("::")));
        }
        // 2. Structs: create all first (so fields may point at each other), then fill them.
        Map<String, List<String[]>> byStruct = new LinkedHashMap<>();
        for (String[] p : types) {
            if (p.length < 3) continue;
            byStruct.computeIfAbsent(p[0].trim(), k -> new ArrayList<>()).add(p);
        }
        for (String s : byStruct.keySet()) structs.put(s, structFor(s));
        int fields = 0;
        for (Map.Entry<String, List<String[]>> e : byStruct.entrySet()) {
            Structure st = structs.get(e.getKey());
            for (String[] p : e.getValue()) {
                String key = p[1].trim();
                try {
                    if (key.equalsIgnoreCase("size")) {
                        int size = (int) parseNum(p[2]);
                        if (st.getLength() < size) st.growStructure(size - st.getLength());
                        else if (st.getLength() > size && !st.isZeroLength())
                            warn(e.getKey() + ": fields reach 0x" + Integer.toHexString(st.getLength()) + " beyond size 0x" + Integer.toHexString(size));
                    } else if (key.equalsIgnoreCase("desc")) {
                        st.setDescription(p.length > 4 ? p[4] : "");
                    } else {
                        if (p.length < 4) continue;
                        DataType dt = parseType(p[2]);
                        if (dt == null) { warn(e.getKey() + "+" + key + ": unknown type '" + p[2] + "'"); continue; }
                        setField(st, (int) parseNum(key), dt, p[3].trim(), p.length > 4 ? p[4] : null);
                        fields++;
                    }
                } catch (Exception ex) {
                    warn(e.getKey() + " " + String.join("|", p) + ": " + ex.getMessage());
                }
            }
        }
        println("structs: " + structs.size() + ", fields applied: " + fields);

        // 3. Names, comments, prototypes and global types.
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
                Function f = functionFor(a, type);
                if (f != null) {
                    if (f.isThunk()) f = f.getThunkedFunction(true);
                    if (!f.getEntryPoint().equals(a)) f = null;
                }
                if (f != null) {
                    if (f.getParentNamespace() != ns) f.setParentNamespace(ns);
                    if (!f.getName().equals(simple)) f.setName(simple, SourceType.USER_DEFINED);
                    if (!note.isEmpty()) setPlateComment(a, note);
                    if (type.contains("(") && applyPrototype(f, type)) typed++;
                    fn++;
                } else {
                    Symbol s = label(a, simple, ns);
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
        if (winArchive != null) winArchive.close();
        println("names applied: " + fn + " functions, " + lab + " labels, " + typed + " typed; warnings: " + warnings);
    }

    // ---- names ---------------------------------------------------------------------------

    private Function functionFor(Address a, String type) throws Exception {
        Function f = getFunctionAt(a);
        if (f != null) return f;
        if (getFunctionContaining(a) != null) return null; // label inside a function
        MemoryBlock b = currentProgram.getMemory().getBlock(a);
        if (b == null || !b.isExecute()) return null;
        if (getInstructionAt(a) == null) {
            if (getDataAt(a) != null && getDataAt(a).isDefined()) return null;
            disassemble(a);
        }
        if (getInstructionAt(a) == null) return null;
        return createFunction(a, null);
    }

    private Symbol label(Address a, String name, Namespace ns) throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        Symbol keep = null;
        for (Symbol s : st.getSymbols(a)) {
            if (s.getSymbolType() != SymbolType.LABEL) continue;
            if (s.getName().equals(name) && s.getParentNamespace().equals(ns)) keep = s;
            else if (s.getSource() == SourceType.USER_DEFINED) s.delete(); // names.tsv owns this address
        }
        if (keep == null) keep = st.createLabel(a, name, ns, SourceType.USER_DEFINED);
        if (!keep.isPrimary()) keep.setPrimary();
        return keep;
    }

    private Namespace classNamespace(String path) throws Exception {
        Namespace ns = currentProgram.getGlobalNamespace();
        for (String part : path.split("::")) {
            Namespace next = currentProgram.getSymbolTable().getNamespace(part, ns);
            if (next == null) next = currentProgram.getSymbolTable().createClass(ns, part, SourceType.USER_DEFINED);
            ns = next;
        }
        if (!(ns instanceof GhidraClass)) ns = NamespaceUtils.convertNamespaceToClass(ns);
        return ns;
    }

    // ---- prototypes ----------------------------------------------------------------------

    private boolean applyPrototype(Function f, String proto) {
        try {
            int open = proto.indexOf('('), close = proto.lastIndexOf(')');
            String head = proto.substring(0, open).trim();
            String plist = proto.substring(open + 1, close).trim();
            // head = "<ret type> [callconv] Name"; drop the name (may contain ::)
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
                    java.util.regex.Matcher m = java.util.regex.Pattern.compile("^(.*?[\\s\\*])([A-Za-z_][A-Za-z0-9_]*)(\\s*(\\[[^\\]]*\\])*)$").matcher(q);
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
            if (c == '(' || c == '[') depth++;
            else if (c == ')' || c == ']') depth--;
            else if (c == ',' && depth == 0) { out.add(s.substring(start, i)); start = i + 1; }
        }
        out.add(s.substring(start));
        return out;
    }

    private boolean isTypeWord(String w) {
        return w.equals("int") || w.equals("short") || w.equals("char") || w.equals("long") || w.equals("unsigned")
                || w.equals("signed") || w.equals("void") || w.equals("float") || w.equals("double") || w.equals("const");
    }

    // ---- types ---------------------------------------------------------------------------

    private Structure structFor(String name) throws Exception {
        Namespace ns = null;
        try {
            List<Namespace> found = NamespaceUtils.getNamespaceByPath(currentProgram, null, name);
            for (Namespace n : found) if (n instanceof GhidraClass) ns = n;
        } catch (Exception ignored) {
        }
        if (ns != null) {
            Structure s = VariableUtilities.findOrCreateClassStruct((GhidraClass) ns, dtm);
            DataType r = dtm.resolve(s, DataTypeConflictHandler.KEEP_HANDLER);
            return (Structure) r;
        }
        String simple = name.replace("::", "_");
        DataType existing = dtm.getDataType(CAT, simple);
        if (existing instanceof Structure) return (Structure) existing;
        Structure s = new StructureDataType(CAT, simple, 0, dtm);
        return (Structure) dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
    }

    private void setField(Structure st, int off, DataType dt, String name, String comment) {
        int len = dt.getLength();
        if (len <= 0) { warn(st.getName() + "+0x" + Integer.toHexString(off) + ": zero-length type " + dt.getName()); return; }
        if (off + len > st.getLength()) st.growStructure(off + len - st.getLength());
        // clear everything overlapping [off, off+len)
        for (int o = off; o < off + len; o++) {
            for (DataTypeComponent c : st.getComponentsContaining(o)) {
                if (c.getDataType() != DataType.DEFAULT) st.clearComponent(c.getOrdinal());
            }
        }
        st.replaceAtOffset(off, dt, len, name.isEmpty() ? null : name, comment == null || comment.isBlank() ? null : comment);
    }

    private DataType parseType(String spec) {
        String s = spec.trim().replaceAll("\\bconst\\b", "").replaceAll("\\bstruct\\b", "").replaceAll("\\bclass\\b", "").trim();
        List<Integer> dims = new ArrayList<>();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\[\\s*(0x[0-9a-fA-F]+|\\d+)\\s*\\]\\s*$").matcher(s);
        while (m.find()) {
            dims.add(0, (int) parseNum(m.group(1)));
            s = s.substring(0, m.start()).trim();
            m = java.util.regex.Pattern.compile("\\[\\s*(0x[0-9a-fA-F]+|\\d+)\\s*\\]\\s*$").matcher(s);
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
        // A class namespace with a structure (RTTI-recovered or ours).
        try {
            for (Namespace n : NamespaceUtils.getNamespaceByPath(currentProgram, null, s)) {
                if (n instanceof GhidraClass) {
                    Structure st = VariableUtilities.findExistingClassStruct((GhidraClass) n, dtm);
                    if (st != null) return st;
                }
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

    // ---- helpers -------------------------------------------------------------------------

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

    private void warn(String msg) {
        warnings++;
        println("WARN " + msg);
    }
}
