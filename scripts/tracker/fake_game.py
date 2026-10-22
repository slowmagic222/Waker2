#!/usr/bin/env python3
"""Serve a tracker state as the game does (docs/TRACKER.md), to try the tracker's autotracking without
the game.

  scripts/tracker/fake_game.py [STATE.json] [--port 6544]

GET /state answers STATE.json, read again at each request (edit it while the tracker runs), with the
same CORS headers as the game's server (native/src/pc/features/pc_tracker_server.cpp). Without a file:
a small made-up state (two swords, the Wind Waker, Link's house's chest, Treasure Chart 25 salvaged).
"""

import argparse
import http.server
import json

SAMPLE = {
    "version": 1, "game": "GZLE99", "rando": True, "frame": 0, "playing": True, "saveSlot": 0,
    "stage": "sea", "room": 44,
    "items": {"Progressive Sword": 2, "Wind Waker": 1, "Telescope": 1, "Boat's Sail": 1, "Treasure Chart 25": 1},
    "smallKeysHeld": {"DRC Small Key": 1},
    "locations": {"Outset Island": {"Underneath Link's House": True}},
    "salvagedCharts": ["Treasure Chart 25"],
}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("state", nargs="?")
    p.add_argument("--port", type=int, default=6544)
    args = p.parse_args()

    class Handler(http.server.BaseHTTPRequestHandler):
        def headers_out(self, status, body=b"", kind=None):
            self.send_response(status)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "*")
            self.send_header("Access-Control-Allow-Private-Network", "true")
            self.send_header("Cache-Control", "no-store")
            if kind:
                self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_OPTIONS(self):
            self.headers_out(204)

        def do_GET(self):
            if self.path.split("?")[0] != "/state":
                self.headers_out(404, b"not found; the state is at /state\n", "text/plain")
                return
            state = json.load(open(args.state, encoding="utf-8")) if args.state else SAMPLE
            self.headers_out(200, json.dumps(state).encode(), "application/json")

    print(f"fake game state at http://127.0.0.1:{args.port}/state")
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
