/**
 * d_a_switch_op.cpp
 * Rando custom actor "SwOp" (proc 0x1F6), added by the randomizer to stages it edits.
 * Ported from the disassembly of wwrando's asm/d_a_switch_op.rel (commit 9775811), which has no source.
 *
 * Params: 0x0000000F operation (daSwOp_c::Operation), 0x00000010 continuous (unset the switch again when
 * the condition stops holding), 0x0000FF00 switch to set, 0x00FF0000 first switch to check,
 * 0xFF000000 number of switches to check.
 * X rotation & 0xFF: event number to play before setting the switch (0xFF for none).
 * Z rotation & 0xFF: frames to wait before setting the switch.
 */

#include "d/dolzel_rel.h" // IWYU pragma: keep
#include "d/actor/d_a_switch_op.h"
#include "d/d_com_inf_game.h"
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_op/f_op_actor_mng.h"

cPhs_State daSwOp_c::create() {
    fopAcM_ct(this, daSwOp_c);

    u32 prm = fopAcM_GetParam(this);
    mOperation = prm & 0xF;
    mIsContinuous = (prm >> 4) & 1;
    mSwitchToSet = (prm >> 8) & 0xFF;
    mFirstSwitchToCheck = (prm >> 16) & 0xFF;
    mNumSwitchesToCheck = (prm >> 24) & 0xFF;
    mEvntNo = home.angle.x & 0xFF;
    mTimer = home.angle.z & 0xFF;
    mEventIdx = dComIfGp_evmng_getEventIdx(NULL, mEvntNo);
    mTimerLeft = mTimer;

    if (mSwitchToSet == 0xFF) {
        mState = STATE_DONE;
    } else if (dComIfGs_isSwitch(mSwitchToSet, fopAcM_GetRoomNo(this))) {
        mState = mIsContinuous ? STATE_WATCH : STATE_DONE;
    } else {
        mState = STATE_CHECK;
    }
    return cPhs_COMPLEATE_e;
}

bool daSwOp_c::checkCondition() {
    int numOff = 0;
    int numOn = 0;
    for (int i = 0; i < mNumSwitchesToCheck; i++) {
        if (dComIfGs_isSwitch((u8)(mFirstSwitchToCheck + i), fopAcM_GetRoomNo(this))) {
            numOn++;
        } else {
            numOff++;
        }
    }

    switch (mOperation) {
    case OP_AND:
        return numOff == 0;
    case OP_NAND:
        return numOff > 0;
    case OP_OR:
        return numOn > 0;
    case OP_NOR:
        return numOn == 0;
    case OP_XOR:
        return numOn == 1;
    case OP_XNOR:
        return numOn != 1;
    default:
        return false;
    }
}

void daSwOp_c::checkProc() {
    if (!checkCondition()) {
        return;
    }
    if (mTimer != 0) {
        mState = STATE_TIMER;
    } else if (mEventIdx != -1) {
        mState = STATE_ORDER_EVENT;
    } else {
        dComIfGs_onSwitch(mSwitchToSet, fopAcM_GetRoomNo(this));
        mState = mIsContinuous ? STATE_WATCH : STATE_DONE;
    }
}

void daSwOp_c::timerProc() {
    if (mIsContinuous && !checkCondition()) {
        mTimerLeft = mTimer;
        mState = STATE_CHECK;
        return;
    }
    if (mTimerLeft != 0) {
        mTimerLeft--;
        return;
    }
    if (mEventIdx != -1) {
        mState = STATE_ORDER_EVENT;
        return;
    }
    dComIfGs_onSwitch(mSwitchToSet, fopAcM_GetRoomNo(this));
    // As in the REL: a continuous operator stops watching here, a one-shot one stays in this state
    // (setting the switch again each frame does nothing)
    if (mIsContinuous) {
        mState = STATE_DONE;
    }
}

void daSwOp_c::orderEventProc() {
    if (eventInfo.checkCommandDemoAccrpt()) {
        mState = STATE_IN_EVENT;
        dComIfGs_onSwitch(mSwitchToSet, fopAcM_GetRoomNo(this));
        return;
    }
    if (mIsContinuous && !checkCondition()) {
        mState = STATE_CHECK;
        return;
    }
    fopAcM_orderOtherEventId(this, mEventIdx, mEvntNo, 0xFFFF, 0, 1);
}

void daSwOp_c::inEventProc() {
    if (dComIfGp_evmng_endCheck(mEventIdx)) {
        mState = mIsContinuous ? STATE_WATCH : STATE_DONE;
        dComIfGp_event_reset();
    }
}

void daSwOp_c::watchProc() {
    if (!checkCondition()) {
        mState = STATE_CHECK;
        dComIfGs_offSwitch(mSwitchToSet, fopAcM_GetRoomNo(this));
    }
}

BOOL daSwOp_c::execute() {
    switch (mState) {
    case STATE_CHECK:
        checkProc();
        break;
    case STATE_TIMER:
        timerProc();
        break;
    case STATE_ORDER_EVENT:
        orderEventProc();
        break;
    case STATE_IN_EVENT:
        inEventProc();
        break;
    case STATE_WATCH:
        watchProc();
        break;
    }
    return TRUE;
}

static BOOL daSwOp_Draw(daSwOp_c*) {
    return TRUE;
}

static BOOL daSwOp_Execute(daSwOp_c* i_this) {
    return i_this->execute();
}

static BOOL daSwOp_IsDelete(daSwOp_c*) {
    return TRUE;
}

static BOOL daSwOp_Delete(daSwOp_c* i_this) {
    i_this->~daSwOp_c();
    return TRUE;
}

static cPhs_State daSwOp_Create(fopAc_ac_c* i_this) {
    return ((daSwOp_c*)i_this)->create();
}

static actor_method_class l_daSwOp_Method = {
    (process_method_func)daSwOp_Create,
    (process_method_func)daSwOp_Delete,
    (process_method_func)daSwOp_Execute,
    (process_method_func)daSwOp_IsDelete,
    (process_method_func)daSwOp_Draw,
};

actor_process_profile_definition g_profile_SWITCH_OP = {
    /* Layer ID     */ fpcLy_CURRENT_e,
    /* List ID      */ 0x0007,
    /* List Prio    */ fpcPi_CURRENT_e,
    /* Proc Name    */ fpcNm_SWITCH_OP_e,
    /* Proc SubMtd  */ &g_fpcLf_Method.base,
    /* Size         */ sizeof(daSwOp_c),
    /* Size Other   */ 0,
    /* Parameters   */ 0,
    /* Leaf SubMtd  */ &g_fopAc_Method.base,
    /* Draw Prio    */ fpcDwPi_RECTANGLE_e,
    /* Actor SubMtd */ &l_daSwOp_Method,
    /* Status       */ 0,
    /* Group        */ fopAc_ACTOR_e,
    /* Cull Type    */ fopAc_CULLBOX_0_e,
};
