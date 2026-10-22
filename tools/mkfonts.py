#!/usr/bin/env python3
"""Generate the LVGL fonts in ui/fonts/*.c, ui/icons.h and proto/plink_icons.h.

Text is Noto Sans. Icons are Material Design Icons from an installed Nerd Font, or set NERD_FONT to the font's
path. Needs node with npx, fontTools and the fonts.
"""

import glob
import os
import subprocess
import sys

from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONV = "lv_font_conv@1.5.3"
NOTO = os.environ.get("NOTO_DIR", "/usr/share/fonts/noto") + "/NotoSans-%s.ttf"

# ASCII, ÅÆØäåæéöøü, ° ² ³ µ μ · – — ‘ ’ “ ” … ₂ €. The ESP passes units and notifications through as they are.
# ÄÖÜ would make every line taller.
TEXT_RANGE = (
    "0x20-0x7E,0xB0,0xB2,0xB3,0xB5,0xB7,0xC5,0xC6,0xD8,0xE4,0xE5,0xE6,0xE9,0xF6,0xF8,0xFC,0x3BC,"
    "0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x2082,0x20AC"
)
NUM_RANGE = "0x20,0x25,0x2B-0x3A,0xB0,0x43,0x68,0x6D,0x2013"  # " %+,-./0-9: ° C h m –"

TEXT = [  # name, weight, size, range
    ("f12", "Medium", 12, TEXT_RANGE),
    ("f14", "Medium", 14, TEXT_RANGE),
    ("f16", "Medium", 16, TEXT_RANGE),
    ("f20", "Bold", 20, TEXT_RANGE),
    ("f28", "Bold", 28, TEXT_RANGE),
    ("n40", "Medium", 40, NUM_RANGE + ",0x61,0x70"),  # and a p for am and pm in the time editor
    ("n64", "Regular", 64, NUM_RANGE),
]

# C name, then the MDI glyph name in the Nerd Font without md-. The index is the icon id on the ESP link, so
# new icons go at the end.
ICONS = [
    ("fan", "fan"),
    ("fan_off", "fan_off"),
    ("fan_auto", "fan_auto"),
    ("fan_clock", "fan_clock"),
    ("thermo", "thermometer"),
    ("thermo_hi", "thermometer_high"),
    ("thermo_lo", "thermometer_low"),
    ("home_thermo", "home_thermometer_outline"),
    ("fire", "fire"),
    ("snow", "snowflake"),
    ("filter", "air_filter"),
    ("humidity", "water_percent"),
    ("co2", "molecule_co2"),
    ("home", "home_outline"),
    ("cog", "cog"),
    ("bell", "bell"),
    ("bell_ring", "bell_ring"),
    ("alert", "alert"),
    ("alert_c", "alert_circle_outline"),
    ("cal_clock", "calendar_clock"),
    ("calendar", "calendar_week"),
    ("clock", "clock_outline"),
    ("info", "information_outline"),
    ("sun", "weather_sunny"),
    ("night", "weather_night"),
    ("leaf", "leaf"),
    ("fireplace", "fireplace"),
    ("wifi", "wifi"),
    ("wifi_off", "wifi_off"),
    ("chart", "chart_line"),
    ("sync", "sync"),
    ("radiator", "radiator"),
    ("brightness", "brightness_6"),
    ("palette", "palette"),
    ("tools", "tools"),
    ("right", "chevron_right"),
    ("left", "chevron_left"),
    ("up", "chevron_up"),
    ("down", "chevron_down"),
    ("check", "check"),
    ("check_c", "check_circle"),
    ("power", "power"),
    ("timer", "timer_outline"),
    ("timer_sand", "timer_sand"),
    ("sleep", "sleep"),
    ("away", "home_export_outline"),
    ("water", "water"),
    ("arrow_r", "arrow_right"),
    ("swap", "swap_horizontal"),
    ("rotor", "rotate_3d_variant"),
    ("help", "help_circle_outline"),
    ("tune", "tune_vertical"),
    ("list", "format_list_bulleted"),
    ("monitor", "monitor"),
    ("gauge", "gauge"),
    ("lan", "lan_connect"),
    ("restore", "restore"),
    ("lock", "lock"),
    ("history", "history"),
    ("counter", "counter"),
    ("shower", "shower"),
    ("window", "window_open_variant"),
    ("heat_wave", "heat_wave"),
    ("sun_thermo", "sun_thermometer"),
    ("snow_thermo", "snowflake_thermometer"),
    ("home_clock", "home_clock"),
    ("run", "exit_run"),
    ("windy", "weather_windy"),
    ("eye", "eye"),
    ("delete", "delete"),
    ("refresh", "refresh"),
    ("toggle_on", "toggle_switch"),
    ("toggle_off", "toggle_switch_off_outline"),
    ("radio_on", "radiobox_marked"),
    ("radio_off", "radiobox_blank"),
    ("plus", "plus"),
    ("minus", "minus"),
    ("chip", "chip"),
    ("service", "account_hard_hat"),
    ("shield", "shield_check"),
    ("bolt", "lightning_bolt"),
    ("coil", "heating_coil"),
    ("boiler", "water_boiler"),
    ("hvac", "hvac"),
    ("thermostat", "thermostat"),
    ("cloud", "weather_partly_cloudy"),
    ("play", "play"),
    ("stop", "stop"),
    ("circle", "circle_medium"),
    ("dots", "dots_horizontal"),
    ("hand", "hand_back_right"),
    ("speed", "speedometer"),
    ("bulb", "lightbulb"),
    ("plug", "power_plug"),
    ("washer", "washing_machine"),
    ("door", "door_open"),
    ("battery", "battery"),
    ("solar", "solar_power"),
    ("cash", "cash"),
    ("rain", "weather_rainy"),
    ("snowy", "weather_snowy"),
    ("hass", "home_assistant"),
    ("vacuum", "robot_vacuum"),
    ("tv", "television"),
    ("unlock", "lock_open"),
    ("motion", "motion_sensor"),
]

# the 32 and 48 px fonts only have the icons the UI draws at that size
BIG_ICONS = {32: "fan fan_off alert filter", 48: "wifi_off check_c hass"}
ICON_SIZES = [16, 22, 32, 48]


def nerd_font():
    if os.environ.get("NERD_FONT"):
        return os.environ["NERD_FONT"]
    found = sorted(glob.glob("/usr/share/fonts/TTF/*NerdFontPropo-Regular.ttf"))
    if not found:
        sys.exit("no Nerd Font found: install one or set NERD_FONT")
    return found[0]


def icon_map(nerd):
    """(C name, glyph name, code point) for each icon."""
    by_name = {}
    for cp, g in TTFont(nerd).getBestCmap().items():
        by_name.setdefault(g, cp)
    missing = [g for _, g in ICONS if "md-" + g not in by_name]
    if missing:
        sys.exit("icons not in %s: %s" % (nerd, ", ".join(missing)))
    return [(c, g, by_name["md-" + g]) for c, g in ICONS]


def conv(out, args):
    """Run lv_font_conv to write the font file `out`."""
    for f in [a for i, a in enumerate(args) if i and args[i - 1] == "--font"]:
        if not os.path.exists(f):
            sys.exit("missing font: " + f)
    cmd = ["npx", "-y", CONV, "--bpp", "4", "--format", "lvgl", "--no-compress", "--lv-include", "lvgl.h"]
    subprocess.run(cmd + args + ["-o", out], check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    path = os.path.join(ROOT, out)
    with open(path) as f:  # drop .user_data, LV_USE_USER_DATA is off
        text = f.read().replace("    .user_data = NULL,\n", "")
    with open(path, "w") as f:
        f.write(text)


def main():
    nerd = nerd_font()
    for name, weight, size, rng in TEXT:
        conv("ui/fonts/font_%s.c" % name, ["--size", str(size), "--font", NOTO % weight, "-r", rng])
    icons = icon_map(nerd)
    for size in ICON_SIZES:
        sub = BIG_ICONS.get(size, "").split()
        rng = ",".join(hex(cp) for c, _, cp in icons if not sub or c in sub)
        conv("ui/fonts/icons_%d.c" % size, ["--size", str(size), "--font", nerd, "-r", rng])
    with open(os.path.join(ROOT, "ui", "icons.h"), "w") as h:
        h.write("// generated by tools/mkfonts.py, Material Design Icons (UTF-8)\n#pragma once\n\n")
        for cname, gname, cp in icons:
            esc = "".join("\\x%02X" % b for b in chr(cp).encode())
            h.write('#define IC_%-12s "%s"  // %s\n' % (cname.upper(), esc, gname))
        h.write("\n")
        for name, *_ in TEXT:
            h.write("LV_FONT_DECLARE(font_%s);\n" % name)
        for size in ICON_SIZES:
            h.write("LV_FONT_DECLARE(icons_%d);\n" % size)
    ids = " \\\n".join("  X(%s)" % cname.upper() for cname, _, _ in icons)
    with open(os.path.join(ROOT, "proto", "plink_icons.h"), "w") as h:
        h.write(
            "// generated by tools/mkfonts.py. K_ITEM_DEF and K_NOTIFY send an icon as its index in this list.\n"
            "// The ESP's name for an icon is its id in lower case.\n#pragma once\n\n"
            "// clang-format off\n#define PLINK_ICONS(X) \\\n%s\n// clang-format on\n"
            "#define ICN_ENUM(id) ICN_##id,\nenum { PLINK_ICONS(ICN_ENUM) ICON_COUNT };\n#undef ICN_ENUM\n" % ids
        )
    print("fonts: %d text, %d icon sizes x %d icons" % (len(TEXT), len(ICON_SIZES), len(icons)))


if __name__ == "__main__":
    main()
