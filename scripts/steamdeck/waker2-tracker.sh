#!/usr/bin/env bash
# Opens the randomizer tracker the running game serves (docs/TRACKER.md) in the default browser.
#   waker2-tracker.sh [--wait]     --wait: wait up to 2 minutes for the game to answer first
# Then pick the seed's permalink on the tracker's page as usual; it autotracks from the game.
port="${COS_TRACKER_SERVER:-1}"
[ "$port" = "1" ] && port=6544
url="http://127.0.0.1:$port/"
if [ "${1:-}" = "--wait" ]; then
    for _ in $(seq 120); do
        curl -fs -o /dev/null "${url}state" 2>/dev/null && break
        sleep 1
    done
fi
exec xdg-open "$url"
