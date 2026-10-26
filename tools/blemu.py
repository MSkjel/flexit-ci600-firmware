#!/usr/bin/env python3
"""Emulate the stock CI600 bootloader and flash a .fxup into it, using the same sequence as fxupload.py.

    uv run --with unicorn --with cryptography tools/blemu.py --stock DUMP [-v] [--echo] [FXUP | --app BIN]

DUMP is a stock CI600 flash dump (default $FLEXIT_STOCK). Without FXUP the stream is built from BIN
(default hw/build/ci600.bin). --echo acts like an adapter that hears its own frames. Exits 0 if the flash matches
and 1 on any failure.
"""

import argparse
import importlib.util
import os
import struct
import sys
from pathlib import Path

import fxupload as fx
import layout
import mkupdate

try:
    from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
    from unicorn import arm_const as arm
except ImportError:  # main() says so
    Uc = None

FLASH, FLASH_SZ = layout.BASE, layout.SIZE
RAM, RAM_SZ = 0x20000000, 0x10000
APP_BASE, APP_MAGIC_ADDR, APP_MAGIC = layout.APP, layout.HEADER + 4, layout.APP_MARK
U = 0x40013800  # USART1
RCC = 0x40021000
FLASHR = 0x40022000
BKP_DR1 = 0x40006C04  # backup register 1
# Addresses inside the stock bootloader
LOOP_TOP = 0x08000A4A
IRQ = 0x08000154
SENTINEL = 0x08000002  # return address for the IRQ handler, emu_start stops before running it
RXBUF, RXCNT = 0x20000828, 0x20000174  # RX buffer and byte count
STATE = 0x2000072C  # loader state, one byte
DISPATCH = (0x01DA, 0x01DC, 0x01DE, 0x01E0, 0x01E2, 0x02A2)  # cases of the sub-command switch, traced with -v


class Boot:
    def __init__(self, img):
        self.regs = [getattr(arm, "UC_ARM_REG_R%d" % i) for i in range(13)]
        self.regs += [arm.UC_ARM_REG_SP, arm.UC_ARM_REG_LR, arm.UC_ARM_REG_PC, arm.UC_ARM_REG_CPSR]
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M3)
        uc.mem_map(FLASH, FLASH_SZ)
        uc.mem_write(FLASH, b"\xff" * FLASH_SZ)
        uc.mem_write(FLASH, img)
        uc.mem_map(RAM, RAM_SZ)
        uc.mem_map(0x40006000, 0x2000)
        uc.mem_map(0x40010000, 0x3000)
        uc.mem_map(0x40020000, 0x1000)
        uc.mem_map(0xE0000000, 0x100000)
        uc.mem_map(0x42000000, 0x2000000)
        uc.mem_write(BKP_DR1, struct.pack("<I", 0xDEAD))  # stay in the bootloader
        rcc = {0: 0x0303C583, 4: 0x001D840A}

        def rcc_read(uc, off, size, data):
            if off == 0:
                return rcc[0] | 0x02020002  # HSE and PLL ready
            if off == 4:
                return (rcc[4] & ~0xC) | ((rcc[4] & 3) << 2)  # SWS follows SW
            return rcc.get(off, 0)

        uc.mmio_map(RCC, 0x1000, rcc_read, None, lambda uc, off, size, v, data: rcc.__setitem__(off, v), None)
        self.flash_regs = {}
        uc.mmio_map(FLASHR, 0x1000, self._flash_read, None, self._flash_write, None)
        self.usart = {"sr": 0x00C0, "tx": [], "reg": {}}
        uc.mmio_map(U & ~0xFFF, 0x1000, self._usart_read, None, self._usart_write, None)
        self.img = img

    # Fake flash controller, never busy and never fails. STRT with PER set erases the page at AR.
    def _flash_read(self, uc, off, size, data):
        return self.flash_regs.get(off, 0)

    def _flash_write(self, uc, off, size, value, data):
        if off == 0x10 and value & 0x42 == 0x42:
            page = self.flash_regs.get(0x14, 0) & ~0x7FF
            if FLASH <= page < FLASH + FLASH_SZ:
                uc.mem_write(page, b"\xff" * 0x800)
            value &= ~0x40
        self.flash_regs[off] = value

    def _usart_read(self, uc, off, size, data):
        r = off - (U & 0xFFF)
        return self.usart["sr"] if r == 0 else self.usart["reg"].get(r, 0)

    def _usart_write(self, uc, off, size, value, data):
        r = off - (U & 0xFFF)
        if r == 4:
            self.usart["tx"].append(value & 0xFF)
        else:
            self.usart["reg"][r] = value

    def read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def state(self):
        return self.read(STATE, 1)[0]

    def _save(self):
        return [self.uc.reg_read(r) for r in self.regs]

    def _load(self, ctx):
        for r, v in zip(self.regs, ctx):
            self.uc.reg_write(r, v)

    def boot_to_loop(self):
        """Run from reset until the main loop has gone round three times. Returns the loader state."""
        hits = [0]

        def hook(uc, addr, size, data):
            hits[0] += 1
            if hits[0] >= 3:
                uc.emu_stop()

        h = self.uc.hook_add(UC_HOOK_CODE, hook, begin=LOOP_TOP, end=LOOP_TOP)
        sp, reset = struct.unpack_from("<II", self.img, 0)
        self.uc.reg_write(arm.UC_ARM_REG_SP, sp)
        self.uc.emu_start(reset | 1, 0, count=40_000_000)
        self.uc.hook_del(h)
        self.ctx = self._save()  # CPU state at LOOP_TOP, each frame resumes from here
        return self.state()

    def deliver(self, frame, trace=False):
        """Feed one frame through the loader's IRQ handler, run the main loop until the state settles, and
        return the loader's reply bytes."""
        uc = self.uc
        self.usart["tx"] = []
        # do what the RXNE path would have done, bytes in the buffer and IDLEIE set, then raise IDLE
        uc.mem_write(RXBUF, bytes(frame) + b"\x00" * (-len(frame) % 4))
        uc.mem_write(RXCNT, struct.pack("<H", len(frame)))
        self.usart["reg"][0xC] = self.usart["reg"].get(0xC, 0) | 0x10
        self.usart["sr"] = 0x00D0  # IDLE | TXE | TC
        self._load(self.ctx)
        uc.reg_write(arm.UC_ARM_REG_SP, 0x2000FE00)
        uc.reg_write(arm.UC_ARM_REG_LR, SENTINEL | 1)
        seen = []

        def trace_hook(uc, addr, size, data):
            if addr & 0xFFFF in DISPATCH:
                r1, r2 = uc.reg_read(arm.UC_ARM_REG_R1), uc.reg_read(arm.UC_ARM_REG_R2)
                seen.append("%04x[r1=%d,r2=%d]" % (addr & 0xFFFF, r1, r2))

        th = None
        if trace:
            uc.ctl_remove_cache(IRQ, FLASH + max(DISPATCH) + 2)  # flush, or a new hook misses code translated earlier
            th = uc.hook_add(UC_HOOK_CODE, trace_hook, begin=IRQ, end=FLASH + max(DISPATCH))
        uc.emu_start(IRQ | 1, SENTINEL, count=2_000_000)
        if th is not None:
            uc.hook_del(th)
            print("  dispatch: " + " ".join(seen))
        reply = bytes(self.usart["tx"])

        # run the main loop until the state has been the same for 30 passes
        self._load(self.ctx)
        self.usart["sr"] = 0x00C0
        info = {"hits": 0, "state": -1, "stable": 0}

        def hook(uc, addr, size, data):
            info["hits"] += 1
            if info["hits"] == 1:
                return  # skip the LOOP_TOP we start on
            s = self.state()
            if s == info["state"]:
                info["stable"] += 1
            else:
                info["state"], info["stable"] = s, 0
            if info["stable"] >= 30 or info["hits"] > 4000:
                uc.emu_stop()

        h = uc.hook_add(UC_HOOK_CODE, hook, begin=LOOP_TOP, end=LOOP_TOP)
        uc.emu_start(uc.reg_read(arm.UC_ARM_REG_PC) | 1, 0, count=60_000_000)
        uc.hook_del(h)
        self.ctx = self._save()
        return reply


class EmuBus:
    """fxupload.Bus for the emulator, echoing if asked. Each frame is fully handled before send() returns."""

    def __init__(self, boot, verbose, echoes=False):
        self.boot, self.verbose, self.echoes = boot, verbose, echoes
        self.reply = b""
        self.traced = False
        self.sent, self.echo = b"", False

    def send(self, f):
        trace = self.verbose and f[2] == fx.SUB_PIECE and not self.traced
        self.traced |= trace
        if self.verbose:
            print("  > " + f.hex())
        self.sent = f
        reply = self.boot.deliver(f, trace=trace)
        e = fx.err_of(fx.find_reply(reply))
        if f[2] == fx.SUB_PIECE and e is not None:  # fxupload never sends a piece the loader should refuse
            raise fx.Failed("a piece was refused: error %d (%s)" % (e, fx.ERRORS.get(e, "?")))
        self.reply = (f if self.echoes else b"") + reply

    def read_reply(self, timeout=0.5):
        f = fx.reply_in(self, self.reply)
        self.reply = b""
        return f

    def pause(self, seconds):
        pass


def main(argv):
    ap = argparse.ArgumentParser(description="Flash a .fxup into the vendor CI600 bootloader, emulated")
    ap.add_argument("fxup", nargs="?", help="the update stream (default: built from --app)")
    ap.add_argument("--stock", default=os.environ.get("FLEXIT_STOCK"), help="stock CI600 dump ($FLEXIT_STOCK)")
    ap.add_argument("--app", type=Path, default=layout.ROOT / "hw" / "build" / "ci600.bin", help="application image")
    ap.add_argument("--echo", action="store_true", help="the adapter hears its own frames, as some do")
    ap.add_argument("-v", "--verbose", action="store_true", help="log frames and the first piece's dispatch")
    a = ap.parse_args(argv)

    if not a.stock:
        ap.error("no stock dump: give --stock or set FLEXIT_STOCK")
    if not Path(a.stock).is_file():
        sys.exit("no such file: %s" % a.stock)
    key = mkupdate.key_from(a.stock)
    if Uc is None or importlib.util.find_spec("cryptography") is None:
        sys.exit("needs unicorn and cryptography: uv run --with unicorn --with cryptography tools/blemu.py ...")
    if a.fxup:
        stream = Path(a.fxup).read_bytes()
    else:
        try:
            stream = mkupdate.build(key, a.app.read_bytes(), b"ci600")
        except (OSError, ValueError) as e:
            sys.exit("can't build a stream from %s: %s" % (a.app, e))

    boot = Boot(Path(a.stock).read_bytes())
    print("booted: state %d" % boot.boot_to_loop())
    try:
        fx.flash(EmuBus(boot, a.verbose, a.echo), stream)
    except fx.Failed as e:
        print("\nFAILED: %s" % e)
        return 1

    try:
        image = mkupdate.decrypt(key, stream)
    except Exception as e:  # the loader accepted a stream we can't decrypt, also a failure
        print("FAILED: can't decrypt the stream to compare (%r)" % e)
        return 1
    flashed = boot.read(APP_BASE, len(image))
    mark = struct.unpack("<I", boot.read(APP_MAGIC_ADDR, 4))[0]
    print("flash matches the %d-byte image: %s" % (len(image), flashed == image))
    print("valid mark at 0x%08x: 0x%08x (%s)" % (APP_MAGIC_ADDR, mark, "ok" if mark == APP_MAGIC else "MISSING"))
    if flashed != image:
        i = next(i for i in range(len(image)) if flashed[i] != image[i]) & ~15
        got, want = flashed[i : i + 16].hex(), image[i : i + 16].hex()
        print("  first diff at 0x%08x: flash %s, want %s" % (APP_BASE + i, got, want))
    return 0 if flashed == image and mark == APP_MAGIC else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
