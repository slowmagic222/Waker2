# Waker2 on the Switch (test build)

The Switch build of Waker2: the game with randomizer seeds, the in-game tracker, the QoL options and the
jump and climb options. Built by `.github/workflows/switch.yml`. Nothing of the game comes with it: you
supply your own disc. For the Switch itself (Atmosphère, the Homebrew Menu, title mode) see
[INSTALL.md](../INSTALL.md).

## SD card

Copy the zip's `switch` folder to the root of the SD card (merge it with the one there), then add your
discs:

```
sdmc:/switch/switchwaker/
├── switchwaker.nro                     the game ("SwitchWaker" in the Homebrew Menu)
├── GZLE01.iso                          YOUR clean USA disc, revision 0, uncompressed, named exactly so
├── initial_pipeline_cache.db
├── initial_dksh_cache.bin
└── native/
    ├── tracker-logic/                  the tracker's logic and art (from the zip)
    └── user/rando/seeds/
        └── WW Random <seed>.iso        your randomizer seeds (any name ending in .iso)
```

Seeds are made on a computer: the in-game generator needs Python, so its Generate row says "not on the
Switch". Use the Wind Waker Randomizer 1.10.0 (or the Steam Deck build's generator) and copy the output
`.iso` into `native/user/rando/seeds/`. Keep its `... - Permalink.txt` and spoiler log on the computer.

## Playing a seed

1. Start the Homebrew Menu in title mode (hold **R** while starting any game) and pick **SwitchWaker**.
2. **Minus (−)** opens the options menu. In the **Randomizer** tab, set **Seed to play** to your seed.
3. Close the game (HOME, then close it) and start it again: it boots into the seed. "None" plays the
   plain game.

In the game, press both sticks in (**L3+R3**) for the tracker. The QoL, jump and climb options are in the
options menu, as on the Deck.

## If something goes wrong

The log of each run is in `sdmc:/switch/switchwaker/native/logs/` (the newest is the last run). Keep the
build's `switchwaker-<sha>.elf` (the second download) to name a crash's address.
