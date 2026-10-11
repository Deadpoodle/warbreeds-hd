"""Static analysis helpers for WARBREED.EXE.

Usage:
  dis.py dump                      linear-disassemble .text into re/text.asm (cache)
  dis.py str <regex>               find ASCII strings matching regex, with VA
  dis.py xref <hexva>              find instructions referencing an address/immediate
  dis.py imm <int> [<int>...]      find instructions using an immediate value
  dis.py at <hexva> [count]        disassemble count instructions from VA
  dis.py func <hexva>              disassemble from VA until ret/int3 padding
"""
import os
import re
import sys

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get("WB_EXE", r"D:\GOG Galaxy\Games\Warbreeds\WARBREED.EXE")
ASM = os.path.join(ROOT, "re", "text.asm")

pe = pefile.PE(EXE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase
data = pe.__data__


def section(name):
    for s in pe.sections:
        if s.Name.rstrip(b"\0").decode() == name:
            return s
    raise KeyError(name)


def va_to_off(va):
    return pe.get_offset_from_rva(va - BASE)


def read(va, n):
    o = va_to_off(va)
    return data[o:o + n]


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def disasm(va, n):
    return md.disasm(read(va, n), va)


def dump():
    t = section(".text")
    start = BASE + t.VirtualAddress
    code = data[t.PointerToRawData:t.PointerToRawData + t.Misc_VirtualSize]
    out = []
    off = 0
    while off < len(code):
        got = False
        for ins in md.disasm(code[off:], start + off):
            out.append(f"{ins.address:08x}  {ins.bytes.hex():24} {ins.mnemonic} {ins.op_str}")
            off = ins.address + ins.size - start
            got = True
        if off < len(code):
            out.append(f"{start + off:08x}  {code[off]:02x}                       db")
            off += 1
        elif not got:
            break
    with open(ASM, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {len(out)} lines to {ASM}")


def lines():
    if not os.path.exists(ASM):
        dump()
    with open(ASM) as f:
        return f.read().splitlines()


def strings(pattern):
    rx = re.compile(pattern.encode(), re.I)
    for m in re.finditer(rb"[\x20-\x7e]{4,}", data):
        if rx.search(m.group()):
            try:
                rva = pe.get_rva_from_offset(m.start())
            except Exception:
                rva = None
            va = f"{BASE + rva:08x}" if rva is not None else "--------"
            print(va, m.group().decode())


def grep_asm(rx):
    r = re.compile(rx, re.I)
    for ln in lines():
        if r.search(ln):
            print(ln)


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "dump":
        dump()
    elif cmd == "str":
        strings(args[0])
    elif cmd == "xref":
        v = int(args[0], 16)
        grep_asm(rf"(0x{v:x}\b)")
    elif cmd == "imm":
        alts = "|".join(f"0x{int(a, 0):x}" for a in args)
        grep_asm(rf"(?:,|\s)(?:{alts})$")
    elif cmd == "at":
        n = int(args[1]) if len(args) > 1 else 40
        for i, ins in enumerate(disasm(int(args[0], 16), n * 8)):
            if i >= n:
                break
            print(f"{ins.address:08x}  {ins.mnemonic} {ins.op_str}")
    elif cmd == "func":
        for ins in disasm(int(args[0], 16), 0x4000):
            print(f"{ins.address:08x}  {ins.mnemonic} {ins.op_str}")
            if ins.mnemonic == "ret" and read(ins.address + ins.size, 1) in (b"\xcc", b"\x90"):
                break
    elif cmd == "grep":
        grep_asm(args[0])


if __name__ == "__main__":
    main()
