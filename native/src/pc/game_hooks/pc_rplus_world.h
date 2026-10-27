// Randomizer+ (pc/pc_rplus.h): what the generated world's land and layout (game_hooks/pc_rplus_terrain.cpp) tell
// the rest of the generated world, its people and its enemies (game_hooks/pc_rplus_life.cpp). World units.
#ifndef PC_RPLUS_WORLD_H
#define PC_RPLUS_WORLD_H

#include <vector>

namespace rplus {

struct TownInfo {
    float x, z, y;  // the plaza's middle and the town's ground
    float r;        // the houses' ring
    float plaza;    // the paved middle's radius
    int room;       // the sea room it is in
};

std::vector<TownInfo> towns();
// The drawn ground's height at (x, z) (what Link and actors stand on).
float groundY(float x, float z);
// The sea room (1-49) at (x, z), 0 outside the sea.
int roomAt(float x, float z);
// The biome letter at (x, z) ('.' for water).
char biomeAt(float x, float z);
// True when (x, z) is clear of buildings, doorsteps, plazas, paths and fences by at least clear.
bool openGround(float x, float z, float clear);
// A spot on land near the centre of cell (cx, cy), in its room.
void cellSpot(int cx, int cy, float* x, float* z);
// Where Link starts.
void startXZ(float* x, float* z);
// A sea room's north-west corner (its square is 100000 units a side).
void roomCorner(int roomNo, float* x, float* z);
// Too steep to stand a creature on.
bool steep(float x, float z);

} // namespace rplus

// The generated world's people and enemies for sea room roomNo (from pc_rplus_room_actors).
void pc_rplus_life_room(int roomNo);

#endif
