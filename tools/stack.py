#!/usr/bin/env python3
"""How deep the stack goes, from the compiler's own count of each function's frame and who calls whom.

    make -C hw stack                    # builds hw/build/stack with -fcallgraph-info=su,da and runs this
    python3 tools/stack.py hw/build/stack [HOW_MANY]

Lists main, the interrupt handlers and every function that's only called through a pointer (LVGL's timers and
events, the link's messages), each with the bytes of stack its deepest chain of calls takes and that chain. A call
through a pointer ends a chain, so what's above a callback has to be added by hand: main and link_poll() for a
message, lv_timer_handler() for a timer, the redraw down to the event for a draw event, and on top of any of them
the interrupts that can come at once. It's an upper bound: a call a function ends with counts as one more frame.
"""

import re
import sys
from pathlib import Path

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')


def load(build):
    """Each function's frame in bytes and the functions it calls, by the names the .ci files give them."""
    size, calls = {}, {}
    for f in Path(build).rglob("*.ci"):
        for line in f.read_text(errors="replace").splitlines():
            if m := NODE.match(line):
                if b := re.search(r"(\d+) bytes", m.group(2)):
                    size[m.group(1)] = int(b.group(1))
            elif m := EDGE.match(line):
                calls.setdefault(m.group(1), set()).add(m.group(2))
    return size, calls


def deepest(name, size, calls, seen, path=()):
    """The bytes of the deepest chain from name, and the chain. A function that calls itself is counted once."""
    if name in seen:
        return seen[name]
    if name in path:
        return 0, []
    below = max((deepest(c, size, calls, seen, path + (name,)) for c in calls.get(name, ())), default=(0, []))
    seen[name] = size.get(name, 0) + below[0], [name.split(":")[-1]] + below[1]
    return seen[name]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.setrecursionlimit(10000)
    size, calls = load(sys.argv[1])
    if not size:
        sys.exit("no .ci files under %s, build it with make -C hw stack" % sys.argv[1])
    called = {c for cs in calls.values() for c in cs}
    seen = {}
    roots = sorted((deepest(n, size, calls, seen), n) for n in size if n not in called)
    for (depth, chain), _ in roots[::-1][: int(sys.argv[2]) if len(sys.argv) > 2 else 16]:
        print("%5d  %s" % (depth, " > ".join(chain)))


if __name__ == "__main__":
    main()
