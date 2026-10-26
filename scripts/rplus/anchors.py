"""Randomizer+: what a generated world has to contain.

Every item location wwrando knows (logic/item_locations.txt) outside the dungeons belongs to one *anchor*: the
thing in the world the player has to reach to check it. An anchor is a door into an interior stage (a house,
a secret cave, a fairy fountain, a dungeon's entrance stage), an NPC or landmark standing outdoors, a puzzle
chest on the terrain, or an object on the open sea. The world generator places anchors; the interiors and
dungeons themselves stay as they are on the disc.

Kinds and where the generator may put them:
  house     a building whose door leads to an interior stage            lowland (village or shore)
  cave      a secret cave's door                                         upland (cliff face, hill)
  fountain  a Great Fairy fountain's door                                upland or grove
  dungeon   the door to a dungeon's entrance stage                       a landmark site in a matching biome
  npc       an NPC who gives items outdoors                              lowland
  landmark  an outdoor structure with a job (lighthouse, gate, tower)    lowland or upland
  puzzle    a chest made reachable by the terrain                        any land, often a peak or a ledge
  sea       a platform, raft, gunboat, Big Octo, submarine or ship       open water
  prefab    a sea room kept as it is (Forsaken Fortress, Tower of the Gods with Hyrule under it)

`door_macro` names the wwrando macro that says what opens that door (a boulder, an ice block, ...). The world
keeps that obstacle in front of the door and adds what it takes to reach the door's site.

`python3 scripts/rplus/anchors.py --csv` prints the inventory as a table.
"""

from dataclasses import dataclass, field


@dataclass
class Anchor:
  id: str
  kind: str
  name: str
  locations: list = field(default_factory=list)
  stage: str = ""         # interior stage behind the door
  actors: tuple = ()      # decomp actors that hand out the items
  biome: str = ""         # preferred biome: forest, volcanic, rock, snow, grass, sand
  door_macro: str = ""    # wwrando macro for the obstacle at the door (kept), "" = none
  home: str = ""          # where it is in the original game
  note: str = ""


def A(*args, **kwargs):
  return Anchor(*args, **kwargs)


ANCHORS = [
  # ---- Outset Island
  A("link_house", "house", "Link's house (with the cellar)", ["Outset Island - Underneath Link's House"],
    stage="LinkRM", home="Outset Island", note="cellar LinkUG is reached from inside"),
  A("mesa_house", "house", "Mesa's house", ["Outset Island - Mesa the Grasscutter's House"],
    stage="Omasao", home="Outset Island"),
  A("orca_house", "house", "Orca's house", ["Outset Island - Orca - Give 10 Knight's Crests",
    "Outset Island - Orca - Hit 500 Times"], stage="Ojhous", actors=("d_a_npc_ji1",), home="Outset Island"),
  A("forest_of_fairies", "house", "Forest of Fairies (leads to a fairy fountain)", ["Outset Island - Great Fairy"],
    stage="A_mori", actors=("d_a_bigelf",), door_macro="Can Access Forest of Fairies", home="Outset Island",
    note="A_mori is a small outdoor stage; Fairy04 is behind a boulder in it"),
  A("jabun_cave", "cave", "Jabun's cave", ["Outset Island - Jabun's Cave"], stage="Pjavdou",
    home="Outset Island", note="stone door, needs Bombs"),
  A("black_soil", "puzzle", "Black soil patch", ["Outset Island - Dig up Black Soil"], home="Outset Island"),
  A("savage_labyrinth", "cave", "Savage Labyrinth", ["Outset Island - Savage Labyrinth - Floor 30",
    "Outset Island - Savage Labyrinth - Floor 50"], stage="Cave09",
    door_macro="Can Access Secret Cave Entrance on Outset Island", home="Outset Island"),

  # ---- Windfall Island: every building and NPC becomes its own anchor so they can spread out
  A("jail", "house", "Town jail (Tingle)", ["Windfall Island - Jail - Tingle - First Gift",
    "Windfall Island - Jail - Tingle - Second Gift", "Windfall Island - Jail - Maze Chest"],
    stage="Pnezumi", actors=("d_a_npc_tc",), home="Windfall Island"),
  A("chu_jelly_shop", "house", "Chu Jelly Juice Shop", ["Windfall Island - Chu Jelly Juice Shop - Give 15 Green Chu Jelly",
    "Windfall Island - Chu Jelly Juice Shop - Give 15 Blue Chu Jelly"], stage="Pdrgsh", home="Windfall Island"),
  A("school", "house", "Mrs. Marie's school", ["Windfall Island - Mrs. Marie - Catch Killer Bees",
    "Windfall Island - Mrs. Marie - Give 1 Joy Pendant", "Windfall Island - Mrs. Marie - Give 21 Joy Pendants",
    "Windfall Island - Mrs. Marie - Give 40 Joy Pendants"], stage="Nitiyou", actors=("d_a_npc_ho",),
    home="Windfall Island"),
  A("killer_bees", "npc", "Ivan and the Killer Bees", ["Windfall Island - Ivan - Catch Killer Bees"],
    actors=("d_a_npc_mk",), home="Windfall Island", note="the kids hide around town"),
  A("lenzo_house", "house", "Lenzo's photo studio", ["Windfall Island - Lenzo's House - Left Chest",
    "Windfall Island - Lenzo's House - Right Chest", "Windfall Island - Lenzo's House - Become Lenzo's Assistant",
    "Windfall Island - Lenzo's House - Bring Forest Firefly"], stage="Ocmera", actors=("d_a_npc_photo",),
    home="Windfall Island"),
  A("house_of_wealth", "house", "House of Wealth", ["Windfall Island - House of Wealth Chest",
    "Windfall Island - Maggie's Father - Give 20 Skull Necklaces"], stage="Orichh", home="Windfall Island"),
  A("maggie", "npc", "Maggie", ["Windfall Island - Maggie - Free Item", "Windfall Island - Maggie - Delivery Reward"],
    actors=("d_a_npc_kp1",), home="Windfall Island"),
  A("cafe_bar", "house", "Cafe bar (postman)", ["Windfall Island - Cafe Bar - Postman"], stage="Opub",
    home="Windfall Island"),
  A("lighthouse", "landmark", "Lighthouse (Kreeb)", ["Windfall Island - Kreeb - Light Up Lighthouse"],
    actors=("d_a_npc_people",), home="Windfall Island"),
  A("transparent_chest", "puzzle", "Transparent chest (lit by the lighthouse)", ["Windfall Island - Transparent Chest"],
    home="Windfall Island", note="should sit near the lighthouse"),
  A("tott", "npc", "Tott", ["Windfall Island - Tott - Teach Rhythm"], home="Windfall Island"),
  A("pirate_ship", "house", "Pirate ship (docked)", ["Windfall Island - Pirate Ship"], stage="Asoko",
    home="Windfall Island", note="moored at a shore site; the ship is the door"),
  A("auction", "house", "Auction house", ["Windfall Island - 5 Rupee Auction", "Windfall Island - 40 Rupee Auction",
    "Windfall Island - 60 Rupee Auction", "Windfall Island - 80 Rupee Auction"], actors=("d_a_auction",),
    home="Windfall Island", note="interior stage to confirm from the disc"),
  A("zunari", "npc", "Zunari's stall", ["Windfall Island - Zunari - Stock Exotic Flower in Zunari's Shop"],
    actors=("d_a_npc_rsh1",), home="Windfall Island"),
  A("sam", "npc", "Sam (town decorations)", ["Windfall Island - Sam - Decorate the Town"], actors=("d_a_npc_people",),
    home="Windfall Island", note="decorations go on the village Sam stands in"),
  A("town_gate", "landmark", "Town gate (Kane)", ["Windfall Island - Kane - Place Shop Guru Statue on Gate",
    "Windfall Island - Kane - Place Postman Statue on Gate", "Windfall Island - Kane - Place Six Flags on Gate",
    "Windfall Island - Kane - Place Six Idols on Gate"], actors=("d_a_npc_people",), home="Windfall Island"),
  A("mila", "npc", "Mila (follow the thief)", ["Windfall Island - Mila - Follow the Thief"], actors=("d_a_npc_people",),
    home="Windfall Island", note="needs a walking path through a village at night"),
  A("battlesquid", "house", "Battlesquid game", ["Windfall Island - Battlesquid - First Prize",
    "Windfall Island - Battlesquid - Second Prize", "Windfall Island - Battlesquid - Under 20 Shots Prize"],
    stage="Kaisen", actors=("d_a_npc_kg1",), home="Windfall Island"),
  A("pompie_vera", "npc", "Pompie and Vera", ["Windfall Island - Pompie and Vera - Secret Meeting Photo"],
    actors=("d_a_npc_people",), home="Windfall Island"),
  A("kamo", "npc", "Kamo (full moon)", ["Windfall Island - Kamo - Full Moon Photo"], actors=("d_a_npc_people",),
    home="Windfall Island"),
  A("minenco", "npc", "Minenco", ["Windfall Island - Minenco - Miss Windfall Photo"], actors=("d_a_npc_people",),
    home="Windfall Island"),
  A("linda_anton", "npc", "Linda and Anton", ["Windfall Island - Linda and Anton"], actors=("d_a_npc_people",),
    home="Windfall Island"),

  # ---- Dragon Roost Island
  A("wind_shrine", "landmark", "Wind shrine", ["Dragon Roost Island - Wind Shrine"], home="Dragon Roost Island"),
  A("rito_aerie", "house", "Rito Aerie", ["Dragon Roost Island - Rito Aerie - Give Hoskit 20 Golden Feathers",
    "Dragon Roost Island - Rito Aerie - Mail Sorting"], stage="Atorizk", biome="rock", home="Dragon Roost Island"),
  A("boulder_chest", "puzzle", "Chest on top of a boulder", ["Dragon Roost Island - Chest on Top of Boulder"],
    home="Dragon Roost Island"),
  A("sky_platforms", "puzzle", "Floating platforms ring", ["Dragon Roost Island - Fly Across Platforms Around Island"],
    home="Dragon Roost Island"),
  A("drc_cave", "cave", "Dragon Roost secret cave", ["Dragon Roost Island - Secret Cave"], stage="TF_06",
    door_macro="Can Access Secret Cave Entrance on Dragon Roost Island", home="Dragon Roost Island"),
  A("dungeon_drc", "dungeon", "Dragon Roost Cavern entrance", [], stage="Adanmae", biome="volcanic",
    door_macro="Can Access Dungeon Entrance on Dragon Roost Island", home="Dragon Roost Island"),

  # ---- Forest Haven
  A("forest_haven", "house", "Forest Haven (Deku Tree)", ["Forest Haven - On Tree Branch"], stage="Omori",
    actors=("d_a_deku_item",), biome="forest", door_macro="Can Access Forest Haven", home="Forest Haven",
    note="Omori is the way to the Forbidden Woods door and the Forest Firefly"),
  A("fh_small_island", "puzzle", "Small island chest by Forest Haven", ["Forest Haven - Small Island Chest"],
    biome="forest", home="Forest Haven"),
  A("dungeon_fw", "dungeon", "Forbidden Woods entrance", [], stage="kindan", biome="forest",
    door_macro="Can Access Dungeon Entrance in Forest Haven Sector", home="Forest Haven",
    note="reached by flying from Forest Haven"),

  # ---- other islands' terrain puzzles and caves
  A("greatfish_chest", "puzzle", "Hidden chest (Greatfish)", ["Greatfish Isle - Hidden Chest"], home="Greatfish Isle"),
  A("mother_isle", "fountain", "Great Fairy inside the Mother Isle", ["Mother and Child Isles - Inside Mother Isle"],
    home="Mother and Child Isles", note="opened by the Ballad of Gales, no door"),
  A("fire_mountain_cave", "cave", "Volcano cave", ["Fire Mountain - Cave - Chest"], stage="MiniKaz", biome="volcanic",
    door_macro="Can Access Secret Cave Entrance on Fire Mountain", home="Fire Mountain"),
  A("frozen_chest", "puzzle", "Frozen chest", ["Ice Ring Isle - Frozen Chest"], biome="snow", home="Ice Ring Isle"),
  A("ice_ring_cave", "cave", "Ice cave (with the inner cave)", ["Ice Ring Isle - Cave - Chest",
    "Ice Ring Isle - Inner Cave - Chest"], stage="MiniHyo", biome="snow",
    door_macro="Can Access Secret Cave Entrance on Ice Ring Isle", home="Ice Ring Isle"),
  A("headstone_top", "puzzle", "Seagull-switch summit", ["Headstone Island - Top of the Island"], home="Headstone Island"),
  A("dungeon_et", "dungeon", "Earth Temple entrance", [], stage="Edaichi", biome="rock",
    door_macro="Can Access Dungeon Entrance on Headstone Island", home="Headstone Island"),
  A("dungeon_wt", "dungeon", "Wind Temple entrance", [], stage="Ekaze", biome="rock",
    door_macro="Can Access Dungeon Entrance on Gale Isle", home="Gale Isle"),
  A("oasis_cabana", "house", "Cabana (with the labyrinth)", ["Private Oasis - Cabana Labyrinth - Lower Floor Chest",
    "Private Oasis - Cabana Labyrinth - Upper Floor Chest"], stage="Abesso", biome="sand",
    door_macro="Can Access Secret Cave Entrance on Private Oasis", home="Private Oasis",
    note="the labyrinth TF_04 is reached from inside the cabana"),
  A("waterfall_chest", "puzzle", "Chest at the top of a waterfall", ["Private Oasis - Chest at Top of Waterfall"],
    home="Private Oasis"),
  A("barrel_shooting", "landmark", "Barrel shooting range", ["Spectacle Island - Barrel Shooting - First Prize",
    "Spectacle Island - Barrel Shooting - Second Prize"], actors=("d_a_npc_kg2",), home="Spectacle Island"),
  A("needle_chest", "puzzle", "Seagull-switch chest", ["Needle Rock Isle - Chest"], home="Needle Rock Isle"),
  A("needle_cave", "cave", "Needle cave", ["Needle Rock Isle - Cave"], stage="SubD42",
    door_macro="Can Access Secret Cave Entrance on Needle Rock Isle", home="Needle Rock Isle"),
  A("angular_peak", "puzzle", "Block-puzzle peak", ["Angular Isles - Peak"], home="Angular Isles"),
  A("angular_cave", "cave", "Angular cave", ["Angular Isles - Cave"], stage="SubD43",
    door_macro="Can Access Secret Cave Entrance on Angular Isles", home="Angular Isles"),
  A("boating_cave", "cave", "Boating course cave", ["Boating Course - Cave"], stage="SubD71",
    door_macro="Can Access Secret Cave Entrance on Boating Course", home="Boating Course"),
  A("stone_watcher_cave", "cave", "Stone watcher cave", ["Stone Watcher Island - Cave"], stage="TF_01",
    door_macro="Can Access Secret Cave Entrance on Stone Watcher Island", home="Stone Watcher Island"),
  A("overlook_cave", "cave", "Overlook cave", ["Overlook Island - Cave"], stage="TF_02",
    door_macro="Can Access Secret Cave Entrance on Overlook Island", home="Overlook Island"),
  A("birds_peak_cave", "cave", "Bird's peak cave", ["Bird's Peak Rock - Cave"], stage="TF_03",
    door_macro="Can Access Secret Cave Entrance on Bird's Peak Rock", home="Bird's Peak Rock"),
  A("chuchu_cave", "cave", "ChuChu cave", ["Pawprint Isle - Chuchu Cave - Chest",
    "Pawprint Isle - Chuchu Cave - Behind Left Boulder", "Pawprint Isle - Chuchu Cave - Behind Right Boulder",
    "Pawprint Isle - Chuchu Cave - Scale the Wall"], stage="TyuTyu",
    door_macro="Can Access Secret Cave Entrance on Pawprint Isle", home="Pawprint Isle"),
  A("wizzrobe_cave", "cave", "Wizzrobe cave", ["Pawprint Isle - Wizzrobe Cave"], stage="Cave07",
    door_macro="Can Access Secret Cave Entrance on Pawprint Isle Side Isle", home="Pawprint Isle"),
  A("warp_maze_cave", "cave", "Warp maze cave", ["Diamond Steppe Island - Warp Maze Cave - First Chest",
    "Diamond Steppe Island - Warp Maze Cave - Second Chest"], stage="WarpD",
    door_macro="Can Access Secret Cave Entrance on Diamond Steppe Island", home="Diamond Steppe Island"),
  A("bomb_cave", "cave", "Bomb cave", ["Bomb Island - Cave"], stage="Cave01",
    door_macro="Can Access Secret Cave Entrance on Bomb Island", home="Bomb Island"),
  A("rock_spire_cave", "cave", "Rock spire cave", ["Rock Spire Isle - Cave"], stage="Cave04",
    door_macro="Can Access Secret Cave Entrance on Rock Spire Isle", home="Rock Spire Isle"),
  A("shark_cave", "cave", "Shark cave", ["Shark Island - Cave"], stage="ITest63",
    door_macro="Can Access Secret Cave Entrance on Shark Island", home="Shark Island"),
  A("cliff_plateau_cave", "cave", "Plateau cave (leads up to the highest isle)", ["Cliff Plateau Isles - Cave",
    "Cliff Plateau Isles - Highest Isle"], stage="Cave03",
    door_macro="Can Access Secret Cave Entrance on Cliff Plateau Isles", home="Cliff Plateau Isles",
    note="its inner exit comes out on top of a tall pillar in the original; here a pillar next to the door"),
  A("crescent_chest", "puzzle", "Beach chest", ["Crescent Moon Island - Chest"], home="Crescent Moon Island"),
  A("golf", "puzzle", "Golf course", ["Horseshoe Island - Play Golf"], home="Horseshoe Island"),
  A("horseshoe_cave", "cave", "Horseshoe cave", ["Horseshoe Island - Cave"], stage="Cave05",
    door_macro="Can Access Secret Cave Entrance on Horseshoe Island", home="Horseshoe Island"),
  A("flight_platform", "landmark", "Bird-Man contest platform", ["Flight Control Platform - Bird-Man Contest - First Prize"],
    actors=("d_a_npc_bmcon1",), home="Flight Control Platform"),
  A("star_cave", "cave", "Star cave", ["Star Island - Cave"], stage="Cave02",
    door_macro="Can Access Secret Cave Entrance on Star Island", home="Star Island"),
  A("tingle_tower", "landmark", "Tingle Tower (Ankle)", ["Tingle Island - Ankle - Reward for All Tingle Statues"],
    actors=("d_a_npc_tc",), home="Tingle Island"),
  A("islet_of_steel", "sea", "Islet of Steel", ["Islet of Steel - Interior"], stage="ShipD", home="Islet of Steel"),

  # ---- fairy fountains (doors)
  A("fountain_thorned", "fountain", "Fairy fountain (Skull Hammer)", ["Thorned Fairy Island - Great Fairy"],
    stage="Fairy05", door_macro="Can Access Fairy Fountain Entrance on Thorned Fairy Island", home="Thorned Fairy Island"),
  A("fountain_eastern", "fountain", "Fairy fountain (boulder)", ["Eastern Fairy Island - Great Fairy"],
    stage="Fairy02", door_macro="Can Access Fairy Fountain Entrance on Eastern Fairy Island", home="Eastern Fairy Island"),
  A("fountain_western", "fountain", "Fairy fountain (Skull Hammer)", ["Western Fairy Island - Great Fairy"],
    stage="Fairy03", door_macro="Can Access Fairy Fountain Entrance on Western Fairy Island", home="Western Fairy Island"),
  A("fountain_southern", "fountain", "Fairy fountain (blockade)", ["Southern Fairy Island - Great Fairy"],
    stage="Fairy06", door_macro="Can Access Fairy Fountain Entrance on Southern Fairy Island", home="Southern Fairy Island"),
  A("fountain_northern", "fountain", "Fairy fountain (open)", ["Northern Fairy Island - Great Fairy"],
    stage="Fairy01", door_macro="Can Access Fairy Fountain Entrance on Northern Fairy Island", home="Northern Fairy Island"),

  # ---- things on the sea, any island
  A("postbox", "landmark", "Rito postbox", [l for l in [
    "Mailbox - Letter from Hoskit's Girlfriend", "Mailbox - Letter from Baito's Mother", "Mailbox - Letter from Baito",
    "Mailbox - Letter from Komali's Father", "Mailbox - Letter Advertising Bombs in Beedle's Shop",
    "Mailbox - Letter Advertising Rock Spire Shop Ship", "Mailbox - Beedle's Silver Membership Reward",
    "Mailbox - Beedle's Gold Membership Reward", "Mailbox - Letter from Orca", "Mailbox - Letter from Grandma",
    "Mailbox - Letter from Aryll", "Mailbox - Letter from Tingle"]], actors=("d_a_obj_toripost",),
    note="one per village; mail only needs one reachable"),
  A("withered_trees", "landmark", "Withered trees (Forest Water)", ["The Great Sea - Withered Trees"],
    actors=("d_a_obj_ftree",), note="several trees over the world"),
  A("beedle_ship", "sea", "Beedle's shop ship", ["The Great Sea - Beedle's Shop Ship - 20 Rupee Item"],
    stage="Obshop", actors=("d_a_npc_bs1",), note="several moored around the world; sells bait and Hyoi pears"),
  A("beedle_special", "sea", "Beedle's special shop ship", ["Rock Spire Isle - Beedle's Special Shop Ship - 500 Rupee Item",
    "Rock Spire Isle - Beedle's Special Shop Ship - 950 Rupee Item",
    "Rock Spire Isle - Beedle's Special Shop Ship - 900 Rupee Item"], stage="Obshop", actors=("d_a_npc_bs1",)),
  A("salvage_corp", "sea", "Salvage Corp", ["The Great Sea - Salvage Corp Gift"], actors=("d_a_npc_sv",)),
  A("cyclos", "sea", "Cyclos (the cyclone)", ["The Great Sea - Cyclos"]),
  A("goron_traders", "sea", "Goron merchants (three ships)", ["The Great Sea - Goron Trading Reward"],
    actors=("d_a_npc_roten",)),
  A("ghost_ship", "sea", "Ghost Ship", ["The Great Sea - Ghost Ship"], stage="PShip"),
  A("big_octo_fairy", "sea", "Big Octo hiding a Great Fairy", ["Two-Eye Reef - Big Octo Great Fairy"]),
]

# Platforms, rafts, gunboats, Big Octos, submarines and eye reef chests: each location is its own sea anchor.
SEA_TYPES = ("Platform", "Raft", "Gunboat", "Big Octo", "Submarine", "Eye Reef Chest")

# The sea rooms that stay as they are. Room numbers count from 1, row by row from the top left of the chart.
PREFABS = [
  A("prefab_ff", "prefab", "Forsaken Fortress", [], home="sea room 1"),
  A("prefab_totg", "prefab", "Tower of the Gods (with the three Triangle Island statues, Hyrule below)", [],
    stage="Siren", home="sea room 26", note="Tower rises in place; Hyrule is entered from it"),
]

DUNGEON_ZONES = ("Dragon Roost Cavern", "Forbidden Woods", "Tower of the Gods", "Forsaken Fortress", "Earth Temple",
                 "Wind Temple", "Hyrule", "Ganon's Tower")

# Island macros that only described the original island's terrain. In a generated world, the door's site and its
# kept obstacle say it instead.
NEUTRAL_MACROS = {
  "Can Reach Outset Island Upper Level": "Nothing",
}


def assign(locations):
  """Location name -> anchor id for every location. Sunken treasure becomes a salvage spot of its own."""
  out = {}
  for a in ANCHORS:
    for l in a.locations:
      out[l] = a.id
  for name, loc in locations.items():
    if name in out:
      continue
    zone = name.split(" - ")[0]
    types = loc["types"]
    if zone in DUNGEON_ZONES or "Boss" in types:
      out[name] = "dungeon:" + zone
    elif "Sunken Treasure" in types:
      out[name] = "salvage:" + zone
    elif any(t in types for t in SEA_TYPES):
      out[name] = "sea:" + name
    else:
      out[name] = None
  return out


if __name__ == "__main__":
  import sys
  import os
  sys.path.insert(0, os.path.dirname(__file__))
  import wwlogic
  if "--csv" in sys.argv:
    import csv
    logic = wwlogic.WwLogic(wwlogic.find_wwrando())
    w = csv.writer(sys.stdout)
    w.writerow(["anchor", "kind", "name", "interior stage", "actors", "original home", "door obstacle", "checks",
                "note"])
    for a in ANCHORS + PREFABS:
      obstacle = logic.macros.get(a.door_macro, "") if a.door_macro else ""
      w.writerow([a.id, a.kind, a.name, a.stage, " ".join(a.actors), a.home, obstacle, len(a.locations), a.note])
