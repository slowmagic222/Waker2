// Randomizer+ discovery tracker (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md "Discovery tracker"; Dillon, 2026-11-02): the
// places of the generated world, each found when Link first comes near it, and only found ones shown on the tracker
// (pc_tracker_overlay.cpp).
//
// The places: every generated door (houses, caves, dungeon entrances, fairy fountains, Link's home), every NPC the
// world stands at an anchor of its own (Maggie, Zunari, Tott...), every town (named after the world file's village
// most of its doors belong to), and the enemy camps and lookout towers as their rooms make them (pc_rplus_life.cpp).
// Found: Link on the sea within a place's reach of it (a town: its ring and some more), or in a door's interior.
// Kept per seed and file in rplus-places-<seed>-<slot>.txt next to the saves (one found place a line: its key, where
// it is and its kind, so a camp found once shows even while its room is not loaded), read and written with the save
// as the camps' file is.
// Each world also keeps its own tracker progress (pc_tracker.cpp progressKey).
// Log lines: "[rplus] places: ..." once the places are made, "[rplus] found <name> (<key>) at x z" per place found.
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "pc/pc_qol.h"
#include "pc/pc_seedgen.h"
#include "pc_rplus_world.h"
#include "pc_internal.h"
#include "d/d_com_inf_game.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Place {
    std::string key, name, anchor, stage;
    char kind;
    float x, z;
    float reach;  // found within this of Link (0: only by going in its door)
    int camp = -1;
    bool world;   // one of the world's own (counted in the total), not a camp or tower
};

struct Found {
    float x, z;
    char kind;
};

std::vector<Place> sPlaces;
bool sBuilt = false;
std::map<std::string, Found> sFound;  // the file's found places, by key
std::vector<std::string> sNews;       // found just now, for the tracker's pop-up
std::string sNewsOut;
int sSlot = -1;

const char* campName(char kind) {
    return kind == 'k' ? "Enemy camp" : "Lookout tower";
}

int indexOf(const std::string& key) {
    for (size_t i = 0; i < sPlaces.size(); i++) {
        if (sPlaces[i].key == key) {
            return (int)i;
        }
    }
    return -1;
}

// A camp or tower (from the room that made it, or a found one from the file); its chest key from its key.
void addCamp(char kind, const std::string& key, float x, float z) {
    if (indexOf(key) >= 0) {
        return;
    }
    Place p;
    p.key = key;
    p.name = campName(kind);
    p.kind = kind;
    p.x = x;
    p.z = z;
    p.reach = kind == 'k' ? 3500.0f : 5000.0f;  // a tower stands high: seen from further
    p.camp = atoi(key.c_str() + key.find(':') + 1);
    p.world = false;
    sPlaces.push_back(p);
}

char kindOfAnchor(const char* id) {
    for (int i = 0; i < pc_rplus_anchor_count(); i++) {
        const char *aid, *kind, *stage;
        int cx, cy;
        if (pc_rplus_anchor(i, &aid, &kind, &stage, &cx, &cy) && strcmp(aid, id) == 0) {
            return strcmp(kind, "cave") == 0       ? 'c'
                   : strcmp(kind, "dungeon") == 0  ? 'd'
                   : strcmp(kind, "fountain") == 0 ? 'f'
                                                   : 'h';
        }
    }
    return 'h';
}

// "mesa_house" -> "Mesa house": a name for a world file without the tracker's lines
std::string nameOf(const char* id) {
    const char* n = pc_rplus_anchor_name(id);
    if (n[0] != '\0') {
        return n;
    }
    std::string s = id;
    for (char& ch : s) {
        ch = ch == '_' ? ' ' : ch;
    }
    if (!s.empty()) {
        s[0] = (char)toupper((unsigned char)s[0]);
    }
    return s;
}

void build() {
    if (sBuilt || !pc_rplus_active() || dComIfGp_getPlayer(0) == nullptr) {
        return;  // (the layout is made in play only)
    }
    sBuilt = true;
    std::vector<Place> camps;
    for (const Place& p : sPlaces) {
        if (!p.world) {
            camps.push_back(p);  // noted before the world's were made
        }
    }
    sPlaces.clear();
    const std::vector<rplus::TownInfo> towns = rplus::towns();
    const std::vector<rplus::DoorInfo> doors = rplus::doors();
    // The towns, named by their doors' villages (most votes), else the nearest village's centre, else "Town".
    for (size_t t = 0; t < towns.size(); t++) {
        std::map<int, int> votes;
        bool home = false;
        for (const rplus::DoorInfo& d : doors) {
            if (d.town == (int)t) {
                home = home || strcmp(d.id, "home") == 0;
                const int v = pc_rplus_anchor_village(d.id);
                if (v >= 0) {
                    votes[v]++;
                }
            }
        }
        int best = -1, bestVotes = 0;
        for (const auto& kv : votes) {
            if (kv.second > bestVotes) {
                best = kv.first;
                bestVotes = kv.second;
            }
        }
        std::string name = home ? "Home" : "", guess;
        float bestD = 1e30f;
        for (int i = 0; i < pc_rplus_village_count(); i++) {
            int id, cx, cy;
            const char* vname;
            if (!pc_rplus_village(i, &id, &vname, &cx, &cy)) {
                continue;
            }
            if (id == best) {
                name = vname;
            }
            float x0, z0;
            rplus::roomCorner(1, &x0, &z0);
            const float cs = 100000.0f / pc_rplus_cells();
            const float vx = x0 + (cx + 0.5f) * cs, vz = z0 + (cy + 0.5f) * cs;
            const float dd = (vx - towns[t].x) * (vx - towns[t].x) + (vz - towns[t].z) * (vz - towns[t].z);
            if (dd < bestD && dd < (12 * cs) * (12 * cs)) {
                bestD = dd;
                guess = vname;
            }
        }
        if (name.empty()) {
            name = guess;
        }
        Place p;
        p.key = "town:" + std::to_string(t);
        p.name = name.empty() ? "Town" : name;
        p.kind = 'T';
        p.x = towns[t].x;
        p.z = towns[t].z;
        p.reach = towns[t].r + 2000.0f;
        p.world = true;
        sPlaces.push_back(p);
    }
    // The doors, each anchor once.
    for (const rplus::DoorInfo& d : doors) {
        const bool home = strcmp(d.id, "home") == 0;
        Place p;
        p.key = std::string("a:") + d.id;
        if (indexOf(p.key) >= 0) {
            continue;
        }
        p.name = home ? "Link's home" : nameOf(d.id);
        p.anchor = home ? "" : d.id;
        p.stage = d.stage != nullptr ? d.stage : "";
        p.kind = home ? 'H' : kindOfAnchor(d.id);
        p.x = d.x;
        p.z = d.z;
        p.reach = 0.0f;  // a door is found by going through it, not by walking past (Dillon, 2026-11-03)
        p.world = true;
        sPlaces.push_back(p);
        if (home) {
            sFound[p.key] = Found{p.x, p.z, p.kind};  // where he starts: known from the start
        }
    }
    // The NPCs standing at anchors of their own (pc_rplus_life.cpp placeNpcs: cellSpot, then 150 east and south).
    for (int i = 0; i < pc_rplus_anchor_count(); i++) {
        const char *id, *kind, *stage;
        int cx, cy;
        if (!pc_rplus_anchor(i, &id, &kind, &stage, &cx, &cy) || !rplus::anchoredNpc(id) ||
            indexOf(std::string("a:") + id) >= 0) {
            continue;
        }
        Place p;
        p.key = std::string("a:") + id;
        p.name = nameOf(id);
        p.anchor = id;
        p.kind = 'n';
        rplus::cellSpot(cx, cy, &p.x, &p.z);
        p.x += 150.0f;
        p.z += 150.0f;
        p.reach = 1800.0f;
        p.world = true;
        sPlaces.push_back(p);
    }
    for (const Place& c : camps) {
        if (indexOf(c.key) < 0) {
            sPlaces.push_back(c);
        }
    }
    for (const auto& kv : sFound) {
        if (kv.second.kind == 'k' || kv.second.kind == 'w') {
            addCamp(kv.second.kind, kv.first, kv.second.x, kv.second.z);
        }
    }
    int world = 0;
    for (const Place& p : sPlaces) {
        world += p.world ? 1 : 0;
    }
    fprintf(stderr, "[rplus] places: %d towns, %d doors and NPCs (%d of the file's found)\n", (int)towns.size(),
            world - (int)towns.size(), (int)sFound.size());
}

void markFound(const Place& p, bool news) {
    if (sFound.count(p.key) != 0) {
        return;
    }
    sFound[p.key] = Found{p.x, p.z, p.kind};
    fprintf(stderr, "[rplus] found %s (%s) at %.0f %.0f\n", p.name.c_str(), p.key.c_str(), p.x, p.z);
    if (news) {
        sNews.push_back(p.name);
    }
}

std::string savePath(int slot) {
    const char* saves = getenv("COS_SAVE_DIR");
    std::string dir = saves != nullptr && saves[0] != '\0' ? saves : pc::seedgen::userDir();
    return dir + "/rplus-places-" + std::to_string(pc_rplus_seed()) + "-" + std::to_string(slot) + ".txt";
}

int sectorAt(float x, float z) {
    const int room = rplus::roomAt(x, z);
    return room >= 1 && room <= 49 ? room - 1 : -1;
}

} // namespace

void rplus::notePlace(char kind, int key, float x, float z) {
    addCamp(kind, std::string(kind == 'k' ? "camp:" : "tower:") + std::to_string(key), x, z);
}

void pc_rplus_places_reset(void) {
    sFound.clear();
    sNews.clear();
    sSlot = -1;
    // the home door again (build marks it): rebuilt with the next look
    sBuilt = false;
}

void pc_rplus_places_load(int slot) {
    pc_rplus_places_reset();
    sSlot = slot;
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(savePath(slot).c_str(), "r");
    if (f == nullptr) {
        return;
    }
    char key[64], kind[4];
    float x, z;
    while (fscanf(f, "%63s %f %f %3s", key, &x, &z, kind) == 4) {
        sFound[key] = Found{x, z, kind[0]};
    }
    fclose(f);
    fprintf(stderr, "[rplus] places: %d found (file slot %d)\n", (int)sFound.size(), slot);
}

void pc_rplus_places_save(int slot) {
    if (!pc_rplus_active()) {
        return;
    }
    FILE* f = fopen(savePath(slot).c_str(), "w");
    if (f == nullptr) {
        fprintf(stderr, "[rplus] places: can't write %s\n", savePath(slot).c_str());
        return;
    }
    for (const auto& kv : sFound) {
        fprintf(f, "%s %.0f %.0f %c\n", kv.first.c_str(), kv.second.x, kv.second.z, kv.second.kind);
    }
    fclose(f);
}

void pc_rplus_places_frame(void) {
    static unsigned sTick = 0;
    if (!pc_rplus_active() || (++sTick % 10) != 0) {
        return;
    }
    fopAc_ac_c* link = dComIfGp_getPlayer(0);
    if (link == nullptr) {
        return;
    }
    build();
    const char* stage = dComIfGp_getStartStageName();
    if (strcmp(stage, "sea") != 0) {
        // In a door's interior: that door is found (a way in that was not walked up to, a warp).
        for (const Place& p : sPlaces) {
            if (!p.stage.empty() && p.stage == stage) {
                markFound(p, true);
            }
        }
        return;
    }
    const float lx = link->current.pos.x, lz = link->current.pos.z;
    for (const Place& p : sPlaces) {
        if (p.reach > 0.0f && (lx - p.x) * (lx - p.x) + (lz - p.z) * (lz - p.z) < p.reach * p.reach) {
            markFound(p, true);
        }
    }
}

extern "C" int pc_rplus_place_count(void) {
    build();
    return (int)sPlaces.size();
}

extern "C" const PcRplusPlace* pc_rplus_place(int i) {
    static PcRplusPlace out;
    if (i < 0 || i >= (int)sPlaces.size()) {
        return nullptr;
    }
    const Place& p = sPlaces[(size_t)i];
    out.key = p.key.c_str();
    out.name = p.name.c_str();
    out.kind = p.kind;
    float x0, z0;
    rplus::roomCorner(1, &x0, &z0);
    const float side = 100000.0f * pc_rplus_squares();
    out.u = (p.x - x0) / side;
    out.v = (p.z - z0) / side;
    out.sector = sectorAt(p.x, p.z);
    out.found = sFound.count(p.key) != 0 ? 1 : 0;
    out.camp = p.camp;
    out.opened = p.camp >= 0 && pc_rplus_camp_chest_opened(p.camp) ? 1 : 0;
    out.anchor = p.anchor.c_str();
    return &out;
}

extern "C" void pc_rplus_places_found(int* found, int* total) {
    build();
    *found = *total = 0;
    for (const Place& p : sPlaces) {
        if (p.world) {
            (*total)++;
            *found += sFound.count(p.key) != 0 ? 1 : 0;
        }
    }
}

extern "C" const char* pc_rplus_places_take_found(void) {
    if (sNews.empty()) {
        return nullptr;
    }
    sNewsOut = sNews.front();
    sNews.erase(sNews.begin());
    return sNewsOut.c_str();
}

extern "C" int pc_rplus_sector_seen(int sector) {
    if (!pc_rplus_active() || sector < 0 || sector >= 49) {
        return 0;
    }
    if (pc_qol(PC_QOL_REVEAL_CHART) || dComIfGs_isSaveArriveGrid(sector)) {
        return 1;
    }
    for (size_t i = 0; i < sPlaces.size(); i++) {
        const PcRplusPlace* p = pc_rplus_place((int)i);
        if (p->found && p->sector == sector) {
            return 1;
        }
    }
    return 0;
}
