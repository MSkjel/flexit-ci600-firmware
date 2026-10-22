#!/usr/bin/env python3
"""Flash the firmware over SWD with an ST-LINK and pyocd, keeping the stock bootloader.

    pip install pyocd
    python3 tools/flash.py hw/build/ci600_full.bin

Writes from the app header page, which mkimage.py copies from the stock dump, to the end of the firmware, so the
settings pages are kept. A stock dump goes back whole, which clears our settings. The bootloader pages are left
alone. A running panel browns out while flashing, so we turn the backlight off and reset and halt the chip first.
More on that in swd.py.
"""

import argparse
import io
import sys
import time

import layout
import swd
from pyocd.flash.file_programmer import FileProgrammer

# START and APP are offsets into the image
FLASH, START, APP, PAGE = layout.BASE, layout.HEADER - layout.BASE, layout.APP - layout.BASE, layout.PAGE
TIM1_CCR1 = 0x40012C34  # backlight PWM duty
RCC_CSR = 0x40021024  # holds the reset flags


def image_end(full):
    """Where the firmware ends in the 512 KiB image, rounded up to a page. The rest is 0xFF padding."""
    end = len(full)
    while end > APP and full[end - 1] == 0xFF:
        end -= 1
    return (end + PAGE - 1) // PAGE * PAGE


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", help="hw/build/ci600_full.bin, or a stock dump (512 KiB)")
    ap.add_argument("--frequency", type=int, default=1000000, help="SWD clock, Hz")
    args = ap.parse_args()

    full = open(args.image, "rb").read()
    if len(full) != layout.SIZE:
        sys.exit("expected the 512 KiB image (ci600_full.bin)")
    end = image_end(full)
    if FLASH + end > layout.SETTINGS:  # the stock app runs past where our settings start
        end = len(full)
    data = full[START:end]

    with swd.session(args.frequency) as s:
        t = s.target
        if t.read32(0xE0042000) & 0xFFF != 0x414:
            sys.exit("not an STM32F103 high-density part")
        boot = bytes(t.read_memory_block8(FLASH, START))
        if boot != full[:START]:
            print("note: the bootloader on the chip differs from the one in the image (it's left as it is)")
        t.write32(TIM1_CCR1, 0)  # backlight off
        time.sleep(0.05)
        t.reset_and_halt()
        t.write32(swd.DBGMCU_CR, 0x107)  # freeze only the watchdog, see swd.py
        print("writing 0x%08x-0x%08x (%d pages)" % (FLASH + START, FLASH + end, len(data) // PAGE))
        t0 = time.time()
        FileProgrammer(s).program(io.BytesIO(data), file_format="bin", base_address=FLASH + START)
        back = bytes(t.read_memory_block8(FLASH + START, len(data)))
        if back != data:
            bad = next(i for i in range(len(data)) if back[i] != data[i])
            sys.exit("verify failed at 0x%08x" % (FLASH + START + bad))
        print("written and verified in %.1f s" % (time.time() - t0))
        t.write32(RCC_CSR, 1 << 24)  # clear the reset flags so a later brown-out can be seen
        t.reset()
        print("running")


if __name__ == "__main__":
    main()
