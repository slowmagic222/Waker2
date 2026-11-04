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

## In the game

With `COS_RPLUS_WORLD=<path to world.rpw>` (in `[dev]` of `native/user/settings.ini`, or the environment) the game
reads the world at start (`native/src/pc/features/pc_rplus.cpp`). Then, on the Great Sea:

- **Land.** Every sea room except the two kept ones (Forsaken Fortress, Tower of the Gods) has the world's land
  instead of its island: `daBg_c::createHeap` asks `pc_rplus_terrain_create`
  (`native/src/pc/game_hooks/pc_rplus_terrain.cpp`), which samples the ground 6 times a cell, builds its collision
  as a DZB made in memory (passed through `cBgS::ConvDzb` like a room's `room.dzb`) and a draw packet with vertex
  colours by biome and light baked in two tones. The ground (`naturalY`): the cells' heights blended smoothly on a
  grid bent by noise (coasts wander off the cells), rolling hills, ridges on rock, volcanic and snow land, small
  bumps, and on some lands a mountain range (`mountainAt`: a slow mask, a broad massif and domain-warped ridged
  multifractal noise on top, up to some 7000 units, snow on the high gentle faces); then levelled where the world
  builds (`shapeGround`). Tiers are off for now (`TIERS`: the ground is `e` itself, no cliffs or stairs; steep
  faces are climbed with the QoL). With them on, that height (`e`) makes the ground in tiers
  (`tierY`): the beach rises smoothly into tier 1, and above it each tier is a nearly flat shelf with an upright
  rock cliff to the next (the mesh's triangles are cut along the crossings, `splitTri`, drawn and solid). Cliffs
  are drawn as rough rock (bulges, a set-back lip, scree at the foot, coloured in layers) over flat walls, which the
  "climb any wall" QoL can climb. The DZB has a root group and the land under it: the game checks groups for
  water and lava only at that second level, so a lone root group read as lava to Link's wall grab.
- **Layout** (`layout()`, made once from the anchors): a door (`KNOB01`) and a house for every anchor whose
  interior stage is known (`kInteriors`), each with an exit floor inside (exit ids `0x20` + its number in its room;
  Link's home, the cabana `Abesso`, is `0x3A`). Houses near each other gather into towns: round a paved plaza, the
  ground levelled, doors facing the middle, more houses without a way in, dirt paths to each door, low brick walls
  by the yards and a wooden fence round the town with gates between the houses. Paths join the towns and lone
  doors of each land, and a land whose paths are all above tier 1 gets one down to it. Stairs (`Ramp`) climb each
  cliff a path crosses: straight down onto the shelf below, or along the cliff's foot where there is no room. Link starts in the start town's plaza. Going through a door enters its interior; the
  interior's way out to the sea comes back out of that door (`pc_rplus_exit`, `pc_rplus_exit_return`).
- **Props** (`scatterProps`): thick lawns of grass in clumps, grass and flowers along paths, rocks at cliffs' feet
  and trees along shelves' edges, groves and forests, palms on beaches, rocks on rugged land, with the disc's params, nearest the towns and doors first within a budget per room. On PC the game's
  grass, flower and tree packets hold 16000 tufts, 2000 flowers and 512 trees.
- **People and enemies** (`native/src/pc/game_hooks/pc_rplus_life.cpp`): the disc's sea rooms are read once
  (`Room<n>.arc`'s `room.dzr`). The townsfolk of the generated rooms move to the towns, and NPCs the world gave an
  anchor of their own (Maggie, Zunari, Tott, the Killer Bees...) stand at it. Generated rooms have no paths
  (`dPath_GetRoomPath`), so moved people stand rather than walk. Enemies (Bokoblins, Moblins, ChuChus, Keese, Boko
  Babas, Magtails) are spread over open land by biome, away from towns and the start, with params from the
  islands and some dungeons. `COS_RPLUS_NPCS=0` or `COS_RPLUS_ENEMIES=0` leaves them out.
- **More enemy kinds** (same file): wandering Darknuts on open high ground (about one room in two, mixed armour
  colours and gear) and one lookout tower in three held by a Darknut guarding its deck; a gang of 8-14 Morths on
  grass, forest floor or dunes (about one room in two); one Phantom Ganon haunt per world (Ganon's Tower maze type:
  one sword hit and he drops his sword), in a forest clearing far from the start and towns, marked by four fires.
  Beating him leaves a chest there (kept in the camps' save). Now and then a Bokoblin, Moblin or Darknut is big
  (1.4x, twice the health) or small (0.7x); only the model grows, the hit cylinders stay. The quick-test start puts
  a Darknut and a Morth gang 3500-7000 out, Phantom Ganon 7000-14000 out and a Darknut on its tower. Log lines:
  `Darknut`, `Morths`, `Phantom Ganon`. `COS_RPLUS_DARKNUTS=0`, `COS_RPLUS_MORTHS=0`, `COS_RPLUS_PGANON=0`,
  `COS_RPLUS_SIZES=0` leave each out.
- **Start gear**: a file with no sword gets the logic's start items at sea (Hero's Sword, shield, Wind Waker,
  Wind's Requiem, Ballad of Gales, Song of Passing, sail, magic).
- **Squares near Link**: the sea's room table loads each square with only the sea floor; `requestRoomLoad` adds
  up to three squares Link is within 22000 units of (dropped past 34000), so an island over a square's edge has its
  props, people and doors on both sides.
- **Enemy camps** (`placeCamps`): up to two a room on open flat land away from the start and towns: a campfire
  (`Fire`, no switch), three to five Bokoblins round it facing in, sometimes a Moblin, two pots. The Bokoblins
  stand (type 0) instead of sitting by the fire, so they see Link and fight; camps and enemy groups need gentle
  ground round them.
- **Camp chests** (`pc_rplus_camps_frame`): when every Bokoblin and Moblin of a camp is gone (with Link within
  12000 units, so a room unloading doesn't count), the puzzle-solved jingle plays and a small wooden chest appears
  300 units from the fire. It holds filler the seed picks for that camp (mostly rupees, sometimes a silver rupee or
  three hearts), so it also brings the next house decor piece. Camp chests keep their own opened flag
  (`daTbox_c::checkOpen` / `OpenInit_com` ask `pc_rplus_camp_chest_key`), saved in `rplus-camps-<seed>-<slot>.txt`
  with the game's save. A beaten camp's chest stands there at once the next time its room loads; once opened it
  stays an open, empty chest. The enemies come back, as the game's own do.
- **Lookout towers** (`placeTower`, `COS_RPLUS_TOWERS=0` to leave out): the sea's lookout platform (`Aygr`, with
  its ladder) standing on a rise of open land, its origin (the sea's water line) at the ground under the ladder's foot, legs into the land. The ladder's collision (`aygrh.dzb`) is cut off at the origin in the loaded archive (held while at sea), so Link steps onto it from the ground, in three rooms of five. Its deck is found with a ground check once
  the tower's collision is in; one to three enemies the seed picks stand on it (Bokoblins, ChuChus, now and then a
  Moblin) and up to two Keese fly round it. Half the towers have a chest on the deck from the start; the rest get one
  when their deck's enemies are beaten. Same chest, item and save as the camps'.
- **Forests** (`forestAt`): broad patches of grassland and most of the forest biome, thinning up the mountains and
  gone at the beach. Four candidate spots per prop square there, trees half as far apart, big trees (`lwood`) among
  clusters of small ones over grass, flowers at the edges, a darker floor. The tree packet holds 2048 (420 a room).
- **Weather by biome** (`placeWeather`, `COS_RPLUS_WEATHER=0` to leave out): a weather tag (`kytag00`) over each
  12500-unit block where snow, volcanic or forest land is most of the land: snow and a grey sky, haze and a dark sky,
  light mist. The sea's daily rolled rain and storms (`dKy_event_proc`, once Link has the Wind Waker) run as before.
- **Leaving an interior**: `pc_rplus_next_stage` sits in `dComIfGp_setNextStage`, so every way out to the sea (an
  exit floor, a cave mouth, a warp, an event) lands at the interior's generated door.
- **Palms** are the straight, solid `Oyashi` (the bending `Vyasi` has no body to bump into).
- **Open-air chests** (`placeChests`, `COS_RPLUS_CHESTS=0` to leave out): each puzzle anchor gets its home island's
  disc chest (from the room's TRES), on the highest open ground near the anchor, as a plain chest (its switch,
  lighthouse or enemy puzzle is not built). Its item and save flag are the disc's.
- **Link's home** (the cabana, `Abesso`, unlocked): its dishes, pots and tubs are left out (`pc_rplus_home_actor`)
  for the house decor Link finds.
- **House decor** (`pc_rplus_decor.cpp`, `COS_RPLUS_DECOR=0` to turn it off): 169 pieces, every carryable thing
  that is not an item (the 16 `d_a_tsubo` types: pots, jug, barrel, stool, skull, bucket, seeds, crate, stone
  figures, Kutani pot; two small rocks, a boulder and a stone head; three pigs; the five Tingle statues; seven
  Tower of the Gods stones and statues; the enemy weapons are weapon unlocks instead) and the Nintendo Gallery's 134 figurines (in the
  cabana the figure alone, without its pedestal). Six (pot, large pot, barrel, stool, bucket, crate) are Link's from the start. The rest are non-progress
  check rewards: a check that holds only filler (rupees, hearts, magic, bombs, arrows, a fairy) and goes through
  `fopAcM_createItemForTrBoxDemo` (chests, sunken treasure, demo pickups) also gives the next piece Link lacks, in
  an order the seed shuffles, in the filler's place: the chest's item-get (`daDitem_c`) shows the piece's own
  model, sized like an item, and the get-item text names it (`pc_rplus_decor_show_bind`, `pc_rplus_decor_msg`);
  the filler is not given. If the piece has no model to show, the filler shows and comes as before. In the cabana every piece found stands
  where Link last set it down, else on the next free spot of rings round where the cabana's pots stood; nothing
  breaks there (sword, bomb or throw: `d_a_tsubo`, `d_a_stone`, `d_a_stone2`, which also stands again where it
  lands), and a figurine is lifted and set down like a Tingle statue (`daObjFigure_c::pcDecorExecute`). At most
  `COS_RPLUS_DECOR_FIGURES` (48) figurines stand there at once, the last found. What was found and where it stands
  is kept per save file in `rplus-decor-<seed>-<slot>.txt` next to the memory card, written with the save
  (`dSv_info_c::memory_to_card`) and read with the file (`card_to_memory`).
- **Weapons on B** (`pc_weapon_unlocks.cpp`, `COS_RPLUS_WEAPONS=0` to turn it off): the six enemy weapons Link
  can pick up (`d_a_boko`: Bokoblin staff, machete, Stalfos mace, Darknut sword, Moblin spear, Phantom Ganon's
  sword). One found in a chest is his for good and goes on B in place of the sword (one picked up from an enemy
  that he doesn't own is on B for now only: not saved, and A or the weapon button drops it as the game does): B swings it, and A puts it away
  (Put away where the game says Throw, `setDoStatusBasic`; `pcRplusPutAway` takes it out of his hand with the sword's
  sheathing sound, not dropped); it stays on B, and B draws it again as it does the sword. While a weapon is on B
  and his hands are free he holds it again of his own, after a door, an item, a ladder or a swim
  (`daPy_lk_c::pcRplusWeaponFrame` makes it in his hand). The weapon button (`COS_RPLUS_WEAPON_BUTTON`: L, D-pad
  down or a stick click, Quality of life tab) switches B to his next weapon, then back to the sword after the last; with the sword in his hand it sheathes the sword and draws the weapon.
  They are also check rewards: a filler check the seed picks (about the 2nd, 5th, 9th, 14th, 20th and 27th, each
  up to two later, the weapons in a seed order) gives one in place of that check's decor piece: the chest shows the
  weapon and its own get-item text (`pc_rplus_decor_show_weapon`) instead of the filler, and it goes on B.
  `COS_RPLUS_WEAPONS_START=1` ("Weapons from the start", off by default) gives all six at once. Kept per save file
  in `rplus-weapons-<seed>-<slot>.txt` (found, filler checks, what is on B). Not yet: slots of their own in the
  item screen. The file is not named `pc_rplus_*` so that a push of it does not start the Steam Deck workflow.
- **Fairy springs** (`placeFairies`, `COS_RPLUS_FAIRIES=0` to turn them off): in half the rooms, one glade on flat
  open grass or forest high ground (away from the start and the towns) with 3 or 4 healing fairies (`Sfairy`, type
  0) flying round it. Where comes from the seed and the room alone.
- **Beedle's stands**: 3 to 5 a world, each two or three cells off a house or person's anchor (so often in a town),
  at least 12 cells apart. A stand is a generated door (house look) into Beedle's shop (`Obshop` room 1, the
  regular shop, not Rock Spire's). Going in sets the ship id to 0xFE and the ship's room to the stand's, so his
  shop's way out (exit 0x3D) brings Link back out of the same stand (`pc_rplus_next_stage`; `doorOfStage` prefers
  the door Link went in by). In by one of his real ships, Link comes back out on its deck as before.
- **Calling the boat**: the Wind's Requiem played on land also moves the boat to the shore nearest Link
  (`pc_rplus_call_ship`, the same shore search that places the boat when a square loads).
- The generated rooms load none of their own actors (`dStage_dt_c_roomReLoader`); the far-away island models
  (`d_a_lod_bg.cpp`) are off; the sea chart and the QoL sea map draw the generated land.

An offline map of the land and layout: build `pc_rplus_terrain.cpp` with `PC_RPLUS_DEBUG_MAP` and call
`pc_rplus_debug_map(path, units per pixel, centre x, centre z, size)` (a PPM).

To try it on a new file, skipping the intro:

```ini
[dev]
COS_RPLUS_WORLD=/path/to/world.rpw
COS_BOOT_STAGE=sea:44:8
COS_BOOT_PRESET=sailing
```

### Quick-test start

For testing new things fast (Dillon, 2026-11-02): a world with a `test 1` line (`worldgen.py --quick-test`), or any
world with **Test start (Randomizer+)** on in the Quality of life tab or `COS_RPLUS_TEST_START=1` in the environment
(`=0` there turns a test world's off), puts by Link's start:

- 12 plain chests in a ring 700-2600 units out, facing the start. Each holds rupees or hearts (filler), so opening
  it brings a pickup weapon (the 1st, 3rd, 5th ... 11th filler check give the six weapons, `pc_weapon_unlocks.cpp`)
  or the next house decor piece (the others).
- an enemy camp 3000-6500 out (Bokoblins and a Moblin), its chest when they are beaten;
- a lookout tower 4500-9000 out, its chest once the enemies on its deck are beaten.

Their opened flags are kept as camps' are (`rplus-camps-<seed>-<slot>.txt`, keys 150-163). The log has a
`[rplus] test start at ...` line and one per chest. `pc_rplus_life.cpp`, `placeTestStart`.

### Discovery tracker

Asked by Dillon (2026-11-02): with a Randomizer+ world, the tracker screen (L3+R3 / F2) shows the generated world and
only the places Link has found (`native/src/pc/game_hooks/pc_rplus_places.cpp`, drawn by
`pc_tracker_overlay.cpp`).

- **Places:** the towns (named after the world file's village most of their doors belong to; the start's is
  "Home"), every generated door (houses, caves, dungeon entrances, fairy fountains, Link's home), the NPCs standing at
  anchors of their own (Maggie, Zunari, Tott...), and the enemy camps and lookout towers once their rooms make them.
- **Found:** a door (house, cave, dungeon, fountain) only once Link goes through it (in its interior); the rest
  when Link on the sea comes within reach of it (an NPC 1800, a camp 3500, a tower 5000, a town its ring + 2000). Undiscovered places are not shown at all; the header says
  "Places found N / M" (camps and towers are not in M). Link's home is known from the start.
- **The chart:** the world's land in its biomes' colours; squares not sailed in yet (the save's arrive bits, as the
  sea chart; a found place opens its square too; every square with Reveal chart) stay dark. A mark per found place:
  town diamond (with its name), house square, cave / dungeon triangle, NPC / fountain dot, camp cross, tower bar;
  grey once all its checks are done. Each square's top-left number: its found places' checks not done.
- **The list:** a square's found places, towns first, each with its wwrando checks ticked as the game has them
  (pc_tracker's location table, `locationDone`); a camp or tower has its chest. The tracker's own logic is the vanilla
  world's, so "in logic" is left out for Randomizer+ (and the "checks opened up" pop-up).
- **Pop-up:** "Discovered!" and the place's name, with the chime (the tracker pop-ups' setting).
- **Saved:** `rplus-places-<seed>-<slot>.txt` next to the saves, with the save (as the camps' file).
- **World file:** `worldgen.py` writes `name`, `check`, `village` and `member` lines. An older world gets them from
  its `world.json` with `python3 scripts/rplus/rpw_add_places.py world.json world.rpw`; without them places are
  named after their anchor ids and have no checks listed.

### The Randomizer+ tab

The options menu has a **Randomizer+** tab (`native/src/pc/features/pc_rplus_menu.cpp`):

- **Make a world**: runs `worldgen.py` (bundled in `seedgen/rplus/` by `scripts/seedgen/bundle.sh`, run by the
  same bundled Python as the Randomizer tab) with a random number, into `user/rando/rplus/<number>/`, and makes it
  the world to play. At the title screen the game restarts into it at once. Not on the Switch (no Python): there,
  copy worlds to `user/rando/rplus/<name>/world.rpw`.
- **World to play**: the launcher's (`COS_RPLUS_WORLD`), off, or any world in `user/rando/rplus/`.
- **Starting sword**: on (a new file starts with the Hero's Sword, as wwrando's default) or off (Link starts
  without one and finds it in a check; make the seed with Sword Mode "No Starting Sword" on the Randomizer tab so its
  logic and item pool match). Applies to new files (`giveStartGear`, `pc_rplus_start_sword`).
- **Seed to play**: the Randomizer tab's row, here too.
- **Save files**: shared (one memory card) or own for each seed: a card per randomizer seed and world in
  `user/saves/<seed>[+<world>]/`, with its 3 files.
- **Restart with these**: restarts the game (Linux) with the choices above.

The choices are kept in `user/rando/rplus.ini`; `pc_rplus_boot` applies them at start (`COS_RPLUS_WORLD`, and
`COS_SAVE_DIR` for the card's folder, given to Aurora in `pc_aurora_init`).

Items are not placed in the game yet.

### Skies: each island's day, night and weather

`native/src/pc/game_hooks/pc_rplus_weather.cpp`. Every land mass is an island, and every sea cell belongs to the
island nearest it. From the world's seed each island gets a clock offset (up to 6 hours either way) and a day
length (9 to 15 hours of daylight), and for each 8 game hours a weather picked by its biome: clear, rain, snow
(snow islands only, a blizzard when stormy) or storm (heavy rain, thunder). The same seed always gives the same
skies. Only the sky changes: the save keeps one clock for the game's rules, and the island's time goes into
`g_env_light.mCurTime`, which the sun, moon, stars and palettes are drawn from. Rain and snow go up to 500
drops (the game's limit is 250), drawn stronger in storms; clouds, sky, land and actors are shaded darker and fog
pulled in. Crossing into another island's zone eases to its time and weather over a few seconds. The log prints
`[rplus] weather: island N (biome) <weather> <strength>` on each change.

## Not done yet

- **Textures** on the land (vertex colours only so far), a corner minimap for generated squares, the Ballad of
  Gales' warp spots on the generated land.
- **Places not built yet**: the islands' puzzle chests and landmarks (Wind Shrine, Tingle's tower, lighthouse...),
  the open sea's platforms, submarines and ships, a mailbox. A seed made in game with a Randomizer+ world loaded
  (`waker2_seedgen.py generate --rplus`) puts no progress items there (`RPLUS_OFF`), so it stays beatable.
- **Item placement on the disc.** wwrando writes items into the stage files of their original rooms. A generated
  world needs those items to follow their anchors: the loader gives each actor its item from `world.json`.
- **NPC code that knows its island.** About 150 places in the game check a room number or the stage `sea`
  (`dIsleRoom_...`). Each moved NPC has to be checked.
- **Link's new house** as an Animal Crossing-style home: the house decor is in (above); furniture and pictures
  tied to in-game events, decor in chests and the shop, and a music player are not.
- Options other than wwrando's defaults: trick levels, more progression types, swordless.
