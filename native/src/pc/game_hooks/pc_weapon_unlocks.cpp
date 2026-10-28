// Randomizer+ weapon unlocks (pc/game_hooks.h, docs/RANDOMIZER_PLUS.md): the enemies' weapons Link can pick up
// (d_a_boko: the Bokoblin's stick, the machete, the Stalfos mace, the Darknut's sword, the Moblin's spear, Phantom
// Ganon's sword) are found as check rewards; once found, a press of the weapon button (COS_RPLUS_WEAPON_BUTTON, the
// Calidad de vida tab) lays one in front of Link and he picks it up, the game's own grab (procGrabReady_init) as
// for one an enemy dropped. The next press puts it away and gives the next weapon he has; after the last, his hands
// are empty again. An enemy's weapon he is holding is dropped instead.
//
// The checks (Dillon, 2026-10-28): like the house decor (pc_rplus_decor.cpp), a check that holds only filler
// (fopAcM_createItemForTrBoxDemo: a chest, sunken treasure, a demo pickup) can give a weapon, at the filler checks
// the seed picks: the k-th weapon at about the 2nd, 5th, 9th, 14th, 20th and 27th, each a little later by the seed,
// in an order the seed shuffles. That check gives no decor piece; the filler still comes.
//
// From the start (Dillon, 2026-10-28): COS_RPLUS_WEAPONS_START, on by default, gives Link all six at once and no
// check gives one; off, they are found as above.
//
// The save: rplus-weapons-<seed>-<slot>.txt next to the memory card (as the decor's), read when the file is loaded
// and written when the game saves. Picking up an enemy's weapon in the world unlocks nothing.
//
// File name: not pc_rplus_* so that a push of it does not start the Steam Deck workflow (steamdeck.yml paths).
#include "pc/game_hooks.h"
#include "pc/pc_button_remap.h"
#include "pc/pc_rplus.h"
#include "pc/pc_seedgen.h"
#include "pc/pc_settings.h"
#include "pc_internal.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// host memory for the notice's string (pc_rplus_decor.cpp: game heaps are freed wholesale)
using Host = JKRPcHostAllocScope;

constexpr int WEAPONS = 6;  // daBoko_c::Type_COUNT_e

struct Weapon {
    const char* key;  // in the save file
    const char* en;
    const char* es;
};

// by daBoko_c type (the actor's params)
const Weapon kWeapons[WEAPONS] = {
    {"stick", "Bokoblin staff", "Bastón de bokoblin"},
    {"machete", "Pirate machete", "Machete pirata"},
    {"mace", "Stalfos mace", "Maza de Stalfos"},
    {"darknut", "Darknut sword", "Espada de Darknut"},
    {"spear", "Moblin spear", "Lanza de Moblin"},
    {"pgsword", "Phantom Ganon's sword", "Espada de Ganon Fantasma"},
};

// the filler checks (1st, 2nd...) that give the k-th weapon, before the seed's jitter
const int kAt[WEAPONS] = {2, 5, 9, 14, 20, 27};

struct State {
    bool owned[WEAPONS] = {};
    int fillers = 0;   // filler checks so far
    int selected = -1; // the weapon the next press draws (-1: the first owned)
};

State sState;

u32 mix(u32 a, u32 b) {
    u32 h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

// the seed's order of the weapons: order[k] is the k-th one found
void order(int out[WEAPONS]) {
    for (int i = 0; i < WEAPONS; i++) {
        out[i] = i;
    }
    for (int i = WEAPONS - 1; i > 0; i--) {
        const int j = (int)(mix((u32)i, pc_rplus_seed() ^ 0x5EA9u) % (u32)(i + 1));
        const int t = out[i];
        out[i] = out[j];
        out[j] = t;
    }
}

// the filler check (1st, 2nd...) that gives the k-th weapon
int checkFor(int k) {
    return kAt[k] + (int)(mix((u32)k, pc_rplus_seed() ^ 0xB0C0u) % 3u);
}

// COS_RPLUS_WEAPONS_START (Dillon, 2026-10-28: "new weapons in default inventory"): Link has all six from the start
// (the default); then no check gives one. 0: they are found at checks.
bool allFromStart() {
    const char* v = pc_settings_get("COS_RPLUS_WEAPONS_START");
    return v == nullptr || v[0] == '\0' || strcmp(v, "0") != 0;
}

bool owned(int i) {
    return sState.owned[i] || allFromStart();
}

int ownedCount() {
    int n = 0;
    for (int i = 0; i < WEAPONS; i++) {
        n += owned(i);
    }
    return n;
}

bool enabled() {
    const char* v = getenv("COS_RPLUS_WEAPONS");
    return v == nullptr || strcmp(v, "0") != 0;
}

std::string savePath(int slot) {
    const char* saves = getenv("COS_SAVE_DIR");
    std::string dir = saves != nullptr && saves[0] != '\0' ? saves : pc::seedgen::userDir();
    return dir + "/rplus-weapons-" + std::to_string(pc_rplus_seed()) + "-" + std::to_string(slot) + ".txt";
}

// ---- The notice

std::string sNotice;
std::chrono::steady_clock::time_point sNoticeEnd;

void notice(const char* line) {
    {
        Host host;
        sNotice = line;
    }
    sNoticeEnd = std::chrono::steady_clock::now() + std::chrono::seconds(6);
}

// ---- The button

// COS_RPLUS_WEAPON_BUTTON: a COS_PAD_* value (l, down, lstick, rstick; 0 or empty: off). A stick click counts
// when it is let go without the other stick held meanwhile (L3+R3 together is the tracker's), as the QoL buttons.
const char* button() {
    const char* v = pc_settings_get("COS_RPLUS_WEAPON_BUTTON");
    return v == nullptr || v[0] == '\0' || strcmp(v, "0") == 0 ? nullptr : v;
}

bool sHeld = false;
bool sSpoiled = false;

bool pressed() {
    const char* b = button();
    const bool held = b != nullptr && pc_button_remap_input_held(0, b);
    bool press;
    if (b != nullptr && (strcmp(b, "lstick") == 0 || strcmp(b, "rstick") == 0)) {
        const char* other = strcmp(b, "lstick") == 0 ? "rstick" : "lstick";
        if (held && pc_button_remap_input_held(0, other)) {
            sSpoiled = true;
        }
        press = sHeld && !held && !sSpoiled;
        if (!held) {
            sSpoiled = false;
        }
    } else {
        press = held && !sHeld;
    }
    sHeld = held;
    return press;
}

// ---- The weapon in the world

fpc_ProcID sOurs = fpcM_ERROR_PROCESS_ID_e;     // the last weapon laid down (held, thrown or waiting)
fpc_ProcID sPending = fpcM_ERROR_PROCESS_ID_e;  // laid down for Link to pick up
int sPendingFrames = 0;

// the owned weapon after `from` (-1: the first owned), -1 past the last
int nextOwned(int from) {
    for (int i = from + 1; i < WEAPONS; i++) {
        if (owned(i)) {
            return i;
        }
    }
    return -1;
}

void removeOurs() {
    if (sOurs != fpcM_ERROR_PROCESS_ID_e) {
        if (fopAc_ac_c* a = fopAcM_SearchByID(sOurs)) {
            fopAcM_delete(a);
        }
    }
    sOurs = sPending = fpcM_ERROR_PROCESS_ID_e;
}

void layDown(fopAc_ac_c* link, int type) {
    removeOurs();  // the last one, unless Link holds it (the player's code puts that one away)
    const s16 ay = link->shape_angle.y;
    const float a = ay * (3.14159265f / 32768.0f);
    cXyz pos(link->current.pos.x + 45.0f * sinf(a), link->current.pos.y + 15.0f, link->current.pos.z + 45.0f * cosf(a));
    csXyz angle(0, (s16)(ay + 0x4000), 0);
    sOurs = fopAcM_create("Boko", (u32)type, &pos, fopAcM_GetRoomNo(link), &angle, nullptr);
    sPending = sOurs;
    sPendingFrames = 0;
    fprintf(stderr, "[rplus] weapons: drawing %s\n", kWeapons[type].key);
}

} // namespace

void pc_rplus_weapon_reset(void) {
    sState = State();
    sOurs = sPending = fpcM_ERROR_PROCESS_ID_e;
}

void pc_rplus_weapon_load(int slot) {
    pc_rplus_weapon_reset();
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(savePath(slot).c_str(), "r");
    if (f == nullptr) {
        return;
    }
    // "fillers <n>", "<weapon>"
    char line[128];
    while (fgets(line, sizeof line, f) != nullptr) {
        char key[64];
        int n = 0;
        if (line[0] == '#' || sscanf(line, "%63s %d", key, &n) < 1) {
            continue;
        }
        if (strcmp(key, "fillers") == 0) {
            sState.fillers = n;
            continue;
        }
        for (int i = 0; i < WEAPONS; i++) {
            if (strcmp(kWeapons[i].key, key) == 0) {
                sState.owned[i] = true;
            }
        }
    }
    fclose(f);
    fprintf(stderr, "[rplus] weapons: file %d read (%d of %d, %d filler checks)\n", slot, ownedCount(), WEAPONS,
            sState.fillers);
}

void pc_rplus_weapon_save(int slot) {
    if (!pc_rplus_active()) {
        return;
    }
    const std::string path = savePath(slot);
    FILE* f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        fprintf(stderr, "[rplus] weapons: cannot write %s\n", path.c_str());
        return;
    }
    fprintf(f, "# Randomizer+ weapon unlocks (pc_weapon_unlocks.cpp): the filler checks so far, the weapons found\n");
    fprintf(f, "fillers %d\n", sState.fillers);
    for (int i = 0; i < WEAPONS; i++) {
        if (sState.owned[i]) {
            fprintf(f, "%s\n", kWeapons[i].key);
        }
    }
    fclose(f);
}

int pc_rplus_weapon_check(int itemNo) {
    if (!pc_rplus_active() || !enabled() || allFromStart()) {
        return 0;
    }
    // filler, as the decor's: a heart, rupees, magic, bombs, arrows, a fairy, three hearts
    static const u8 kFiller[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0A, 0x0B,
                                 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x16, 0x1E};
    bool filler = false;
    for (u8 f : kFiller) {
        filler = filler || itemNo == f;
    }
    if (!filler) {
        return 0;
    }
    sState.fillers++;
    int ord[WEAPONS];
    order(ord);
    for (int k = 0; k < WEAPONS; k++) {
        // the k-th weapon at its check, or at a later one if that check was passed without it (an old save)
        if (sState.owned[ord[k]] || sState.fillers < checkFor(k)) {
            continue;
        }
        const int w = ord[k];
        sState.owned[w] = true;
        char line[200];
        const char* b = button();
        if (pc::pc_ui_spanish()) {
            snprintf(line, sizeof line, "Arma: %s (%d de %d)%s", kWeapons[w].es, ownedCount(), WEAPONS,
                     b != nullptr ? ", pulsa el botón de armas para sacarla" : "; elige un botón de armas en Calidad de vida");
        } else {
            snprintf(line, sizeof line, "Weapon unlocked: %s (%d of %d)%s", kWeapons[w].en, ownedCount(), WEAPONS,
                     b != nullptr ? ", press the weapon button to draw it" : "; pick a weapon button in Quality of life");
        }
        notice(line);
        fprintf(stderr, "[rplus] weapons: found %s at filler check %d\n", kWeapons[w].key, sState.fillers);
        return 1;
    }
    return 0;
}

int pc_rplus_weapon_frame(unsigned int heldId, int free) {
    const bool press = pressed();  // followed every frame, so a press made elsewhere does not count later
    if (!pc_rplus_active()) {
        return PC_RPLUS_WEAPON_NONE;
    }
    if (sPending != fpcM_ERROR_PROCESS_ID_e && ++sPendingFrames > 90) {
        sPending = fpcM_ERROR_PROCESS_ID_e;  // not picked up in time: it stays on the ground for the A button
    }
    if (!press || !free || ownedCount() == 0 || dComIfGp_event_runCheck() || fopOvlpM_IsDoingReq()) {
        return PC_RPLUS_WEAPON_NONE;
    }
    fopAc_ac_c* link = dComIfGp_getLinkPlayer();
    if (link == nullptr) {
        return PC_RPLUS_WEAPON_NONE;
    }
    if (heldId != fpcM_ERROR_PROCESS_ID_e && heldId == sOurs) {
        // put away (the player's code deletes it); the next weapon Link has is laid down now, none after the last
        sOurs = sPending = fpcM_ERROR_PROCESS_ID_e;
        sState.selected = nextOwned(sState.selected);
        if (sState.selected >= 0) {
            layDown(link, sState.selected);
        }
        return PC_RPLUS_WEAPON_PUT_AWAY;
    }
    if (heldId != fpcM_ERROR_PROCESS_ID_e) {
        return PC_RPLUS_WEAPON_DROP;  // an enemy's: dropped; the next press draws one of Link's
    }
    if (sState.selected < 0 || !owned(sState.selected)) {
        sState.selected = nextOwned(-1);
    }
    layDown(link, sState.selected);
    return PC_RPLUS_WEAPON_NONE;
}

fopAc_ac_c* pc_rplus_weapon_ready(void) {
    if (sPending == fpcM_ERROR_PROCESS_ID_e || fpcM_IsCreating(sPending)) {
        return nullptr;
    }
    fopAc_ac_c* a = fopAcM_SearchByID(sPending);
    if (a == nullptr || fopAcM_checkCarryNow(a)) {
        return nullptr;
    }
    return a;
}

void pc_rplus_weapon_taken(void) {
    sPending = fpcM_ERROR_PROCESS_ID_e;
}

int pc_rplus_weapon_owns(unsigned int procId) {
    return procId != fpcM_ERROR_PROCESS_ID_e && procId == sOurs;
}

const char* pc_rplus_weapon_notice(void) {
    if (sNotice.empty() || std::chrono::steady_clock::now() > sNoticeEnd) {
        return nullptr;
    }
    return sNotice.c_str();
}
