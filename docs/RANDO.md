# Wind Waker Randomizer seeds (rando mode)

Status: **steps 1 to 3 of the plan below** (written 2026-10-22). The port opens a randomizer's output disc,
gives the 73 code-given items, and has the code a seed needs to be completable. Nothing has run with a real
seed yet: see "What works".

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
| Custom actors `d_a_dungeon_flag_sw`, `d_a_switch_op` | new RELs | `d_a_dungeon_flag_sw.cpp`, `d_a_switch_op.cpp` (step 3) |

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
- Items given by code (step 2, below).
- What a seed needs to be completable (step 3, below). Not yet: progressive items when picked up, the removed
  cutscenes, `misc_rando_features` (step 4) and the optional settings (step 5), so a seed can still hit a
  vanilla cutscene or give a sword tier out of order.

## Items given by code (step 2)

`pc_rando_rel_item("<rel>", offset, vanilla)` and `pc_rando_dol_item(address, vanilla)` (`pc_rando.h`) read the
byte wwrando wrote at its `logic/item_locations.txt` path from the randomizer's disc (its REL in
`files/rels/`, else in `RELS.arc`), so the offsets are the randomizer's own and the hunk names them. Without
`COS_RANDO_DISC` they return the original item. The hunks are pinned to wwrando's paths at `9775811`
(2026-07-20); another wwrando version with other paths reads the wrong bytes.

All 73 are hooked (each hunk names its wwrando location and path):

| Locations | File | Notes |
|---|---|---|
| 12 mailbox letters | `d_a_obj_toripost.cpp` | `m_letter`'s 13 entries, 12 bytes apart (Baito's mother twice) |
| 4 Windfall auctions | `d_a_auction.cpp` | the shop item and the item given |
| Battlesquid: first, second, under-20 prizes | `d_a_npc_kg1.cpp` | |
| Barrel Shooting: first, second prizes | `d_a_npc_kg2.cpp` | |
| Bird-Man Contest first prize | `d_a_npc_bmcon1.cpp` | |
| Goron trading reward | `d_a_npc_roten.cpp` | |
| Deku Leaf (Forest Haven, on the tree branch) | `d_a_deku_item.cpp` | |
| Mrs. Marie: Killer Bees, 1, 21 and 40 Joy Pendants | `d_a_npc_ho.cpp` | |
| 6 Great Fairies | `d_a_bigelf.cpp` | each gives one fixed item, not the next tier; the Big Octo fairy no longer doubles the magic meter herself, and the magic meter upgrade refills it (`d_item.cpp`) |
| Windfall townspeople: Sam, Pompie and Vera, Minenco, Linda and Anton, Kreeb, Kamo; Kane's gate rewards | `d_a_npc_people.cpp` | photo rewards tracked by event bits 6904, 6908, 6910 |
| Lenzo: Deluxe Picto Box, Forest Firefly, 2 photo rewards | `d_a_npc_photo.cpp` | Deluxe Picto Box slot tracked by event bit 6920 |
| Zunari's Magic Armor slot | `d_a_npc_rsh1.cpp` | event bit 6940 |
| Salvage Corp | `d_a_npc_sv.cpp` | event bit 6980 |
| Maggie's two rewards | `d_a_npc_kp1.cpp` | event bit 6A01 |
| Orca: 10 Knight's Crests, 500 hits | `d_a_npc_ji1.cpp` | Hurricane Spin is event bit 6901 (`d_item.cpp`, `procCutTurnMove_init`) |
| Ivan: Catch Killer Bees | `d_a_npc_mk.cpp` | the Killer Bees (`mk`, `uk`) no longer give the Picto Box hint |
| Beedle: Shop Ship 20 Rupee item, Rock Spire shop (3 items) | `d_a_npc_bs1.cpp`, `d_shop.cpp` | the bait slot is tracked by event bit 6902 |
| Tingle's 2 gifts, Ankle's all-statues reward | `d_a_npc_tc_cut.inc` | the Tingle Statues set event bits 6A04 to 6A40, read by `dComIfGs_isStageTbox(3 to 7, 0xF)` |
| 6 boss drops | the six boss actors, `f_op_actor_mng.cpp`, `d_a_disappear.cpp`, `d_a_boss_item.cpp` | the item travels in the disappear cloud's params; item flag 0x15 |
| Doc Bandam's first Green and Blue Potions | `d_a_npc_ds1.cpp` | given even without an empty bottle |
| Medli's Grappling Hook | `d_a_npc_md.cpp` | |
| Withered trees | `d_a_obj_ftree.cpp` | created with `fopAcM_createItem` so the model is loaded |
| Master Sword, Jabun, Mother Isle | `d_a_demo00.cpp` | `daDemo00_c`'s item table in `main.dol` |

The pig's buried item is stage data, but it is also created with `fopAcM_createItem` in rando mode, for the
same reason as the withered trees.

None of this has run with a real seed yet (syntax build only).

## Completable seeds (step 3)

All behind `pc_rando_active()`, each hunk naming the wwrando patch and offset it redoes.

| Part | wwrando source | Here |
|---|---|---|
| Nonlinear story | `make_game_nonlinear.asm` (all hunks but one) | about 30 actors and `d_com_inf_game.cpp`, `d_save.cpp`, `d_menu_dmap.cpp`, `d_menu_fmap.cpp`, `d_door.cpp`: the boat and sail, Medli and Makar spawning in their temples, Farore's Pearl and the Forest Haven, the Tower of the Gods and Hyrule doors, Ganon's Tower, ... The Great Deku Tree hunk (`d_a_npc_de1` 0xDB0) is not redone: it cannot be placed in the decomp's function without the REL |
| New game | `tweaks.py` starting gear, health, magic, sword mode, shards, pigs, targeting; `init_save_with_tweaks` | `d_save.cpp` `dSv_randoInitSave`, reading the custom symbols at 0x803FDC70 from the randomizer's `main.dol`; `dItem_randoProgressive` gives each starting item at its tier. The starting island and spawn come from the `li` immediates at 0x80058BA7 / 0x80058BAF |
| Item locations that move | `flexible_item_locations.asm` | the Rito postman (event bit 6A02), Windfall letters, salvage, Puppet Ganon's sword and Ganon's Tower chest, Helmaroc King's Wind's Requiem, pearl and song item get music, the Mighty Darknuts (switch 5), pots (Z rotation 0xFF00) and stone heads (X rotation 0xFF) that drop any item |
| Any item as a field item | `tweaks.py allow_all_items_to_be_field_items` | `dItem_data::randoAllowAllFieldItems` (run from `d_s_play.cpp` phase 1) copies the item get model into the field model and gives every item a pickup cylinder; `d_a_item.cpp` plays the full item get for any item, raises it by 23 and gives it heart piece physics |
| Dungeon items anywhere | `tweaks.py allow_dungeon_items_to_appear_anywhere`, `custom_funcs.asm` | the 22 dungeon-specific keys, maps and compasses (`d_item.cpp` `dItem_randoDungeonItemGet`) count for their dungeon wherever they are found; their models and pickup data are the plain ones' |
| Custom actors | `d_a_dungeon_flag_sw.cpp`, `d_a_switch_op.rel` | `SwOp` (proc 0x1F6) and `DngSw` (0x1F7), in `f_pc_name.h`, the profile list and `l_objectName`. `d_a_switch_op` ships without source and is ported from its disassembly |
| Chests | `asm/actors/d_a_tbox.cpp` | every chest opens with the short cutscene. Trap chests (an option) are step 5 |
| Shops, stone heads | `tweaks.py fix_shop_item_y_offsets`, `fix_stone_head_bugs` | shop items with no Y offset are raised; stone heads execute out of view |

Left for step 4 with the rest of `tweaks.py`: the Ballad of Gales warp to the Forsaken Fortress
(`add_new_bog_warp`) and the key and Tingle Statue counters on the menus.

## Plan

1. Rando disc.
2. The 73 code-given item locations, read from the randomizer's patched files.
3. `custom_funcs` / `custom_data` helpers as a C++ module, then `make_game_nonlinear` and
   `flexible_item_locations` (what a seed needs to be completable).
4. Progressive items, `misc_rando_features`, `remove_cutscenes`.
5. The optional settings (swordless, hero mode, remove music, ...) and enemy randomization.

The patch inventory (every hunk and the function it patches) and the reasoning are in the project's
write-up; the patches already redone for the QoL options are listed in `docs/QOL_PLAN.md`.
