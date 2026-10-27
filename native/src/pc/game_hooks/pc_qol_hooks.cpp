// The quality-of-life options that act from outside the game's own code (pc_qol.h, docs/QOL_PLAN.md):
// COS_QOL_HUD_BUTTON hides and shows the HUD, COS_QOL_ITEM_SETS swaps the X/Y/Z items with a second set,
// COS_QOL_JUMP_BUTTON hands a press to the player's code (daPy_lk_c::execute takes it),
// COS_QOL_AUTOSAVE saves the game as its save screen does. pc_qol_frame runs on
// the game thread once per frame (pc_frame.cpp), after the options menu, so the game sees a change on its
// next frame. The buttons are physical ones (pc_button_remap_input_held), not GameCube actions.
#include "d/dolzel.h" // IWYU pragma: keep
#include "pc/pc_qol.h"
#include "pc/pc_button_remap.h"
#include "pc/pc_settings.h"
#include "pc_internal.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter.h"
#include "d/d_stage.h"
#include "JAZelAudio/JAIZelBasic.h"
#include "JAZelAudio/JAZelAudio_SE.h"
#include "JSystem/JAudio/JAISound.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_MemCard.h"
#include "m_Do/m_Do_MemCardRWmng.h"

#if __has_include("pc/pc_tracker.h")
#include "pc/pc_tracker.h" // the in-game tracker (its own pull request): its screen
#define PC_QOL_TRACKER 1
#endif

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

// The tracker's screen is open: the game is paused under it and its buttons are the tracker's
bool trackerOpen() {
#if PC_QOL_TRACKER
    return pc::tracker::overlayShown();
#else
    return false;
#endif
}

Button sHudButton;
Button sItemSetButton;
Button sJumpButton;
bool sHudHidden = false;
// A jump press waiting for Link's next frame: the frames it still has (pc_qol_jump_take).
int sJumpFrames = 0;

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

// ---- autosave ------------------------------------------------------------------------------------------

// What counts as "a new item": the inventory without the bottles (their contents change all the time), the
// collection (sword, shield, pearls, Triforce, songs, ...), and the heart and magic maximums.
struct Progress {
    u8 items[dInvSlot_ItemLast_e];
    u8 collect[sizeof(dSv_player_collect_c)];
    u16 maxLife;
    u8 maxMagic;
};

void readProgress(Progress& p) {
    memset(&p, 0, sizeof(p));
    for (int slot = 0; slot < dInvSlot_ItemLast_e; slot++) {
        const bool bottle = slot >= dInvSlot_BOTTLE0_e && slot <= dInvSlot_BOTTLE3_e;
        p.items[slot] = bottle ? 0 : dComIfGs_getItem(slot);
    }
    memcpy(p.collect, &g_dComIfG_gameInfo.save.getPlayer().getCollect(), sizeof(p.collect));
    p.maxLife = g_dComIfG_gameInfo.save.getPlayer().getPlayerStatusA().getMaxLife();
    p.maxMagic = g_dComIfG_gameInfo.save.getPlayer().getPlayerStatusA().getMaxMagic();
}

constexpr unsigned int kAutosaveTimerFrames = 30 * 60 * 10; // 10 minutes of play at 30 game frames a second
constexpr unsigned int kAutosaveMinGap = 30 * 30;           // at least 30 seconds between two autosaves
constexpr unsigned int kAutosaveNoticeFrames = 30 * 2;

enum class SaveStep { Idle, Loading, Saving };

struct Autosave {
    bool known = false; // `last` holds this play's progress
    Progress last;
    int dataNum = -1;
    bool itemPending = false;
    unsigned int playFrames = 0;    // since the last save (autosave or the game's own: see below)
    unsigned int sinceLast = kAutosaveMinGap;
    SaveStep step = SaveStep::Idle;
    unsigned int stepFrames = 0;
    unsigned int noticeFrames = 0;
    bool loggedNoFile = false;
    alignas(32) u8 card[3 * sizeof(card_gamedata)];
} sAuto;

// The save screen's own test (dMenu_save_c::memCardDataSave): a new file, or the file this card holds.
bool saveAllowed() {
    if (dComIfGs_getNewFile() == 1) {
        return true;
    }
    return dComIfGs_getNoFile() == 0 && dComIfGs_getMemCardCheckID() == mDoMemCd_getCardSerialNo();
}

// The card thread idle and the card ready (getStatus 2), as the save screen requires before it saves.
bool cardIdle() {
    return mDoMemCd_isCardCommNone() && mDoMemCd_getStatus(0) == 2;
}

// Writes this file into the card image just read, as dMenu_save_c::memCardDataSave and dataWrite do, and
// starts the write. The recollection data (the boss refights' copy of the player) is exchanged back at
// once rather than after the write: the copy in the card image is what the save screen writes.
void startWrite() {
    dComIfGs_exchangePlayerRecollectionData();
    dComIfGs_putSave(dStage_stagInfo_GetSaveTbl(dComIfGp_getStage().getStagInfo()));
    dComIfGs_setGameStartStage();
    const int res = dComIfGs_setMemoryToCard(sAuto.card, dComIfGs_getDataNum());
    dComIfGs_exchangePlayerRecollectionData();
    if (res == -1) {
        pc::writef(STDERR_FILENO, "[cos] qol: autosave: memory_to_card failed\n");
        sAuto.step = SaveStep::Idle;
        return;
    }
    mDoMemCdRWm_SetCheckSumGameData(sAuto.card, dComIfGs_getDataNum());
    mDoMemCd_Save(sAuto.card, sizeof(sAuto.card), 0);
    sAuto.step = SaveStep::Saving;
    sAuto.stepFrames = 0;
}

void autosaveFrame(bool safe) {
    const int mode = pc_qol(PC_QOL_AUTOSAVE);
    Progress now;
    readProgress(now);
    if (!sAuto.known || sAuto.dataNum != dComIfGs_getDataNum()) {
        // a file was loaded (or the option turned on): what it has now is the starting point
        sAuto.known = true;
        sAuto.last = now;
        sAuto.dataNum = dComIfGs_getDataNum();
        sAuto.itemPending = false;
        sAuto.playFrames = 0;
    } else if (memcmp(&now, &sAuto.last, sizeof(now)) != 0) {
        sAuto.last = now;
        sAuto.itemPending = true;
    }
    sAuto.playFrames++;
    sAuto.sinceLast++;
    if (sAuto.noticeFrames > 0) {
        sAuto.noticeFrames--;
    }

    switch (sAuto.step) {
    case SaveStep::Idle: {
        if (mode == 0) {
            sAuto.itemPending = false;
            return;
        }
        const bool due = ((mode & PC_QOL_AUTOSAVE_ITEMS) && sAuto.itemPending) ||
                         ((mode & PC_QOL_AUTOSAVE_TIMER) && sAuto.playFrames >= kAutosaveTimerFrames);
        if (!due || !safe || sAuto.sinceLast < kAutosaveMinGap || !cardIdle()) {
            return;
        }
        if (!saveAllowed()) {
            if (!sAuto.loggedNoFile) {
                pc::writef(STDERR_FILENO, "[cos] qol: autosave: this file is not on this card yet (save once from "
                                          "the save screen)\n");
                sAuto.loggedNoFile = true;
            }
            sAuto.itemPending = false;
            sAuto.playFrames = 0;
            return;
        }
        // the card's file first, so the other two files are written back as they are
        mDoMemCd_Load();
        sAuto.step = SaveStep::Loading;
        sAuto.stepFrames = 0;
        return;
    }
    case SaveStep::Loading: {
        if (++sAuto.stepFrames > 30 * 10) {
            pc::writef(STDERR_FILENO, "[cos] qol: autosave: the card did not load in 10 s\n");
            sAuto.step = SaveStep::Idle;
            sAuto.sinceLast = 0;
            return;
        }
        if (!mDoMemCd_isCardCommNone()) {
            return;
        }
        const u32 load = mDoMemCd_LoadSync(sAuto.card, sizeof(sAuto.card), 0);
        if (load == 0) {
            return; // the card thread still holds its lock
        }
        if (load != 1) {
            pc::writef(STDERR_FILENO, "[cos] qol: autosave: the card's file did not load (%u)\n", load);
            sAuto.step = SaveStep::Idle;
            sAuto.sinceLast = 0;
            return;
        }
        // the moment may have passed while the card was read (a message, an event, a scene change)
        if (!safe) {
            sAuto.step = SaveStep::Idle;
            return;
        }
        startWrite();
        return;
    }
    case SaveStep::Saving: {
        if (++sAuto.stepFrames > 30 * 10) {
            pc::writef(STDERR_FILENO, "[cos] qol: autosave: the card did not answer in 10 s\n");
            sAuto.step = SaveStep::Idle;
            sAuto.sinceLast = 0;
            return;
        }
        const s32 res = mDoMemCd_SaveSync();
        if (res == 0) {
            return;
        }
        sAuto.step = SaveStep::Idle;
        sAuto.sinceLast = 0;
        if (res == 1) {
            // as the save screen does after a good write
            dComIfGs_setMemCardCheckID(mDoMemCd_getCardSerialNo());
            dComIfGs_setNewFile(0);
            dComIfGs_setNoFile(0);
            sAuto.itemPending = false;
            sAuto.playFrames = 0;
            sAuto.noticeFrames = kAutosaveNoticeFrames;
            pc::writef(STDERR_FILENO, "[cos] qol: autosave: file %d saved (stage %s)\n", dComIfGs_getDataNum() + 1,
                       dComIfGp_getStartStageName());
        } else {
            pc::writef(STDERR_FILENO, "[cos] qol: autosave: the write failed (%d)\n", (int)res);
        }
        return;
    }
    }
}

} // namespace

extern "C" {

int pc_qol_hud_hidden(void) {
    return sHudHidden && pc_qol(PC_QOL_HUD_BUTTON) != 0;
}

int pc_qol_jump_take(void) {
    if (sJumpFrames == 0 || pc_qol(PC_QOL_JUMP_BUTTON) == 0) {
        return 0;
    }
    sJumpFrames = 0;
    return 1;
}

const char* pc_qol_notice(void) {
    return sAuto.noticeFrames > 0 ? (pc::pc_ui_spanish() ? "Guardado" : "Saved") : nullptr;
}

// COS_QOL_NO_MUSIC: the background music (the main and second BGM and the streamed one) at volume 0 on a
// volume line of its own, which the game never sets, so its fades and mutes still work and the sound
// effects keep playing. Each frame, so a new song is silenced as it starts and a change shows at once.
static void musicFrame() {
    JAIZelBasic* zel = JAIZelBasic::getInterface();
    if (zel == nullptr) {
        return;
    }
    const float volume = strcmp(pc_settings_get("COS_QOL_NO_MUSIC"), "1") == 0 ? 0.0f : 1.0f;
    constexpr u8 kLine = 19; // the last of the 20 volume lines (JAISequenceMgr.h, JAIStreamMgr.h)
    JAISound* const sounds[] = {zel->mpMainBgmSound, zel->mpSubBgmSound, zel->mpStreamBgmSound};
    for (JAISound* sound : sounds) {
        if (sound != nullptr) {
            sound->setVolume(volume, 0, kLine);
        }
    }
}

void pc_qol_frame(void) {
    musicFrame();
    if (fpcM_Search(isPlayScene, nullptr) == nullptr) {
        sAuto.known = false; // the title or the file select: the next play starts from what its file has
    }
    if (pc::menuOpen() || trackerOpen() || fpcM_Search(isPlayScene, nullptr) == nullptr) {
        // the buttons' state is still followed, so a press that opened or closed something is not a press here
        pressed(PC_QOL_HUD_BUTTON, sHudButton);
        pressed(PC_QOL_ITEM_SETS, sItemSetButton);
        pressed(PC_QOL_JUMP_BUTTON, sJumpButton);
        sJumpFrames = 0;
        return;
    }
    // A press read now is taken by the player's next frame; if his code does not run in the next one
    // (a pause, a scene change) it lapses rather than jumping later.
    if (sJumpFrames > 0) {
        sJumpFrames--;
    }
    if (pressed(PC_QOL_JUMP_BUTTON, sJumpButton)) {
        sJumpFrames = 2;
    }
    if (pressed(PC_QOL_HUD_BUTTON, sHudButton)) {
        sHudHidden = !sHudHidden;
        pc::writef(STDERR_FILENO, "[cos] qol: HUD %s\n", sHudHidden ? "hidden" : "shown");
    }
    if (pressed(PC_QOL_ITEM_SETS, sItemSetButton) && canSwapItems()) {
        swapItemSets();
    }
    autosaveFrame(canSwapItems());
}

} // extern "C"
