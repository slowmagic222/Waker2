// COS_QOL_CLIMB (pc_qol.h, docs/QOL_PLAN.md): Link climbs any plain wall as he climbs ivy, Breath of the
// Wild style, on a stamina wheel. The player's code does the climbing (d_a_player_main.cpp's
// setFrontWallType calls a plain wall ivy, d_a_player_climb.inc's setMoveBGCorrectClimb keeps holding
// it); this file keeps the wheel: it drains while Link climbs a plain wall (less while he hangs still),
// and once empty he lets go and cannot grab again until it has refilled on the ground. Ivy costs nothing.
// The wheel is drawn with Aurora's ImGui beside Link's shoulder, projected with the game's own camera.
// The idea and the numbers follow Bluewake's climbing (elliotttate/Wind-Waker-Recomp, runtime/host/src/
// climb.c), redone here in the decompiled game.
#include "d/dolzel.h" // IWYU pragma: keep
#include "pc_internal.h"

#include "pc/pc_aspect.h"
#include "pc/pc_qol.h"
#include "pc/pc_settings.h"
#include "d/d_com_inf_game.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"

#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <unistd.h>

namespace {

constexpr float kFrameSeconds = 1.0f / 30.0f; // the player's code runs once a game frame
constexpr float kHangShare = 0.4f;            // hanging still costs this share of climbing
constexpr float kRefillSeconds = 3.0f;        // empty to full on the ground
constexpr int kRefillDelayFrames = 15;        // standing on the ground this long first

float sStamina = 1.0f;  // 0..1
bool sExhausted = false; // ran out: no grabbing until full again
unsigned sFrame = 0;
unsigned sPlainFrame = 0; // the last frame Link held on to a plain wall
int sGroundFrames = 0;
float sAlpha = 0.0f; // the wheel's fade
float sShoulder[3] = {};

// COS_QOL_CLIMB_STAMINA: seconds of climbing on a full wheel (4 to 30, 12 by default).
float staminaSeconds() {
    const float s = (float)atof(pc_settings_get("COS_QOL_CLIMB_STAMINA"));
    return s >= 4.0f && s <= 30.0f ? s : 12.0f;
}

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

} // namespace

extern "C" {

int pc_qol_climb_can_grab(void) {
    return pc_qol(PC_QOL_CLIMB) && !sExhausted && sStamina > 0.0f;
}

void pc_qol_climb_plain(void) {
    sPlainFrame = sFrame;
}

void pc_qol_climb_frame(int climbing, int ground, int still, const float shoulder[3]) {
    if (!pc_qol(PC_QOL_CLIMB)) {
        sStamina = 1.0f;
        sExhausted = false;
        sAlpha = 0.0f;
        return;
    }
    sFrame++;
    // setMoveBGCorrectClimb kept him on a plain wall in his last update
    const bool plain = climbing && sFrame - sPlainFrame <= 2u;
    if (plain) {
        sStamina -= (still ? kHangShare : 1.0f) * kFrameSeconds / staminaSeconds();
        sGroundFrames = 0;
        if (sStamina <= 0.0f) {
            sStamina = 0.0f;
            sExhausted = true;
            pc::writef(STDERR_FILENO, "[cos] qol: climbing out of stamina, letting go\n");
        }
    } else if (!climbing && ground) {
        if (++sGroundFrames > kRefillDelayFrames && sStamina < 1.0f) {
            sStamina += kFrameSeconds / kRefillSeconds;
            if (sStamina >= 1.0f) {
                sStamina = 1.0f;
                sExhausted = false;
            }
        }
    } else {
        sGroundFrames = 0;
    }
    // the wheel shows while it is not full, and fades a second after
    const float target = plain || sStamina < 1.0f ? 1.0f : 0.0f;
    sAlpha += (target - sAlpha) * (target > sAlpha ? 0.5f : 0.06f);
    if (sAlpha < 0.01f) {
        sAlpha = 0.0f;
    }
    for (int i = 0; i < 3; i++) {
        sShoulder[i] = shoulder[i];
    }
}

} // extern "C"

namespace pc {

// In this frame's ImGui frame (pc_frame.cpp, next to the sea map): the wheel, a ring that empties
// clockwise, green, then red once it ran out and is refilling.
void climbWheelFrame() {
    if (ImGui::GetCurrentContext() == nullptr || !pc_qol(PC_QOL_CLIMB) || sAlpha <= 0.0f || menuOpen() ||
        pc_qol_hud_hidden() || fpcM_Search(isPlayScene, nullptr) == nullptr || dComIfGd_getView() == nullptr) {
        return;
    }
    // Link's shoulder through the camera's projection; the game's picture is fitted into the window at its
    // aspect (4:3, or the widescreen one, pc_aspect.h), so the wheel follows that rectangle.
    Mtx44& m = *dComIfGd_getProjViewMtx();
    const float* p = sShoulder;
    const float w = m[3][0] * p[0] + m[3][1] * p[1] + m[3][2] * p[2] + m[3][3];
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float nx = 0.0f, ny = 0.1f; // Link behind the camera: the picture's middle
    if (w > 1.0f) {
        nx = (m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3]) / w;
        ny = (m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3]) / w;
        if (std::fabs(nx) > 1.2f || std::fabs(ny) > 1.2f) {
            nx = 0.0f;
            ny = 0.1f;
        }
    }
    const float aspect = pc_aspect_wide() ? pc_aspect_ratio() : 4.0f / 3.0f;
    float pw = display.x, ph = display.x / aspect;
    if (ph > display.y) {
        ph = display.y;
        pw = ph * aspect;
    }
    const float r = std::floor(ph * 0.03f);
    // to the right of the shoulder
    const ImVec2 c((display.x - pw) / 2.0f + pw * (0.5f + 0.5f * nx) + r * 2.5f,
                   (display.y - ph) / 2.0f + ph * (0.5f - 0.5f * ny));
    const int a = (int)(sAlpha * 255.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float thick = std::fmax(3.0f, r * 0.45f);
    dl->AddCircleFilled(c, r + thick * 0.5f, IM_COL32(0, 0, 0, a * 90 / 255), 32);
    dl->PathArcTo(c, r, 0.0f, 6.2831853f, 40);
    dl->PathStroke(IM_COL32(255, 255, 255, a * 50 / 255), ImDrawFlags_Closed, thick);
    if (sStamina > 0.0f) {
        const float start = -1.5707963f;
        dl->PathArcTo(c, r, start, start + 6.2831853f * sStamina, 40);
        const ImU32 col = sExhausted ? IM_COL32(230, 70, 50, a) : sStamina < 0.25f ? IM_COL32(240, 180, 60, a)
                                                                                   : IM_COL32(110, 220, 90, a);
        dl->PathStroke(col, 0, thick);
    }
}

} // namespace pc
