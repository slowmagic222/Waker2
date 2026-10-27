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

/* The world's seed, for the log and the file select. */
unsigned int pc_rplus_seed(void);

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
