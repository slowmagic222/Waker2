/**
 * d_save_init.cpp
 *
 */

#include "d/dolzel.h" // IWYU pragma: keep
#include "d/d_save_init.h"
#include "d/d_com_inf_game.h"
#include "d/actor/d_a_npc_kg1.h"

#ifdef TARGET_PC
#include "pc/game_hooks.h"
#endif

/* 8005EF88-8005EFDC       .text setInitEventBit__Fv */
void setInitEventBit() {
    dComIfGs_setEventReg(dSv_event_flag_c::UNK_BEFF, daNpc_Kg1_c::m_highscore);
    dComIfGs_setEventReg(dSv_event_flag_c::UNK_7EFF, 0xE);

#ifdef TARGET_PC
    pc_rplus_init_story_events();
#endif
}
