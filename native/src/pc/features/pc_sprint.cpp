// Sprint with A button (QoL feature): Hold A while moving on ground to run faster,
// drains stamina. Tap A rolls or acts as normal. Refills when not sprinting.
// Shares stamina wheel with climbing when both are enabled.
#include "d/dolzel.h" // IWYU pragma: keep
#include "pc_internal.h"

#include "pc/pc_qol.h"
#include "pc/pc_settings.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_controller_pad.h"

#include <cmath>
#include <unistd.h>

namespace {

constexpr float kRefillSeconds = 3.0f;        // empty to full on the ground
constexpr int kRefillDelayFrames = 15;        // standing on the ground this long first
constexpr float kDrainRate = 1.0f;            // stamina drain per second while sprinting
constexpr float kFrameSeconds = 1.0f / 30.0f; // default game frame rate

float sSprintStamina = 1.0f;  // shared with climbing (0..1)
int sGroundFrames = 0;        // frames on ground (for stamina refill delay)
int sAButtonHeldFrames = 0;   // frames A button held (for hold vs tap detection)
unsigned sFrame = 0;
int sIsSprinting = 0;         // current sprint state (suppress attacks while true)

float staminaSeconds() {
    const float s = (float)atof(pc_settings_get("COS_QOL_CLIMB_STAMINA"));
    return s >= 4.0f && s <= 30.0f ? s : 12.0f;
}

} // namespace

// Check if A button is held long enough to sprint (not just a tap for roll)
// Sprint activates after holding A for ~0.3s (9 frames at 30fps) while moving
int pc_sprint_a_held(void) {
    if (!CPad_CHECK_HOLD_A(0)) {
        sAButtonHeldFrames = 0;  // reset hold counter if A is released
        return 0;
    }
    // Count frames holding A; sprint starts after ~9 frames (0.3s at 30fps)
    sAButtonHeldFrames++;
    return sAButtonHeldFrames > 9 ? 1 : 0;
}

// Each player frame (daPy_lk_c::execute): `sprinting` A is held long enough while Link runs on the ground and, with
// climbing on, the climbing wheel (pc_climb.cpp) has stamina; the wheel itself drains through pc_qol_climb_sprint.
void pc_sprint_frame(int sprinting, int ground, float deltaSeconds) {
    (void)ground;
    (void)deltaSeconds;
    sIsSprinting = pc_qol(PC_QOL_SPRINT) && sprinting ? 1 : 0;
    sSprintStamina = 1.0f;
}

// Shared stamina getter (used by climbing and sprint)
float pc_sprint_get_stamina(void) {
    return sSprintStamina;
}

// Shared stamina setter (used by climbing and sprint)
void pc_sprint_set_stamina(float value) {
    sSprintStamina = (value >= 0.0f && value <= 1.0f) ? value : sSprintStamina;
}

// Check if currently sprinting (used to suppress sword attacks)
int pc_sprint_is_active(void) {
    return sIsSprinting;
}

// Get sprint speed multiplier (1.0 for normal, 1.6 for sprinting with stamina)
float pc_sprint_speed_multiplier(void) {
    if (!sIsSprinting) {
        return 1.0f;
    }
    return 1.6f;  // 1.6x speed boost while sprinting with stamina
}
