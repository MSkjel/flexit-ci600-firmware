#!/usr/bin/env python3
"""Make the README's screens from the frames make check and make met draw. CI pushes DEST to the screens branch.

gallery.py OUT DEST

DEST/readme/ gets the README's screens at 2x in a bezel, plus tour.png, an animation of the home screen's views.
DEST/README.md shows every screen and DEST/all/ has them as PNGs.
"""

import argparse
import glob
import os

from PIL import Image, ImageDraw

# file in the README: frame in OUT
README = {
    "menu": "shots/08_menu",
    "setting": "shots/10_edit_setpoint",
    "fan": "shots/05_fan_hud_max",
    "week": "shots/16_week_program",
    "alarms": "shots/27_alarm_list",
    "message": "shots/53_notification",
    "saver_picture": "shots/55_saver_picture_clock",
    "saver_photo": "shots/59c_saver_photo",
    "saver_animation": "shots/58b_saver_animation",
    "daylight": "shots/theme1_dashboard",
    "graphite": "shots/theme2_dashboard",
    "norwegian": "shots_no/01_dashboard",
    "weather_clear": "met/01_clear_noon",
    "weather_sunset": "met/03_sunset",
    "weather_aurora": "met/31_aurora",
    "weather_rain": "met/08_heavy_rain",
    "weather_snow": "met/09_snow",
    "weather_thunder": "met/10_thunder",
    "weather_forecast": "met/15_fair_forecast",
    "weather_coast": "met/24_coast",
    "weather_mountains": "met/26_mountains",
    "weather_city": "met/28_city_night",
    "weather_fog": "met/11_fog",
    "weather_spring": "met/21_spring",
}
TOUR = [
    "shots/01_dashboard",
    "shots/02_airflow",
    "shots/03_trends",
    "shots/03b_sensors",
    "shots/04_comfort",
    "shots/50_smart_view",
]
GROUPS = [
    ("shots", "Screens"),
    ("shots_no", "In Norwegian"),
    ("esp", "Pictures the ESP makes"),
    ("met", "The ESP's weather picture"),
    ("replay", "Replayed bus captures"),
]

SCALE, BEZEL, RADIUS, SCREEN_RADIUS = 2, 18, 26, 6
BEZEL_RGB, EDGE_RGB = (36, 40, 44), (66, 72, 78)


def rounded(size, r):
    """A mask with rounded corners, drawn 4x and scaled down so the corners are smooth"""
    w, h = size
    m = Image.new("L", (w * 4, h * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), r * 4, fill=255)
    return m.resize(size, Image.LANCZOS)


def layer(im, mask):
    im = im.convert("RGBA")
    im.putalpha(mask)
    return im


def framed(im):
    screen = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
    w, h = screen.width + 2 * BEZEL, screen.height + 2 * BEZEL
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.alpha_composite(layer(Image.new("RGB", (w, h), EDGE_RGB), rounded((w, h), RADIUS)))
    out.alpha_composite(layer(Image.new("RGB", (w - 2, h - 2), BEZEL_RGB), rounded((w - 2, h - 2), RADIUS - 1)), (1, 1))
    out.alpha_composite(layer(screen, rounded(screen.size, SCREEN_RADIUS)), (BEZEL, BEZEL))
    return out


def frame(out, name):
    return Image.open(os.path.join(out, name + ".ppm")).convert("RGB")


ap = argparse.ArgumentParser()
ap.add_argument("out", help="make check's output, sim/build/out")
ap.add_argument("dest")
a = ap.parse_args()

os.makedirs(os.path.join(a.dest, "readme"), exist_ok=True)
for name, src in README.items():
    framed(frame(a.out, src)).save(os.path.join(a.dest, "readme", name + ".png"), optimize=True)
tour = [framed(frame(a.out, src)) for src in TOUR]
tour[0].save(os.path.join(a.dest, "readme", "tour.png"), save_all=True, append_images=tour[1:], duration=2500, loop=0)

sha = os.environ.get("GITHUB_SHA", "")
page = [
    "# Screens",
    "",
    "Every screen the simulator draws in `make check`, and the weather pictures from `make met`"
    + (", from %s." % sha[:7] if sha else ".")
    + " CI replaces this branch on every push to main.",
    "",
]
for group, title in GROUPS:
    names = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(a.out, group, "*.ppm")))
    if group == "met":
        names = [n for n in names if n[:2] < "40"]  # 40 and up are the holidays, they stay a surprise
    if not names:
        raise SystemExit("no frames in %s, run make check first" % os.path.join(a.out, group))
    os.makedirs(os.path.join(a.dest, "all", group), exist_ok=True)
    page += ["## " + title, "", "<table>"]
    for i, name in enumerate(names):
        frame(a.out, os.path.join(group, name)).save(os.path.join(a.dest, "all", group, name + ".png"), optimize=True)
        if i % 3 == 0:
            page.append("<tr>")
        page.append(
            '<td align="center"><img src="all/%s/%s.png" width="280"><br><sub>%s</sub></td>' % (group, name, name)
        )
        if i % 3 == 2 or i == len(names) - 1:
            page.append("</tr>")
    page += ["</table>", ""]
open(os.path.join(a.dest, "README.md"), "w").write("\n".join(page))
print("%d README screens, the tour and every screen -> %s" % (len(README), a.dest))
