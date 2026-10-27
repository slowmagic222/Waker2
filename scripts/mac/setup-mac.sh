#!/usr/bin/env bash
# Sets up a Mac (Apple silicon) to build and play Waker2's Randomizer+ test build, as the Mac mini was set
# up by hand: clone, prerequisites, a configure that works around a mixed Homebrew and older Command Line
# Tools, the assets from your disc, the seed 7 world and the in-game world maker's folder.
#
#   scripts/mac/setup-mac.sh /path/to/GZLE01.iso      (or COS_DISC=/path/to/GZLE01.iso scripts/mac/setup-mac.sh)
#
# It can also be run before the clone exists (curl or copy it anywhere): it clones into ~/waker2-rplus.
# Re-running it is safe: each step skips what is already there.
#
# Environment (all optional):
#   WAKER2_DIR     where the repository goes (default ~/waker2-rplus)
#   WAKER2_BRANCH  the branch (default claude/project-thread-ukglnp)
#   WWRANDO_DIR    a wwrando checkout to use (default ~/wwrando-9775811, cloned at 9775811)
#   PYTHON         a Python 3.10+ to use (default: the first one found)
set -euo pipefail

REPO_URL=https://github.com/slowmagic222/Waker2
BRANCH=${WAKER2_BRANCH:-claude/project-thread-ukglnp}
DIR=${WAKER2_DIR:-$HOME/waker2-rplus}
WWRANDO_COMMIT=9775811
WWRANDO=${WWRANDO_DIR:-$HOME/wwrando-$WWRANDO_COMMIT}
DISC=${1:-${COS_DISC:-}}

say() { printf '\n== %s\n' "$*"; }
die() { printf 'setup-mac: %s\n' "$*" >&2; exit 1; }

# ---- 1. The machine -----------------------------------------------------------------------------------
say "Checking the machine"
[ "$(uname -s)" = Darwin ] || die "this script is for macOS"
# The native build is arm64 only (Aurora's prebuilt Dawn package is darwin-arm64; docs: "Apple silicon").
# A shell running under Rosetta reports x86_64 on an Apple silicon Mac: check the hardware instead.
if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" != 1 ]; then
    die "this Mac has an Intel CPU; the Mac build needs Apple silicon (M1 or later)"
fi
if [ "$(uname -m)" != arm64 ]; then
    die "this shell runs under Rosetta (x86_64); open a native Terminal (arm64) and run the script again"
fi

[ -n "$DISC" ] || die "give the path of your Wind Waker USA disc image (GZLE01 revision 0 .iso) as the first argument"
[ -f "$DISC" ] || die "no such file: $DISC"
[ "$(head -c 6 "$DISC")" = GZLE01 ] || die "$DISC is not a GZLE01 (USA) disc image"
DISC=$(cd "$(dirname "$DISC")" && pwd)/$(basename "$DISC")

# ---- 2. Prerequisites -----------------------------------------------------------------------------------
say "Prerequisites"
if ! xcode-select -p >/dev/null 2>&1; then
    xcode-select --install || true
    die "the Xcode command line tools are being installed: run this script again when that finishes"
fi
echo "command line tools: $(xcode-select -p)"

BREW=""
for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [ -x "$b" ]; then BREW=$b; break; fi
done
need_tool() {  # need_tool <command> <brew formula>
    if ! command -v "$1" >/dev/null 2>&1; then
        [ -n "$BREW" ] || die "$1 is missing and there is no Homebrew: install Homebrew (https://brew.sh), then run this again"
        echo "installing $2 with $BREW"
        "$BREW" install "$2"
    fi
}
export PATH="/opt/homebrew/bin:$PATH:/usr/local/bin"
need_tool git git
need_tool cmake cmake
need_tool ninja ninja
cmake_version=$(cmake --version | head -1 | awk '{print $3}')
if [ "$(printf '%s\n3.28\n' "$cmake_version" | sort -V | head -1)" != 3.28 ]; then
    die "cmake $cmake_version is too old (3.28 or newer is needed): update it (brew upgrade cmake)"
fi
echo "cmake $cmake_version, ninja $(ninja --version)"

# Python 3.10 or newer (the asset scripts and the world generator).
find_python() {
    local c
    for c in ${PYTHON:-} python3.13 python3.12 python3.11 python3.10 \
             /opt/homebrew/bin/python3 /Library/Frameworks/Python.framework/Versions/Current/bin/python3 python3; do
        [ -n "$c" ] || continue
        if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
            command -v "$c"
            return 0
        fi
    done
    return 1
}
PY=$(find_python) || {
    [ -n "$BREW" ] || die "no Python 3.10+ found: install one (https://www.python.org or Homebrew), then run this again"
    "$BREW" install python
    PY=$(find_python) || die "no Python 3.10+ found after installing it"
}
PY=$("$PY" -c 'import os, sys; print(os.path.realpath(sys.executable))')
echo "python: $PY ($("$PY" --version))"

# ---- 3. The repository ----------------------------------------------------------------------------------
say "Repository: $DIR ($BRANCH)"
if [ -d "$DIR/.git" ]; then
    git -C "$DIR" fetch origin "$BRANCH"
    git -C "$DIR" checkout "$BRANCH"
    git -C "$DIR" pull --ff-only origin "$BRANCH" || echo "(local changes: pull skipped)"
else
    git clone -b "$BRANCH" "$REPO_URL" "$DIR"
fi
cd "$DIR"
git log --oneline -1

# ---- 4. Assets from the disc ----------------------------------------------------------------------------
say "Assets from the disc"
export COS_DISC="$DISC"
if [ ! -d ref/recompcore ]; then
    native/tools/fetch_recompcore.sh
fi
if [ ! -d build/native-mac/assets/GZLE01 ]; then
    native/tools/gen_assets.sh        # checks main.dol's SHA-1 (GZLE01 revision 0) and writes build/native-mac/assets/GZLE01
else
    echo "build/native-mac/assets/GZLE01 already there"
fi

# ---- 5. Configure and build -----------------------------------------------------------------------------
say "Configure and build"
# Workarounds (each one harmless where it is not needed):
# - Homebrew libraries can be the wrong architecture (an Intel Homebrew in /usr/local on Apple silicon) and
#   Aurora links png, freetype and zstd from the system when it finds them: ignore /usr/local and turn
#   pkg-config off, so Aurora fetches and builds its own arm64 copies.
# - Command Line Tools older than 26 ship a libc++ where std::jthread / std::stop_token are experimental
#   (Aurora's lib/thread.hpp needs them): add -fexperimental-library only when a test program needs it.
CXXFLAGS_EXTRA=""
tmp=$(mktemp -d)
printf '#include <thread>\n#include <stop_token>\nint main(){std::jthread t([](std::stop_token){}); return 0;}\n' > "$tmp/t.cpp"
if ! c++ -std=c++20 -arch arm64 "$tmp/t.cpp" -o "$tmp/t" >/dev/null 2>&1; then
    if c++ -std=c++20 -fexperimental-library -arch arm64 "$tmp/t.cpp" -o "$tmp/t" >/dev/null 2>&1; then
        CXXFLAGS_EXTRA="-fexperimental-library"
        echo "libc++ has std::jthread only as experimental: building with -fexperimental-library"
    else
        rm -rf "$tmp"
        die "this compiler has no std::jthread at all: update the command line tools (softwareupdate, or reinstall them)"
    fi
fi
rm -rf "$tmp"

cmake -S native -B build/native-mac -G Ninja \
    -DCMAKE_IGNORE_PREFIX_PATH=/usr/local \
    -DPKG_CONFIG_EXECUTABLE=/nonexistent/pkg-config \
    -DPython3_EXECUTABLE="$PY" \
    ${CXXFLAGS_EXTRA:+-DCMAKE_CXX_FLAGS="$CXXFLAGS_EXTRA"}
ninja -C build/native-mac switchwaker
ls -la build/native-mac/switchwaker

# ---- 6. The Randomizer+ world and the world maker -------------------------------------------------------
say "Randomizer+: wwrando logic, the seed 7 world, the in-game world maker"
if [ ! -d "$WWRANDO/.git" ]; then
    git clone https://github.com/LagoLunatic/wwrando "$WWRANDO"
fi
git -C "$WWRANDO" checkout -q "$WWRANDO_COMMIT"
if [ ! -f build/rplus/world.rpw ]; then
    WWRANDO_DIR="$WWRANDO" "$PY" scripts/rplus/worldgen.py --seed 7 --out build/rplus
fi
ls -la build/rplus/world.rpw

# The options menu's "Make a world" looks for seedgen/ (rplus/worldgen.py and a Python); the bundled one is
# only built for the Linux package, so on the Mac it is this folder of links plus COS_SEEDGEN_PYTHON.
mkdir -p build/seedgen-mac
ln -sfn "$DIR/scripts/rplus" build/seedgen-mac/rplus
ln -sfn "$WWRANDO" build/seedgen-mac/wwrando

# ---- 7. How to play -------------------------------------------------------------------------------------
say "Done. To play (sound on, no time limit; close the window to quit):"
cat <<EOF

cd "$DIR" && mkdir -p build/native-mac/runs/rplus-play && cd build/native-mac/runs/rplus-play && env COS_SEEDGEN="$DIR/build/seedgen-mac" COS_SEEDGEN_PYTHON="$PY" COS_DISC="$DISC" COS_RPLUS_WORLD="$DIR/build/rplus/world.rpw" COS_BOOT_STAGE=sea:44:8 COS_BOOT_PRESET=sailing COS_SHOT=300,900 COS_SHOT_DIR=\$PWD "$DIR/build/native-mac/switchwaker" > log.txt 2>&1; echo exit \$?

The log and screenshots go to $DIR/build/native-mac/runs/rplus-play/.
To update later: git -C "$DIR" pull, then ninja -C "$DIR/build/native-mac" switchwaker.
EOF
