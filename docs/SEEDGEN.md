# Making seeds in the game (the Randomizer tab)

Status: **first version** (written 2026-10-22). Seeds are made on the device from the options menu,
with no computer: the menu's **Randomizer** tab runs LagoLunatic's
[Wind Waker Randomizer](https://github.com/LagoLunatic/wwrando) (wwrando) itself, bundled with the
game, and restarts the game into the new seed. Linux (the Steam Deck) first; see "The Switch".

The generator's side was tested here: the bundle built from `scripts/seedgen/bundle.sh`, the tab's code
in a host test, driving the bundled wwrando (reading its options, colors, presets and starting gear,
importing a permalink, making a seed without a disc with `COS_SEEDGEN_DRY=1`). The menu code is checked
with a syntax build. **Not yet run in the game or with a real disc.**

## How it fits together

| Part | Where |
|---|---|
| wwrando, pinned to the commit rando mode reads its offsets from (`9775811`, docs/RANDO.md), unchanged | `seedgen/wwrando/` |
| A relocatable Python 3.12 (python-build-standalone, through `uv`) with wwrando's requirements, without Qt, and gclib's C speedups | `seedgen/python/` |
| The driver the game runs: `describe`, `generate`, `permalink` | `scripts/seedgen/waker2_seedgen.py` -> `seedgen/` |
| The player's custom player models (wwrando's `models/` folder) | `seedgen/models/` |
| The tab: rows, the child process, the restart | `native/src/pc/features/pc_seedgen.cpp`, `native/include/pc/pc_seedgen.h` |
| The tab's choices (`NAME=value`) | `<user dir>/rando/seedgen.ini` |
| The seeds, their logs and permalinks | `<user dir>/rando/seeds/` |

`scripts/seedgen/bundle.sh <out dir>` builds `seedgen/` (about 190 MB); the game looks for it next to its
executable, or at `COS_SEEDGEN=<dir>`. `COS_SEEDGEN_PYTHON` names another Python 3.12 with the
requirements (for development). `<user dir>` is the folder of the options menu's `settings.ini`
(`user/` next to the executable on Linux).

The driver prints one record per line, fields separated by tabs, so the game needs no JSON or YAML
parser; the records are listed at the top of `waker2_seedgen.py`. wwrando's progress (its own
`randomize()` steps) becomes the tab's percentage.

## The tab

The rows come from wwrando's option list (`describe`), with the labels and descriptions of its own
window, so they follow the bundled version. Up/down skips the section titles.

- **Generate seed**: makes a seed from the clean disc (`COS_DISC`) with the tab's options, in the
  background (the menu can be closed). A cancels while it runs. When it is done the seed becomes the
  seed to play, its permalink goes to the clipboard and next to the seed, and at the title screen the
  game restarts into it; in the middle of a game it waits (a toast says so), since the game is not saved.
- **Seed to play**: the seed the game loads (kept in `seedgen.ini` as `play=`): the launcher's choice
  (`COS_RANDO_DISC` as the launcher set it), the original game, or a seed of `rando/seeds/`, newest
  first. A restarts into it. It wins over `COS_RANDO_DISC` from the environment, because it is the
  player's later choice; `pc_seedgen_boot` applies it before the harness reads `COS_RANDO_DISC`
  (the launcher's value is kept in `COS_SEEDGEN_LAUNCHER_DISC` across a restart).
- **Seed name**: random (a new name each time, from wwrando's word lists) or the last seed's.
- **Import permalink (clipboard)**: a wwrando permalink's seed and options into the tab (a race's
  settings). It must be for the bundled version (`1.10.0_9775811`).
- **wwrando's default options**.
- Every wwrando option of its window: progress locations, modes, difficulty, entrances, other
  randomizers, hints, tweaks, starting items (one row per item: how many to start with), cosmetic,
  meta.
- **Cosmetic**: the player model (Link, the models in `seedgen/models/`, Random), casual clothes, the
  model's color presets, and one row per color of the model (hero or casual clothes) with a swatch:
  left/right walk a palette, A picks a random color; "Random colors" and "Default colors" set them all.
  As in wwrando's window, another model or clothes resets the colors and the preset.

Restarting is an `exec` of the game with its own command line (Linux); every file but stdin, stdout and
stderr is closed (the tracker's port is free again). On the Mac the tab says to restart by hand.

## Notes

- All seeds share the `GZLE99` save files (as with wwrando on Dolphin): a save made on one seed loaded
  on another is not that seed's game.
- The cosmetic changes are data of the randomizer's disc (the model's `Link.arc` and its textures), so
  they need no game code. wwrando's custom models can also set the sword, boomerang and arrow trail
  colors in `main.dol` (`customizer.py`, from a model's `metadata.txt`): those are code here and are not
  read yet.
- Options rando mode does not redo yet (docs/RANDO.md, step 5) still go into the seed, but the game
  may not act on them.
- Generating takes the CPU for a minute or two (the Deck has cores to spare; the game keeps running).

## The Switch

The Switch cannot run Python, so the Generate row says to make seeds elsewhere: copy the `.iso` to
`user/rando/seeds/` on the SD card and pick it in "Seed to play", which works there. Generating on the
console itself needs the randomizer ported off Python (its logic and the disc writing): a later step.

## Steam Deck package

The package's `seedgen/` comes from `scripts/seedgen/bundle.sh out/waker2/seedgen` in the steamdeck
workflow's packaging step (needs a C compiler and network access to PyPI and GitHub, which the Actions
runner has).
