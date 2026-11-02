/*
 * pc_qol.h - the options menu's "Calidad de vida" tab (docs/QOL_PLAN.md, issue #4): BetterWW's and
 * wwrando's quality-of-life patches redone in the decompiled game, each behind its own setting.
 *
 * COS_QOL_INSTANT_TEXT=1     text boxes appear whole, without their waits; holding B skips text
 * COS_QOL_FAST_ROLL=1        faster roll
 * COS_QOL_FAST_BLOCKS=1      faster block pushing and pulling
 * COS_QOL_FAST_GRAPPLE=1     faster grappling hook
 * COS_QOL_FAST_CLIMB=1       faster crawling, climbing, sidling and talk camera
 * COS_QOL_FAST_BALLAD=1      the Ballad of Gales warps without the boat's rise into the cyclone
 * COS_QOL_REVEAL_CHART=1     the sea chart shows every sector (the save is not written)
 * COS_QOL_TINGLE_CHESTS=1    Tingle chests appear with any bomb, no Tingle Tuner
 * COS_QOL_SAIL=swift|brisk|wind  the sail behaves like HD's Swift Sail (or BetterWW's Brisk Sail);
 *                            wind: the original sail with the wind always behind the boat
 * COS_QOL_SWING_TURN=1       the main stick turns the player while swinging on a rope or the hook
 * COS_QOL_NO_SONG_REPLAY=1   no conducting replay after a song
 * COS_QOL_FREE_BOAT=1        the boat does not turn back at the story's limits (only the sea's edge)
 *                            and can be boarded whatever the wind
 * COS_QOL_SKIP_INTRO=1       a new game starts on Outset without the opening legend
 * COS_QOL_FIX_BUGS=1         fixes for bugs of the original game (docs/QOL_PLAN.md, "Bugs of the
 *                            original game")
 * COS_QOL_HUD_BUTTON=<input> a press of that physical button hides the HUD (hearts, rupees, buttons,
 *                            minimap), the next press shows it again; <input> is a COS_PAD_* value
 *                            (pc_button_remap.h): l, down, lstick or rstick
 * COS_QOL_ITEMS_STAY=1       dropped rupees, hearts, magic, arrows and bombs do not time out; they go
 *                            once their time is up and the player is far away (or the room unloads)
 * COS_QOL_ITEM_SETS=<input>  a second set of X/Y/Z items: a press of that button swaps the two sets
 *                            (BetterWW's optional Gecko code); the other set is kept per save slot in
 *                            item_sets.txt next to settings.ini
 * COS_QOL_SEA_MAP=1          a small live chart of the Great Sea in the bottom-right corner while on
 *                            the sea (pc_sea_map.cpp)
 * COS_QOL_AUTOSAVE=items|10|items10  the game saves to its file on its own (as the save screen does): after
 *                            a new item or upgrade, every 10 minutes of play, or both; only once the
 *                            file was saved by hand on this card, and only when the Start menu could open
 * COS_QOL_JUMP_BUTTON=<input> a press of that physical button makes Link jump while he stands, walks or
 *                            runs on the ground (the game's own jump off a ledge, procAutoJump_init:
 *                            its animation, voice, arc, ledge grabs and glides); <input> as for
 *                            COS_QOL_HUD_BUTTON (pc_qol_hooks.cpp, d_a_player_main.cpp)
 * COS_QOL_CLIMB=1            Link climbs any plain wall as he climbs ivy, Breath of the Wild style, on
 *                            a stamina wheel drawn beside him (COS_QOL_CLIMB_STAMINA seconds of
 *                            climbing, 12 by default); out of stamina he lets go, and grabs again once
 *                            it has refilled on the ground
 * COS_QOL_SPRINT=1           Link sprints forward while holding A and moving on the ground, consuming
 *                            stamina (shared wheel with climbing); tap A still rolls/acts normally;
 *                            refills stamina when standing still
 *
 * All off by default (0: the original game). Each value is read from the environment (the settings
 * file's value, pc_settings.h) the first time the game asks; the menu's apply function sets it
 * afterwards, so every option switches live. The game thread calls all of them.
 */
#ifndef PC_QOL_H
#define PC_QOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PcQol {
    PC_QOL_INSTANT_TEXT = 0,
    PC_QOL_FAST_ROLL,
    PC_QOL_FAST_BLOCKS,
    PC_QOL_FAST_GRAPPLE,
    PC_QOL_FAST_CLIMB,
    PC_QOL_FAST_BALLAD,
    PC_QOL_REVEAL_CHART,
    PC_QOL_TINGLE_CHESTS,
    PC_QOL_SAIL,
    PC_QOL_SWING_TURN,
    PC_QOL_NO_SONG_REPLAY,
    PC_QOL_FREE_BOAT,
    PC_QOL_SKIP_INTRO,
    PC_QOL_FIX_BUGS,
    PC_QOL_HUD_BUTTON,
    PC_QOL_ITEMS_STAY,
    PC_QOL_ITEM_SETS,
    PC_QOL_SEA_MAP,
    PC_QOL_AUTOSAVE,
    PC_QOL_JUMP_BUTTON,
    PC_QOL_CLIMB,
    PC_QOL_SPRINT,
    PC_QOLS
} PcQol;

/* PC_QOL_SAIL's values. */
#define PC_QOL_SAIL_SWIFT 1
#define PC_QOL_SAIL_BRISK 2
#define PC_QOL_SAIL_WIND 3 /* BetterWW's "normal sail controls wind": the wind only, no speed change */

/* PC_QOL_HUD_BUTTON's and PC_QOL_ITEM_SETS's values: the physical button (a COS_PAD_* value). */
#define PC_QOL_BUTTON_L 1      /* "l": L (the left shoulder; no GameCube action by default) */
#define PC_QOL_BUTTON_DOWN 2   /* "down": D-pad down (the GameCube's D-pad down does nothing in play) */
#define PC_QOL_BUTTON_LSTICK 3 /* "lstick": left stick click */
#define PC_QOL_BUTTON_RSTICK 4 /* "rstick": right stick click */

/* PC_QOL_AUTOSAVE's values: bits. */
#define PC_QOL_AUTOSAVE_ITEMS 1 /* "items": after a new item or upgrade */
#define PC_QOL_AUTOSAVE_TIMER 2 /* "10": every 10 minutes of play */

/* The option's value: 0 off (the original game), else on (PC_QOL_SAIL: PC_QOL_SAIL_SWIFT/BRISK;
   PC_QOL_HUD_BUTTON, PC_QOL_ITEM_SETS, PC_QOL_JUMP_BUTTON: PC_QOL_BUTTON_*). */
int pc_qol(PcQol which);
/* PC_QOL_SAIL's speed part: PC_QOL_SAIL_SWIFT or PC_QOL_SAIL_BRISK, else 0 (off or the wind only). */
int pc_qol_fast_sail(void);
/* The settings key of an option (COS_QOL_...). */
const char* pc_qol_key(PcQol which);
/* The options menu: a new value of the setting `key` ("0", "1", "swift", "brisk"); 0 if the key is
   not a QoL option. */
int pc_qol_set(const char* key, const char* value);

/* The COS_PAD_* value of a PC_QOL_BUTTON_* ("l", "down", ...); "" for 0. */
const char* pc_qol_button_input(int button);
/* 1 while COS_QOL_HUD_BUTTON is on and its button hid the HUD (d_meter.cpp reads it as the game's own
   "2D hidden" state, the one of a screen fade). */
int pc_qol_hud_hidden(void);
/* The game thread, once per frame after the options menu (pc_frame.cpp): the HUD and item set buttons
   (pc_qol_hooks.cpp). */
void pc_qol_frame(void);
/* COS_QOL_JUMP_BUTTON (pc_qol_hooks.cpp): 1 once for a press of the jump button, which daPy_lk_c::execute
   spends on Link's next frame, jump or not; a press it does not take within two frames lapses. */
int pc_qol_jump_take(void);
/* COS_QOL_CLIMB (pc_climb.cpp), from the player's code: 1 if Link may hold on to a plain wall now (the
   option is on and the stamina wheel is not empty or refilling after running out). */
int pc_qol_climb_can_grab(void);
/* Link held on to a plain wall in this frame's climbing (setMoveBGCorrectClimb). */
void pc_qol_climb_plain(void);
/* Link sprints in this frame (pc_sprint.cpp, before pc_qol_climb_frame): the wheel drains as for climbing. */
void pc_qol_climb_sprint(void);
/* Once per player frame (daPy_lk_c::execute): `climbing` Link is in a climbing proc, `ground` he stands
   on the ground (not in the air or swimming), `still` the stick is let go; `shoulder` where the wheel
   goes, in the world. Drains and refills the wheel. */
void pc_qol_climb_frame(int climbing, int ground, int still, const float shoulder[3]);
/* A short line for the corner of the screen ("Guardado", an autosave just finished), NULL if none now. */
const char* pc_qol_notice(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_QOL_H */
