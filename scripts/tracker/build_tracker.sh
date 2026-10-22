#!/usr/bin/env bash
# Build wooferzfg's TWW Randomizer Tracker with the autotracking patch (docs/TRACKER.md) into OUT_DIR,
# for the game to serve itself (COS_TRACKER_WEB=OUT_DIR):
#
#   scripts/tracker/build_tracker.sh OUT_DIR [WORK_DIR]
#
# Clones the tracker at the commit the patch was made against into WORK_DIR (default
# build/tww-rando-tracker), applies scripts/tracker/tww-rando-tracker-autotrack.patch, runs its tests
# and its production build (webpack), and copies the result to OUT_DIR. Needs git, Node.js 18 or newer
# and the network. The tracker is GPL-3.0-or-later; its LICENSE goes into OUT_DIR with it.
set -euo pipefail

TRACKER_REPO=https://github.com/wooferzfg/tww-rando-tracker.git
TRACKER_COMMIT=1c6d5a06b4f0109f742036e3491a7e8ad46bd3ba

[ $# -ge 1 ] || { echo "usage: $0 OUT_DIR [WORK_DIR]" >&2; exit 2; }
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../.." && pwd)"
out="$1"
work="${2:-$repo/build/tww-rando-tracker}"

rm -rf "$work"
git init -q "$work"
git -C "$work" remote add origin "$TRACKER_REPO"
git -C "$work" fetch -q --depth 1 origin "$TRACKER_COMMIT"
git -C "$work" checkout -q FETCH_HEAD
git -C "$work" -c user.name=waker2 -c user.email=waker2@localhost am -q "$script_dir/tww-rando-tracker-autotrack.patch"

(cd "$work" && npm ci --no-audit --no-fund && npx jest --maxWorkers=2 && npx webpack --mode=production)

rm -rf "$out"
mkdir -p "$out"
cp -R "$work/dist/." "$out/"
cp "$work/LICENSE" "$out/LICENSE.txt"
echo "build_tracker: tracker at $(git -C "$work" rev-parse --short HEAD) (patched) in $out"
