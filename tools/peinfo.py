"""Dump sections, imports and exports of a PE file."""
import sys
import pefile

pe = pefile.PE(sys.argv[1])
print(f"ImageBase {pe.OPTIONAL_HEADER.ImageBase:#x}  Entry {pe.OPTIONAL_HEADER.AddressOfEntryPoint:#x}  "
      f"Linker {pe.OPTIONAL_HEADER.MajorLinkerVersion}.{pe.OPTIONAL_HEADER.MinorLinkerVersion}  "
      f"Characteristics {pe.FILE_HEADER.Characteristics:#x}")
for s in pe.sections:
    name = s.Name.rstrip(b"\0").decode()
    print(f"  {name:8} va={s.VirtualAddress:#08x} vsz={s.Misc_VirtualSize:#08x} "
          f"raw={s.PointerToRawData:#08x} rsz={s.SizeOfRawData:#08x} ch={s.Characteristics:#x}")
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        names = [(i.name.decode() if i.name else f"#{i.ordinal}") + f"@{i.address:#x}" for i in d.imports]
        print(f"[{d.dll.decode()}] " + ", ".join(names))
if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        print(f"export {e.name} @ {pe.OPTIONAL_HEADER.ImageBase + e.address:#x}")
