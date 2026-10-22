#ifndef D_A_SWITCH_OP_H
#define D_A_SWITCH_OP_H

#include "f_op/f_op_actor.h"

// Rando custom actor "SwOp" (wwrando asm/d_a_switch_op.rel, ported from its disassembly; wwrando ships the
// REL without source). It sets a switch once a logic operation over a range of switches holds, optionally
// after a timer and an event.
class daSwOp_c : public fopAc_ac_c {
public:
    enum Operation {
        OP_AND,
        OP_NAND,
        OP_OR,
        OP_NOR,
        OP_XOR,
        OP_XNOR,
    };

    enum State {
        STATE_CHECK,
        STATE_TIMER,
        STATE_ORDER_EVENT,
        STATE_IN_EVENT,
        STATE_WATCH,
        STATE_DONE,
    };

    cPhs_State create();
    BOOL execute();
    bool checkCondition();
    void checkProc();
    void timerProc();
    void orderEventProc();
    void inEventProc();
    void watchProc();
    void finish();

    /* 0x290 */ u8 mState;
    /* 0x291 */ u8 mOperation;
    /* 0x292 */ u8 mIsContinuous;
    /* 0x293 */ u8 mSwitchToSet;
    /* 0x294 */ u8 mFirstSwitchToCheck;
    /* 0x295 */ u8 mNumSwitchesToCheck;
    /* 0x296 */ u8 mEvntNo;
    /* 0x298 */ s16 mEventIdx;
    /* 0x29A */ u8 mTimer;
    /* 0x29B */ u8 mTimerLeft;
};  // Size: 0x29C

#endif /* D_A_SWITCH_OP_H */
