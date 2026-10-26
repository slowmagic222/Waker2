#!/usr/bin/env bash
# Builds the in-game seed generator's folder (docs/SEEDGEN.md) for a Linux x86_64 package (the
# Steam Deck): a relocatable Python, the Wind Waker Randomizer at the commit rando mode is pinned
# to, its dependencies (with gclib's C speedups) and waker2_seedgen.py.
#
#   scripts/seedgen/bundle.sh <out dir> [work dir]
#
# <out dir> gets python/, wwrando/, models/ (empty: the player's custom models go there),
# waker2_seedgen.py and rplus/ (Randomizer+'s world generator, docs/RANDOMIZER_PLUS.md). The game looks for it as seedgen/ next to its executable (or COS_SEEDGEN).
# Needs git, a C compiler (gclib's speedups are built from source) and network access to PyPI and
# GitHub. About 200 MB.
set -euo pipefail

# The wwrando commit rando mode reads its offsets from (docs/RANDO.md: "pinned to wwrando's paths
# at 9775811"); a seed made by another version may not match the game.
WWRANDO_REPO=https://github.com/LagoLunatic/wwrando.git
WWRANDO_COMMIT=9775811b6fd039992822fbaf30ea6259b339f4b0
PYTHON_VERSION=3.12   # wwrando needs 3.12 (options/base_options.py uses a 3.12-only dataclasses name)

out=${1:?usage: bundle.sh <out dir> [work dir]}
work=${2:-$(mktemp -d)}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out" "$work"
out=$(cd "$out" && pwd)
work=$(cd "$work" && pwd)

# uv fetches a python-build-standalone build: relocatable, glibc 2.17+, so it runs on SteamOS.
if ! command -v uv >/dev/null 2>&1; then
    python3 -m venv "$work/uv-venv"
    "$work/uv-venv/bin/pip" install -q uv
    PATH="$work/uv-venv/bin:$PATH"
fi
rm -rf "$work/python-install"
uv python install "$PYTHON_VERSION" --install-dir "$work/python-install" --no-bin
pydir=$(find "$work/python-install" -maxdepth 1 -type d -name "cpython-$PYTHON_VERSION.*-linux-x86_64-gnu" | sort -V | tail -1)
[ -n "$pydir" ] || { echo "bundle.sh: no Python $PYTHON_VERSION from uv" >&2; exit 1; }
rm -rf "$out/python"
cp -a "$pydir" "$out/python"
# Our own copy: let pip install into it.
find "$out/python/lib" -maxdepth 2 -name EXTERNALLY-MANAGED -delete
py="$out/python/bin/python3"

# wwrando and its gclib submodule.
if [ ! -d "$work/wwrando/.git" ]; then
    git clone -q "$WWRANDO_REPO" "$work/wwrando"
fi
git -C "$work/wwrando" fetch -q origin
git -C "$work/wwrando" checkout -q "$WWRANDO_COMMIT"
git -C "$work/wwrando" submodule update -q --init --recursive
rm -rf "$out/wwrando"
mkdir -p "$out/wwrando"
git -C "$work/wwrando" archive "$WWRANDO_COMMIT" | tar -x -C "$out/wwrando"
gclib_commit=$(git -C "$work/wwrando" rev-parse HEAD:gclib)
git -C "$work/wwrando/gclib" archive "$gclib_commit" | tar -x -C "$out/wwrando/gclib"
# version.py adds the commit to the version from .git/HEAD; permalinks carry it, so seeds and
# permalinks match a wwrando run from the same commit.
mkdir -p "$out/wwrando/.git"
echo "$WWRANDO_COMMIT" > "$out/wwrando/.git/HEAD"
# Not needed to randomize.
rm -rf "$out/wwrando/test" "$out/wwrando/docs" "$out/wwrando/wwr_ui/uic"

# Dependencies: wwrando's requirements without the Qt GUI, gclib's with its C speedups.
grep -viE '^(QtPy|PySide6|PyQt5|-r)' "$out/wwrando/requirements.txt" > "$work/requirements.txt"
"$py" -m pip install -q --no-cache-dir --disable-pip-version-check \
    -r "$work/requirements.txt" -r "$out/wwrando/gclib/requirements_full.txt"
"$py" -c "import pyfastyaz0yay0, pyfastbti, pyfasttextureutils, PIL, numpy, ruamel.yaml"

cp "$here/waker2_seedgen.py" "$out/"
mkdir -p "$out/rplus"
cp "$here"/../rplus/*.py "$out/rplus/"
mkdir -p "$out/models"
cp "$out/wwrando/models/About Custom Models.txt" "$out/models/" 2>/dev/null || true
# Drop bytecode caches and pip's leftovers to keep the package small.
find "$out/python" -name __pycache__ -type d -prune -exec rm -rf {} +
rm -rf "$out/python/lib/python$PYTHON_VERSION/test" "$out/python/lib/python$PYTHON_VERSION/idlelib" \
    "$out/python/lib/python$PYTHON_VERSION/tkinter" "$out/python/lib/python$PYTHON_VERSION/ensurepip"

"$py" "$out/waker2_seedgen.py" describe --wwrando "$out/wwrando" --models "$out/models" > "$work/describe.tsv"
grep -m1 "^version" "$work/describe.tsv"
WWRANDO_DIR="$out/wwrando" "$py" "$out/rplus/worldgen.py" --seed 1 --out "$work/rplus-test"
du -sh "$out"
