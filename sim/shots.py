#!/usr/bin/env python3
"""Convert the .ppm frames in DIR to pixel exact PNGs and make a contact sheet.

shots.py [--scale N] [--cols N] [--before BEFORE] DIR

With --before, a row per frame: BEFORE's frame of that name, the frame, and the pixels that differ in magenta.
"""

import argparse
import glob
import os

from PIL import Image, ImageChops, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("--scale", type=int, default=2)
ap.add_argument("--cols", type=int, default=4)
ap.add_argument("--before")
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


def changes(before, after):
    """after, dimmed, with the pixels that differ from before in magenta, and how many those are"""
    mask = ImageChops.difference(before, after).convert("L").point(lambda v: 255 if v else 0)
    out = Image.blend(Image.new("RGB", after.size), after, 0.35)
    out.paste((255, 0, 255), mask=mask)
    return out, mask.histogram()[255]


# The cells of the sheet, in rows of cols: a caption and a frame, or None for an empty one
cells = []
if a.before:
    a.cols = 3
    for name, im in frames:
        path = os.path.join(a.before, name + ".ppm")
        before = Image.open(path).convert("RGB") if os.path.exists(path) else None
        if before and before.size == im.size:
            diff, n = changes(before, im)
            cells += [(name + " before", before), ("after", im), ("%d pixels differ" % n, diff)]
        else:
            cells += [(name + " before", before), ("after", im), ("no before" if not before else "resized", None)]
else:
    cells = frames

w, h = frames[0][1].width * a.scale, frames[0][1].height * a.scale
pad, cap = 24, 26
rows = (len(cells) + a.cols - 1) // a.cols
sheet = Image.new("RGB", (a.cols * (w + pad) + pad, rows * (h + pad + cap) + pad), (40, 44, 46))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("NotoSans-Medium.ttf", 18)
except OSError:
    font = ImageFont.load_default()
for i, (name, im) in enumerate(cells):
    x = pad + (i % a.cols) * (w + pad)
    y = pad + (i // a.cols) * (h + pad + cap)
    draw.text((x, y), name, fill=(220, 226, 228), font=font)
    if im:
        sheet.paste(im.resize((w, h), Image.NEAREST), (x, y + cap))
sheet.save(os.path.join(a.dir, "contact_sheet.png"))
print("%d frames -> %s" % (len(frames), os.path.join(a.dir, "contact_sheet.png")))
