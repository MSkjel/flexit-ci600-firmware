#!/usr/bin/env python3
"""Serve a .fxup from mkupdate.py over plain HTTP on the LAN, for the ESP's panel updater in
packages/panel/update.yaml. No TLS, so the ESP needs no certificate bundle. Set "Panel Firmware URL" to the
http://... line this prints.

    python3 tools/fxserve.py serve/ci600.fxup            # port 8000
    python3 tools/fxserve.py serve/ci600.fxup --port 8080

Leave it running while you press "Flash Panel Firmware", then Ctrl-C. It logs who fetched the file and how
many bytes went. Stdlib only.
"""

import argparse
import http.server
import os
import shutil
import socket
import sys


def lan_ips():
    ips = []
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None):
            ip = info[4][0]
            if "." in ip and not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except Exception:
        pass
    # the source address of a route to the internet, the best pick when the host has several addresses
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        if ip not in ips:
            ips.insert(0, ip)
    except Exception:
        pass
    return ips


def main(argv):
    ap = argparse.ArgumentParser(description="Serve a .fxup over HTTP for the panel updater")
    ap.add_argument("file", help="the firmware file to serve (e.g. serve/ci600.fxup)")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--bind", default="0.0.0.0")
    a = ap.parse_args(argv)

    path = os.path.abspath(a.file)
    if not os.path.isfile(path):
        sys.exit("no such file: " + path)
    name = os.path.basename(path)
    size = os.path.getsize(path)

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _serve(self, body):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            if body:
                with open(path, "rb") as f:
                    shutil.copyfileobj(f, self.wfile)
                self.log_message("sent %s (%d bytes) to %s", name, size, self.client_address[0])

        def do_GET(self):
            self._serve(True)

        def do_HEAD(self):
            self._serve(False)

        def log_message(self, fmt, *args):
            sys.stderr.write("  %s\n" % (fmt % args))

    httpd = http.server.ThreadingHTTPServer((a.bind, a.port), Handler)
    print("Serving %s (%d bytes) on port %d" % (name, size, a.port))
    print('Set the panel\'s "Panel Firmware URL" to one of:')
    for ip in lan_ips() or ["<this-host-ip>"]:
        print("    http://%s:%d/%s" % (ip, a.port, name))
    print("Ctrl-C to stop.\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main(sys.argv[1:])
