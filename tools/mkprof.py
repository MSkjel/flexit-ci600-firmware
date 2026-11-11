#!/usr/bin/env python3
"""Put the profiler's function table just after a debug build's image, where hw/prof.c looks for it.

    python3 tools/mkprof.py hw/build/prof/ci600.elf hw/build/prof/ci600.bin    # make -C hw PROF=1 does it

The table is "PROF", how many functions, where each starts going up, the offset of each one's name (2 bytes each),
then the names. A name that's used more than once has its file after it.
"""

import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

import layout

MAGIC = 0x464F5250  # "PROF"


def symbols(elf):
    """(address, size, type, name, file) of each symbol, by address."""
    out = subprocess.run(
        ["arm-none-eabi-nm", "-n", "-S", "-l", "--defined-only", elf], capture_output=True, text=True, check=True
    ).stdout
    for line in out.splitlines():
        sym, _, where = line.partition("\t")
        p = sym.split()
        if len(p) == 3:
            p.insert(1, "0")
        yield int(p[0], 16), int(p[1], 16), p[2], p[3], Path(where.rpartition(":")[0]).name


def main():
    elf, image = sys.argv[1], Path(sys.argv[2])
    syms = list(symbols(elf))
    addr = {s[3]: s[0] for s in syms}
    end = addr["_sidata"] + addr["_edata"] - addr["_sdata"]  # .data's copy is the image's last part
    data = image.read_bytes()
    if layout.APP + len(data) != end:
        sys.exit("%s isn't the image of %s by itself" % (image, elf))
    # those without a size too, like libgcc's __aeabi_ldivmod: its time would go to the function before it
    funcs = [s for s in syms if s[2] in "tTwW" and layout.APP <= s[0] < end]
    twice = Counter(s[3] for s in funcs)
    names = [s[3] + (" (%s)" % s[4] if twice[s[3]] > 1 and s[4] else "") for s in funcs]
    text, offsets = b"", []
    for n in names:
        offsets.append(len(text))
        text += n.encode() + b"\0"
    if len(text) > 0xFFFF:
        sys.exit("the names take %d bytes, prof.c's offsets 2" % len(text))
    n = len(funcs)
    table = struct.pack("<II%dI%dH" % (n, n), MAGIC, n, *(s[0] for s in funcs), *offsets) + text
    if end + len(table) > layout.SETTINGS:
        sys.exit("the table takes %d bytes, %d are left" % (len(table), layout.SETTINGS - end))
    image.write_bytes(data + table)
    print("profiler: %d functions, %d bytes, %d left" % (n, len(table), layout.SETTINGS - end - len(table)))


if __name__ == "__main__":
    main()
