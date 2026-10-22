#!/usr/bin/env python3
"""Convert the .ppm frames in DIR to pixel exact PNGs and make a contact sheet.

shots.py [--scale N] [--cols N] DIR
"""

import argparse
import glob
import os

from PIL import Image, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("--scale", type=int, default=2)
ap.add_argument("--cols", type=int, default=4)
a = ap.parse_args()

for old in glob.glob(os.path.join(a.dir, "*.png")):
    os.remove(old)
frames = []
for p in sorted(glob.glob(os.path.join(a.dir, "*.ppm"))):
    im = Image.open(p).convert("RGB")
    name = os.path.splitext(os.path.basename(p))[0]
    im.resize((im.width * a.scale, im.height * a.scale), Image.NEAREST).save(os.path.join(a.dir, name + ".png"))
    frames.append((name, im))
if not frames:
    raise SystemExit("no .ppm files in " + a.dir)

w, h = frames[0][1].width * a.scale, frames[0][1].height * a.scale
pad, cap = 24, 26
rows = (len(frames) + a.cols - 1) // a.cols
sheet = Image.new("RGB", (a.cols * (w + pad) + pad, rows * (h + pad + cap) + pad), (40, 44, 46))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("NotoSans-Medium.ttf", 18)
except OSError:
    font = ImageFont.load_default()
for i, (name, im) in enumerate(frames):
    x = pad + (i % a.cols) * (w + pad)
    y = pad + (i // a.cols) * (h + pad + cap)
    draw.text((x, y), name, fill=(220, 226, 228), font=font)
    sheet.paste(im.resize((w, h), Image.NEAREST), (x, y + cap))
sheet.save(os.path.join(a.dir, "contact_sheet.png"))
print("%d frames -> %s" % (len(frames), os.path.join(a.dir, "contact_sheet.png")))
