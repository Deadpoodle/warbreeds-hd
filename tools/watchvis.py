"""Poll for WARBREED.EXE windows for N seconds and report whether any was ever
visible or foreground (to check headless test runs stay off the user's screen).
Usage: watchvis.py <seconds>"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

u = ctypes.windll.user32
P = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
end = time.time() + float(sys.argv[1])
seen_visible = seen_foreground = 0
samples = 0
while time.time() < end:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq WARBREED.EXE", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = {int(l.split(",")[1].strip('"')) for l in out.splitlines() if l.startswith('"WARBREED')}
    fg = u.GetForegroundWindow()

    def cb(h, _):
        global seen_visible, seen_foreground
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value in pids:
            if u.IsWindowVisible(h):
                seen_visible += 1
            if h == fg:
                seen_foreground += 1
        return True

    u.EnumWindows(P(cb), 0)
    samples += 1
    time.sleep(0.25)
print(f"samples={samples} visible_hits={seen_visible} foreground_hits={seen_foreground}")
