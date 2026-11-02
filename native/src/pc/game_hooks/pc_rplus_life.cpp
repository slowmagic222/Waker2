// Randomizer+ (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): the generated world's people and enemies.
//
// The disc's sea rooms are read once (each Room<n>.arc's room.dzr): their townsfolk and other NPCs, and the
// enemies of their islands, with the params the game gave them. A generated room's own island is gone, so its
// people are moved: an NPC the world gave a place of its own (an anchor: Maggie, Zunari, Tott...) stands at that
// anchor; the rest of the townsfolk go to the generated towns. Enemies are spread over open land by biome, from
// the kinds and params the islands and some dungeons have.
//
// COS_RPLUS_NPCS=0 or COS_RPLUS_ENEMIES=0 leaves them out.
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "pc/pc_seedgen.h"
#include "pc_rplus_world.h"
#include "pc_internal.h"
#include "d/d_item_data.h"
#include "d/d_bg_s_gnd_chk.h"
#include "res/Object/Aygr.h"
#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "d/d_camera.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_audio.h"
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// One actor of a disc room: its name and what the room's ACTR (layer -1) or ACT<layer> chunk gave it.
struct Entry {
    char name[9];
    u32 params;
    float x, y, z;
    s16 ax, ay, az;
    s16 proc;    // its profile (dStage_searchName)
    int room;    // the disc room it came from
    int layer;   // -1: every layer
};

u32 be32(const u8* p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

float bef(const u8* p) {
    u32 v = be32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// The actors of a room.dzr: ACTR and ACT0..ACTb, and its chests: TRES and TRE0..TREb (each entry 0x20 bytes).
void parseDzr(const std::vector<uint8_t>& d, int room, std::vector<Entry>& out) {
    if (d.size() < 4) {
        return;
    }
    const u32 chunks = be32(d.data());
    for (u32 c = 0; c < chunks && 4 + (size_t)c * 12 + 12 <= d.size(); c++) {
        const u8* h = d.data() + 4 + c * 12;
        int layer;
        if (memcmp(h, "ACTR", 4) == 0 || memcmp(h, "TRES", 4) == 0) {
            layer = -1;
        } else if ((memcmp(h, "ACT", 3) == 0 || memcmp(h, "TRE", 3) == 0) &&
                   ((h[3] >= '0' && h[3] <= '9') || (h[3] >= 'a' && h[3] <= 'b'))) {
            layer = h[3] <= '9' ? h[3] - '0' : h[3] - 'a' + 10;
        } else {
            continue;
        }
        const u32 num = be32(h + 4), off = be32(h + 8);
        for (u32 i = 0; i < num && off + (size_t)(i + 1) * 0x20 <= d.size(); i++) {
            const u8* a = d.data() + off + i * 0x20;
            Entry e{};
            memcpy(e.name, a, 8);
            e.name[8] = '\0';
            e.params = be32(a + 8);
            e.x = bef(a + 0x0C);
            e.y = bef(a + 0x10);
            e.z = bef(a + 0x14);
            e.ax = (s16)((a[0x18] << 8) | a[0x19]);
            e.ay = (s16)((a[0x1A] << 8) | a[0x1B]);
            e.az = (s16)((a[0x1C] << 8) | a[0x1D]);
            const dStage_objectNameInf* inf = dStage_searchName(e.name);
            if (inf == nullptr) {
                continue;
            }
            e.proc = inf->procname;
            e.room = room;
            e.layer = layer;
            out.push_back(e);
        }
    }
}

bool readRoomDzr(const char* stage, int room, std::vector<uint8_t>& out) {
    char path[64];
    snprintf(path, sizeof(path), "/res/Stage/%s/Room%d.arc", stage, room);
    static const char* const kInner[] = {"dzr/room.dzr", "room.dzr", "room/dzr/room.dzr"};
    for (const char* in : kInner) {
        if (pc::readDiscFile(path, in, out)) {
            return true;
        }
    }
    return false;
}

// The NPCs that may be moved: townsfolk and others who stand about outdoors. Left out: the story's people, the
// ones that live on the sea or in one place's machinery, and ones that need a path of their room
// (d_a_npc_btsw2 reads it unchecked).
const s16 kMovable[] = {
    fpcNm_NPC_PEOPLE_e, fpcNm_NPC_KO1_e,  fpcNm_NPC_KO2_e,  fpcNm_NPC_YM1_e,  fpcNm_NPC_YM2_e,  fpcNm_NPC_YW1_e,
    fpcNm_NPC_OB1_e,    fpcNm_NPC_AJ1_e,  fpcNm_NPC_KM1_e,  fpcNm_NPC_BM1_e,  fpcNm_NPC_BM2_e,  fpcNm_NPC_BM3_e,
    fpcNm_NPC_BM4_e,    fpcNm_NPC_BM5_e,  fpcNm_NPC_BJ1_e,  fpcNm_NPC_BJ2_e,  fpcNm_NPC_BJ3_e,  fpcNm_NPC_BJ4_e,
    fpcNm_NPC_BJ5_e,    fpcNm_NPC_BJ6_e,  fpcNm_NPC_BJ7_e,  fpcNm_NPC_BJ8_e,  fpcNm_NPC_BJ9_e,  fpcNm_NPC_KK1_e,
    fpcNm_NPC_PM1_e,    fpcNm_NPC_KP1_e,  fpcNm_NPC_PF1_e,  fpcNm_NPC_GP1_e,  fpcNm_NPC_KF1_e,  fpcNm_NPC_TT_e,
    fpcNm_NPC_HR_e,     fpcNm_NPC_HO_e,   fpcNm_NPC_MK_e,   fpcNm_NPC_UK_e,   fpcNm_NPC_CO1_e,  fpcNm_NPC_MT_e,
    fpcNm_NPC_MN_e,     fpcNm_NPC_AH_e,   fpcNm_NPC_PHOTO_e, fpcNm_NPC_KG1_e, fpcNm_NPC_KG2_e,  fpcNm_NPC_GK1_e,
    fpcNm_NPC_AC1_e,    fpcNm_NPC_HI1_e,  fpcNm_NPC_RSH1_e, fpcNm_NPC_ZK1_e,  fpcNm_NPC_DS1_e,
    fpcNm_NPC_BMCON1_e,
};

bool movable(s16 proc) {
    for (s16 p : kMovable) {
        if (p == proc) {
            return true;
        }
    }
    return false;
}

// The world's anchors with an NPC of their own (scripts/rplus/anchors.py): the profile that stands there.
struct AnchorNpc {
    const char* id;
    s16 proc;
};
const AnchorNpc kAnchorNpcs[] = {
    {"killer_bees", fpcNm_NPC_MK_e},    {"maggie", fpcNm_NPC_KP1_e},        {"tott", fpcNm_NPC_TT_e},
    {"zunari", fpcNm_NPC_RSH1_e},       {"barrel_shooting", fpcNm_NPC_KG2_e}, {"flight_platform", fpcNm_NPC_BMCON1_e},
    {"lighthouse", fpcNm_NPC_PEOPLE_e}, {"sam", fpcNm_NPC_PEOPLE_e},
    {"town_gate", fpcNm_NPC_PEOPLE_e},  {"mila", fpcNm_NPC_PEOPLE_e},       {"pompie_vera", fpcNm_NPC_PEOPLE_e},
    {"kamo", fpcNm_NPC_PEOPLE_e},       {"minenco", fpcNm_NPC_PEOPLE_e},    {"linda_anton", fpcNm_NPC_PEOPLE_e},
};

// Enemies by biome letter, with how many come together.
struct EnemyKind {
    const char* name;
    const char* biomes;  // "b": the beach
    u8 groupMin, groupMax;
    u8 weight;
    u32 clearPath;  // params bits to set (no path, no switch) for kinds whose layout is known; 0 none
};
const EnemyKind kEnemies[] = {
    {"Bk", "gfr", 1, 2, 10, 0xFFFF0000u},      // Bokoblins (d_a_bk: bits 16-23 path, 24-31 switch)
    {"mo2", "rf", 1, 1, 4, 0xFFFF0000u},       // Moblins (d_a_mo2: 16-23 path, 24-31 spawn switch)
    {"c_green", "gfd", 2, 4, 10, 0xFF000000u}, // ChuChus (d_a_cc: 24-31 spawn switch)
    {"c_red", "rv", 1, 3, 6, 0xFF000000u},
    {"c_blue", "s", 1, 2, 3, 0xFF000000u},
    {"c_kiiro", "db", 1, 3, 4, 0xFF000000u},
    {"keeth", "grfs", 2, 4, 6, 0x00FF0000u},   // Keese (d_a_ki: 16-23 path)
    {"Fkeeth", "v", 2, 4, 6, 0x00FF0000u},
    {"bbaba", "f", 1, 2, 6, 0},                // Boko Babas
    {"magtail", "v", 1, 1, 2, 0},
};
constexpr int ENEMY_GROUPS_PER_ROOM = 9;

struct Pools {
    bool read = false;
    std::vector<Entry> npcs;               // movable NPCs of the generated sea rooms
    std::vector<Entry> enemies;            // one entry per (kind, params) seen
    std::vector<Entry> campDecor;          // a camp's fire and pots, as the disc has them (first seen of each)
    std::vector<Entry> chests;             // the chests of the generated sea rooms, in disc order
};
const char* const kCampDecor[] = {"Fire", "kotubo", "ootubo1"};

Pools& pools() {
    static Pools p;
    if (p.read) {
        return p;
    }
    p.read = true;
    std::vector<uint8_t> dzr;
    std::vector<Entry> all;
    int rooms = 0;
    for (int r = 1; r <= 49; r++) {
        if (readRoomDzr("sea", r, dzr)) {
            std::vector<Entry> here;
            parseDzr(dzr, r, here);
            for (const Entry& e : here) {
                if (movable(e.proc) && pc_rplus_sea_room_generated(r)) {
                    p.npcs.push_back(e);
                }
                if (e.proc == fpcNm_TBOX_e && pc_rplus_sea_room_generated(r)) {
                    p.chests.push_back(e);
                }
            }
            all.insert(all.end(), here.begin(), here.end());
            rooms++;
        }
    }
    // A few dungeons for the enemies the islands lack (fire Keese, Magtails, Boko Babas, Moblins).
    // The Private Oasis's cabana for a campfire.
    static const char* const kDungeons[] = {"M_NewD2", "kindan", "Siren", "M_Dai", "kaze", "Abesso"};
    for (const char* st : kDungeons) {
        for (int r = 0; r < 24; r++) {
            if (readRoomDzr(st, r, dzr)) {
                parseDzr(dzr, -1, all);
            }
        }
    }
    for (const Entry& e : all) {
        for (const char* dn : kCampDecor) {
            bool seen = false;
            for (const Entry& o : p.campDecor) {
                seen = seen || strcmp(o.name, dn) == 0;
            }
            if (!seen && strcmp(e.name, dn) == 0) {
                p.campDecor.push_back(e);
            }
        }
        for (const EnemyKind& k : kEnemies) {
            if (strcmp(e.name, k.name) != 0) {
                continue;
            }
            bool seen = false;
            for (const Entry& o : p.enemies) {
                seen = seen || (strcmp(o.name, e.name) == 0 && o.params == e.params);
            }
            if (!seen) {
                p.enemies.push_back(e);
            }
        }
    }
    fprintf(stderr, "[rplus] read %d sea rooms: %d NPCs to move, %d enemy kinds and params\n", rooms,
            (int)p.npcs.size(), (int)p.enemies.size());
    return p;
}

bool enabled(const char* env) {
    const char* v = getenv(env);
    return v == nullptr || strcmp(v, "0") != 0;
}

u32 mix(u32 a, u32 b) {
    u32 h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

// Size variants (Dillon, 2026-11-02: "Bigger versions or small versions?"): now and then a Bokoblin, Moblin or
// Darknut comes big (1.4x, twice the health: d_a_bk/d_a_mo2/d_a_tn under TARGET_PC) or small (0.7x), picked by its
// spot and the seed so it is the same each visit. Only the model grows: the game's hit cylinders stay as they are, so
// the sizes stay modest. COS_RPLUS_SIZES=0 leaves everyone their own size.
float sizeVariant(const char* name, float x, float z) {
    struct Kind {
        const char* name;
        u8 bigPct, smallPct;
    };
    static const Kind kKinds[] = {{"Bk", 12, 12}, {"mo2", 18, 4}, {"Tn", 15, 0}};
    static const int on = enabled("COS_RPLUS_SIZES");
    if (!on) {
        return 1.0f;
    }
    for (const Kind& k : kKinds) {
        if (strcmp(k.name, name) != 0) {
            continue;
        }
        const u32 h = mix((u32)(s32)x * 31u ^ (u32)(s32)z, pc_rplus_seed() ^ 0x512Eu) % 100u;
        if (h < k.bigPct) {
            return 1.4f;
        }
        if (h < (u32)(k.bigPct + k.smallPct)) {
            return 0.7f;
        }
        return 1.0f;
    }
    return 1.0f;
}

// At (x, y, z): y below -1e8 is the ground's there.
fpc_ProcID spawnAt(const Entry& e, float x, float y, float z, s16 ay, int roomNo, u32 params) {
    if (strcmp(e.name, "Bk") == 0) {
        // A Bokoblin's type (bits 0-3, d_a_bk jyunkai): sitting by a fire (4, 10), asleep until a switch (6), on a
        // path (9) or in an event (8) only wake in their own scenes; here they all stand and look out (0).
        const u32 type = params & 0xF;
        if (type == 4 || type == 6 || type == 8 || type == 9 || type == 10) {
            params &= ~0xFu;
        }
    }
    cXyz pos(x, y < -1e8f ? rplus::groundY(x, z) + 5.0f : y, z);
    csXyz angle(e.ax, ay, e.az);
    // By name, so the profile's argument (a ChuChu's colour, a person's look) comes with it.
    char name[9];
    memcpy(name, e.name, sizeof(name));
    cXyz scale(1.0f, 1.0f, 1.0f);
    const float size = sizeVariant(e.name, x, z);
    if (size != 1.0f) {
        scale.set(size, size, size);
    }
    return fopAcM_create(name, params, &pos, roomNo, &angle, &scale);
}

fpc_ProcID spawn(const Entry& e, float x, float z, s16 ay, int roomNo, u32 params) {
    return spawnAt(e, x, -1e9f, z, ay, roomNo, params);
}

// The townsfolk and anchored NPCs whose new place is in roomNo.
void placeNpcs(int roomNo) {
    Pools& P = pools();
    if (P.npcs.empty()) {
        return;
    }
    const std::vector<rplus::TownInfo> towns = rplus::towns();
    // Which NPCs belong to which anchor: by profile, in order, so each anchor gets its own.
    std::vector<int> taken(P.npcs.size(), 0);
    std::vector<std::pair<int, std::pair<float, float>>> anchored;  // npc index, spot
    for (int i = 0; i < pc_rplus_anchor_count(); i++) {
        const char *id, *kind, *stage;
        int cx, cy;
        if (!pc_rplus_anchor(i, &id, &kind, &stage, &cx, &cy)) {
            continue;
        }
        for (const AnchorNpc& an : kAnchorNpcs) {
            if (strcmp(an.id, id) != 0) {
                continue;
            }
            for (size_t k = 0; k < P.npcs.size(); k++) {
                if (!taken[k] && P.npcs[k].proc == an.proc) {
                    taken[k] = 1;
                    float x, z;
                    rplus::cellSpot(cx, cy, &x, &z);
                    anchored.push_back({(int)k, {x, z}});
                    break;
                }
            }
        }
    }
    int made = 0;
    for (const auto& a : anchored) {
        const Entry& e = P.npcs[a.first];
        const float x = a.second.first, z = a.second.second;
        if (rplus::roomAt(x, z) != roomNo) {
            continue;
        }
        if (e.layer >= 0 && e.layer != dComIfG_play_c::getLayerNo(e.room)) {
            continue;
        }
        spawn(e, x + 150.0f, z + 150.0f, e.ay, roomNo, e.params);
        made++;
    }
    // The rest of the townsfolk: shared out among the towns, each standing between the plaza and the houses.
    if (!towns.empty()) {
        for (size_t k = 0; k < P.npcs.size(); k++) {
            if (taken[k]) {
                continue;
            }
            const Entry& e = P.npcs[k];
            if (e.layer >= 0 && e.layer != dComIfG_play_c::getLayerNo(e.room)) {
                continue;
            }
            const u32 h = mix((u32)k, pc_rplus_seed());
            const rplus::TownInfo& t = towns[h % towns.size()];
            if (t.room != roomNo) {
                continue;
            }
            for (int tries = 0; tries < 8; tries++) {
                const u32 g = mix(h, (u32)tries);
                const float a = (g & 0xFFFF) / 65536.0f * 6.2831853f;
                const float r = t.plaza + 150.0f + ((g >> 16) & 0xFF) / 256.0f * std::max(100.0f, t.r - t.plaza - 650.0f);
                const float x = t.x + sinf(a) * r, z = t.z + cosf(a) * r;
                if (rplus::roomAt(x, z) == roomNo) {
                    spawn(e, x, z, (s16)(g >> 8), roomNo, e.params);
                    made++;
                    break;
                }
            }
        }
    }
    if (made > 0) {
        fprintf(stderr, "[rplus] room %d: %d NPCs\n", roomNo, made);
    }
}

// Gentle ground round (x, z): within r the ground rises or falls at most `rise` (enemies on steep ground lose their
// footing and stop fighting).
bool gentle(float x, float z, float r, float rise) {
    const float y = rplus::groundY(x, z);
    for (int k = 0; k < 8; k++) {
        const float a = k * 0.785398f;
        if (fabsf(rplus::groundY(x + sinf(a) * r, z + cosf(a) * r) - y) > rise) {
            return false;
        }
    }
    return true;
}

const Entry* findEntry(const std::vector<Entry>& v, const char* name) {
    for (const Entry& e : v) {
        if (strcmp(e.name, name) == 0) {
            return &e;
        }
    }
    return nullptr;
}

// ---- More enemy kinds (Dillon, 2026-11-02: "Random darknuts on islands or platforms. Those little spiky balls in a
// gang somewhere. Even shadow Gannons in a specific spot.")
//
// These are made from params of our own rather than the disc's (the dungeons' Darknuts and Morths wait on switches,
// paths and events of their rooms).

// A Darknut (d_a_tn, "Tn"): params bits 0-3 how it acts (0 roams and comes for Link, 4 guards its spot and goes back
// to it), 4-7 its armour's colour (0-5), 8-15 how near Link must come (x10), 16-23 path (none), 24-31 switch (none).
// Angle x bits 5-7: its gear (1 helmet, 3 helmet and shield, 4 shield and cape, 5 all three); angle z its death
// switch (none). Most wear the plain silver; now and then gold, black or red.
Entry darknut(u32 h, bool guard) {
    Entry e{};
    strcpy(e.name, "Tn");
    static const u8 kColours[] = {1, 1, 1, 1, 0, 2, 3, 4, 5};
    static const u8 kGear[] = {1, 1, 3, 3, 3, 4, 5, 5};
    const u32 colour = kColours[(h >> 4) % sizeof(kColours)];
    const u32 range = guard ? 0x40 : 0x64;  // 640 on a deck, 1000 on open land
    e.params = (guard ? 4u : 0u) | colour << 4 | range << 8 | 0xFFu << 16 | 0xFFu << 24;
    e.ax = (s16)(kGear[(h >> 9) % sizeof(kGear)] << 5);
    e.az = 0;
    return e;
}

// A Morth (d_a_ks, "kuro_s"): params 3 is one that crawls about on its own (0 and 1 are the dungeons' spawners).
Entry morth() {
    Entry e{};
    strcpy(e.name, "kuro_s");
    e.params = 3;
    return e;
}

// Phantom Ganon (d_a_fganon, "Fganon"): type 1, Ganon's Tower maze's: waits unseen till Link comes within 1280 (bits
// 8-15, x10), then fights; one good sword hit and he drops his sword and is gone. No switches (bits 16-31). As in the
// maze, he no longer comes once Ganon's Tower's Light Arrow chest is opened.
constexpr u32 PG_PARAMS = 1u | 0x80u << 8 | 0xFFu << 16 | 0xFFu << 24;

// ---- Camp chests: a camp whose Bokoblins and Moblin are all beaten gets a chest by its fire.
//
// Each camp has a number (its room's, two a room: room * CAMPS_PER_ROOM + its index), and the chest a small wooden
// one (d_a_tbox shape 0, a plain chest) holding filler the seed picks for that camp, given through
// fopAcM_createItemForTrBoxDemo like any chest, so it also brings the next house decor piece (pc_rplus_decor_check).
// The chest is marked as a camp's (params bits 24-31 0xC4, angle x bit 14, the camp in bits 6-13) and keeps its own
// opened flag here instead of the sea's chest bits (pc_rplus_camp_chest_key, daTbox_c::checkOpen / OpenInit_com).
//
// Lookout towers (Dillon, 2026-11-02): the sea's lookout platform (d_a_obj_aygr, "Aygr", with its ladder) standing on
// land, at most one a room. Its deck is found once the tower's collision is in (a ground check from above,
// pc_rplus_camps_frame), and enemies the seed picks stand on it (Bokoblins, ChuChus, now and then a Moblin; Keese
// about it). Half the towers have a chest on the deck from the start, as some of the sea's do; the rest get one there
// when their enemies are beaten, as a camp does. A tower's number is TOWER_KEY0 + its room.
//
// The save: rplus-camps-<seed>-<slot>.txt next to the memory card, like the house decor's: read when the file is
// loaded and written when the game saves. A camp beaten but its chest not opened has the chest standing at once the
// next time its room loads; an opened one an open, empty chest. The enemies come back, as the game's own do.
constexpr int CAMPS_PER_ROOM = 2;
constexpr int TOWER_KEY0 = 50 * CAMPS_PER_ROOM;
// The quick-test start's (pc_rplus_test_start): its chests, then its camp and its tower.
constexpr int TEST_CHESTS = 12;
constexpr int TEST_KEY0 = TOWER_KEY0 + 50;
constexpr int TEST_CAMP_KEY = TEST_KEY0 + TEST_CHESTS;
constexpr int TEST_TOWER_KEY = TEST_CAMP_KEY + 1;
constexpr int PG_KEY = TEST_TOWER_KEY + 1;      // Phantom Ganon's haunt: his chest when he is beaten
constexpr int CAMP_KEYS = PG_KEY + 1;  // under 256: the key is 8 bits of the chest's angle x
constexpr int CAMP_ENEMIES = 8;
constexpr int CAMPS_LIVE = 16;
constexpr u32 CAMP_CHEST_PARAMS = 0xC4000000u | 0xFFu << 12 | 0x1Fu << 7;  // no switch, no chest bit, type 0, shape 0
enum CampState : u8 { CAMP_NEW, CAMP_BEATEN, CAMP_OPENED };

// Fixed arrays, not host lists: nothing here is taken from the game's heaps (pc_rplus_decor.cpp, Host).
u8 sCampState[CAMP_KEYS];

// A camp of a loaded room whose enemies are being watched.
struct CampLive {
    bool used;
    int key, room;
    float x, z;  // the fire, or the tower
    float y;     // a tower's deck (-1e9: a camp, its chest on the ground)
    int n;
    fpc_ProcID ids[CAMP_ENEMIES];
    bool seen[CAMP_ENEMIES];  // made: one that never was (its spot failed) doesn't count
};
CampLive sCamps[CAMPS_LIVE];

// The camp's filler: rupees mostly, now and then a silver rupee or three hearts.
u8 campItem(int key) {
    if (key == PG_KEY) {
        return dItemNo_SILVER_RUPEE_e;  // Phantom Ganon's
    }
    static const struct { u8 item, weight; } kItems[] = {
        {dItemNo_BLUE_RUPEE_e, 10},  {dItemNo_RED_RUPEE_e, 30},    {dItemNo_PURPLE_RUPEE_e, 30},
        {dItemNo_ORANGE_RUPEE_e, 18}, {dItemNo_SILVER_RUPEE_e, 4}, {dItemNo_TRIPLE_HEART_e, 8},
    };
    int total = 0;
    for (const auto& it : kItems) {
        total += it.weight;
    }
    int pick = (int)(mix((u32)key, pc_rplus_seed() ^ 0xC4E5u) % (u32)total);
    for (const auto& it : kItems) {
        if (pick < it.weight) {
            return it.item;
        }
        pick -= it.weight;
    }
    return dItemNo_RED_RUPEE_e;
}

void spawnCampChest(int key, int roomNo, float fx, float fz, float deckY) {
    const u32 h = mix((u32)key, pc_rplus_seed() ^ 0x7B0Fu);
    if (deckY > -1e8f) {
        // A tower's: in the deck's middle.
        cXyz pos(fx, deckY, fz);
        csXyz angle((s16)(0x4000 | key << 6 | roomNo), (s16)(h >> 8), (s16)(campItem(key) << 8 | 0xFF));
        fopAcM_create((char*)"takara", CAMP_CHEST_PARAMS, &pos, roomNo, &angle, nullptr);
        fprintf(stderr, "[rplus] room %d: tower %d's chest (item 0x%02X%s)\n", roomNo, key, campItem(key),
                sCampState[key] == CAMP_OPENED ? ", opened" : "");
        return;
    }
    // Beside the fire, facing away from it (Link opens it coming from outside the camp).
    float x = fx, z = fz;
    for (int k = 0; k < 8; k++) {
        const float a = ((h & 7) + k) * 0.785398f;
        const float cx = fx + sinf(a) * 300.0f, cz = fz + cosf(a) * 300.0f;
        if (rplus::roomAt(cx, cz) == roomNo && !rplus::steep(cx, cz)) {
            x = cx, z = cz;
            break;
        }
    }
    cXyz pos(x, rplus::groundY(x, z), z);
    csXyz angle((s16)(0x4000 | key << 6 | roomNo), cM_atan2s(x - fx, z - fz), (s16)(campItem(key) << 8 | 0xFF));
    fopAcM_create((char*)"takara", CAMP_CHEST_PARAMS, &pos, roomNo, &angle, nullptr);
    fprintf(stderr, "[rplus] room %d: camp %d's chest (item 0x%02X%s)\n", roomNo, key, campItem(key),
            sCampState[key] == CAMP_OPENED ? ", opened" : "");
}

void watchCamp(int key, int roomNo, float x, float z, float deckY, const fpc_ProcID* ids, int n) {
    if (n == 0) {
        return;
    }
    CampLive* slot = nullptr;
    for (CampLive& c : sCamps) {
        if (c.used && c.key == key) {
            slot = &c;  // the room loaded again: the new enemies
            break;
        }
        if (!c.used && slot == nullptr) {
            slot = &c;
        }
    }
    if (slot == nullptr) {
        return;
    }
    *slot = CampLive{};
    slot->used = true;
    slot->key = key, slot->room = roomNo, slot->x = x, slot->z = z, slot->y = deckY;
    slot->n = std::min(n, CAMP_ENEMIES);
    for (int i = 0; i < slot->n; i++) {
        slot->ids[i] = ids[i];
    }
}

// Enemy camps: on open flat land away from the start and the towns, a campfire with Bokoblins round it (and
// sometimes a Moblin), a few pots by it. At most CAMPS_PER_ROOM a room.
int makeCamp(int key, int roomNo, float x, float z, u32 h);

int placeCamps(int roomNo, float rx0, float rz0, float sx, float sz, const std::vector<rplus::TownInfo>& towns) {
    if (findEntry(pools().enemies, "Bk") == nullptr) {
        return 0;
    }
    int camps = 0, made = 0;
    for (int tries = 0; tries < 300 && camps < CAMPS_PER_ROOM; tries++) {
        const u32 h = mix((u32)roomNo * 104729u + (u32)tries, pc_rplus_seed() ^ 0xCA3Bu);
        if (tries == 0 && (h & 3) == 0) {
            break;  // a quarter of the rooms have none
        }
        const float x = rx0 + (0.05f + (h & 0xFFFF) / 65536.0f * 0.9f) * 100000.0f;
        const float z = rz0 + (0.05f + ((h >> 16) & 0xFFFF) / 65536.0f * 0.9f) * 100000.0f;
        const char b = rplus::biomeAt(x, z);
        const float y = rplus::groundY(x, z);
        if (strchr("gfrd", b) == nullptr || y < 160.0f || rplus::steep(x, z) || !rplus::openGround(x, z, 1400.0f) ||
            !gentle(x, z, 1000.0f, 160.0f)) {
            continue;
        }
        if ((x - sx) * (x - sx) + (z - sz) * (z - sz) < 14000.0f * 14000.0f) {
            continue;
        }
        bool nearTown = false;
        for (const rplus::TownInfo& t : towns) {
            nearTown = nearTown || (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) < (t.r + 6000.0f) * (t.r + 6000.0f);
        }
        if (nearTown) {
            continue;
        }
        made += makeCamp(roomNo * CAMPS_PER_ROOM + camps, roomNo, x, z, h);
        camps++;
    }
    return made;
}

// One camp (its fire, enemies and pots) at (x, z); h picks its look. Returns the actors made.
int makeCamp(int key, int roomNo, float x, float z, u32 h) {
    Pools& P = pools();
    const Entry* bk = findEntry(P.enemies, "Bk");
    if (bk == nullptr) {
        return 0;
    }
    const Entry* mo = findEntry(P.enemies, "mo2");
    const Entry* fire = findEntry(P.campDecor, "Fire");
    const Entry* pot = findEntry(P.campDecor, "kotubo");
    if (pot == nullptr) {
        pot = findEntry(P.campDecor, "ootubo1");
    }
    int made = 0;
    {
        fpc_ProcID ids[CAMP_ENEMIES];
        int nIds = 0;
        if (fire != nullptr) {
            // No switch, no treasure-chest bit: always lit.
            spawn(*fire, x, z, 0, roomNo, (fire->params & ~0x1F0FFu) | 0x1F0FFu);
            made++;
        }
        const int n = 3 + (int)((h >> 5) % 3);
        for (int k = 0; k < n; k++) {
            const float a = (k + ((h >> 9) & 0xFF) / 256.0f) * 6.2831853f / n;
            const float ex = x + sinf(a) * 520.0f, ez = z + cosf(a) * 520.0f;
            if (rplus::roomAt(ex, ez) != roomNo || rplus::steep(ex, ez)) {
                continue;
            }
            const fpc_ProcID id = spawn(*bk, ex, ez, cM_atan2s(x - ex, z - ez), roomNo, bk->params | 0xFFFF0000u);
            if (id != fpcM_ERROR_PROCESS_ID_e && nIds < CAMP_ENEMIES) {
                ids[nIds++] = id;
            }
            made++;
        }
        if (mo != nullptr && ((h >> 13) & 1)) {
            const fpc_ProcID id = spawn(*mo, x + 900.0f, z, cM_atan2s(-1.0f, 0.0f), roomNo, mo->params | 0xFFFF0000u);
            if (id != fpcM_ERROR_PROCESS_ID_e && nIds < CAMP_ENEMIES) {
                ids[nIds++] = id;
            }
            made++;
        }
        if (pot != nullptr) {
            for (int k = 0; k < 2; k++) {
                const float a = ((h >> (17 + k * 3)) & 7) * 0.785f;
                spawn(*pot, x + sinf(a) * 1000.0f, z + cosf(a) * 1000.0f, 0, roomNo, pot->params);
                made++;
            }
        }
        if (sCampState[key] == CAMP_NEW) {
            watchCamp(key, roomNo, x, z, -1e9f, ids, nIds);
        } else {
            spawnCampChest(key, roomNo, x, z, -1e9f);
        }
        fprintf(stderr, "[rplus] room %d: enemy camp %d at %.0f %.0f (%d Bokoblins%s)\n", roomNo, key, x, z, n,
                mo != nullptr && ((h >> 13) & 1) ? " and a Moblin" : "");
    }
    return made;
}

void highSpot(float* x, float* z, float r);

// ---- Lookout towers (see "Camp chests" above)

constexpr int TOWERS_LIVE = 8;
constexpr int TOWER_WAIT_FRAMES = 90;  // how long to wait for the tower's collision before giving up on its deck

// A tower made, its deck not found yet.
struct TowerLive {
    bool used;
    int key, room;
    float x, z;
    fpc_ProcID id;
    int frames;
    u32 h;  // its seed (its facing)
    float lift;  // how far it was made over the ground
};
TowerLive sTowers[TOWERS_LIVE];

bool towerChestFromStart(int key) {
    if (key == TEST_TOWER_KEY) {
        return false;  // the quick-test tower: beat its deck for the chest
    }
    return (mix((u32)key, pc_rplus_seed() ^ 0x70E7u) & 1) != 0;
}

// The deck under (x, z): the first ground from high above, if it is well over the land (the tower's, not the land's).
bool deckAt(float x, float z, float* y) {
    const float land = rplus::groundY(x, z);
    dBgS_ObjGndChk gnd;
    cXyz top(x, land + 12000.0f, z);
    gnd.SetPos(&top);
    const f32 h = dComIfG_Bgsp()->GroundCross(&gnd);
    if (h < land + 300.0f || h > land + 11900.0f) {
        return false;
    }
    *y = h;
    return true;
}

// The deck's enemies and its chest.
void fillTower(TowerLive& t, float deckY) {
    Pools& P = pools();
    struct Pick {
        const char* name;
        u8 weight;
        u32 clearPath;
    };
    static const Pick kTop[] = {
        {"Bk", 6, 0xFFFF0000u}, {"c_red", 2, 0xFF000000u}, {"c_green", 2, 0xFF000000u}, {"mo2", 1, 0xFFFF0000u},
    };
    const u32 h = mix((u32)t.key, pc_rplus_seed() ^ 0x70E8u);
    fpc_ProcID ids[CAMP_ENEMIES];
    int nIds = 0;
    int n = 1 + (int)(h % 3);
    // One tower in three is held by a Darknut, guarding the deck (it walks back to its spot rather than off the edge),
    // with fewer others beside it. The quick-test tower always is.
    if (enabled("COS_RPLUS_DARKNUTS") && (t.key == TEST_TOWER_KEY || (h >> 24) % 3 == 0)) {
        const Entry dn = darknut(h, true);
        const float a = ((h >> 8) & 0xFF) / 256.0f * 6.2831853f + 3.14159f;
        const float ex = t.x + sinf(a) * 220.0f, ez = t.z + cosf(a) * 220.0f;
        float ey;
        if (!deckAt(ex, ez, &ey)) {
            ey = deckY;
        }
        const fpc_ProcID id = spawnAt(dn, ex, ey + 5.0f, ez, cM_atan2s(ex - t.x, ez - t.z), t.room, dn.params);
        if (id != fpcM_ERROR_PROCESS_ID_e) {
            ids[nIds++] = id;
        }
        n = std::max(0, n - 1);
    }
    for (int k = 0; k < n; k++) {
        const u32 g = mix(h, (u32)k + 7u);
        int total = 0;
        for (const Pick& p : kTop) {
            total += findEntry(P.enemies, p.name) != nullptr ? p.weight : 0;
        }
        if (total == 0) {
            break;
        }
        int pick = (int)(g % (u32)total);
        for (const Pick& p : kTop) {
            const Entry* e = findEntry(P.enemies, p.name);
            if (e == nullptr) {
                continue;
            }
            if (pick >= p.weight) {
                pick -= p.weight;
                continue;
            }
            // Round the deck's middle (the chest's), each on the deck where it stands.
            const float a = (k + ((h >> 8) & 0xFF) / 256.0f) * 6.2831853f / n;
            const float ex = t.x + sinf(a) * 180.0f, ez = t.z + cosf(a) * 180.0f;
            float ey;
            if (!deckAt(ex, ez, &ey)) {
                ey = deckY;
            }
            const fpc_ProcID id = spawnAt(*e, ex, ey + 5.0f, ez, cM_atan2s(ex - t.x, ez - t.z), t.room,
                                          e->params | p.clearPath);
            if (id != fpcM_ERROR_PROCESS_ID_e && nIds < CAMP_ENEMIES) {
                ids[nIds++] = id;
            }
            break;
        }
    }
    // Keese about the tower (not needed to beat it).
    const Entry* keese = findEntry(P.enemies, "keeth");
    if (keese != nullptr) {
        for (int k = 0; k < (int)((h >> 4) % 3); k++) {
            const float a = (h >> (12 + k * 4) & 15) * 0.3927f;
            spawnAt(*keese, t.x + sinf(a) * 600.0f, deckY + 400.0f, t.z + cosf(a) * 600.0f, 0, t.room,
                    keese->params | 0x00FF0000u);
        }
    }
    const u8 state = sCampState[t.key];
    if (state != CAMP_NEW || towerChestFromStart(t.key) || nIds == 0) {  // none made: nothing to beat
        spawnCampChest(t.key, t.room, t.x, t.z, deckY);
    } else {
        watchCamp(t.key, t.room, t.x, t.z, deckY, ids, nIds);
    }
    fprintf(stderr, "[rplus] room %d: tower %d's deck at %.0f (%d enemies%s)\n", t.room, t.key, deckY, nIds,
            state == CAMP_NEW && !towerChestFromStart(t.key) ? ", chest when beaten" : "");
}

// The tower stands on the sea with its ladder's foot under the water. On land it is lifted so the ladder's foot
// (the lowest point of its collision, Aygr's aygrh.dzb) is at the ground: Link then steps onto the ladder from the
// ground instead of being put at its foot under the land. Read from the first tower made (the archive is loaded
// then); that tower is made again at the lifted height.
float sTowerLift = -1e9f;  // unknown yet

void makeTower(int key, int roomNo, float x, float y, float z, u32 h);

float dzbMinY(int index, float* maxY) {
    cBgD_t* d = (cBgD_t*)dComIfG_getObjectRes("Aygr", index);
    if (d == nullptr || d->m_v_num <= 0) {
        return 1e9f;
    }
    float lo = 1e9f, hi = -1e9f;
    for (s32 i = 0; i < d->m_v_num; i++) {
        lo = std::min(lo, d->m_v_tbl[i].y);
        hi = std::max(hi, d->m_v_tbl[i].y);
    }
    *maxY = hi;
    return lo;
}

void towersFrame() {
    for (TowerLive& t : sTowers) {
        if (!t.used) {
            continue;
        }
        fopAc_ac_c* a = nullptr;
        if (!fopAcM_SearchByID(t.id, &a)) {
            t.used = false;  // gone (its room went, or it failed)
            continue;
        }
        if (a == nullptr) {
            continue;  // still being made
        }
        if (sTowerLift < -1e8f) {
            float ladderTop = 0, bodyTop = 0;
            const float ladderFoot = dzbMinY(dRes_INDEX_AYGR_DZB_AYGRH_e, &ladderTop);
            const float bodyFoot = dzbMinY(dRes_INDEX_AYGR_DZB_AYGR_e, &bodyTop);
            sTowerLift = ladderFoot < 1e8f ? -ladderFoot : 0.0f;
            fprintf(stderr,
                    "[rplus] lookout tower collision: ladder %.0f..%.0f, body %.0f..%.0f; lifted %.0f on land\n",
                    ladderFoot, ladderTop, bodyFoot, bodyTop, sTowerLift);
        }
        if (fabsf(t.lift - sTowerLift) > 1.0f) {
            // Made before the lift was known: made again at the lifted height (the ladder's collision is placed
            // once, when it is made).
            const TowerLive again = t;
            const float groundAt = a->home.pos.y - t.lift;
            t.used = false;
            fopAcM_delete(a);
            makeTower(again.key, again.room, again.x, groundAt, again.z, again.h);
            continue;
        }
        float deckY;
        if (deckAt(t.x, t.z, &deckY)) {
            t.used = false;
            fillTower(t, deckY);
        } else if (++t.frames > TOWER_WAIT_FRAMES) {
            t.used = false;
            fprintf(stderr, "[rplus] room %d: tower %d has no deck to stand on, left empty\n", t.room, t.key);
        }
    }
}


// One lookout tower in roomNo (most rooms): on a rise of gentle open land, away from the start and the towns.
void placeTower(int roomNo, float rx0, float rz0, float sx, float sz, const std::vector<rplus::TownInfo>& towns) {
    const int key = TOWER_KEY0 + roomNo;
    if (key >= CAMP_KEYS) {
        return;
    }
    for (int tries = 0; tries < 200; tries++) {
        const u32 h = mix((u32)roomNo * 15485863u + (u32)tries, pc_rplus_seed() ^ 0x70E9u);
        if (tries == 0 && h % 5 < 2) {
            return;  // two rooms in five have none
        }
        float x = rx0 + (0.08f + (h & 0xFFFF) / 65536.0f * 0.84f) * 100000.0f;
        float z = rz0 + (0.08f + ((h >> 16) & 0xFFFF) / 65536.0f * 0.84f) * 100000.0f;
        if (rplus::biomeAt(x, z) == '.') {
            continue;
        }
        highSpot(&x, &z, 1200.0f);
        const float y = rplus::groundY(x, z);
        if (rplus::roomAt(x, z) != roomNo || rplus::biomeAt(x, z) == '.' || y < 200.0f || rplus::steep(x, z) ||
            !rplus::openGround(x, z, 1000.0f) || !gentle(x, z, 450.0f, 120.0f)) {
            continue;
        }
        if ((x - sx) * (x - sx) + (z - sz) * (z - sz) < 10000.0f * 10000.0f) {
            continue;
        }
        bool nearTown = false;
        for (const rplus::TownInfo& t : towns) {
            nearTown = nearTown || (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) < (t.r + 5000.0f) * (t.r + 5000.0f);
        }
        if (nearTown) {
            continue;
        }
        makeTower(key, roomNo, x, y, z, h);
        return;
    }
}

// A tower (with its ladder) standing at (x, y, z); its deck is filled once its collision is in (towersFrame).
void makeTower(int key, int roomNo, float x, float y, float z, u32 h) {
    const float lift = sTowerLift > -1e8f ? sTowerLift : 0.0f;
    cXyz pos(x, y + lift, z);
    csXyz angle(0, (s16)(h >> 3), 0);
    // Params bit 0: with its ladder.
    const fpc_ProcID id = fopAcM_create((char*)"Aygr", 0x00000001u, &pos, roomNo, &angle, nullptr);
    if (id == fpcM_ERROR_PROCESS_ID_e) {
        return;
    }
    TowerLive* slot = nullptr;
    for (TowerLive& t : sTowers) {
        if (t.used && t.key == key) {
            slot = &t;
            break;
        }
        if (!t.used && slot == nullptr) {
            slot = &t;
        }
    }
    if (slot != nullptr) {
        *slot = TowerLive{true, key, roomNo, x, z, id, 0, h, lift};
    }
    fprintf(stderr, "[rplus] room %d: lookout tower %d at %.0f %.0f %.0f\n", roomNo, key, x, pos.y, z);
}

// Away from the start and the towns by at least `away` past each town's ring.
bool wild(float x, float z, float sx, float sz, float fromStart, float away,
          const std::vector<rplus::TownInfo>& towns) {
    if ((x - sx) * (x - sx) + (z - sz) * (z - sz) < fromStart * fromStart) {
        return false;
    }
    for (const rplus::TownInfo& t : towns) {
        if ((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) < (t.r + away) * (t.r + away)) {
            return false;
        }
    }
    return true;
}

// Wandering Darknuts: a knight or two on open high ground (grass, rock, snow), about one room in two. Rare enough to
// be a surprise; tough enough (helmets, shields, capes) to be worth going round.
void placeDarknuts(int roomNo, float rx0, float rz0, float sx, float sz, const std::vector<rplus::TownInfo>& towns) {
    const u32 h0 = mix((u32)roomNo * 6007u, pc_rplus_seed() ^ 0xDA4Bu);
    if (h0 % 2 != 0) {
        return;
    }
    for (int tries = 0; tries < 200; tries++) {
        const u32 h = mix(h0, (u32)tries);
        float x = rx0 + (0.06f + (h & 0xFFFF) / 65536.0f * 0.88f) * 100000.0f;
        float z = rz0 + (0.06f + ((h >> 16) & 0xFFFF) / 65536.0f * 0.88f) * 100000.0f;
        if (strchr("grs", rplus::biomeAt(x, z)) == nullptr) {
            continue;
        }
        highSpot(&x, &z, 1500.0f);
        if (rplus::roomAt(x, z) != roomNo || strchr("grs", rplus::biomeAt(x, z)) == nullptr ||
            rplus::groundY(x, z) < 250.0f || rplus::steep(x, z) || !rplus::openGround(x, z, 1200.0f) ||
            !gentle(x, z, 600.0f, 150.0f) || !wild(x, z, sx, sz, 15000.0f, 6000.0f, towns)) {
            continue;
        }
        const int n = (h >> 3) % 3 == 0 ? 2 : 1;
        for (int k = 0; k < n; k++) {
            const u32 g = mix(h, (u32)k + 31u);
            const float ex = x + k * 450.0f, ez = z + k * 200.0f;
            const Entry dn = darknut(g, false);
            spawn(dn, ex, ez, (s16)(g >> 8), roomNo, dn.params);
        }
        fprintf(stderr, "[rplus] room %d: %d Darknut%s at %.0f %.0f\n", roomNo, n, n > 1 ? "s" : "", x, z);
        return;
    }
}

// A gang of Morths: 8 to 14 of the little spiky balls crawling over one patch of grass, forest floor or dunes, about
// one room in two. Each is its own (no spawner), spread over 700 units so they come at Link from all sides.
int makeMorths(int roomNo, float x, float z, u32 h) {
    const Entry m = morth();
    const int n = 8 + (int)((h >> 5) % 7);
    int made = 0;
    for (int k = 0; k < n; k++) {
        const u32 g = mix(h, (u32)k + 77u);
        const float a = (g & 0xFFFF) / 65536.0f * 6.2831853f;
        const float r = 80.0f + ((g >> 16) & 0xFF) / 256.0f * 350.0f;
        const float ex = x + sinf(a) * r, ez = z + cosf(a) * r;
        if (rplus::roomAt(ex, ez) != roomNo || rplus::steep(ex, ez) || rplus::groundY(ex, ez) < 100.0f) {
            continue;
        }
        spawn(m, ex, ez, (s16)(g >> 8), roomNo, m.params);
        made++;
    }
    fprintf(stderr, "[rplus] room %d: a gang of %d Morths at %.0f %.0f\n", roomNo, made, x, z);
    return made;
}

int placeMorths(int roomNo, float rx0, float rz0, float sx, float sz, const std::vector<rplus::TownInfo>& towns) {
    const u32 h0 = mix((u32)roomNo * 7333u, pc_rplus_seed() ^ 0x3047u);
    if (h0 % 2 != 0) {
        return 0;
    }
    for (int tries = 0; tries < 200; tries++) {
        const u32 h = mix(h0, (u32)tries);
        const float x = rx0 + (0.06f + (h & 0xFFFF) / 65536.0f * 0.88f) * 100000.0f;
        const float z = rz0 + (0.06f + ((h >> 16) & 0xFFFF) / 65536.0f * 0.88f) * 100000.0f;
        if (strchr("gfd", rplus::biomeAt(x, z)) == nullptr || rplus::groundY(x, z) < 150.0f || rplus::steep(x, z) ||
            !rplus::openGround(x, z, 900.0f) || !gentle(x, z, 500.0f, 120.0f) ||
            !wild(x, z, sx, sz, 12000.0f, 4000.0f, towns)) {
            continue;
        }
        return makeMorths(roomNo, x, z, h);
    }
    return 0;
}

// Phantom Ganon's haunt: one spot in the whole world, the same for a seed. A clearing deep in a forest (misty, see
// placeWeather) far from the start and the towns; failing that, any quiet open land; with the quick-test start, past
// its tower so it can be tried at once. Four fires mark it. Beat him and a chest (a silver rupee, and the next decor
// or weapon) stands there, kept beaten in the camps' save; until then he waits each time his room loads.
struct PgHaunt {
    bool made, found;
    unsigned int seed;
    int test;
    float x, z;
};
PgHaunt sPg;

const PgHaunt& pgHaunt() {
    if (sPg.made && sPg.seed == pc_rplus_seed() && sPg.test == pc_rplus_test_start()) {
        return sPg;
    }
    sPg = PgHaunt{};
    sPg.made = true;
    sPg.seed = pc_rplus_seed();
    sPg.test = pc_rplus_test_start();
    float sx, sz;
    rplus::startXZ(&sx, &sz);
    const std::vector<rplus::TownInfo> towns = rplus::towns();
    auto good = [&](float x, float z, const char* biomes, float fromStart, float fromTowns) {
        return rplus::roomAt(x, z) != 0 && strchr(biomes, rplus::biomeAt(x, z)) != nullptr &&
               rplus::groundY(x, z) >= 200.0f && !rplus::steep(x, z) && rplus::openGround(x, z, 1500.0f) &&
               gentle(x, z, 1000.0f, 150.0f) && wild(x, z, sx, sz, fromStart, fromTowns, towns);
    };
    if (sPg.test) {
        // A ring 7000-14000 out from the start, from a bearing the seed turns.
        const float a0 = (mix(0x96A0u, pc_rplus_seed()) & 0xFFFF) / 65536.0f * 6.2831853f;
        for (float r = 7000.0f; r <= 14000.0f && !sPg.found; r += 500.0f) {
            for (int k = 0; k < 24 && !sPg.found; k++) {
                const float x = sx + sinf(a0 + k * 0.2618f) * r, z = sz + cosf(a0 + k * 0.2618f) * r;
                if (good(x, z, "grfdsv", 0.0f, 1500.0f)) {
                    sPg.found = true, sPg.x = x, sPg.z = z;
                }
            }
        }
    }
    static const char* const kPasses[] = {"f", "grfdsv"};
    for (const char* biomes : kPasses) {
        for (int tries = 0; tries < 6000 && !sPg.found; tries++) {
            const u32 h = mix((u32)tries, pc_rplus_seed() ^ 0x96A1u);
            const int room = 1 + (int)(h % 49);
            if (!pc_rplus_sea_room_generated(room)) {
                continue;
            }
            float rx0, rz0;
            rplus::roomCorner(room, &rx0, &rz0);
            const u32 g = mix(h, 0x51u);
            const float x = rx0 + (0.08f + (g & 0xFFFF) / 65536.0f * 0.84f) * 100000.0f;
            const float z = rz0 + (0.08f + ((g >> 16) & 0xFFFF) / 65536.0f * 0.84f) * 100000.0f;
            if (good(x, z, biomes, 25000.0f, 8000.0f)) {
                sPg.found = true, sPg.x = x, sPg.z = z;
            }
        }
    }
    if (sPg.found) {
        fprintf(stderr, "[rplus] Phantom Ganon haunts room %d at %.0f %.0f\n", rplus::roomAt(sPg.x, sPg.z), sPg.x,
                sPg.z);
    } else {
        fprintf(stderr, "[rplus] Phantom Ganon: no spot for him in this world\n");
    }
    return sPg;
}

void placePhantomGanon(int roomNo) {
    const PgHaunt& pg = pgHaunt();
    if (!pg.found || rplus::roomAt(pg.x, pg.z) != roomNo) {
        return;
    }
    // The fires round his spot.
    const Entry* fire = findEntry(pools().campDecor, "Fire");
    if (fire != nullptr) {
        for (int k = 0; k < 4; k++) {
            const float a = k * 1.5707963f + 0.785398f;
            spawn(*fire, pg.x + sinf(a) * 1100.0f, pg.z + cosf(a) * 1100.0f, 0, roomNo,
                  (fire->params & ~0x1F0FFu) | 0x1F0FFu);
        }
    }
    if (sCampState[PG_KEY] != CAMP_NEW) {
        spawnCampChest(PG_KEY, roomNo, pg.x, pg.z, -1e9f);
        return;
    }
    Entry e{};
    strcpy(e.name, "Fganon");
    const fpc_ProcID id = spawnAt(e, pg.x, rplus::groundY(pg.x, pg.z) + 150.0f, pg.z,
                                  (s16)(mix(0x96A2u, pc_rplus_seed()) >> 8), roomNo, PG_PARAMS);
    if (id != fpcM_ERROR_PROCESS_ID_e) {
        watchCamp(PG_KEY, roomNo, pg.x, pg.z, -1e9f, &id, 1);
    }
    fprintf(stderr, "[rplus] room %d: Phantom Ganon waits at %.0f %.0f\n", roomNo, pg.x, pg.z);
}

void placeEnemies(int roomNo) {
    Pools& P = pools();
    if (P.enemies.empty()) {
        return;
    }
    const std::vector<rplus::TownInfo> towns = rplus::towns();
    float sx, sz;
    rplus::startXZ(&sx, &sz);
    float rx0, rz0;
    rplus::roomCorner(roomNo, &rx0, &rz0);
    int made = placeCamps(roomNo, rx0, rz0, sx, sz, towns), groups = 0;
    if (getenv("COS_RPLUS_TOWERS") == nullptr || strcmp(getenv("COS_RPLUS_TOWERS"), "0") != 0) {
        placeTower(roomNo, rx0, rz0, sx, sz, towns);
    }
    if (enabled("COS_RPLUS_DARKNUTS")) {
        placeDarknuts(roomNo, rx0, rz0, sx, sz, towns);
    }
    if (enabled("COS_RPLUS_MORTHS")) {
        made += placeMorths(roomNo, rx0, rz0, sx, sz, towns);
    }
    if (enabled("COS_RPLUS_PGANON")) {
        placePhantomGanon(roomNo);
    }
    for (int tries = 0; tries < 400 && groups < ENEMY_GROUPS_PER_ROOM; tries++) {
        const u32 h = mix((u32)roomNo * 7919u + (u32)tries, pc_rplus_seed() ^ 0xE7E7u);
        const float x = rx0 + (h & 0xFFFF) / 65536.0f * 100000.0f;
        const float z = rz0 + ((h >> 16) & 0xFFFF) / 65536.0f * 100000.0f;
        const char b = rplus::biomeAt(x, z);
        const float y = rplus::groundY(x, z);
        if (b == '.' || y < 120.0f || rplus::steep(x, z) || !rplus::openGround(x, z, 1500.0f) ||
            !gentle(x, z, 700.0f, 220.0f)) {
            continue;
        }
        if ((x - sx) * (x - sx) + (z - sz) * (z - sz) < 9000.0f * 9000.0f) {
            continue;  // the start is safe
        }
        bool nearTown = false;
        for (const rplus::TownInfo& t : towns) {
            nearTown = nearTown || (x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) < (t.r + 3500.0f) * (t.r + 3500.0f);
        }
        if (nearTown) {
            continue;
        }
        const bool beach = y < 150.0f;
        // A kind that lives here and that the disc has, by weight.
        int total = 0;
        for (const EnemyKind& k : kEnemies) {
            if (strchr(k.biomes, beach ? 'b' : b) == nullptr && !(beach && strchr(k.biomes, b))) {
                continue;
            }
            for (const Entry& e : P.enemies) {
                if (strcmp(e.name, k.name) == 0) {
                    total += k.weight;
                    break;
                }
            }
        }
        if (total == 0) {
            continue;
        }
        int pick = (int)((h >> 7) % (u32)total);
        const EnemyKind* kind = nullptr;
        for (const EnemyKind& k : kEnemies) {
            if (strchr(k.biomes, beach ? 'b' : b) == nullptr && !(beach && strchr(k.biomes, b))) {
                continue;
            }
            bool has = false;
            for (const Entry& e : P.enemies) {
                has = has || strcmp(e.name, k.name) == 0;
            }
            if (!has) {
                continue;
            }
            if (pick < k.weight) {
                kind = &k;
                break;
            }
            pick -= k.weight;
        }
        if (kind == nullptr) {
            continue;
        }
        // One of the params the disc gives this kind.
        std::vector<const Entry*> forms;
        for (const Entry& e : P.enemies) {
            if (strcmp(e.name, kind->name) == 0) {
                forms.push_back(&e);
            }
        }
        const Entry& e = *forms[(h >> 3) % forms.size()];
        const int count = kind->groupMin + (int)((h >> 11) % (u32)(kind->groupMax - kind->groupMin + 1));
        for (int k = 0; k < count; k++) {
            const u32 g = mix(h, (u32)k + 99u);
            const float ex = x + ((g & 0xFF) / 256.0f - 0.5f) * 900.0f, ez = z + (((g >> 8) & 0xFF) / 256.0f - 0.5f) * 900.0f;
            if (rplus::roomAt(ex, ez) != roomNo || rplus::groundY(ex, ez) < 100.0f) {
                continue;
            }
            spawn(e, ex, ez, (s16)(g >> 16), roomNo, e.params | kind->clearPath);
            made++;
        }
        groups++;
    }
    if (made > 0) {
        fprintf(stderr, "[rplus] room %d: %d enemies in %d groups\n", roomNo, made, groups);
    }
}

// The island a puzzle anchor's chest came from (scripts/rplus/anchors.py, the anchor's home): its sea room.
struct PuzzleHome {
    const char* id;
    int room;
};
const PuzzleHome kPuzzleHomes[] = {
    {"transparent_chest", 11}, {"boulder_chest", 13}, {"sky_platforms", 13}, {"fh_small_island", 41},
    {"greatfish_chest", 23},   {"frozen_chest", 40},  {"headstone_top", 45}, {"waterfall_chest", 33},
    {"needle_chest", 29},      {"angular_peak", 47},  {"crescent_chest", 5},
};

// The highest standable open ground within r of (x, z): a chest on a hilltop or ledge, seen from below.
void highSpot(float* x, float* z, float r) {
    float bx = *x, bz = *z, by = rplus::groundY(bx, bz);
    for (int i = -3; i <= 3; i++) {
        for (int j = -3; j <= 3; j++) {
            const float px = *x + i * r / 3.0f, pz = *z + j * r / 3.0f;
            if (rplus::biomeAt(px, pz) == '.' || rplus::steep(px, pz) || !rplus::openGround(px, pz, 300.0f)) {
                continue;
            }
            const float py = rplus::groundY(px, pz);
            if (py > by) {
                bx = px, bz = pz, by = py;
            }
        }
    }
    *x = bx, *z = bz;
}

// The open-air chests: each puzzle anchor gets its home island's chests (the k-th anchor of an island its k-th
// chest), standing on the anchor's high ground. The island's own puzzle (a switch, the lighthouse, enemies) is not
// built, so the chest is a plain one (function type 0; the randomizer's trap bit 0x40 stays). Its item (angle z) and
// save flag (params) are the disc's; its room (angle x, bits 0-5) is the one it now stands in.
void placeChests(int roomNo) {
    Pools& P = pools();
    int used[50] = {};
    for (int i = 0; i < pc_rplus_anchor_count(); i++) {
        const char *id, *kind, *stage;
        int cx, cy;
        if (!pc_rplus_anchor(i, &id, &kind, &stage, &cx, &cy) || strcmp(kind, "puzzle") != 0) {
            continue;
        }
        int home = 0;
        for (const PuzzleHome& ph : kPuzzleHomes) {
            home = strcmp(ph.id, id) == 0 ? ph.room : home;
        }
        if (home == 0) {
            continue;
        }
        const Entry* chest = nullptr;
        for (int k = 0, n = 0; k < (int)P.chests.size(); k++) {
            const Entry& e = P.chests[k];
            if (e.room != home || (e.layer >= 0 && e.layer != dComIfG_play_c::getLayerNo(e.room))) {
                continue;
            }
            if (n++ == used[home]) {
                chest = &e;
                break;
            }
        }
        used[home]++;
        if (chest == nullptr) {
            continue;
        }
        float x, z;
        rplus::cellSpot(cx, cy, &x, &z);
        highSpot(&x, &z, 1500.0f);
        if (rplus::roomAt(x, z) != roomNo) {
            continue;
        }
        Entry e = *chest;
        e.ax = (s16)((e.ax & ~0x3F) | roomNo);
        spawn(e, x, z, (s16)(mix((u32)i, pc_rplus_seed()) & 0xFFFF), roomNo, e.params & ~0x3Fu);
        fprintf(stderr, "[rplus] room %d: chest %s (%s) from sea room %d\n", roomNo, id, e.name, home);
    }
}

// Weather by biome: a weather tag (ky_tag0, d_a_kytag00) over each part of the room where one biome holds most of
// the land: snow and a grey sky on snowfields, haze and a dark sky over volcanoes, a light mist in forests. Each tag
// fades in over 3000 units at its edge. The sea's own weather (rain and storms rolled each day) runs as before.
void placeWeather(int roomNo) {
    struct Look {
        char biome;
        u8 psel, mode;  // the sky's palette (0xFF: as it is), the effect (2 snow, 3 haze, 4 mist)
    };
    static const Look kLooks[] = {{'s', 1, 2}, {'v', 2, 3}, {'f', 0xFF, 4}};
    constexpr float BLOCK = 12500.0f;
    float rx0, rz0;
    rplus::roomCorner(roomNo, &rx0, &rz0);
    int made = 0;
    for (int bj = 0; bj < 8; bj++) {
        for (int bi = 0; bi < 8; bi++) {
            int count[3] = {}, land = 0;
            for (int j = 0; j < 5; j++) {
                for (int i = 0; i < 5; i++) {
                    const char b = rplus::biomeAt(rx0 + (bi + (i + 0.5f) / 5.0f) * BLOCK,
                                                  rz0 + (bj + (j + 0.5f) / 5.0f) * BLOCK);
                    land += b != '.';
                    for (int k = 0; k < 3; k++) {
                        count[k] += b == kLooks[k].biome;
                    }
                }
            }
            int best = 0;
            for (int k = 1; k < 3; k++) {
                best = count[k] > count[best] ? k : best;
            }
            if (land < 6 || count[best] * 2 < land) {
                continue;
            }
            const Look& lk = kLooks[best];
            cXyz pos(rx0 + (bi + 0.5f) * BLOCK, -1000.0f, rz0 + (bj + 0.5f) * BLOCK);
            cXyz scale(0.8f, 3.0f, 0.8f);  // 4000 units round, then 3000 fading; 15000 high
            csXyz angle(0x00FF, 0, 0);       // no switch; mode 0 (the large radii)
            // psel bits 0-7, effect 8-15, fade width 16-23 (x100 units), height fade 24-31 (x100 units).
            const u32 params = lk.psel | (u32)lk.mode << 8 | 30u << 16 | 10u << 24;
            fopAcM_create((char*)"kytag00", params, &pos, roomNo, &angle, &scale);
            made++;
        }
    }
    if (made > 0) {
        fprintf(stderr, "[rplus] room %d: %d weather tags\n", roomNo, made);
    }
}

// ---- The quick-test start (pc_rplus_test_start; Dillon, 2026-11-02: "a test seed that includes chests and locations
// in the start, for faster testing ... new weapons and some decor items")
//
// TEST_CHESTS plain chests in a ring round Link's start (700-2600 away, facing it), each holding a camp's filler, so
// each brings a weapon or a house decor piece (pc_rplus_weapon_check: the 1st, 3rd ... 11th filler checks give the
// six weapons, the others decor); an enemy camp 3000-6500 away, its chest when beaten; a lookout tower 4500-9000
// away, its chest when its deck is beaten. Their opened flags are camp keys (TEST_KEY0 on), saved as a camp's are.

struct TestSpot {
    float x, z;
};

struct TestLayout {
    bool made;
    unsigned int seed;
    float sx, sz;
    int nChests;
    TestSpot chests[TEST_CHESTS];
    bool hasCamp, hasTower, hasFoes;
    TestSpot camp, tower, foes;
};
TestLayout sTest;

bool testSpotOk(float x, float z, float clear, float gentleR, float rise, const TestSpot* used, int n, float sep) {
    if (rplus::roomAt(x, z) == 0 || rplus::biomeAt(x, z) == '.' || rplus::groundY(x, z) < 120.0f ||
        rplus::steep(x, z) || !rplus::openGround(x, z, clear) || !gentle(x, z, gentleR, rise)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if ((x - used[i].x) * (x - used[i].x) + (z - used[i].z) * (z - used[i].z) < sep * sep) {
            return false;
        }
    }
    return true;
}

// The nearest good spot from rMin out to rMax round the start (rings 250 apart, 24 bearings from one the seed turns).
bool testSpot(float rMin, float rMax, float clear, float gentleR, float rise, u32 salt, const TestSpot* used, int n,
              float sep, TestSpot* out) {
    const float a0 = (mix(salt, pc_rplus_seed() ^ 0x7E57u) & 0xFFFF) / 65536.0f * 6.2831853f;
    for (float r = rMin; r <= rMax; r += 250.0f) {
        for (int k = 0; k < 24; k++) {
            const float a = a0 + k * 0.2617994f;
            const float x = sTest.sx + sinf(a) * r, z = sTest.sz + cosf(a) * r;
            if (testSpotOk(x, z, clear, gentleR, rise, used, n, sep)) {
                *out = TestSpot{x, z};
                return true;
            }
        }
    }
    return false;
}

const TestLayout& testLayout() {
    if (sTest.made && sTest.seed == pc_rplus_seed()) {
        return sTest;
    }
    sTest = TestLayout{};
    sTest.made = true;
    sTest.seed = pc_rplus_seed();
    rplus::startXZ(&sTest.sx, &sTest.sz);
    TestSpot used[TEST_CHESTS + 3];
    int n = 0;
    for (int i = 0; i < TEST_CHESTS; i++) {
        TestSpot sp;
        // Flat open ground first; then nearly anywhere standable.
        if (testSpot(700.0f, 2600.0f, 150.0f, 200.0f, 80.0f, (u32)i, used, n, 350.0f, &sp) ||
            testSpot(600.0f, 4500.0f, 0.0f, 120.0f, 120.0f, (u32)i, used, n, 250.0f, &sp)) {
            used[n++] = sp;
            sTest.chests[sTest.nChests++] = sp;
        }
    }
    sTest.hasCamp = testSpot(3000.0f, 6500.0f, 700.0f, 700.0f, 200.0f, 0xCA3Bu, used, n, 1600.0f, &sTest.camp) ||
                    testSpot(2500.0f, 9000.0f, 300.0f, 500.0f, 300.0f, 0xCA3Bu, used, n, 1400.0f, &sTest.camp);
    if (sTest.hasCamp) {
        used[n++] = sTest.camp;
    }
    sTest.hasTower = testSpot(4500.0f, 9000.0f, 600.0f, 450.0f, 150.0f, 0x70E9u, used, n, 2000.0f, &sTest.tower) ||
                     testSpot(3000.0f, 12000.0f, 300.0f, 400.0f, 250.0f, 0x70E9u, used, n, 1500.0f, &sTest.tower);
    if (sTest.hasTower) {
        used[n++] = sTest.tower;
    }
    // The new kinds (a Darknut, a Morth gang; Phantom Ganon's haunt is further out, pgHaunt) to try at once.
    sTest.hasFoes = testSpot(3500.0f, 7000.0f, 600.0f, 500.0f, 150.0f, 0xF0E5u, used, n, 1800.0f, &sTest.foes) ||
                    testSpot(2500.0f, 10000.0f, 300.0f, 400.0f, 250.0f, 0xF0E5u, used, n, 1500.0f, &sTest.foes);
    fprintf(stderr, "[rplus] test start at %.0f %.0f: %d chests, camp %s, tower %s, Darknut and Morths %s\n",
            sTest.sx, sTest.sz, sTest.nChests, sTest.hasCamp ? "yes" : "no room", sTest.hasTower ? "yes" : "no room",
            sTest.hasFoes ? "yes" : "no room");
    return sTest;
}

void placeTestStart(int roomNo) {
    const TestLayout& T = testLayout();
    for (int i = 0; i < T.nChests; i++) {
        const TestSpot& c = T.chests[i];
        if (rplus::roomAt(c.x, c.z) != roomNo) {
            continue;
        }
        const int key = TEST_KEY0 + i;
        cXyz pos(c.x, rplus::groundY(c.x, c.z), c.z);
        // Facing the start: Link sees each one's front from where he stands.
        csXyz angle((s16)(0x4000 | key << 6 | roomNo), cM_atan2s(T.sx - c.x, T.sz - c.z),
                    (s16)(campItem(key) << 8 | 0xFF));
        fopAcM_create((char*)"takara", CAMP_CHEST_PARAMS, &pos, roomNo, &angle, nullptr);
        fprintf(stderr, "[rplus] room %d: test chest %d at %.0f %.0f (item 0x%02X%s)\n", roomNo, i + 1, c.x, c.z,
                campItem(key), sCampState[key] == CAMP_OPENED ? ", opened" : "");
    }
    if (T.hasCamp && rplus::roomAt(T.camp.x, T.camp.z) == roomNo) {
        makeCamp(TEST_CAMP_KEY, roomNo, T.camp.x, T.camp.z, mix(TEST_CAMP_KEY, pc_rplus_seed()) | 1u << 13);
    }
    if (T.hasTower && rplus::roomAt(T.tower.x, T.tower.z) == roomNo) {
        makeTower(TEST_TOWER_KEY, roomNo, T.tower.x, rplus::groundY(T.tower.x, T.tower.z), T.tower.z,
                  mix(TEST_TOWER_KEY, pc_rplus_seed()));
    }
    if (T.hasFoes && rplus::roomAt(T.foes.x, T.foes.z) == roomNo) {
        const u32 h = mix(0xF0E5u, pc_rplus_seed());
        const Entry dn = darknut(h, false);
        spawn(dn, T.foes.x, T.foes.z, cM_atan2s(T.sx - T.foes.x, T.sz - T.foes.z), roomNo, dn.params);
        fprintf(stderr, "[rplus] room %d: test Darknut at %.0f %.0f\n", roomNo, T.foes.x, T.foes.z);
        // The gang off to one side, between the Darknut and the start.
        const float a = cM_atan2s(T.sx - T.foes.x, T.sz - T.foes.z) * (3.14159265f / 32768.0f) + 1.2f;
        makeMorths(roomNo, T.foes.x + sinf(a) * 1300.0f, T.foes.z + cosf(a) * 1300.0f, h);
    }
}

} // namespace

void pc_rplus_life_room(int roomNo) {
    if (!pc_rplus_active()) {
        return;
    }
    if (pc_rplus_test_start()) {
        placeTestStart(roomNo);
    }
    if (enabled("COS_RPLUS_NPCS")) {
        placeNpcs(roomNo);
    }
    if (enabled("COS_RPLUS_ENEMIES")) {
        placeEnemies(roomNo);
    }
    if (enabled("COS_RPLUS_CHESTS")) {
        placeChests(roomNo);
    }
    if (enabled("COS_RPLUS_WEATHER")) {
        placeWeather(roomNo);
    }
}

// ---- Camp chests (see "Camp chests" above)

namespace {

std::string campSavePath(int slot) {
    const char* saves = getenv("COS_SAVE_DIR");
    std::string dir = saves != nullptr && saves[0] != '\0' ? saves : pc::seedgen::userDir();
    return dir + "/rplus-camps-" + std::to_string(pc_rplus_seed()) + "-" + std::to_string(slot) + ".txt";
}

} // namespace

void pc_rplus_camps_reset(void) {
    memset(sCampState, 0, sizeof(sCampState));
    memset(sCamps, 0, sizeof(sCamps));
    memset(sTowers, 0, sizeof(sTowers));
}

void pc_rplus_camps_load(int slot) {
    pc_rplus_camps_reset();
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(campSavePath(slot).c_str(), "r");
    if (f == nullptr) {
        return;
    }
    int key, state, n = 0;
    while (fscanf(f, "%d %d", &key, &state) == 2) {
        if (key >= 0 && key < CAMP_KEYS && state >= CAMP_NEW && state <= CAMP_OPENED) {
            sCampState[key] = (u8)state;
            n++;
        }
    }
    fclose(f);
    fprintf(stderr, "[rplus] camps: %d beaten (file slot %d)\n", n, slot);
}

void pc_rplus_camps_save(int slot) {
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(campSavePath(slot).c_str(), "w");
    if (f == nullptr) {
        fprintf(stderr, "[rplus] camps: can't write %s\n", campSavePath(slot).c_str());
        return;
    }
    for (int k = 0; k < CAMP_KEYS; k++) {
        if (sCampState[k] != CAMP_NEW) {
            fprintf(f, "%d %d\n", k, sCampState[k]);
        }
    }
    fclose(f);
}

void pc_rplus_camps_room_gone(int roomNo) {
    // Its enemies go with it: not beaten.
    for (CampLive& c : sCamps) {
        if (c.used && c.room == roomNo) {
            c.used = false;
        }
    }
    for (TowerLive& t : sTowers) {
        if (t.used && t.room == roomNo) {
            t.used = false;
        }
    }
}

void pc_rplus_camps_frame(void) {
    if (!pc_rplus_active()) {
        return;
    }
    if (strcmp(dComIfGp_getStartStageName(), "sea") != 0) {
        memset(sCamps, 0, sizeof(sCamps));  // left the sea: its rooms' enemies are gone
        memset(sTowers, 0, sizeof(sTowers));
        return;
    }
    for (CampLive& c : sCamps) {
        if (!c.used) {
            continue;
        }
        bool left = false, any = false;
        for (int i = 0; i < c.n; i++) {
            fopAc_ac_c* a = nullptr;
            const BOOL alive = fopAcM_SearchByID(c.ids[i], &a);  // TRUE while being made too
            if (alive && a != nullptr) {
                c.seen[i] = true;
            }
            left = left || alive;
            any = any || c.seen[i];
        }
        if (left || !any) {
            continue;
        }
        // Only with Link near: a room going away (far from him) takes its enemies before it is gone itself.
        fopAc_ac_c* link = dComIfGp_getPlayer(0);
        if (link == nullptr || (link->current.pos.x - c.x) * (link->current.pos.x - c.x) +
                                       (link->current.pos.z - c.z) * (link->current.pos.z - c.z) >
                                   12000.0f * 12000.0f) {
            if (link != nullptr) {
                c.used = false;
            }
            continue;
        }
        c.used = false;
        if (c.key < 0 || c.key >= CAMP_KEYS || sCampState[c.key] != CAMP_NEW) {
            continue;
        }
        sCampState[c.key] = CAMP_BEATEN;
        fprintf(stderr, "[rplus] room %d: camp %d beaten\n", c.room, c.key);
        mDoAud_seStart(JA_SE_READ_RIDDLE_1, NULL, 0, 0);  // the puzzle-solved jingle
        spawnCampChest(c.key, c.room, c.x, c.z, c.y);
    }
    towersFrame();
}

int pc_rplus_camp_chest_key(unsigned int params, int angleX) {
    if (!pc_rplus_active() || (params & 0xFF000000u) != (CAMP_CHEST_PARAMS & 0xFF000000u) ||
        (angleX & 0xC000) != 0x4000) {
        return -1;
    }
    const int key = (angleX >> 6) & 0xFF;
    return key < CAMP_KEYS ? key : -1;
}

int pc_rplus_camp_chest_opened(int key) {
    return key >= 0 && key < CAMP_KEYS && sCampState[key] == CAMP_OPENED;
}

void pc_rplus_camp_chest_open(int key) {
    if (key >= 0 && key < CAMP_KEYS) {
        sCampState[key] = CAMP_OPENED;
        fprintf(stderr, "[rplus] camp %d's chest opened\n", key);
    }
}

void pc_rplus_init_story_events() {
    if (!pc_rplus_active()) {
        return;
    }
    // Mark the Forest of Fairies opening event (Tetra capture) and related events as complete so
    // skipped story sequences don't crash when they try to play their demo files. The Forest of
    // Fairies room (A_mori) would try to play a cutscene about 4 seconds after entering, but
    // since the randomizer skips that story arc, the demo file isn't loaded.
    //
    // Event bits:
    // 0x2A80 (bit 10 of flag 0x540): Tetra found / captured (triggers Forest of Fairies sequence)
    // 0x2A81 (bit 11 of flag 0x540): Forest of Fairies cleared
    dComIfGs_setEventReg(0x2A80, 1);
    dComIfGs_setEventReg(0x2A81, 1);
}

void pc_rplus_clear_camera_lock_on_room(int roomNo) {
    // Get the camera and check if its lock-on target is from the room being deleted
    // This prevents dangling pointers when streamed rooms unload while the camera
    // is still locked onto an actor in that room.
    dCamera_c* camera = dCam_getBody();
    if (!camera) {
        return;
    }

    // Check force lock-on (mLockOnActorId / mpLockonActor)
    fopAc_ac_c* target = camera->GetForceLockOnActor();
    if (target && target->current.roomNo == roomNo) {
        // ForceLockOff with fpcM_ERROR_PROCESS_ID_e will clear any lock-on
        camera->ForceLockOff(fpcM_ERROR_PROCESS_ID_e);
    }

    // Note: SubjectLockOff() clears mpLockonTarget but we don't have direct access to it
    // from here. The force lock system handles the primary reference we care about.
}
