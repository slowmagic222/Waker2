// COS_QOL_SEA_MAP (pc_qol.h, docs/QOL_PLAN.md item 20): a small live chart of the Great Sea in the screen's
// bottom-right corner during play, drawn with Aurora's ImGui (like the frame-rate
// panel, pc_overlay.cpp). The 7x7 sectors, the ones reached shaded (the save's "arrive" bits, the same the
// sea chart menu reads; every sector with COS_QOL_REVEAL_CHART), the player's sector outlined, an arrow at
// the player's position pointing where they face, and the sector's name under the chart. Inside (a house, a
// dungeon) the sector last sailed in stays outlined, without the arrow. Hidden only with the HUD, in menus,
// cutscenes, screen fades, the telescope and the picto box.
//
// The islands are the sea chart's own pictures: the zoomed-in sector's island texture (Fmap.arc's
// "R%02X_b.bti", dMenu_Fmap_c::changeFmapTexture), placed where the chart's check point data puts it in the
// sector (setDispIslandPos), read once and decoded from its GX format. A sector without one (open sea) keeps
// the simple shape (drawIsland).
//
// The sea: stage "sea", room 1-49 = sector A1..G7 row by row; a sector is 100000 units, the chart centred
// on 0 (dMap_c::point2Grid), x to the east (columns 1-7), z to the south (rows A-G).
#include "pc_internal.h"

#include "pc/pc_qol.h"
#include "pc/pc_settings.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "d/d_com_inf_game.h"
#include "d/d_map.h"
#include "d/d_meter.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"

#if __has_include("pc/pc_tracker.h")
// the in-game tracker (its own pull request): with it, each sector's checks in logic
#include "pc/pc_tracker.h"
#define PC_SEA_MAP_TRACKER 1
#endif

#include <aurora/imgui.h>
#include <imgui.h>

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace pc {

namespace {

const char* const kSectorNames[49] = {
    "Forsaken Fortress",        "Star Island",          "Northern Fairy Island",    "Gale Isle",
    "Crescent Moon Island",     "Seven-Star Isles",     "Overlook Island",          "Four-Eye Reef",
    "Mother and Child Isles",   "Spectacle Island",     "Windfall Island",          "Pawprint Isle",
    "Dragon Roost Island",      "Flight Control Platform", "Western Fairy Island",  "Rock Spire Isle",
    "Tingle Island",            "Northern Triangle Island", "Eastern Fairy Island", "Fire Mountain",
    "Star Belt Archipelago",    "Three-Eye Reef",       "Greatfish Isle",           "Cyclops Reef",
    "Six-Eye Reef",             "Tower of the Gods",    "Eastern Triangle Island",  "Thorned Fairy Island",
    "Needle Rock Isle",         "Islet of Steel",       "Stone Watcher Island",     "Southern Triangle Island",
    "Private Oasis",            "Bomb Island",          "Bird's Peak Rock",         "Diamond Steppe Island",
    "Five-Eye Reef",            "Shark Island",         "Southern Fairy Island",    "Ice Ring Isle",
    "Forest Haven",             "Cliff Plateau Isles",  "Horseshoe Island",         "Outset Island",
    "Headstone Island",         "Two-Eye Reef",         "Angular Isles",            "Boating Course",
    "Five-Star Isles",
};

// How a sector's island is drawn, from its name: a reef is a ring of rocks, a fairy island a dot, a group
// of isles three dots, a big island a bigger blob.
void drawIsland(ImDrawList* dl, int no, ImVec2 c, float cell, ImU32 land, ImU32 rim) {
    const char* name = kSectorNames[no];
    const float r = cell * 0.22f;
    if (strstr(name, "Reef") != nullptr) {
        dl->AddCircle(c, r * 1.1f, land, 10, cell * 0.08f);
    } else if (strstr(name, "Fairy") != nullptr) {
        dl->AddCircleFilled(c, r * 0.5f, land);
    } else if (strstr(name, "Isles") != nullptr || strstr(name, "Archipelago") != nullptr) {
        dl->AddCircleFilled(ImVec2(c.x - r * 0.8f, c.y + r * 0.4f), r * 0.45f, land);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.8f, c.y + r * 0.4f), r * 0.45f, land);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.6f), r * 0.45f, land);
    } else {
        static const int kBig[] = {0, 10, 12, 19, 25, 39, 40, 43}; // FF, Windfall, Dragon Roost, Fire Mountain,
                                                                    // Tower, Ice Ring, Forest Haven, Outset
        bool big = false;
        for (int b : kBig) {
            big = big || b == no;
        }
        const float rr = big ? r * 1.35f : r;
        dl->AddCircleFilled(c, rr, land);
        dl->AddCircle(c, rr, rim, 0, 1.0f);
    }
}

// A GX texture (a BTI's image, as ResTIMG describes it) decoded to RGBA: the formats the menus use
bool decodeGx(int format, int w, int h, const u8* src, const u8* palette, int paletteFormat, int colors,
              std::vector<uint8_t>& out) {
    out.assign(size_t(w) * h * 4, 0);
    const auto rgb565 = [](u16 v, u8* p) {
        p[0] = u8(((v >> 11) & 31) * 255 / 31);
        p[1] = u8(((v >> 5) & 63) * 255 / 63);
        p[2] = u8((v & 31) * 255 / 31);
        p[3] = 255;
    };
    const auto rgb5a3 = [](u16 v, u8* p) {
        if (v & 0x8000) {
            p[0] = u8(((v >> 10) & 31) * 255 / 31);
            p[1] = u8(((v >> 5) & 31) * 255 / 31);
            p[2] = u8((v & 31) * 255 / 31);
            p[3] = 255;
        } else {
            p[0] = u8(((v >> 8) & 15) * 17);
            p[1] = u8(((v >> 4) & 15) * 17);
            p[2] = u8((v & 15) * 17);
            p[3] = u8(((v >> 12) & 7) * 255 / 7);
        }
    };
    const auto be16 = [](const u8* p) { return u16((p[0] << 8) | p[1]); };
    const auto paletteColor = [&](int index, u8* p) {
        if (palette == nullptr || index >= colors) {
            return;
        }
        const u16 v = be16(palette + index * 2);
        if (paletteFormat == GX_TL_IA8) {
            p[0] = p[1] = p[2] = u8(v & 0xFF);
            p[3] = u8(v >> 8);
        } else if (paletteFormat == GX_TL_RGB565) {
            rgb565(v, p);
        } else {
            rgb5a3(v, p);
        }
    };
    int bw, bh;
    switch (format) {
    case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: bw = 8; bh = 8; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: bw = 8; bh = 4; break;
    case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_RGBA8: bw = 4; bh = 4; break;
    default: return false;
    }
    const auto texel = [&](int x, int y) -> u8* {
        static u8 dummy[4];
        return x < w && y < h ? &out[(size_t(y) * w + x) * 4] : dummy;
    };
    const u8* at = src;
    for (int by = 0; by < h; by += bh) {
        for (int bx = 0; bx < w; bx += bw) {
            if (format == GX_TF_CMPR) {
                // four 4x4 DXT1 blocks
                for (int sub = 0; sub < 4; sub++, at += 8) {
                    const u16 c0 = be16(at), c1 = be16(at + 2);
                    u8 pal[4][4];
                    rgb565(c0, pal[0]);
                    rgb565(c1, pal[1]);
                    for (int k = 0; k < 3; k++) {
                        if (c0 > c1) {
                            pal[2][k] = u8((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = u8((pal[0][k] + 2 * pal[1][k]) / 3);
                        } else {
                            pal[2][k] = u8((pal[0][k] + pal[1][k]) / 2);
                            pal[3][k] = 0;
                        }
                    }
                    pal[2][3] = 255;
                    pal[3][3] = c0 > c1 ? 255 : 0;
                    for (int y = 0; y < 4; y++) {
                        const u8 bits = at[4 + y];
                        for (int x = 0; x < 4; x++) {
                            memcpy(texel(bx + (sub & 1) * 4 + x, by + (sub >> 1) * 4 + y), pal[(bits >> (6 - x * 2)) & 3], 4);
                        }
                    }
                }
                continue;
            }
            if (format == GX_TF_RGBA8) {
                // the AR halves, then the GB halves
                for (int i = 0; i < 16; i++) {
                    u8* p = texel(bx + i % 4, by + i / 4);
                    p[3] = at[i * 2];
                    p[0] = at[i * 2 + 1];
                    p[1] = at[32 + i * 2];
                    p[2] = at[32 + i * 2 + 1];
                }
                at += 64;
                continue;
            }
            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    u8* p = texel(bx + x, by + y);
                    const int i = y * bw + x;
                    switch (format) {
                    case GX_TF_I4: {
                        const u8 v = u8(((i & 1) == 0 ? at[i / 2] >> 4 : at[i / 2] & 15) * 17);
                        p[0] = p[1] = p[2] = p[3] = v;
                        break;
                    }
                    case GX_TF_I8: p[0] = p[1] = p[2] = p[3] = at[i]; break;
                    case GX_TF_IA4:
                        p[0] = p[1] = p[2] = u8((at[i] & 15) * 17);
                        p[3] = u8((at[i] >> 4) * 17);
                        break;
                    case GX_TF_IA8:
                        p[3] = at[i * 2];
                        p[0] = p[1] = p[2] = at[i * 2 + 1];
                        break;
                    case GX_TF_RGB565: rgb565(be16(at + i * 2), p); break;
                    case GX_TF_RGB5A3: rgb5a3(be16(at + i * 2), p); break;
                    case GX_TF_C4: paletteColor((i & 1) == 0 ? at[i / 2] >> 4 : at[i / 2] & 15, p); break;
                    case GX_TF_C8: paletteColor(at[i], p); break;
                    case GX_TF_C14X2: paletteColor(be16(at + i * 2) & 0x3FFF, p); break;
                    }
                }
            }
            at += 32;
        }
    }
    return true;
}

// A sector's island picture from the sea chart: loaded once (a few sectors a frame), then kept
struct IslandArt {
    enum { Unread, Ready, None } state = Unread;
    ImTextureID tex = ImTextureID{};
    int w = 0, h = 0;
    bool grey = false;  // an intensity texture (I4/I8/IA4/IA8): tinted with the land colour
    float ox = 0, oy = 0; // where the island sits in its sector, in units (x east, y south)
};
IslandArt sIslands[49];
// How much of a cell an island's picture fills (its larger side); tune here if the islands look too big
constexpr float kIslandFit = 0.9f;

void readIsland(int no) {
    IslandArt& art = sIslands[no];
    art.state = IslandArt::None;
    // the chart's check point for the sector: where the island is in it (dMenu_Fmap_c::setDispIslandPos)
    const dMap_FmapChkPnt* chk = static_cast<const dMap_FmapChkPnt*>(dComIfGp_getFmapData());
    const int gx = no % 7 - 3, gy = no / 7 - 3;
    bool found = false;
    for (u32 i = 0; chk != nullptr && i < u32(chk->field_0x0) && i < 64; i++) {
        const FmapChkPnt& p = chk->field_0x4[i];
        if (p.mSectorX == gx && p.mSectorY == gy) {
            art.ox = s16(p.field_0x2);
            art.oy = s16(p.field_0x4);
            found = true;
            break;
        }
    }
    if (!found) {
        return; // the chart shows no island picture there either
    }
    char name[16];
    snprintf(name, sizeof(name), "R%02X_b.bti", no + 1);
    alignas(32) static u8 sBuf[0x2c00]; // the size the chart's own buffer has (mChkPntTxt_p)
    const u32 size = JKRReadTypeResource(sBuf, sizeof sBuf, 'FM  ', name, dComIfGp_getFmapArchive());
    if (size < sizeof(ResTIMG)) {
        return;
    }
    const ResTIMG* t = reinterpret_cast<const ResTIMG*>(sBuf);
    const int w = u16(t->width), h = u16(t->height);
    const u32 image = u32(t->imageOffset) != 0 ? u32(t->imageOffset) : sizeof(ResTIMG);
    if (w <= 0 || h <= 0 || w > 512 || h > 512 || image >= size) {
        return;
    }
    const u8* palette = t->indexTexture && u32(t->paletteOffset) < size ? sBuf + u32(t->paletteOffset) : nullptr;
    std::vector<uint8_t> rgba;
    if (!decodeGx(t->format, w, h, sBuf + image, palette, t->colorFormat, u16(t->numColors), rgba)) {
        return;
    }
    art.tex = aurora_imgui_add_texture(u32(w), u32(h), rgba.data());
    art.w = w;
    art.h = h;
    art.grey = t->format == GX_TF_I4 || t->format == GX_TF_I8 || t->format == GX_TF_IA4 || t->format == GX_TF_IA8;
    art.state = IslandArt::Ready;
}

// The pictures not read yet, a few a frame (the archive is in ARAM: each read is a copy)
void readIslands() {
    if (dComIfGp_getFmapArchive() == nullptr || dComIfGp_getFmapData() == nullptr) {
        return;
    }
    int budget = 4;
    for (int no = 0; no < 49 && budget > 0; no++) {
        if (sIslands[no].state == IslandArt::Unread) {
            readIsland(no);
            budget--;
        }
    }
}

// The checks in logic (the tracker's logic: not checked and reachable now) of a sector's island and of the
// dungeon on it, -1 without the tracker or its logic. The dungeons, in the tracker's order (its areas 49-55):
// DRC on Dragon Roost, FW on Forest Haven, TotG and Ganon's Tower (under Hyrule) on the Tower of the Gods'
// sector, FF on the Forsaken Fortress, ET on Headstone Island, WT on Gale Isle.
void sectorChecks(int no, int& island, int& dungeon) {
    island = dungeon = -1;
#if PC_SEA_MAP_TRACKER
    static const int kDungeonSector[7] = {12, 40, 25, 0, 44, 3, 25};
    const pc::tracker::LogicResult& logic = pc::tracker::logicResult();
    if (!logic.ready || logic.areas.size() < 56) {
        return;
    }
    island = logic.areas[no].available;
    for (int d = 0; d < 7; d++) {
        if (kDungeonSector[d] == no) {
            dungeon = (dungeon < 0 ? 0 : dungeon) + logic.areas[49 + d].available;
        }
    }
#else
    (void)no;
#endif
}

// A count in a small dark badge, its corner at p (right: p is the badge's right edge)
void drawCount(ImDrawList* dl, int n, ImVec2 p, bool right, float fontSize, ImU32 bg, ImU32 fg) {
    char t[8];
    snprintf(t, sizeof t, "%d", n);
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, t);
    const float x = right ? p.x - ts.x - 2.0f : p.x;
    dl->AddRectFilled(ImVec2(x - 1.0f, p.y), ImVec2(x + ts.x + 3.0f, p.y + ts.y), bg, 3.0f);
    dl->AddText(font, fontSize, ImVec2(x + 1.0f, p.y), fg, t);
}

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

// The chart shows when the HUD would: on the sea, in play, no menu, message, event or screen fade, and the
// HUD not hidden by its button.
// COS_QOL_SEA_MAP_WHEN: "always" (all through play), "sea" (on the stage "sea", islands included), "boat"
// (riding the boat).
bool seaMapVisible() {
    if (!pc_qol(PC_QOL_SEA_MAP) || pc_qol_hud_hidden() || menuOpen() || fpcM_Search(isPlayScene, nullptr) == nullptr) {
        return false;
    }
#if PC_SEA_MAP_TRACKER
    if (pc::tracker::overlayShown()) {
        return false; // the tracker's screen is open (it has its own sea chart)
    }
#endif
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
        return false;
    }
    const char* when = pc_settings_get("COS_QOL_SEA_MAP_WHEN");
    if (strcmp(when, "boat") == 0 && !dComIfGp_checkPlayerStatus0(0, daPyStts0_SHIP_RIDE_e)) {
        return false;
    }
    if (strcmp(when, "sea") == 0 && strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        return false;
    }
    return dMenu_flag() == 0 && !fopOvlpM_IsDoingReq() && !dComIfGp_event_runCheck() &&
           !dComIfGp_checkCameraAttentionStatus(0, dCamAttnStts_TELESCOPE_LOOK_e) &&
           !dComIfGp_checkCameraAttentionStatus(0, dCamAttnStts_PICTO_BOX_AIM_e);
}

} // namespace

// COS_QOL_AUTOSAVE's notice (pc_qol_notice): a word in the bottom-left corner for two seconds.
void noticeFrame() {
    const char* notice = pc_qol_notice();
    if (notice == nullptr) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float margin = std::floor(display.y * 0.04f);
    const ImVec2 ts = ImGui::CalcTextSize(notice);
    const ImVec2 p(margin, display.y - margin - ts.y);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(p.x - 6, p.y - 4), ImVec2(p.x + ts.x + 6, p.y + ts.y + 4), IM_COL32(0, 0, 0, 130), 6.0f);
    dl->AddText(p, IM_COL32(255, 240, 200, 240), notice);
}

void seaMapFrame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    noticeFrame();
    if (!seaMapVisible()) {
        return;
    }
    readIslands();
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    // On the sea (islands included: they are rooms of the stage "sea") the player's own position; inside
    // (a house, a cave, a dungeon) the last one on the sea, without the arrow.
    static float sLastX = 0.0f, sLastZ = 0.0f;
    static bool sHaveLast = false;
    const bool onSea = strcmp(dComIfGp_getStartStageName(), "sea") == 0;
    if (onSea) {
        sLastX = player->current.pos.x;
        sLastZ = player->current.pos.z;
        sHaveLast = true;
    }
    const float px = sLastX;
    const float pz = sLastZ;
    const short angle = player->shape_angle.y;

    // COS_QOL_SEA_MAP_SIZE (a scale, 1 = 7 cells of 3.4% of the screen's height), _POS (the corner or side),
    // _OPACITY (percent): read every frame, so a change in the menu shows at once
    float scale = (float)atof(pc_settings_get("COS_QOL_SEA_MAP_SIZE"));
    scale = scale < 0.5f || scale > 3.0f ? 1.0f : scale;
    int opacity = atoi(pc_settings_get("COS_QOL_SEA_MAP_OPACITY"));
    opacity = opacity < 10 || opacity > 100 ? 100 : opacity;
    const char* pos = pc_settings_get("COS_QOL_SEA_MAP_POS");
    auto C = [opacity](int r, int g, int b, int a) { return IM_COL32(r, g, b, a * opacity / 100); };

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float cell = std::floor(display.y * 0.034f * scale);
    const float size = cell * 7.0f;
    const float margin = std::floor(display.y * 0.04f);
    const float textH = ImGui::GetFontSize() + 4.0f;
    const bool left = pos[0] == 'l' || strncmp(pos, "top-left", 8) == 0 || strncmp(pos, "bottom-left", 11) == 0;
    const bool top = strncmp(pos, "top", 3) == 0;
    const bool middle = strcmp(pos, "left") == 0 || strcmp(pos, "right") == 0;
    const float ox = left ? margin : display.x - margin - size;
    const float oy = top ? margin : middle ? std::floor((display.y - size - textH) / 2.0f) : display.y - margin - size - textH;
    const ImVec2 o(ox, oy);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 sea = C(24, 62, 104, 170);
    const ImU32 reached = C(70, 132, 186, 200); // a lighter sea where reached
    const ImU32 grid = C(255, 255, 255, 60);
    const ImU32 frame = C(255, 240, 200, 220);
    dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + size + 3, o.y + size + textH + 3), C(0, 0, 0, 110),
                      6.0f);
    dl->AddRectFilled(o, ImVec2(o.x + size, o.y + size), sea);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            const int no = row * 7 + col;
            const ImVec2 a(o.x + col * cell, o.y + row * cell);
            const ImVec2 b(a.x + cell, a.y + cell);
            const bool known = pc_qol(PC_QOL_REVEAL_CHART) || dComIfGs_isSaveArriveGrid(no);
            if (known) {
                dl->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), reached, 2.0f);
            }
            dl->AddRect(a, b, grid);
            // the island: the chart's picture, fitted to the cell as the chart fits it to its pane, at its place
            // in the sector; full colour where reached, a faint silhouette elsewhere
            const IslandArt& art = sIslands[no];
            if (art.state == IslandArt::Ready) {
                const float fit = cell * kIslandFit / (float)(art.w > art.h ? art.w : art.h);
                const ImVec2 c(a.x + cell / 2.0f + art.ox / 100000.0f * cell, a.y + cell / 2.0f + art.oy / 100000.0f * cell);
                const ImVec2 half(art.w * fit / 2.0f, art.h * fit / 2.0f);
                const ImU32 tint = known ? (art.grey ? C(150, 205, 110, 255) : C(255, 255, 255, 255))
                                         : C(255, 255, 255, 70);
                dl->PushClipRect(a, b, true);
                dl->AddImage(art.tex, ImVec2(c.x - half.x, c.y - half.y), ImVec2(c.x + half.x, c.y + half.y), ImVec2(0, 0),
                             ImVec2(1, 1), tint);
                dl->PopClipRect();
            } else {
                // not read yet or no picture: a simple shape, in land colours where reached, a faint outline elsewhere
                drawIsland(dl, no, ImVec2(a.x + cell / 2.0f, a.y + cell / 2.0f), cell,
                           known ? C(96, 150, 70, 235) : C(255, 255, 255, 45), known ? C(60, 40, 20, 200) : C(0, 0, 0, 0));
            }
            // the checks in logic there (the tracker's: the island's and its dungeon's together), when any, in
            // the cell's bottom left
            int island, dungeon;
            sectorChecks(no, island, dungeon);
            const int open = (island > 0 ? island : 0) + (dungeon > 0 ? dungeon : 0);
            const float fs = std::floor(cell * 0.36f) < 9.0f ? 9.0f : std::floor(cell * 0.36f);
            if (open > 0) {
                drawCount(dl, open, ImVec2(a.x + 2.0f, b.y - fs - 1.0f), false, fs, C(0, 0, 0, 170),
                          C(140, 205, 255, 255));
            }
        }
    }
    dl->AddRect(o, ImVec2(o.x + size, o.y + size), frame, 0.0f, 0, 1.5f);

    // the player's sector (clamped to the chart, as point2Grid does) and position
    int col = (int)std::floor(px / 100000.0f + 0.5f) + 3;
    int row = (int)std::floor(pz / 100000.0f + 0.5f) + 3;
    col = col < 0 ? 0 : col > 6 ? 6 : col;
    row = row < 0 ? 0 : row > 6 ? 6 : row;
    if (!sHaveLast) {
        return; // not on the sea yet since the game started: no place to mark
    }
    const ImVec2 sa(o.x + col * cell, o.y + row * cell);
    dl->AddRect(sa, ImVec2(sa.x + cell, sa.y + cell), C(120, 255, 120, 255), 2.0f, 0, 2.0f);

    float mx = o.x + size / 2.0f + px / 100000.0f * cell;
    float my = o.y + size / 2.0f + pz / 100000.0f * cell;
    mx = mx < o.x ? o.x : mx > o.x + size ? o.x + size : mx;
    my = my < o.y ? o.y : my > o.y + size ? o.y + size : my;
    // angle 0 faces +z (south on the chart); the arrow's tip in that direction
    const float rad = angle * (3.14159265f / 32768.0f);
    const float dx = std::sin(rad), dz = std::cos(rad);
    const float len = cell * 0.45f;
    const ImVec2 tip(mx + dx * len, my + dz * len);
    const ImVec2 wingL(mx - dx * len * 0.5f + dz * len * 0.45f, my - dz * len * 0.5f - dx * len * 0.45f);
    const ImVec2 wingR(mx - dx * len * 0.5f - dz * len * 0.45f, my - dz * len * 0.5f + dx * len * 0.45f);
    if (onSea) {
        dl->AddTriangleFilled(tip, wingL, wingR, C(255, 70, 50, 255));
        dl->AddTriangle(tip, wingL, wingR, C(255, 255, 255, 230), 1.0f);
    }

    const int no = row * 7 + col;
    char label[48];
    snprintf(label, sizeof(label), "%c%d %s", 'A' + row, col + 1,
             pc_qol(PC_QOL_REVEAL_CHART) || dComIfGs_isSaveArriveGrid(no) ? kSectorNames[no] : "?");
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float tx = ts.x > size ? o.x + size - ts.x : o.x + (size - ts.x) / 2.0f;
    dl->AddText(ImVec2(tx, o.y + size + 3.0f), C(255, 255, 255, 235), label);
}

} // namespace pc
