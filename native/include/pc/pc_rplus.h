/*
 * pc_rplus.h - Randomizer+ (docs/RANDOMIZER_PLUS.md): a generated Great Sea.
 *
 * scripts/rplus/worldgen.py makes a world (land masses, villages, where every NPC, door and chest goes) and
 * writes it as world.rpw. With COS_RPLUS_WORLD=<path to world.rpw> the game reads it at start and the sea rooms
 * the world generates (all but the prefab rooms it names) are built from it: terrain and collision made in memory
 * (game_hooks/pc_rplus_terrain.cpp) in place of the room's island, the room's own actors left out.
 *
 * Cells: the sea is squares x squares rooms (7 x 7), each cells x cells cells. Cell (0, 0) is the north-west
 * corner of room 1 (chart square A1); x grows east, y south. The game thread calls these.
 */
#ifndef PC_RPLUS_H
#define PC_RPLUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when a world was read (COS_RPLUS_WORLD), else 0. */
int pc_rplus_active(void);

/* Grass, flowers and small trees drawn from the game's packets: scales the model matrix m (its 3 x 3 part) a
   little up or down, the same each frame for the spot (x, z); nothing when Randomizer+ is off. */
void pc_rplus_veg_scale(float m[3][4], float x, float z);

/* 1 when sea room roomNo (1-49) is generated, 0 when it is kept as it is (or Randomizer+ is off). */
int pc_rplus_sea_room_generated(int roomNo);

/* The grid: squares per side, cells per square side. */
int pc_rplus_squares(void);
int pc_rplus_cells(void);

/* Cell height, 0 for water, else 1..1000 (relative height on its land), and biome letter ('.' for water). Cells
 * outside the grid are water. */
int pc_rplus_cell_height(int cx, int cy);
char pc_rplus_cell_biome(int cx, int cy);

/* The start cell (Link's house). */
void pc_rplus_start_cell(int* cx, int* cy);

/* The world's anchors (scripts/rplus/anchors.py): what stands where. kind is house, cave, fountain, dungeon, npc,
 * landmark, puzzle or sea; stage is the interior behind its door ("" without one). 1 when i is one. */
int pc_rplus_anchor_count(void);
int pc_rplus_anchor(int i, const char** id, const char** kind, const char** stage, int* cx, int* cy);

/* The tracker's lines of the world file (worldgen.py write_rpw; scripts/rplus/rpw_add_places.py adds them to an
 * older world): an anchor's name ("" without one), its wwrando locations ("Outset Island - Jabun's Cave"), the
 * village it is in (a village's id, -1 none); the villages (id, name, centre cell). 1 when i is one. */
const char* pc_rplus_anchor_name(const char* id);
int pc_rplus_anchor_check_count(const char* id);
const char* pc_rplus_anchor_check(const char* id, int i);
int pc_rplus_anchor_village(const char* id);
int pc_rplus_village_count(void);
int pc_rplus_village(int i, int* id, const char** name, int* cx, int* cy);

/* Randomizer+ places found (game_hooks/pc_rplus_places.cpp, docs/RANDOMIZER_PLUS.md "Discovery tracker"): every
 * door, anchored NPC and town of the world, and the camps and lookout towers once made, each found when Link comes
 * near it (or goes in its door). Only found places show on the tracker; found ones are kept per seed and file in
 * rplus-places-<seed>-<slot>.txt next to the saves. */
typedef struct PcRplusPlace {
    const char* key;   /* "a:<anchor id>", "town:<n>", "camp:<key>", "tower:<key>" */
    const char* name;  /* "Tozi Cove", "Mesa's house", "Enemy camp" */
    char kind;         /* 'T' town, 'h' house, 'c' cave, 'd' dungeon, 'f' fairy fountain, 'n' NPC, 'k' camp,
                          'w' lookout tower */
    float u, v;        /* on the chart: 0..1 west to east, north to south */
    int sector;        /* 0-48 (chart square A1..G7), -1 off the chart */
    int found;
    int camp;          /* camps and towers: the camp chest key, else -1 */
    int opened;        /* camps and towers: their chest opened */
    const char* anchor;/* the anchor id whose checks it has ("" none) */
} PcRplusPlace;
/* All places known now (the world's and the camps and towers made or found so far); NULL past the end. */
int pc_rplus_place_count(void);
const PcRplusPlace* pc_rplus_place(int i);
/* The world's own places (not camps and towers) and how many of them are found. */
void pc_rplus_places_found(int* found, int* total);
/* The name of the place found last, once ("Tozi Cove"); NULL when none is new. */
const char* pc_rplus_places_take_found(void);
/* 1 when the tracker shows sector (0-48) open: sailed in (the save's arrive bit, as the sea chart), a place in it
 * found, or COS_QOL_REVEAL_CHART. */
int pc_rplus_sector_seen(int sector);

/* The world's seed, for the log and the file select. */
unsigned int pc_rplus_seed(void);
// Quick-test start (Dillon, 2026-11-02): the world file's "test 1" line, the Quality of life tab's "Test start" row,
// or COS_RPLUS_TEST_START=1 in the environment (=0 there turns it off even for a test world). Chests by the start give the six weapons and house decor, with a camp and a lookout tower near.
int pc_rplus_test_start(void);

/* From pc_harness_init, right after pc_seedgen_boot: applies the Randomizer+ tab's choices kept in
 * <user dir>/rando/rplus.ini to the environment: "World to play" sets or clears COS_RPLUS_WORLD, and
 * "Save files: own for each seed" sets COS_SAVE_DIR (<user dir>/saves/<seed>[+<world>]/), the memory
 * card's folder pc_aurora_init gives Aurora. */
void pc_rplus_boot(void);

#ifdef __cplusplus
}

#include <string>

/* The options menu's Randomizer+ tab (pc_rplus_menu.cpp), by row index, as pc_seedgen.h's tab. Worlds
 * are made by scripts/rplus/worldgen.py (bundled in seedgen/rplus/, run by seedgen's Python) into
 * <user dir>/rando/rplus/<number>/world.rpw. */
namespace pc::rplus_menu {

enum class RowStyle { Header, Action, Value };

int rowCount();
RowStyle rowStyle(int row);
std::string rowLabel(int row);
std::string rowValue(int row);
std::string rowHelp(int row);
/* dir 0: A; -1 / 1: left / right. */
void activate(int row, int dir);
std::string takeToast();
/* True once after a world was made (it is now the world to play). */
bool takeWorldReady();

} // namespace pc::rplus_menu
#endif

#endif /* PC_RPLUS_H */
