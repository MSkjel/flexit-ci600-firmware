# CI600 replacement firmware

New firmware for the Flexit CI600 control panel (STM32F103, 320x240 screen, five keys). It keeps Flexit's
bootloader, so it goes on over the bus like a normal update and the panel stays on the wall.

On its own it does what the stock panel does, with a new UI. Add an ESP32 running
[esphome-flexit-modbus-server](https://github.com/MSkjel/esphome-flexit-modbus-server) and it also shows stuff
from Home Assistant: sensors, switches, messages, pictures and the weather.

> This is a hobby project, not made by or affiliated with Flexit. You flash it at your own risk, see the
> [disclaimer](#disclaimer).

<p align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/tour.png" width="560" alt="The home screen's views: overview, air flow, trends, sensors, comfort and smart home"></p>

- [What it does](#what-it-does)
- [Installing](#installing)
- [First setup](#first-setup)
- [Keys](#keys)
- [Troubleshooting](#troubleshooting)
- [Going back to stock](#going-back-to-stock)
- [Disclaimer](#disclaimer)
- [Screens](#screens)
- [Development](#development)
- [License](#license)

## What it does

Everything the stock panel does:

- Fan speed (Stop, Min, Normal, Max), boost, and the fan % for each speed if you have EC fans
- Setpoint, supply air limits, heater and cooling
- Week program, up to four periods a day with their own fan speed and setpoint
- Filter timer and reset
- Alarms, with pop-ups and history
- Run hours
- All the unit's temperatures, plus the panel's own room sensor
- Date and time, with summer time
- Factory reset of the unit

Plus:

- Home screen views: an overview with tiles you pick, the air flow through the unit, and 24 h temperature charts
- Three themes, a night mode with its own hours, brightness and theme, and a screensaver
- Help for every setting on the ? key
- Key lock
- Settings for when the three lights are on
- English and Norwegian

With an ESP:

- Any Home Assistant sensor, switch, number, button or text on the panel. You pick them in the ESP config.
- 24 h charts of the ESP's sensors, like CO₂ and humidity
- Messages from Home Assistant
- A picture, photo, animation or the local weather (MET Norway) as the screensaver
- The clock from Home Assistant, and panel updates from Home Assistant

## Installing

Works on a stock panel, nothing to open. Grab `ci600.fxup` from the
[latest release](https://github.com/MSkjel/flexit-ci600-firmware/releases/latest) and send it to the panel one of
these ways:

**From Home Assistant (ESP).** Add `packages/panel.yaml` and `packages/panel/update.yaml` to the ESP config and
press **Flash Panel Firmware**. It fetches the latest release by itself. A stock panel is at address 1, so the ESP
has to be at 2 until the panel is flashed. The new panel comes up at 3, so move the ESP to 1 then. To flash a file
you downloaded, point **Panel Firmware URL** at it, e.g. `http://homeassistant.local:8123/local/ci600.fxup` for a
file in `/config/www`, or run `python3 tools/fxserve.py ci600.fxup` and use the URL it prints.

**USB-RS485 adapter.** Hook it up to the panel's bus and run:

    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 ci600.fxup

A stock panel is always at address 1. For one that isn't, add `--panel-addr N`. On Windows the port is something
like `COM3`. The bootloader answers at address 1, so unplug the ESP (or whatever else is at 1) or move it to
another address before flashing.

The screen goes dark with all three lights on for a few minutes. When it's back:

1. Restart the ventilation unit (power off and on). The CU60, the unit's controller, only looks for panels when
   it starts.
2. Do the [first setup](#first-setup). Every update wipes the panel's own settings, since the bootloader erases
   all of its flash. Stuff the unit keeps, like fan speeds, setpoint and the week program, is untouched.

More on flashing, and what to do when it doesn't work, in [docs/flashing.md](docs/flashing.md).

## First setup

OK on the home screen opens the menu.

- **System > Panel address:** 1 without an ESP, 3 if the ESP is at 1. The CU60 only runs the week program and
  the boost timer if something answers at 1, and only takes the setpoint and NORMAL fan speeds from the lowest
  address. Restart the unit after changing it.
- **System > Equipment:** make it match your unit (ESP, cooling). EC fans and water heater come from the unit's
  DIP switches.
- **Display:** language and theme.
- **Date and time,** unless the ESP sets it.

## Keys

| Key | Does |
|---|---|
| ▲ ▼ | Move or change a value. Fan speed on the home screen. |
| OK | Open or save. Menu on the home screen. |
| ◀ | Back. Next view on the home screen. |
| ? | Help. Hold 3 s to lock or unlock the keys. |

The bottom line shows what the keys do. Stuff that can't be undone wants OK held for 2 s.

## Troubleshooting

**"No contact with the unit" or empty values.** Restart the ventilation unit, it only finds panels at start-up.
Make sure nothing else on the bus (another panel, the ESP) has the same address. System > Bus shows the traffic.
If *Polls to this panel* stays at 0 after a restart, check the wiring.

**Setpoint or a NORMAL fan speed jumps back.** Only the lowest address can change those. Put the panel at 1, or
change them from Home Assistant if the ESP is at 1.

**Week program doesn't run, or a boost stops after a few seconds.** Check that Week program is switched on in its
menu, the clock is right, and that something is at address 1. The CU60 only runs either with a panel there.

**Clock jumps back after setting it.** A stock panel on the bus keeps its own time and sends it every second, so
set it on that one.

**Update stopped halfway, screen dark, all lights on.** The panel is sitting in the bootloader, which won't start
anything it hasn't fully checked. Just flash again. With `fxupload.py` add `--no-enter`, and `--status` shows
what the bootloader is up to.

**Settings gone after an update.** Normal, see [Installing](#installing). Set the address again and restart the
unit.

**ESP stuff doesn't show up.** Look at System > Bus > ESP link.
- *waiting*: the ESP isn't answering. Is it online, with `packages/panel.yaml`? It only talks to the panel at
  its `panel_address`, 3 by default.
- *v1, the ESP v2* or similar: panel firmware and ESP packages are from different releases. Update the old one.
- *too short* under Pause after status: the CU60 leaves too little room on the bus, so the link backs off for a
  bit and tries again.

**Keys don't do anything.** They're locked, hold ? for 3 s. System > Lock when idle locks them automatically.

**Screen too bright or too dark.** Display > Brightness, Screensaver > Idle brightness, and Display > Night mode.

**Panel temperature reads high.** The panel warms up a bit when used. Compare with a thermometer after an hour
of not touching it and set Temperature > Panel sensor offset.

**Anything else.** System > Restart panel keeps your settings. System > Reset panel settings goes back to the
defaults but keeps the address and equipment. Neither touches the unit. If you open an issue, add the version
from System > About.

## Going back to stock

Needs SWD and a dump of a stock CI600's flash: `python3 tools/flash.py <the dump>`. It writes everything after the
bootloader, which also clears this firmware's settings. Over the bus you'd need a stock update file, and that's not
in here.

## Disclaimer

- It's a hobby project. Flexit didn't make it, doesn't support it and has nothing to do with it. Flexit and CI600
  are their names, used here to say what this runs on.
- Flashing it is your call. It can void your warranty, and don't expect Flexit's support to help with a panel
  running this. Going back to stock needs SWD and a stock dump.
- No warranty, none at all. If your panel, your unit or anything else gets hurt by it, that's on you.
- The unit's own controller still runs the ventilation. The panel just shows things and changes settings, same as
  the stock one, so a setting changed here does reach the unit.
- Flexit's firmware and the bootloader's key aren't in this repo. They're not ours to share.

## Screens

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/menu.png" width="280" alt="Menu"><br><sub>Menu</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/setting.png" width="280" alt="Changing a setting"><br><sub>Changing a setting</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/fan.png" width="280" alt="Fan speed"><br><sub>Fan speed</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/week.png" width="280" alt="Week program"><br><sub>Week program</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/alarms.png" width="280" alt="Alarms"><br><sub>Alarms</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/message.png" width="280" alt="A message from Home Assistant"><br><sub>A message from Home Assistant</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/saver_picture.png" width="280" alt="Screensaver with a picture from the ESP"><br><sub>Screensaver with a picture from the ESP</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/saver_photo.png" width="280" alt="Screensaver with a photo"><br><sub>a photo</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/saver_animation.png" width="280" alt="Screensaver with an animation"><br><sub>or an animation</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/daylight.png" width="280" alt="Daylight theme"><br><sub>Daylight theme</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/graphite.png" width="280" alt="Graphite theme"><br><sub>Graphite theme</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/norwegian.png" width="280" alt="In Norwegian"><br><sub>In Norwegian</sub></td>
</tr>
</table>

The weather screensaver (`packages/panel/met.yaml` in the ESP repo) draws the sky for the time of day, the next
hour's weather from MET Norway, and a landscape you pick.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_clear.png" width="280" alt="Weather picture: Clear"><br><sub>Clear</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_sunset.png" width="280" alt="Weather picture: Sunset"><br><sub>Sunset</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_aurora.png" width="280" alt="Weather picture: Northern lights"><br><sub>Northern lights</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_rain.png" width="280" alt="Weather picture: Heavy rain"><br><sub>Heavy rain</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_snow.png" width="280" alt="Weather picture: Snow"><br><sub>Snow</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_thunder.png" width="280" alt="Weather picture: Thunder"><br><sub>Thunder</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_forecast.png" width="280" alt="Weather picture: With the forecast"><br><sub>With the forecast</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_coast.png" width="280" alt="Weather picture: Coast"><br><sub>Coast</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_mountains.png" width="280" alt="Weather picture: Mountains"><br><sub>Mountains</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_city.png" width="280" alt="Weather picture: City at night"><br><sub>City at night</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_fog.png" width="280" alt="Weather picture: Fog"><br><sub>Fog</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/weather_spring.png" width="280" alt="Weather picture: Spring"><br><sub>Spring</sub></td>
</tr>
</table>

All of these come from the simulator. CI redraws them on every push to main and keeps them on the
[screens](https://github.com/MSkjel/flexit-ci600-firmware/tree/screens) branch.

## Development

| Directory | What |
|---|---|
| `hw/` | Panel hardware: start-up, display, keys, lights, clock, settings in flash, the Modbus server |
| `app/` | Values, panel logic, the panel side of the ESP link, pictures |
| `ui/` | Screens (LVGL 8.3) and the texts for each language |
| `proto/` | The ESP link, shared by both ends: messages (`plink`), frames (`pbulk`), bus timing (`pbus`) |
| `sim/` | The simulator and the tests |
| `tools/` | Flashing, update files, fonts and icons |
| `lib/lvgl` | LVGL 8.3.11 submodule, patched with `lib/lvgl-ci600.patch` in the build dir |
| `lib/tjpgd` | TJpgDec for JPEG pictures |
| `docs/` | The [hardware](docs/hardware.md) and [flashing](docs/flashing.md) |

### Build and test

    git clone --recursive https://github.com/MSkjel/flexit-ci600-firmware
    make          # hw/build/ci600.bin, needs arm-none-eabi-gcc
    make check    # all tests, SAN=1 adds ASan and UBSan
    make format   # clang-format and ruff

`make check` needs PyYAML, `pillow` for the pictures, and `gcc-multilib` and `g++-multilib` since the simulator is
32-bit. It also needs the ESP repo next to this one as `esphome-flexit-modbus-server` (or `ESP_REPO=`), since it
runs the ESP's panel packages against this repo's link code. Every screen the tests draw is hashed into
`sim/golden.txt`: `make -C sim changed` shows what changed, `make -C sim golden` accepts it.
`make -C sim check gallery` draws the README pictures into `sim/build/out/gallery`.

### Update files and SWD

- **`.fxup`:** `tools/mkupdate.py hw/build/ci600.bin ci600.fxup`. It needs the bootloader's key, which isn't in
  this repo. Give it a stock flash dump with `--stock`, or set `CI600_FXUP_KEY` (64 hex digits). CI has it as a
  secret, and pushing a tag like `v1.2.3` makes a release with `ci600.fxup` and `ci600.bin`. The newest `vX.Y.Z`
  tag is the version the panel reports, 0.0.0 without one. A build from a later commit gets the next patch level,
  so the ESP can tell it from the release.
- **SWD:** `make -C hw flash STOCK=<a stock flash dump>` with an ST-LINK and `pip install pyocd`. Leave the
  ST-LINK's 3.3 V off when the unit powers the panel. The bootloader comes from the dump. SWD keeps the panel's
  settings.

The firmware starts at 0x08002100, right after the bootloader.

### The ESP link

About 9 times a second the CU60 sends a status broadcast and then leaves the bus quiet for a moment. The ESP
sends a frame in that gap and the panel answers. Anything left over goes in the gaps after the CU60's polls.
Messages are numbered and resent until the other side has them. `proto/pbus.h` has the bus timing, `pbulk.h` the
frames, `plink_keys.h` and `plink_values.h` the messages. The ESP repo keeps a copy of `proto/`: update it with
`make -C sim esp_sync` (`esp_test` fails if they differ).

### Adding things

- **Something from Home Assistant:** nothing to do here, the ESP sends it as an item.
- **A value or setting:** a line in `proto/plink_values.h` if the ESP should see it, otherwise in `app/values.h`.
  Then a menu row in `ui/ui_menu.c` and the default in `panel_defaults()` (`app/panel.c`). The key is also its id
  in flash.
- **Optional equipment:** a line in `app/equipment.h`. Anything with `.need = FBIT(FEAT_x)` hides without it.
- **A text:** name and English text in `ui/lang.h`, the translation in each `ui/lang_*.c`.
- **A language:** copy `ui/lang_no.c`, add it to `LANGS` in `ui/lang.c` and bump `N_LANGS`
  (`proto/plink_values.h`). `make check` catches missing texts, mismatched placeholders and characters the fonts
  don't have (`TEXT_RANGE` in `tools/mkfonts.py`).
- **An icon:** add it to `ICONS` in `tools/mkfonts.py` and run it. Its place in the list is its id on the link.
- **A message:** a new key in `proto/plink_keys.h`. Unknown keys are ignored. Changing what a key means needs a new
  `PLINK_VERSION`.

## License

MIT, see [LICENSE.md](LICENSE.md). The bundled bits keep their own licenses:

- LVGL (`lib/lvgl`): MIT.
- TJpgDec (`lib/tjpgd`): ChaN's free license, at the top of `tjpgd.c`.
- The fonts (`ui/fonts`): made from Noto Sans and a Nerd Font, SIL Open Font License 1.1, see `ui/fonts/OFL.txt`.
  The icons are Material Design Icons by Pictogrammers, under the Pictogrammers Free License.

Flexit's firmware and the bootloader key aren't in here, and none of this covers them.
