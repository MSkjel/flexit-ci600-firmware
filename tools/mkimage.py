#!/usr/bin/env python3
"""Build a full 512 KiB flash image. The stock bootloader and app header come from a stock dump, then this
firmware, and the rest is erased. Flash it with flash.py or st-flash write ci600_full.bin 0x08000000.

    mkimage.py STOCK_DUMP APP_BIN OUT
"""

import struct
import sys

import layout

APP, HEADER, END = layout.APP - layout.BASE, layout.HEADER - layout.BASE, layout.SETTINGS - layout.BASE
stock, app_path, out = sys.argv[1:4]
boot = open(stock, "rb").read()
# APP + 4 in the dump is the stock app's reset vector, 0x0800ED95
if len(boot) != layout.SIZE or boot[APP + 4 : APP + 8] != bytes.fromhex("95ed0008"):
    sys.exit("%s isn't the stock CI600 dump this was made for" % stock)
if struct.unpack_from("<I", boot, HEADER + 4)[0] != layout.APP_MARK:
    sys.exit("%s has no app header mark: the bootloader wouldn't start the firmware" % stock)
app = open(app_path, "rb").read()
if APP + len(app) > END:
    sys.exit("firmware too big: %d bytes, %d fit" % (len(app), END - APP))
img = bytearray(b"\xff" * layout.SIZE)
img[:APP] = boot[:APP]
img[APP : APP + len(app)] = app
open(out, "wb").write(img)
print("%s: bootloader + %d bytes of firmware (%.0f%% of the space)" % (out, len(app), 100 * len(app) / (END - APP)))
