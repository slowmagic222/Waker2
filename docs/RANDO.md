# Wind Waker Randomizer seeds (rando mode)

Status: **step 1 of the plan below, in progress** (written 2026-10-22). This step only lets the port open a
randomizer's output disc. It does not make a seed playable yet: see "What works".

## The idea

The [Wind Waker Randomizer](https://github.com/LagoLunatic/wwrando) (wwrando, LagoLunatic, MIT) stays the seed
generator. Its output is a new disc image: game ID `GZLE99`, with stage files, text and archives changed, plus
asm patches written into `main.dol` and the RELs. SwitchWaker compiles the game from the decompilation, so the
asm patches never run here (the same reason `docs/QOL_PLAN.md` redoes BetterWW's patches in C++). The parts
of a seed split like this:

| Part | Where wwrando puts it | Here |
|---|---|---|
| 247 of 320 item locations (chests, scalable objects, actor params, event actions) | stage `.arc` files | Read from the randomizer's disc as they are |
| 73 item locations given by code (NPCs, bosses, minigames) | an item ID byte in `main.dol` or a REL | To read from the randomizer's patched `main.dol` / RELs at the offsets wwrando writes (`logic/item_locations.txt`) |
| Behaviour (nonlinear story, progressive items, removed cutscenes, ...) | 22 asm patches, about 440 hunks | To redo in C++ behind rando mode, as the QoL options are |
| Settings (starting gear, health, spawn, ...) | `tweaks.py` writes into `main.dol` | To read as data from the patched `main.dol` |
| Custom actors `d_a_dungeon_flag_sw`, `d_a_switch_op` | new RELs | To write as decomp-style actors |

## Use

Two images, both uncompressed `.iso`:

```sh
export COS_DISC=/path/to/GZLE01.iso                 # the clean disc, as always
export COS_RANDO_DISC="/path/to/WW Random <seed>.iso" # the randomizer's output
build/native-linux/switchwaker
```

On the Switch, `COS_RANDO_DISC=sdmc:/switch/switchwaker/<file>.iso` in the `[dev]` section of
`native/user/settings.ini`. USA build only (wwrando randomizes `GZLE01` only).

- `COS_DISC` is checked as before (header, and `run.sh`'s SHA-1). The data arrays of a `COS_RUNTIME_ASSETS`
  build are cut from the **clean** disc's `main.dol` and RELs, read by `pc_assets.cpp`'s own disc image
  reader (`CleanDisc`), and still SHA-1 checked.
- The game's disc (`aurora_dvd_open`) is `COS_RANDO_DISC`, which must be `GZLE99` revision 0. Saves made with
  it are under the `GZLE99` game code, apart from the original game's.
- A randomizer output given as `COS_DISC` is refused with a message naming `COS_RANDO_DISC`.

## What works

- The checks and the disc switch above (built; checked with a syntax build of the changed units and a test of
  `CleanDisc` against a made-up disc image; **not yet run with a real seed**).
- Nothing of the randomizer's code side: items given by code are the original ones, and stage data that
  expects wwrando's patches (custom actors, item IDs the original code does not handle, custom text commands)
  can misbehave. Expect a seed to be unplayable until the steps below land.

## Plan

1. Rando disc (this step).
2. The 73 code-given item locations, read from the randomizer's patched files.
3. `custom_funcs` / `custom_data` helpers as a C++ module, then `make_game_nonlinear` and
   `flexible_item_locations` (what a seed needs to be completable).
4. Progressive items, `misc_rando_features`, `remove_cutscenes`.
5. The optional settings (swordless, hero mode, remove music, ...) and enemy randomization.

The patch inventory (every hunk and the function it patches) and the reasoning are in the project's
write-up; the patches already redone for the QoL options are listed in `docs/QOL_PLAN.md`.
