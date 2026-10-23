// COS_QOL_SEA_MAP (pc_qol.h, docs/QOL_PLAN.md item 20): a small live chart of the Great Sea in the screen's
// bottom-right corner during play, drawn with Aurora's ImGui (like the frame-rate
// panel, pc_overlay.cpp). The 7x7 sectors, the ones reached shaded (the save's "arrive" bits, the same the
// sea chart menu reads; every sector with COS_QOL_REVEAL_CHART), the player's sector outlined, an arrow at
// the player's position pointing where they face, and the sector's name under the chart. Inside (a house, a
// dungeon) the sector last sailed in stays outlined, without the arrow. Hidden only with the HUD, in menus,
// cutscenes, screen fades, the telescope and the picto box.
//
// The sea: stage "sea", room 1-49 = sector A1..G7 row by row; a sector is 100000 units, the chart centred
// on 0 (dMap_c::point2Grid), x to the east (columns 1-7), z to the south (rows A-G).
#include "pc_internal.h"

#include "pc/pc_qol.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"

#include <imgui.h>

#include <cmath>
#include <cstring>

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

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

// The chart shows when the HUD would: on the sea, in play, no menu, message, event or screen fade, and the
// HUD not hidden by its button.
bool seaMapVisible() {
    if (!pc_qol(PC_QOL_SEA_MAP) || pc_qol_hud_hidden() || menuOpen() || fpcM_Search(isPlayScene, nullptr) == nullptr) {
        return false;
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    return player != nullptr && dMenu_flag() == 0 && !fopOvlpM_IsDoingReq() && !dComIfGp_event_runCheck() &&
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

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float cell = std::floor(display.y * 0.034f);
    const float size = cell * 7.0f;
    const float margin = std::floor(display.y * 0.04f);
    const float textH = ImGui::GetFontSize() + 4.0f;
    const ImVec2 o(display.x - margin - size, display.y - margin - size - textH);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 sea = IM_COL32(24, 62, 104, 170);
    const ImU32 reached = IM_COL32(222, 196, 132, 200);
    const ImU32 grid = IM_COL32(255, 255, 255, 60);
    const ImU32 frame = IM_COL32(255, 240, 200, 220);
    dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + size + 3, o.y + size + textH + 3), IM_COL32(0, 0, 0, 110),
                      6.0f);
    dl->AddRectFilled(o, ImVec2(o.x + size, o.y + size), sea);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            const int no = row * 7 + col;
            const ImVec2 a(o.x + col * cell, o.y + row * cell);
            const ImVec2 b(a.x + cell, a.y + cell);
            if (pc_qol(PC_QOL_REVEAL_CHART) || dComIfGs_isSaveArriveGrid(no)) {
                dl->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), reached, 2.0f);
            }
            dl->AddRect(a, b, grid);
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
    dl->AddRect(sa, ImVec2(sa.x + cell, sa.y + cell), IM_COL32(120, 255, 120, 255), 2.0f, 0, 2.0f);

    float mx = o.x + size / 2.0f + px / 100000.0f * cell;
    float my = o.y + size / 2.0f + pz / 100000.0f * cell;
    mx = mx < o.x ? o.x : mx > o.x + size ? o.x + size : mx;
    my = my < o.y ? o.y : my > o.y + size ? o.y + size : my;
    // angle 0 faces +z (south on the chart); the arrow's tip in that direction
    const float rad = angle * (3.14159265f / 32768.0f);
    const float dx = std::sin(rad), dz = std::cos(rad);
    const float len = cell * 0.45f;
    const ImVec2 tip(mx + dx * len, my + dz * len);
    const ImVec2 left(mx - dx * len * 0.5f + dz * len * 0.45f, my - dz * len * 0.5f - dx * len * 0.45f);
    const ImVec2 right(mx - dx * len * 0.5f - dz * len * 0.45f, my - dz * len * 0.5f + dx * len * 0.45f);
    if (onSea) {
        dl->AddTriangleFilled(tip, left, right, IM_COL32(255, 70, 50, 255));
        dl->AddTriangle(tip, left, right, IM_COL32(255, 255, 255, 230), 1.0f);
    }

    const int no = row * 7 + col;
    char label[48];
    snprintf(label, sizeof(label), "%c%d %s", 'A' + row, col + 1,
             pc_qol(PC_QOL_REVEAL_CHART) || dComIfGs_isSaveArriveGrid(no) ? kSectorNames[no] : "?");
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float tx = ts.x > size ? o.x + size - ts.x : o.x + (size - ts.x) / 2.0f;
    dl->AddText(ImVec2(tx, o.y + size + 3.0f), IM_COL32(255, 255, 255, 235), label);
}

} // namespace pc
