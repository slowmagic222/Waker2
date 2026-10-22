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
# Say so in a window when the clean disc is missing (a Game Mode start shows no terminal).
if [ ! -f "$COS_DISC" ]; then
    msg="Waker2 needs your clean USA disc of The Wind Waker as $COS_DISC (named exactly GZLE01.iso)."
    echo "[waker2] $msg" > "$here/waker2.log"
    if command -v kdialog >/dev/null 2>&1; then kdialog --error "$msg"
    elif command -v zenity >/dev/null 2>&1; then zenity --error --text="$msg"; fi
    echo "[waker2] $msg" >&2
    exit 1
fi

export COS_FULLSCREEN="${COS_FULLSCREEN:-1}"
# The randomizer tracker is inside the game: L3+R3 (docs/TRACKER.md). Its logic and art come from
# tracker-logic/ next to switchwaker.

# The game's output also goes to waker2.log next to it (the last run), to read after a Game Mode start.
exec > >(tee "$here/waker2.log") 2>&1
echo "[waker2] COS_DISC=$COS_DISC"
echo "[waker2] COS_RANDO_DISC=${COS_RANDO_DISC:-(none, plain game)}"
exec "$here/switchwaker" "$@"
