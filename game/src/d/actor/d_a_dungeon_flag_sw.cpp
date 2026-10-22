/**
 * d_a_dungeon_flag_sw.cpp
 * Rando custom actor "DngSw" (proc 0x1F7), added by the randomizer to stages it edits.
 * Port of wwrando's asm/actors/d_a_dungeon_flag_sw.cpp (commit 9775811).
 */

#include "d/dolzel_rel.h" // IWYU pragma: keep
#include "d/actor/d_a_dungeon_flag_sw.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_draw_priority.h"

cPhs_State daDungeonFlagSw_c::create() {
    fopAcM_ct(this, daDungeonFlagSw_c);

    if (fopAcM_isSwitch(this, prmGetSwitch())) {
        return cPhs_ERROR_e;
    }
    return cPhs_COMPLEATE_e;
}

BOOL daDungeonFlagSw_c::checkConditionsMet() {
    u32 stageNoMask = prmGetStageNoMask();
    u32 condToCheck = prmGetCondToCheck();
    for (int stageNo = 0; stageNo < 0x10; stageNo++) {
        if (!(stageNoMask & (1 << stageNo))) {
            continue;
        }
        if (!g_dComIfG_gameInfo.save.getSavedata().getSave(stageNo).getBit().isDungeonItem(condToCheck)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL daDungeonFlagSw_c::execute() {
    if (checkConditionsMet()) {
        fopAcM_onSwitch(this, prmGetSwitch());
        fopAcM_delete(this);
    }
    return TRUE;
}

static BOOL daDungeonFlagSw_Draw(daDungeonFlagSw_c*) {
    return TRUE;
}

static BOOL daDungeonFlagSw_Execute(daDungeonFlagSw_c* i_this) {
    return i_this->execute();
}

static BOOL daDungeonFlagSw_IsDelete(daDungeonFlagSw_c*) {
    return TRUE;
}

static BOOL daDungeonFlagSw_Delete(daDungeonFlagSw_c* i_this) {
    i_this->~daDungeonFlagSw_c();
    return TRUE;
}

static cPhs_State daDungeonFlagSw_Create(fopAc_ac_c* i_this) {
    return ((daDungeonFlagSw_c*)i_this)->create();
}

static actor_method_class l_daDungeonFlagSw_Method = {
    (process_method_func)daDungeonFlagSw_Create,
    (process_method_func)daDungeonFlagSw_Delete,
    (process_method_func)daDungeonFlagSw_Execute,
    (process_method_func)daDungeonFlagSw_IsDelete,
    (process_method_func)daDungeonFlagSw_Draw,
};

actor_process_profile_definition g_profile_DUNGEON_FLAG_SW = {
    /* Layer ID     */ fpcLy_CURRENT_e,
    /* List ID      */ 0x0007,
    /* List Prio    */ fpcPi_CURRENT_e,
    /* Proc Name    */ fpcNm_DUNGEON_FLAG_SW_e,
    /* Proc SubMtd  */ &g_fpcLf_Method.base,
    /* Size         */ sizeof(daDungeonFlagSw_c),
    /* Size Other   */ 0,
    /* Parameters   */ 0,
    /* Leaf SubMtd  */ &g_fopAc_Method.base,
    /* Draw Prio    */ fpcDwPi_ANDSW2_e,
    /* Actor SubMtd */ &l_daDungeonFlagSw_Method,
    /* Status       */ fopAcStts_UNK4000_e | fopAcStts_UNK40000_e,
    /* Group        */ fopAc_ACTOR_e,
    /* Cull Type    */ fopAc_CULLBOX_6_e,
};
