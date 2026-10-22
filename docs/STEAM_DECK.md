# Waker2 on the Steam Deck (test build)

A test build of the Linux x86_64 game for SteamOS, made by `.github/workflows/steamdeck.yml` without the
disc. Nothing of the game comes with it: it reads your own discs every time it starts. Untested on a Deck so
far; the Linux build has only run in a container with software Vulkan (README.md, "Status").

## What you need

- `GZLE01.iso`: your own **USA** disc of The Wind Waker, revision 0, as an uncompressed `.iso` (Dolphin:
  right-click the game, *Convert File...*, ISO).
- Optional, for a randomized game: a seed made with the [Wind Waker Randomizer](https://github.com/LagoLunatic/wwrando)
  from that disc (its output, `WW Random <seed>.iso`). The port follows wwrando at commit `9775811`; see
  [RANDO.md](RANDO.md) for what a seed does and does not do yet.

## Install (Desktop Mode)

1. Download the `waker2-steamdeck-<commit>` artifact from the workflow run (GitHub, Actions tab, the
   *steamdeck* run, *Artifacts*), unzip it, and extract the `.tar.gz` inside into your home folder, so
   you have `~/waker2/switchwaker` and `~/waker2/waker2.sh`. Use Dolphin (the file manager)'s *Extract*,
   or `tar -xzf waker2-steamdeck-*.tar.gz -C ~` in Konsole.
2. Copy your clean disc into that folder as `~/waker2/GZLE01.iso`.
3. For a seed, copy the randomizer's `.iso` into the same folder. `waker2.sh` uses the first `.iso` that
   is not `GZLE01.iso` as `COS_RANDO_DISC`; keep only one seed there. Take it out to play the plain game.
4. Try it once from Konsole: `~/waker2/waker2.sh`. If it closes at once, the output (also saved
   as `~/waker2/waker2.log`, the last run) says why.

## Add it to Steam (for Game Mode)

1. In Steam (Desktop Mode): *Games*, *Add a Non-Steam Game to My Library...*, *Browse...*, pick
   `~/waker2/waker2.sh` (set the file type filter to *All files*), *Add Selected Programs*.
2. Optional: in its *Properties*, rename it (e.g. "Wind Waker Rando"). Launch options can override the
   launcher, e.g. `COS_RANDO_DISC="/home/deck/waker2/my seed.iso" %command%`, or
   `COS_BACKEND=opengl %command%` if Vulkan misbehaves.
3. Back in Game Mode it is in your library under *Non-Steam*. The Deck's controls arrive as an Xbox-style
   controller (use the *Gamepad* controller layout). **F1** (or L+R+Z) opens the options menu.

## What the launcher sets

| Variable | Default | |
|---|---|---|
| `COS_DISC` | `~/waker2/GZLE01.iso` | the clean disc |
| `COS_RANDO_DISC` | the other `.iso` in the folder, if any | the seed; set it empty (`COS_RANDO_DISC= %command%`) to ignore one |
| `COS_FULLSCREEN` | `1` | |
| `COS_TRACKER_SERVER` | `1` | serves the tracker and its state on port 6544 |
| `COS_TRACKER_WEB` | `~/waker2/tracker` | the tracker's files the game serves |
| `COS_TRACKER_OPEN` | `0` | `1`: open the tracker in the browser once the game runs |
| `COS_TRACKER_BIND` | `127.0.0.1` | `0.0.0.0`: the tracker can be opened from a phone or PC on the same network |

Saves, settings and caches go in `~/waker2/user/`.

## The tracker

While the game runs it serves [wooferzfg's TWW Randomizer Tracker](https://www.wooferzfg.me/tww-rando-tracker/)
with autotracking ([TRACKER.md](TRACKER.md)): open **http://127.0.0.1:6544/** in a browser, paste the seed's
permalink and launch as usual. Items and checked locations follow the game by themselves, about once a
second; marks made by hand stay. The tracker downloads the seed's logic from GitHub, so the Deck needs the
network the first time for each wwrando version.

- **Desktop Mode:** `~/waker2/waker2-tracker.sh` opens it in the default browser (or start the game
  with `COS_TRACKER_OPEN=1`). Put the two windows side by side, or switch with Alt+Tab.
- **Game Mode** shows one window at a time. Start the game with `COS_TRACKER_OPEN=1 %command%` (launch
  options) so the browser opens as a window of the game, then switch with the **STEAM** button: the
  game's entry lists its windows. Not yet tried on a Deck.
- **Another screen:** `COS_TRACKER_BIND=0.0.0.0 %command%`, then open `http://<the Deck's address>:6544/`
  on a phone or PC on the same network (the Deck's address: *Settings*, *Internet*, the network's
  details).
