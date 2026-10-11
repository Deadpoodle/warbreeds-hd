"""Capture a process's top-level window via PrintWindow (no desktop screenshot,
no focus change). Usage: wincap.py <pid> <out.png> [--scale 0.5]"""
import ctypes
import ctypes.wintypes as wt
import sys

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.SetProcessDPIAware()

pid = int(sys.argv[1])
out = sys.argv[2]
scale = float(sys.argv[sys.argv.index("--scale") + 1]) if "--scale" in sys.argv else 1.0

found = []
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def cb(hwnd, _):
    p = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
    if p.value == pid and user32.IsWindowVisible(hwnd):
        r = wt.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(r))
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        found.append((r.right * r.bottom, hwnd, r.right, r.bottom, buf.value))
    return True


user32.EnumWindows(EnumProc(cb), 0)
if not found:
    sys.exit("no visible window for pid")
found.sort(reverse=True)
_, hwnd, w, h, title = found[0]
print(f"hwnd={hwnd:#x} client={w}x{h} title={title!r}")

hdc = user32.GetDC(hwnd)
mdc = gdi32.CreateCompatibleDC(hdc)
bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
gdi32.SelectObject(mdc, bmp)
ok = user32.PrintWindow(hwnd, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


bi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
data = ctypes.create_string_buffer(w * h * 4)
gdi32.GetDIBits(mdc, bmp, 0, h, data, ctypes.byref(bi), 0)
gdi32.DeleteObject(bmp)
gdi32.DeleteDC(mdc)
user32.ReleaseDC(hwnd, hdc)

img = Image.frombuffer("RGBA", (w, h), data, "raw", "BGRA", 0, 1).convert("RGB")
if scale != 1.0:
    img = img.resize((int(w * scale), int(h * scale)), Image.LANCZOS)
img.save(out)
print(f"PrintWindow={ok} saved {out}")
