#!/usr/bin/env python3
"""Randomizer+: generate a new Great Sea and check that it can be beaten.

This is the logic test (docs/RANDOMIZER_PLUS.md). From a seed it:
  1. raises land masses of every size on the 7x7 sea, from one-cell rocks to continents spanning several squares,
     and gives each a biome (grass, forest, rock, volcanic, snow, sand) and the obstacles of its terrain (cliffs
     around the shore, a boulder on the path up, a ledge to the peak);
  2. places every anchor of anchors.py on a site that suits it: houses and NPCs in the lowlands and on the shore,
     caves and fairy fountains up in the hills, dungeon entrances on land of their biome, ships and platforms on
     open water, NPCs spread over many islands;
  3. rewrites wwrando's logic for that world: each anchor's checks also need what it takes to reach its site;
  4. places the progression items with an assumed fill and checks the seed from the start: every progression
     item collectable and Ganondorf beatable.

  python3 scripts/rplus/worldgen.py --seed 1 --out build/rplus        # one world: world.json, spoiler.txt, map.png
  python3 scripts/rplus/worldgen.py --test 200                        # many seeds, report how many pass

WWRANDO_DIR points at a wwrando checkout (the logic text files are read, nothing of wwrando runs).
"""

import argparse
import json
import math
import os
import random
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anchors as A  # noqa: E402
import wwlogic  # noqa: E402

SQUARES = 7            # the sea chart is 7x7 squares; each is one room of the stage "sea"
CELLS = 12             # cells per square side
N = SQUARES * CELLS
SQUARE_UNITS = 100000.0  # a sea room is 100000 units wide

# Sea rooms kept as they are (room number -> anchor). Rooms count from 1, row by row from the top left.
PREFAB_ROOMS = {1: "prefab_ff", 26: "prefab_totg"}

BIOMES = ["grass", "forest", "rock", "volcanic", "snow", "sand"]

# Obstacles the terrain can put between the shore and a site, with what gets past them and why.
LANDING_GATES = [
  ("Hookshot", "sheer cliffs all round; hookshot to a tree on top"),
  ("Grappling Hook", "a reef ring; swing across on a hanging branch"),
  ("Can Fly With Deku Leaf Outdoors", "cliffs all round; glide in from a sea stack"),
]
UPPER_GATES = [
  ("Can Move Boulders", "a boulder blocks the path up"),
  ("Power Bracelets", "a heavy stone blocks the path up"),
  ("Bombs", "a cracked wall closes the path up"),
  ("Grappling Hook", "a gap on the path, crossed on a hanging branch"),
  ("Hookshot", "a cliff step, hookshot to the tree above"),
  ("Can Fly With Deku Leaf Outdoors", "a gorge to glide over"),
]
PEAK_GATES = [
  ("Hookshot | Can Fly With Deku Leaf Outdoors", "a sheer summit"),
  ("Grappling Hook", "a summit reached by swinging over a crevasse"),
  ("Can Fly With Deku Leaf Outdoors", "a summit reached by gliding from a ridge"),
]


# Buildings and townsfolk that belong in a village (the rest of the NPCs wander the islands on their own).
TOWN = {"jail", "chu_jelly_shop", "school", "lenzo_house", "house_of_wealth", "cafe_bar", "auction", "battlesquid",
        "mesa_house", "orca_house", "pirate_ship", "sam", "town_gate", "mila", "killer_bees", "pompie_vera", "kamo",
        "minenco", "linda_anton", "maggie", "zunari"}

_SYL = ["ka", "lo", "mi", "ra", "su", "to", "ne", "wa", "be", "ri", "do", "fa", "go", "le", "pu", "zi"]


def village_name(rng):
  return ("".join(rng.choice(_SYL) for _ in range(rng.randint(2, 3)))).capitalize() + " " + \
    rng.choice(["Town", "Village", "Harbor", "Cove", "Hamlet", "Landing"])


def room_of(cx, cy):
  return (cy // CELLS) * SQUARES + (cx // CELLS) + 1


def chart_name(room):
  # The sea chart names columns A-G and rows 1-7.
  r = room - 1
  return "%s%d" % ("ABCDEFG"[r % SQUARES], r // SQUARES + 1)


def cell_units(cx, cy):
  """Cell centre in sea coordinates, the chart's centre at 0,0 (+x east, +z south)."""
  half = N / 2.0
  scale = SQUARE_UNITS / CELLS
  return ((cx + 0.5 - half) * scale, (cy + 0.5 - half) * scale)


# ---------------------------------------------------------------------------------------------------------------
# Terrain


class Land:
  def __init__(self, lid, cells, height):
    self.id = lid
    self.cells = cells
    self.area = len(cells)
    self.maxh = max(height[c] for c in cells)
    self.biome = "grass"
    self.landing = ("Nothing", "")
    self.upper = ("Nothing", "")
    self.peak = ("Nothing", "")
    self.anchors = []

  def size_name(self):
    a = self.area
    return ("rock" if a <= 2 else "islet" if a <= 10 else "island" if a <= 45 else "large island" if a <= 150
            else "continent")


class World:
  def __init__(self, seed):
    self.seed = seed
    self.rng = random.Random(seed)
    self.height = {}
    self.lands = []
    self.land_of = {}
    self.level = {}
    self.place = {}      # anchor id -> dict(cell, land, gate, why)
    self.sea_place = {}  # location or anchor id -> cell, for sea anchors and salvage
    self.used = set()

  # -- land

  def prefab_cell(self, cx, cy, margin=1):
    for room in PREFAB_ROOMS:
      r = room - 1
      x0, y0 = (r % SQUARES) * CELLS, (r // SQUARES) * CELLS
      if x0 - margin <= cx < x0 + CELLS + margin and y0 - margin <= cy < y0 + CELLS + margin:
        return True
    return False

  def raise_land(self):
    rng = self.rng
    classes = [  # name, radius range in cells, weight, peak height range
      ("tiny", (0.7, 1.4), 34, (0.2, 0.5)),
      ("small", (1.5, 2.8), 32, (0.3, 0.8)),
      ("medium", (2.8, 4.5), 20, (0.5, 1.0)),
      ("large", (4.5, 7.0), 6, (0.7, 1.0)),
      ("huge", (7.0, 10.0), 1, (0.8, 1.0)),
    ]
    blobs = []
    count = rng.randint(45, 60)
    picks = [rng.choices(classes, [c[2] for c in classes])[0] for _ in range(count)]
    picks += [classes[3], classes[4] if rng.random() < 0.6 else classes[3]]  # always some big land
    for name, (r0, r1), _, (h0, h1) in picks:
      r = rng.uniform(r0, r1)
      for _ in range(50):
        cx, cy = rng.uniform(r, N - r), rng.uniform(r, N - r)
        if not self.prefab_cell(int(cx), int(cy), margin=int(r) + 2):
          break
      else:
        continue
      lobes = [(cx, cy, r)]
      if name in ("large", "huge"):  # continents get a few lobes, so they are not round
        for _ in range(rng.randint(2, 4)):
          ang = rng.uniform(0, 2 * math.pi)
          d = r * rng.uniform(0.5, 0.9)
          lobes.append((cx + math.cos(ang) * d, cy + math.sin(ang) * d, r * rng.uniform(0.4, 0.7)))
      for (lx, ly, lr) in lobes:
        harm = [(k, rng.uniform(0, 0.25 / k * 2), rng.uniform(0, 2 * math.pi)) for k in range(2, 6)]
        blobs.append((lx, ly, lr, rng.uniform(h0, h1), harm))
    h = {}
    for cy in range(N):
      for cx in range(N):
        if self.prefab_cell(cx, cy):
          continue
        best = 0.0
        for (bx, by, br, bh, harm) in blobs:
          dx, dy = cx + 0.5 - bx, cy + 0.5 - by
          d = math.hypot(dx, dy)
          if d > br * 1.6:
            continue
          th = math.atan2(dy, dx)
          reff = br * (1 + sum(a * math.sin(k * th + p) for k, a, p in harm))
          if d < reff:
            best = max(best, bh * (1 - d / reff) ** 0.7)
        if best > 0.02:
          h[(cx, cy)] = best
    self.height = h

  def find_lands(self):
    seen = set()
    for c in sorted(self.height):
      if c in seen:
        continue
      stack, cells = [c], []
      seen.add(c)
      while stack:
        x, y = stack.pop()
        cells.append((x, y))
        for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
          if n in self.height and n not in seen:
            seen.add(n)
            stack.append(n)
      land = Land(len(self.lands), cells, self.height)
      self.lands.append(land)
      for cell in cells:
        self.land_of[cell] = land

  def is_shore(self, c):
    x, y = c
    return any(n not in self.height for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))

  def shape_lands(self):
    rng = self.rng
    lands = sorted(self.lands, key=lambda l: -l.area)
    big = [l for l in lands if 9 <= l.area <= 100] or [l for l in lands if l.area >= 9]
    # Biomes: every biome a dungeon or cave asks for exists on some sizeable land.
    need = ["volcanic", "forest", "rock", "snow", "sand"]
    rng.shuffle(big)
    for l, b in zip(big, need):
      l.biome = b
    for l in lands:
      if l in big[:len(need)]:
        continue
      if l.area <= 2:
        l.biome = rng.choice(["rock", "sand", "grass"])
      else:
        l.biome = rng.choices(BIOMES, [4, 3, 3, 1, 1, 2])[0]
    for l in lands:
      if l.area >= 9 and rng.random() < 0.15:
        l.landing = rng.choice(LANDING_GATES)
      if l.area >= 6 and rng.random() < 0.4:
        l.upper = rng.choice(UPPER_GATES)
      if l.area >= 12 and rng.random() < 0.6:
        l.peak = rng.choice(PEAK_GATES)
      for c in l.cells:
        rel = self.height[c] / l.maxh
        if l.area <= 4 or self.is_shore(c):
          lv = "shore"
        elif rel < 0.45:
          lv = "low"
        elif rel < 0.85 or l.area < 12:
          lv = "up"
        else:
          lv = "peak"
        self.level[c] = lv

  def gate(self, c):
    """Requirement to stand on cell c, and why."""
    l = self.land_of[c]
    parts, why = [], []
    lv = self.level[c]
    for g, active in ((l.landing, True), (l.upper, lv in ("up", "peak")), (l.peak, lv == "peak")):
      if active and g[0] != "Nothing":
        parts.append("(%s)" % g[0])
        why.append(g[1])
    return (" & ".join(parts) or "Nothing"), "; ".join(why)

  # -- placing anchors

  def free_cells(self, levels, lands=None, min_area=1, biome=None, open_only=False):
    out = []
    for l in (lands or self.lands):
      if l.area < min_area or (biome and l.biome != biome):
        continue
      if open_only and l.landing[0] != "Nothing":
        continue
      for c in l.cells:
        if c not in self.used and self.level[c] in levels:
          out.append(c)
    return out

  def put(self, a, cells, spread=1.0):
    if not cells:
      return False
    # Spread out: prefer land with fewer anchors already, a little randomness on top.
    def score(c):
      l = self.land_of[c]
      return len(l.anchors) * spread / (1.0 + math.sqrt(l.area) / 6) + self.rng.random()
    c = min(cells, key=score)
    self.used.add(c)
    l = self.land_of[c]
    l.anchors.append(a.id)
    g, why = self.gate(c)
    self.place[a.id] = {"cell": c, "land": l.id, "gate": g, "why": why}
    return True

  def place_anchors(self):
    by_id = {a.id: a for a in A.ANCHORS}
    todo = [a for a in A.ANCHORS if a.kind != "sea"]
    lowland = ("low", "shore")
    upland = ("up", "peak")

    # Link's house: the start, on open land of some size.
    start = by_id["link_house"]
    cells = [c for c in self.free_cells(lowland, min_area=9, open_only=True)]
    if not self.put(start, cells):
      return False
    self.start_land = self.land_of[self.place["link_house"]["cell"]]

    # Forest Haven and the Forbidden Woods door on the same forest land (the way in is a glide from Forest Haven).
    fh = by_id["forest_haven"]
    if not self.put(fh, self.free_cells(upland + lowland, biome="forest", min_area=9)):
      return False
    fh_land = [self.lands[self.place["forest_haven"]["land"]]]
    for aid in ("dungeon_fw", "fh_small_island"):
      if not self.put(by_id[aid], self.free_cells(upland + lowland, lands=fh_land)):
        return False

    if not self.found_villages():
      return False

    order = {"dungeon": 0, "cave": 1, "fountain": 2, "house": 3, "landmark": 4, "npc": 5, "puzzle": 6}
    rest = [a for a in todo if a.id not in self.place]
    rest.sort(key=lambda a: order[a.kind])
    for a in rest:
      if a.id in ("postbox", "withered_trees", "transparent_chest"):
        continue
      if a.id in TOWN or a.id == "lighthouse":
        if self.put_in_village(a):
          continue
      if a.kind == "dungeon":
        cells = self.free_cells(upland, biome=a.biome, min_area=9) or self.free_cells(upland, min_area=9)
        cells = [c for c in cells if not any(by_id[x].kind == "dungeon" for x in self.land_of[c].anchors)]
      elif a.kind in ("cave", "fountain"):
        cells = (self.free_cells(upland, biome=a.biome or None) if a.biome else []) or self.free_cells(upland) \
          or self.free_cells(lowland)
      elif a.kind == "house":
        cells = (self.free_cells(lowland, biome=a.biome, min_area=5) if a.biome else []) \
          or self.free_cells(("low",), min_area=5) or self.free_cells(lowland, min_area=3)
      elif a.kind == "npc":
        cells = self.free_cells(lowland, min_area=3)
      elif a.id == "lighthouse" or a.id == "tingle_tower" or a.id == "flight_platform" or a.id == "barrel_shooting":
        cells = self.free_cells(("shore",), min_area=3)
      elif a.kind == "landmark":
        cells = self.free_cells(lowland + upland, min_area=3)
      else:  # puzzle
        cells = self.free_cells(upland) or self.free_cells(lowland)
      if not self.put(a, cells, spread=1.5 if a.kind == "npc" else 1.0):
        return False

    # The transparent chest is lit by the lighthouse: same land.
    lh = [self.lands[self.place["lighthouse"]["land"]]]
    if not self.put(by_id["transparent_chest"], self.free_cells(lowland + upland, lands=lh)):
      return False

    # Postboxes: one in every village and one by Link's house; mail needs any one of them.
    self.postboxes = []
    for cells in [v["cells"] for v in self.villages] + [self.start_land.cells]:
      cells = [c for c in cells if c not in self.used]
      if cells:
        c = self.rng.choice(cells)
        self.used.add(c)
        self.postboxes.append(c)
    # Withered trees: four, on four different lands. The quest needs all of them.
    self.trees = []
    lands = [l for l in self.lands if l.area >= 3]
    self.rng.shuffle(lands)
    for l in lands:
      cells = [c for c in l.cells if c not in self.used]
      if cells:
        c = self.rng.choice(cells)
        self.used.add(c)
        self.trees.append(c)
      if len(self.trees) == 4:
        break
    if not self.postboxes or len(self.trees) < 4:
      return False
    for aid, cells in (("postbox", self.postboxes), ("withered_trees", self.trees)):
      gates = [self.gate(c) for c in cells]
      join = " | " if aid == "postbox" else " & "
      self.place[aid] = {"cells": cells, "lands": [self.land_of[c].id for c in cells],
                         "gate": join.join("(%s)" % g for g, _ in gates), "why": ""}
    return True

  def found_villages(self):
    """Villages: a different number in different places every seed, each a few buildings and townsfolk."""
    rng = self.rng
    cands = [c for l in self.lands if l.area >= 9 for c in l.cells
             if c not in self.used and self.level[c] in ("low", "shore")]
    rng.shuffle(cands)
    want = rng.randint(3, 6)
    self.villages = []
    for c in cands:
      if len(self.villages) == want:
        break
      if any(math.dist(c, v["center"]) < 10 for v in self.villages):
        continue
      land = self.land_of[c]
      cells = [d for d in land.cells if d not in self.used and self.level[d] in ("low", "shore")
               and math.dist(c, d) <= 3.5]
      if len(cells) < 5:
        continue
      self.villages.append({"id": len(self.villages), "name": village_name(rng), "center": c, "land": land.id,
                            "cells": cells, "houses": rng.randint(2, 4), "members": []})
    return len(self.villages) >= 2

  def put_in_village(self, a):
    is_house = a.kind == "house"
    vs = [v for v in self.villages if any(c not in self.used for c in v["cells"])
          and (not is_house or sum(1 for m in v["members"] if m[1] == "house") < v["houses"])]
    if a.id == "mila":  # the thief walks between houses
      vs = [v for v in vs if sum(1 for m in v["members"] if m[1] == "house") >= 2] or vs
    if not vs:
      return False
    v = min(vs, key=lambda v: len(v["members"]) + self.rng.random())
    cells = [c for c in v["cells"] if c not in self.used]
    if a.id in ("lighthouse", "pirate_ship"):
      cells = [c for c in cells if self.level[c] == "shore"] or cells
    self.put(a, [self.rng.choice(cells)])
    v["members"].append((a.id, a.kind))
    self.place[a.id]["village"] = v["id"]
    return True

  def water_cells(self, room=None):
    out = []
    for cy in range(N):
      for cx in range(N):
        c = (cx, cy)
        if c in self.height or c in self.used or self.prefab_cell(cx, cy, margin=0):
          continue
        if room is not None and room_of(cx, cy) != room:
          continue
        out.append(c)
    return out

  def place_sea(self, logic, assignment):
    water = self.water_cells()
    self.rng.shuffle(water)
    sea_ids = [a.id for a in A.ANCHORS if a.kind == "sea"]
    sea_ids += sorted({v for v in assignment.values() if v and v.startswith("sea:")})
    for sid in sea_ids:
      c = water.pop()
      self.used.add(c)
      self.sea_place[sid] = c
    # Sunken treasure stays in its chart square (the charts point at squares).
    for loc, v in assignment.items():
      if v and v.startswith("salvage:"):
        room = None
        for p in logic.locations[loc]["paths"]:
          if p.startswith("sea/Room"):
            room = int(p.split("/")[1][4:].split(".")[0])
            break
        cells = self.water_cells(room) if room and room not in PREFAB_ROOMS else []
        if room in PREFAB_ROOMS:
          r = room - 1
          cells = [((r % SQUARES) * CELLS + CELLS // 2, (r // SQUARES) * CELLS + CELLS // 2)]
        c = self.rng.choice(cells) if cells else water.pop()
        self.used.add(c)
        self.sea_place["salvage:" + loc] = c

  def generate(self):
    self.raise_land()
    self.find_lands()
    self.shape_lands()
    return self.place_anchors()


# ---------------------------------------------------------------------------------------------------------------
# Logic for the world


def apply_world_logic(world, logic, assignment):
  for name, text in A.NEUTRAL_MACROS.items():
    logic.set_macro(name, text)
  for a in A.ANCHORS:
    if a.kind == "sea":
      continue  # open water: the boat reaches it
    gate = world.place[a.id]["gate"]
    if gate == "Nothing":
      continue
    if a.door_macro:
      logic.set_macro(a.door_macro, "(%s) & (%s)" % (gate, logic.macros[a.door_macro] or "Nothing"))
    for loc in a.locations:
      if a.door_macro and a.kind != "puzzle":
        continue  # the door's macro already carries the gate
      logic.set_need(loc, "(%s) & (%s)" % (gate, logic.locations[loc]["need"] or "Nothing"))
  # Locations whose need names the door macro only indirectly (Forest Haven's firefly, the Forest Haven chests)
  # follow through the macros, so nothing more is needed here.


# ---------------------------------------------------------------------------------------------------------------
# Item fill


def item_pool():
  pool = list(wwlogic.PROGRESS_ITEMS)
  for it in wwlogic.DEFAULT_START:
    if it in pool:
      pool.remove(it)
  return pool


def zone(loc):
  return loc.split(" - ")[0]


def fill(logic, rng, start):
  progress_locs = [l for l in logic.locations if logic.is_progress_location(l)]
  placement = {}
  keys = [(k, d) for d, ks in wwlogic.DUNGEON_KEYS.items() for k in ks]
  items = item_pool()
  rng.shuffle(keys)
  rng.shuffle(items)
  queue = [(k, d) for k, d in keys] + [(it, None) for it in items]
  remaining = Counter(k for k, _ in queue)
  for item, dungeon in queue:
    remaining[item] -= 1
    assumed = list(start) + list(remaining.elements())
    owned, _ = wwlogic.sweep(logic, assumed, placement)
    ch = logic.checker(owned.elements())
    if dungeon:
      cands = [l for l in logic.locations if zone(l) == dungeon and l not in placement
               and "Dungeon" in logic.locations[l]["types"] and logic.is_progress_location(l) and ch.location(l)]
    else:
      cands = [l for l in progress_locs if l not in placement and ch.location(l)]
    if not cands:
      return None, "no room for %s" % item
    placement[rng.choice(cands)] = item
  return placement, ""


def check(logic, start, placement):
  owned, reached = wwlogic.sweep(logic, start, placement)
  ch = logic.checker(owned.elements())
  missing = [l for l, it in placement.items() if l not in reached]
  return ch.atom("Can Reach and Defeat Ganondorf") and not missing, missing


def playthrough(logic, start, placement):
  spheres = []
  items = Counter(start)
  got = set()
  while True:
    ch = logic.checker(items.elements())
    new = [l for l in placement if l not in got and ch.location(l)]
    if not new:
      break
    spheres.append(sorted(new))
    for l in new:
      got.add(l)
      items[placement[l]] += 1
  return spheres


# ---------------------------------------------------------------------------------------------------------------


def build(seed, wwrando_dir, tries=20):
  """Returns (world, logic, assignment, placement) or raises."""
  rng = random.Random(seed)
  for attempt in range(tries):
    wseed = rng.randrange(1 << 30)
    world = World(wseed)
    if not world.generate():
      continue
    logic = wwlogic.WwLogic(wwrando_dir)
    assignment = A.assign(logic.locations)
    world.place_sea(logic, assignment)
    apply_world_logic(world, logic, assignment)
    for _ in range(3):
      placement, why = fill(logic, random.Random(rng.randrange(1 << 30)), wwlogic.DEFAULT_START)
      if placement is None:
        continue
      ok, _ = check(logic, wwlogic.DEFAULT_START, placement)
      if ok:
        world.attempts = attempt + 1
        world.request_seed = seed
        return world, logic, assignment, placement
  raise RuntimeError("no beatable world in %d tries" % tries)


def describe(world, logic, assignment, placement):
  by_id = {a.id: a for a in A.ANCHORS + A.PREFABS}
  out = {"seed": world.request_seed, "world_seed": world.seed, "grid": {"squares": SQUARES, "cells_per_square": CELLS,
                                      "square_units": SQUARE_UNITS},
         "prefab_rooms": {str(r): by_id[a].name for r, a in PREFAB_ROOMS.items()},
         "lands": [], "anchors": [], "sea": [], "items": placement,
         "villages": [{"id": v["id"], "name": v["name"], "center": list(v["center"]),
                       "square": chart_name(room_of(*v["center"])), "members": [m[0] for m in v["members"]]}
                      for v in world.villages]}
  for l in world.lands:
    out["lands"].append({"id": l.id, "size": l.size_name(), "cells": len(l.cells), "biome": l.biome,
                         "landing": l.landing[0], "path_up": l.upper[0], "peak": l.peak[0],
                         "squares": sorted({chart_name(room_of(*c)) for c in l.cells}),
                         "anchors": l.anchors})
  for aid, p in world.place.items():
    a = by_id[aid]
    cells = p.get("cells") or [p["cell"]]
    out["anchors"].append({"id": aid, "kind": a.kind, "name": a.name, "stage": a.stage, "actors": list(a.actors),
                           "at": [{"cell": list(c), "square": chart_name(room_of(*c)), "room": room_of(*c),
                                   "level": world.level[c], "pos": cell_units(*c)} for c in cells],
                           "gate": p["gate"], "why": p["why"], "checks": a.locations,
                           "village": p.get("village")})
  for sid, c in world.sea_place.items():
    out["sea"].append({"id": sid, "cell": list(c), "square": chart_name(room_of(*c)), "pos": cell_units(*c)})
  return out


def write_spoiler(path, world, logic, assignment, placement):
  by_id = {a.id: a for a in A.ANCHORS}
  lines = ["Randomizer+ seed %d (world %d)" % (world.request_seed, world.seed), ""]
  lines.append("Villages (%d):" % len(world.villages))
  for v in world.villages:
    lines.append("  %s (%s): %s" % (v["name"], chart_name(room_of(*v["center"])),
                                    ", ".join(by_id[m].name for m, _ in v["members"]) or "empty"))
  lines.append("")
  lines.append("Lands (%d):" % len(world.lands))
  for l in sorted(world.lands, key=lambda l: -l.area):
    if not l.anchors:
      continue
    sq = sorted({chart_name(room_of(*c)) for c in l.cells})
    lines.append("  #%d %s %s in %s" % (l.id, l.biome, l.size_name(), ",".join(sq)))
    for g, label in ((l.landing, "landing"), (l.upper, "path up"), (l.peak, "peak")):
      if g[0] != "Nothing":
        lines.append("      %s: %s (%s)" % (label, g[1], g[0]))
    for aid in l.anchors:
      p = world.place[aid]
      lines.append("      - %-45s %-5s %s" % (by_id[aid].name, world.level[p["cell"]],
                                                "" if p["gate"] == "Nothing" else "needs " + p["gate"]))
  lines.append("")
  lines.append("Playthrough (progression items by sphere):")
  for i, sphere in enumerate(playthrough(logic, wwlogic.DEFAULT_START, placement), 1):
    lines.append("  Sphere %d:" % i)
    for l in sphere:
      where = assignment.get(l) or ""
      lines.append("    %-70s %s" % (l, placement[l]))
  open(path, "w").write("\n".join(lines) + "\n")


def draw_map(path, world, scale=10):
  from PIL import Image, ImageDraw
  by_id = {a.id: a for a in A.ANCHORS}
  colors = {"grass": (120, 180, 90), "forest": (60, 130, 70), "rock": (150, 140, 125), "volcanic": (130, 80, 70),
            "snow": (225, 235, 240), "sand": (225, 205, 140)}
  kind_color = {"house": (255, 255, 255), "npc": (255, 220, 0), "cave": (40, 40, 40), "fountain": (255, 120, 220),
                "dungeon": (220, 0, 0), "landmark": (0, 200, 255), "puzzle": (255, 150, 0)}
  W = N * scale
  img = Image.new("RGB", (W + 360, W), (30, 90, 160))
  d = ImageDraw.Draw(img)
  for (cx, cy), h in world.height.items():
    base = colors[world.land_of[(cx, cy)].biome]
    k = 0.75 + 0.35 * h
    d.rectangle([cx * scale, cy * scale, cx * scale + scale - 1, cy * scale + scale - 1],
                fill=tuple(min(255, int(v * k)) for v in base))
  for room, aid in PREFAB_ROOMS.items():
    r = room - 1
    x0, y0 = (r % SQUARES) * CELLS * scale, (r // SQUARES) * CELLS * scale
    d.rectangle([x0, y0, x0 + CELLS * scale - 1, y0 + CELLS * scale - 1], outline=(255, 80, 80), width=3)
    d.text((x0 + 6, y0 + 6), "FF" if aid == "prefab_ff" else "Tower of the Gods", fill=(255, 200, 200))
  for i in range(SQUARES + 1):
    p = i * CELLS * scale
    d.line([p, 0, p, W], fill=(70, 130, 190))
    d.line([0, p, W, p], fill=(70, 130, 190))
  for v in world.villages:
    x, y = v["center"][0] * scale + scale // 2, v["center"][1] * scale + scale // 2
    r = int(3.8 * scale)
    d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 240, 160), width=2)
    d.text((x - r, y - r - 12), v["name"], fill=(255, 240, 160))
  for sid, (cx, cy) in world.sea_place.items():
    x, y = cx * scale + scale // 2, cy * scale + scale // 2
    col = (150, 200, 255) if sid.startswith("salvage:") else (255, 255, 255)
    d.ellipse([x - 2, y - 2, x + 2, y + 2], outline=col)
  legend = []
  n = 0
  for aid, p in world.place.items():
    a = by_id[aid]
    for c in p.get("cells") or [p["cell"]]:
      x, y = c[0] * scale + scale // 2, c[1] * scale + scale // 2
      col = kind_color.get(a.kind, (255, 255, 255))
      if aid == "link_house":
        d.rectangle([x - 5, y - 5, x + 5, y + 5], outline=(255, 255, 0), width=2)
      d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=col, outline=(0, 0, 0))
    if aid in ("postbox", "withered_trees"):
      continue
    n += 1
    x, y = p["cell"][0] * scale + scale // 2, p["cell"][1] * scale + scale // 2
    d.text((x + 5, y - 5), str(n), fill=(0, 0, 0))
    legend.append((n, a, p))
  y = 6
  d.text((W + 8, y), "Randomizer+ seed %d" % world.request_seed, fill=(255, 255, 255))
  y += 16
  for kind, col in kind_color.items():
    d.ellipse([W + 8, y + 2, W + 16, y + 10], fill=col, outline=(0, 0, 0))
    d.text((W + 22, y), kind, fill=(255, 255, 255))
    y += 13
  y += 6
  rows = (len(legend) + 1) // 2
  for i, (n, a, p) in enumerate(legend):
    gate = "" if p["gate"] == "Nothing" else " *"
    col, row = divmod(i, rows)
    d.text((W + 8 + col * 176, y + row * 11), "%2d %s%s" % (n, a.name[:25], gate), fill=(255, 255, 255))
  d.text((W + 8, W - 30), "* needs an item to reach", fill=(255, 255, 255))
  d.text((W + 8, W - 16), "circles on water: ships, platforms, salvage", fill=(255, 255, 255))
  img.save(path)


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("--seed", type=int, default=1)
  ap.add_argument("--out", default="build/rplus")
  ap.add_argument("--test", type=int, default=0, help="generate this many seeds and report")
  args = ap.parse_args()
  ww = wwlogic.find_wwrando()

  if args.test:
    ok, tries, sizes = 0, [], Counter()
    for s in range(args.seed, args.seed + args.test):
      try:
        world, logic, assignment, placement = build(s, ww)
      except RuntimeError as e:
        print("seed %d: %s" % (s, e))
        continue
      ok += 1
      tries.append(world.attempts)
      for l in world.lands:
        sizes[l.size_name()] += 1
    print("%d/%d seeds beatable; worlds tried per seed: avg %.2f, max %d" %
          (ok, args.test, sum(tries) / max(1, len(tries)), max(tries or [0])))
    print("land sizes over all seeds:", dict(sizes))
    return 0 if ok == args.test else 1

  world, logic, assignment, placement = build(args.seed, ww)
  os.makedirs(args.out, exist_ok=True)
  json.dump(describe(world, logic, assignment, placement), open(os.path.join(args.out, "world.json"), "w"),
            indent=1)
  write_spoiler(os.path.join(args.out, "spoiler.txt"), world, logic, assignment, placement)
  draw_map(os.path.join(args.out, "map.png"), world)
  print("seed %d: %d lands, %d anchors placed, beatable; wrote %s" %
        (args.seed, len(world.lands), len(world.place), args.out))
  return 0


if __name__ == "__main__":
  sys.exit(main())
