#!/usr/bin/env python3
"""Press keys on the panel over SWD and print redraw times, CPU load and unused stack from PERF in
hw/main.c.

    python3 tools/perf.py ok down down down back      # keys: up down ok back help home
    python3 tools/perf.py                             # just the counters
"""

import struct
import subprocess
import sys
import time

import swd

KEYS = {"up": 1, "down": 2, "ok": 3, "back": 4, "help": 5, "home": 6}
ELF = __file__.rsplit("/", 2)[0] + "/hw/build/ci600.elf"


def symbol(name):
    """Return (address, size) of a symbol in the ELF."""
    out = subprocess.run(["arm-none-eabi-nm", "-S", ELF], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] == name:
            return int(p[0], 16), int(p[1], 16)
    sys.exit("no %s in %s" % (name, ELF))


def main():
    (perf, size), (dbg, _) = symbol("PERF"), symbol("dbg_key")
    if size != 32:
        sys.exit("PERF is %d bytes, this script reads 8 words: update both" % size)
    with swd.session() as s:
        t = s.target

        def read():
            return struct.unpack("<8I", bytes(t.read_memory_block8(perf, 32)))

        frames, last, mx, flush, px, keys, busy, stack = read()
        print(
            "so far: %d redraws, slowest %.1f ms, %d keys; awake %.1f %% of the last second, %d B of stack never used"
            % (frames, mx / 1000, keys, busy / 10, stack)
        )
        for name in sys.argv[1:]:
            before = read()[0]
            t.write8(dbg, KEYS[name])
            t0 = time.time()
            seen = []
            while time.time() - t0 < 1.5:  # collect every redraw the key causes over 1.5 s
                f = read()
                if f[0] != before:
                    seen.append(f)
                    before = f[0]
                time.sleep(0.01)
            if not seen:
                print("%-5s no redraw" % name)
            for f in seen:
                print(
                    "%-5s %6.1f ms  (sending %5.1f ms, %5d px = %3d%% of the screen)"
                    % (name, f[1] / 1000, f[3] / 1000, f[4], f[4] * 100 // (320 * 240))
                )
                name = ""


if __name__ == "__main__":
    main()
