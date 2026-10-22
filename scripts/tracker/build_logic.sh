#!/usr/bin/env bash
# The tracker inside the game (docs/TRACKER.md, "In the game"): its logic and art, for the game's
# tracker-logic/ folder:
#
#   scripts/tracker/build_logic.sh OUT_DIR [WORK_DIR]
#
# Clones wooferzfg's TWW Randomizer Tracker at the commit below into WORK_DIR (default
# build/tww-rando-tracker), adds the merge of the game's state (scripts/tracker/logic/autotracker.js)
# to its services, runs that merge's tests (the tracker's jest) and bundles. OUT_DIR gets:
#   logic.js            the tracker's own logic code and the merge of the game's state, bundled into one
#                       script for QuickJS (scripts/tracker/logic/entry.js; esbuild)
#   item_locations.txt  wwrando's logic files at the commit rando mode is pinned to (docs/RANDO.md)
#   macros.txt
#   art.rgba art.txt    the tracker's images in one atlas (scripts/tracker/logic/pack_art.js)
#   LICENSE-tracker.txt LICENSE-wwrando.txt
# Needs git, Node.js 18 or newer and the network.
set -euo pipefail

TRACKER_REPO=https://github.com/wooferzfg/tww-rando-tracker.git
# its next-rando-version branch: the main branch reads only wwrando 1.10.0 permalinks, and rando mode
# is pinned to a later wwrando (9775811, docs/RANDO.md), whose permalinks and logic only this branch reads
TRACKER_COMMIT=dd48285589d340a1a65a5975def94ba3c64ef214
WWRANDO_REPO=https://github.com/LagoLunatic/wwrando.git
WWRANDO_COMMIT=9775811b6fd039992822fbaf30ea6259b339f4b0
ESBUILD=esbuild@0.24.0

[ $# -ge 1 ] || { echo "usage: $0 OUT_DIR [WORK_DIR]" >&2; exit 2; }
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../.." && pwd)"
out="$1"
tracker="${2:-$repo/build/tww-rando-tracker}"
rm -rf "$tracker"
git init -q "$tracker"
git -C "$tracker" remote add origin "$TRACKER_REPO"
git -C "$tracker" fetch -q --depth 1 origin "$TRACKER_COMMIT"
git -C "$tracker" checkout -q FETCH_HEAD
cp "$script_dir/logic/autotracker.js" "$script_dir/logic/autotracker.test.js" "$tracker/src/services/"
(cd "$tracker" && npm ci --no-audit --no-fund && npx jest --maxWorkers=2 --coverage=false src/services/autotracker.test.js)
tracker="$(cd "$tracker" && pwd)"
rm -rf "$out"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# the logic: entry.js in the tracker's src/, Buffer (which webpack provides the page) from the
# tracker's own "buffer" package
cp "$script_dir/logic/entry.js" "$tracker/src/cos-logic-entry.js"
echo "export { Buffer } from 'buffer';" > "$tracker/src/cos-logic-buffer.js"
(cd "$tracker" && npx --yes "$ESBUILD" src/cos-logic-entry.js --bundle --format=iife --target=es2020 --minify \
    --inject:src/cos-logic-buffer.js --outfile="$out/logic.js" --log-level=warning)

# the art
(cd "$tracker" && node "$script_dir/logic/images_map.js" "$tracker" "$tracker/cos-images-map.json" &&
    NODE_PATH="$tracker/node_modules" node "$script_dir/logic/pack_art.js" "$tracker/cos-images-map.json" \
        "$tracker/src/images" "$out")

# wwrando's logic files
ww="$(mktemp -d)"
trap 'rm -rf "$ww"' EXIT
git init -q "$ww"
git -C "$ww" remote add origin "$WWRANDO_REPO"
git -C "$ww" fetch -q --depth 1 origin "$WWRANDO_COMMIT"
for f in logic/item_locations.txt logic/macros.txt LICENSE.txt; do
    git -C "$ww" show "FETCH_HEAD:$f" > "$out/$(basename "$f")"
done
mv "$out/LICENSE.txt" "$out/LICENSE-wwrando.txt"
cp "$tracker/LICENSE" "$out/LICENSE-tracker.txt"
echo "build_logic: tracker $(git -C "$tracker" rev-parse --short HEAD), wwrando ${WWRANDO_COMMIT:0:7} in $out"
ls -la "$out"
