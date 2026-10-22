#!/usr/bin/env python3
"""Build a .fxup update stream for the CI600's stock bootloader from the application image.

uv run --with cryptography tools/mkupdate.py --stock DUMP hw/build/ci600.bin ci600.fxup [--version X.Y.Z] [--no-guard]
uv run --with cryptography tools/mkupdate.py --stock DUMP --verify ci600.fxup hw/build/ci600.bin

The bootloader's key isn't in this repo. --stock reads it from a stock flash dump, or set CI600_FXUP_KEY to it
(64 hex digits). CI has it as the CI600_FXUP_KEY secret. The stream's name is "ci600 X.Y.Z", the version config.mk
builds in unless --version says otherwise. The ESP reads it to tell the file's version from the panel's.
"""

import hashlib
import os
import re
import struct
import subprocess
import sys

import layout

# AES-SIV key, S2V key then CTR key. It's in the stock bootloader at KEY_AT, same on every CI600. Only its hash is
# here, to tell the right key from a wrong one.
KEY_AT = 0x1BE6
KEY_SHA256 = "858c7eced24b95c7363d3de2190613e312c1f438cad79e5c8d4a059e3ffe7d82"

APP_BASE, APP_END = layout.APP, layout.SETTINGS  # the bootloader writes the image from APP_BASE
NAME_MAX = 0xEF  # longest name in field 1 the bootloader accepts
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# The stream is a protobuf message:
#   field 1  name, the bootloader stores it at 0x08002010 and returns it for sub-command 8
#   field 2  guard program
#   field 3  SIV || ciphertext of the image, with fields 1 and 2 as associated data
#
# The guard is a list of varints. An even v pushes v >> 1, an odd v runs operator v >> 1. It passes if it leaves
# exactly one value, 1. The bootloader runs it before erasing anything and rejects the image with detail 1 if it
# fails. The default guard checks the CI600 bootloader's identity fields 3=500, 5=97 and 7=3. Operator 18 reads
# a bootloader field with a default, 7 is equal and 9 is and. b"\x02" just pushes 1 and accepts any panel.
GUARD_ACCEPT_ALL = b"\x02"


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


def tag_version():
    """X.Y.Z like config.mk: the newest vX.Y.Z tag, the next patch level on a later commit, 0.0.0 without one."""
    tag = subprocess.run(
        ["git", "-C", ROOT, "describe", "--tags", "--match", "v[0-9]*.[0-9]*.[0-9]*", "--exclude", "v*[!0-9.]*"],
        capture_output=True,
        text=True,
    ).stdout.strip()
    if not tag:
        return "0.0.0"
    v = re.split(r"[.-]", tag[1:])
    return "%s.%s.%d" % (v[0], v[1], int(v[2]) + (len(v) > 3))


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


def encrypt(key, plaintext, name, guard):
    from cryptography.hazmat.primitives.ciphers.aead import AESSIV

    return AESSIV(key).encrypt(plaintext, [name, guard])  # SIV(16) || ciphertext


def decrypt(key, blob, name, guard):
    from cryptography.hazmat.primitives.ciphers.aead import AESSIV

    return AESSIV(key).decrypt(blob, [name, guard])


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


def build(key, app, name, guard=True, check=True):
    if check:
        check_app(app)
    if len(name) > NAME_MAX:
        raise ValueError("name is %d bytes, the bootloader takes at most %d" % (len(name), NAME_MAX))
    program = GUARD if guard else GUARD_ACCEPT_ALL
    return pb_bytes(1, name) + pb_bytes(2, program) + pb_bytes(3, encrypt(key, app, name, program))


def parse(stream):
    """Split a stream into (name, guard, blob). Like the bootloader, rejects anything after field 3."""
    fields = {}
    i = 0
    while i < len(stream):
        key, i = read_varint(stream, i)
        if 3 in fields or key & 7 != 2 or key >> 3 not in (1, 2, 3) or key >> 3 in fields:
            raise ValueError("unexpected protobuf key 0x%x before offset %d" % (key, i))
        n, i = read_varint(stream, i)
        fields[key >> 3] = stream[i : i + n]
        i += n
    return fields.get(1), fields.get(2), fields.get(3)


def read_varint(buf, i):
    v = s = 0
    while True:
        b = buf[i]
        i += 1
        v |= (b & 0x7F) << s
        if not b & 0x80:
            return v, i
        s += 7


def main(argv):
    stock = None
    if "--stock" in argv:
        i = argv.index("--stock")
        if i + 1 == len(argv):
            sys.exit(__doc__)
        stock = argv[i + 1]
        argv = argv[:i] + argv[i + 2 :]
    key = key_from(stock)
    if argv and argv[0] == "--verify":
        if len(argv) != 3:
            sys.exit(__doc__)
        stream = open(argv[1], "rb").read()
        app = open(argv[2], "rb").read()
        name, guard, blob = parse(stream)
        pt = decrypt(key, blob, name, guard)
        ok = pt == app
        print("name   : %r" % name)
        print("version: %s" % (name_version(name) or "none"))
        print("guard  : %s" % guard.hex())
        print("stream : %d bytes, %d ciphertext" % (len(stream), len(blob)))
        print("verify : %s (%d plaintext bytes, authenticates)" % ("OK" if ok else "MISMATCH", len(pt)))
        return 0 if ok else 1

    no_guard = "--no-guard" in argv
    argv = [a for a in argv if a != "--no-guard"]
    version = tag_version()
    if "--version" in argv:
        i = argv.index("--version")
        if i + 1 == len(argv) or not re.fullmatch(r"\d+\.\d+\.\d+", argv[i + 1]):
            sys.exit(__doc__)
        version = argv[i + 1]
        argv = argv[:i] + argv[i + 2 :]
    if len(argv) != 2:
        sys.exit(__doc__)
    app_path, out = argv[0], argv[1]
    name = ("ci600 " + version).encode()
    app = open(app_path, "rb").read()
    try:
        stream = build(key, app, name, guard=not no_guard)
    except ValueError as e:
        sys.exit("refusing to build: %s" % e)
    open(out, "wb").write(stream)
    # check that the stream decrypts back to the image before anyone flashes it
    name2, guard2, blob2 = parse(stream)
    assert decrypt(key, blob2, name2, guard2) == app, "self-test failed"
    print(
        "%s: %r, %d bytes of firmware -> %d-byte stream (%d ciphertext bytes), self-test OK"
        % (out, name, len(app), len(stream), len(blob2))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
