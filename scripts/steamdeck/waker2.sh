#!/usr/bin/env bash
# Steam Deck (or any Linux x86_64) launcher for the Waker2 test build. Put it next to switchwaker,
# with the discs in the same folder:
#   GZLE01.iso    your clean USA disc, revision 0, uncompressed (COS_DISC)
#   any other .iso   a Wind Waker Randomizer output (COS_RANDO_DISC); leave it out for the plain game
# Any COS_* variable already set wins (e.g. from Steam's launch options: COS_RANDO_DISC=... %command%).
here=$(cd "$(dirname "$0")" && pwd)
cd "$here" || exit 1

if [ -z "${COS_DISC:-}" ]; then
    export COS_DISC="$here/GZLE01.iso"
fi
if [ -z "${COS_RANDO_DISC+x}" ]; then
    for iso in "$here"/*.iso; do
        [ -f "$iso" ] || continue
        [ "$iso" = "$COS_DISC" ] && continue
        export COS_RANDO_DISC="$iso"
        break
    done
fi
export COS_FULLSCREEN="${COS_FULLSCREEN:-1}"
export COS_TRACKER_SERVER="${COS_TRACKER_SERVER:-1}"   # only does something in builds with the tracker
# The randomizer tracker, served by the game at http://127.0.0.1:6544/ while it runs (docs/TRACKER.md);
# it autotracks by itself. COS_TRACKER_BIND=0.0.0.0 opens it to a phone or PC on the same network.
if [ -z "${COS_TRACKER_WEB:-}" ] && [ -f "$here/tracker/index.html" ]; then
    export COS_TRACKER_WEB="$here/tracker"
fi
# COS_TRACKER_OPEN=1: also open the tracker in the browser once the game answers (waker2-tracker.sh)
if [ "${COS_TRACKER_OPEN:-0}" = "1" ] && [ -x "$here/waker2-tracker.sh" ]; then
    "$here/waker2-tracker.sh" --wait >/dev/null 2>&1 &
fi

# The game's output also goes to waker2.log next to it (the last run), to read after a Game Mode start.
exec > >(tee "$here/waker2.log") 2>&1
echo "[waker2] COS_DISC=$COS_DISC"
echo "[waker2] COS_RANDO_DISC=${COS_RANDO_DISC:-(none, plain game)}"
exec "$here/switchwaker" "$@"
