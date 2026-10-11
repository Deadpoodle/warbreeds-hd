"""Read a running WARBREED.EXE's memory (read-only) for reverse engineering.

Usage:
  peek.py ptrs <hexaddr> <count>      list dwords at addr; name RTTI class of any that point at objects
  peek.py shorts <hexaddr> <bytes>    dump memory as int16
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

k32 = ctypes.windll.kernel32
PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400


def find_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq WARBREED.EXE", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split(",")]
        if len(parts) > 1 and parts[0].upper() == "WARBREED.EXE":
            return int(parts[1])
    sys.exit("WARBREED.EXE not running")


h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, find_pid())


def read(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
        return None
    return buf.raw


def dword(addr):
    b = read(addr, 4)
    return struct.unpack("<I", b)[0] if b else None


def rtti(obj):
    vt = dword(obj)
    if not vt or not (0x500000 <= vt < 0x519000):
        return None
    col = dword(vt - 4)
    td = dword(col + 0xC) if col else None
    name = read(td + 8, 64) if td else None
    return name.split(b"\0")[0].decode(errors="replace") if name else None


cmd = sys.argv[1]
addr = int(sys.argv[2], 16)
if cmd == "ptrs":
    for i in range(int(sys.argv[3])):
        a = addr + 4 * i
        v = dword(a)
        name = rtti(v) if v else None
        print(f"{a:08x} +{a - addr:03x}: {v:08x} {name or ''}")
elif cmd == "shorts":
    b = read(addr, int(sys.argv[3]))
    s = struct.unpack(f"<{len(b) // 2}h", b)
    for i in range(0, len(s), 16):
        print(f"+{i * 2:03x}: " + " ".join(f"{v:6d}" for v in s[i:i + 16]))
