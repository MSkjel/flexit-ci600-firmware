#!/usr/bin/env python3
"""Build a .fxup update stream for the CI600's stock bootloader from the application image.

uv run --with cryptography tools/mkupdate.py --stock DUMP hw/build/ci600.bin ci600.fxup
uv run --with cryptography tools/mkupdate.py --stock DUMP --verify ci600.fxup hw/build/ci600.bin

The bootloader's key isn't in this repo. --stock reads it from a stock flash dump, or set CI600_FXUP_KEY to it
(64 hex digits). CI has it as the CI600_FXUP_KEY secret. The stream's name is "ci600 X.Y.Z", the version the image
was built with, from the version file the build writes next to it. The ESP reads it to tell the file's version
from the panel's.
"""

import argparse
import hashlib
import os
import re
import struct
import sys
from pathlib import Path

import layout

# AES-SIV key, S2V key then CTR key. It's in the stock bootloader at KEY_AT, same on every CI600. Only its hash is
# here, to tell the right key from a wrong one.
KEY_AT = 0x1BE6
KEY_SHA256 = "858c7eced24b95c7363d3de2190613e312c1f438cad79e5c8d4a059e3ffe7d82"

APP_BASE, APP_END = layout.APP, layout.SETTINGS  # the bootloader writes the image from APP_BASE
NAME_MAX = 0xEF  # longest name in field 1 the bootloader accepts

# The stream is a protobuf message:
#   field 1  name, the bootloader stores it at 0x08002010 and returns it for sub-command 8
#   field 2  guard program
#   field 3  SIV || ciphertext of the image, with fields 1 and 2 as associated data
#
# The guard is a list of varints. An even v pushes v >> 1, an odd v runs operator v >> 1. It passes if it leaves
# exactly one value, 1. The bootloader runs it before erasing anything and rejects the image with detail 1 if it
# fails. Ours checks the CI600 bootloader's identity fields 3=500, 5=97 and 7=3. Operator 18 reads a bootloader
# field with a default, 7 is equal and 9 is and.


def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)


def ident_guard():
    def push(v):
        return varint(v << 1)

    def op(o):
        return varint((o << 1) | 1)

    def field_is(f, v):
        return push(f) + push(0) + op(18) + push(v) + op(7)

    return field_is(3, 500) + field_is(5, 97) + op(9) + field_is(7, 3) + op(9)


GUARD = ident_guard()


def pb_bytes(field, data):
    return varint(field << 3 | 2) + varint(len(data)) + data


def built_version(app_path):
    """X.Y.Z from the version file next to the image, which config.mk writes with every build."""
    path = Path(app_path).with_name("version")
    try:
        version = path.read_text().strip()
    except OSError:
        sys.exit("no %s: give the image the firmware build made, hw/build/ci600.bin" % path)
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit("%s holds %r, not X.Y.Z" % (path, version))
    return version


def name_version(name):
    """The version in a stream name, "ci600 X.Y.Z", or None."""
    m = re.fullmatch(rb"ci600 (\d+\.\d+\.\d+)", name or b"")
    return m.group(1).decode() if m else None


def is_key(key):
    return hashlib.sha256(key).hexdigest() == KEY_SHA256


def key_from(stock=None):
    """The key from a stock dump if there is one, else from CI600_FXUP_KEY. Exits if it isn't there."""
    if stock:
        key, where = open(stock, "rb").read()[KEY_AT : KEY_AT + 32], "%s at 0x%x" % (stock, KEY_AT)
    else:
        try:
            key = bytes.fromhex(os.environ.get("CI600_FXUP_KEY", ""))
        except ValueError:
            key = b""
        where = "CI600_FXUP_KEY"
    if not is_key(key):
        sys.exit("no CI600 update key in %s: give a stock flash dump with --stock, or set CI600_FXUP_KEY" % where)
    return key


def aessiv(key):
    from cryptography.hazmat.primitives.ciphers.aead import AESSIV

    return AESSIV(key)


def check_app(app):
    """Raise ValueError if the panel couldn't run this image."""
    room = APP_END - APP_BASE
    if len(app) > room:
        raise ValueError("application is %d bytes, only %d fit below the settings pages" % (len(app), room))
    if len(app) < 8:
        raise ValueError("application is too small to hold a vector table")
    sp, pc = struct.unpack("<II", app[:8])
    if not (0x20000000 < sp <= 0x20010000):
        raise ValueError("initial stack pointer 0x%08x is not in RAM: is this linked for 0x%08x?" % (sp, APP_BASE))
    if not (APP_BASE <= pc < APP_BASE + len(app)) or not pc & 1:
        raise ValueError(
            "reset vector 0x%08x is not a Thumb address inside the image: is this linked for 0x%08x?" % (pc, APP_BASE)
        )


def build(key, app, name):
    check_app(app)
    if len(name) > NAME_MAX:
        raise ValueError("name is %d bytes, the bootloader takes at most %d" % (len(name), NAME_MAX))
    blob = aessiv(key).encrypt(app, [name, GUARD])  # SIV(16) || ciphertext
    return pb_bytes(1, name) + pb_bytes(2, GUARD) + pb_bytes(3, blob)


def parse(stream):
    """Split a stream into (name, guard, blob). Raises ValueError unless field 3 ends where the stream does: a
    cut-off download is refused, and like the bootloader nothing may follow it."""
    fields = {}
    i = 0
    while 3 not in fields:
        if i == len(stream):
            raise ValueError("the stream ends before field 3")
        key, i = read_varint(stream, i)
        if key & 7 != 2 or key >> 3 not in (1, 2, 3) or key >> 3 in fields:
            raise ValueError("unexpected protobuf key 0x%x before offset %d" % (key, i))
        n, i = read_varint(stream, i)
        if i + n > len(stream):
            raise ValueError("field %d runs %d bytes past the end of the stream" % (key >> 3, i + n - len(stream)))
        fields[key >> 3] = stream[i : i + n]
        i += n
    if i != len(stream):
        raise ValueError("%d bytes after field 3" % (len(stream) - i))
    return fields.get(1, b""), fields.get(2, b""), fields[3]  # a stream not from here may lack 1 and 2


def read_varint(buf, i):
    v = s = 0
    while True:
        if i >= len(buf):
            raise ValueError("the stream ends inside a varint")
        b = buf[i]
        i += 1
        v |= (b & 0x7F) << s
        if not b & 0x80:
            return v, i
        s += 7


def decrypt(key, stream):
    """The image in a stream. Raises if it isn't one or doesn't authenticate."""
    name, guard, blob = parse(stream)
    return aessiv(key).decrypt(blob, [name, guard])


def verify(key, stream, app):
    """Print what's in the stream, True if it decrypts to app."""
    name, guard, blob = parse(stream)
    image = decrypt(key, stream)
    print("name   : %r" % name)
    print("version: %s" % (name_version(name) or "none"))
    print("guard  : %s" % guard.hex())
    print("stream : %d bytes, %d ciphertext" % (len(stream), len(blob)))
    print("verify : %s (%d plaintext bytes, authenticates)" % ("OK" if image == app else "MISMATCH", len(image)))
    return image == app


def main(argv):
    ap = argparse.ArgumentParser(description="Build a .fxup update stream for the CI600's stock bootloader")
    ap.add_argument("app", help="the application image, hw/build/ci600.bin")
    ap.add_argument("out", nargs="?", help="the stream to write")
    ap.add_argument("--stock", metavar="DUMP", help="a stock flash dump to take the key from, else $CI600_FXUP_KEY")
    ap.add_argument("--verify", metavar="FXUP", help="check that FXUP decrypts to the image instead")
    a = ap.parse_args(argv)
    if (a.out is None) == (a.verify is None):
        ap.error("give either OUT or --verify FXUP")
    key = key_from(a.stock)
    app = open(a.app, "rb").read()
    if a.verify:
        return 0 if verify(key, open(a.verify, "rb").read(), app) else 1

    try:
        stream = build(key, app, ("ci600 " + built_version(a.app)).encode())
    except ValueError as e:
        sys.exit("refusing to build: %s" % e)
    # check that the stream decrypts back to the image before anyone flashes it
    if not verify(key, stream, app):
        sys.exit("the stream doesn't decrypt back to the image")
    open(a.out, "wb").write(stream)
    print("%s: %d bytes of firmware -> %d-byte stream" % (a.out, len(app), len(stream)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
