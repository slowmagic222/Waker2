// Randomizer+ terrain (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): a generated sea room's land, built from the world's
// cell heights when the room's daBg is created, in place of the room's island model and collision.
//
// Height: the world gives each cell a relative height (0 = water). The surface is the bilinear blend of the cells'
// heights at their centres (land 80 + h * 3.2 units, water -1400), sampled SUB times per cell side, with
// noise on land (hills, ridges, terraces: naturalY) and the ground levelled for buildings and paths (shapeGround).
// It is a function of the world position alone, so neighbouring rooms meet without a seam.
//
// Collision: a DZB built in memory in the disc's layout (big-endian, table offsets from the start), then made
// ready by cBgS::ConvDzb as a room's room.dzb is (d_resorce.cpp), and handed to dBgW::Set. One group (the room),
// one poly info per ground kind, blocks of BLK x BLK quads under an 8-way tree.
//
// Drawing: one J3DPacket with the triangles in GX immediate mode, vertex colours from the biome with the light
// baked in two tones; the game's own textures come later.
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "pc_rplus_world.h"
#include "d/d_com_inf_game.h"
#include "d/d_bg_w.h"
#include "d/d_kankyo.h"
#include "d/d_item.h"
#include "d/d_item_data.h"
#include "d/d_stage.h"
#include "d/actor/d_a_sea.h"
#include "f_op/f_op_actor_mng.h"
#include "SSystem/SComponent/c_math.h"
#include "SSystem/SComponent/c_counter.h"
#include "SSystem/SComponent/c_bg_s.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "dolphin/gx/GX.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr int SUB = 6;               // samples per cell side
constexpr int BLK = 4;               // quads per collision block side
constexpr float ROOM_UNITS = 100000.0f;
constexpr float LAND_BASE = 80.0f;
constexpr float LAND_SCALE = 3.2f;   // height units per relative height (0..1000)
constexpr float WATER_Y = -1400.0f;
constexpr float KEEP_Y = -600.0f;    // a quad with a corner above this is kept

float cellSize() {
    return ROOM_UNITS / pc_rplus_cells();
}

// The room's centre in the world: the stage's MULT entry, else the chart grid around the origin.
void roomCentre(int roomNo, float* x, float* z) {
    f32 tx, tz;
    s16 ang;
    if (dComIfGp_getMapTrans(roomNo, &tx, &tz, &ang)) {
        *x = tx;
        *z = tz;
        return;
    }
    int n = pc_rplus_squares();
    int r = roomNo - 1;
    *x = ((r % n) - (n - 1) * 0.5f) * ROOM_UNITS;
    *z = ((r / n) - (n - 1) * 0.5f) * ROOM_UNITS;
}

// The world's north-west corner, from room 1's centre.
void worldOrigin(float* x0, float* z0) {
    float cx, cz;
    roomCentre(1, &cx, &cz);
    *x0 = cx - ROOM_UNITS * 0.5f;
    *z0 = cz - ROOM_UNITS * 0.5f;
}

float cellY(int cx, int cy) {
    int h = pc_rplus_cell_height(cx, cy);
    return h == 0 ? WATER_Y : LAND_BASE + h * LAND_SCALE;
}

u32 hash2(int x, int y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + pc_rplus_seed() * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth value noise in [0, 1] at (x, y) in noise units, one lattice per salt.
float vnoise(float x, float y, int salt) {
    const int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    auto v = [&](int i, int j) { return (hash2(i * 31 + salt * 977, j * 17 - salt * 131) & 0xFFFF) / 65535.0f; };
    const float a = v(ix, iy), b = v(ix + 1, iy), c = v(ix, iy + 1), d = v(ix + 1, iy + 1);
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

// Fractal noise: octaves of vnoise, each twice as fine and half as strong, in [0, 1].
float fbm(float x, float y, int salt, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; o++) {
        sum += vnoise(x, y, salt + o * 7) * amp;
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// How rugged a cell's biome is: rock, volcanic and snow rise in ridges and terraces, the rest rolls.
float cellRugged(int cx, int cy) {
    switch (pc_rplus_cell_biome(cx, cy)) {
    case 'r':
    case 'v': return 1.0f;
    case 's': return 0.8f;
    case 'f': return 0.35f;
    default: return 0.0f;
    }
}

// The natural height at grid position (gx, gy) in cells from the world's corner. The cells' heights blended smoothly (on a
// grid bent by noise, so coasts wander instead of following the cells), then on land: rolling hills everywhere,
// ridges where the biome is rugged, and small bumps. The ground made from it rises in shelves (tierY).
float naturalY(float gx, float gy) {
    // (fbm's spread is narrow: about 0.5 +- 0.1, so the bends are some 0.4 cells broad and 0.15 fine.)
    const float wx = gx + (fbm(gx * 0.35f, gy * 0.35f, 11, 3) - 0.5f) * 4.0f +
                     (vnoise(gx * 1.3f, gy * 1.3f, 17) - 0.5f) * 0.5f;
    const float wy = gy + (fbm(gx * 0.35f, gy * 0.35f, 23, 3) - 0.5f) * 4.0f +
                     (vnoise(gx * 1.3f, gy * 1.3f, 29) - 0.5f) * 0.5f;
    float fx = wx - 0.5f, fy = wy - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = cellY(ix, iy), b = cellY(ix + 1, iy), c = cellY(ix, iy + 1), d = cellY(ix + 1, iy + 1);
    float y = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    if (y <= LAND_BASE) {
        return y;
    }
    const float land = y - LAND_BASE;
    const float inland = fminf(1.0f, land / 700.0f);  // the shore stays a beach
    const float rug = (cellRugged(ix, iy) * (1 - tx) + cellRugged(ix + 1, iy) * tx) * (1 - ty) +
                      (cellRugged(ix, iy + 1) * (1 - tx) + cellRugged(ix + 1, iy + 1) * tx) * ty;
    // Rolling hills, about two cells across.
    float h = land + (fbm(gx * 0.55f, gy * 0.55f, 41, 4) - 0.45f) * 900.0f * inland;
    // Ridges: the creases of noise, sharp on top.
    const float r = 1.0f - fabsf(fbm(gx * 0.8f, gy * 0.8f, 57, 3) * 2.0f - 1.0f);
    h += r * r * r * 1500.0f * rug * inland;
    // Bumps.
    h += (fbm(gx * 3.0f, gy * 3.0f, 73, 2) - 0.5f) * 140.0f * inland;
    return LAND_BASE + fmaxf(h, 5.0f * inland);
}

float shapeGround(float gx, float gy, float y);

// The natural height levelled where the world builds (shapeGround: towns, buildings): the land's "e", which the
// ground's shelves are made from.
float eAt(float gx, float gy) {
    return shapeGround(gx, gy, naturalY(gx, gy));
}

// Shelves: above the lowland the ground rises in tiers, each nearly flat (keeping SHELF of the rise under it), with
// a cliff where e crosses into the next (TIER_H of e, so a cliff is TIER_H * (1 - SHELF) high). The beach and
// tier 1 join smoothly, so every island can be walked onto from the sea; higher tiers are reached by the stairs
// built where paths cross their cliffs (Layout::ramps).
constexpr float TIER1 = LAND_BASE + 420.0f;
constexpr float TIER_H = 760.0f;
constexpr float SHELF = 0.22f;

int tierOf(float e) {
    return e < TIER1 ? 0 : 1 + (int)floorf((e - TIER1) / TIER_H);
}
// Where tier k starts in e (k >= 1).
float tierStart(int k) {
    return TIER1 + (k - 1) * TIER_H;
}
// Tier k's ground for e: linear in e within the tier.
float tierY(int k, float e) {
    return k <= 0 ? e : tierStart(k) + (e - tierStart(k)) * SHELF;
}

// e at the world's mesh sample (i, j) (SUB a cell), kept once worked out: the mesh, the collision and every height
// asked for read the same values. Cleared when the layout (its levelled ground) is made again.
std::vector<float> sGridE;
int sGridN = 0;

void clearGridE() {
    sGridE.clear();
    sGridN = 0;
}

float gridE(int i, int j) {
    const int n = pc_rplus_squares() * pc_rplus_cells() * SUB + 1;
    if (i < 0 || j < 0 || i >= n || j >= n || n > 4096) {
        return eAt((float)i / SUB, (float)j / SUB);
    }
    if (sGridN != n || sGridE.empty()) {
        sGridE.assign((size_t)n * n, NAN);
        sGridN = n;
    }
    float& v = sGridE[(size_t)j * n + i];
    if (std::isnan(v)) {
        v = eAt((float)i / SUB, (float)j / SUB);
    }
    return v;
}

// e at (gx, gy) as the mesh has it: linear over each of a quad's two triangles (split on the (i+1, j)-(i, j+1)
// diagonal, as drawn), so the tiers' edges are the straight lines the mesh draws.
float meshE(float gx, float gy) {
    const float sx = gx * SUB, sy = gy * SUB;
    const int i = (int)floorf(sx), j = (int)floorf(sy);
    const float tx = sx - i, ty = sy - j;
    if (tx + ty <= 1.0f) {
        const float a = gridE(i, j);
        return a + (gridE(i + 1, j) - a) * tx + (gridE(i, j + 1) - a) * ty;
    }
    const float d = gridE(i + 1, j + 1);
    return d + (gridE(i, j + 1) - d) * (1.0f - tx) + (gridE(i + 1, j) - d) * (1.0f - ty);
}

// The ground's height at (gx, gy) in cells from the world's corner: exactly the drawn and solid ground.
float heightAt(float gx, float gy) {
    const float e = meshE(gx, gy);
    return tierY(tierOf(e), e);
}

// The ground's height at world position (x, z).
float heightAt_world(float x, float z) {
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    return heightAt((x - x0) / cs, (z - z0) / cs);
}

char biomeNear(float gx, float gy) {
    int cx = (int)floorf(gx), cy = (int)floorf(gy);
    char b = pc_rplus_cell_biome(cx, cy);
    if (b != '.') {
        return b;
    }
    for (int r = 1; r <= 1; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                char o = pc_rplus_cell_biome(cx + dx, cy + dy);
                if (o != '.') {
                    return o;
                }
            }
        }
    }
    return 'd';
}

struct Rgb {
    u8 r, g, b;
};

Rgb biomeColour(char b) {
    switch (b) {
    case 'g': return {112, 176, 82};
    case 'f': return {62, 128, 66};
    case 'r': return {146, 136, 120};
    case 'v': return {112, 74, 64};
    case 's': return {232, 236, 242};
    default: return {222, 204, 146};
    }
}

// Ground kinds: the poly info table, and the attribute each uses (dBgS::GetAttributeCode's atr_conv index).
// G_SHORE: the boat's shore walls, which Link, the camera and arrows pass through (poly info 3's pass bits,
// dBgW::ChkPolyThrough).
// G_EXIT: the floor just inside a generated door, with its exit id (poly info 0's bits 13-18, dBgS::GetExitId).
enum Ground { G_GRASS, G_STONE, G_SAND, G_DIRT, G_SHORE, G_EXIT, G_COUNT };
const u32 kGroundAttr[G_COUNT] = {0x04, 0x03, 0x0B, 0x01, 0x03, 0x03};
const u32 kGroundPass[G_COUNT] = {0, 0, 0, 0, 0x01 | 0x04 | 0x08, 0};
constexpr int HOME_EXIT = 0x3A;  // Link's home's door (pc_rplus_exit; not a room SCLS index)
constexpr int DOOR_EXIT0 = 0x20; // the other generated doors: 0x20 + their number in their room
// A generated building's door: the interior it opens to and where it stands.
struct GenDoor {
    const char* stage;  // the interior
    s8 spawn, room;     // where Link comes in
    const char* also;   // another interior whose way out to the sea comes back here too (or nullptr)
    const char* id;     // the anchor ("home": Link's home)
    int style;          // its look (houseQuads): 0 house, 1 cave or dungeon, 2 fairy fountain
    int seaRoom;        // the sea room it stands in
    cXyz pos;           // on the ground, the door's front facing angle
    s16 angle;
    int exitId;         // its exit floor's exit id
    int variant = 0;    // which roof colour (houses)
    int town = -1;      // the town it stands in (Layout::towns), or -1
};
const std::vector<GenDoor>& genDoors();
// A town: houses round a plaza at (x, z), the ground levelled to y, the houses r from the middle.
struct Town {
    float x, z, y, r;
    bool start;      // the start town (Link's home in it)
    float a0 = 0;    // the first house's angle round the middle, and how many houses
    int slots = 0;
};
// Ground levelled to y: wholly within r0 of (x, z), blending back to the land's own height by r1.
struct Pad {
    float x, z, y, r0, r1;
};
// A piece of path, and a piece of fence (kind 0 wooden fence, 1 low brick wall).
struct Seg {
    float x0, z0, x1, z1;
};
struct Fence {
    float x0, z0, x1, z1;
    int kind;
};
// Stairs up a cliff where a path climbs it: from the bottom (bx, bz) on the lower shelf at by to the top (tx, tz)
// just over the cliff's edge at ty, half wide.
struct Ramp {
    float bx, bz, tx, tz, by, ty, half;
};
float segDist2(const Seg& sg, float x, float z) {
    const float dx = sg.x1 - sg.x0, dz = sg.z1 - sg.z0, l2 = dx * dx + dz * dz;
    float t = l2 > 0.0f ? ((x - sg.x0) * dx + (z - sg.z0) * dz) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float ex = sg.x0 + dx * t - x, ez = sg.z0 + dz * t - z;
    return ex * ex + ez * ez;
}
// A town's house without a way in: drawn and solid, its door shut.
struct Deco {
    cXyz pos;
    s16 angle;
    int variant;
    int seaRoom;
};
struct Layout;
const Layout& layout();
const std::vector<Deco>& layoutDecos();
// What the ground is made into at (x, z): 0 the land's own, 1 a path, 2 a town's plaza.
int groundMark(float x, float z);
const std::vector<Fence>& layoutFences();
const std::vector<Ramp>& layoutRamps();
// Where Link starts (the start town's plaza, or land by the start cell) and its room.
int startSpot(float* x, float* z);

Ground groundOf(char b, float y, float ny) {
    if (y < LAND_BASE + 60.0f) {
        return G_SAND;
    }
    if (ny < 0.75f) {
        return G_STONE;
    }
    switch (b) {
    case 'g':
    case 'f': return G_GRASS;
    case 'r':
    case 'v': return G_STONE;
    case 'd': return G_SAND;
    default: return G_DIRT;
    }
}

struct Vtx {
    float x, y, z;
    u8 r, g, b, a;
};

// Big-endian writer for the DZB.
struct BeOut {
    u8* p;
    void u16at(u32 off, u16 v) { p[off] = (u8)(v >> 8); p[off + 1] = (u8)v; }
    void u32at(u32 off, u32 v) {
        p[off] = (u8)(v >> 24); p[off + 1] = (u8)(v >> 16); p[off + 2] = (u8)(v >> 8); p[off + 3] = (u8)v;
    }
    void f32at(u32 off, float f) {
        u32 v;
        memcpy(&v, &f, 4);
        u32at(off, v);
    }
};

u32 align4(u32 v) {
    return (v + 3) & ~3u;
}

} // namespace

class PcRplusPacket : public J3DPacket {
public:
    Vtx* mVtx = nullptr;   // triangle list, 3 per triangle
    int mTriCount = 0;
    int mRowTris[257] = {};  // triangles per sample row, to split GXBegin calls (and one row for the house)
    int mRows = 0;
    void draw() override;
};

struct PcRplusTerrain {
    PcRplusPacket packet;
    u8* dzb = nullptr;
};

namespace {

// The ground's detail: a 64 x 64 I8 texture of soft noise and short streaks (blades, grit), light enough to only
// shade the vertex colours, tiled over the world from the positions (GX_TG_POS) every TEX_SPAN units.
constexpr int TEX_N = 64;
constexpr float TEX_SPAN = 420.0f;
alignas(32) u8 sDetailTex[TEX_N * TEX_N];

const u8* detailTexture() {
    static bool made = false;
    if (made) {
        return sDetailTex;
    }
    made = true;
    for (int y = 0; y < TEX_N; y++) {
        for (int x = 0; x < TEX_N; x++) {
            // Tileable: the noise is sampled on a lattice that wraps at TEX_N.
            auto wrapNoise = [](float fx, float fy, int cells, int salt) {
                const float sx = fx * cells / TEX_N, sy = fy * cells / TEX_N;
                const int ix = (int)floorf(sx), iy = (int)floorf(sy);
                float tx = sx - ix, ty = sy - iy;
                tx = tx * tx * (3 - 2 * tx);
                ty = ty * ty * (3 - 2 * ty);
                auto v = [&](int i, int j) {
                    i = ((i % cells) + cells) % cells;
                    j = ((j % cells) + cells) % cells;
                    u32 h = (u32)i * 374761393u + (u32)j * 668265263u + (u32)salt * 2246822519u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
                };
                return (v(ix, iy) * (1 - tx) + v(ix + 1, iy) * tx) * (1 - ty) +
                       (v(ix, iy + 1) * (1 - tx) + v(ix + 1, iy + 1) * tx) * ty;
            };
            const float n = wrapNoise((float)x, (float)y, 4, 1) * 0.45f + wrapNoise((float)x, (float)y, 16, 2) * 0.35f +
                            wrapNoise((float)x * 1.0f, (float)y, 64, 3) * 0.2f;
            // Streaks: brighter dashes along one way, as blades or grit catch the light.
            const float streak = wrapNoise((float)x, (float)y * 0.25f, 32, 4);
            float v = 205.0f + n * 40.0f + (streak > 0.72f ? 12.0f : 0.0f);
            v = v > 255.0f ? 255.0f : v;
            // I8 is in 8 x 4 tiles.
            const int tile = (y / 4) * (TEX_N / 8) + (x / 8);
            sDetailTex[tile * 32 + (y % 4) * 8 + (x % 8)] = (u8)v;
        }
    }
    return sDetailTex;
}

} // namespace

void PcRplusPacket::draw() {
    if (mTriCount == 0) {
        return;
    }
    j3dSys.reinitGX();
    GXSetNumIndStages(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    // The detail texture, its coordinates from the world position: s along x, t along z (and a little y, so
    // slopes and walls aren't smeared flat).
    GXTexObj tex;
    GXInitTexObj(&tex, (void*)detailTexture(), TEX_N, TEX_N, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GXInitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    const float k = 1.0f / TEX_SPAN;
    Mtx texMtx = {{k, 0.0f, 0.0f, 0.0f}, {0.0f, k * 0.6f, k, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    GXLoadTexMtxImm(texMtx, GX_TEXMTX0, GX_MTX2x4);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetZCompLoc(GX_TRUE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    dKy_GxFog_set();
    GXLoadPosMtxImm(j3dSys.getViewMtx(), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetClipMode(GX_CLIP_ENABLE);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    const Vtx* v = mVtx;
    for (int row = 0; row < mRows; row++) {
        int n = mRowTris[row] * 3;
        if (n == 0) {
            continue;
        }
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, (u16)n);
        for (int i = 0; i < n; i++, v++) {
            GXPosition3f32(v->x, v->y, v->z);
            GXColor4u8(v->r, v->g, v->b, v->a);
        }
        GXEnd();
    }
    GXClearVtxDesc();
    J3DShape::resetVcdVatCache();
}

namespace {

// Link's home's house, drawn: walls round the door's hut (its collision is in buildTerrain) with a doorway the
// door fills and a gable roof. Each quad is 4 corners (p0 p1 p2 p3 going round) and its colour.
constexpr float HUT_HALF = 260.0f, HUT_DEPTH = 420.0f, DOORWAY_HALF = 85.0f;
constexpr float HUT_WALL = 300.0f, HUT_DOOR_TOP = 235.0f, HUT_RIDGE = 470.0f, HUT_EAVE = 45.0f;

struct HouseQuad {
    float p[4][3];
    u8 r, g, b;
};

// The house's colours by style: walls, walls in shade, trim, the roof's two slopes.
const u8 kHouseColours[3][5][3] = {
    {{236, 222, 186}, {196, 182, 150}, {120, 84, 52}, {178, 64, 44}, {148, 52, 36}},     // plaster, red roof
    {{150, 146, 138}, {120, 116, 110}, {90, 84, 78}, {104, 98, 90}, {84, 80, 74}},       // stone
    {{240, 240, 236}, {210, 212, 214}, {120, 160, 200}, {90, 140, 200}, {70, 116, 170}}, // white, blue roof
};

// Houses' roofs, by variant: red, blue, green, brown (each slope's colour).
const u8 kRoofs[4][2][3] = {{{178, 64, 44}, {148, 52, 36}},
                            {{64, 98, 160}, {50, 80, 134}},
                            {{74, 132, 82}, {58, 108, 66}},
                            {{136, 92, 60}, {112, 74, 48}}};

void houseQuads(const cXyz& door, s16 ang, int style, int variant, std::vector<HouseQuad>& out) {
    const u8 (*col)[3] = kHouseColours[style < 0 || style > 2 ? 0 : style];
    const u8 (*roof)[3] = style == 0 ? kRoofs[variant & 3] : &col[3];
    const float s = cM_ssin(ang), c = cM_scos(ang);  // out of the door's front
    auto pt = [&](float side, float ahead, float up, float* o) {
        o[0] = door.x + c * side + s * ahead;
        o[1] = door.y + up;
        o[2] = door.z - s * side + c * ahead;
    };
    auto quad = [&](const float (&q)[4][3], u8 r, u8 g, u8 b) {
        HouseQuad h;
        for (int k = 0; k < 4; k++) {
            pt(q[k][0], q[k][1], q[k][2], h.p[k]);
        }
        h.r = r;
        h.g = g;
        h.b = b;
        out.push_back(h);
    };
    const float W = HUT_HALF, D = -HUT_DEPTH, B = -700.0f, T = HUT_WALL;  // walls reach well into the ground
    const u8 wr = col[0][0], wg = col[0][1], wb = col[0][2];  // walls
    const u8 sr = col[1][0], sg = col[1][1], sb = col[1][2];  // walls in shade
    const u8 tr = col[2][0], tg = col[2][1], tb = col[2][2];  // trim
    // Front, either side of the doorway, and over it.
    quad({{-W, 0, B}, {-DOORWAY_HALF, 0, B}, {-DOORWAY_HALF, 0, T}, {-W, 0, T}}, wr, wg, wb);
    quad({{DOORWAY_HALF, 0, B}, {W, 0, B}, {W, 0, T}, {DOORWAY_HALF, 0, T}}, wr, wg, wb);
    quad({{-DOORWAY_HALF, 0, HUT_DOOR_TOP}, {DOORWAY_HALF, 0, HUT_DOOR_TOP}, {DOORWAY_HALF, 0, T},
          {-DOORWAY_HALF, 0, T}}, wr, wg, wb);
    // Sides and back.
    quad({{-W, D, B}, {-W, 0, B}, {-W, 0, T}, {-W, D, T}}, sr, sg, sb);
    quad({{W, 0, B}, {W, D, B}, {W, D, T}, {W, 0, T}}, sr, sg, sb);
    quad({{W, D, B}, {-W, D, B}, {-W, D, T}, {W, D, T}}, sr, sg, sb);
    // Gables: the triangles over the front and back walls (a quad with two corners at the ridge).
    quad({{-W, 0, T}, {W, 0, T}, {0, 0, HUT_RIDGE}, {0, 0, HUT_RIDGE}}, wr, wg, wb);
    quad({{W, D, T}, {-W, D, T}, {0, D, HUT_RIDGE}, {0, D, HUT_RIDGE}}, sr, sg, sb);
    // Timber band at the eaves and a frame round the doorway.
    quad({{-W - 2, 2, T - 22}, {W + 2, 2, T - 22}, {W + 2, 2, T}, {-W - 2, 2, T}}, tr, tg, tb);
    quad({{-DOORWAY_HALF - 18, 2, B}, {-DOORWAY_HALF, 2, B}, {-DOORWAY_HALF, 2, HUT_DOOR_TOP + 18},
          {-DOORWAY_HALF - 18, 2, HUT_DOOR_TOP + 18}}, tr, tg, tb);
    quad({{DOORWAY_HALF, 2, B}, {DOORWAY_HALF + 18, 2, B}, {DOORWAY_HALF + 18, 2, HUT_DOOR_TOP + 18},
          {DOORWAY_HALF, 2, HUT_DOOR_TOP + 18}}, tr, tg, tb);
    quad({{-DOORWAY_HALF, 2, HUT_DOOR_TOP}, {DOORWAY_HALF, 2, HUT_DOOR_TOP}, {DOORWAY_HALF, 2, HUT_DOOR_TOP + 18},
          {-DOORWAY_HALF, 2, HUT_DOOR_TOP + 18}}, tr, tg, tb);
    // Roof: two slopes from the eaves (past the walls) to the ridge, front to back with an overhang.
    const float ew = W + HUT_EAVE, ey = T - HUT_EAVE * (HUT_RIDGE - T) / W;
    const float f = HUT_EAVE, k = D - HUT_EAVE;
    quad({{-ew, f, ey}, {0, f, HUT_RIDGE + 6}, {0, k, HUT_RIDGE + 6}, {-ew, k, ey}}, roof[0][0], roof[0][1], roof[0][2]);
    quad({{0, f, HUT_RIDGE + 6}, {ew, f, ey}, {ew, k, ey}, {0, k, HUT_RIDGE + 6}}, roof[1][0], roof[1][1], roof[1][2]);
}

constexpr float FENCE_H = 120.0f, BRICK_H = 110.0f, BRICK_HALF = 32.0f;

// A fence piece, drawn: a wooden fence (a post at each end, two rails) or a low brick wall (a box with a cap),
// standing on the ground at both ends.
void fenceQuads(const Fence& f, std::vector<HouseQuad>& out) {
    const float ya = heightAt_world(f.x0, f.z0), yb = heightAt_world(f.x1, f.z1);
    const float dx = f.x1 - f.x0, dz = f.z1 - f.z0, len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        return;
    }
    const float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;  // along, across
    auto box = [&](float t0, float t1, float h0, float h1, float half, u8 r, u8 g, u8 b, u8 tr, u8 tg, u8 tb) {
        // Corners at t0 and t1 along, +-half across, from the ground + h0 to the ground + h1.
        float p[2][2][3][2];  // [end][side][xyz][bottom/top]
        for (int e = 0; e < 2; e++) {
            const float t = e == 0 ? t0 : t1;
            const float gy = ya + (yb - ya) * (t / len);
            for (int sd = 0; sd < 2; sd++) {
                const float o = sd == 0 ? -half : half;
                p[e][sd][0][0] = p[e][sd][0][1] = f.x0 + ux * t + nx * o;
                p[e][sd][2][0] = p[e][sd][2][1] = f.z0 + uz * t + nz * o;
                p[e][sd][1][0] = gy + h0;
                p[e][sd][1][1] = gy + h1;
            }
        }
        auto put = [&](int e0, int s0, int b0, int e1, int s1, int b1, int e2, int s2, int b2, int e3, int s3, int b3,
                       u8 cr, u8 cg, u8 cb) {
            HouseQuad q;
            const int idx[4][3] = {{e0, s0, b0}, {e1, s1, b1}, {e2, s2, b2}, {e3, s3, b3}};
            for (int k = 0; k < 4; k++) {
                q.p[k][0] = p[idx[k][0]][idx[k][1]][0][idx[k][2]];
                q.p[k][1] = p[idx[k][0]][idx[k][1]][1][idx[k][2]];
                q.p[k][2] = p[idx[k][0]][idx[k][1]][2][idx[k][2]];
            }
            q.r = cr;
            q.g = cg;
            q.b = cb;
            out.push_back(q);
        };
        put(0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, r, g, b);                // one side
        put(1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, r, g, b);                // the other
        put(0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, tr, tg, tb);             // the ends
        put(1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, tr, tg, tb);
        put(0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, tr, tg, tb);             // the top
    };
    if (f.kind == 1) {
        box(0.0f, len, -40.0f, BRICK_H, BRICK_HALF, 172, 88, 66, 150, 74, 56);
        box(-8.0f, len + 8.0f, BRICK_H, BRICK_H + 16.0f, BRICK_HALF + 8.0f, 208, 196, 176, 186, 176, 158);
    } else {
        const u8 wr = 150, wg = 108, wb = 66, dr = 122, dg = 86, db = 52;
        box(0.0f, 16.0f, -30.0f, FENCE_H + 10.0f, 10.0f, wr, wg, wb, dr, dg, db);
        box(len - 16.0f, len, -30.0f, FENCE_H + 10.0f, 10.0f, wr, wg, wb, dr, dg, db);
        box(0.0f, len, FENCE_H - 30.0f, FENCE_H - 8.0f, 6.0f, wr, wg, wb, dr, dg, db);
        box(0.0f, len, FENCE_H * 0.4f - 10.0f, FENCE_H * 0.4f + 12.0f, 6.0f, wr, wg, wb, dr, dg, db);
    }
}

// Stairs: treads and risers from the bottom to the top, and a stone side under each step down to the shelf.
void rampQuads(const Ramp& rp, std::vector<HouseQuad>& out) {
    const float dx = rp.tx - rp.bx, dz = rp.tz - rp.bz, len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        return;
    }
    const float ux = dx / len, uz = dz / len, nx = -uz * rp.half, nz = ux * rp.half;
    const int steps = std::max(4, (int)((rp.ty - rp.by) / 45.0f));
    auto put = [&](const float (&q)[4][3], u8 r, u8 g, u8 b) {
        HouseQuad h;
        memcpy(h.p, q, sizeof(q));
        h.r = r;
        h.g = g;
        h.b = b;
        out.push_back(h);
    };
    const float base = rp.by - 80.0f;
    for (int k = 0; k < steps; k++) {
        const float t0 = len * k / steps, t1 = len * (k + 1) / steps;
        const float y0 = rp.by + (rp.ty - rp.by) * k / steps, y1 = rp.by + (rp.ty - rp.by) * (k + 1) / steps;
        const float ax = rp.bx + ux * t0, az = rp.bz + uz * t0, bx = rp.bx + ux * t1, bz = rp.bz + uz * t1;
        // Riser at t0 from y0 to y1, tread from t0 to t1 at y1.
        put({{ax - nx, y0, az - nz}, {ax + nx, y0, az + nz}, {ax + nx, y1, az + nz}, {ax - nx, y1, az - nz}}, 150, 142, 128);
        put({{ax - nx, y1, az - nz}, {ax + nx, y1, az + nz}, {bx + nx, y1, bz + nz}, {bx - nx, y1, bz - nz}}, 200, 192, 176);
        // Sides.
        put({{ax - nx, base, az - nz}, {bx - nx, base, bz - nz}, {bx - nx, y1, bz - nz}, {ax - nx, y1, az - nz}}, 132, 126, 116);
        put({{ax + nx, base, az + nz}, {bx + nx, base, bz + nz}, {bx + nx, y1, bz + nz}, {ax + nx, y1, az + nz}}, 132, 126, 116);
    }
}

// A point of a mesh triangle with its e (gi: its sample's index in the room's grid, or -1 for a point made on an edge).
struct TP {
    float x, z, e;
    int gi;
};
// The point on p-q where e is t, worked out from the same end whichever way the edge is walked, so neighbouring
// triangles get the very same point.
TP crossE(const TP& p, const TP& q, float t) {
    const bool flip = q.x < p.x || (q.x == p.x && q.z < p.z);
    const TP& a = flip ? q : p;
    const TP& b = flip ? p : q;
    const float s = (t - a.e) / (b.e - a.e);
    return {a.x + (b.x - a.x) * s, a.z + (b.z - a.z) * s, t, -1};
}
// Keeps the part of a convex polygon with e >= t (above) or e <= t.
void clipE(std::vector<TP>& poly, float t, bool above) {
    std::vector<TP> out;
    const size_t n = poly.size();
    for (size_t a = 0; a < n; a++) {
        const TP& p = poly[a];
        const TP& q = poly[(a + 1) % n];
        const bool pin = above ? p.e >= t : p.e <= t, qin = above ? q.e >= t : q.e <= t;
        if (pin) {
            out.push_back(p);
        }
        if (pin != qin && p.e != q.e) {
            out.push_back(crossE(p, q, t));
        }
    }
    poly.swap(out);
}
// A triangle's ground on one tier (its points' heights are tierY(tier, e)).
struct Piece {
    int tier;
    std::vector<TP> v;
};
// A cliff: upright from y0 (the lower shelf) to y1 (the upper one) along p -> q, facing (q - p turned: (dz, -dx))
// the lower shelf.
struct Cliff {
    float px, pz, qx, qz, y0, y1;
};
// A mesh triangle cut where its e crosses into another tier: a piece per tier and a cliff on each crossing above
// tier 1 (the lowland rises smoothly into tier 1).
void splitTri(const TP (&t)[3], std::vector<Piece>& pieces, std::vector<Cliff>& cliffs) {
    const int k0 = tierOf(t[0].e), k1 = tierOf(t[1].e), k2 = tierOf(t[2].e);
    const int kmin = std::min(k0, std::min(k1, k2)), kmax = std::max(k0, std::max(k1, k2));
    if (kmin == kmax) {
        pieces.push_back({kmin, {t[0], t[1], t[2]}});
        return;
    }
    for (int k = kmin; k <= kmax; k++) {
        Piece pc{k, {t[0], t[1], t[2]}};
        if (k > kmin) {
            clipE(pc.v, tierStart(k), true);
        }
        if (k < kmax) {
            clipE(pc.v, tierStart(k + 1), false);
        }
        if (pc.v.size() >= 3) {
            pieces.push_back(pc);
        }
    }
    // e's slope over the triangle, to face each cliff downhill.
    const float dx1 = t[1].x - t[0].x, dz1 = t[1].z - t[0].z, de1 = t[1].e - t[0].e;
    const float dx2 = t[2].x - t[0].x, dz2 = t[2].z - t[0].z, de2 = t[2].e - t[0].e;
    const float det = dx1 * dz2 - dx2 * dz1;
    if (det == 0.0f) {
        return;
    }
    const float ga = (de1 * dz2 - de2 * dz1) / det, gb = (dx1 * de2 - dx2 * de1) / det;
    for (int k = std::max(kmin + 1, 2); k <= kmax; k++) {
        const float th = tierStart(k);
        TP pts[2];
        int np = 0;
        for (int a = 0; a < 3 && np < 2; a++) {
            const TP& p = t[a];
            const TP& q = t[(a + 1) % 3];
            if ((p.e < th) != (q.e < th)) {
                pts[np++] = crossE(p, q, th);
            }
        }
        if (np < 2) {
            continue;
        }
        Cliff c{pts[0].x, pts[0].z, pts[1].x, pts[1].z, tierY(k - 1, th), tierStart(k)};
        if ((c.qz - c.pz) * ga - (c.qx - c.px) * gb > 0.0f) {
            std::swap(c.px, c.qx);
            std::swap(c.pz, c.qz);
        }
        cliffs.push_back(c);
    }
}

// A generated room's land: the drawing, and with o_bgw its collision (allocated in the current heap, the room's).
// Without o_bgw only the drawing, from the host's heap: a far room's land, drawn while its own room isn't.
PcRplusTerrain* buildTerrain(int roomNo, dBgW** o_bgw) {
    const bool far = o_bgw == nullptr;
    const int cells = pc_rplus_cells();
    const int n = cells * SUB;          // quads per side
    const int nv = n + 1;               // samples per side
    const float cs = cellSize();
    const float step = cs / SUB;
    float x0, z0;
    worldOrigin(&x0, &z0);
    const int r = roomNo - 1;
    const int gx0 = (r % pc_rplus_squares()) * cells;   // the room's first cell
    const int gy0 = (r / pc_rplus_squares()) * cells;
    float rx, rz;
    roomCentre(roomNo, &rx, &rz);
    const float bx = rx - ROOM_UNITS * 0.5f, bz = rz - ROOM_UNITS * 0.5f;  // the room's north-west corner

    // Samples: e, and the ground's height there.
    float* es = new float[(size_t)nv * nv];
    float* ys = new float[(size_t)nv * nv];
    if (es == nullptr || ys == nullptr) {
        delete[] es;
        delete[] ys;
        return nullptr;
    }
    for (int j = 0; j < nv; j++) {
        for (int i = 0; i < nv; i++) {
            const float e = gridE(gx0 * SUB + i, gy0 * SUB + j);
            es[j * nv + i] = e;
            ys[j * nv + i] = tierY(tierOf(e), e);
        }
    }
    auto Y = [&](int i, int j) { return ys[j * nv + i]; };
    auto keep = [&](int i, int j) {
        return Y(i, j) > KEEP_Y || Y(i + 1, j) > KEEP_Y || Y(i, j + 1) > KEEP_Y || Y(i + 1, j + 1) > KEEP_Y;
    };
    auto tp = [&](int i, int j) { return TP{bx + i * step, bz + j * step, es[j * nv + i], j * nv + i}; };
    // A quad's two triangles (as the collision has them), each cut into its tiers' pieces and cliffs.
    std::vector<Piece> pieces;
    std::vector<Cliff> cliffs;
    auto quadPieces = [&](int i, int j, int tri) {
        pieces.clear();
        cliffs.clear();
        const TP a = tp(i, j), b = tp(i, j + 1), c = tp(i + 1, j), d = tp(i + 1, j + 1);
        const TP t0[3] = {a, b, c}, t1[3] = {c, b, d};
        splitTri(tri == 0 ? t0 : t1, pieces, cliffs);
    };

    PcRplusTerrain* t = far ? new (std::malloc(sizeof(PcRplusTerrain))) PcRplusTerrain() : new PcRplusTerrain();
    if (t == nullptr) {
        delete[] es;
        delete[] ys;
        return nullptr;
    }
    // ---- Drawing: triangles by sample row (the land and the shallows; the sea bed is not drawn).
    PcRplusPacket& pk = t->packet;
    std::vector<HouseQuad> house;
    for (const GenDoor& gd : genDoors()) {
        if (gd.seaRoom == roomNo) {
            houseQuads(gd.pos, gd.angle, gd.style, gd.variant, house);
        }
    }
    auto inRoom = [&](float x, float z) {
        return x >= bx && x < bx + n * step && z >= bz && z < bz + n * step;
    };
    for (const Fence& f : layoutFences()) {
        if (inRoom((f.x0 + f.x1) * 0.5f, (f.z0 + f.z1) * 0.5f)) {
            fenceQuads(f, house);
        }
    }
    for (const Deco& d : layoutDecos()) {
        if (d.seaRoom != roomNo) {
            continue;
        }
        houseQuads(d.pos, d.angle, 0, d.variant, house);
        // Its door, shut: a plank panel in the doorway.
        const float s = cM_ssin(d.angle), c = cM_scos(d.angle);
        auto pt = [&](float side, float up, float* o) {
            o[0] = d.pos.x + c * side - s * 4.0f;
            o[1] = d.pos.y + up;
            o[2] = d.pos.z - s * side - c * 4.0f;
        };
        HouseQuad q;
        pt(-DOORWAY_HALF, -20.0f, q.p[0]);
        pt(DOORWAY_HALF, -20.0f, q.p[1]);
        pt(DOORWAY_HALF, HUT_DOOR_TOP, q.p[2]);
        pt(-DOORWAY_HALF, HUT_DOOR_TOP, q.p[3]);
        q.r = 112;
        q.g = 72;
        q.b = 40;
        house.push_back(q);
    }
    for (const Ramp& rp : layoutRamps()) {
        if (inRoom((rp.bx + rp.tx) * 0.5f, (rp.bz + rp.tz) * 0.5f)) {
            rampQuads(rp, house);
        }
    }
    std::vector<Vtx> dv;
    dv.reserve((size_t)n * n + house.size() * 6);
    pk.mRows = n < 256 ? n : 256;
    const float lx = 0.42f, ly = 0.80f, lz = 0.42f;  // light direction (normalised enough)
    auto emitTri = [&](const float (&px)[3], const float (&py)[3], const float (&pz)[3]) {
        // Normal (v1 - v0) x (v2 - v0), up for this winding.
        float ax = px[1] - px[0], ay = py[1] - py[0], az = pz[1] - pz[0];
        float qx = px[2] - px[0], qy = py[2] - py[0], qz = pz[2] - pz[0];
        float nx = ay * qz - az * qy, ny = az * qx - ax * qz, nz = ax * qy - ay * qx;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0) {
            nx /= len; ny /= len; nz /= len;
        }
        float lit = nx * lx + ny * ly + nz * lz;
        float tone = lit > 0.55f ? 1.0f : 0.72f;  // two tones, as the game's toon light
        for (int k = 0; k < 3; k++) {
            const float gxw = (px[k] - x0) / cs, gyw = (pz[k] - z0) / cs;
            char b = biomeNear(gxw, gyw);
            Rgb col = biomeColour(b);
            if (py[k] < LAND_BASE + 60.0f) {
                col = biomeColour('d');
            } else if (ny < 0.75f && b != 's') {
                col = biomeColour('r');
            } else {
                // Patches: the grass (and the rest) lighter and darker in broad soft blotches.
                const float patch = vnoise(px[k] * 0.00035f, pz[k] * 0.00035f, 131) * 0.6f +
                                    vnoise(px[k] * 0.0012f, pz[k] * 0.0012f, 137) * 0.4f;
                const float m = 0.86f + patch * 0.26f;
                col.r = (u8)std::min(255.0f, col.r * m);
                col.g = (u8)std::min(255.0f, col.g * m);
                col.b = (u8)std::min(255.0f, col.b * m);
                const int mark = groundMark(px[k], pz[k]);
                if (mark == 1) {
                    col = {172, 136, 92};   // a dirt path
                } else if (mark == 2) {
                    col = {196, 188, 172};  // the plaza's paving
                }
            }
            float tk = tone * (py[k] < 0.0f ? 0.85f : 1.0f);
            dv.push_back({px[k], py[k], pz[k], (u8)(col.r * tk), (u8)(col.g * tk), (u8)(col.b * tk), 0xFF});
        }
    };
    auto emitCliff = [&](const Cliff& c) {
        // Rock, lit by which way it faces; a little lighter at the top, and in bands that differ cliff to cliff.
        const float dx = c.qx - c.px, dz = c.qz - c.pz, l = sqrtf(dx * dx + dz * dz);
        if (l <= 0.0f) {
            return 0;
        }
        const float lit = (dz / l) * lx + (-dx / l) * lz;
        const float tone = lit > 0.1f ? 0.95f : 0.7f;
        const float vary = 0.9f + 0.2f * vnoise(c.px * 0.0009f, c.pz * 0.0009f, 151);
        const Rgb rock = biomeColour('r');
        auto shade = [&](float k) { return Vtx{0, 0, 0, (u8)std::min(255.0f, rock.r * k), (u8)std::min(255.0f, rock.g * k),
                                               (u8)std::min(255.0f, rock.b * k), 0xFF}; };
        Vtx lo = shade(tone * vary * 0.82f), hi = shade(tone * vary * 1.04f);
        const float yb = c.y0 - 40.0f;
        Vtx v[4] = {lo, hi, hi, lo};
        const float pts[4][3] = {{c.px, yb, c.pz}, {c.px, c.y1, c.pz}, {c.qx, c.y1, c.qz}, {c.qx, yb, c.qz}};
        for (int k = 0; k < 4; k++) {
            v[k].x = pts[k][0];
            v[k].y = pts[k][1];
            v[k].z = pts[k][2];
        }
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            dv.push_back(v[k]);
        }
        return 2;
    };
    for (int j = 0; j < n && j < 256; j++) {
        for (int i = 0; i < n; i++) {
            if (!keep(i, j)) {
                continue;
            }
            for (int tri = 0; tri < 2; tri++) {
                quadPieces(i, j, tri);
                for (const Piece& pc : pieces) {
                    for (size_t a = 1; a + 1 < pc.v.size(); a++) {
                        const TP* q[3] = {&pc.v[0], &pc.v[a], &pc.v[a + 1]};
                        float px[3], py[3], pz[3];
                        for (int k = 0; k < 3; k++) {
                            px[k] = q[k]->x;
                            pz[k] = q[k]->z;
                            py[k] = tierY(pc.tier, q[k]->e);
                        }
                        emitTri(px, py, pz);
                        pk.mRowTris[j]++;
                    }
                }
                for (const Cliff& c : cliffs) {
                    pk.mRowTris[j] += emitCliff(c);
                }
            }
        }
    }
    pk.mTriCount = (int)(dv.size() / 3);
    // The house, after the land's rows.
    for (const HouseQuad& h : house) {
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            const float* p = h.p[order[k]];
            dv.push_back({p[0], p[1], p[2], h.r, h.g, h.b, 0xFF});
        }
    }
    if (!house.empty()) {
        pk.mRowTris[pk.mRows++] = (int)house.size() * 2;
        pk.mTriCount += (int)house.size() * 2;
    }
    pk.mVtx = far ? (Vtx*)std::malloc(sizeof(Vtx) * dv.size() + 1) : new Vtx[dv.size() + 1];
    if (pk.mVtx == nullptr) {
        pk.mTriCount = 0;
    } else if (!dv.empty()) {
        memcpy(pk.mVtx, dv.data(), sizeof(Vtx) * dv.size());
    }

    if (far) {
        delete[] es;
        delete[] ys;
        return t;
    }

    // ---- Collision: a DZB in the disc's layout, over the whole room: the land and the sea bed. Every room
    // needs ground somewhere under any point, or Link, made at a spawn point over open water, waits for ground
    // forever (daPy_lk_c::makeBgWait) and the game never starts.
    // Shore walls: where a dry quad (a corner above the sea) meets a wet one, an upright wall on their edge from
    // under the water to just above it, facing the sea. The beach's slope alone counts as ground, which the boat
    // doesn't stop at (it sailed straight through the land).
    const int nb = (n + BLK - 1) / BLK;  // blocks per side
    auto dry = [&](int i, int j) {
        return Y(i, j) > 0.0f || Y(i + 1, j) > 0.0f || Y(i, j + 1) > 0.0f || Y(i + 1, j + 1) > 0.0f;
    };
    struct Wall {
        int blk;
        float px, pz, qx, qz;  // the edge, ordered so the wall faces the sea (or out of the hut)
        float y0, y1;
        Ground g;
        float qy0 = NAN, qy1 = NAN;  // the q end's bottom and top, when not y0 and y1 (a fence on a slope)
    };
    // From the sea bed to above the boat's highest wall circle (d_a_ship.cpp: 0, 75 and 150 over it, and one at
    // y -600), so none passes under or over. Link passes through them (G_SHORE): he swims ashore anywhere.
    constexpr float WALL_BOTTOM = WATER_Y - 10.0f, WALL_TOP = 250.0f;
    std::vector<Wall> walls;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            if (!dry(i, j)) {
                continue;
            }
            const float x0e = bx + i * step, x1e = bx + (i + 1) * step;
            const float z0e = bz + j * step, z1e = bz + (j + 1) * step;
            const int blk = (j / BLK) * nb + (i / BLK);
            // (p, q) with the sea on the left of p -> q seen from above, x east and z south: see the normal below.
            if (j > 0 && !dry(i, j - 1)) walls.push_back({blk, x0e, z0e, x1e, z0e, WALL_BOTTOM, WALL_TOP, G_SHORE});  // sea to the north
            if (j < n - 1 && !dry(i, j + 1)) walls.push_back({blk, x1e, z1e, x0e, z1e, WALL_BOTTOM, WALL_TOP, G_SHORE});  // south
            if (i > 0 && !dry(i - 1, j)) walls.push_back({blk, x0e, z1e, x0e, z0e, WALL_BOTTOM, WALL_TOP, G_SHORE});  // west
            if (i < n - 1 && !dry(i + 1, j)) walls.push_back({blk, x1e, z0e, x1e, z1e, WALL_BOTTOM, WALL_TOP, G_SHORE});  // east
        }
    }
    // Floors: Link's home's exit floor, just behind its door (the door opens onto it).
    struct Floor {
        int blk;
        float x0, z0, x1, z1, y;
    };
    std::vector<Floor> floors;
    std::vector<int> floorExit;  // each floor's exit id (its poly info is G_COUNT + its number)
    for (const GenDoor& gd : genDoors()) {
        if (gd.seaRoom != roomNo) {
            continue;
        }
        const cXyz& door = gd.pos;
        const s16 ang = gd.angle;
        {
            // A hut behind the door (collision only for now): its front in the door's plane with a doorway the
            // door fills, its sides and back facing out, so Link only reaches the door from the front (from
            // behind it plays the door's other event, which never finished). Inside, the exit floor: the door's
            // event leaves Link about 80 past the door.
            const float s = cM_ssin(ang), c = cM_scos(ang);  // out of the door's front
            auto at = [&](float side, float ahead, float* x, float* z) {
                *x = door.x + c * side + s * ahead;
                *z = door.z - s * side + c * ahead;
            };
            auto blkOf = [&](float x, float z) {
                int qi = (int)((x - bx) / step), qj = (int)((z - bz) / step);
                qi = qi < 0 ? 0 : qi >= n ? n - 1 : qi;
                qj = qj < 0 ? 0 : qj >= n ? n - 1 : qj;
                return (qj / BLK) * nb + (qi / BLK);
            };
            const float y0 = door.y - 200.0f, y1 = door.y + 400.0f;
            // Corners going round the outside, each edge p -> q with "out" on its left seen from above.
            const float ring[][2] = {{-DOORWAY_HALF, 0.0f}, {-HUT_HALF, 0.0f}, {-HUT_HALF, -HUT_DEPTH},
                                     {HUT_HALF, -HUT_DEPTH}, {HUT_HALF, 0.0f}, {DOORWAY_HALF, 0.0f}};
            for (int k = 0; k + 1 < 6; k++) {
                float px, pz, qx, qz;
                at(ring[k][0], ring[k][1], &px, &pz);
                at(ring[k + 1][0], ring[k + 1][1], &qx, &qz);
                walls.push_back({blkOf((px + qx) * 0.5f, (pz + qz) * 0.5f), px, pz, qx, qz, y0, y1, G_STONE});
            }
            float fx, fz;
            at(0.0f, -(HUT_DEPTH * 0.5f + 10.0f), &fx, &fz);
            const float hw = HUT_HALF - 10.0f, hd = HUT_DEPTH * 0.5f - 20.0f;  // half extents across, along
            const float ex = fabsf(c) * hw + fabsf(s) * hd, ez = fabsf(s) * hw + fabsf(c) * hd;
            // Above the land everywhere inside (it rises behind the door on a slope), or Link's ground is the
            // land under it and its exit never counts.
            float wx0, wz0;
            worldOrigin(&wx0, &wz0);
            const float wcs = cellSize();
            float top = door.y;
            for (int a = 0; a <= 8; a++) {
                for (int b = 0; b <= 8; b++) {
                    const float x = fx - ex + 2.0f * ex * a / 8.0f, z = fz - ez + 2.0f * ez * b / 8.0f;
                    top = std::max(top, heightAt((x - wx0) / wcs, (z - wz0) / wcs));
                }
            }
            floors.push_back({blkOf(fx, fz), fx - ex, fz - ez, fx + ex, fz + ez, top + 3.0f});
            floorExit.push_back(gd.exitId);
        }
    }
    // Towns' houses without a way in: their walls all round.
    for (const Deco& d : layoutDecos()) {
        if (d.seaRoom != roomNo) {
            continue;
        }
        const float s = cM_ssin(d.angle), c = cM_scos(d.angle);
        const float ring[][2] = {{-DOORWAY_HALF, 0.0f}, {-HUT_HALF, 0.0f}, {-HUT_HALF, -HUT_DEPTH},
                                 {HUT_HALF, -HUT_DEPTH}, {HUT_HALF, 0.0f},   {DOORWAY_HALF, 0.0f},
                                 {-DOORWAY_HALF, 0.0f}};
        for (int k = 0; k + 1 < 7; k++) {
            const float px = d.pos.x + c * ring[k][0] + s * ring[k][1], pz = d.pos.z - s * ring[k][0] + c * ring[k][1];
            const float qx = d.pos.x + c * ring[k + 1][0] + s * ring[k + 1][1],
                        qz = d.pos.z - s * ring[k + 1][0] + c * ring[k + 1][1];
            const float mx = (px + qx) * 0.5f, mz = (pz + qz) * 0.5f;
            int qi = (int)((mx - bx) / step), qj = (int)((mz - bz) / step);
            qi = qi < 0 ? 0 : qi >= n ? n - 1 : qi;
            qj = qj < 0 ? 0 : qj >= n ? n - 1 : qj;
            walls.push_back({(qj / BLK) * nb + (qi / BLK), px, pz, qx, qz, d.pos.y - 200.0f, d.pos.y + 400.0f, G_STONE});
        }
    }
    // Fences and low walls: upright both ways (Link stops on either side), from under the ground to their top.
    for (const Fence& f : layoutFences()) {
        const float mx = (f.x0 + f.x1) * 0.5f, mz = (f.z0 + f.z1) * 0.5f;
        if (mx < bx || mx >= bx + n * step || mz < bz || mz >= bz + n * step) {
            continue;
        }
        int qi = (int)((mx - bx) / step), qj = (int)((mz - bz) / step);
        qi = qi < 0 ? 0 : qi >= n ? n - 1 : qi;
        qj = qj < 0 ? 0 : qj >= n ? n - 1 : qj;
        const int blk = (qj / BLK) * nb + (qi / BLK);
        const float ya = heightAt_world(f.x0, f.z0), yb = heightAt_world(f.x1, f.z1);
        const float top = f.kind == 1 ? BRICK_H + 16.0f : FENCE_H;
        Wall w1{blk, f.x0, f.z0, f.x1, f.z1, ya - 60.0f, ya + top, G_STONE};
        w1.qy0 = yb - 60.0f;
        w1.qy1 = yb + top;
        Wall w2{blk, f.x1, f.z1, f.x0, f.z0, yb - 60.0f, yb + top, G_STONE};
        w2.qy0 = ya - 60.0f;
        w2.qy1 = ya + top;
        walls.push_back(w1);
        walls.push_back(w2);
    }
    // Cliffs: facing the lower shelf (Link walks off the top), from under the lower shelf to the upper one's edge;
    // left open where stairs come up.
    auto blkAt = [&](float x, float z) {
        int qi = (int)((x - bx) / step), qj = (int)((z - bz) / step);
        qi = qi < 0 ? 0 : qi >= n ? n - 1 : qi;
        qj = qj < 0 ? 0 : qj >= n ? n - 1 : qj;
        return (qj / BLK) * nb + (qi / BLK);
    };
    auto addCliffWall = [&](float px, float pz, float qx, float qz, float y0, float y1) {
        walls.push_back({blkAt((px + qx) * 0.5f, (pz + qz) * 0.5f), px, pz, qx, qz, y0 - 30.0f, y1, G_STONE});
    };
    // Collision vertices (x y z): every sample first (the ground's triangles use the sample grid's indices), then
    // the points cut ones need, the walls' and the floors'.
    std::vector<float> cv;
    cv.reserve((size_t)nv * nv * 3 + 4096);
    for (int j = 0; j < nv; j++) {
        for (int i = 0; i < nv; i++) {
            cv.push_back(bx + i * step);
            cv.push_back(Y(i, j));
            cv.push_back(bz + j * step);
        }
    }
    auto addV = [&](float x, float y, float z) {
        cv.push_back(x);
        cv.push_back(y);
        cv.push_back(z);
        return (u32)(cv.size() / 3 - 1);
    };
    struct CTri {
        u32 a, b, c;
        u16 g;
        int blk;
    };
    std::vector<CTri> ctris;
    ctris.reserve((size_t)n * n * 2 + 1024);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const int qb = (j / BLK) * nb + (i / BLK);
            for (int tri = 0; tri < 2; tri++) {
                quadPieces(i, j, tri);
                for (const Piece& pc : pieces) {
                    const float yA = tierY(pc.tier, pc.v[0].e);
                    float ymid = 0.0f, cxm = 0.0f, czm = 0.0f;
                    for (const TP& v : pc.v) {
                        ymid += tierY(pc.tier, v.e) / pc.v.size();
                        cxm += v.x / pc.v.size();
                        czm += v.z / pc.v.size();
                    }
                    // How flat: the piece's rise over a sample step.
                    float rise = 0.0f;
                    for (const TP& v : pc.v) {
                        rise = std::max(rise, fabsf(tierY(pc.tier, v.e) - yA));
                    }
                    const float ny = step / sqrtf(step * step + rise * rise);
                    Ground g = groundOf(biomeNear((cxm - x0) / cs, (czm - z0) / cs), ymid, ny);
                    if (ymid > LAND_BASE + 60.0f) {
                        const int mark = groundMark(cxm, czm);
                        g = mark == 1 ? G_DIRT : mark == 2 ? G_STONE : g;
                    }
                    std::vector<u32> idx;
                    for (const TP& v : pc.v) {
                        idx.push_back(v.gi >= 0 ? (u32)v.gi : addV(v.x, tierY(pc.tier, v.e), v.z));
                    }
                    for (size_t k = 1; k + 1 < idx.size(); k++) {
                        ctris.push_back({idx[0], idx[k], idx[k + 1], (u16)g, qb});
                    }
                }
                for (const Cliff& c : cliffs) {
                    // Open where stairs reach the top: the part of the edge within the stairs' half (and some) of
                    // their top left out.
                    float t0 = 0.0f, t1 = 1.0f;
                    bool cut = false;
                    float ca = 1.0f, cb = 0.0f;
                    const float ex = c.qx - c.px, ez = c.qz - c.pz, el2 = ex * ex + ez * ez;
                    for (const Ramp& rp : layoutRamps()) {
                        const float r = rp.half + 120.0f;
                        const float fx = c.px - rp.tx, fz = c.pz - rp.tz;
                        // |p + t e - top|^2 = r^2
                        const float qa = el2, qb2 = 2.0f * (fx * ex + fz * ez), qc = fx * fx + fz * fz - r * r;
                        const float disc = qb2 * qb2 - 4.0f * qa * qc;
                        if (qa <= 0.0f || disc <= 0.0f) {
                            continue;
                        }
                        const float sq = sqrtf(disc);
                        const float lo = (-qb2 - sq) / (2.0f * qa), hi = (-qb2 + sq) / (2.0f * qa);
                        if (hi <= 0.0f || lo >= 1.0f) {
                            continue;
                        }
                        cut = true;
                        ca = std::min(ca, lo);
                        cb = std::max(cb, hi);
                    }
                    if (!cut) {
                        addCliffWall(c.px, c.pz, c.qx, c.qz, c.y0, c.y1);
                        continue;
                    }
                    if (ca > t0) {
                        addCliffWall(c.px, c.pz, c.px + ex * ca, c.pz + ez * ca, c.y0, c.y1);
                    }
                    if (cb < t1) {
                        addCliffWall(c.px + ex * cb, c.pz + ez * cb, c.qx, c.qz, c.y0, c.y1);
                    }
                }
            }
        }
    }
    // Walls (shore, huts, fences, cliffs): (p bottom, p top, q bottom) and (q bottom, p top, q top). The normal
    // (v1 - v0) x (v2 - v0) = up x (q - p), which points to the left of p -> q: the sea.
    for (const Wall& wl : walls) {
        const float qy0 = std::isnan(wl.qy0) ? wl.y0 : wl.qy0, qy1 = std::isnan(wl.qy1) ? wl.y1 : wl.qy1;
        const u32 v = addV(wl.px, wl.y0, wl.pz);
        addV(wl.px, wl.y1, wl.pz);
        addV(wl.qx, qy0, wl.qz);
        addV(wl.qx, qy1, wl.qz);
        ctris.push_back({v, v + 1, v + 2, (u16)wl.g, wl.blk});
        ctris.push_back({v + 2, v + 1, v + 3, (u16)wl.g, wl.blk});
    }
    // Floors, facing up: (x0 z0, x0 z1, x1 z0) and (x1 z0, x0 z1, x1 z1).
    for (size_t f = 0; f < floors.size(); f++) {
        const Floor& fl = floors[f];
        const u32 v = addV(fl.x0, fl.y, fl.z0);
        addV(fl.x0, fl.y, fl.z1);
        addV(fl.x1, fl.y, fl.z0);
        addV(fl.x1, fl.y, fl.z1);
        ctris.push_back({v, v + 1, v + 2, (u16)(G_COUNT + f), fl.blk});
        ctris.push_back({v + 2, v + 1, v + 3, (u16)(G_COUNT + f), fl.blk});
    }
    // Stairs: a slope from their bottom to their top (the steps are only drawn), facing up.
    for (const Ramp& rp : layoutRamps()) {
        const float mx = (rp.bx + rp.tx) * 0.5f, mz = (rp.bz + rp.tz) * 0.5f;
        if (mx < bx || mx >= bx + n * step || mz < bz || mz >= bz + n * step) {
            continue;
        }
        const float dx = rp.tx - rp.bx, dz = rp.tz - rp.bz, len = sqrtf(dx * dx + dz * dz);
        if (len < 1.0f) {
            continue;
        }
        const float nx = -dz / len * rp.half, nz = dx / len * rp.half;
        const u32 v = addV(rp.bx - nx, rp.by, rp.bz - nz);
        addV(rp.bx + nx, rp.by, rp.bz + nz);
        addV(rp.tx - nx, rp.ty, rp.tz - nz);
        addV(rp.tx + nx, rp.ty, rp.tz + nz);
        // Up for (v0, v1, v2) when (v1 - v0) x (v2 - v0) has y > 0: (2n) x (len u) has y = 2 (nz ux - nx uz) * len.
        const bool up = (nz * dx - nx * dz) > 0.0f;
        const int blkR = blkAt(mx, mz);
        if (up) {
            ctris.push_back({v, v + 1, v + 2, (u16)G_STONE, blkR});
            ctris.push_back({v + 2, v + 1, v + 3, (u16)G_STONE, blkR});
        } else {
            ctris.push_back({v, v + 2, v + 1, (u16)G_STONE, blkR});
            ctris.push_back({v + 2, v + 3, v + 1, (u16)G_STONE, blkR});
        }
    }
    std::stable_sort(ctris.begin(), ctris.end(), [](const CTri& p, const CTri& q) { return p.blk < q.blk; });
    const u32 nVtx = (u32)(cv.size() / 3);
    const u32 nTri = (u32)ctris.size();
    u32 nBlk = 0;
    for (size_t k = 0; k < ctris.size(); k++) {
        nBlk += (k == 0 || ctris[k].blk != ctris[k - 1].blk) ? 1 : 0;
    }
    // Tree: the leaves (one per block), then 8-way parents up to one root.
    u32 nTree = nBlk;
    for (u32 level = nBlk; level > 1;) {
        level = (level + 7) / 8;
        nTree += level;
    }
    if (nVtx > 0xFFFF || nTri > 0xFFFF || nTree > 0xFFFF) {
        fprintf(stderr, "[rplus] room %d: too many collision polygons (%u, %u points)\n", roomNo, nTri, nVtx);
        delete[] es;
        delete[] ys;
        return t;
    }

    const u32 offV = 0x34;
    const u32 offT = align4(offV + nVtx * 12);
    const u32 offB = align4(offT + nTri * 10);
    const u32 offTree = align4(offB + nBlk * 2);
    const u32 offG = align4(offTree + nTree * 0x14);
    const u32 offTi = align4(offG + 0x34);
    const u32 nInfo = G_COUNT + (u32)floors.size();
    const u32 offName = offTi + nInfo * 0x10;
    const u32 size = align4(offName + 8);
    u8* blob = new u8[size];
    memset(blob, 0, size);
    BeOut o{blob};
    o.u32at(0x00, nVtx);
    o.u32at(0x04, offV);
    o.u32at(0x08, nTri);
    o.u32at(0x0C, offT);
    o.u32at(0x10, nBlk);
    o.u32at(0x14, offB);
    o.u32at(0x18, nTree);
    o.u32at(0x1C, offTree);
    o.u32at(0x20, 1);
    o.u32at(0x24, offG);
    o.u32at(0x28, nInfo);
    o.u32at(0x2C, offTi);
    o.u32at(0x30, 0);
    for (u32 v = 0; v < nVtx; v++) {
        const u32 at = offV + v * 12;
        o.f32at(at, cv[v * 3]);
        o.f32at(at + 4, cv[v * 3 + 1]);
        o.f32at(at + 8, cv[v * 3 + 2]);
    }
    u32 blk = 0;
    for (u32 tri = 0; tri < nTri; tri++) {
        const CTri& ct = ctris[tri];
        if (tri == 0 || ct.blk != ctris[tri - 1].blk) {
            o.u16at(offB + blk * 2, (u16)tri);
            blk++;
        }
        const u32 at = offT + tri * 10;
        o.u16at(at, (u16)ct.a);
        o.u16at(at + 2, (u16)ct.b);
        o.u16at(at + 4, (u16)ct.c);
        o.u16at(at + 6, ct.g);
        o.u16at(at + 8, 0);
    }
    // Tree nodes: leaves first, then each level's parents; the root is the last node.
    {
        u32 node = 0;
        for (u32 b = 0; b < nBlk; b++, node++) {
            u32 at = offTree + node * 0x14;
            o.u16at(at, 1);  // leaf
            o.u16at(at + 2, 0xFFFF);
            for (int k = 0; k < 8; k++) {
                o.u16at(at + 4 + k * 2, 0xFFFF);
            }
            o.u16at(at + 4, (u16)b);
        }
        u32 levelStart = 0, levelCount = nBlk;
        while (levelCount > 1) {
            u32 parents = (levelCount + 7) / 8;
            for (u32 p = 0; p < parents; p++, node++) {
                u32 at = offTree + node * 0x14;
                o.u16at(at, 0);  // branch
                o.u16at(at + 2, 0xFFFF);
                for (int k = 0; k < 8; k++) {
                    u32 child = p * 8 + k;
                    if (child < levelCount) {
                        o.u16at(at + 4 + k * 2, (u16)(levelStart + child));
                        o.u16at(offTree + (levelStart + child) * 0x14 + 2, (u16)node);
                    } else {
                        o.u16at(at + 4 + k * 2, 0xFFFF);
                    }
                }
            }
            levelStart += levelCount;
            levelCount = parents;
        }
    }
    // The group: the whole room.
    o.u32at(offG + 0x00, offName);
    o.f32at(offG + 0x04, 1.0f);
    o.f32at(offG + 0x08, 1.0f);
    o.f32at(offG + 0x0C, 1.0f);
    o.u16at(offG + 0x24, 0xFFFF);  // parent
    o.u16at(offG + 0x26, 0xFFFF);  // next sibling
    o.u16at(offG + 0x28, 0xFFFF);  // first child
    o.u16at(offG + 0x2A, (u16)roomNo);
    o.u16at(offG + 0x2C, 0);
    o.u16at(offG + 0x2E, (u16)(nTree - 1));
    o.u32at(offG + 0x30, (u32)roomNo & 0xFF);  // room info id (dBgS::GetGrpRoomInfId)
    // Poly infos: no camera, exit, link or path; the ground kind's attribute.
    for (u32 i = 0; i < nInfo; i++) {
        u32 at = offTi + i * 0x10;
        const int g = i < G_COUNT ? (int)i : G_EXIT;
        const u32 exitId = i < G_COUNT ? 0x3Fu : (u32)floorExit[i - G_COUNT];
        o.u32at(at + 0x0, (0x07FFE0FFu & ~(0x3Fu << 13)) | (exitId << 13));
        o.u32at(at + 0x4, 0x000000FFu | (kGroundAttr[g] << 16));
        o.u32at(at + 0x8, 0xFFFFFFFFu);
        o.u32at(at + 0xC, kGroundPass[g]);
    }
    memcpy(blob + offName, "rplus", 6);
    delete[] es;
    delete[] ys;

    cBgS::ConvDzb(blob);
    dBgW* bgw = new dBgW();
    if (bgw != nullptr && !bgw->Set((cBgD_t*)blob, cBgW::GLOBAL_e, NULL)) {
        *o_bgw = bgw;
    } else {
        fprintf(stderr, "[rplus] room %d: collision not set\n", roomNo);
    }
    t->dzb = blob;
    fprintf(stderr, "[rplus] room %d: %d triangles drawn, %u for collision (%zu walls) in %u blocks\n", roomNo,
            pk.mTriCount, nTri, walls.size(), nBlk);
    return t;
}

} // namespace

PcRplusTerrain* pc_rplus_terrain_create(int roomNo, dBgW** o_bgw) {
    *o_bgw = nullptr;
    if (strcmp(dComIfGp_getStartStageName(), "sea") != 0 || !pc_rplus_sea_room_generated(roomNo)) {
        return nullptr;
    }
    return buildTerrain(roomNo, o_bgw);
}

void pc_rplus_far_draw(int roomNo) {
    // Once a frame, from the sea's draw: the land of every generated room not drawn by its own room (the original
    // sea shows far islands with LOD models, which a generated world has none of).
    (void)roomNo;
    static u32 sDrawnAt = ~0u;
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0 || sDrawnAt == g_Counter.mCounter0) {
        return;
    }
    sDrawnAt = g_Counter.mCounter0;
    static PcRplusTerrain* sFar[64] = {};
    const int rooms = pc_rplus_squares() * pc_rplus_squares();
    for (int r = 1; r <= rooms && r < 64; r++) {
        if (!pc_rplus_sea_room_generated(r) ||
            (dComIfGp_roomControl_checkStatusFlag(r, 0x01) && dComIfGp_roomControl_checkRoomDisp(r))) {
            continue;
        }
        if (sFar[r] == nullptr) {
            sFar[r] = buildTerrain(r, nullptr);
        }
        pc_rplus_terrain_draw(sFar[r]);
    }
}

void pc_rplus_terrain_draw(PcRplusTerrain* t) {
    if (t == nullptr || t->packet.mTriCount == 0) {
        return;
    }
    j3dSys.getDrawBuffer(J3DSysDrawBuf_Opa)->entryImm(&t->packet, 0);
}

int pc_rplus_room_actors_off(int roomNo) {
    return strcmp(dComIfGp_getStartStageName(), "sea") == 0 && pc_rplus_sea_room_generated(roomNo);
}

namespace {

// Asks the room control to load room roomNo and its RTBL neighbours (one room a call, as dStage_RoomCheck does
// every frame from the ground under Link).
void requestRoomLoad(int roomNo) {
    roomRead_class* rr = dComIfGp_getStageRoom();
    if (rr == nullptr) {
        return;
    }
    int entry = -1;
    for (int i = 0; i < rr->num && entry != roomNo; i++) {
        roomRead_data_class* d = rr->m_entries[i];
        u8* rooms = d->m_rooms;
        for (int k = 0; k < d->num; k++) {
            if (dStage_roomRead_dt_c_GetLoadRoomIndex(rooms[k]) == roomNo) {
                entry = i;  // an entry with the room; the room's own entry (index roomNo) wins
                break;
            }
        }
    }
    static int logged = -1;
    if (logged != roomNo) {
        logged = roomNo;
        fprintf(stderr, "[rplus] loading room %d under Link (room table entry %d)\n", roomNo, entry);
    }
    if (entry < 0) {
        return;
    }
    roomRead_data_class* d = rr->m_entries[entry];
    int ok = dComIfGp_roomControl_loadRoom(d->num, d->m_rooms);
    static int tries = 0, lastOk = -1, lastRoom = -1;
    if (++tries <= 5 || ok != lastOk || roomNo != lastRoom) {
        lastOk = ok;
        lastRoom = roomNo;
        fprintf(stderr, "[rplus] room load for room %d (entry %d, %d rooms): %s (try %d)\n", roomNo, entry, d->num,
                ok ? "asked" : "busy", tries);
    }
}

} // namespace

int pc_rplus_wave_max(int roomNo, int wave) {
    if (wave == 0 && pc_rplus_active() && strcmp(dComIfGp_getStartStageName(), "sea") == 0 &&
        pc_rplus_sea_room_generated(roomNo)) {
        return 30;
    }
    return wave;
}

int pc_rplus_room_check(void) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return -1;
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
        return -1;
    }
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float sx = (player->current.pos.x - x0) / ROOM_UNITS, sz = (player->current.pos.z - z0) / ROOM_UNITS;
    const int n = pc_rplus_squares();
    if (sx < 0.0f || sz < 0.0f || sx >= n || sz >= n) {
        return -1;
    }
    const int room = (int)sz * n + (int)sx + 1;
    if (!pc_rplus_sea_room_generated(room)) {
        return -1;  // a kept island (the original room's own ground and room table)
    }
    if (room != dComIfGp_roomControl_getStayNo()) {
        static int logged = -1;
        if (logged != room) {
            logged = room;
            fprintf(stderr, "[rplus] Link sails into room %d\n", room);
        }
        dComIfGp_roomControl_zoneCountCheck(room);
    }
    requestRoomLoad(room);
    return 1;
}

int pc_rplus_keep_room_actor(const char* name) {
    // The King of Red Lions (Link starting at sea waits for him, daPy_lk_c::makeBgWait) and the sea itself
    // (d_a_sea.cpp: the water, its waves and swimming are its l_cloth).
    if (strcmp(name, "Ship") == 0 || strcmp(name, "sea") == 0) {
        fprintf(stderr, "[rplus] kept the room's actor %s\n", name);
        return 1;
    }
    return 0;
}

int pc_rplus_any_point(const char* stage, int point) {
    if (!pc_rplus_active()) {
        return 0;
    }
    fprintf(stderr, "[rplus] %s has no spawn point %d here: Link starts at its first one\n", stage, point);
    return 1;
}

float pc_rplus_prop_far(int kind) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return 0.0f;
    }
    static const float kFar[3] = {7000.0f, 7000.0f, 16000.0f};
    return kind >= 0 && kind < 3 ? kFar[kind] : 0.0f;
}

int pc_rplus_lod_off(void) {
    return pc_rplus_active();
}

void pc_rplus_spawn_lift(cXyz* pos) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return;
    }
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    const float gx = (pos->x - x0) / cs, gy = (pos->z - z0) / cs;
    const int n = pc_rplus_squares() * pc_rplus_cells();
    if (gx < 0 || gy < 0 || gx >= n || gy >= n) {
        return;
    }
    const int room = ((int)gy / pc_rplus_cells()) * pc_rplus_squares() + ((int)gx / pc_rplus_cells()) + 1;
    if (!pc_rplus_sea_room_generated(room)) {
        return;
    }
    // Rooms load from the ground under Link (dStage_RoomCheck), and he has none until his room is in: asked for
    // here every frame he waits (the start cell's room is often not the room the stage loaded first).
    if (!dComIfGp_roomControl_checkStatusFlag(room, 0x01)) {
        requestRoomLoad(room);
    }
    const float ground = heightAt(gx, gy);
    if (pos->y < ground + 10.0f) {
        fprintf(stderr, "[rplus] spawn point %.0f %.0f %.0f is under the land: lifted to %.0f\n", pos->x, pos->y,
                pos->z, ground + 10.0f);
        pos->y = ground + 10.0f;
    }
}

namespace {
bool sStartPointSet = false;  // pc_rplus_start_point moved Link's spawn (pc_rplus_place_player has nothing to do)
} // namespace

namespace {

// An arrival at sea that the generated world moved: coming out of a moved door (pc_rplus_exit_return).
struct Arrival {
    bool on = false;
    int room = 0;
    cXyz pos;
    s16 angleY = 0;
} sArrival;

} // namespace

int pc_rplus_start_room(void) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0 ||
        dComIfGp_getStartStagePoint() < 0) {
        return -1;
    }
    if (sArrival.on) {
        return sArrival.room;
    }
    if (sStartPointSet) {
        return -1;
    }
    float sx, sz;
    return startSpot(&sx, &sz);
}

void pc_rplus_start_point(u32* params, cXyz* pos, s16* angleY) {
    const int room = pc_rplus_start_room();
    if (room < 0) {
        return;
    }
    if (sArrival.on) {
        sArrival.on = false;
        *pos = sArrival.pos;
        *angleY = sArrival.angleY;
        fprintf(stderr, "[rplus] Link comes out at %.0f %.0f %.0f in room %d\n", pos->x, pos->y, pos->z, room);
    } else {
        sStartPointSet = true;
        float sx, sz;
        startSpot(&sx, &sz);
        pos->set(sx, heightAt_world(sx, sz) + 10.0f, sz);
        fprintf(stderr, "[rplus] Link starts in room %d at %.0f %.0f %.0f\n", room, pos->x, pos->y, pos->z);
    }
    // The room bits pick the room the stage loads first (dStage_Create), so the room's land and collision are in
    // before Link looks for ground (daPy_lk_c::makeBgWait). Standing start (mode 0), not on a raft (0x80).
    *params = (*params & ~0xF0BFu) | (u32)room;
}

// ---- Link's home: the Private Oasis cabana's interior (Abesso), its door on the start island ----------------

namespace {

constexpr float HOME_DOOR_AHEAD = 900.0f;  // from the start cell's centre, north
constexpr int CABANA_ROOM = 33, CABANA_OUT_POINT = 1;  // Abesso's way out: sea, point 1, room 33

// The interiors a generated door can open to, and where Link comes in (from the disc's stages' first exits in).
struct Interior {
    const char* stage;
    s8 spawn, room;
    int style;
    const char* also;  // another stage whose way out to the sea comes back to this door
};
const Interior kInteriors[] = {
    {"LinkRM", 1, 0, 0, nullptr},  {"Ojhous", 0, 0, 0, nullptr},  {"Omasao", 0, 0, 0, nullptr},
    {"Onobuta", 0, 0, 0, nullptr}, {"Kaisen", 0, 0, 0, nullptr},  {"Nitiyou", 0, 0, 0, nullptr},
    {"Obombh", 0, 0, 0, nullptr},  {"Ocmera", 0, 0, 0, nullptr},  {"Opub", 0, 0, 0, nullptr},
    {"Orichh", 0, 0, 0, nullptr},  {"Pdrgsh", 0, 0, 0, nullptr},  {"Pnezumi", 0, 0, 0, nullptr},
    {"A_mori", 0, 0, 0, nullptr},  {"Pjavdou", 0, 0, 1, nullptr}, {"Atorizk", 0, 0, 1, nullptr},
    {"Adanmae", 0, 0, 1, nullptr}, {"Edaichi", 0, 0, 1, nullptr}, {"Ekaze", 0, 0, 1, nullptr},
    {"MiniKaz", 0, 0, 1, nullptr}, {"MiniHyo", 0, 0, 1, nullptr}, {"Omori", 1, 0, 0, nullptr},
    {"kindan", 0, 0, 1, nullptr},  {"ShipD", 0, 0, 1, nullptr},   {"Fairy01", 0, 0, 2, nullptr},
    {"Fairy02", 0, 0, 2, nullptr}, {"Fairy03", 0, 0, 2, nullptr}, {"Fairy05", 0, 0, 2, nullptr},
    {"Fairy06", 0, 0, 2, nullptr}, {"Cave01", 0, 0, 1, nullptr},  {"Cave02", 0, 0, 1, nullptr},
    {"Cave03", 0, 0, 1, nullptr},  {"Cave04", 0, 0, 1, nullptr},  {"Cave05", 0, 0, 1, nullptr},
    {"Cave07", 0, 0, 1, nullptr},  {"Cave09", 0, 0, 1, nullptr},  {"ITest63", 0, 0, 1, nullptr},
    {"SubD42", 0, 0, 1, nullptr},  {"SubD43", 0, 0, 1, nullptr},  {"SubD71", 0, 0, 1, nullptr},
    {"TF_01", 0, 0, 1, nullptr},   {"TF_02", 0, 0, 1, nullptr},   {"TF_03", 0, 0, 1, nullptr},
    {"TF_06", 0, 0, 1, nullptr},   {"TyuTyu", 0, 0, 1, nullptr},  {"WarpD", 0, 0, 1, nullptr},
};

constexpr float LINK_HOUSE_EAST = 1400.0f;  // the start cell's own house (Outset's), beside Link's home
constexpr float DOOR_GAP = 1000.0f;         // doors closer than this to one already placed are left out
constexpr int DOORS_PER_ROOM = HOME_EXIT - DOOR_EXIT0;
constexpr float TOWN_LINK = 4.5f;           // houses fewer cells apart than this gather into one town
constexpr float PLAZA_R = 750.0f;           // a town's paved middle
constexpr float PATH_HALF = 230.0f;         // half a path's width

// Where the world builds: its doors, its towns (houses round a plaza, the ground levelled), the paths between
// them, and the fences and low walls along them. Made once from the anchors (again at sea if first made
// elsewhere, where the room positions are the stage's).
struct Layout {
    float x0 = 0, z0 = 0, cs = 1;  // the world's corner and cell size
    float startX = 0, startZ = 0;  // where Link starts: the start town's plaza, or land by the start cell
    std::vector<GenDoor> doors;
    std::vector<Town> towns;
    std::vector<Pad> pads;
    std::vector<Seg> paths;
    std::vector<Fence> fences;
    std::vector<Deco> decos;
    std::vector<Ramp> ramps;
};

float natAt(const Layout& L, float x, float z) {
    return naturalY((x - L.x0) / L.cs, (z - L.z0) / L.cs);
}

// e as the mesh has it (with the levelled ground: only once the layout's pads are all in), and its tier.
float meshEAt(const Layout& L, float x, float z) {
    return meshE((x - L.x0) / L.cs, (z - L.z0) / L.cs);
}
int tierAt(const Layout& L, float x, float z) {
    return tierOf(meshEAt(L, x, z));
}
// The ground's height for e where it is levelled (a town or a building's pad).
float padGround(float e) {
    return tierY(tierOf(e), e);
}

bool solidLand(const Layout& L, float x, float z) {
    return natAt(L, x, z) > LAND_BASE + 40.0f;
}

float slopeAt(const Layout& L, float x, float z) {
    const float d = 200.0f;
    return (fabsf(natAt(L, x + d, z) - natAt(L, x - d, z)) + fabsf(natAt(L, x, z + d) - natAt(L, x, z - d))) /
           (2.0f * d);
}

int roomAtXZ(const Layout& L, float x, float z) {
    const int sx = (int)floorf((x - L.x0) / ROOM_UNITS), sz = (int)floorf((z - L.z0) / ROOM_UNITS);
    const int n = pc_rplus_squares();
    return sx < 0 || sz < 0 || sx >= n || sz >= n ? 0 : sz * n + sx + 1;
}

// A spot for a lone building near (x, z): on land, not steep, in the same room; else (x, z) as it is.
void landSpot(const Layout& L, float* x, float* z) {
    if (solidLand(L, *x, *z) && slopeAt(L, *x, *z) < 0.3f) {
        return;
    }
    const int room = roomAtXZ(L, *x, *z);
    for (float r = 600.0f; r <= 6000.0f; r += 600.0f) {
        for (int k = 0; k < 16; k++) {
            const float a = k * 6.2831853f / 16.0f;
            const float tx = *x + sinf(a) * r, tz = *z + cosf(a) * r;
            if (roomAtXZ(L, tx, tz) == room && solidLand(L, tx, tz) && slopeAt(L, tx, tz) < 0.3f) {
                *x = tx;
                *z = tz;
                return;
            }
        }
    }
}

// Land all the way from a to b (a path can run there).
bool lineOnLand(const Layout& L, float ax, float az, float bx, float bz) {
    const float len = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
    const int steps = (int)(len / 400.0f) + 1;
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / steps;
        if (natAt(L, ax + (bx - ax) * t, az + (bz - az) * t) < LAND_BASE + 25.0f) {
            return false;
        }
    }
    return true;
}

bool segsCross(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
    auto side = [](float px, float pz, float qx, float qz, float rx, float rz) {
        return (qx - px) * (rz - pz) - (qz - pz) * (rx - px);
    };
    const float d1 = side(ax, az, bx, bz, cx, cz), d2 = side(ax, az, bx, bz, dx, dz);
    const float d3 = side(cx, cz, dx, dz, ax, az), d4 = side(cx, cz, dx, dz, bx, bz);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

// A path from a to b, bent a little (when the bent one stays on land).
void addPath(Layout& L, float ax, float az, float bx, float bz) {
    const float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        return;
    }
    const float nx = -dz / len, nz = dx / len;
    constexpr int PARTS = 5;
    float px[PARTS + 1], pz[PARTS + 1];
    bool ok = true;
    for (int i = 0; i <= PARTS; i++) {
        const float t = (float)i / PARTS;
        const float bend = i == 0 || i == PARTS ? 0.0f
                                                : (vnoise(ax * 0.0003f + i * 0.71f, az * 0.0003f, 91) - 0.5f) * len * 0.18f;
        px[i] = ax + dx * t + nx * bend;
        pz[i] = az + dz * t + nz * bend;
        if (i > 0 && !lineOnLand(L, px[i - 1], pz[i - 1], px[i], pz[i])) {
            ok = false;
        }
    }
    if (!ok) {
        L.paths.push_back({ax, az, bx, bz});
        return;
    }
    for (int i = 0; i < PARTS; i++) {
        L.paths.push_back({px[i], pz[i], px[i + 1], pz[i + 1]});
    }
}

Layout& layoutRef() {
    static Layout L;
    return L;
}

const Layout& layout() {
    Layout& L = layoutRef();
    static bool made = false, atSeaMade = false;
    const bool atSea = strcmp(dComIfGp_getStartStageName(), "sea") == 0;
    if (!pc_rplus_active() || (made && (atSeaMade || !atSea))) {
        return L;
    }
    made = true;
    atSeaMade = atSea;
    L = Layout();
    clearGridE();
    worldOrigin(&L.x0, &L.z0);
    L.cs = cellSize();
    const float cs = L.cs;

    // Every door that will stand: where its anchor is, before towns move them.
    struct Cand {
        const Interior* in;  // nullptr: Link's home
        const char* id;
        float x, z;
        int town = -1;
        s16 angle = 0;
        float y = 0;
        bool placed = false;
    };
    std::vector<Cand> cands;
    int sx, sy;
    pc_rplus_start_cell(&sx, &sy);
    const float startX = L.x0 + (sx + 0.5f) * cs, startZ = L.z0 + (sy + 0.5f) * cs;
    L.startX = startX;
    L.startZ = startZ;
    landSpot(L, &L.startX, &L.startZ);
    cands.push_back({nullptr, "home", startX, startZ - HOME_DOOR_AHEAD});
    for (int i = 0; i < pc_rplus_anchor_count(); i++) {
        const char *id, *kind, *stage;
        int cx, cy;
        if (!pc_rplus_anchor(i, &id, &kind, &stage, &cx, &cy) || stage[0] == '\0' || strcmp(stage, "Abesso") == 0) {
            continue;
        }
        const Interior* in = nullptr;
        for (const Interior& k : kInteriors) {
            if (strcmp(k.stage, stage) == 0) {
                in = &k;
                break;
            }
        }
        if (in == nullptr || pc_rplus_cell_height(cx, cy) == 0) {
            continue;
        }
        float x = L.x0 + (cx + 0.5f) * cs, z = L.z0 + (cy + 0.5f) * cs;
        if (cx == sx && cy == sy) {
            x += LINK_HOUSE_EAST;
            z -= HOME_DOOR_AHEAD;
        }
        const int room = roomAtXZ(L, x, z);
        if (room == 0 || !pc_rplus_sea_room_generated(room)) {
            continue;
        }
        cands.push_back({in, id, x, z});
    }

    // On land first (the coasts wander off the cells the anchors were put on).
    for (Cand& c : cands) {
        landSpot(L, &c.x, &c.z);
    }
    // Towns: houses (and Link's home) near each other, joined.
    const size_t nc = cands.size();
    std::vector<int> parent(nc);
    for (size_t i = 0; i < nc; i++) {
        parent[i] = (int)i;
    }
    auto find = [&](int i) {
        while (parent[i] != i) {
            i = parent[i] = parent[parent[i]];
        }
        return i;
    };
    auto housey = [&](const Cand& c) { return c.in == nullptr || c.in->style == 0; };
    const float link = TOWN_LINK * cs;
    for (size_t i = 0; i < nc; i++) {
        for (size_t j = i + 1; j < nc; j++) {
            if (housey(cands[i]) && housey(cands[j]) &&
                (cands[i].x - cands[j].x) * (cands[i].x - cands[j].x) +
                        (cands[i].z - cands[j].z) * (cands[i].z - cands[j].z) < link * link &&
                lineOnLand(L, cands[i].x, cands[i].z, cands[j].x, cands[j].z)) {
                parent[find((int)i)] = find((int)j);
            }
        }
    }
    for (size_t root = 0; root < nc; root++) {
        std::vector<int> members;
        for (size_t i = 0; i < nc; i++) {
            if (find((int)i) == (int)root) {
                members.push_back((int)i);
            }
        }
        if (members.size() < 2) {
            continue;
        }
        // The middle: the start cell for the start town, else the member nearest the members' centre.
        float cx = 0, cz = 0;
        bool start = false;
        for (int m : members) {
            cx += cands[m].x / members.size();
            cz += cands[m].z / members.size();
            start = start || cands[m].in == nullptr;
        }
        if (start) {
            cx = startX;
            cz = startZ;
        } else {
            int best = members[0];
            for (int m : members) {
                if ((cands[m].x - cx) * (cands[m].x - cx) + (cands[m].z - cz) * (cands[m].z - cz) <
                    (cands[best].x - cx) * (cands[best].x - cx) + (cands[best].z - cz) * (cands[best].z - cz)) {
                    best = m;
                }
            }
            cx = cands[best].x;
            cz = cands[best].z;
            landSpot(L, &cx, &cz);
        }
        const int n = (int)members.size();
        // Houses round the plaza, doors facing it; the ring turned and sized until every house is on land, and
        // the middle moved inland when no ring fits where it is.
        float R = 0, a0 = 0;
        int slots = n;  // the houses on the ring: the doors' and some more (homes without a way in)
        auto fits = [&](float mx, float mz) {
            if (!solidLand(L, mx, mz)) {
                return false;
            }
            const float scales[] = {1.0f, 0.85f, 1.2f, 0.7f};
            for (float sc : scales) {
                R = (1100.0f + 300.0f * slots) * sc;
                for (int turn = 0; turn < 12; turn++) {
                    a0 = turn * 6.2831853f / 12.0f / slots;
                    bool ok = true;
                    for (int k = 0; k < slots && ok; k++) {
                        const float a = a0 + k * 6.2831853f / slots;
                        const float hx = mx + sinf(a) * R, hz = mz + cosf(a) * R;
                        // The house behind the door, and the yard in front, on land.
                        ok = solidLand(L, hx, hz) &&
                             solidLand(L, mx + sinf(a) * (R + 500.0f), mz + cosf(a) * (R + 500.0f)) &&
                             lineOnLand(L, mx, mz, hx, hz);
                    }
                    if (ok) {
                        return true;
                    }
                }
            }
            return false;
        };
        bool ok = false;
        const int extra = 3 + (int)(hash2((int)cx, (int)cz) % 3);
        for (int m = extra; m >= 0 && !ok; m -= 2) {
            slots = n + m;
            ok = fits(cx, cz);
        }
        slots = ok ? slots : n;
        for (float rr = 1500.0f; rr <= 15000.0f && !ok; rr += 1500.0f) {
            for (int k = 0; k < 16 && !ok; k++) {
                const float a = k * 6.2831853f / 16.0f;
                const float mx = cx + sinf(a) * rr, mz = cz + cosf(a) * rr;
                if (roomAtXZ(L, mx, mz) != 0 && pc_rplus_sea_room_generated(roomAtXZ(L, mx, mz)) && fits(mx, mz)) {
                    cx = mx;
                    cz = mz;
                    ok = true;
                }
            }
        }
        if (!ok) {
            fprintf(stderr, "[rplus] a town of %d houses has no room round its middle: they stand apart\n", n);
            continue;
        }
        const float ty = std::max(natAt(L, cx, cz), LAND_BASE + 60.0f);
        if (start) {
            L.startX = cx;
            L.startZ = cz;
        }
        const int t = (int)L.towns.size();
        L.towns.push_back({cx, cz, padGround(ty), R, start, a0, slots});
        // Level past the houses' backs by more than a mesh triangle, so the mesh is flat under every house.
        const float flat = R + HUT_DEPTH + 100.0f + 1.5f * L.cs / SUB;
        L.pads.push_back({cx, cz, ty, flat, flat + 1800.0f});
        // The ring's slots: the doors spread evenly, the homes without a way in between.
        int next = 0;
        for (int k = 0; k < slots; k++) {
            const float a = a0 + k * 6.2831853f / slots;
            const float hx = cx + sinf(a) * R, hz = cz + cosf(a) * R;
            const s16 ang = cM_atan2s(cx - hx, cz - hz);  // the door's front toward the plaza
            const bool door = next < n && k == (next * slots) / n;
            if (door) {
                Cand& c = cands[members[next++]];
                c.x = hx;
                c.z = hz;
                c.angle = ang;
                c.y = padGround(ty);
                c.town = t;
                c.placed = true;
            } else {
                const int room = roomAtXZ(L, hx, hz);
                L.decos.push_back({cXyz(hx, padGround(ty), hz), ang, (int)(hash2((int)hx, (int)hz) % 4), room});
            }
            addPath(L, cx, cz, hx + sinf(a) * -150.0f, hz + cosf(a) * -150.0f);
            // Low brick walls either side of the yard in front of the house.
            const float s = cM_ssin(ang), co = cM_scos(ang);
            for (int side = -1; side <= 1; side += 2) {
                const float wx = hx + co * side * (HUT_HALF + 60.0f), wz = hz - s * side * (HUT_HALF + 60.0f);
                L.fences.push_back({wx, wz, wx + s * 520.0f, wz + co * 520.0f, 1});
            }
        }
    }
    // Lone buildings: on a good spot near their anchor, the ground levelled round them.
    for (Cand& c : cands) {
        if (c.placed) {
            continue;
        }
        landSpot(L, &c.x, &c.z);
        const float e = natAt(L, c.x, c.z);
        c.y = padGround(e);
        c.placed = true;
        // Past the house's back by more than a mesh triangle (cs / SUB, ~1400 units, a side), or the mesh's
        // triangles span the levelled spot and the house stands above the ground on a slope or a ledge.
        const float flat = std::max(650.0f, HUT_DEPTH + 1.5f * L.cs / SUB);
        L.pads.push_back({c.x, c.z, e, flat, flat + std::max(850.0f, 1.5f * L.cs / SUB)});
    }

    // The doors, with their exit ids.
    int perRoom[64] = {};
    for (const Cand& c : cands) {
        const int room = roomAtXZ(L, c.x, c.z);
        if (c.in != nullptr) {
            if (room == 0 || perRoom[room] >= DOORS_PER_ROOM) {
                continue;
            }
            bool clear = true;
            for (const GenDoor& d : L.doors) {
                if ((d.pos.x - c.x) * (d.pos.x - c.x) + (d.pos.z - c.z) * (d.pos.z - c.z) < DOOR_GAP * DOOR_GAP) {
                    clear = false;
                    break;
                }
            }
            if (!clear) {
                fprintf(stderr, "[rplus] door %s (%s) left out: too close to another\n", c.id, c.in->stage);
                continue;
            }
        }
        GenDoor d = c.in == nullptr ? GenDoor{"Abesso", 0, 0, nullptr, "home", 0, room, cXyz(), 0, HOME_EXIT}
                                    : GenDoor{c.in->stage, c.in->spawn, c.in->room, c.in->also, c.id, c.in->style,
                                              room, cXyz(), 0, 0};
        d.pos.set(c.x, c.y, c.z);
        d.angle = c.angle;
        d.variant = (int)(hash2((int)c.x, (int)c.z) % 4);
        if (c.in != nullptr) {
            d.exitId = DOOR_EXIT0 + perRoom[room]++;
        }
        d.town = c.town;
        L.doors.push_back(d);
    }

    // Paths: between the towns and the lone doors of each land, the shortest net joining them (Prim's), each
    // link straight over land and not too long.
    struct Site {
        float x, z;
        int town;
    };
    std::vector<Site> sites;
    for (size_t t = 0; t < L.towns.size(); t++) {
        sites.push_back({L.towns[t].x, L.towns[t].z, (int)t});
    }
    for (const GenDoor& d : L.doors) {
        if (d.town < 0) {
            sites.push_back({d.pos.x + cM_ssin(d.angle) * 400.0f, d.pos.z + cM_scos(d.angle) * 400.0f, -1});
        }
    }
    const size_t ns = sites.size();
    const float maxLink = 7.0f * cs;
    std::vector<bool> joined(ns, false);
    for (size_t s0 = 0; s0 < ns; s0++) {
        if (joined[s0]) {
            continue;
        }
        joined[s0] = true;
        std::vector<int> tree{(int)s0};
        for (;;) {
            float best = maxLink * maxLink;
            int bi = -1, bj = -1;
            for (int i : tree) {
                for (size_t j = 0; j < ns; j++) {
                    if (joined[j]) {
                        continue;
                    }
                    const float d2 = (sites[i].x - sites[j].x) * (sites[i].x - sites[j].x) +
                                     (sites[i].z - sites[j].z) * (sites[i].z - sites[j].z);
                    if (d2 < best && lineOnLand(L, sites[i].x, sites[i].z, sites[j].x, sites[j].z)) {
                        best = d2;
                        bi = i;
                        bj = (int)j;
                    }
                }
            }
            if (bj < 0) {
                break;
            }
            joined[bj] = true;
            tree.push_back(bj);
            addPath(L, sites[bi].x, sites[bi].z, sites[bj].x, sites[bj].z);
        }
        // A net all above tier 1 gets a path down to the lowland (which joins the beach): from its lowest site
        // to the nearest lowland, its stairs made below with the rest.
        int low = tree[0];
        for (int i : tree) {
            if (meshEAt(L, sites[i].x, sites[i].z) < meshEAt(L, sites[low].x, sites[low].z)) {
                low = i;
            }
        }
        if (tierAt(L, sites[low].x, sites[low].z) >= 2) {
            bool found = false;
            for (float rr = 800.0f; rr <= 24000.0f && !found; rr += 800.0f) {
                for (int k = 0; k < 24 && !found; k++) {
                    const float a = k * 6.2831853f / 24.0f;
                    const float px = sites[low].x + sinf(a) * rr, pz = sites[low].z + cosf(a) * rr;
                    if (solidLand(L, px, pz) && tierAt(L, px, pz) <= 1 &&
                        lineOnLand(L, sites[low].x, sites[low].z, px, pz)) {
                        L.paths.push_back({sites[low].x, sites[low].z, px, pz});
                        found = true;
                    }
                }
            }
            if (!found) {
                fprintf(stderr, "[rplus] a site at %.0f %.0f has no way down to the lowland\n", sites[low].x, sites[low].z);
            }
        }
    }

    // Stairs where a path crosses a cliff (one tier up, above tier 1): the stairs come down onto the lower shelf
    // along the path, as gently as the shelf below has room for.
    int noRoom = 0;
    for (const Seg& sg : L.paths) {
        const float dx = sg.x1 - sg.x0, dz = sg.z1 - sg.z0, len = sqrtf(dx * dx + dz * dz);
        if (len < 1.0f) {
            continue;
        }
        const int steps = (int)(len / 50.0f) + 1;
        float px = sg.x0, pz = sg.z0;
        int pk = tierAt(L, px, pz);
        for (int st = 1; st <= steps; st++) {
            const float qx = sg.x0 + dx * st / steps, qz = sg.z0 + dz * st / steps;
            const int qk = tierAt(L, qx, qz);
            if (qk != pk) {
                const int hiK = std::max(pk, qk), loK = std::min(pk, qk);
                if (hiK >= 2 && hiK - loK == 1) {
                    // The crossing, closer: e = tierStart(hiK) between p and q.
                    const float th = tierStart(hiK);
                    float ax = px, az = pz, bx2 = qx, bz2 = qz;
                    for (int it = 0; it < 12; it++) {
                        const float mx = (ax + bx2) * 0.5f, mz = (az + bz2) * 0.5f;
                        if ((meshEAt(L, mx, mz) >= th) == (pk == hiK)) {
                            ax = mx;
                            az = mz;
                        } else {
                            bx2 = mx;
                            bz2 = mz;
                        }
                    }
                    const float cx = (ax + bx2) * 0.5f, cz = (az + bz2) * 0.5f;
                    // Up: from the lower side to the higher.
                    float ux = dx / len, uz = dz / len;
                    if (pk == hiK) {
                        ux = -ux;
                        uz = -uz;
                    }
                    bool near = false;
                    for (const Ramp& o : L.ramps) {
                        near = near || (o.tx - cx) * (o.tx - cx) + (o.tz - cz) * (o.tz - cz) < 800.0f * 800.0f;
                    }
                    if (!near) {
                        const float yTop = tierStart(hiK), dh = yTop - tierY(loK, th);
                        constexpr float HALF = 190.0f;
                        bool made = false;
                        for (float f : {1.7f, 1.35f, 1.1f}) {
                            const float lr = dh * f;
                            bool ok = true;
                            for (float t = 60.0f; t <= lr + 60.0f && ok; t += 60.0f) {
                                for (int side = -1; side <= 1 && ok; side++) {
                                    const float sx = cx - ux * t + uz * HALF * side, sz = cz - uz * t - ux * HALF * side;
                                    ok = tierAt(L, sx, sz) == loK && solidLand(L, sx, sz);
                                }
                            }
                            if (ok) {
                                const float rbx = cx - ux * lr, rbz = cz - uz * lr;
                                L.ramps.push_back({rbx, rbz, cx + ux * 60.0f, cz + uz * 60.0f,
                                                   heightAt((rbx - L.x0) / L.cs, (rbz - L.z0) / L.cs), yTop + 2.0f, HALF});
                                made = true;
                                break;
                            }
                        }
                        if (!made) {
                            // No room straight down: stairs along the cliff's foot instead, their top beside the
                            // crossing (overlapping the edge a little, so the upper shelf is a step away).
                            const float gx = meshEAt(L, cx + 40.0f, cz) - meshEAt(L, cx - 40.0f, cz);
                            const float gz = meshEAt(L, cx, cz + 40.0f) - meshEAt(L, cx, cz - 40.0f);
                            const float gl = sqrtf(gx * gx + gz * gz);
                            if (gl > 0.0f) {
                                const float dnx = -gx / gl, dnz = -gz / gl;  // down the slope
                                const float ox = cx + dnx * (HALF - 15.0f), oz = cz + dnz * (HALF - 15.0f);
                                for (float f : {1.7f, 1.35f, 1.1f}) {
                                    const float lr = dh * f;
                                    for (int dir = -1; dir <= 1 && !made; dir += 2) {
                                        const float tx = -dnz * dir, tz = dnx * dir;  // along the cliff
                                        bool ok = true;
                                        for (float t = 0.0f; t <= lr + 60.0f && ok; t += 60.0f) {
                                            for (float lat : {0.0f, HALF * 0.8f, -HALF * 0.8f}) {
                                                // The cliff side may touch the upper shelf near the top.
                                                if (lat < 0.0f && t < lr * 0.35f) {
                                                    continue;
                                                }
                                                const float sx = ox + tx * t + dnx * lat, sz = oz + tz * t + dnz * lat;
                                                ok = ok && tierAt(L, sx, sz) == loK && solidLand(L, sx, sz);
                                            }
                                        }
                                        if (ok) {
                                            const float rbx = ox + tx * lr, rbz = oz + tz * lr;
                                            L.ramps.push_back({rbx, rbz, ox, oz,
                                                               heightAt((rbx - L.x0) / L.cs, (rbz - L.z0) / L.cs),
                                                               yTop + 2.0f, HALF});
                                            made = true;
                                        }
                                    }
                                    if (made) {
                                        break;
                                    }
                                }
                            }
                        }
                        noRoom += made ? 0 : 1;
                    }
                }
            }
            px = qx;
            pz = qz;
            pk = qk;
        }
    }

    // A wooden fence round each town, open where paths leave it and where the land ends.
    for (const Town& t : L.towns) {
        const float fr = t.r + 800.0f;
        const int parts = std::max(16, (int)(fr * 6.2831853f / 450.0f));
        for (int k = 0; k < parts; k++) {
            const float a = k * 6.2831853f / parts, b = (k + 1) * 6.2831853f / parts;
            const float ax = t.x + sinf(a) * fr, az = t.z + cosf(a) * fr, bx = t.x + sinf(b) * fr, bz = t.z + cosf(b) * fr;
            if (!solidLand(L, ax, az) || !solidLand(L, bx, bz) || tierAt(L, ax, az) != tierAt(L, bx, bz)) {
                continue;
            }
            // Gates: open between every two houses, so the town is never shut in.
            bool crossed = false;
            const float mid = (a + b) * 0.5f;
            for (int k = 0; k < t.slots && !crossed; k++) {
                float d = fmodf(mid - (t.a0 + (k + 0.5f) * 6.2831853f / t.slots) + 31.415926f, 6.2831853f) - 3.1415926f;
                crossed = fabsf(d) * fr < 260.0f;
            }
            for (const Seg& p : L.paths) {
                // A gap a little wider than the path.
                const float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
                if (segsCross(ax, az, bx, bz, p.x0, p.z0, p.x1, p.z1) || segDist2(p, mx, mz) < 500.0f * 500.0f) {
                    crossed = true;
                    break;
                }
            }
            if (!crossed) {
                L.fences.push_back({ax, az, bx, bz, 0});
            }
        }
    }
    fprintf(stderr, "[rplus] layout: %d doors, %d towns, %d path pieces, %d fence pieces, %d stairs (%d crossings "
            "without room for them)\n", (int)L.doors.size(), (int)L.towns.size(), (int)L.paths.size(),
            (int)L.fences.size(), (int)L.ramps.size(), noRoom);
    return L;
}

const std::vector<GenDoor>& genDoors() {
    return layout().doors;
}

float shapeGround(float gx, float gy, float y) {
    if (y <= LAND_BASE || !pc_rplus_active()) {
        return y;
    }
    const Layout& L = layout();
    const float x = L.x0 + gx * L.cs, z = L.z0 + gy * L.cs;
    // Inside a pad's level middle the ground is that pad's height, whatever other pads blend in nearby.
    const Pad* core = nullptr;
    float coreD2 = 0.0f;
    for (const Pad& p : L.pads) {
        const float dx = x - p.x, dz = z - p.z, d2 = dx * dx + dz * dz;
        if (d2 < p.r0 * p.r0 && (core == nullptr || d2 < coreD2)) {
            core = &p;
            coreD2 = d2;
        }
    }
    if (core != nullptr) {
        return core->y;
    }
    for (const Pad& p : L.pads) {
        const float dx = x - p.x, dz = z - p.z, d2 = dx * dx + dz * dz;
        if (d2 >= p.r1 * p.r1) {
            continue;
        }
        const float d = sqrtf(d2);
        float w = d <= p.r0 ? 1.0f : 1.0f - (d - p.r0) / (p.r1 - p.r0);
        w = w * w * (3.0f - 2.0f * w);
        y += (p.y - y) * w;
    }
    return y;
}

int groundMark(float x, float z) {
    if (!pc_rplus_active()) {
        return 0;
    }
    const Layout& L = layout();
    for (const Town& t : L.towns) {
        if ((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) < PLAZA_R * PLAZA_R) {
            return 2;
        }
    }
    // A path's edge wanders a little.
    const float half = PATH_HALF * (0.8f + 0.4f * vnoise(x * 0.004f, z * 0.004f, 101));
    for (const Seg& sg : L.paths) {
        if (segDist2(sg, x, z) < half * half) {
            return 1;
        }
    }
    return 0;
}

const std::vector<Fence>& layoutFences() {
    return layout().fences;
}

const std::vector<Deco>& layoutDecos() {
    return layout().decos;
}

const std::vector<Ramp>& layoutRamps() {
    return layout().ramps;
}

int startSpot(float* x, float* z) {
    const Layout& L = layout();
    *x = L.startX;
    *z = L.startZ;
    return roomAtXZ(L, *x, *z);
}

// The door whose interior is stage (or whose also is), else nullptr.
const GenDoor* doorOfStage(const char* stage) {
    for (const GenDoor& d : genDoors()) {
        if (strcmp(d.stage, stage) == 0) {
            return &d;
        }
    }
    for (const GenDoor& d : genDoors()) {
        if (d.also != nullptr && strcmp(d.also, stage) == 0) {
            return &d;
        }
    }
    return nullptr;
}

} // namespace

int pc_rplus_exit(int exitId, f32 speed, u32 mode) {
    if (exitId < DOOR_EXIT0 || exitId > HOME_EXIT || !pc_rplus_active() ||
        strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return 0;
    }
    // The door with this exit id nearest Link (the room he stands in may not be the stay room yet: rooms of the
    // generated world are entered on foot without the room table noticing).
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    const GenDoor* door = nullptr;
    float best = 0.0f;
    for (const GenDoor& d : genDoors()) {
        if (d.exitId != exitId) {
            continue;
        }
        const float dd = player == nullptr ? 0.0f : (player->current.pos - d.pos).abs2XZ();
        if (door == nullptr || dd < best) {
            door = &d;
            best = dd;
        }
    }
    if (door == nullptr || (player != nullptr && best > 3000.0f * 3000.0f)) {
        static int logged = -1;
        if (logged != exitId) {
            logged = exitId;
            fprintf(stderr, "[rplus] exit 0x%X: no generated door near Link (nearest %.0f away)\n", exitId,
                    door == nullptr ? -1.0f : sqrtf(best));
        }
        return 0;  // not by one of ours: a kept island's own exit
    }
    if (dComIfGp_isEnableNextStage()) {
        return 1;  // already on the way (Link stands on the exit floor for a few frames)
    }
    fprintf(stderr, "[rplus] through door %s into %s (point %d, room %d)\n", door->id, door->stage, door->spawn,
            door->room);
    dComIfGp_setNextStage(door->stage, door->spawn, door->room, -1, speed, mode, TRUE, 0);
    return 1;
}

int pc_rplus_exit_return(const char* stage, s16 point, s8 room, f32 speed, u32 mode, s32 wipe) {
    if (!pc_rplus_active() || strcmp(stage, "sea") != 0) {
        return 0;
    }
    // Out of an interior with a generated door, back to its island's room: out of that door instead.
    const GenDoor* door = doorOfStage(dComIfGp_getStartStageName());
    if (door == nullptr || (door->exitId != HOME_EXIT && !pc_rplus_sea_room_generated(room)) ||
        (door->exitId == HOME_EXIT && (room != CABANA_ROOM || point != CABANA_OUT_POINT))) {
        return 0;
    }
    if (dComIfGp_isEnableNextStage()) {
        return 1;  // already on the way (Link stands on the exit floor for a few frames)
    }
    // In front of the door, facing away from it.
    sArrival.on = true;
    sArrival.room = door->seaRoom;
    sArrival.pos.set(door->pos.x + cM_ssin(door->angle) * 250.0f, door->pos.y + 10.0f,
                     door->pos.z + cM_scos(door->angle) * 250.0f);
    sArrival.angleY = door->angle;
    fprintf(stderr, "[rplus] out of %s by door %s to room %d\n", dComIfGp_getStartStageName(), door->id,
            door->seaRoom);
    // A point the room has (dStage_playerInit falls back to its first one; the position is ours).
    dComIfGp_setNextStage("sea", 0, door->seaRoom, -1, speed, mode, TRUE, wipe);
    return 1;
}

namespace {

constexpr float FMAP_CLOSE_SPAN = 20000.0f;  // the chart's island close-up covers this much (d_menu_fmap.cpp /20000)

// The world position of a chart square's centre (grid 0..48 = sea room 1..49).
bool fmapRoom(int grid, float* rx, float* rz) {
    const int room = grid + 1;
    if (!pc_rplus_active() || grid < 0 || grid >= 49 || !pc_rplus_sea_room_generated(room)) {
        return false;
    }
    roomCentre(room, rx, rz);
    return true;
}

bool landAt(float x, float z) {
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    return heightAt((x - x0) / cs, (z - z0) / cs) > 0.0f;
}

// An IA4 100x100 picture (the disc's size) of span x span units round (x, z): the land opaque and light, its
// shore darker, the sea clear. The bytes written, 0 with no land in it.
u32 fmapPicture(float cx, float cz, float span, u8* out, u32 size) {
    constexpr int W = 100, H = 100, PW = 104;  // IA4 tiles are 8x4: the width padded to 104
    const u32 bytes = 0x20 + PW * H;
    if (bytes > size) {
        return 0;
    }
    memset(out, 0, bytes);
    BeOut o{out};
    out[0x00] = 0x02;  // GX_TF_IA4 (GX: I4 0, I8 1, IA4 2, IA8 3)
    out[0x01] = 2;     // alpha, as the disc's
    o.u16at(0x02, W);
    o.u16at(0x04, H);
    out[0x14] = 1;     // linear
    out[0x15] = 1;
    out[0x18] = 1;     // one image
    o.u32at(0x1C, 0x20);
    static bool land[H + 2][W + 2];
    for (int y = -1; y <= H; y++) {
        for (int x = -1; x <= W; x++) {
            land[y + 1][x + 1] = x >= 0 && y >= 0 && x < W && y < H &&
                                 landAt(cx - span / 2 + (x + 0.5f) * span / W, cz - span / 2 + (y + 0.5f) * span / H);
        }
    }
    bool any = false;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!land[y + 1][x + 1]) {
                continue;
            }
            any = true;
            const bool shore = !land[y + 1][x] || !land[y + 1][x + 2] || !land[y][x + 1] || !land[y + 2][x + 1];
            const int tile = (y / 4) * (PW / 8) + (x / 8);
            out[0x20 + tile * 32 + (y % 4) * 8 + (x % 8)] = shore ? 0xF0 : 0xFF;  // alpha high nibble, intensity low: the disc's dark rim
        }
    }
    return any ? bytes : 0;
}

// The middle of a square's land (its bounding box's centre), in units from the square's centre.
bool fmapLandCentre(int grid, float* ox, float* oz) {
    float rx, rz;
    if (!fmapRoom(grid, &rx, &rz)) {
        return false;
    }
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (int j = 0; j < 100; j++) {
        for (int i = 0; i < 100; i++) {
            const float x = rx - ROOM_UNITS / 2 + (i + 0.5f) * ROOM_UNITS / 100, z = rz - ROOM_UNITS / 2 + (j + 0.5f) * ROOM_UNITS / 100;
            if (landAt(x, z)) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                z0 = std::min(z0, z);
                z1 = std::max(z1, z);
            }
        }
    }
    if (x1 < x0) {
        *ox = *oz = 0.0f;
        return true;
    }
    *ox = (x0 + x1) * 0.5f - rx;
    *oz = (z0 + z1) * 0.5f - rz;
    return true;
}

} // namespace

int pc_rplus_fmap_texture(const char* name, void* buf, u32 size) {
    // The chart's island close-up, "R%02X_b.bti" (room number in hex): the land round its middle.
    unsigned room = 0;
    if (sscanf(name, "R%02X_b.bti", &room) != 1) {
        return -1;
    }
    float rx, rz, ox, oz;
    if (!fmapRoom((int)room - 1, &rx, &rz) || !fmapLandCentre((int)room - 1, &ox, &oz)) {
        return -1;
    }
    return (int)fmapPicture(rx + ox, rz + oz, FMAP_CLOSE_SPAN, (u8*)buf, size);
}

const void* pc_rplus_fmap_whole(int grid, u32* size) {
    float rx, rz;
    if (!fmapRoom(grid, &rx, &rz)) {
        return nullptr;
    }
    alignas(32) static u8 sPics[49][0x2c00];
    *size = fmapPicture(rx, rz, ROOM_UNITS, sPics[grid], sizeof sPics[grid]);
    return sPics[grid];
}

int pc_rplus_fmap_centre(int grid, s16* x, s16* y) {
    float ox, oz;
    if (!fmapLandCentre(grid, &ox, &oz)) {
        return 0;
    }
    *x = (s16)ox;
    *y = (s16)oz;
    return 1;
}

int pc_rplus_home_unlocked(void) {
    return pc_rplus_active() && strcmp(dComIfGp_getStartStageName(), "Abesso") == 0;
}

namespace {

// ---- Props: the islands' grass, flowers, trees, rocks and pots, with the params the disc's sea rooms use
// (scripts/rplus placement survey), scattered by biome. The same each time a room loads (the seed and the cell).

struct PropKind {
    const char* name;
    u32 params;
    int cost;            // what it takes from its packet's budget (grass tufts, flowers, trees), 0 for actors
    int pool;            // 0 grass, 1 flowers, 2 trees (dTree), 3 actors
    float clearance;     // nothing else within this many units
};

enum PropId {
    P_GRASS21, P_GRASS7, P_GRASS1, P_FLWR7, P_PFLWR7, P_FLOWER, P_SWOOD, P_SWOOD3, P_SWOOD5, P_LWOOD, P_PALM,
    P_PEBBLE, P_ROCK, P_ROCK2, P_BOMBFLOWER, P_COUNT
};
const PropKind kProps[P_COUNT] = {
    {"kusax21", 0x00000FC2, 21, 0, 260.0f}, {"kusax7", 0x00000FC1, 7, 0, 170.0f},
    {"kusax1", 0x00000FC0, 1, 0, 90.0f},    {"flwr7", 0x00000FE4, 7, 1, 200.0f},
    {"pflwrx7", 0x00000FF6, 7, 1, 200.0f},  {"flower", 0x00000FE0, 1, 1, 110.0f},
    {"swood", 0x00000010, 1, 2, 420.0f},    {"swood3", 0x00000013, 3, 2, 520.0f},
    {"swood5", 0x00000017, 5, 2, 620.0f},   {"lwood", 0xFFFFFFFF, 0, 3, 1100.0f},
    {"Vyasi", 0x00000038, 0, 3, 600.0f},    {"koisi1", 0x707FFFE0, 0, 3, 160.0f},
    {"Ebrock", 0xF27FFF3F, 0, 3, 500.0f},   {"Ebrock2", 0xF47FFF3A, 0, 3, 500.0f},
    {"BFlower", 0x0000FF0F, 0, 3, 320.0f},
};
// What one room may use of each packet (the game's packets now hold 16000 tufts, 2000 flowers, 512 trees,
// shared by the rooms loaded at once) and how many plain actors it makes.
const int kPoolBudget[4] = {2600, 300, 70, 70};
constexpr float PROP_STEP = 420.0f;  // the candidate spots' spacing

// The drawn land's height.
float meshY(float gx, float gy) {
    return heightAt(gx, gy);
}

// What grows at a spot: meadows (dense grass and flowers in broad patches), groves and forests of trees, rock
// on rugged land, palms on beaches; P_COUNT for nothing. h is a hash of the spot.
// edge: 1 at a cliff's foot (rocks), 2 along a shelf's top edge (a line of trees); lawn: thick grass in clumps
// where it is high; pathSide: just off a path (grass and flowers along it).
PropId propAt(char b, bool beach, float meadow, float grove, u32 h, int edge, float lawn, bool pathSide) {
    const u32 r = h % 1000;
    const bool green = b == 'g' || b == 'f';
    if (edge == 1 && !beach) {
        return r < 200 ? (b == 's' ? P_ROCK2 : P_ROCK) : r < 420 ? P_PEBBLE : r < 560 && green ? P_GRASS7 : P_COUNT;
    }
    if (edge == 2 && green) {
        return r < 240 ? P_SWOOD3 : r < 330 ? P_SWOOD : r < 560 ? P_GRASS7 : P_COUNT;
    }
    if (lawn > 0.6f && (green || b == 'd') && !beach) {
        return r < 620 ? P_GRASS21 : r < 860 ? P_GRASS7 : r < 910 && green ? P_FLWR7 : P_COUNT;
    }
    if (pathSide && green) {
        return r < 380 ? P_GRASS7 : r < 460 ? P_FLOWER : r < 520 ? P_GRASS1 : P_COUNT;
    }
    if (beach) {
        return r < 25 ? P_PALM : r < 60 ? P_GRASS1 : r < 75 ? P_PEBBLE : P_COUNT;
    }
    switch (b) {
    case 'g':
        if (grove > 0.62f) {
            return r < 300 ? P_SWOOD3 : r < 450 ? P_SWOOD : r < 520 ? P_SWOOD5 : r < 800 ? P_GRASS7 : P_COUNT;
        }
        if (meadow > 0.55f) {
            return r < 520 ? P_GRASS21 : r < 760 ? P_GRASS7 : r < 830 ? P_FLWR7 : r < 870 ? P_PFLWR7 : P_COUNT;
        }
        if (meadow > 0.45f) {
            return r < 220 ? P_GRASS7 : r < 380 ? P_FLOWER : r < 450 ? P_FLWR7 : r < 600 ? P_GRASS1 : P_COUNT;
        }
        return r < 50 ? P_GRASS1 : r < 70 ? P_GRASS7 : r < 85 ? P_SWOOD : r < 95 ? P_PEBBLE : P_COUNT;
    case 'f':
        if (grove > 0.4f) {
            return r < 260 ? P_SWOOD5 : r < 430 ? P_SWOOD3 : r < 460 ? P_LWOOD : r < 700 ? P_GRASS7 : P_COUNT;
        }
        return r < 120 ? P_GRASS7 : r < 220 ? P_SWOOD3 : r < 260 ? P_GRASS1 : P_COUNT;
    case 'd':
        return r < 30 ? P_PALM : r < 120 ? P_GRASS1 : r < 160 ? P_GRASS7 : r < 175 ? P_PEBBLE : P_COUNT;
    case 'r':
        return r < 40 ? P_ROCK : r < 90 ? P_PEBBLE : r < 200 ? P_GRASS1 : r < 230 ? P_GRASS7 : P_COUNT;
    case 'v':
        return r < 30 ? P_ROCK : r < 55 ? P_BOMBFLOWER : r < 100 ? P_PEBBLE : P_COUNT;
    case 's':
        return r < 30 ? P_ROCK2 : r < 70 ? P_PEBBLE : P_COUNT;
    default:
        return P_COUNT;
    }
}

void scatterProps(int roomNo) {
    const int n = pc_rplus_squares();
    const int r = roomNo - 1;
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    float rx, rz;
    roomCentre(roomNo, &rx, &rz);
    const float bx = rx - ROOM_UNITS * 0.5f, bz = rz - ROOM_UNITS * 0.5f;
    (void)n;
    (void)r;
    const Layout& L = layout();
    std::vector<const GenDoor*> doors;
    for (const GenDoor& gd : L.doors) {
        if (gd.seaRoom == roomNo) {
            doors.push_back(&gd);
        }
    }
    // The candidate spots on land, nearest the room's doors and towns first (each with a little jitter in its
    // order), so a room's budget goes first where people are.
    struct Spot {
        float x, z, y, key;
        u32 h;
    };
    std::vector<Spot> spots;
    const int side = (int)(ROOM_UNITS / PROP_STEP);
    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            const u32 h = hash2(i * 7 + roomNo * 9973, j * 13 + 0x5151);
            const float x = bx + (i + ((h >> 4) & 0xFF) / 256.0f) * PROP_STEP;
            const float z = bz + (j + ((h >> 12) & 0xFF) / 256.0f) * PROP_STEP;
            const float gx = (x - x0) / cs, gy = (z - z0) / cs;
            if (pc_rplus_cell_height((int)gx, (int)gy) == 0) {
                // Open sea (no land in the cells round it either) costs nothing more.
                bool land = false;
                for (int dj = -1; dj <= 1 && !land; dj++) {
                    for (int di = -1; di <= 1 && !land; di++) {
                        land = pc_rplus_cell_height((int)gx + di, (int)gy + dj) != 0;
                    }
                }
                if (!land || naturalY(gx, gy) < LAND_BASE) {
                    continue;
                }
            }
            const float y = meshY(gx, gy);
            if (y < LAND_BASE + 15.0f) {
                continue;  // in the surf
            }
            float near = 1e12f;
            for (const GenDoor* d : doors) {
                near = std::min(near, (x - d->pos.x) * (x - d->pos.x) + (z - d->pos.z) * (z - d->pos.z));
            }
            for (const Town& t : L.towns) {
                near = std::min(near, (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z));
            }
            spots.push_back({x, z, y, sqrtf(near) * (0.75f + 0.5f * ((h >> 20) & 0xFF) / 256.0f), h});
        }
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.key < b.key; });

    int used[4] = {};
    int made[P_COUNT] = {};
    struct Placed {
        float x, z, clear;
    };
    std::vector<Placed> placed;
    // Placed props by coarse grid cell, for the clearance check.
    const float GRID = 1200.0f;
    const int gside = (int)(ROOM_UNITS / GRID) + 1;
    std::vector<std::vector<int>> grid((size_t)gside * gside);
    for (const Spot& sp : spots) {
        const float x = sp.x, z = sp.z, y = sp.y;
        const float gx = (x - x0) / cs, gy = (z - z0) / cs;
        // Clear of houses, doorsteps, plazas, paths and fences.
        bool blocked = false;
        for (const GenDoor* d : doors) {
            if ((x - d->pos.x) * (x - d->pos.x) + (z - d->pos.z) * (z - d->pos.z) < 800.0f * 800.0f) {
                blocked = true;
                break;
            }
        }
        if (blocked || groundMark(x, z) != 0) {
            continue;
        }
        for (const Town& t : L.towns) {
            const float d2 = (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z);
            if (d2 < (t.r + 450.0f) * (t.r + 450.0f) && d2 > PLAZA_R * PLAZA_R) {
                blocked = true;  // the houses' ring and yards (the plaza is already kept clear)
                break;
            }
        }
        for (const Fence& f : L.fences) {
            if (!blocked && segDist2(Seg{f.x0, f.z0, f.x1, f.z1}, x, z) < 220.0f * 220.0f) {
                blocked = true;
            }
        }
        bool pathSide = false;
        for (const Seg& p : L.paths) {
            const float d2 = segDist2(p, x, z);
            if (!blocked && d2 < (PATH_HALF + 160.0f) * (PATH_HALF + 160.0f)) {
                blocked = true;
            }
            pathSide = pathSide || d2 < (PATH_HALF + 650.0f) * (PATH_HALF + 650.0f);
        }
        for (const Ramp& rp : L.ramps) {
            if (!blocked && segDist2(Seg{rp.bx, rp.bz, rp.tx, rp.tz}, x, z) < (rp.half + 200.0f) * (rp.half + 200.0f)) {
                blocked = true;
            }
        }
        if (blocked) {
            continue;
        }
        // By a cliff: higher ground a little way off (its foot), or lower (its top edge).
        int edge = 0;
        {
            const int k = tierOf(meshE(gx, gy));
            const float d = 380.0f / cs;
            const float off[4][2] = {{d, 0}, {-d, 0}, {0, d}, {0, -d}};
            for (const auto& o : off) {
                const int ko = tierOf(meshE(gx + o[0], gy + o[1]));
                if (ko > k && ko >= 2) {
                    edge = 1;
                } else if (ko < k && k >= 2 && edge == 0) {
                    edge = 2;
                }
            }
        }
        const float dxy = fabsf(meshY(gx + 0.02f, gy) - y) + fabsf(meshY(gx, gy + 0.02f) - y);
        if (dxy > 0.02f * cs * 0.9f) {
            continue;  // too steep
        }
        const bool beach = y < LAND_BASE + 70.0f;
        const float meadow = fbm(x * 0.00022f, z * 0.00022f, 151, 3);
        const float grove = fbm(x * 0.00030f, z * 0.00030f, 163, 3);
        const float lawn = fbm(x * 0.0007f, z * 0.0007f, 171, 2) + (meadow - 0.5f) * 0.6f;
        const PropId id = propAt(biomeNear(gx, gy), beach, meadow, grove, sp.h >> 3, edge, lawn, pathSide);
        if (id == P_COUNT) {
            continue;
        }
        const PropKind& kind = kProps[id];
        const int cost = kind.pool == 3 ? 1 : kind.cost;
        if (used[kind.pool] + cost > kPoolBudget[kind.pool]) {
            continue;
        }
        const int gi = std::min(gside - 1, std::max(0, (int)((x - bx) / GRID)));
        const int gj = std::min(gside - 1, std::max(0, (int)((z - bz) / GRID)));
        bool clear = true;
        for (int dj = -1; dj <= 1 && clear; dj++) {
            for (int di = -1; di <= 1 && clear; di++) {
                const int ci = gi + di, cj = gj + dj;
                if (ci < 0 || cj < 0 || ci >= gside || cj >= gside) {
                    continue;
                }
                for (int k : grid[(size_t)cj * gside + ci]) {
                    const Placed& p = placed[k];
                    const float c2 = std::max(p.clear, kind.clearance);
                    if ((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < c2 * c2) {
                        clear = false;
                        break;
                    }
                }
            }
        }
        if (!clear) {
            continue;
        }
        cXyz pos(x, y, z);
        csXyz angle(0, (s16)(sp.h >> 16), 0);
        fopAcM_create((char*)kind.name, kind.params, &pos, roomNo, &angle, nullptr);
        grid[(size_t)gj * gside + gi].push_back((int)placed.size());
        placed.push_back({x, z, kind.clearance});
        used[kind.pool] += cost;
        made[id]++;
    }
    if (!placed.empty()) {
        fprintf(stderr, "[rplus] room %d: %d props (grass %d tufts, flowers %d, trees %d, others %d)\n", roomNo,
                (int)placed.size(), used[0], used[1], used[2], used[3]);
    }
}
} // namespace

namespace rplus {

std::vector<TownInfo> towns() {
    std::vector<TownInfo> out;
    if (!pc_rplus_active()) {
        return out;
    }
    const Layout& L = layout();
    for (const Town& t : L.towns) {
        out.push_back({t.x, t.z, t.y, t.r, PLAZA_R, roomAtXZ(L, t.x, t.z)});
    }
    return out;
}

float groundY(float x, float z) {
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    return meshY((x - x0) / cs, (z - z0) / cs);
}

int roomAt(float x, float z) {
    return roomAtXZ(layout(), x, z);
}

char biomeAt(float x, float z) {
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    return biomeNear((x - x0) / cs, (z - z0) / cs);
}

bool openGround(float x, float z, float clear) {
    const Layout& L = layout();
    if (groundMark(x, z) != 0) {
        return false;
    }
    const float c2 = clear * clear;
    for (const GenDoor& d : L.doors) {
        if ((x - d.pos.x) * (x - d.pos.x) + (z - d.pos.z) * (z - d.pos.z) < c2 + 700.0f * 700.0f) {
            return false;
        }
    }
    for (const Deco& d : L.decos) {
        if ((x - d.pos.x) * (x - d.pos.x) + (z - d.pos.z) * (z - d.pos.z) < c2 + 700.0f * 700.0f) {
            return false;
        }
    }
    for (const Fence& f : L.fences) {
        if (segDist2(Seg{f.x0, f.z0, f.x1, f.z1}, x, z) < c2 + 150.0f * 150.0f) {
            return false;
        }
    }
    return true;
}

void cellSpot(int cx, int cy, float* x, float* z) {
    const Layout& L = layout();
    *x = L.x0 + (cx + 0.5f) * L.cs;
    *z = L.z0 + (cy + 0.5f) * L.cs;
    landSpot(L, x, z);
}

void startXZ(float* x, float* z) {
    startSpot(x, z);
}

void roomCorner(int roomNo, float* x, float* z) {
    roomCentre(roomNo, x, z);
    *x -= ROOM_UNITS * 0.5f;
    *z -= ROOM_UNITS * 0.5f;
}

bool steep(float x, float z) {
    const float y = groundY(x, z);
    return fabsf(groundY(x + 150.0f, z) - y) + fabsf(groundY(x, z + 150.0f) - y) > 150.0f;
}

} // namespace rplus

void pc_rplus_room_actors(int roomNo) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return;
    }
    if (pc_rplus_sea_room_generated(roomNo)) {
        scatterProps(roomNo);
        pc_rplus_life_room(roomNo);
    }
    for (const GenDoor& gd : genDoors()) {
        if (gd.seaRoom != roomNo) {
            continue;
        }
        // A KNOB01 door, both sides a stage exit (angle x 0x0FFF), no lock (type 0), event or switch. Its model
        // is the shape (params bits 20-27, the Knob archive's door_a..h): Link's home the cabana's (4, its
        // deed lock skipped in Abesso), houses the plain wooden door (0), the rest by style.
        static const u32 kShape[3] = {0, 2, 3};
        const u32 shape = gd.exitId == HOME_EXIT ? 4 : kShape[gd.style];
        cXyz pos = gd.pos;
        csXyz angle(0x0FFF, gd.angle, 0);
        cXyz scale(1.0f, 1.0f, 1.0f);
        fopAcM_create((char*)"KNOB01", 0x000FF0FFu | (shape << 20), &pos, roomNo, &angle, &scale);
        fprintf(stderr, "[rplus] door %s (%s) in room %d at %.0f %.0f %.0f\n", gd.id, gd.stage, roomNo, pos.x,
                pos.y, pos.z);
    }
}

void pc_rplus_ship_pos(int roomNo, cXyz* pos, s16* angle) {
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0 ||
        !pc_rplus_sea_room_generated(roomNo)) {
        return;
    }
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    // From the start cell in the start room (Link's first spawn there is moved to it), else from the SHIP entry.
    int rx, ry;
    pc_rplus_start_cell(&rx, &ry);
    const int startRoom = (ry / pc_rplus_cells()) * pc_rplus_squares() + (rx / pc_rplus_cells()) + 1;
    if (roomNo != startRoom) {
        rx = (int)floorf((pos->x - x0) / cs);
        ry = (int)floorf((pos->z - z0) / cs);
    }
    // The nearest sea cell, then out from the land toward it to the first wet quad (the shore walls stand on the
    // dry quads' edges, d_a_ship's wall circles are 250 across) and a boat's length on: just off the shore.
    const int n = pc_rplus_squares() * pc_rplus_cells();
    int best = -1, bx = 0, by = 0;
    for (int r = 1; r < n && best < 0; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) {
                    continue;  // the ring at distance r only
                }
                const int cx = rx + dx, cy = ry + dy;
                if (cx < 0 || cy < 0 || cx >= n || cy >= n || pc_rplus_cell_height(cx, cy) != 0) {
                    continue;
                }
                const int d = dx * dx + dy * dy;
                if (best < 0 || d < best) {
                    best = d;
                    bx = cx;
                    by = cy;
                }
            }
        }
    }
    if (best < 0) {
        return;
    }
    const float lx = x0 + (rx + 0.5f) * cs, lz = z0 + (ry + 0.5f) * cs;
    float dx = (bx - rx), dz = (by - ry);
    const float len = sqrtf(dx * dx + dz * dz);
    dx /= len;
    dz /= len;
    const float q = cs / SUB;  // a quad's side
    auto wetQuad = [&](float x, float z) {
        const float qx = floorf((x - x0) / q), qz = floorf((z - z0) / q);
        for (int k = 0; k < 4; k++) {
            if (heightAt((qx + (k & 1)) / SUB, (qz + (k >> 1)) / SUB) > 0.0f) {
                return false;
            }
        }
        return true;
    };
    float sx = x0 + (bx + 0.5f) * cs, sz = z0 + (by + 0.5f) * cs;  // the sea cell's centre if nothing nearer
    for (float t = 0.0f; t < len * cs + cs; t += 100.0f) {
        const float x = lx + dx * t, z = lz + dz * t;
        if (wetQuad(x, z) && wetQuad(x + dx * 600.0f, z + dz * 600.0f)) {
            sx = x + dx * 600.0f;
            sz = z + dz * 600.0f;
            break;
        }
    }
    *angle = cM_atan2s(dx, dz);  // facing away from the land
    fprintf(stderr, "[rplus] the boat in room %d moved from %.0f %.0f to the shore at %.0f %.0f\n", roomNo, pos->x,
            pos->z, sx, sz);
    pos->set(sx, 0.0f, sz);
}

namespace {

// What the world's logic starts with (scripts/rplus/wwlogic.py DEFAULT_START, wwrando's defaults with "Start with
// Hero's Sword"): given at sea to a file that has no sword yet, as a new file in Randomizer+ is.
void giveStartGear() {
    if (dComIfGs_isCollect(0, 0) || dComIfGs_isCollect(0, 1) || dComIfGs_isCollect(0, 2) ||
        dComIfGs_isCollect(0, 3)) {
        return;
    }
    static const u8 kGear[] = {dItemNo_SWORD_e,         dItemNo_SHIELD_e,          dItemNo_WIND_WAKER_e,
                               dItemNo_WINDS_REQUIEM_e, dItemNo_BALLAD_OF_GALES_e, dItemNo_SONG_OF_PASSING_e,
                               dItemNo_SAIL_e,          dItemNo_MAGIC_POWER_e};
    for (u8 item : kGear) {
        execItemGet(item);
    }
    if (dComIfGs_getMaxMagic() == 0) {
        dComIfGp_setItemMaxMagicCount(16);
        dComIfGp_setItemMagicCount(16);
    }
    fprintf(stderr, "[rplus] new file: gave the start gear (sword, shield, Wind Waker, 3 songs, sail, magic)\n");
}

} // namespace

void pc_rplus_place_player(int roomNo) {
    static bool placed = false;
    if (!pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return;
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
        return;
    }
    if (roomNo == dComIfGp_roomControl_getStayNo() && !dComIfGp_roomControl_checkStatusFlag(0, 0x01)) {
        // Room 0 is the whole sea's floor, one group a square: the ground under the boat out of the loaded
        // squares, which loads the square sailed into (dStage_RoomCheck). It comes with each square's room table
        // entry, which is asked for here until it is in.
        requestRoomLoad(roomNo);
    }
    if (roomNo == dComIfGp_roomControl_getStayNo()) {
        // Where Link is about every two seconds (in the run logs, for the land, water and respawn checks).
        static u32 tick = 0;
        if (tick++ % 60 == 0) {
            const cXyz& p = player->current.pos;
            float x0, z0;
            worldOrigin(&x0, &z0);
            const bool sea = daSea_ChkArea(p.x, p.z);
            fprintf(stderr, "[rplus] Link at %.0f %.0f %.0f in room %d, land %.0f, sea %s %.0f\n", p.x, p.y, p.z,
                    roomNo, heightAt((p.x - x0) / cellSize(), (p.z - z0) / cellSize()), sea ? "here" : "not here",
                    sea ? daSea_calcWave(p.x, p.z) : 0.0f);
        }
    }
    giveStartGear();
    if (placed || sStartPointSet) {
        return;
    }
    float sx, sz;
    const int startRoom = startSpot(&sx, &sz);
    cXyz pos(sx, heightAt_world(sx, sz) + 150.0f, sz);
    player->current.pos = pos;
    player->old.pos = pos;
    player->speedF = 0.0f;
    player->speed = cXyz(0.0f, 0.0f, 0.0f);
    // Held there until the start room's own land is loaded. The game loads rooms from the ground under Link
    // (dStage_RoomCheck), and there is none until the start room is in, so its load is asked for here.
    if (roomNo != startRoom) {
        requestRoomLoad(startRoom);
        return;
    }
    placed = true;
    fprintf(stderr, "[rplus] Link put on the start spot at %.0f %.0f %.0f\n", pos.x, pos.y, pos.z);
}

#ifdef PC_RPLUS_DEBUG_MAP
// An offline check of the generated land (built only into a test with PC_RPLUS_DEBUG_MAP): the whole world as a
// PPM, px world units a pixel: land shaded by height and slope, paths, plazas, fences, doors.
extern "C" void pc_rplus_debug_map(const char* path, float px, float cx, float cz, float size) {
    const Layout& L = layout();
    const float worldW = pc_rplus_squares() * ROOM_UNITS;
    const float ox = size > 0 ? cx - size / 2 : L.x0, oz = size > 0 ? cz - size / 2 : L.z0;
    const int W = (int)((size > 0 ? size : worldW) / px);
    std::vector<u8> img((size_t)W * W * 3);
    float hmin = 1e9f, hmax = -1e9f, steep = 0;
    int landPx = 0;
    for (int j = 0; j < W; j++) {
        for (int i = 0; i < W; i++) {
            const float x = ox + (i + 0.5f) * px, z = oz + (j + 0.5f) * px;
            const float y = heightAt_world(x, z);
            u8* o = &img[((size_t)j * W + i) * 3];
            if (y <= 0.0f) {
                o[0] = 40; o[1] = 90; o[2] = (u8)std::max(120.0f, 200.0f + y * 0.05f);
                continue;
            }
            landPx++;
            hmin = std::min(hmin, y);
            hmax = std::max(hmax, y);
            const float yx = heightAt_world(x + 50.0f, z), yz = heightAt_world(x, z + 50.0f);
            const float sl = sqrtf((yx - y) * (yx - y) + (yz - y) * (yz - y)) / 50.0f;
            steep = std::max(steep, sl);
            float shade = 0.75f + 0.25f * std::max(-1.0f, std::min(1.0f, -(yx - y + yz - y) / 50.0f));
            Rgb c = biomeColour(biomeNear((x - L.x0) / L.cs, (z - L.z0) / L.cs));
            if (y < LAND_BASE + 60.0f) c = biomeColour('d');
            if (sl > 0.9f) c = {120, 110, 100};
            // Cliffs: where the tier changes above tier 1, dark.
            const int k0 = tierOf(meshE((x - L.x0) / L.cs, (z - L.z0) / L.cs));
            const int kx = tierOf(meshE((x + px - L.x0) / L.cs, (z - L.z0) / L.cs));
            const int kz = tierOf(meshE((x - L.x0) / L.cs, (z + px - L.z0) / L.cs));
            if (std::max(k0, std::max(kx, kz)) >= 2 && (k0 != kx || k0 != kz)) c = {40, 34, 30};
            const int m = groundMark(x, z);
            if (m == 1) c = {172, 136, 92};
            if (m == 2) c = {196, 188, 172};
            const float lift = 0.7f + 0.3f * std::min(1.0f, y / 3000.0f);
            o[0] = (u8)std::min(255.0f, c.r * shade * lift);
            o[1] = (u8)std::min(255.0f, c.g * shade * lift);
            o[2] = (u8)std::min(255.0f, c.b * shade * lift);
        }
    }
    auto dot = [&](float x, float z, int rad, u8 r, u8 g, u8 b) {
        const int ci = (int)((x - ox) / px), cj = (int)((z - oz) / px);
        for (int dj = -rad; dj <= rad; dj++)
            for (int di = -rad; di <= rad; di++) {
                const int i = ci + di, j = cj + dj;
                if (i >= 0 && j >= 0 && i < W && j < W) {
                    u8* o = &img[((size_t)j * W + i) * 3];
                    o[0] = r; o[1] = g; o[2] = b;
                }
            }
    };
    for (const Fence& f : L.fences) {
        for (int k = 0; k <= 8; k++) {
            dot(f.x0 + (f.x1 - f.x0) * k / 8.0f, f.z0 + (f.z1 - f.z0) * k / 8.0f, 0, f.kind ? 160 : 90,
                f.kind ? 40 : 60, f.kind ? 30 : 20);
        }
    }
    for (const Deco& d : L.decos) {
        dot(d.pos.x, d.pos.z, 2, 240, 150, 40);
    }
    for (size_t k = 0; k < L.ramps.size() && size <= 0; k += 8) {
        fprintf(stderr, "  stairs at %.0f %.0f, %.0f up\n", L.ramps[k].tx, L.ramps[k].tz, L.ramps[k].ty - L.ramps[k].by);
    }
    for (const Ramp& rp : L.ramps) {
        for (int k = 0; k <= 10; k++) {
            dot(rp.bx + (rp.tx - rp.bx) * k / 10.0f, rp.bz + (rp.tz - rp.bz) * k / 10.0f, 1, 255, 230, 40);
        }
    }
    for (const GenDoor& d : L.doors) {
        dot(d.pos.x, d.pos.z, 2, d.exitId == HOME_EXIT ? 255 : 220, d.exitId == HOME_EXIT ? 255 : 30, 30);
    }
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, W);
    fwrite(img.data(), 1, img.size(), f);
    fclose(f);
    fprintf(stderr, "[rplus] map %dx%d: land %d px, height %.0f..%.0f, steepest slope %.2f\n", W, W, landPx, hmin, hmax,
            steep);
    {
        // The mesh's slopes (the samples SUB to a cell), what Link walks on.
        const float st = L.cs / SUB;
        const int ns = size > 0 ? 0 : (int)(worldW / st);
        int land = 0, over40 = 0, over50 = 0;
        for (int j = 0; j < ns; j++) {
            for (int i = 0; i < ns; i++) {
                const float a = heightAt(i / (float)SUB, j / (float)SUB);
                const float bq = heightAt((i + 1) / (float)SUB, j / (float)SUB), c = heightAt(i / (float)SUB, (j + 1) / (float)SUB);
                if (a <= LAND_BASE + 30.0f) continue;
                land++;
                const float sl = std::max(fabsf(bq - a), fabsf(c - a)) / st;
                over40 += sl > 0.84f;
                over50 += sl > 1.19f;
            }
        }
        fprintf(stderr, "[rplus] mesh: %d land samples, %.1f%% steeper than 40 deg, %.1f%% steeper than 50 deg\n", land,
                100.0f * over40 / std::max(1, land), 100.0f * over50 / std::max(1, land));
    }
    for (const Town& t : L.towns) {
        fprintf(stderr, "  town at %.0f %.0f y %.0f r %.0f%s\n", t.x, t.z, t.y, t.r, t.start ? " (start)" : "");
    }
    for (const GenDoor& d : L.doors) {
        fprintf(stderr, "  door %-20s %-8s room %2d at %7.0f %6.0f %7.0f ang %6d town %d ground %.0f\n", d.id, d.stage,
                d.seaRoom, d.pos.x, d.pos.y, d.pos.z, d.angle, d.town, heightAt_world(d.pos.x, d.pos.z));
    }
}
#endif
