// Sprint with B button (QoL feature): Hold B while moving on ground to run faster,
// drains the climbing stamina wheel. Refills when not sprinting.
// Shares stamina with climbing (COS_QOL_CLIMB) since climbing is a prerequisite.
#include "d/dolzel.h" // IWYU pragma: keep
#include "pc_internal.h"

#include "pc/pc_qol.h"
#include "pc/pc_settings.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"

#include <cmath>
#include <unistd.h>

namespace {

constexpr float kRefillSeconds = 3.0f;        // empty to full on the ground
constexpr int kRefillDelayFrames = 15;        // standing on the ground this long first
constexpr float kDrainRate = 1.0f;            // stamina drain per second while sprinting
constexpr float kFrameSeconds = 1.0f / 30.0f; // default game frame rate

float sSprintStamina = 1.0f;  // shared with climbing (0..1)
int sGroundFrames = 0;
unsigned sFrame = 0;

float staminaSeconds() {
    const float s = (float)atof(pc_settings_get("COS_QOL_CLIMB_STAMINA"));
    return s >= 4.0f && s <= 30.0f ? s : 12.0f;
}

} // namespace

extern "C" {

// Check if B button is held
int pc_sprint_b_held(void) {
    extern mDoCPd_cpadInfo_c g_mDoCPd_cpadInfo[4];
    return g_mDoCPd_cpadInfo[0].mButtonHold.b ? 1 : 0;
}

// Update sprint stamina state each frame
// Called from player frame update with: sprinting (B held + moving on ground), ground state
void pc_sprint_frame(int sprinting, int ground, float deltaSeconds) {
    if (!pc_qol(PC_QOL_CLIMB)) {
        sSprintStamina = 1.0f;
        return;
    }

    sFrame++;

    // Use real delta time if available, otherwise use frame-based estimate
    float dt = deltaSeconds > 0.0f ? deltaSeconds : kFrameSeconds;

    if (sprinting && ground) {
        // Drain stamina while sprinting
        sSprintStamina -= kDrainRate * dt / staminaSeconds();
        sGroundFrames = 0;
        if (sSprintStamina < 0.0f) {
            sSprintStamina = 0.0f;
        }
    } else if (!sprinting && ground) {
        // Refill stamina on ground
        if (++sGroundFrames > kRefillDelayFrames && sSprintStamina < 1.0f) {
            sSprintStamina += dt / kRefillSeconds;
            if (sSprintStamina > 1.0f) {
                sSprintStamina = 1.0f;
            }
        }
    } else {
        sGroundFrames = 0;
    }
}

// Shared stamina getter (used by climbing and sprint)
float pc_sprint_get_stamina(void) {
    return sSprintStamina;
}

// Shared stamina setter (used by climbing and sprint)
void pc_sprint_set_stamina(float value) {
    sSprintStamina = (value >= 0.0f && value <= 1.0f) ? value : sSprintStamina;
}

} // extern "C"
