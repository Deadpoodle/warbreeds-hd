"""Extract images from WarBreeds' Mohawk (.MHK) bitmap sets (tBMH) with their
palette (tPAL), e.g. the clean backgrounds of the menu screens.

Usage: mhkimg.py <file.MHK> <outdir> [--pal N] [--frames 0,1,...]
Writes <outdir>/<name>-<frame>.png (index 0 transparent unless the image is opaque).

Formats (from the data):
  tBMH: u16 count, u16 ?, u16 count, u16 flags, then
        flags 0x0002: u32 offsets[count] into the resource;
        flags 0x0102: u32 unpacked size, u32 packed size, u16 ring size, then
                      LZ data that unpacks to (header-less) u32 offsets[count]
                      followed by the images; offsets are relative to that buffer
                      plus the 8-byte header.
  image: u16 width, u16 height, u16 bytesPerRow, u16 format; format & 0x10 = RLE8
         rows (u16 row length, then (b & 0x80 ? run of (b & 0x7f) + 1 copies of the
         next byte : (b + 1) literal bytes)), else raw rows.
  LZ: Mohawk LZSS (see unlz).
  tPAL: u16 first, u16 count, then count x (r, g, b, 0)."""
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def parse(path):
    d = open(path, "rb").read()
    assert d[:4] == b"MHWK" and d[8:12] == b"RSRC"
    ver, comp, rsz, absoff, fto, fts = struct.unpack(">HHIIHH", d[12:28])
    nlo, tc = struct.unpack(">HH", d[absoff:absoff + 4])
    ft = absoff + fto
    fc = struct.unpack(">I", d[ft:ft + 4])[0]
    files = []
    for i in range(fc):
        o, s, sh, fl, u = struct.unpack(">IHBBH", d[ft + 4 + 10 * i:ft + 14 + 10 * i])
        files.append((o, s | (sh << 16)))
    out = {}
    for t in range(tc):
        tag, rto, nto = struct.unpack(">4sHH", d[absoff + 4 + 8 * t:absoff + 12 + 8 * t])
        rc = struct.unpack(">H", d[absoff + rto:absoff + rto + 2])[0]
        lst = []
        for k in range(rc):
            rid, idx = struct.unpack(">HH", d[absoff + rto + 2 + 4 * k:absoff + rto + 6 + 4 * k])
            o, s = files[idx - 1]
            lst.append((rid, d[o:o + s]))
        out[tag.decode("latin1")] = lst
    return out


def unlz(data, size):
    """Mohawk LZSS: 1024-byte ring starting at 958 (1024 - 66), flag bits LSB
    first (1 = literal), match = BE u16: ring position (low 10 bits), length
    (high 6 bits) + 3."""
    ring = bytearray(1024)
    r = 958
    out = bytearray()
    i = 0
    flags = 0
    while i < len(data) and len(out) < size:
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xff00
            i += 1
        if flags & 1:
            c = data[i]
            i += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & 1023
        else:
            if i + 2 > len(data):
                break
            code = data[i] << 8 | data[i + 1]
            i += 2
            pos = code & 1023
            for k in range((code >> 10) + 3):
                c = ring[(pos + k) & 1023]
                out.append(c)
                ring[r] = c
                r = (r + 1) & 1023
    return bytes(out[:size])


def images(blob):
    count, _, _, flags = struct.unpack(">HHHH", blob[:8])
    if flags & 0x100:
        size, packed, ring = struct.unpack(">IIH", blob[8:18])
        body = unlz(blob[18:18 + packed], size)
        base = b"\0" * 8 + body  # offsets count from the start of the 8-byte header
    else:
        base = blob
    offs = [struct.unpack(">I", base[8 + 4 * i:12 + 4 * i])[0] for i in range(count)]
    for o in offs:
        w, h, bpr, fmt = struct.unpack(">HHHH", base[o:o + 8])
        if w > 4096 or h > 4096:
            yield w, h, fmt, b""
            continue
        p = o + 8
        px = bytearray(w * h)
        for y in range(h):
            if fmt & 0x10:
                n = struct.unpack(">H", base[p:p + 2])[0]
                q, end, x = p + 2, p + 2 + n, 0
                while q < end and x < w:
                    c = base[q]
                    q += 1
                    if c & 0x80:
                        k = (c & 0x7f) + 1
                        px[y * w + x:y * w + x + k] = bytes([base[q]]) * k
                        q += 1
                    else:
                        k = c + 1
                        px[y * w + x:y * w + x + k] = base[q:q + k]
                        q += k
                    x += k
                p = end
            else:
                px[y * w:y * w + w] = base[p:p + w]
                p += bpr
        yield w, h, fmt, bytes(px[:w * h])


def palette(blob):
    first, count = struct.unpack(">HH", blob[:4])
    pal = [(0, 0, 0)] * 256
    for i in range(count):
        r, g, b = blob[4 + 4 * i:7 + 4 * i]
        if first + i < 256:
            pal[first + i] = (r, g, b)
    return pal


def main():
    path, outdir = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    palno = int(args[args.index("--pal") + 1]) if "--pal" in args else 0
    frames = [int(x) for x in args[args.index("--frames") + 1].split(",")] if "--frames" in args else None
    res = parse(path)
    pal = palette(res["tPAL"][palno][1]) if "tPAL" in res else [(i, i, i) for i in range(256)]
    flat = [c for rgb in pal for c in rgb]
    os.makedirs(outdir, exist_ok=True)
    name = os.path.splitext(os.path.basename(path))[0]
    for rid, blob in res.get("tBMH", []):
        for i, (w, h, fmt, px) in enumerate(images(blob)):
            if frames is not None and i not in frames:
                continue
            if w * h != len(px) or w > 4096 or h > 4096:
                print(f"{name} {rid} frame {i}: skipped (bad header {w}x{h})")
                continue
            im = Image.frombytes("P", (w, h), px)
            im.putpalette(flat)
            im.convert("RGB").save(os.path.join(outdir, f"{name}-{rid}-{i}.png"))
            print(f"{name} {rid} frame {i}: {w}x{h} fmt {fmt:#x}")


if __name__ == "__main__":
    main()
