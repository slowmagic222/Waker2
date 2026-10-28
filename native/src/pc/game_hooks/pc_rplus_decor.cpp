// Randomizer+ house decor (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): things Link finds about the generated world and
// keeps in his home (the cabana, Abesso), where he can carry them about and set them down where he likes.
//
// The catalog: what Link can lift or hold that is not an item (pots, jugs, barrels, stools, skulls, buckets, crates,
// seeds, stone figures, rocks, boulders, the pigs, the Tingle statues, the Tower of the Gods' stones and statues, the
// enemies' weapons) and the Nintendo Gallery's figurines, each on its own pedestal. The bomb flowers, stunned enemies,
// Medli, Makar and the Command Melody's statue are not decor (they blow up, or are people).
//
// The checks (Dillon, 2026-10-28): decor is a non-progress check reward, not something standing about. A check that
// holds only filler (rupees, hearts, refills: a chest, sunken treasure, a demo pickup, all through
// fopAcM_createItemForTrBoxDemo) also gives the next piece Link lacks, in an order the seed shuffles; the filler
// still comes with it ("Found decor: Barrel"). In the cabana every piece he has stands where he last set it down, or on the next free
// spot of a ring round the room's middle; there nothing breaks (d_a_tsubo, d_a_stone, d_a_stone2) and the figurines
// can be carried too (d_a_obj_figure).
//
// The save: rplus-decor-<seed>-<slot>.txt next to the memory card (COS_SAVE_DIR, else the settings folder), read when
// the file is loaded (dSv_info_c::card_to_memory) and written when the game saves (memory_to_card), so what is found
// and moved after the last save is gone again after a reset, as the game's own items are.
//
// COS_RPLUS_DECOR=0 gives no decor from checks; COS_RPLUS_DECOR_FIGURES=<n> is how many figurines stand in the
// cabana at once (the last n found, 48 by default: each loads its gallery room's archive and a heap of its own).
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "pc/pc_seedgen.h"
#include "pc_internal.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "d/d_com_inf_game.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_bg_s_lin_chk.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_overlap_mng.h"
#include "m_Do/m_Do_audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The game's global operator new takes a block of whatever JKR heap is current (pc_jkr_heap.cpp), and game heaps
// are freed wholesale: every list and string here is host memory, allocated in one of these scopes (fopAcM_create
// stays outside them, so actors still come from the game's heaps).
using Host = JKRPcHostAllocScope;

enum Kind { K_TSUBO, K_STONE, K_STONE2, K_PIG, K_VTIL, K_TRY, K_BOKO, K_FIGURE };

struct Decor {
    std::string key;    // in the save file
    std::string label;  // in the notice
    const char* actor;  // the stage name that makes it (its profile)
    u32 params;
    Kind kind;
    bool starter;       // Link's from the start (the cabana's own pots and stools)
};

constexpr int FIGURES = 0x86;  // d_a_obj_figure's TOTAL_FIGURE_COUNT

const std::vector<Decor>& catalog() {
    static const std::vector<Decor> c = [] {
        Host host;
        std::vector<Decor> v;
        // d_a_tsubo: the type in bits 24-27; no item (0x3F), shown at once (spec 0x3F), no saved item (0x7F)
        auto tsubo = [&](const char* key, const char* label, int type, bool starter) {
            v.push_back({key, label, "kotubo", 0x007F3F3Fu | (u32)type << 24, K_TSUBO, starter});
        };
        tsubo("pot", "Small pot", 0, true);
        tsubo("big_pot", "Large pot", 1, true);
        tsubo("jug", "Water jug", 2, false);
        tsubo("barrel", "Barrel", 3, true);
        tsubo("stool", "Stool", 4, true);
        tsubo("skull", "Skull", 5, false);
        tsubo("bucket", "Bucket", 6, true);
        tsubo("seed", "Spiky seed", 7, false);
        tsubo("crate", "Wooden crate", 8, true);
        tsubo("stone_fig1", "Stone figure", 9, false);
        tsubo("stone_fig2", "Stone figure (second)", 10, false);
        tsubo("stone_fig3", "Stone figure (third)", 11, false);
        tsubo("stone_fig4", "Stone figure (fourth)", 12, false);
        tsubo("seed2", "Spiky seed (small)", 13, false);
        tsubo("kutani", "Kutani pot", 14, false);
        tsubo("kkiba", "Odd prop", 15, false);
        // d_a_stone (koisi: types 0, 1), d_a_stone2 (M_attr: 2 Ebrock, 3 Ekao; 0 and 1 have no archive): the type in bits
        // 24-26; no item, switch or saved item
        v.push_back({"rock_small", "Small rock", "koisi1", 0x007FFF3Fu, K_STONE, false});
        v.push_back({"rock", "Rock", "koisi1", 0x017FFF3Fu, K_STONE, false});
        v.push_back({"boulder", "Boulder", "Ebrock", 0x027FFF3Fu, K_STONE2, false});
        v.push_back({"stone_head", "Stone head", "Ebrock", 0x037FFF3Fu, K_STONE2, false});
        // d_a_kb: its look (bits 0-3), roaming 100 units round where it stands (4-7), no big pig or path
        static const char* const kPigs[3] = {"Pig", "Pig (second look)", "Pig (third look)"};
        for (int i = 0; i < 3; i++) {
            v.push_back({"pig" + std::to_string(i), kPigs[i], "Pig", 0xFFFFFF10u | (u32)i, K_PIG, false});
        }
        // d_a_obj_vtil: the type (bits 0-3), shown whatever its dungeon's chest says (pc_rplus_decor_owns)
        static const char* const kTingle[5] = {"Dragon Tingle Statue", "Forbidden Tingle Statue",
                                               "Goddess Tingle Statue", "Earth Tingle Statue", "Wind Tingle Statue"};
        for (int i = 0; i < 5; i++) {
            v.push_back({"tingle" + std::to_string(i), kTingle[i], "Vtil1", (u32)i, K_VTIL, false});
        }
        // d_a_obj_try: the type (bits 0-3), no switch (0xFF, bits 8-15)
        static const struct { int type; const char* label; } kTry[] = {
            {0, "Tower stone"}, {1, "Tower stone (second)"}, {2, "Tower stone (third)"}, {3, "Tower stone (fourth)"},
            {4, "Tower stone (fifth)"}, {5, "Tower statue"}, {7, "Tower statue (second)"},
        };
        for (const auto& t : kTry) {
            v.push_back({"tower" + std::to_string(t.type), t.label, "Hseki1", 0x0000FF00u | (u32)t.type, K_TRY, false});
        }
        // d_a_boko: the weapon is the params
        static const char* const kBoko[6] = {"Boko stick", "Machete", "Stalfos mace", "Darknut sword", "Moblin spear",
                                             "Phantom Ganon's sword"};
        for (int i = 0; i < 6; i++) {
            v.push_back({"weapon" + std::to_string(i), kBoko[i], "Boko", (u32)i, K_BOKO, false});
        }
        for (int i = 0; i < FIGURES; i++) {
            v.push_back({"figure" + std::to_string(i), "Figurine " + std::to_string(i + 1), "Figure", (u32)i,
                         K_FIGURE, false});
        }
        return v;
    }();
    return c;
}

// What Link has: found (order: when, 0 = not found) and where it stands in the cabana.
struct Piece {
    int order = 0;
    bool placed = false;
    float x = 0, y = 0, z = 0;
    s16 angle = 0;
};

struct State {
    std::vector<Piece> p;
    int nextOrder = 1;
};

State sLive;  // this game, since the file was loaded

State fresh() {
    Host host;
    State s;
    s.p.resize(catalog().size());
    for (size_t i = 0; i < s.p.size(); i++) {
        if (catalog()[i].starter) {
            s.p[i].order = s.nextOrder++;
        }
    }
    return s;
}

bool enabled() {
    const char* v = getenv("COS_RPLUS_DECOR");
    return v == nullptr || strcmp(v, "0") != 0;
}

std::string savePath(int slot) {
    const char* saves = getenv("COS_SAVE_DIR");
    std::string dir = saves != nullptr && saves[0] != '\0' ? saves : pc::seedgen::userDir();
    return dir + "/rplus-decor-" + std::to_string(pc_rplus_seed()) + "-" + std::to_string(slot) + ".txt";
}

u32 mix(u32 a, u32 b) {
    u32 h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

struct Live {
    fpc_ProcID id;
    int decor;
    int missing;  // frames not found (still being made, or its room went)
};

std::vector<Live> sHome;   // the pieces standing in the cabana

fpc_ProcID spawn(int decor, const cXyz& at, s16 ay, int roomNo) {
    const Decor& d = catalog()[decor];
    cXyz pos = at;
    // d_a_tsubo's and d_a_stone2's angle z is their switch and event (0xFF: none)
    csXyz angle(0, ay, d.kind == K_TSUBO || d.kind == K_STONE2 ? 0x00FF : 0);
    char name[9] = {};
    strncpy(name, d.actor, 8);
    return fopAcM_create(name, d.params, &pos, roomNo, &angle, nullptr);
}

// ---- The notice

std::string sNotice;
std::chrono::steady_clock::time_point sNoticeEnd;

int foundCount() {
    int n = 0;
    for (const Piece& p : sLive.p) {
        n += p.order > 0;
    }
    return n;
}

void found(int decor) {
    Piece& p = sLive.p[decor];
    if (p.order > 0) {
        return;
    }
    p.order = sLive.nextOrder++;
    p.placed = false;
    char line[160];
    snprintf(line, sizeof line, pc::pc_ui_spanish() ? "Decoración: %s (%d de %d), ya está en casa"
                                                : "Found decor: %s (%d of %d), it's at home now",
             catalog()[decor].label.c_str(), foundCount(), (int)catalog().size());
    {
        Host host;
        sNotice = line;
    }
    sNoticeEnd = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    fprintf(stderr, "[rplus] decor: found %s\n", catalog()[decor].key.c_str());
}

// ---- The cabana

std::vector<cXyz> sHomeHints;  // where the cabana's own dishes and pots stood (its floor)
bool sHomeSpawned = false;
int sHomeFrames = 0;

bool spotFree(const cXyz& c, const std::vector<cXyz>& used) {
    for (const cXyz& u : used) {
        if (hypotf(u.x - c.x, u.z - c.z) < 100.0f) {
            return false;
        }
    }
    return true;
}

// The next free spot of the rings round mid (110 units apart), on floor about as high as mid's and in sight of it.
bool nextSpot(const cXyz& mid, std::vector<cXyz>& used, cXyz* out) {
    for (int ring = 0; ring <= 14; ring++) {
        const float r = ring * 110.0f;
        const int count = ring == 0 ? 1 : (int)(2.0f * 3.14159f * r / 110.0f);
        for (int k = 0; k < count; k++) {
            const float a = (k + 0.5f * (ring & 1)) * 2.0f * 3.14159f / count;
            cXyz c(mid.x + r * sinf(a), mid.y, mid.z + r * cosf(a));
            if (!spotFree(c, used)) {
                continue;
            }
            dBgS_ObjGndChk gnd;
            cXyz top(c.x, mid.y + 150.0f, c.z);
            gnd.SetPos(&top);
            const f32 h = dComIfG_Bgsp()->GroundCross(&gnd);
            if (h < mid.y - 60.0f || h > mid.y + 60.0f) {
                continue;
            }
            dBgS_LinChk lin;
            cXyz from(mid.x, mid.y + 60.0f, mid.z), to(c.x, h + 60.0f, c.z);
            lin.Set(&from, &to, nullptr);
            if (ring > 0 && dComIfG_Bgsp()->LineCross(&lin)) {
                continue;
            }
            c.y = h;
            used.push_back(c);
            *out = c;
            return true;
        }
    }
    return false;
}

void spawnHome(fopAc_ac_c* link) {
    cXyz mid = link->current.pos;
    if (!sHomeHints.empty()) {
        cXyz sum(0, 0, 0);
        for (const cXyz& h : sHomeHints) {
            sum += h;
        }
        mid.set(sum.x / sHomeHints.size(), link->current.pos.y, sum.z / sHomeHints.size());
        dBgS_ObjGndChk gnd;
        cXyz top(mid.x, mid.y + 150.0f, mid.z);
        gnd.SetPos(&top);
        const f32 h = dComIfG_Bgsp()->GroundCross(&gnd);
        mid.y = h > mid.y - 300.0f ? h : link->current.pos.y;
    }
    // what stands where, worked out in host memory; then the actors, from the game's heaps
    struct Plan {
        int decor;
        cXyz at;
        s16 angle;
    };
    std::vector<Plan> plan;
    {
        Host host;
        // the last COS_RPLUS_DECOR_FIGURES figurines found
        int figCap = 48;
        if (const char* v = getenv("COS_RPLUS_DECOR_FIGURES"); v != nullptr && v[0] != '\0') {
            figCap = atoi(v);
        }
        std::vector<int> orders;
        for (size_t i = 0; i < catalog().size(); i++) {
            if (catalog()[i].kind == K_FIGURE && sLive.p[i].order > 0) {
                orders.push_back(sLive.p[i].order);
            }
        }
        int figOrderMin = 0;
        if (figCap >= 0 && (int)orders.size() > figCap) {
            std::sort(orders.begin(), orders.end());
            figOrderMin = figCap == 0 ? 0x7FFFFFFF : orders[orders.size() - figCap];
        }

        std::vector<cXyz> used;
        used.push_back(link->current.pos);  // the doorway
        for (const Piece& p : sLive.p) {
            if (p.order > 0 && p.placed) {
                used.push_back(cXyz(p.x, p.y, p.z));
            }
        }
        for (size_t i = 0; i < catalog().size(); i++) {
            const Piece& p = sLive.p[i];
            if (p.order == 0 || (catalog()[i].kind == K_FIGURE && p.order < figOrderMin)) {
                continue;
            }
            cXyz at;
            if (p.placed) {
                at.set(p.x, p.y, p.z);
            } else if (!nextSpot(mid, used, &at)) {
                at = mid;
            }
            plan.push_back({(int)i, at, p.placed ? p.angle : (s16)(mix((u32)i, 7) & 0xFFFF)});
        }
        sHome.reserve(sHome.size() + plan.size());
    }
    const int roomNo = fopAcM_GetRoomNo(link);
    int shown = 0;
    for (const Plan& pl : plan) {
        const fpc_ProcID id = spawn(pl.decor, pl.at, pl.angle, roomNo);
        if (id != fpcM_ERROR_PROCESS_ID_e) {
            sHome.push_back({id, pl.decor, 0});
            shown++;
        }
    }
    fprintf(stderr, "[rplus] decor: %d pieces in Link's home (%d found)\n", shown, foundCount());
}

void homeFrame() {
    for (size_t i = 0; i < sHome.size(); i++) {
        Live& h = sHome[i];
        fopAc_ac_c* a = fopAcM_SearchByID(h.id);
        if (a == nullptr || fopAcM_checkCarryNow(a) || fabsf(a->speed.y) > 0.5f || fabsf(a->speedF) > 0.5f) {
            continue;
        }
        Piece& p = sLive.p[h.decor];
        p.placed = true;
        p.x = a->current.pos.x, p.y = a->current.pos.y, p.z = a->current.pos.z;
        p.angle = a->shape_angle.y;
    }
}

} // namespace

void pc_rplus_decor_reset(void) {
    Host host;
    sLive = fresh();
}

void pc_rplus_decor_load(int slot) {
    Host host;
    sLive = fresh();
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(savePath(slot).c_str(), "r");
    if (f == nullptr) {
        return;
    }
    // "<key> <order>" or "<key> <order> <x> <y> <z> <angle>"; order 0: not found (a starting piece given away)
    char line[256];
    int read = 0;
    while (fgets(line, sizeof line, f) != nullptr) {
        char key[64];
        int order = 0, angle = 0;
        float x, y, z;
        const int n = sscanf(line, "%63s %d %f %f %f %d", key, &order, &x, &y, &z, &angle);
        if (n < 2 || key[0] == '#') {
            continue;
        }
        for (size_t i = 0; i < catalog().size(); i++) {
            if (catalog()[i].key != key) {
                continue;
            }
            Piece& p = sLive.p[i];
            p.order = order;
            p.placed = n == 6;
            if (p.placed) {
                p.x = x, p.y = y, p.z = z, p.angle = (s16)angle;
            }
            sLive.nextOrder = order >= sLive.nextOrder ? order + 1 : sLive.nextOrder;
            read++;
        }
    }
    fclose(f);
    fprintf(stderr, "[rplus] decor: file %d read (%d found)\n", slot, foundCount());
}

void pc_rplus_decor_save(int slot) {
    if (!pc_rplus_active()) {
        return;
    }
    Host host;
    const std::string path = savePath(slot);
    FILE* f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        fprintf(stderr, "[rplus] decor: cannot write %s\n", path.c_str());
        return;
    }
    fprintf(f, "# Randomizer+ house decor (pc_rplus_decor.cpp): <piece> <found order> [<x> <y> <z> <angle>]\n");
    for (size_t i = 0; i < catalog().size(); i++) {
        const Piece& p = sLive.p[i];
        if (p.order == 0 && !catalog()[i].starter) {
            continue;
        }
        if (p.placed) {
            fprintf(f, "%s %d %.1f %.1f %.1f %d\n", catalog()[i].key.c_str(), p.order, p.x, p.y, p.z, (int)p.angle);
        } else {
            fprintf(f, "%s %d\n", catalog()[i].key.c_str(), p.order);
        }
    }
    fclose(f);
}

int pc_rplus_decor_check(int itemNo) {
    if (!pc_rplus_active() || !enabled()) {
        return 0;
    }
    // filler: a heart, rupees, magic, bombs, arrows, a fairy, three hearts
    static const u8 kFiller[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0A, 0x0B,
                                 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x16, 0x1E};
    bool filler = false;
    for (u8 f : kFiller) {
        filler = filler || itemNo == f;
    }
    if (!filler) {
        return 0;
    }
    if (sLive.p.empty()) {
        Host host;
        sLive = fresh();
    }
    // the pieces Link lacks, the seed's order: the lowest key mix(piece, seed) first
    int best = -1;
    u32 bestKey = 0;
    for (size_t i = 0; i < sLive.p.size(); i++) {
        if (sLive.p[i].order > 0) {
            continue;
        }
        const u32 key = mix((u32)i, pc_rplus_seed() ^ 0xDEC0u);
        if (best < 0 || key < bestKey) {
            best = (int)i, bestKey = key;
        }
    }
    if (best < 0) {
        return 0;
    }
    found(best);
    return 1;
}

void pc_rplus_decor_home_slot(const cXyz& pos) {
    if (!sHomeSpawned) {
        Host host;
        sHomeHints.push_back(pos);
    }
}

void pc_rplus_decor_frame(void) {
    if (!pc_rplus_active()) {
        return;
    }
    fopAc_ac_c* link = dComIfGp_getLinkPlayer();
    if (link == nullptr) {
        return;
    }
    if (sLive.p.empty()) {
        Host host;
        sLive = fresh();
    }
    if (!pc_rplus_home_unlocked()) {
        if (sHomeSpawned || !sHome.empty()) {
            sHome.clear();
            sHomeHints.clear();
        }
        sHomeSpawned = false;
        sHomeFrames = 0;
        return;
    }
    // in the cabana: its pieces once Link stands in it (its floor's collision is there), the door's wipe and event over
    if (!sHomeSpawned) {
        sHomeFrames++;
        if (((sHomeFrames >= 20 && !sHomeHints.empty()) || sHomeFrames >= 120) && !dComIfGp_event_runCheck() &&
            !fopOvlpM_IsDoingReq()) {
            spawnHome(link);
            sHomeSpawned = true;
        }
        return;
    }
    homeFrame();
}

int pc_rplus_decor_owns(unsigned int procId) {
    return pc_rplus_decor_in_home(procId);
}

int pc_rplus_decor_in_home(unsigned int procId) {
    for (const Live& l : sHome) {
        if (l.id == procId) {
            return 1;
        }
    }
    return 0;
}

const char* pc_rplus_decor_notice(void) {
    if (sNotice.empty() || std::chrono::steady_clock::now() > sNoticeEnd) {
        return nullptr;
    }
    return sNotice.c_str();
}
