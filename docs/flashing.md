# Flashing

The short version is in the [README](../README.md#installing). This one has a bit more, and what to do when it
doesn't work.

So far it's only been tested on my own panel. There might be hardware differences between panels that I don't
know about, but I doubt it. If something's off, open an issue.

## Before you start

- Every flash wipes the panel's own settings, like its address, language and theme. Fan speeds, setpoint and
  the week program live in the unit and aren't touched.
- Only stock 7.x has been tested. The ESP checks that for you. Over USB, look under Settings > Advanced user
  (PIN 1000) > Service (PIN 8888) > System information, at SW ADVANCED PANEL.
- Going back to stock isn't easy, see [Read protection](hardware.md#read-protection).
- The bootloader always answers at address 1, so nothing else can be at 1 while you flash.

## From Home Assistant

1. Add `packages/panel.yaml` and `packages/panel/update.yaml` to the ESP config, see
   [the ESP docs](https://github.com/MSkjel/esphome-flexit-modbus-server/blob/panel/docs/panel.md).
2. For now the ESP needs `flexit_ref: panel` and `modbus_library_ref: dev`. The dev library switches the
   direction pin in hardware, which the bootloader needs (see [The bootloader](hardware.md#the-bootloader)). If
   you've built with `main` before, do a Clean Build Files, or ESPHome keeps the old library.
3. A stock panel is at address 1, so put the ESP at 2 (`flexit_address: "2"`).
4. Press Flash Panel Firmware and watch Panel Firmware Update. It takes a few minutes.
5. The new panel comes up at 3. Move the ESP to 1 and restart the ventilation unit. It only looks for panels when
   it starts.
6. Do the [first setup](../README.md#first-setup).

Before anything is flashed the ESP asks the panel what it runs. It flashes a stock panel on 7.x, and this
firmware if the file is another version. Anything else gets refused, and Panel Firmware Update says why. Panel
Firmware Force skips the check, but with a stock version other than 7.x you're on your own.

## With a USB RS-485 adapter

If the ESP doesn't work out, a USB RS-485 adapter does the same thing from a PC. Hook A, B and GND up to the bus
like the ESP. The panel stays powered by the unit. Take the ESP off the bus, or at least off address 1.

    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 ci600.fxup

- `--panel-addr N` if the panel isn't at 1
- `--status` shows what the bootloader is doing
- `--no-enter` if the panel is already in the bootloader

This one doesn't check the version, so check it yourself first.

## When it doesn't work

**It stopped halfway, the screen is dark and all three lights are on.** The panel is in the bootloader and
waits for another try. Press Flash Panel Firmware again, or use `fxupload.py --no-enter`.

**The ESP says something else is at address 1.** Take that off the bus, or give it another address.

**No panel answers.** Check the wiring and the ESP's address. Restart the unit and try again.

**It's done, but the panel says "No contact with the unit".** Restart the ventilation unit.

**The panel doesn't start and doesn't answer on the bus at all.** Force it into the bootloader, see
[Forcing the bootloader](hardware.md#forcing-the-bootloader), and flash again.

**Still stuck.** Open an issue with what Panel Firmware Update or `fxupload.py -v` said.
