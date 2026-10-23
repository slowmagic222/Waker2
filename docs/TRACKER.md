# Randomizer tracker (in the game)

Status (2026-10-22): the game reads its own save, and on a rando disc runs [wooferzfg's TWW Randomizer
Tracker](https://www.wooferzfg.me/tww-rando-tracker/)'s own logic on it; L3+R3 (or F2) shows the tracker
screen in the game, in the tracker's art. No browser, no server: it works the same on the Switch. Not yet
run in the game with a real seed (see "Checked so far").

Goes with rando mode ([RANDO.md](RANDO.md)), but the screen works with the original game too (items and
checks, no logic).

## The idea

The tracker stays the tracker: its logic (wwrando's `logic/` files for the seed's permalink), its
colours, its art and its manual marks. What the game adds is the part a player does by hand: clicking
each item picked up and each location checked. So the game reads its save every 15 frames and writes the
tracker's own data model, in the tracker's names:

| Tracker field | From the save |
|---|---|
| items (`"Progressive Sword": 2`) | inventory slots, the collect bits (swords, shields, bracelets, Hero's Charm), songs, pearls, Triforce Shards, wallet / magic / quiver / bomb bag sizes, bottles, the Delivery Bag's letters, charts owned, each dungeon's map, compass and big key; rando mode's Hurricane Spin and Tingle Statue event bits (6901, 6A04-6A40, as the rando branch sets them) |
| small keys | held now, per dungeon (see "Limits") |
| locations checked | 268 of wwrando's 320 locations by their flag: a stage's chest, switch or item bit (the save's, or the current stage's memory not yet put back), an event bit, an ocean bit (Big Octos, gunboats), and two special cases (Ankle's reward, Maggie's delivery) |
| sunken treasures (49) | the charts salvaged ("complete" map bits); the tracker's chart mapping says which island each chart leads to |

The location flags come from [Archipelago](https://github.com/ArchipelagoMW/Archipelago)'s Wind Waker
client (`worlds/tww`, MIT), which reads them from Dolphin's memory by GameCube address.
`native/tools/gen_tracker_tables.py` turns each address into the save field the decompilation names
and writes `native/src/pc/features/pc_tracker_tables.inc`; it checks every sunken treasure's bit
against the chart's collect-map number in `game/src/d/d_item.cpp` (all 49 agree), and with
`--wwrando` the names against wwrando's `logic/item_locations.txt`.

## Use

Press both sticks in together (L3+R3, on any controller; the Switch's too) or F2.

1. The tracker screen: the game pauses, and the screen shows the tracker's items, its sea chart with each
   island's checks "in logic / not checked", and the dungeons (keys, map, compass, big key, counts), in
   wooferzfg's tracker's own art. The stick or the D-pad moves a cursor over the items, the islands and
   the dungeons. On a place, the list on the right says how many of its checks are checked and names
   them, coloured as the tracker colours them: blue in logic, red not yet, and checked ones grey, ticked
   and struck out. A moves into the list, and A there marks a check by hand (or unmarks it). On an item
   it shows its name and count; items come from the game only and cannot be changed. B goes back, and B
   again closes the screen.
   The screen is drawn over the paused game, which shows through, in the game's own font (its message
   font, read from the game; ImGui's until the game has loaded it), with the pause menu's own item and
   dungeon item icons (read from the game's `itemicon.arc`; the tracker's art where the game has none,
   such as the Triforce and the charts). The first time the screen opens the log lists the pause menu's
   archives (`[cos] tracker: itemres.arc: ...`), for the menu's frames next. A green ring marks where Link is: the
   sea sector he sails in (on an island's own stages, the last one, dimmer) or the dungeon he is in, and
   the cursor starts there.
2. A second press: a small panel in a corner while the game runs (in logic / left).
3. A third press hides it.

Nothing to turn on; the default button map gives neither stick click to the game. With the options menu
open it is hidden. The checks marked by hand are kept per seed and save slot in
`user/tracker/<disc>-<size>-file<N>-tracker.json`, next to the progress file, as soon as it is set.

**The logic** is the tracker's own answer: the game runs the tracker's logic code (its `src/services`,
bundled by `scripts/tracker/build_logic.sh`) in QuickJS on a thread of its own, on the seed's settings and
wwrando's logic files, and merges the game's state into the tracker's (`scripts/tracker/logic/autotracker.js`):

- an item is set when the game's count changed since the last state (the first state keeps the higher
  of the tracker's and the game's count);
- a small key count gains what the game's held count gains;
- locations are only ever checked, never unchecked;
- another save slot starts over.

It needs:

- the `tracker-logic/` folder next to the game (on the Switch, next to `user/`; `COS_TRACKER_LOGIC=<dir>`
  elsewhere), which `scripts/tracker/build_logic.sh <dir>` makes: the tracker's logic bundle, its art in
  one atlas, wwrando's `item_locations.txt` and `macros.txt` at `9775811`. The tracker is pinned to
  `dd48285`, its next-rando-version branch: its main branch reads only wwrando 1.10.0 permalinks.
- the seed's permalink, from the text next to the rando disc: `<disc> - Permalink.txt` (the in-game seed
  generator writes it) or the `Permalink:` line of wwrando's `<disc> - Non-Spoiler Log.txt` /
  `- Spoiler Log.txt`, `<disc>` being the disc's path without `.iso`.

Without either, the screen says which is missing and shows the items and checks only.

## The state (version 1)

What the game hands the logic, as JSON (`pc_tracker.cpp`):

```json
{"version": 1, "game": "GZLE99", "rando": true, "frame": 1234, "playing": true, "saveSlot": 0,
 "stage": "sea", "room": 44,
 "items": {"Progressive Sword": 2, "Wind Waker": 1, "Treasure Chart 25": 1, "DRC Big Key": 0, ...},
 "smallKeysHeld": {"DRC Small Key": 1, "FW Small Key": 0, ...},
 "locations": {"Outset Island": {"Underneath Link's House": true, ...}, ...},
 "salvagedCharts": ["Treasure Chart 25"], "keptChecks": 0}
```

Outside play (title, file select): `{"version": 1, "game": ..., "playing": false}`. Every item and
location the game can tell is listed, false and 0 included; what it cannot tell is left out.

## Progress kept on disk

The state is read from the save in memory, so a crash (or loading an older save) used to take back every
check made since the last save. With a randomizer disc the game also keeps what it has seen on disk, per
seed and file slot: `user/tracker/<randomizer .iso name>-<size>-file<N>.txt` next to the game
(`~/waker2/user/tracker/` on the Deck, `COS_TRACKER_SAVE_DIR=<dir>` elsewhere). A location checked, an item
count reached or a chart salvaged is written as soon as it is seen, and stays in the state (the tracker screen
and its logic) even when the save in memory no longer has it. `keptChecks` (and a line on the screen)
counts the checks kept that the save in memory does not have: those chests are closed again in the game
and their items are not held. Small keys held are not kept (they go down when used). Delete the file to
start a seed's file over.

## Limits

- **Small keys:** the save holds the keys held, not the keys found, so the tracker adds what the held
  count gains between two states (every 15 frames on a rando disc).
- **Untracked locations:** Orca's 500 hits and Beedle's two membership letters (no flag known; listed at
  the end of `pc_tracker_tables.inc`). Kane's four gate rewards use flags Archipelago left unverified.
- **Names:** the table uses wwrando's current names; a tracker on an older wwrando version that names a
  location differently simply does not get that one (the sunken treasures under "Forsaken Fortress
  Sector" / "Tower of the Gods Sector" are handled both ways).
- **Randomized charts:** the sunken treasure goes to the island the tracker's chart mapping names for
  that chart; with randomized charts and no mapping entered yet, it waits.
- **Entrances** are not read yet.

## Checked so far

- `pc_tracker.cpp`, `pc_tracker_logic.cpp` and `pc_tracker_overlay.cpp` compile against the game's,
  Aurora's ImGui and SDL 3 headers (clang, syntax only; the cloud machine cannot build the whole game).
- The logic: `pc_tracker_logic.cpp` with QuickJS, built on its own with `build_logic.sh`'s folder and a
  9775811 permalink, worked out a sample state (19 in logic of 118 left) in about 0.6 s the first time,
  and a check marked by hand moved the totals. The merge's tests (`autotracker.test.js`) pass in the
  tracker's jest.
- The screen: drawn with Aurora's ImGui in a small SDL harness from the same code (screenshots of the
  items, the sea chart, a dungeon, the list and a mark by hand).
- Not yet: the whole game, on the Deck or the Switch, with a real seed.

## Plan

1. The state from the save. (Done.)
2. The tracker inside the game, with its logic and art, marks by hand. (Done; untested in the game.)
3. Next: keep the marks made by hand with the save; the chart mapping and entrances for randomized
   charts and entrances; a nicer font; the `tracker-logic/` folder in the Switch package.

## Files

- `native/include/pc/pc_tracker.h`, `native/src/pc/features/pc_tracker.cpp`: the state, built after each
  15th game frame (`pc_frame.cpp`) on a rando disc or while the screen is shown.
- `native/src/pc/features/pc_tracker_overlay.cpp`: the tracker screen and the small panel (ImGui) and
  their toggle.
- `native/src/pc/features/pc_tracker_logic.cpp`, `native/cmake/quickjs.cmake`: the tracker's logic in
  QuickJS (quickjs-ng, MIT) on its own thread; the seed's permalink.
- `scripts/tracker/build_logic.sh`, `scripts/tracker/logic/`: the `tracker-logic/` folder: the logic
  bundle (`entry.js`), the merge of the game's state (`autotracker.js` and its tests), the art atlas
  (`pack_art.js`, `images_map.js`), wwrando's logic files.
- `native/src/pc/features/pc_tracker_tables.inc`, `native/tools/gen_tracker_tables.py`: the location and
  chart tables and their generator (`--ap <Archipelago>/worlds/tww --ap-rev <commit> --wwrando <wwrando>`;
  `--check` compares with the committed file). Made from Archipelago `4036a35` and wwrando `9775811`.
- `.github/workflows/steamdeck.yml`: puts `tracker-logic/` in the Deck package.
