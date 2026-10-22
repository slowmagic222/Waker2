# Randomizer tracker (autotracking)

Status: **step 1 of the plan below, and the start of step 3** (written 2026-10-22). The game reads its
own save and serves what [wooferzfg's TWW Randomizer Tracker](https://www.wooferzfg.me/tww-rando-tracker/)
tracks; a patch for the tracker reads it. The game also draws a small tracker over itself (L3+R3 or F2).
Not yet run in the game with a real seed (see "Checked so far").

Goes with rando mode ([RANDO.md](RANDO.md)), but works with the original game too (the item counts and
most locations mean the same there).

## The idea

The tracker stays the tracker: its logic (wwrando's `logic/` files for the seed's permalink), its
layout and its manual marks. What the game adds is the part a player does by hand: clicking each item
picked up and each location checked. So the game reads its save every half second and writes the
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

**The overlay, in the game:** press both sticks in together (L3+R3, on any controller) or F2. The first
press shows the items page (items with their counts, each dungeon's map, compass, big key, keys held and
checks), the second the locations page (checks per island and dungeon, sunken treasures), the third hides
it. Nothing to turn on, and the game keeps its input (the default button map gives neither stick click
to the game). It shows what the game knows, not the tracker's logic (what is in logic needs the seed's
settings; plan step 3). With the options menu open it is hidden.

**The game's server:** `COS_TRACKER_SERVER=1` (environment on Linux and the Mac, the `[dev]` section of the
settings file on the Switch). The log says `[cos] tracker: state at http://127.0.0.1:6544/state`.
A number other than 1 is the port. It listens on 127.0.0.1 on a computer and on every address on the
Switch (the tracker runs elsewhere); `COS_TRACKER_BIND=<ipv4>` overrides that.

**The tracker, served by the game:** with `COS_TRACKER_WEB=<dir>` the same server also serves the
files of that directory, so `http://127.0.0.1:6544/` is the tracker itself and it autotracks with no
setup (it finds the state next to the page). `scripts/tracker/build_tracker.sh <dir>` makes that
directory: wooferzfg/tww-rando-tracker at `1c6d5a0` (GPL-3.0) with
`scripts/tracker/tww-rando-tracker-autotrack.patch`, its tests, its production build. The Steam Deck
package has it in `tracker/` and its launcher sets this up ([STEAM_DECK.md](STEAM_DECK.md), "The
tracker"); its launcher opens it in the browser when the game starts (Desktop Mode), and
`scripts/steamdeck/waker2-tracker.sh` opens it again.

**The tracker, elsewhere:** the patched tracker reads any game's address with `?autotrack=1` (the game
on 127.0.0.1:6544) or `?autotrack=http://<address>:6544` after its URL
(`http://localhost:8080/#/tracker/new/<permalink>?autotrack=1` from `npm start` in a patched checkout);
`?autotrack=0` turns autotracking off. A toast says when it connects. It polls once a second and merges:

- an item is set when the game's count changed since the last poll (the first poll keeps the higher
  of the tracker's and the game's count), so a mark made by hand stays until the game's count moves;
- a small key count gains what the game's held count gains;
- locations are only ever checked, never unchecked;
- another save slot starts over.

The hosted tracker (https) cannot read the Switch's plain-http address (mixed content); the tracker
the game serves can (`COS_TRACKER_BIND=0.0.0.0`, then `http://<address>:6544/`), and so can the
hosted one for 127.0.0.1 once the patch is upstream.

`scripts/tracker/fake_game.py [state.json]` serves a state the way the game does, to try the tracker
side without the game.

## The state (GET /state, version 1)

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
count reached or a chart salvaged is written as soon as it is seen, and stays in the state (the overlay,
`GET /state`) even when the save in memory no longer has it. `keptChecks` (and a line on the overlay)
counts the checks kept that the save in memory does not have: those chests are closed again in the game
and their items are not held. Small keys held are not kept (they go down when used). Delete the file to
start a seed's file over.

## Limits

- **Small keys:** the save holds the keys held, not the keys found, so the tracker adds what the held
  count gains. Keys picked up while the tracker is not polling are missed.
- **Untracked locations:** Orca's 500 hits and Beedle's two membership letters (no flag known; listed at
  the end of `pc_tracker_tables.inc`). Kane's four gate rewards use flags Archipelago left unverified.
- **Names:** the table uses wwrando's current names; a tracker on an older wwrando version that names a
  location differently simply does not get that one (the sunken treasures under "Forsaken Fortress
  Sector" / "Tower of the Gods Sector" are handled both ways).
- **Randomized charts:** the sunken treasure goes to the island the tracker's chart mapping names for
  that chart; with randomized charts and no mapping entered yet, it waits.
- **Entrances** are not read yet.

## Checked so far

- `pc_tracker.cpp` and `pc_tracker_server.cpp` compile against the game's headers (clang, syntax only;
  the cloud machine cannot build or run the game). The server was built on its own and answered
  `curl` (GET /state, the CORS preflight, 404).
- The tracker patch: the tracker's 549 tests pass (the new `autotracker.test.js` included, 100%
  coverage as the tracker requires), eslint is clean, webpack builds. In headless Chromium the
  patched tracker, opened on a 1.10.0 permalink with `?autotrack=`, read a sample state from the
  server above and showed the items, the checked locations and the salvaged chart's sunken treasure.
- The tracker served by the game: the server, built on its own with `COS_TRACKER_WEB` pointing at
  `build_tracker.sh`'s output, served the tracker at `/`; opened there in headless Chromium with no
  parameter, it found the state by itself and showed the same items and checks.
- `pc_tracker_overlay.cpp` compiles against the game's, Aurora's ImGui and SDL 3 headers (syntax only).
- Not yet: a real game. First test: `COS_TRACKER_SERVER=1`, a seed, `curl http://127.0.0.1:6544/state`
  after opening a chest and picking up an item.

## Plan

1. The state from the save, served over HTTP; the tracker's autotracking patch. (This step.)
2. Run it with a seed; send the autotracking upstream to wooferzfg's tracker (or keep a fork) so the
   hosted tracker can use it; read the chart mapping and entrances from the randomizer's disc.
3. A tracker inside the game: an overlay drawn with the ImGui the options menu already uses
   (`pc_menu.cpp`), from the same state. Items and checked counts per area are in
   (`pc_tracker_overlay.cpp`); next, the tracker's logic (which locations are in logic) once the
   permalink's settings can be read from the seed.

## Files

- `native/include/pc/pc_tracker.h`, `native/src/pc/features/pc_tracker.cpp`: the state, built after each
  15th game frame (`pc_frame.cpp`) while the server runs or the overlay is shown.
- `native/src/pc/features/pc_tracker_overlay.cpp`: the overlay (ImGui) and its toggle.
- `native/src/pc/features/pc_tracker_server.cpp`: the HTTP server (one thread, host memory).
- `native/src/pc/features/pc_tracker_tables.inc`, `native/tools/gen_tracker_tables.py`: the location and
  chart tables and their generator (`--ap <Archipelago>/worlds/tww --ap-rev <commit> --wwrando <wwrando>`;
  `--check` compares with the committed file). Made from Archipelago `4036a35` and wwrando `9775811`.
- `scripts/tracker/`: the tracker patch, `build_tracker.sh` (the patched tracker's build, for
  `COS_TRACKER_WEB`) and `fake_game.py`.
- `scripts/steamdeck/waker2-tracker.sh`, `.github/workflows/steamdeck.yml`: the Deck package's tracker.
