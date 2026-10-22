// The randomizer tracker's state, read from the save (pc_tracker.h, docs/TRACKER.md): what wooferzfg's
// TWW Randomizer Tracker tracks, in its own names, rebuilt every 15 frames on the game thread while the
// tracker server runs, and kept as JSON for the server's thread.
//
// The overlay (pc_tracker_overlay.cpp) shows the same state inside the game; it is built while either runs.
//
// The JSON (version 1):
//   {"version": 1, "game": "GZLE99", "rando": true, "playing": true, "frame": 1234, "saveSlot": 0,
//    "stage": "sea", "room": 44,
//    "items": {"Progressive Sword": 2, "Wind Waker": 1, "Treasure Chart 25": 1, "DRC Big Key": 0, ...},
//    "smallKeysHeld": {"DRC Small Key": 1, ...},
//    "locations": {"Outset Island": {"Underneath Link's House": true, ...}, ...},
//    "salvagedCharts": ["Treasure Chart 25", ...]}
// Every item and location the game can tell is listed, false/0 included; the others (pc_tracker_tables.inc's
// untracked list, small keys used) are left to the tracker. Small keys are the ones held now: the save does
// not count the keys used, so the tracker adds what the game's count gains. A sunken treasure is the
// chart salvaged: the tracker's own chart mapping says which island that chart leads to.
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include "JSystem/JKernel/JKRHeap.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_s_play.h"
#include "d/d_save.h"
#include "d/d_stage.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace pc::tracker {

namespace {

enum Kind { CHEST, SWITCH, PICKUP, EVENT, EVENT_MASK, OCEAN, SPECIAL };
enum Special { SPECIAL_ANKLE = 0, SPECIAL_MAGGIE = 1 };

struct Location {
    const char* general;
    const char* detailed;
    Kind kind;
    int a;
    int b;
};

const Location kLocations[] = {
#define PC_TRACKER_LOCATION(general, detailed, kind, a, b) {general, detailed, kind, a, b},
#include "pc_tracker_tables.inc"
#undef PC_TRACKER_LOCATION
};

struct Chart {
    const char* name;
    int index; // dSv_player_map_c's map index (the collect-map number - 1)
};

const Chart kIslandCharts[] = {
#define PC_TRACKER_CHART(name, index, island, islandName) {name, index},
#include "pc_tracker_tables.inc"
#undef PC_TRACKER_CHART
};

const Chart kOtherCharts[] = {
#define PC_TRACKER_SPECIAL_CHART(name, index) {name, index},
#include "pc_tracker_tables.inc"
#undef PC_TRACKER_SPECIAL_CHART
};

// The dungeons' save stages (dSv_save_c::SaveStageTbl) and the tracker's short names
struct Dungeon {
    const char* name;
    int stage;
    bool keys; // small and big keys (none in the Forsaken Fortress)
};
const Dungeon kDungeons[] = {
    {"DRC", dSv_save_c::STAGE_DRC, true},   {"FW", dSv_save_c::STAGE_FW, true}, {"TotG", dSv_save_c::STAGE_TOTG, true},
    {"FF", dSv_save_c::STAGE_FF, false},    {"ET", dSv_save_c::STAGE_ET, true}, {"WT", dSv_save_c::STAGE_WT, true},
};

constexpr unsigned kEveryFrames = 15;

// The last state for the overlay (game thread only)
Snapshot sSnapshot;

std::mutex sMu;
std::string sJson = "{\"version\": 1, \"playing\": false}";
bool sServing = false;

// The save stage the current stage keeps its bits for in dSv_info_c::mMemory (put back into the save at
// the next stage change), -1 outside play
int sCurStage = -1;

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

dSv_memBit_c& savedBit(int stage) {
    return g_dComIfG_gameInfo.save.getSavedata().getSave(stage).getBit();
}

// A stage's bit: in the save, or in the current stage's memory not yet put back
template <typename F> bool stageBit(int stage, F test) {
    if (stage < 0 || stage >= dSv_save_c::STAGE_MAX) {
        return false;
    }
    if (test(savedBit(stage))) {
        return true;
    }
    return stage == sCurStage && test(g_dComIfG_gameInfo.save.getMemory().getBit());
}

u8 eventByte(int idx) {
    return g_dComIfG_gameInfo.save.getEvent().getPEventBit()[idx & 0xFF];
}

bool special(int which) {
    dSv_player_c& player = g_dComIfG_gameInfo.save.getPlayer();
    switch (which) {
    case SPECIAL_ANKLE:
        // the five Tingle Statues' rewards: the Dragon one's bit is apart from the other four (Archipelago's
        // TINGLE_STATUE_1_ADDR / _2_ADDR)
        return (eventByte(0x12) & 0x40) == 0x40 && (eventByte(0x1D) & 0x0F) == 0x0F;
    case SPECIAL_MAGGIE: {
        // no flag known: Moblin's Letter was owned and is no longer in the Delivery Bag (Archipelago's
        // workaround)
        if (!player.getGetBagItem().isReserve(dItemNo_MOBLINS_LETTER_e - dItemNo_TOWN_FLOWER_e)) {
            return false;
        }
        for (int i = 0; i < 8; i++) {
            if (player.getBagItem().getReserve(i) == dItemNo_MOBLINS_LETTER_e) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

bool checked(const Location& l) {
    switch (l.kind) {
    case CHEST:
        return stageBit(l.a, [&](dSv_memBit_c& b) { return b.isTbox(l.b); });
    case SWITCH:
        return stageBit(l.a, [&](dSv_memBit_c& b) { return b.isSwitch(l.b); });
    case PICKUP:
        return stageBit(l.a, [&](dSv_memBit_c& b) { return b.isItem(l.b); });
    case EVENT:
        return dComIfGs_isEventBit((u16)l.b);
    case EVENT_MASK:
        return (eventByte(l.a) & l.b) == l.b;
    case OCEAN:
        return g_dComIfG_gameInfo.save.getOcean().isOceanSvBit((u8)l.a, (u16)l.b);
    case SPECIAL:
        return special(l.a);
    }
    return false;
}

bool has(int slot) {
    return dComIfGs_getItem(slot) != dItemNo_NONE_e;
}

int count(std::initializer_list<BOOL> bits) {
    int n = 0;
    for (BOOL b : bits) {
        n += b ? 1 : 0;
    }
    return n;
}

void jsonString(std::string& out, const char* s) {
    out += '"';
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') {
            out += '\\';
        }
        out += *s;
    }
    out += '"';
}

struct Writer {
    std::string& out;
    std::vector<NamedCount>* record = nullptr; // also kept for the overlay (pc_tracker_overlay.cpp)
    bool first = true;
    void key(const char* k) {
        out += first ? "" : ", ";
        first = false;
        jsonString(out, k);
        out += ": ";
    }
    void num(const char* k, int v) {
        key(k);
        out += std::to_string(v);
        if (record != nullptr) {
            record->push_back({k, v});
        }
    }
};

void items(std::string& out) {
    dSv_player_c& player = g_dComIfG_gameInfo.save.getPlayer();
    Writer w{out, &sSnapshot.items};
    out += '{';
    const struct {
        const char* name;
        int slot;
    } kSlots[] = {
        {"Telescope", dInvSlot_TELESCOPE_e},       {"Boat's Sail", dInvSlot_SAIL_e},
        {"Wind Waker", dInvSlot_WIND_WAKER_e},     {"Grappling Hook", dInvSlot_GRAPPLING_HOOK_e},
        {"Spoils Bag", dInvSlot_SPOILS_BAG_e},     {"Boomerang", dInvSlot_BOOMERANG_e},
        {"Deku Leaf", dInvSlot_DEKU_LEAF_e},       {"Tingle Tuner", dInvSlot_TINGLE_TUNER_e},
        {"Iron Boots", dInvSlot_IRON_BOOTS_e},     {"Magic Armor", dInvSlot_MAGIC_ARMOR_e},
        {"Bait Bag", dInvSlot_BAIT_BAG_e},         {"Bombs", dInvSlot_BOMB_e},
        {"Delivery Bag", dInvSlot_DELIVERY_BAG_e}, {"Hookshot", dInvSlot_HOOKSHOT_e},
        {"Skull Hammer", dInvSlot_SKULL_HAMMER_e},
    };
    for (const auto& s : kSlots) {
        w.num(s.name, has(s.slot) ? 1 : 0);
    }
    const u8 camera = dComIfGs_getItem(dInvSlot_CAMERA_e);
    w.num("Progressive Picto Box", camera == dItemNo_DELUXE_PICTO_BOX_e ? 2 : camera == dItemNo_PICTO_BOX_e ? 1 : 0);
    const u8 bow = dComIfGs_getItem(dInvSlot_BOW_e);
    w.num("Progressive Bow", bow == dItemNo_LIGHT_ARROW_e ? 3 : bow == dItemNo_MAGIC_ARROW_e ? 2 : bow == dItemNo_BOW_e ? 1 : 0);
    int bottles = 0;
    for (int i = dInvSlot_BOTTLE0_e; i <= dInvSlot_BOTTLE3_e; i++) {
        bottles += has(i) ? 1 : 0;
    }
    w.num("Empty Bottle", bottles);
    // d_item.cpp: the swords set collect 0 bits 0-3, the shields collect 1 bits 0-1, the Power Bracelets
    // collect 2 bit 0, the Hero's Charm collect 4 bit 0
    w.num("Progressive Sword", count({dComIfGs_isCollect(0, 0), dComIfGs_isCollect(0, 1), dComIfGs_isCollect(0, 2),
                                      dComIfGs_isCollect(0, 3)}));
    w.num("Progressive Shield", count({dComIfGs_isCollect(1, 0), dComIfGs_isCollect(1, 1)}));
    w.num("Power Bracelets", dComIfGs_isCollect(2, 0) ? 1 : 0);
    w.num("Hero's Charm", dComIfGs_isCollect(4, 0) ? 1 : 0);
    w.num("Progressive Wallet", dComIfGs_getWalletSize());
    const u8 magic = dComIfGs_getMaxMagic();
    w.num("Progressive Magic Meter", magic > 16 ? 2 : magic > 0 ? 1 : 0);
    const u8 arrows = dComIfGs_getArrowMax();
    w.num("Progressive Quiver", arrows >= 99 ? 2 : arrows >= 60 ? 1 : 0);
    const u8 bombs = dComIfGs_getBombMax();
    w.num("Progressive Bomb Bag", bombs >= 99 ? 2 : bombs >= 60 ? 1 : 0);
    int shards = 0;
    for (int i = 0; i < 8; i++) {
        shards += dComIfGs_isTriforce(i) ? 1 : 0;
    }
    w.num("Triforce Shard", shards);
    w.num("Nayru's Pearl", dComIfGs_isSymbol(dSymbol_NAYRU_e) ? 1 : 0);
    w.num("Din's Pearl", dComIfGs_isSymbol(dSymbol_DIN_e) ? 1 : 0);
    w.num("Farore's Pearl", dComIfGs_isSymbol(dSymbol_FARORE_e) ? 1 : 0);
    const char* const kSongs[] = {"Wind's Requiem",    "Ballad of Gales", "Command Melody",
                                  "Earth God's Lyric", "Wind God's Aria", "Song of Passing"};
    for (int i = 0; i < 6; i++) {
        w.num(kSongs[i], dComIfGs_isTact(i) ? 1 : 0);
    }
    // rando mode's Hurricane Spin item and Tingle Statues set event bits unused in the original game
    // (wwrando's hurricane_spin_item_func, dragon_tingle_statue_item_get_func...); without a randomizer
    // disc, a Tingle Statue is its dungeon's chest 0xF
    const bool rando = gConfig.randoDisc != nullptr;
    w.num("Hurricane Spin", dComIfGs_isEventBit(0x6901) ? 1 : 0);
    int statues = 0;
    for (int i = 0; i < 5; i++) {
        statues += rando ? (eventByte(0x6A) & (0x04 << i)) != 0
                         : stageBit(dSv_save_c::STAGE_DRC + i, [](dSv_memBit_c& b) { return b.isTbox(0xF); });
    }
    w.num("Tingle Statue", statues);
    // the Delivery Bag's "got it" flags: item number - the first bag item (the town flower, 0x8C)
    const struct {
        const char* name;
        u8 item;
    } kLetters[] = {
        {"Note to Mom", dItemNo_NOTE_TO_MOM_e},
        {"Maggie's Letter", dItemNo_MAGGIES_LETTER_e},
        {"Moblin's Letter", dItemNo_MOBLINS_LETTER_e},
        {"Cabana Deed", dItemNo_CABANA_DEED_e},
    };
    for (const auto& l : kLetters) {
        w.num(l.name, player.getGetBagItem().isReserve(l.item - dItemNo_TOWN_FLOWER_e) ? 1 : 0);
    }
    for (const Chart& c : kIslandCharts) {
        w.num(c.name, player.getMap().isGetMap(c.index) ? 1 : 0);
    }
    for (const Chart& c : kOtherCharts) {
        w.num(c.name, player.getMap().isGetMap(c.index) ? 1 : 0);
    }
    char name[32];
    for (const Dungeon& d : kDungeons) {
        snprintf(name, sizeof(name), "%s Dungeon Map", d.name);
        w.num(name, stageBit(d.stage, [](dSv_memBit_c& b) { return b.isDungeonItemMap(); }) ? 1 : 0);
        snprintf(name, sizeof(name), "%s Compass", d.name);
        w.num(name, stageBit(d.stage, [](dSv_memBit_c& b) { return b.isDungeonItemCompass(); }) ? 1 : 0);
        if (d.keys) {
            snprintf(name, sizeof(name), "%s Big Key", d.name);
            w.num(name, stageBit(d.stage, [](dSv_memBit_c& b) { return b.isDungeonItemBossKey(); }) ? 1 : 0);
        }
    }
    out += '}';
}

void smallKeys(std::string& out) {
    Writer w{out, &sSnapshot.smallKeysHeld};
    out += '{';
    char name[32];
    for (const Dungeon& d : kDungeons) {
        if (!d.keys) {
            continue;
        }
        snprintf(name, sizeof(name), "%s Small Key", d.name);
        const int held = d.stage == sCurStage ? g_dComIfG_gameInfo.save.getMemory().getBit().getKeyNum()
                                              : savedBit(d.stage).getKeyNum();
        w.num(name, held);
    }
    out += '}';
}

void locations(std::string& out) {
    // grouped by general location, in the table's order of first appearance
    std::vector<const char*> generals;
    for (const Location& l : kLocations) {
        bool seen = false;
        for (const char* g : generals) {
            seen = seen || strcmp(g, l.general) == 0;
        }
        if (!seen) {
            generals.push_back(l.general);
        }
    }
    Writer outer{out};
    out += '{';
    for (const char* g : generals) {
        outer.key(g);
        Writer inner{out};
        Area area{g, 0, 0};
        out += '{';
        for (const Location& l : kLocations) {
            if (strcmp(l.general, g) == 0) {
                const bool done = checked(l);
                inner.key(l.detailed);
                out += done ? "true" : "false";
                area.checked += done ? 1 : 0;
                area.total++;
            }
        }
        out += '}';
        sSnapshot.areas.push_back(area);
    }
    out += '}';
}

void salvagedCharts(std::string& out) {
    dSv_player_map_c& map = g_dComIfG_gameInfo.save.getPlayer().getMap();
    out += '[';
    bool first = true;
    for (const Chart& c : kIslandCharts) {
        if (map.isCompleteMap(c.index)) {
            sSnapshot.salvaged++;
            out += first ? "" : ", ";
            first = false;
            jsonString(out, c.name);
        }
    }
    out += ']';
}

std::string build(unsigned int frame) {
    std::string out;
    out.reserve(24 * 1024);
    out += "{\"version\": 1, \"game\": ";
    jsonString(out, gameDiscId());
    out += gConfig.randoDisc != nullptr ? ", \"rando\": true" : ", \"rando\": false";
    out += ", \"frame\": " + std::to_string(frame);
    stage_stag_info_class* info = dComIfGp_getStageStagInfo();
    const bool play = fpcM_Search(isPlayScene, nullptr) != nullptr && info != nullptr;
    sSnapshot = Snapshot{};
    if (!play) {
        sCurStage = -1;
        out += ", \"playing\": false}";
        return out;
    }
    sSnapshot.playing = true;
    sCurStage = dStage_stagInfo_GetSaveTbl(info);
    out += ", \"playing\": true, \"saveSlot\": " + std::to_string(dComIfGs_getDataNum());
    out += ", \"stage\": ";
    jsonString(out, dComIfGp_getStartStageName());
    out += ", \"room\": " + std::to_string(dComIfGp_getStartStageRoomNo());
    out += ", \"items\": ";
    items(out);
    out += ", \"smallKeysHeld\": ";
    smallKeys(out);
    out += ", \"locations\": ";
    locations(out);
    out += ", \"salvagedCharts\": ";
    salvagedCharts(out);
    out += '}';
    return out;
}

// COS_TRACKER_SERVER: 0/unset off, 1 the default port, else that port
int serverPort() {
    const char* v = getenv("COS_TRACKER_SERVER");
    if (v == nullptr || *v == '\0') {
        return 0;
    }
    const int n = atoi(v);
    return n == 1 ? 6544 : (n > 0 && n < 65536 ? n : 0);
}

} // namespace

void holdGame(bool first) {
    if (fpcM_Search(isPlayScene, nullptr) == nullptr) {
        return; // the title, the name entry: they run on (without input), as behind the options menu
    }
    if (first) {
        // a rumble running now would never get the game's stop (pc_menu.cpp does the same)
        dComIfGp_getVibration().Kill();
    }
    // the hit-stop pause of the PLAY scene, renewed every frame (pc_menu.cpp)
    dScnPly_ply_c::setPauseTimer(2);
}

const Snapshot& snapshot() {
    return sSnapshot;
}

std::string stateJson() {
    std::lock_guard<std::mutex> lk(sMu);
    return sJson;
}

} // namespace pc::tracker

extern "C" void pc_tracker_frame_end(unsigned int frame) {
    using namespace pc::tracker;
    // the JSON and the server thread are the host's memory, not the game's current heap
    JKRPcHostAllocScope hostAlloc;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (const int port = serverPort(); port != 0) {
            const char* bind = getenv("COS_TRACKER_BIND");
#if defined(__SWITCH__)
            const char* defaultBind = "0.0.0.0"; // the tracker runs on another machine
#else
            const char* defaultBind = "127.0.0.1";
#endif
            sServing = startServer(bind != nullptr && *bind != '\0' ? bind : defaultBind, port);
        }
    }
    // a rando disc: always, for the logic (pc_tracker_logic.cpp), which follows the game's state the way
    // the tracker's autotracking does (small keys gained between two states, ...)
    const bool logic = pc::gConfig.randoDisc != nullptr;
    if ((!sServing && !overlayShown() && !logic) || frame % kEveryFrames != 0) {
        return;
    }
    std::string json = build(frame);
    if (logic) {
        // only a changed state (the frame number aside) costs the logic thread anything
        static std::string sLastForLogic;
        std::string key = json;
        if (const size_t at = key.find(", \"frame\": "); at != std::string::npos) {
            key.erase(at, key.find(',', at + 2) - at);
        }
        if (key != sLastForLogic) {
            sLastForLogic.swap(key);
            logicSubmit(json);
        }
    }
    std::lock_guard<std::mutex> lk(sMu);
    sJson.swap(json);
}
