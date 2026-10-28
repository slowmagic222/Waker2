"""Randomizer+: wwrando's logic, read as data.

Reads wwrando's logic/item_locations.txt and logic/macros.txt (the same text files wwrando itself reads) and
answers "can this location be reached with these items". Nothing of wwrando's code runs: the world generator
rewrites some requirements for the generated world and checks them here, and the same rules can later move to
C++ for the in-game generator.

Requirement syntax (wwrando's): names joined with & and |, parentheses, "Item xN" for counts, macros, Nothing,
Impossible, `Option "name" Is "value"` / Enabled / Disabled, `Can Access Item Location "name"`, and the trick
levels `Obscure N` / `Precise N`.
"""

import os
import re
from collections import Counter

# wwrando's starting items with its default options (randomizer.py starting_items + DEFAULT_STARTING_ITEMS, sword
# mode "Start with Hero's Sword").
DEFAULT_START = [
  "Wind Waker", "Wind's Requiem", "Boat's Sail",
  "Progressive Shield", "Progressive Magic Meter", "Ballad of Gales", "Song of Passing",
  "Progressive Sword",
]

# logic/item_types.py PROGRESS_ITEMS and the dungeon keys.
PROGRESS_ITEMS = [
  "Tingle Tuner", "Wind Waker", "Spoils Bag", "Grappling Hook", "Power Bracelets", "Iron Boots", "Bait Bag",
  "Boomerang", "Hookshot", "Delivery Bag", "Bombs", "Skull Hammer", "Deku Leaf",
] + ["Triforce Shard %d" % i for i in range(1, 9)] + [
  "Nayru's Pearl", "Din's Pearl", "Farore's Pearl",
  "Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric", "Wind God's Aria", "Song of Passing",
  "Boat's Sail", "Note to Mom", "Maggie's Letter", "Moblin's Letter", "Cabana Deed",
  "Dragon Tingle Statue", "Forbidden Tingle Statue", "Goddess Tingle Statue", "Earth Tingle Statue",
  "Wind Tingle Statue",
  "Progressive Bomb Bag", "Progressive Bomb Bag", "Progressive Quiver", "Progressive Quiver",
  "Ghost Ship Chart", "Magic Armor",
] + ["Progressive Sword"] * 4 + ["Progressive Shield"] * 2 + ["Progressive Bow"] * 3 + \
  ["Progressive Wallet"] * 2 + ["Progressive Picto Box"] * 2 + ["Progressive Magic Meter"] * 2 + \
  ["Empty Bottle"] * 4

DUNGEON_KEYS = {
  "Dragon Roost Cavern": ["DRC Big Key"] + ["DRC Small Key"] * 4,
  "Forbidden Woods": ["FW Big Key", "FW Small Key"],
  "Tower of the Gods": ["TotG Big Key"] + ["TotG Small Key"] * 2,
  "Earth Temple": ["ET Big Key"] + ["ET Small Key"] * 3,
  "Wind Temple": ["WT Big Key"] + ["WT Small Key"] * 2,
}

# wwrando's defaults for the options its logic reads.
DEFAULT_OPTIONS = {
  "logic_obscurity": "None",
  "logic_precision": "None",
  "sword_mode": "Start with Hero's Sword",
  "skip_rematch_bosses": False,
  "required_bosses": False,
  "progression": {"Dungeon", "Puzzle Secret Cave", "Great Fairy", "Free Gift", "Expensive Purchase", "Other Chest",
                  "Misc"},
}
TRICK_LEVEL = {"None": 0, "Normal": 1, "Hard": 2, "Very Hard": 3}

# Location types and the progression option that allows each (logic.py filter_locations_for_progression_static).
NEVER_PROGRESS = {"No progression", "Consumables only"}


def find_wwrando():
  for p in (os.environ.get("WWRANDO_DIR"), "seedgen/wwrando", os.path.expanduser("~/wwrando")):
    if p and os.path.isfile(os.path.join(p, "logic", "macros.txt")):
      return p
  raise SystemExit("wwrando not found: set WWRANDO_DIR to a wwrando checkout (pinned 9775811)")


def _strip_comment(line):
  i = line.find("#")
  return line if i < 0 else line[:i]


def read_macros(path):
  macros = {}
  name = None
  for raw in open(path, encoding="utf-8"):
    line = _strip_comment(raw).rstrip()
    if not line.strip():
      continue
    if not line.startswith(" "):
      key, _, rest = line.partition(":")
      name = key.strip()
      macros[name] = rest.strip()
    elif name is not None:
      macros[name] = (macros[name] + " " + line.strip()).strip()
  return macros


def read_locations(path):
  locs = {}
  cur = None
  field = None
  for raw in open(path, encoding="utf-8"):
    if raw.lstrip().startswith("#") or not raw.strip():
      continue
    if not raw.startswith(" "):
      cur = {"need": "", "types": set(), "paths": [], "original": ""}
      locs[raw.strip().rstrip(":")] = cur
      field = None
      continue
    line = _strip_comment(raw).strip()
    if not line:
      continue
    if line.startswith("Need:"):
      field = "need"
      cur["need"] = line[5:].strip()
    elif line.startswith("Original item:"):
      field = None
      cur["original"] = line.split(":", 1)[1].strip()
    elif line.startswith("Types:"):
      field = None
      cur["types"] = {t.strip() for t in line.split(":", 1)[1].split(",") if t.strip()}
    elif line.startswith("Paths:"):
      field = "paths"
    elif field == "paths" and line.startswith("- "):
      cur["paths"].append(line[2:])
    elif field == "need":
      cur["need"] = (cur["need"] + " " + line).strip()
  return locs


_TOKEN = re.compile(r'\s*(\(|\)|&|\||"[^"]*"|[^&|()"]+)')


def parse(expr):
  """Requirement text -> tree: ("and", [...]), ("or", [...]) or an atom string."""
  tokens = []
  pos = 0
  expr = expr.strip()
  while pos < len(expr):
    m = _TOKEN.match(expr, pos)
    if not m:
      raise ValueError("bad requirement: %r" % expr)
    tokens.append(m.group(1))
    pos = m.end()
  # Join atom pieces (a name and a quoted string next to each other are one atom).
  merged = []
  for t in tokens:
    t = t.strip()
    if not t:
      continue
    if t in ("&", "|") and (not merged or merged[-1] == "("):
      continue  # wwrando allows "( & a & b )"
    if t in "()&|" or not merged or merged[-1] in ("(", ")", "&", "|"):
      merged.append(t)
    else:
      merged[-1] = merged[-1] + " " + t
  pos = 0

  def expr_or():
    nonlocal pos
    items = [expr_and()]
    while pos < len(merged) and merged[pos] == "|":
      pos += 1
      items.append(expr_and())
    return items[0] if len(items) == 1 else ("or", items)

  def expr_and():
    nonlocal pos
    items = [term()]
    while pos < len(merged) and merged[pos] == "&":
      pos += 1
      items.append(term())
    return items[0] if len(items) == 1 else ("and", items)

  def term():
    nonlocal pos
    t = merged[pos]
    pos += 1
    if t == "(":
      e = expr_or()
      if merged[pos] != ")":
        raise ValueError("unbalanced: %r" % expr)
      pos += 1
      return e
    return t.strip()

  if not merged:
    return "Nothing"
  tree = expr_or()
  if pos != len(merged):
    raise ValueError("trailing tokens in %r" % expr)
  return tree


_COUNT = re.compile(r"^(.*) x(\d+)$")
_OPTION = re.compile(r'^Option "([^"]+)" (Is Not|Is|Enabled|Disabled)(?: "([^"]+)")?$')
_LOCATION = re.compile(r'^Can Access Item Location "([^"]+)"$')
_TRICK = re.compile(r"^(Obscure|Precise) (\d)$")


class WwLogic:
  def __init__(self, wwrando_dir, options=None):
    self.dir = wwrando_dir
    self.macros = read_macros(os.path.join(wwrando_dir, "logic", "macros.txt"))
    self.locations = read_locations(os.path.join(wwrando_dir, "logic", "item_locations.txt"))
    self.options = dict(DEFAULT_OPTIONS, **(options or {}))
    self._macro_tree = {}
    self._need_tree = {}

  # The world generator changes requirements through these two.
  def set_macro(self, name, text):
    self.macros[name] = text
    self._macro_tree.pop(name, None)

  def set_need(self, location, text):
    self.locations[location]["need"] = text
    self._need_tree.pop(location, None)

  def need_tree(self, location):
    t = self._need_tree.get(location)
    if t is None:
      t = self._need_tree[location] = parse(self.locations[location]["need"] or "Nothing")
    return t

  def macro_tree(self, name):
    t = self._macro_tree.get(name)
    if t is None:
      t = self._macro_tree[name] = parse(self.macros[name] or "Nothing")
    return t

  def is_progress_location(self, name):
    types = self.locations[name]["types"]
    if types & NEVER_PROGRESS or "Sunken Treasure" in types:
      return False
    if "Boss" in types:
      return "Dungeon" in self.options["progression"]
    allowed = self.options["progression"]
    return all(t in allowed or t in ("Randomizable Miniboss Room",) for t in types)

  def checker(self, items):
    return Checker(self, Counter(items))


class Checker:
  """Evaluates requirements for one set of owned items (memoised; make a new one when the items change)."""

  def __init__(self, logic, items):
    self.logic = logic
    self.items = items
    self.memo = {}
    self.busy = set()

  def location(self, name):
    return self.eval(self.logic.need_tree(name), name)

  def eval(self, tree, ctx=""):
    if isinstance(tree, tuple):
      op, kids = tree
      if op == "and":
        return all(self.eval(k, ctx) for k in kids)
      return any(self.eval(k, ctx) for k in kids)
    return self.atom(tree)

  def atom(self, a):
    if a in self.memo:
      return self.memo[a]
    if a in self.busy:
      return False  # a cycle: not reachable through itself
    self.busy.add(a)
    try:
      r = self._atom(a)
    finally:
      self.busy.discard(a)
    self.memo[a] = r
    return r

  def _atom(self, a):
    L = self.logic
    if a == "Nothing":
      return True
    if a == "Impossible":
      return False
    m = _LOCATION.match(a)
    if m:
      return self.eval(L.need_tree(m.group(1)))
    m = _OPTION.match(a)
    if m:
      name, op, value = m.groups()
      have = L.options.get(name)
      if op == "Enabled":
        return bool(have)
      if op == "Disabled":
        return not have
      if op == "Is":
        return have == value
      return have != value
    m = _TRICK.match(a)
    if m:
      opt = "logic_obscurity" if m.group(1) == "Obscure" else "logic_precision"
      return TRICK_LEVEL[L.options[opt]] >= int(m.group(2))
    if a in L.macros:
      return self.eval(L.macro_tree(a))
    m = _COUNT.match(a)
    if m:
      return self.items[m.group(1)] >= int(m.group(2))
    return self.items[a] >= 1


def sweep(logic, start_items, placement, locations=None):
  """Collect every item reachable from start_items given placement {location: item}. Returns (items, reached)."""
  items = Counter(start_items)
  reached = set()
  pending = [l for l in (locations or logic.locations) if l in placement]
  changed = True
  while changed:
    changed = False
    ch = Checker(logic, items)
    still = []
    for l in pending:
      if ch.location(l):
        reached.add(l)
        items[placement[l]] += 1
        changed = True
      else:
        still.append(l)
    pending = still
  return items, reached
