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

/* The world's seed, for the log and the file select. */
unsigned int pc_rplus_seed(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_RPLUS_H */
