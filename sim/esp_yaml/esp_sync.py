#!/usr/bin/env python3
"""Copy proto/ into the ESP repo, which builds the same files, and note the copies in its proto.lock.

    esp_sync.py FIRMWARE ESP_REPO [--force]

proto.lock has the firmware commit and each copy's sha256. A copy changed in the ESP repo is only overwritten or
removed with --force. Files gone from proto/ go from there too. gen.py checks the copies against proto/ and the lock.
"""

import glob
import hashlib
import os
import shutil
import subprocess
import sys

PANEL = os.path.join("packages", "code", "panel")  # the copies in the ESP repo
LOCK = "proto.lock"
HEADER = "# Written by make -C sim esp_sync in flexit-ci600-firmware. Don't edit."


def proto_files(fw):
    """proto/'s sources by name."""
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(fw, "proto", "*.[ch]")))


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_lock(path):
    """proto.lock's commit and {name: sha256}, None without one. Exits if it isn't one esp_sync wrote."""
    if not os.path.exists(path):
        return None
    lines = open(path).read().splitlines()
    if len(lines) < 2 or lines[0] != HEADER or not lines[1].startswith("firmware "):
        sys.exit(f"{path} isn't one esp_sync wrote")
    hashes = {}
    for line in lines[2:]:
        sha, sep, name = line.partition("  ")
        if not sep or len(sha) != 64:
            sys.exit(f"{path}: {line!r} isn't a hash and a file")
        hashes[name] = sha
    return lines[1].split(" ", 1)[1], hashes


def lock_text(commit, hashes):
    """By name in C order, which is Python's for these."""
    return "\n".join([HEADER, f"firmware {commit}"] + [f"{hashes[n]}  {n}" for n in sorted(hashes)]) + "\n"


def git(fw, *args):
    try:
        return subprocess.run(["git", "-C", fw, *args], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"esp_sync needs the firmware's git checkout for the commit it copies from: {e}")


def main():
    args = [a for a in sys.argv[1:] if a != "--force"]
    force = len(args) < len(sys.argv) - 1
    if len(args) != 2:
        sys.exit(__doc__)
    fw, esp = args
    panel = os.path.join(esp, PANEL)
    if not os.path.isdir(panel):
        sys.exit(f"{panel} isn't there: ESP_REPO=<esphome-flexit-modbus-server>")
    commit = git(fw, "rev-parse", "HEAD").strip()
    if git(fw, "status", "--porcelain", "--", "proto"):
        print(f"esp_sync: proto/ has changes that aren't committed, proto.lock says {commit[:12]}", file=sys.stderr)

    names = proto_files(fw)
    new = {n: digest(os.path.join(fw, "proto", n)) for n in names}
    lock = read_lock(os.path.join(panel, LOCK))
    had = lock[1] if lock else {}
    # Copies changed in the ESP repo that this would overwrite or remove. Without a proto.lock yet there's nothing to
    # keep. A file the lock doesn't have isn't ours.
    changed = []
    for n in sorted(set(new) | set(had)) if lock else []:
        path = os.path.join(panel, n)
        if os.path.exists(path) and digest(path) not in (new.get(n), had.get(n)):
            changed.append(n)
    if changed and not force:
        sys.exit(
            f"esp_sync: {', '.join(changed)} in {panel} changed there since the last sync, and this would overwrite or "
            "remove it.\nMake the change in proto/ instead, or drop it with make -C sim esp_sync FORCE=1"
        )

    copied = removed = 0
    for n in names:
        path = os.path.join(panel, n)
        if not os.path.exists(path) or digest(path) != new[n]:
            shutil.copyfile(os.path.join(fw, "proto", n), path)
            copied += 1
    for n in sorted(set(had) - set(new)):
        if os.path.exists(os.path.join(panel, n)):
            os.remove(os.path.join(panel, n))
            removed += 1
    text = lock_text(commit, new)
    if not lock or open(os.path.join(panel, LOCK)).read() != text:
        with open(os.path.join(panel, LOCK), "w") as f:
            f.write(text)
    print(f"esp_sync: {copied} copied, {removed} removed, proto.lock at {commit[:12]}")


if __name__ == "__main__":
    main()
