#!/usr/bin/env python3
"""Flash a CI600 panel through its stock bootloader, with a USB-RS485 adapter on the bus. Build the .fxup with
mkupdate.py. The bootloader answers at address 1, so nothing else, like the ESP, can be there.

    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 ci600.fxup                # panel at 1, as stock
    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 --panel-addr 3 ci600.fxup
    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 --no-enter ci600.fxup     # already in the bootloader
    uv run --with pyserial tools/fxupload.py --port /dev/ttyUSB0 --status
"""

import argparse
import sys
import time

from mkupdate import parse

BL_ADDR = 1  # the bootloader always uses address 1, whatever the panel address is
FC = 0x64  # function code of all bootloader commands
SUB_ARM, SUB_STATUS, SUB_PIECE, SUB_END, SUB_RESET = 2, 3, 4, 6, 7
PIECE = 240  # stream bytes per piece, 249 at most

# Error codes in the exception reply [01 e4 code crc], sent when a frame is refused
ERRORS = {1: "unknown command", 3: "bad length/value", 6: "wrong state (the loader is busy or not armed)"}
# Loader states, from the status reply [01 64 03 02 state detail crc]
STATES = {
    0: "starting",
    1: "idle",
    2: "ready for a piece",
    3: "working on a piece",
    4: "committing",
    5: "failed",
    6: "piece done, waiting for a poll",
}
# Failure details that come with state 5
DETAILS = {
    1: "the panel's compatibility check rejected the firmware",
    2: "authentication failed (wrong key, corrupt or truncated stream)",
    4: "flash erase failed",
    6: "flash program failed",
    7: "flash verify failed",
    8: "the firmware does not fit in flash",
    9: "the stream ended early",
}


def crc16(data):
    """Modbus CRC-16, low byte first."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return bytes([crc & 0xFF, crc >> 8])


def frame(addr, sub, payload=b""):
    """[addr 0x64 sub len payload crc]"""
    body = bytes([addr, FC, sub, len(payload)]) + payload
    return body + crc16(body)


def find_reply(buf):
    """The first bootloader reply in buf, [01 64 sub len data crc] or [01 e4 code crc], or None. The CX60's polls
    to address 1 and the bootloader's exceptions to those have other function codes."""
    for start in range(len(buf) - 4):
        if buf[start] != BL_ADDR:
            continue
        if buf[start + 1] == FC:
            n = 6 + buf[start + 3]
        elif buf[start + 1] == FC | 0x80:
            n = 5
        else:
            continue
        f = bytes(buf[start : start + n])
        if len(f) == n and crc16(f[:-2]) == f[-2:]:
            return f
    return None


def reply_in(bus, buf):
    """The bootloader's reply in what came back on the bus, or None. Some adapters hear our own frame first, which
    our status poll coming back shows. Then only what comes after it counts."""
    at = buf.find(bus.sent)
    if at >= 0 and bus.sent[2] == SUB_STATUS:
        bus.echo = True
    if bus.echo:
        if at < 0:
            return None  # not all of the echo yet
        buf = buf[at + len(bus.sent) :]
    f = find_reply(buf)
    if f and bus.verbose:
        print("  < " + f.hex())
    return f


class Bus:
    """The serial port. blemu.py has an emulated version with the same methods."""

    def __init__(self, port, baud, verbose):
        import serial

        self.s = serial.Serial(port, baud, timeout=0.05)
        self.verbose = verbose
        self.sent, self.echo = b"", False

    def send(self, f):
        self.s.reset_input_buffer()
        self.s.write(f)
        self.s.flush()
        self.sent = f
        if self.verbose:
            print("  > " + f.hex())

    def read_reply(self, timeout=0.5):
        end = time.time() + timeout
        buf = bytearray()
        while time.time() < end:
            chunk = self.s.read(64)
            if chunk:
                buf += chunk
                f = reply_in(self, buf)
                if f:
                    return f
        return None

    def pause(self, seconds):
        time.sleep(seconds)


def err_of(f):
    """Return an error code if f is an exception reply, else None."""
    if f and len(f) >= 3 and f[1] == FC | 0x80:
        return f[2]
    return None


def do(bus, sub, payload=b"", tries=4, timeout=0.4):
    """Send a 0x64 sub-command and return (reply, error code or None). Resends up to `tries` times, so only
    idempotent commands should use more than one."""
    for _ in range(tries):
        bus.send(frame(BL_ADDR, sub, payload))
        f = bus.read_reply(timeout)
        if f is None:
            continue
        e = err_of(f)
        if e is not None:
            return f, e
        if f[1] == FC and f[2] == sub:
            return f, None
    return None, None


class Failed(Exception):
    pass


def status(bus, tries=4):
    """Read the loader status. Returns (state, detail), or None if it doesn't answer. Raises Failed if something
    else at address 1 refuses the poll."""
    f, e = do(bus, SUB_STATUS, tries=tries)
    if e == 1:
        raise Failed("Something else answers at address 1, like the ESP. Unplug it or give it another address")
    if f is None or e is not None or len(f) < 8:
        return None
    return f[4], f[5]


def detail(d):
    return DETAILS.get(d, "error %d" % d)


def state_text(st):
    s = "state %d (%s)" % (st[0], STATES.get(st[0], "?"))
    return s + ": " + detail(st[1]) if st[0] == 5 else s


def refused(what, f, e):
    return Failed("%s failed: %s" % (what, "no reply" if f is None else "error %d (%s)" % (e, ERRORS.get(e, "?"))))


def wait_state(bus, want, timeout=90.0):
    """Poll until the loader reaches a state in `want`. Polling in state 6 is what moves it on to 2. Raises
    Failed on state 5 or timeout."""
    end = time.time() + timeout
    while time.time() < end:
        st = status(bus, tries=1)
        if st is not None:
            if st[0] in want:
                return st
            if st[0] == 5:
                raise Failed("the panel reports failure: " + detail(st[1]))
        bus.pause(0.02)
    raise Failed("the panel stopped answering (waited for state %s)" % sorted(want))


def piece_taken(bus):
    """After a piece got no reply: True if the loader took it, False if not, None if it can't tell. A status poll
    in state 6 moves the loader on to 2, so once a poll has gone unanswered a 2 could mean either."""
    unanswered = False
    end = time.time() + 30
    while time.time() < end:
        st = status(bus, tries=1)
        if st is None:
            unanswered = True
        elif st[0] == 5:
            raise Failed("the panel reports failure: " + detail(st[1]))
        elif st[0] in (3, 4, 6):
            return True
        elif st[0] == 2:
            return None if unanswered else False
        bus.pause(0.02)
    raise Failed("the panel stopped answering after a piece")


def arm(bus):
    """Start the stream, from its first piece. Also when part of it has been sent already."""
    f, e = do(bus, SUB_ARM)
    if f is None or e:
        raise refused("Arm", f, e)
    wait_state(bus, {2})


def flash(bus, stream):
    """Wait for the bootloader, arm it, send the stream and commit it. Raises Failed."""
    for _ in range(40):  # the loader reports state 0 during its start-up delay
        st = status(bus, tries=1)
        if st is not None and st[0] != 0:
            print("Bootloader is up: " + state_text(st))
            break
        bus.pause(0.3)
    else:
        raise Failed("The panel never answered the bootloader at address 1")

    # The loader only accepts a piece in state 2. The first piece erases the old application, which takes a
    # few seconds with no answer.
    print("Arming...")
    sent, restarts, t0 = 0, 0, time.time()
    arm(bus)
    while sent < len(stream):
        piece = stream[sent : sent + PIECE]
        f, e = do(bus, SUB_PIECE, piece, tries=1)
        if f is None or e is not None:
            taken = piece_taken(bus)
            if taken is None:  # sending it again or not could both break the stream
                if restarts == 3:
                    raise Failed("too many lost replies, can't tell whether a piece arrived")
                restarts += 1
                print("\n  can't tell whether the piece at %d arrived, starting over" % sent)
                sent = 0
                arm(bus)
                continue
            if not taken:
                continue
        sent += len(piece)
        print("\r  %d/%d bytes (%d%%)" % (sent, len(stream), 100 * sent // len(stream)), end="", flush=True)
        wait_state(bus, {2})
    print("\n  streamed in %.1f s" % (time.time() - t0))

    # state 1 after SUB_END means the new firmware is marked valid
    f, e = do(bus, SUB_END, tries=2)
    if f is None or e:
        raise refused("End", f, e)
    st = wait_state(bus, {1}, timeout=30)
    print("Committed: the loader is idle (state %d)" % st[0])


def main(argv):
    ap = argparse.ArgumentParser(description="Flash a CI600 panel over RS-485")
    ap.add_argument("fxup", nargs="?", help="the update stream from tools/mkupdate.py")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--panel-addr", type=int, default=1, help="the running panel's bus address, sent 0x64/2 first (default 1)"
    )
    ap.add_argument("--no-enter", action="store_true", help="the panel is already in the bootloader")
    ap.add_argument("--status", action="store_true", help="just read the bootloader state")
    ap.add_argument("--no-reset", action="store_true", help="don't reset into the new app at the end")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    if not 1 <= a.panel_addr <= 247:
        ap.error("--panel-addr must be 1..247")
    bus = Bus(a.port, a.baud, a.verbose)

    if a.status:
        try:
            st = status(bus)
        except Failed as ex:
            sys.exit(str(ex))
        if st is None:
            sys.exit("No bootloader answered at address 1. A panel running its firmware doesn't answer this")
        print("Bootloader: " + state_text(st))
        return 0
    if not a.fxup:
        sys.exit("give a .fxup file (or --status)")
    stream = open(a.fxup, "rb").read()
    try:
        name = parse(stream)[0]
    except (ValueError, IndexError):
        sys.exit("%s isn't a firmware stream" % a.fxup)
    print("Firmware stream: %d bytes, %s" % (len(stream), name.decode(errors="replace") if name else "no name"))

    try:
        if not a.no_enter:  # the running app reboots into the bootloader on 0x64/2 and doesn't answer
            status(bus, tries=2)  # only a panel already in the bootloader may answer at 1
            print("Entering the bootloader (0x64/2 -> address %d)..." % a.panel_addr)
            bus.send(frame(a.panel_addr, SUB_ARM))
            time.sleep(2.0)
        flash(bus, stream)
    except Failed as ex:
        sys.exit("\n%s. Nothing's broken, just run it again" % ex)

    if not a.no_reset:
        print("Resetting into the new firmware...")
        for _ in range(3):  # the loader can miss it, so check it's gone
            bus.send(frame(BL_ADDR, SUB_RESET))
            time.sleep(0.5)
            st = status(bus, tries=1)
            if st is None or st[0] == 0:
                break
        else:
            sys.exit("The panel didn't restart. Power it off and on, the new firmware is in")
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
