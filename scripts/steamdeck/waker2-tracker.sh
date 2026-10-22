#!/usr/bin/env bash
# Opens the randomizer tracker the running game serves (docs/TRACKER.md) in the default browser.
#   waker2-tracker.sh [--wait]     --wait: wait up to 2 minutes for the game to answer first
# It opens on the seed being played when its permalink is next to the disc ("<disc> - Permalink.txt",
# or wwrando's "<disc> - Non-Spoiler Log.txt"); otherwise on the tracker's first page. It autotracks.
port="${COS_TRACKER_SERVER:-1}"
[ "$port" = "1" ] && port=6544
url="http://127.0.0.1:$port/"
open_url="${url}open"   # the game redirects into the tracker for the seed being played
if [ "${1:-}" = "--wait" ]; then
    for _ in $(seq 120); do
        curl -fs -o /dev/null "${url}state" 2>/dev/null && break
        sleep 1
    done
fi
exec xdg-open "$open_url"
