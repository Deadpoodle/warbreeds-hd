"""Print the size and non-empty bounds of each autopilot interface-layer capture.
Usage: hudbounds.py <testgame dir>"""
import glob
import os
import sys

from PIL import Image

for f in sorted(glob.glob(os.path.join(sys.argv[1], "auto-*-hud.bmp"))):
    im = Image.open(f)
    print(os.path.basename(f), im.size, "bounds", im.point(lambda v: 255 if v else 0).getbbox())
