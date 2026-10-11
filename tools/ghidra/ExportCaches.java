// Export an analysed Ghidra program as plain text files that research agents can grep.
// Extends the New Game+ skill's ExportCaches.java (Tier 1) with per-class files, an RTTI class
// table and the struct definitions. Apply names first with ApplyNames.java:
//
//   analyzeHeadless <projDir> <projName> -process <program> -noanalysis -scriptPath <this folder>
//       -postScript ApplyNames.java <names.tsv> <types.tsv> -postScript ExportCaches.java <outDir>
//
// Writes into <outDir>:
//   functions.tsv   entry, name (Class::Method when it has a class), size, callers, callees, signature
//   decompiled.c    every function's decompiled C, each headed "// ==== <entry> <name>"
//   by-class/*.c    the same functions split by class/namespace (<Class>.c, nested A::B -> A__B.c);
//                   functions without a namespace go to range_<start>-<end>.c (64 KB of .text each)
//   classes.tsv     every MSVC RTTI class: name, vtable, this-offset, base classes, slot count,
//                   then each virtual slot as name@address
//   types.h         C definitions of the structs (category /WarBreeds and the class structs)
//   xrefs.tsv       from, to, type, from-function, to-symbol (code and data references)
//   strings.tsv     address, referencing functions, the string
//   symbols.tsv     every non-default label/function/class symbol: address, type, namespace::name
//@category Export

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class ExportCaches extends GhidraScript {

    private PrintWriter open(File dir, String name) throws Exception {
        return new PrintWriter(new File(dir, name), StandardCharsets.UTF_8);
    }

    private String fname(Address a) {
        Function f = getFunctionContaining(a);
        return f == null ? "" : qname(f);
    }

    private String qname(Function f) {
        return f.getName(true);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "ghidra-export");
        out.mkdirs();
        File byClass = new File(out, "by-class");
        byClass.mkdirs();
        for (File old : Objects.requireNonNull(byClass.listFiles())) old.delete();

        // functions.tsv
        int count = 0;
        try (PrintWriter w = open(out, "functions.tsv")) {
            w.println("entry\tname\tsize\tcallers\tcallees\tsignature");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                w.printf("%s\t%s\t%d\t%d\t%d\t%s%n", f.getEntryPoint(), qname(f), f.getBody().getNumAddresses(),
                        f.getCallingFunctions(monitor).size(), f.getCalledFunctions(monitor).size(),
                        f.getPrototypeString(false, false));
                count++;
            }
        }
        println("functions: " + count);

        // decompiled.c + by-class/*.c
        DecompInterface di = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        opts.grabFromProgram(currentProgram);
        di.setOptions(opts);
        di.openProgram(currentProgram);
        Map<String, StringBuilder> groups = new TreeMap<>();
        int ok = 0, failed = 0;
        try (PrintWriter w = open(out, "decompiled.c")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                if (f.isThunk() || f.isExternal()) continue;
                StringBuilder sb = new StringBuilder();
                sb.append("// ==== ").append(f.getEntryPoint()).append(' ').append(qname(f)).append('\n');
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    sb.append(r.getDecompiledFunction().getC()).append('\n');
                    ok++;
                } else {
                    sb.append("// decompile failed: ").append(r == null ? "?" : r.getErrorMessage()).append('\n');
                    failed++;
                }
                w.print(sb);
                groups.computeIfAbsent(groupOf(f), k -> new StringBuilder()).append(sb);
            }
        }
        di.dispose();
        for (Map.Entry<String, StringBuilder> e : groups.entrySet()) {
            try (PrintWriter w = open(byClass, e.getKey() + ".c")) {
                w.print(e.getValue());
            }
        }
        println("decompiled: " + ok + " ok, " + failed + " failed; by-class files: " + groups.size());

        // classes.tsv
        println("classes: " + exportClasses(out));

        // types.h
        try (PrintWriter w = open(out, "types.h")) {
            DataTypeManager dtm = currentProgram.getDataTypeManager();
            List<DataType> list = new ArrayList<>();
            Iterator<Structure> it = dtm.getAllStructures();
            while (it.hasNext()) {
                Structure s = it.next();
                String cat = s.getCategoryPath().getPath();
                if (cat.startsWith("/WarBreeds") || cat.startsWith("/ClassDataTypes")) list.add(s);
            }
            new DataTypeWriter(dtm, w, false).write(list, monitor, false);
            println("types.h structs: " + list.size());
        }

        // xrefs.tsv
        ReferenceManager rm = currentProgram.getReferenceManager();
        int refs = 0;
        try (PrintWriter w = open(out, "xrefs.tsv")) {
            w.println("from\tto\ttype\tfrom_function\tto_symbol");
            AddressIterator src = rm.getReferenceSourceIterator(currentProgram.getMinAddress(), true);
            while (src.hasNext() && !monitor.isCancelled()) {
                Address from = src.next();
                for (Reference r : rm.getReferencesFrom(from)) {
                    Address to = r.getToAddress();
                    if (!to.isMemoryAddress() && !to.isExternalAddress()) continue;
                    Symbol s = getSymbolAt(to);
                    w.printf("%s\t%s\t%s\t%s\t%s%n", from, to, r.getReferenceType(), fname(from),
                            s == null ? "" : s.getName(true));
                    refs++;
                }
            }
        }
        println("xrefs: " + refs);

        // strings.tsv
        int strings = 0;
        try (PrintWriter w = open(out, "strings.tsv")) {
            w.println("address\tfunctions\tstring");
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (monitor.isCancelled()) break;
                if (!d.hasStringValue()) continue;
                Set<String> users = new TreeSet<>();
                for (Reference r : rm.getReferencesTo(d.getAddress())) users.add(fname(r.getFromAddress()));
                String v = String.valueOf(d.getValue()).replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
                w.printf("%s\t%s\t%s%n", d.getAddress(), String.join(",", users), v);
                strings++;
            }
        }
        println("strings: " + strings);

        // symbols.tsv
        int syms = 0;
        try (PrintWriter w = open(out, "symbols.tsv")) {
            w.println("address\ttype\tname");
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Symbol s = it.next();
                if (s.getSource() == SourceType.DEFAULT || s.isExternal()) continue;
                w.printf("%s\t%s\t%s%n", s.getAddress(), s.getSymbolType(), s.getName(true));
                syms++;
            }
        }
        println("symbols: " + syms);
    }

    // by-class file name for a function
    private String groupOf(Function f) {
        Namespace ns = f.getParentNamespace();
        if (ns != null && !ns.isGlobal() && !(ns instanceof Library)) {
            return ns.getName(true).replace("::", "__").replaceAll("[^A-Za-z0-9_$.-]", "_");
        }
        long a = f.getEntryPoint().getOffset() & ~0xffffL;
        return String.format("range_%08x-%08x", a, a + 0xffff);
    }

    // ---- RTTI ------------------------------------------------------------------------------

    private int exportClasses(File out) throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock text = mem.getBlock(".text");
        // type descriptors: {vftable of type_info, spare, ".?AV...@@"}
        Map<Long, String> tds = new HashMap<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            for (int i = 8; i + 4 < buf.length; i++) {
                if (buf[i] == '.' && buf[i + 1] == '?' && buf[i + 2] == 'A' && (buf[i + 3] == 'V' || buf[i + 3] == 'U')) {
                    int j = i;
                    while (j < buf.length && buf[j] != 0) j++;
                    tds.put(b.getStart().getOffset() + i - 8, new String(buf, i, j - i, StandardCharsets.US_ASCII));
                }
            }
        }
        // complete object locators: {sig 0, offset, cdOffset, pTypeDescriptor, pClassHierarchyDescriptor}
        Map<Long, long[]> cols = new HashMap<>();
        Map<Long, String> colName = new HashMap<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address s = b.getStart();
            for (long o = 12; o + 8 <= b.getSize(); o += 4) {
                long v = u32(s.add(o));
                if (!tds.containsKey(v)) continue;
                if (u32(s.add(o - 12)) != 0) continue;
                long chd = u32(s.add(o + 4));
                if (!mem.contains(toAddr(chd))) continue;
                long col = s.getOffset() + o - 12;
                cols.put(col, new long[] {u32(s.add(o - 8)), v, chd});
                colName.put(col, tds.get(v));
            }
        }
        // vftables: the dword before a vftable points at its COL
        TreeMap<Long, Long> vtables = new TreeMap<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address s = b.getStart();
            for (long o = 0; o + 8 <= b.getSize(); o += 4) {
                long v = u32(s.add(o));
                if (cols.containsKey(v)) vtables.put(s.getOffset() + o + 4, v);
            }
        }
        int n = 0;
        try (PrintWriter w = open(out, "classes.tsv")) {
            w.println("class\tvtable\tthis_offset\tbases\tslots\tvirtual_functions");
            for (Map.Entry<Long, Long> e : vtables.entrySet()) {
                long vt = e.getKey();
                long[] col = cols.get(e.getValue());
                String cls = className(toAddr(vt), colName.get(e.getValue()));
                // bases from the class hierarchy descriptor {sig, attr, numBases, pBaseClassArray}
                List<String> bases = new ArrayList<>();
                long chd = col[2];
                long nb = u32(toAddr(chd + 8)), bca = u32(toAddr(chd + 12));
                for (int i = 1; i < nb && i < 64; i++) {
                    long bcd = u32(toAddr(bca + 4L * i));
                    bases.add(demangle(tds.getOrDefault(u32(toAddr(bcd)), "?")));
                }
                List<String> slots = new ArrayList<>();
                for (long a = vt;; a += 4) {
                    if (a != vt && (vtables.containsKey(a + 4) || vtables.containsKey(a))) break;
                    if (!mem.contains(toAddr(a))) break;
                    long f = u32(toAddr(a));
                    if (!text.contains(toAddr(f))) break;
                    if (a != vt && getSymbolAt(toAddr(a)) != null && getSymbolAt(toAddr(a)).getSource() != SourceType.DEFAULT
                            && getSymbolAt(toAddr(a)).getName().contains("vftable")) break;
                    Function fn = getFunctionAt(toAddr(f));
                    slots.add((fn == null ? "?" : qname(fn)) + "@" + Long.toHexString(f));
                }
                w.printf("%s\t%08x\t%d\t%s\t%d\t%s%n", cls, vt, col[0], String.join(",", bases), slots.size(),
                        String.join(" ", slots));
                n++;
            }
        }
        return n;
    }

    private String className(Address vt, String mangled) {
        Symbol s = getSymbolAt(vt);
        if (s != null && s.getName().contains("vftable") && !s.getParentNamespace().isGlobal()) {
            return s.getParentNamespace().getName(true);
        }
        return demangle(mangled);
    }

    // ".?AVtPathStep@ACreatureBody@@" -> "ACreatureBody::tPathStep"; templates are kept mangled.
    private String demangle(String m) {
        if (!m.startsWith(".?A") || !m.endsWith("@@")) return m;
        String body = m.substring(4, m.length() - 2);
        if (body.startsWith("?$")) return body;
        String[] parts = body.split("@");
        StringBuilder sb = new StringBuilder();
        for (int i = parts.length - 1; i >= 0; i--) {
            if (parts[i].isEmpty()) continue;
            if (sb.length() > 0) sb.append("::");
            sb.append(parts[i]);
        }
        return sb.toString();
    }

    private long u32(Address a) throws Exception {
        return currentProgram.getMemory().getInt(a) & 0xffffffffL;
    }
}
