# Flashing

How to get the firmware on the panel, and what to do when it doesn't work.

So far it's only been tested on my own panel. There might be hardware differences between panels that I don't
know about, but I doubt it. If something's off, open an issue.

## Before you start

- Every flash wipes the panel's own settings, like its address, language and theme. If the ESP flashes a panel
  that already runs this firmware, it saves them first and sends them back when the panel starts again, and the
  panel restarts once more to take them. The ESP only keeps them in RAM, so if it restarts in between, the panel
  starts from defaults. From stock or over USB you start from defaults. Fan speeds, setpoint and the week program
  live in the unit and aren't touched.
- Only stock 7.x has been tested. The ESP checks that for you. Over USB, look under Settings > Advanced user
  (PIN 1000) > Service (PIN 8888) > System information, at SW ADVANCED PANEL.
- Going back to stock isn't easy, see [Going back to stock](../README.md#going-back-to-stock).
- Nothing else can be at address 1 while you flash, the [bootloader](hardware.md#the-bootloader) answers there.

## From Home Assistant

1. Set up the ESP with `packages/panel.yaml` and `packages/panel/update.yaml`, see
   [the ESP docs](https://github.com/MSkjel/esphome-flexit-modbus-server/blob/panel/docs/panel.md#setup).
2. A stock panel is at address 1, so put the ESP at 2 (`flexit_address: "2"`). If the panel already runs this
   firmware, the ESP stays at 1.
3. Press Flash Panel Firmware and watch Panel Firmware Update. It takes a few minutes, with the screen dark and
   all three lights on.
4. From stock, the new panel comes up at 3, so move the ESP to 1.
5. Restart the ventilation unit (power off and on).
6. From stock, do the [first setup](../README.md#first-setup).

The ESP fetches the latest release by itself. To flash another file, put its URL in Panel Firmware URL, like
`http://homeassistant.local:8123/local/ci600.fxup` for a file in `/config/www`, or run
`python3 tools/fxserve.py ci600.fxup` and use the URL it prints. When the ESP restarts the box goes back to
update.yaml's `url` var, which is the latest release unless you set it.

Before anything is flashed the ESP asks the panel what it runs. It flashes a stock panel on 7.x, and this
firmware if the file is another version. Anything else gets refused, and Panel Firmware Update says why. Panel
Firmware Force also flashes the same version again, or a stock panel that isn't 7.x, but with another stock
version you're on your own.

## With a USB RS-485 adapter

If the ESP doesn't work out, a USB RS-485 adapter does the same thing from a PC. Grab `ci600.fxup` from the
[latest release](https://github.com/MSkjel/flexit-ci600-firmware/releases/latest). Hook A, B and GND up to the bus
like the ESP. The panel stays powered by the unit.

    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 ci600.fxup

- `--port COM3` or so on Windows
- `--panel-addr N` if the panel isn't at 1
- `--status` shows what the bootloader is doing
- `--no-enter` if the panel is already in the bootloader

This one doesn't check the version, so check it yourself first. When it's done, restart the ventilation unit and
do the [first setup](../README.md#first-setup).

## When it doesn't work

**It stopped halfway, the screen is dark and all three lights are on.** The panel is in the bootloader, which
won't start anything it hasn't fully checked, and waits for another try. Press Flash Panel Firmware again and the
ESP picks it up there, before any other panel. Or use `fxupload.py --no-enter`.

**The ESP says something else is at address 1.** Take that off the bus, or give it another address.

**No panel answers.** Check the wiring and the ESP's address. Restart the unit and try again.

**The panel doesn't start and doesn't answer on the bus at all.** Force it into the bootloader, see
[Forcing the bootloader](hardware.md#forcing-the-bootloader), and flash again.

**Still stuck.** Open an issue with what Panel Firmware Update or `fxupload.py -v` said.
