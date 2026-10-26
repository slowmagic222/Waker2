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
#include "f_op/f_op_actor_mng.h"
#include "SSystem/SComponent/c_bg_s.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "dolphin/gx/GX.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

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
enum Ground { G_GRASS, G_STONE, G_SAND, G_DIRT, G_COUNT };
const u32 kGroundAttr[G_COUNT] = {0x04, 0x03, 0x0B, 0x01};

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

PcRplusTerrain* pc_rplus_terrain_create(int roomNo, dBgW** o_bgw) {
    *o_bgw = nullptr;
    if (strcmp(dComIfGp_getStartStageName(), "sea") != 0 || !pc_rplus_sea_room_generated(roomNo)) {
        return nullptr;
    }
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

    PcRplusTerrain* t = new PcRplusTerrain();
    if (t == nullptr) {
        delete[] ys;
        return nullptr;
    }
    // ---- Drawing: triangles by sample row (the land and the shallows; the sea bed is not drawn).
    PcRplusPacket& pk = t->packet;
    pk.mVtx = new Vtx[(size_t)quads * 6];
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

    // ---- Collision: a DZB in the disc's layout, over the whole room: the land and the sea bed. Every room
    // needs ground somewhere under any point, or Link, made at a spawn point over open water, waits for ground
    // forever (daPy_lk_c::makeBgWait) and the game never starts.
    const int nb = (n + BLK - 1) / BLK;  // blocks per side
    // Vertices: every sample (simple, and the indices are the sample grid's).
    const u32 nVtx = (u32)nv * nv;
    const u32 nTri = (u32)n * n * 2;
    int* blkTris = new int[(size_t)nb * nb]();
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            blkTris[(j / BLK) * nb + (i / BLK)] += 2;
        }
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
        o.u32at(at + 0xC, 0);
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
    fprintf(stderr, "[rplus] room %d: %d triangles drawn, %u for collision in %u blocks\n", roomNo, pk.mTriCount,
            nTri, nBlk);
    return t;
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
        fprintf(stderr, "[rplus] loading the start room %d (room table entry %d)\n", roomNo, entry);
    }
    if (entry < 0) {
        return;
    }
    roomRead_data_class* d = rr->m_entries[entry];
    dComIfGp_roomControl_loadRoom(d->num, d->m_rooms);
}

} // namespace

int pc_rplus_keep_room_actor(const char* name) {
    // The King of Red Lions: Link starting at sea waits for him (daPy_lk_c::makeBgWait).
    if (strcmp(name, "Ship") == 0) {
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
    const float ground = heightAt(gx, gy);
    if (pos->y < ground + 10.0f) {
        fprintf(stderr, "[rplus] spawn point %.0f %.0f %.0f is under the land: lifted to %.0f\n", pos->x, pos->y,
                pos->z, ground + 10.0f);
        pos->y = ground + 10.0f;
    }
}

void pc_rplus_place_player(int roomNo) {
    static bool placed = false;
    if (placed || !pc_rplus_active() || strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return;
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
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
