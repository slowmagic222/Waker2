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
#include "pc_rplus_world.h"
#include "pc_internal.h"
#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "d/d_camera.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
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

void spawn(const Entry& e, float x, float z, s16 ay, int roomNo, u32 params) {
    if (strcmp(e.name, "Bk") == 0) {
        // A Bokoblin's type (bits 0-3, d_a_bk jyunkai): sitting by a fire (4, 10), asleep until a switch (6), on a
        // path (9) or in an event (8) only wake in their own scenes; here they all stand and look out (0).
        const u32 type = params & 0xF;
        if (type == 4 || type == 6 || type == 8 || type == 9 || type == 10) {
            params &= ~0xFu;
        }
    }
    cXyz pos(x, rplus::groundY(x, z) + 5.0f, z);
    csXyz angle(e.ax, ay, e.az);
    // By name, so the profile's argument (a ChuChu's colour, a person's look) comes with it.
    char name[9];
    memcpy(name, e.name, sizeof(name));
    fopAcM_create(name, params, &pos, roomNo, &angle, nullptr);
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

// Enemy camps: on open flat land away from the start and the towns, a campfire with Bokoblins round it (and
// sometimes a Moblin), a few pots by it. At most CAMPS_PER_ROOM a room.
constexpr int CAMPS_PER_ROOM = 2;
int placeCamps(int roomNo, float rx0, float rz0, float sx, float sz, const std::vector<rplus::TownInfo>& towns) {
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
            spawn(*bk, ex, ez, cM_atan2s(x - ex, z - ez), roomNo, bk->params | 0xFFFF0000u);
            made++;
        }
        if (mo != nullptr && ((h >> 13) & 1)) {
            spawn(*mo, x + 900.0f, z, cM_atan2s(-1.0f, 0.0f), roomNo, mo->params | 0xFFFF0000u);
            made++;
        }
        if (pot != nullptr) {
            for (int k = 0; k < 2; k++) {
                const float a = ((h >> (17 + k * 3)) & 7) * 0.785f;
                spawn(*pot, x + sinf(a) * 1000.0f, z + cosf(a) * 1000.0f, 0, roomNo, pot->params);
                made++;
            }
        }
        camps++;
        fprintf(stderr, "[rplus] room %d: an enemy camp at %.0f %.0f (%d Bokoblins%s)\n", roomNo, x, z, n,
                mo != nullptr && ((h >> 13) & 1) ? " and a Moblin" : "");
    }
    return made;
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

} // namespace

void pc_rplus_life_room(int roomNo) {
    if (!pc_rplus_active()) {
        return;
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
