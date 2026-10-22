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

## Items given by code (step 2)

`pc_rando_rel_item("<rel>", offset, vanilla)` and `pc_rando_dol_item(address, vanilla)` (`pc_rando.h`) read the
byte wwrando wrote at its `logic/item_locations.txt` path from the randomizer's disc (its REL in
`files/rels/`, else in `RELS.arc`), so the offsets are the randomizer's own and the hunk names them. Without
`COS_RANDO_DISC` they return the original item. The hunks are pinned to wwrando's paths at `9775811`
(2026-07-20); another wwrando version with other paths reads the wrong bytes.

Hooked (each hunk names its wwrando location and path):

| Locations | File | Notes |
|---|---|---|
| 12 mailbox letters | `d_a_obj_toripost.cpp` | `m_letter`'s 13 entries, 12 bytes apart (Baito's mother twice) |
| 4 Windfall auctions | `d_a_auction.cpp` | the shop item and the item given |
| Battlesquid: first, second, under-20 prizes | `d_a_npc_kg1.cpp` | |
| Barrel Shooting: first, second prizes | `d_a_npc_kg2.cpp` | |
| Bird-Man Contest first prize | `d_a_npc_bmcon1.cpp` | |
| Goron trading reward | `d_a_npc_roten.cpp` | |
| Deku Leaf | `d_a_deku_item.cpp` | |
| Mrs. Marie: Killer Bees, 1, 21 and 40 Joy Pendants | `d_a_npc_ho.cpp` | |
| 6 Great Fairies | `d_a_bigelf.cpp` | in rando mode each gives one fixed item, not the next tier (wwrando `flexible_item_locations.asm`) |

Still to do: the locations whose NPC also needs wwrando's tracking fixes (Windfall townspeople, Lenzo, Zunari,
Maggie, Salvage Corp, Beedle's shops, Tingle, Orca, Ivan; they check "has the item" to know they gave it), the
boss heart containers (`createItemForBoss`), Medli, the withered trees, and the pearls and the Master Sword.

## Plan

1. Rando disc (this step).
2. The 73 code-given item locations, read from the randomizer's patched files.
3. `custom_funcs` / `custom_data` helpers as a C++ module, then `make_game_nonlinear` and
   `flexible_item_locations` (what a seed needs to be completable).
4. Progressive items, `misc_rando_features`, `remove_cutscenes`.
5. The optional settings (swordless, hero mode, remove music, ...) and enemy randomization.

The patch inventory (every hunk and the function it patches) and the reasoning are in the project's
write-up; the patches already redone for the QoL options are listed in `docs/QOL_PLAN.md`.
