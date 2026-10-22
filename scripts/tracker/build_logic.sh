#!/usr/bin/env bash
# The tracker inside the game (docs/TRACKER.md, "In the game"): its logic and art, for the game's
# tracker-logic/ folder:
#
#   scripts/tracker/build_logic.sh OUT_DIR [TRACKER_DIR]
#
# TRACKER_DIR is the patched tracker that scripts/tracker/build_tracker.sh made (default
# build/tww-rando-tracker, with its node_modules). OUT_DIR gets:
#   logic.js            the tracker's own logic code and the autotracking merge, bundled into one script
#                       for QuickJS (scripts/tracker/logic/entry.js; esbuild)
#   item_locations.txt  wwrando's logic files at the commit rando mode is pinned to (docs/RANDO.md)
#   macros.txt
#   art.rgba art.txt    the tracker's images in one atlas (scripts/tracker/logic/pack_art.js)
#   LICENSE-tracker.txt LICENSE-wwrando.txt
# Needs git, Node.js 18 or newer and the network.
set -euo pipefail

WWRANDO_REPO=https://github.com/LagoLunatic/wwrando.git
WWRANDO_COMMIT=9775811b6fd039992822fbaf30ea6259b339f4b0
ESBUILD=esbuild@0.24.0

[ $# -ge 1 ] || { echo "usage: $0 OUT_DIR [TRACKER_DIR]" >&2; exit 2; }
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../.." && pwd)"
out="$1"
tracker="${2:-$repo/build/tww-rando-tracker}"
[ -d "$tracker/node_modules" ] || { echo "build_logic: no tracker with node_modules in $tracker (scripts/tracker/build_tracker.sh first)" >&2; exit 1; }
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
