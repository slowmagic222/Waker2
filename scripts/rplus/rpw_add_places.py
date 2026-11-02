#!/usr/bin/env python3
"""Adds the tracker's lines (name, check, village, member) to a world.rpw made before worldgen.py wrote them.

  python3 scripts/rplus/rpw_add_places.py <world.json> <world.rpw> [<more .rpw of the same world> ...]

The world.json is the one worldgen.py wrote next to that world.rpw. Old tracker lines are replaced, nothing else
changes (the same world, the same saves).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from worldgen import place_lines  # noqa: E402


def main():
  if len(sys.argv) < 3:
    print(__doc__)
    return 2
  world = json.load(open(sys.argv[1]))
  lines = place_lines(world["anchors"], world["villages"])
  for path in sys.argv[2:]:
    keep = [l for l in open(path).read().splitlines()
            if l.split(" ", 1)[0] not in ("name", "check", "village", "member")]
    seed = next((l for l in keep if l.startswith("seed ")), "")
    if seed.split()[1:3] != [str(world["seed"]), str(world["world_seed"])]:
      print("%s: %s is not the world of %s (seed %s %s): skipped" %
            (path, seed, sys.argv[1], world["seed"], world["world_seed"]))
      continue
    open(path, "w").write("\n".join(keep + lines) + "\n")
    print("%s: %d tracker lines" % (path, len(lines)))
  return 0


if __name__ == "__main__":
  sys.exit(main())
