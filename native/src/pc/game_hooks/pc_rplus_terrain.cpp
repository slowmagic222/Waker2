// Randomizer+ terrain (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): a generated sea room's land, built from the world's
// cell heights when the room's daBg is created, in place of the room's island model and collision.
//
// Height: the world gives each cell a relative height (0 = water). The surface is the bilinear blend of the cells'
// heights at their centres (land 80 + h * 3.2 units, water -1400), sampled SUB times per cell side, with a little
// value noise on land. It is a function of the world position alone, so neighbouring rooms meet without a seam.
//
// Collision: a DZB built in memory in the disc's layout (big-endian, table offsets from the start), then made
// ready by cBgS::ConvDzb as a room's room.dzb is (d_resorce.cpp), and handed to dBgW::Set. One group (the room),
// one poly info per ground kind, blocks of BLK x BLK quads under an 8-way tree.
//
// Drawing: one J3DPacket with the triangles in GX immediate mode, vertex colours from the biome with the light
// baked in two tones; the game's own textures come later.
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "d/d_com_inf_game.h"
#include "d/d_bg_w.h"
#include "d/d_kankyo.h"
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr int SUB = 4;               // samples per cell side
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

// Height at grid position (gx, gy) in cells from the world's corner.
float heightAt(float gx, float gy) {
    float fx = gx - 0.5f, fy = gy - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - ix, ty = fy - iy;
    float a = cellY(ix, iy), b = cellY(ix + 1, iy), c = cellY(ix, iy + 1), d = cellY(ix + 1, iy + 1);
    float y = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    if (y > LAND_BASE) {
        // A little relief on land: value noise on the sample grid.
        int sx = (int)floorf(gx * SUB), sy = (int)floorf(gy * SUB);
        float n = (hash2(sx, sy) & 0xFFFF) / 65535.0f - 0.5f;
        y += n * 120.0f * fminf(1.0f, (y - LAND_BASE) / 400.0f);
    }
    return y;
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
enum Ground { G_GRASS, G_STONE, G_SAND, G_DIRT, G_SHORE, G_COUNT };
const u32 kGroundAttr[G_COUNT] = {0x04, 0x03, 0x0B, 0x01, 0x03};
const u32 kGroundPass[G_COUNT] = {0, 0, 0, 0, 0x01 | 0x04 | 0x08};

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
    int mRowTris[256] = {};  // triangles per sample row, to split GXBegin calls
    int mRows = 0;
    void draw() override;
};

struct PcRplusTerrain {
    PcRplusPacket packet;
    u8* dzb = nullptr;
};

void PcRplusPacket::draw() {
    if (mTriCount == 0) {
        return;
    }
    j3dSys.reinitGX();
    GXSetNumIndStages(0);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
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

    // Samples.
    float* ys = new float[(size_t)nv * nv];
    if (ys == nullptr) {
        return nullptr;
    }
    for (int j = 0; j < nv; j++) {
        for (int i = 0; i < nv; i++) {
            ys[j * nv + i] = heightAt(gx0 + (float)i / SUB, gy0 + (float)j / SUB);
        }
    }
    auto Y = [&](int i, int j) { return ys[j * nv + i]; };
    auto keep = [&](int i, int j) {
        return Y(i, j) > KEEP_Y || Y(i + 1, j) > KEEP_Y || Y(i, j + 1) > KEEP_Y || Y(i + 1, j + 1) > KEEP_Y;
    };

    // Kept quads, in collision block order.
    int quads = 0;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            quads += keep(i, j) ? 1 : 0;
        }
    }

    PcRplusTerrain* t = far ? new (std::malloc(sizeof(PcRplusTerrain))) PcRplusTerrain() : new PcRplusTerrain();
    if (t == nullptr) {
        delete[] ys;
        return nullptr;
    }
    // ---- Drawing: triangles by sample row (the land and the shallows; the sea bed is not drawn).
    PcRplusPacket& pk = t->packet;
    pk.mVtx = far ? (Vtx*)std::malloc(sizeof(Vtx) * (size_t)quads * 6 + 1) : new Vtx[(size_t)quads * 6];
    pk.mRows = n < 256 ? n : 256;
    Vtx* out = pk.mVtx;
    const float lx = 0.42f, ly = 0.80f, lz = 0.42f;  // light direction (normalised enough)
    for (int j = 0; j < n && j < 256; j++) {
        for (int i = 0; i < n; i++) {
            if (!keep(i, j)) {
                continue;
            }
            int ci[6][2] = {{i, j}, {i, j + 1}, {i + 1, j}, {i + 1, j}, {i, j + 1}, {i + 1, j + 1}};
            for (int tri = 0; tri < 2; tri++) {
                int (*c)[2] = &ci[tri * 3];
                float px[3], py[3], pz[3];
                for (int k = 0; k < 3; k++) {
                    px[k] = bx + c[k][0] * step;
                    pz[k] = bz + c[k][1] * step;
                    py[k] = Y(c[k][0], c[k][1]);
                }
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
                    float gxw = gx0 + (float)c[k][0] / SUB, gyw = gy0 + (float)c[k][1] / SUB;
                    char b = biomeNear(gxw, gyw);
                    Rgb col = biomeColour(b);
                    if (py[k] < LAND_BASE + 60.0f) {
                        col = biomeColour('d');
                    } else if (ny < 0.75f && b != 's') {
                        col = biomeColour('r');
                    }
                    if (py[k] < 0.0f) {
                        tone *= 0.85f;
                    }
                    out->x = px[k];
                    out->y = py[k];
                    out->z = pz[k];
                    out->r = (u8)(col.r * tone);
                    out->g = (u8)(col.g * tone);
                    out->b = (u8)(col.b * tone);
                    out->a = 0xFF;
                    out++;
                }
            }
            pk.mRowTris[j] += 2;
        }
    }
    pk.mTriCount = quads * 2;
    if (far) {
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
        float px, pz, qx, qz;  // the edge, ordered so the wall faces the sea
    };
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
            if (j > 0 && !dry(i, j - 1)) walls.push_back({blk, x0e, z0e, x1e, z0e});  // sea to the north
            if (j < n - 1 && !dry(i, j + 1)) walls.push_back({blk, x1e, z1e, x0e, z1e});  // south
            if (i > 0 && !dry(i - 1, j)) walls.push_back({blk, x0e, z1e, x0e, z0e});  // west
            if (i < n - 1 && !dry(i + 1, j)) walls.push_back({blk, x1e, z0e, x1e, z1e});  // east
        }
    }
    // From the sea bed to above the boat's highest wall circle (d_a_ship.cpp: 0, 75 and 150 over it, and one at
    // y -600), so none passes under or over. Link passes through them (G_SHORE): he swims ashore anywhere.
    constexpr float WALL_BOTTOM = WATER_Y - 10.0f, WALL_TOP = 250.0f;
    // Vertices: every sample (simple, and the indices are the sample grid's), then 4 per wall.
    const u32 nVtx = (u32)nv * nv + (u32)walls.size() * 4;
    const u32 nTri = (u32)n * n * 2 + (u32)walls.size() * 2;
    int* blkTris = new int[(size_t)nb * nb]();
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            blkTris[(j / BLK) * nb + (i / BLK)] += 2;
        }
    }
    for (const Wall& w : walls) {
        blkTris[w.blk] += 2;
    }
    u32 nBlk = 0;
    for (int b = 0; b < nb * nb; b++) {
        nBlk += blkTris[b] != 0 ? 1 : 0;
    }
    // Tree: the leaves (one per block), then 8-way parents up to one root.
    u32 nTree = nBlk;
    for (u32 level = nBlk; level > 1;) {
        level = (level + 7) / 8;
        nTree += level;
    }
    if (nVtx > 0xFFFF || nTri > 0xFFFF || nTree > 0xFFFF) {
        fprintf(stderr, "[rplus] room %d: too many collision polygons (%u)\n", roomNo, nTri);
        delete[] blkTris;
        delete[] ys;
        return t;
    }

    const u32 offV = 0x34;
    const u32 offT = align4(offV + nVtx * 12);
    const u32 offB = align4(offT + nTri * 10);
    const u32 offTree = align4(offB + nBlk * 2);
    const u32 offG = align4(offTree + nTree * 0x14);
    const u32 offTi = align4(offG + 0x34);
    const u32 offName = offTi + G_COUNT * 0x10;
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
    o.u32at(0x28, G_COUNT);
    o.u32at(0x2C, offTi);
    o.u32at(0x30, 0);
    for (int j = 0; j < nv; j++) {
        for (int i = 0; i < nv; i++) {
            u32 at = offV + (u32)(j * nv + i) * 12;
            o.f32at(at, bx + i * step);
            o.f32at(at + 4, Y(i, j));
            o.f32at(at + 8, bz + j * step);
        }
    }
    for (size_t w = 0; w < walls.size(); w++) {
        const Wall& wl = walls[w];
        const float vx[4] = {wl.px, wl.px, wl.qx, wl.qx}, vy[4] = {WALL_BOTTOM, WALL_TOP, WALL_BOTTOM, WALL_TOP},
                    vz[4] = {wl.pz, wl.pz, wl.qz, wl.qz};
        for (int k = 0; k < 4; k++) {
            u32 at = offV + ((u32)nv * nv + (u32)w * 4 + k) * 12;
            o.f32at(at, vx[k]);
            o.f32at(at + 4, vy[k]);
            o.f32at(at + 8, vz[k]);
        }
    }
    u32 tri = 0, blk = 0;
    for (int bj = 0; bj < nb; bj++) {
        for (int bi = 0; bi < nb; bi++) {
            if (blkTris[bj * nb + bi] == 0) {
                continue;
            }
            o.u16at(offB + blk * 2, (u16)tri);
            blk++;
            for (int j = bj * BLK; j < bj * BLK + BLK && j < n; j++) {
                for (int i = bi * BLK; i < bi * BLK + BLK && i < n; i++) {
                    u16 a = (u16)(j * nv + i), b = (u16)((j + 1) * nv + i), c = (u16)(j * nv + i + 1),
                        d = (u16)((j + 1) * nv + i + 1);
                    u16 tv[2][3] = {{a, b, c}, {c, b, d}};
                    for (int k = 0; k < 2; k++) {
                        float ymid = (Y(i, j) + Y(i + 1, j + 1)) * 0.5f;
                        float slope = fabsf(Y(i + 1, j) - Y(i, j)) + fabsf(Y(i, j + 1) - Y(i, j));
                        float ny = step / sqrtf(step * step + slope * slope);
                        Ground g = groundOf(biomeNear(gx0 + (i + 0.5f) / SUB, gy0 + (j + 0.5f) / SUB), ymid, ny);
                        u32 at = offT + tri * 10;
                        o.u16at(at, tv[k][0]);
                        o.u16at(at + 2, tv[k][1]);
                        o.u16at(at + 4, tv[k][2]);
                        o.u16at(at + 6, (u16)g);
                        o.u16at(at + 8, 0);
                        tri++;
                    }
                }
            }
            // The block's shore walls: (p bottom, p top, q bottom) and (q bottom, p top, q top). The normal
            // (v1 - v0) x (v2 - v0) = up x (q - p), which points to the left of p -> q: the sea.
            const int thisBlk = bj * nb + bi;
            for (size_t w = 0; w < walls.size(); w++) {
                if (walls[w].blk != thisBlk) {
                    continue;
                }
                const u16 v = (u16)((u32)nv * nv + (u32)w * 4);
                const u16 tv[2][3] = {{v, (u16)(v + 1), (u16)(v + 2)}, {(u16)(v + 2), (u16)(v + 1), (u16)(v + 3)}};
                for (int k = 0; k < 2; k++) {
                    u32 at = offT + tri * 10;
                    o.u16at(at, tv[k][0]);
                    o.u16at(at + 2, tv[k][1]);
                    o.u16at(at + 4, tv[k][2]);
                    o.u16at(at + 6, (u16)G_SHORE);
                    o.u16at(at + 8, 0);
                    tri++;
                }
            }
        }
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
    for (int g = 0; g < G_COUNT; g++) {
        u32 at = offTi + g * 0x10;
        o.u32at(at + 0x0, 0x07FFE0FFu);
        o.u32at(at + 0x4, 0x000000FFu | (kGroundAttr[g] << 16));
        o.u32at(at + 0x8, 0xFFFFFFFFu);
        o.u32at(at + 0xC, kGroundPass[g]);
    }
    memcpy(blob + offName, "rplus", 6);
    delete[] blkTris;
    delete[] ys;

    cBgS::ConvDzb(blob);
    dBgW* bgw = new dBgW();
    if (bgw != nullptr && !bgw->Set((cBgD_t*)blob, cBgW::GLOBAL_e, NULL)) {
        *o_bgw = bgw;
    } else {
        fprintf(stderr, "[rplus] room %d: collision not set\n", roomNo);
    }
    t->dzb = blob;
    fprintf(stderr, "[rplus] room %d: %d triangles drawn, %u for collision (%zu shore walls) in %u blocks\n", roomNo,
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
    static int tries = 0, lastOk = -1;
    if (++tries <= 5 || tries % 300 == 0 || ok != lastOk) {
        lastOk = ok;
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

int pc_rplus_start_room(void) {
    if (sStartPointSet || !pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0 ||
        dComIfGp_getStartStagePoint() < 0) {
        return -1;
    }
    int cx, cy;
    pc_rplus_start_cell(&cx, &cy);
    return (cy / pc_rplus_cells()) * pc_rplus_squares() + (cx / pc_rplus_cells()) + 1;
}

void pc_rplus_start_point(u32* params, cXyz* pos) {
    if (pc_rplus_start_room() < 0) {
        return;
    }
    sStartPointSet = true;
    int cx, cy;
    pc_rplus_start_cell(&cx, &cy);
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    const int startRoom = (cy / pc_rplus_cells()) * pc_rplus_squares() + (cx / pc_rplus_cells()) + 1;
    pos->set(x0 + (cx + 0.5f) * cs, heightAt(cx + 0.5f, cy + 0.5f) + 10.0f, z0 + (cy + 0.5f) * cs);
    // The room bits pick the room the stage loads first (dStage_Create), so the start room's land and collision
    // are in before Link looks for ground (daPy_lk_c::makeBgWait). Standing start (mode 0), not on a raft (0x80).
    *params = (*params & ~0xF0BFu) | (u32)startRoom;
    fprintf(stderr, "[rplus] Link starts on the start cell %d,%d in room %d at %.0f %.0f %.0f\n", cx, cy, startRoom,
            pos->x, pos->y, pos->z);
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
    if (placed || sStartPointSet) {
        return;
    }
    int cx, cy;
    pc_rplus_start_cell(&cx, &cy);
    float x0, z0;
    worldOrigin(&x0, &z0);
    const float cs = cellSize();
    cXyz pos(x0 + (cx + 0.5f) * cs, 0.0f, z0 + (cy + 0.5f) * cs);
    pos.y = heightAt(cx + 0.5f, cy + 0.5f) + 150.0f;
    player->current.pos = pos;
    player->old.pos = pos;
    player->speedF = 0.0f;
    player->speed = cXyz(0.0f, 0.0f, 0.0f);
    // Held there until the start room's own land is loaded. The game loads rooms from the ground under Link
    // (dStage_RoomCheck), and there is none until the start room is in, so its load is asked for here.
    const int startRoom = (cy / pc_rplus_cells()) * pc_rplus_squares() + (cx / pc_rplus_cells()) + 1;
    if (roomNo != startRoom) {
        requestRoomLoad(startRoom);
        return;
    }
    placed = true;
    fprintf(stderr, "[rplus] Link put on the start cell %d,%d at %.0f %.0f %.0f\n", cx, cy, pos.x, pos.y, pos.z);
}
