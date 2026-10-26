# Randomizer+: a generated Great Sea

Randomizer+ replaces the Great Sea's islands with a new, generated world: land masses of every size, from
one-cell rocks to continents across several chart squares. Every NPC, building, cave, fairy fountain and dungeon
door the randomizer needs is placed somewhere on it. The interiors and dungeons themselves are not changed.

First step: a **logic test**. `scripts/rplus/` generates worlds offline and checks that each one can be beaten. It
doesn't touch the game yet. How the world looks comes later and will use the game's own textures and terrain
pieces.

## What a world has to contain

`scripts/rplus/anchors.py` sorts every one of wwrando's 320 item locations by what the player has to reach to get
it:

| | Count | Where the generator puts it |
|---|---|---|
| Interior doors (houses, shops, Rito Aerie, Forest Haven, cabana, pirate ship ...) | 16 | lowland or shore of land of some size |
| Secret cave doors | 20 | hills and cliffs |
| Great Fairy fountains | 6 | hills and cliffs |
| Dungeon entrance doors (DRC, FW, ET, WT) | 4 | land of their biome (volcanic, forest, rock), one per land |
| NPCs outdoors (Tott, Maggie, Zunari, Mila, Kamo, Ivan ...) | 10 | lowland, spread over many lands |
| Outdoor landmarks (lighthouse, town gate, wind shrine, Tingle Tower, postboxes, withered trees ...) | 8 | shore or anywhere on land |
| Puzzle chests on the terrain | 13 | hills and peaks |
| Ships and objects at sea (Beedle, Salvage Corp, Gorons, Ghost Ship, Islet of Steel, Cyclos ...) | 8 + 51 | open water |
| Sunken treasure | 49 | open water in its chart square (the charts point at squares) |
| Kept as they are | 2 | Forsaken Fortress (room 1), Tower of the Gods with Hyrule (room 26) |
| Dungeon locations | 96 | unchanged, behind their doors |

These 47 interior stages sit behind doors: `python3 scripts/rplus/anchors.py --csv` prints the full table, with
each door's interior stage, the actors that give the items, the original island and the obstacle at the door.

## How a world is made (`worldgen.py`)

1. **Land.** Noisy blobs on a 84x84 cell grid (12 cells per chart square), between about 45 and 60 of them,
   tiny to huge. Large ones get extra lobes so they aren't round. The two kept squares stay open water.
2. **Biomes and terrain obstacles.** Each sizeable biome gets at least one land of reasonable size. Some lands get
   cliffs all round (Hookshot, Grappling Hook or Deku Leaf to land), a blocked path up (boulder, heavy stone,
   cracked wall, gap, cliff step, gorge) or a summit you need an item to reach.
3. **Villages.** Between 3 and 6 of them, each with a generated name, in a different place every seed. Each
   takes 2 to 4 of the town buildings (jail, shops, school, Lenzo, House of Wealth, cafe, auction, Battlesquid,
   Mesa, Orca, the pirate ship) and the townsfolk whose quests need a town around them (Sam, Kane's gate, Mila,
   Ivan, Pompie and Vera, Kamo, Minenco, Linda and Anton, Maggie, Zunari), and the lighthouse goes on a village
   shore.
4. **Anchors.** Link's house (the start) goes on open land. Forest Haven, the Forbidden Woods door and the
   small-island chest share one forest land, because you reach the Woods by gliding from Forest Haven. The
   transparent chest goes on the lighthouse's land. Everything else is spread out, with fewer anchors per land
   where there are already many. Each land with two or more anchors gets a postbox. Four withered trees go on four
   lands.
5. **Logic.** Each anchor's requirement is what it takes to reach its site (landing, path up, peak) AND the
   obstacle kept at its door from wwrando's macros (the boulder in front of a cave, the ice over the Fire
   Mountain cave ...). The two are combined in wwrando's own `Can Access ... Entrance on ...` macros, or in each
   location's need if the anchor has no door macro.
6. **Items.** An assumed fill places the dungeon keys in their dungeons, then the progression items, using
   wwrando's default progression options. The seed passes when every placed item can be collected from the start
   and Ganondorf can be beaten. If a world can't be filled, the generator makes a new one.

`wwlogic.py` reads wwrando's `logic/item_locations.txt` and `logic/macros.txt` as data. None of wwrando's code
runs, so the same rules can move to C++ later, which the Switch needs because it has no Python.

## Running it

```sh
export WWRANDO_DIR=/path/to/wwrando        # a checkout of the pinned wwrando (9775811); only its logic/ is read
python3 scripts/rplus/worldgen.py --seed 7 --out build/rplus   # world.json, spoiler.txt, map.png
python3 scripts/rplus/worldgen.py --test 200                   # many seeds, report how many are beatable
```

`world.json` contains each land (biome, size, chart squares, obstacles), each anchor's cell, chart square, sea
room, sea position in game units and requirement, the sea objects and the item placement. The in-game loader
will read this file.

## In the game (first step: the land)

With `COS_RPLUS_WORLD=<path to world.rpw>` (in `[dev]` of `native/user/settings.ini`, or the environment) the game
reads the world at start (`native/src/pc/features/pc_rplus.cpp`). Then, on the Great Sea:

- every sea room except the two kept ones (Forsaken Fortress, Tower of the Gods) has the world's land instead of
  its island: `daBg_c::createHeap` asks `pc_rplus_terrain_create` (`native/src/pc/game_hooks/pc_rplus_terrain.cpp`),
  which builds the surface from the cell heights (bilinear between cell centres, 4 samples a cell, a little noise),
  its collision as a DZB made in memory (passed through `cBgS::ConvDzb` like a room's `room.dzb`), and a draw
  packet with vertex colours by biome (grass, forest, rock, volcanic, snow, sand beaches), light baked in two tones;
- the generated rooms load none of their own actors (`dStage_dt_c_roomReLoader`), so no NPC, chest or door of the
  old island is left floating;
- the far-away island models (`d_a_lod_bg.cpp`) are off;
- Link is put on the start cell (where Link's house will be) once the sea is loaded.

To try it on a new file, skipping the intro:

```ini
[dev]
COS_RPLUS_WORLD=/path/to/world.rpw
COS_BOOT_STAGE=sea:44:8
COS_BOOT_PRESET=sailing
```

### The Randomizer+ tab

The options menu has a **Randomizer+** tab (`native/src/pc/features/pc_rplus_menu.cpp`):

- **Make a world**: runs `worldgen.py` (bundled in `seedgen/rplus/` by `scripts/seedgen/bundle.sh`, run by the
  same bundled Python as the Randomizer tab) with a random number, into `user/rando/rplus/<number>/`, and makes it
  the world to play. At the title screen the game restarts into it at once. Not on the Switch (no Python): there,
  copy worlds to `user/rando/rplus/<name>/world.rpw`.
- **World to play**: the launcher's (`COS_RPLUS_WORLD`), off, or any world in `user/rando/rplus/`.
- **Seed to play**: the Randomizer tab's row, here too.
- **Save files**: shared (one memory card) or own for each seed: a card per randomizer seed and world in
  `user/saves/<seed>[+<world>]/`, with its 3 files.
- **Restart with these**: restarts the game (Linux) with the choices above.

The choices are kept in `user/rando/rplus.ini`; `pc_rplus_boot` applies them at start (`COS_RPLUS_WORLD`, and
`COS_SAVE_DIR` for the card's folder, given to Aurora in `pc_aurora_init`).

Doors, NPCs, villages and items are not placed in the game yet: this step is only the land, to see it and walk and
sail on it.

## Not done yet

- **The game side.** The game doesn't use the world yet. Next comes a loader that builds the terrain of each sea
  room from `world.json` (mesh and collision made in memory, see `dBgW_NewSet`), sets door exits to the interior
  stages and places the NPCs and objects as actors.
- **Item placement on the disc.** wwrando writes items into the stage files of their original rooms. A generated
  world needs those items to follow their anchors: the loader gives each actor its item from `world.json`.
- **NPC code that knows its island.** About 150 places in the game check a room number or the stage `sea`
  (`dIsleRoom_...`). Each moved NPC has to be checked.
- **Link's new house** as an Animal Crossing-style home: unlockable furniture and pictures tied to in-game events,
  a music player. Its own phase after the first playable world.
- Options other than wwrando's defaults: trick levels, more progression types, swordless.
