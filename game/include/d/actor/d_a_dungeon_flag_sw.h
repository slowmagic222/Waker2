#ifndef D_A_DUNGEON_FLAG_SW_H
#define D_A_DUNGEON_FLAG_SW_H

#include "f_op/f_op_actor.h"

// Rando custom actor "DngSw" (wwrando asm/actors/d_a_dungeon_flag_sw.cpp). It sets a switch once a
// dungeon flag is set for every one of the given dungeons, e.g. once all the bosses are dead.
class daDungeonFlagSw_c : public fopAc_ac_c {
public:
    cPhs_State create();
    BOOL execute();
    BOOL checkConditionsMet();

    // The switch to set once the condition is met
    u32 prmGetSwitch() { return (fopAcM_GetParam(this) & 0x000000FF) >> 0x00; }
    // The stage numbers to check, one bit per stage (DRC 0x03 and ET 0x06 is 0x0048)
    u32 prmGetStageNoMask() { return (fopAcM_GetParam(this) & 0x00FFFF00) >> 0x08; }
    // The dungeon flag to check: 0 map, 1 compass, 2 big key, 3 boss dead, 4 heart container taken,
    // 5 boss intro watched
    u32 prmGetCondToCheck() { return (fopAcM_GetParam(this) & 0x07000000) >> 0x18; }
};

#endif /* D_A_DUNGEON_FLAG_SW_H */
