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
# COS_TRACKER_BROWSER=<command>: the browser to run (it gets the address as its last argument).
if [ -n "${COS_TRACKER_BROWSER:-}" ]; then
    # shellcheck disable=SC2086
    exec $COS_TRACKER_BROWSER "$open_url"
fi
# Game Mode (gamescope) lists only the game's own processes as its windows, and xdg-open hands the
# address to a browser started outside the game. So there the browser runs from here, as a child of
# the game's launcher: an installed Flatpak browser, Chromium-based ones as an app window of their own.
if [ -n "${GAMESCOPE_WAYLAND_DISPLAY:-}" ] || [ "${XDG_CURRENT_DESKTOP:-}" = "gamescope" ]; then
    if command -v flatpak >/dev/null 2>&1; then
        for app in com.google.Chrome org.chromium.Chromium com.brave.Browser com.microsoft.Edge; do
            if flatpak info "$app" >/dev/null 2>&1; then
                exec flatpak run "$app" --new-window --app="$open_url"
            fi
        done
        if flatpak info org.mozilla.firefox >/dev/null 2>&1; then
            exec flatpak run org.mozilla.firefox --new-window "$open_url"
        fi
    fi
    for cmd in google-chrome chromium chromium-browser; do
        command -v "$cmd" >/dev/null 2>&1 && exec "$cmd" --new-window --app="$open_url"
    done
    command -v firefox >/dev/null 2>&1 && exec firefox --new-window "$open_url"
fi
exec xdg-open "$open_url"
