// The quality-of-life options that have a button of their own (pc_qol.h, docs/QOL_PLAN.md): COS_QOL_HUD_BUTTON
// hides and shows the HUD, COS_QOL_ITEM_SETS swaps the X/Y/Z items with a second set. pc_qol_frame runs on
// the game thread once per frame (pc_frame.cpp), after the options menu, so the game sees a change on its
// next frame. The buttons are physical ones (pc_button_remap_input_held), not GameCube actions.
#include "d/dolzel.h" // IWYU pragma: keep
#include "pc/pc_qol.h"
#include "pc/pc_button_remap.h"
#include "pc/pc_settings.h"
#include "pc_internal.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter.h"
#include "JAZelAudio/JAZelAudio_SE.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_controller_pad.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

// ---- the buttons ---------------------------------------------------------------------------------

// One option's button: a press is the moment it goes down, except for a stick click, which counts when
// it is let go without the other stick having been clicked meanwhile (L3+R3 together is the tracker's).
struct Button {
    bool held = false;
    bool spoiled = false; // a stick click: the other stick was held during this press
};

bool stickClick(int button) {
    return button == PC_QOL_BUTTON_LSTICK || button == PC_QOL_BUTTON_RSTICK;
}

// True once per press of the option's button (port 0, the player's controller).
bool pressed(PcQol which, Button& b) {
    const int button = pc_qol(which);
    const bool held = button != 0 && pc_button_remap_input_held(0, pc_qol_button_input(button));
    bool press;
    if (stickClick(button)) {
        const int other = button == PC_QOL_BUTTON_LSTICK ? PC_QOL_BUTTON_RSTICK : PC_QOL_BUTTON_LSTICK;
        if (held && pc_button_remap_input_held(0, pc_qol_button_input(other))) {
            b.spoiled = true;
        }
        press = b.held && !held && !b.spoiled;
        if (!held) {
            b.spoiled = false;
        }
    } else {
        press = held && !b.held;
    }
    b.held = held;
    return press;
}

Button sHudButton;
Button sItemSetButton;
bool sHudHidden = false;

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

// ---- the second item set ---------------------------------------------------------------------------

// The other set of each save slot: the inventory slots (dInvSlot_*) of X, Y and Z, as the save keeps the
// equipped ones (dSv_player_status_a_c::mSelectItem). Kept in item_sets.txt next to settings.ini, one line
// per save slot: "<slot> <x> <y> <z>" in hex; ff is an empty button.
constexpr int kSlots = 3;
u8 sOtherSet[kSlots][dItemBtn_COUNT_e];
bool sLoaded = false;

std::string itemSetsPath() {
    std::string path = pc_settings_path();
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos ? std::string(".") : path.substr(0, slash)) + "/item_sets.txt";
}

void loadItemSets() {
    sLoaded = true;
    memset(sOtherSet, dInvSlot_NONE_e, sizeof(sOtherSet));
    FILE* f = fopen(itemSetsPath().c_str(), "r");
    if (f == nullptr) {
        return;
    }
    unsigned int slot, x, y, z;
    while (fscanf(f, "%u %x %x %x", &slot, &x, &y, &z) == 4) {
        if (slot < (unsigned int)kSlots) {
            sOtherSet[slot][dItemBtn_X_e] = (u8)x;
            sOtherSet[slot][dItemBtn_Y_e] = (u8)y;
            sOtherSet[slot][dItemBtn_Z_e] = (u8)z;
        }
    }
    fclose(f);
}

void saveItemSets() {
    const std::string path = itemSetsPath();
    FILE* f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        pc::writef(STDERR_FILENO, "[cos] qol: cannot write %s\n", path.c_str());
        return;
    }
    for (int slot = 0; slot < kSlots; slot++) {
        fprintf(f, "%d %02x %02x %02x\n", slot, sOtherSet[slot][dItemBtn_X_e], sOtherSet[slot][dItemBtn_Y_e],
                sOtherSet[slot][dItemBtn_Z_e]);
    }
    fclose(f);
}

// When the pause menu could open (d_menu_window.cpp, dMs_Execute's Start check): no menu, message, event,
// screen fade, telescope or picto box, the player is Link. The game already copes with X/Y/Z changing
// then (the item menu changes them), so the swap does no more than equipping from the menu.
bool canSwapItems() {
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    return player != nullptr && player == dComIfGp_getLinkPlayer() && dMenu_flag() == 0 && !fopOvlpM_IsDoingReq() &&
           dComIfGp_getMesgStatus() == 0 && dComIfGp_getScopeMesgStatus() == 0 && !dComIfGp_event_runCheck() &&
           !dComIfGp_isEnableNextStage() &&
           !dComIfGp_checkCameraAttentionStatus(0, dCamAttnStts_TELESCOPE_LOOK_e) &&
           !dComIfGp_checkCameraAttentionStatus(0, dCamAttnStts_PICTO_BOX_AIM_e) &&
           !dComIfGp_checkPlayerStatus0(0, daPyStts0_UNK800000_e) && !CPad_CHECK_HOLD_X(0) &&
           !CPad_CHECK_HOLD_Y(0) && !CPad_CHECK_HOLD_Z(0);
}

void swapItemSets() {
    if (!sLoaded) {
        loadItemSets();
    }
    const int slot = dComIfGs_getDataNum();
    if (slot < 0 || slot >= kSlots) {
        return;
    }
    for (int btn = 0; btn < dItemBtn_COUNT_e; btn++) {
        const u8 equipped = dComIfGs_getSelectItem(btn);
        dComIfGs_setSelectItem(btn, sOtherSet[slot][btn]);
        sOtherSet[slot][btn] = equipped;
        dComIfGp_setSelectItem(btn); // an inventory slot emptied since then becomes an empty button
    }
    saveItemSets();
    mDoAud_seStart(JA_SE_ITM_MENU_SET);
    pc::writef(STDERR_FILENO, "[cos] qol: item set swapped (slot %d): %02x %02x %02x\n", slot,
               dComIfGs_getSelectItem(dItemBtn_X_e), dComIfGs_getSelectItem(dItemBtn_Y_e),
               dComIfGs_getSelectItem(dItemBtn_Z_e));
}

} // namespace

extern "C" {

int pc_qol_hud_hidden(void) {
    return sHudHidden && pc_qol(PC_QOL_HUD_BUTTON) != 0;
}

void pc_qol_frame(void) {
    if (pc::menuOpen() || fpcM_Search(isPlayScene, nullptr) == nullptr) {
        // the buttons' state is still followed, so a press that opened or closed something is not a press here
        pressed(PC_QOL_HUD_BUTTON, sHudButton);
        pressed(PC_QOL_ITEM_SETS, sItemSetButton);
        return;
    }
    if (pressed(PC_QOL_HUD_BUTTON, sHudButton)) {
        sHudHidden = !sHudHidden;
        pc::writef(STDERR_FILENO, "[cos] qol: HUD %s\n", sHudHidden ? "hidden" : "shown");
    }
    if (pressed(PC_QOL_ITEM_SETS, sItemSetButton) && canSwapItems()) {
        swapItemSets();
    }
}

} // extern "C"
