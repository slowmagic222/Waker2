// Quality-of-life options (native/include/pc/pc_qol.h, docs/QOL_PLAN.md): their values. The game's
// hunks that act on them are listed in the plan; the menu rows are in pc_menu.cpp.
#include "pc_internal.h"

#include "pc/pc_qol.h"
#include "pc/pc_rando.h"

#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

struct Option {
    const char* key;
    int value; // -1: not read yet
};

Option sOptions[PC_QOLS] = {
    {"COS_QOL_INSTANT_TEXT", -1},  {"COS_QOL_FAST_ROLL", -1},     {"COS_QOL_FAST_BLOCKS", -1},
    {"COS_QOL_FAST_GRAPPLE", -1},  {"COS_QOL_FAST_CLIMB", -1},    {"COS_QOL_FAST_BALLAD", -1},
    {"COS_QOL_REVEAL_CHART", -1},  {"COS_QOL_TINGLE_CHESTS", -1},
    {"COS_QOL_SAIL", -1},          {"COS_QOL_SWING_TURN", -1},    {"COS_QOL_NO_SONG_REPLAY", -1},
    {"COS_QOL_FREE_BOAT", -1},     {"COS_QOL_SKIP_INTRO", -1},    {"COS_QOL_FIX_BUGS", -1},
    {"COS_QOL_HUD_BUTTON", -1},    {"COS_QOL_ITEMS_STAY", -1},    {"COS_QOL_ITEM_SETS", -1},
    {"COS_QOL_SEA_MAP", -1},       {"COS_QOL_AUTOSAVE", -1},      {"COS_QOL_JUMP_BUTTON", -1},
    {"COS_QOL_CLIMB", -1},         {"COS_QOL_SPRINT", -1},
};

// PC_QOL_BUTTON_*'s COS_PAD_* values, by number (0: off)
const char* const kButtons[] = {"", "l", "down", "lstick", "rstick"};

int parse(int which, const char* v) {
    if (v == nullptr) {
        return 0;
    }
    if (which == PC_QOL_SAIL) {
        if (strcmp(v, "swift") == 0) {
            return PC_QOL_SAIL_SWIFT;
        }
        if (strcmp(v, "brisk") == 0) {
            return PC_QOL_SAIL_BRISK;
        }
        if (strcmp(v, "wind") == 0) {
            return PC_QOL_SAIL_WIND;
        }
        return 0;
    }
    if (which == PC_QOL_AUTOSAVE) {
        return strcmp(v, "items") == 0  ? PC_QOL_AUTOSAVE_ITEMS
               : strcmp(v, "10") == 0   ? PC_QOL_AUTOSAVE_TIMER
               : strcmp(v, "items10") == 0 ? PC_QOL_AUTOSAVE_ITEMS | PC_QOL_AUTOSAVE_TIMER
                                        : 0;
    }
    if (which == PC_QOL_HUD_BUTTON || which == PC_QOL_ITEM_SETS || which == PC_QOL_JUMP_BUTTON) {
        for (int i = 1; i < (int)(sizeof(kButtons) / sizeof(kButtons[0])); i++) {
            if (strcmp(v, kButtons[i]) == 0) {
                return i;
            }
        }
        return 0;
    }
    return strcmp(v, "1") == 0 ? 1 : 0;
}

} // namespace

extern "C" {

int pc_qol(PcQol which) {
    Option& o = sOptions[which];
    if (o.value < 0) {
        const char* v = getenv(o.key);
        o.value = parse(which, v);
        if (o.value != 0) {
            pc::writef(STDERR_FILENO, "[cos] qol: %s=%s\n", o.key, v);
        }
    }
    // Rando: wwrando always applies these patches (remove_cutscenes.asm: the intro, song replays and the Ballad
    // of Gales warp cutscene; tingle_chests_without_tuner.asm; fix_vanilla_bugs.asm; misc_rando_features.asm's
    // turning while swinging on a rope), so a seed plays with
    // the matching options on whatever the menu says
    if (o.value == 0 && pc_rando_active() &&
        (which == PC_QOL_SKIP_INTRO || which == PC_QOL_NO_SONG_REPLAY || which == PC_QOL_FAST_BALLAD ||
         which == PC_QOL_TINGLE_CHESTS || which == PC_QOL_FIX_BUGS || which == PC_QOL_SWING_TURN)) {
        return 1;
    }
    // Rando: the seed's options reveal_sea_chart.asm and swift_sail.asm, the same as these two options
    if (o.value == 0 && which == PC_QOL_REVEAL_CHART && pc_rando_option(PC_RANDO_REVEAL_CHART)) {
        return 1;
    }
    if (which == PC_QOL_SAIL && o.value != PC_QOL_SAIL_SWIFT && pc_rando_option(PC_RANDO_SWIFT_SAIL)) {
        return PC_QOL_SAIL_SWIFT;
    }
    return o.value;
}

int pc_qol_fast_sail(void) {
    const int sail = pc_qol(PC_QOL_SAIL);
    return sail == PC_QOL_SAIL_SWIFT || sail == PC_QOL_SAIL_BRISK ? sail : 0;
}

const char* pc_qol_button_input(int button) {
    return button > 0 && button < (int)(sizeof(kButtons) / sizeof(kButtons[0])) ? kButtons[button] : "";
}

const char* pc_qol_key(PcQol which) {
    return sOptions[which].key;
}

int pc_qol_set(const char* key, const char* value) {
    for (int i = 0; i < PC_QOLS; i++) {
        if (strcmp(sOptions[i].key, key) == 0) {
            sOptions[i].value = parse(i, value); // pc_settings logs the change
            return 1;
        }
    }
    return 0;
}

} // extern "C"
