// The randomizer tracker's state, read from the save (pc_tracker.h, docs/TRACKER.md): what wooferzfg's
// TWW Randomizer Tracker tracks, in its own names, rebuilt every 15 frames on the game thread on a rando
// disc (for the tracker's logic, pc_tracker_logic.cpp, which takes it as JSON) or while the tracker
// screen (pc_tracker_overlay.cpp) is shown.
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
//
// With a randomizer disc the state is also kept on disk, per seed and file slot (see "Progress kept on
// disk" below): what was checked or found stays checked across a crash or a reload of an older save.
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTResFont.h"
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
#include <climits>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#if defined(__SWITCH__)
#include "cos_switch.h"
#endif

// the game's message font (m_Do_ext.cpp)
extern JUTFont* mDoExt_font0;

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

// The save stage the current stage keeps its bits for in dSv_info_c::mMemory (put back into the save at
// the next stage change), -1 outside play
int sCurStage = -1;

void* isPlayScene(void* proc, void*) {
    return fpcM_GetName(proc) == fpcNm_PLAY_SCENE_e ? proc : nullptr;
}

// Progress kept on disk. The state above is the save in memory, so a crash (or loading an older save)
// takes back every check made since the last save. With a randomizer disc, every location checked, item
// count reached and chart salvaged is also written to <user dir>/tracker/<disc>-<size>-file<N>.txt as
// soon as it is seen (one line each, by the tracker's names), and the state shows the union of the two.
// Small keys held are left out: they go down when used. The file is read again when the seed or the file
// slot changes; delete it to start a seed's file over.
constexpr size_t kLocationCount = sizeof(kLocations) / sizeof(kLocations[0]);
constexpr size_t kIslandChartCount = sizeof(kIslandCharts) / sizeof(kIslandCharts[0]);

struct Progress {
    std::string key;  // the file's name, "" when nothing is kept (no randomizer disc, no file loaded)
    std::string path;
    std::vector<bool> locations = std::vector<bool>(kLocationCount);
    std::map<std::string, int> items;
    std::vector<bool> salvaged = std::vector<bool>(kIslandChartCount);
    bool dirty = false;
};
Progress sProgress;

std::string progressDir() {
    if (const char* d = getenv("COS_TRACKER_SAVE_DIR"); d != nullptr && d[0] != '\0') {
        return d;
    }
#if defined(__SWITCH__)
    return COS_SWITCH_ROOT "/user/tracker";
#else
    // next to the options menu's user/settings.ini (pc_settings.cpp)
    char exe[PATH_MAX] = {};
    pc::executablePath(exe, sizeof(exe));
    char real[PATH_MAX];
    std::string dir = ".";
    if (exe[0] != '\0') {
        const char* path = realpath(exe, real) != nullptr ? real : exe;
        const char* slash = strrchr(path, '/');
        if (slash != nullptr) {
            dir.assign(path, (size_t)(slash - path));
        }
    }
    return dir + "/user/tracker";
#endif
}

// The randomizer disc's file name (wwrando names it after the seed) and size, and the file slot
std::string progressKey() {
    if (gConfig.randoDisc == nullptr) {
        return "";
    }
    static std::string sDisc; // the disc's part, worked out once
    if (sDisc.empty()) {
        const char* base = strrchr(gConfig.randoDisc, '/');
        base = base != nullptr ? base + 1 : gConfig.randoDisc;
        for (const char* c = base; *c != '\0'; c++) {
            const bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                               *c == '-' || *c == '_' || *c == '.';
            sDisc += plain ? *c : '_';
        }
        struct stat st = {};
        if (stat(gConfig.randoDisc, &st) == 0) {
            sDisc += "-" + std::to_string((long long)st.st_size);
        }
    }
    return sDisc + "-file" + std::to_string(dComIfGs_getDataNum());
}

bool readLine(FILE* f, std::string& out) {
    out.clear();
    char buf[512];
    bool any = false;
    while (fgets(buf, sizeof(buf), f) != nullptr) {
        any = true;
        out += buf;
        if (out.back() == '\n') {
            break;
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return any;
}

// "L\t<general>\t<detailed>", "I\t<item>\t<count>", "C\t<chart>"
void loadProgress(const std::string& key) {
    sProgress = Progress{};
    sProgress.key = key;
    if (key.empty()) {
        return;
    }
    const std::string dir = progressDir();
    mkdir(dir.c_str(), 0755);
    sProgress.path = dir + "/" + key + ".txt";
    FILE* f = fopen(sProgress.path.c_str(), "r");
    if (f == nullptr) {
        return;
    }
    int n = 0;
    std::string line;
    while (readLine(f, line)) {
        const size_t t1 = line.find('\t');
        if (t1 != 1) {
            continue;
        }
        const size_t t2 = line.find('\t', 2);
        const std::string a = line.substr(2, t2 == std::string::npos ? std::string::npos : t2 - 2);
        const std::string b = t2 == std::string::npos ? "" : line.substr(t2 + 1);
        if (line[0] == 'L') {
            for (size_t i = 0; i < kLocationCount; i++) {
                if (a == kLocations[i].general && b == kLocations[i].detailed) {
                    sProgress.locations[i] = true;
                    n++;
                }
            }
        } else if (line[0] == 'I') {
            sProgress.items[a] = atoi(b.c_str());
        } else if (line[0] == 'C') {
            for (size_t i = 0; i < kIslandChartCount; i++) {
                if (a == kIslandCharts[i].name) {
                    sProgress.salvaged[i] = true;
                }
            }
        }
    }
    fclose(f);
    printf("[cos] tracker: %d checks kept from %s\n", n, sProgress.path.c_str());
}

// Written to a new file then renamed over the old one, so a crash mid-write keeps the last whole file
void saveProgress() {
    sProgress.dirty = false;
    if (sProgress.path.empty()) {
        return;
    }
    const std::string tmp = sProgress.path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f == nullptr) {
        printf("[cos] tracker: cannot write %s\n", tmp.c_str());
        return;
    }
    fprintf(f, "# Waker2 tracker progress (docs/TRACKER.md). Delete this file to start the seed's file over.\n");
    for (size_t i = 0; i < kLocationCount; i++) {
        if (sProgress.locations[i]) {
            fprintf(f, "L\t%s\t%s\n", kLocations[i].general, kLocations[i].detailed);
        }
    }
    for (const auto& [name, count] : sProgress.items) {
        if (count > 0) {
            fprintf(f, "I\t%s\t%d\n", name.c_str(), count);
        }
    }
    for (size_t i = 0; i < kIslandChartCount; i++) {
        if (sProgress.salvaged[i]) {
            fprintf(f, "C\t%s\n", kIslandCharts[i].name);
        }
    }
    fflush(f);
#if !defined(__SWITCH__)
    fsync(fileno(f));
#endif
    fclose(f);
    rename(tmp.c_str(), sProgress.path.c_str());
}

// A count the save shows now, raised to the highest one kept (and the kept one raised to it)
int keptCount(const char* name, int live) {
    if (sProgress.key.empty()) {
        return live;
    }
    int& kept = sProgress.items[name];
    if (live > kept) {
        kept = live;
        sProgress.dirty = true;
    }
    return kept;
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
    bool keep = false;                         // merged with the progress kept on disk
    bool first = true;
    void key(const char* k) {
        out += first ? "" : ", ";
        first = false;
        jsonString(out, k);
        out += ": ";
    }
    void num(const char* k, int v) {
        if (keep) {
            v = keptCount(k, v);
        }
        key(k);
        out += std::to_string(v);
        if (record != nullptr) {
            record->push_back({k, v});
        }
    }
};

void items(std::string& out) {
    dSv_player_c& player = g_dComIfG_gameInfo.save.getPlayer();
    Writer w{out, &sSnapshot.items, true};
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
                const bool live = checked(l);
                bool done = live;
                if (!sProgress.key.empty()) {
                    std::vector<bool>::reference kept = sProgress.locations[&l - kLocations];
                    if (live && !kept) {
                        kept = true;
                        sProgress.dirty = true;
                    }
                    done = kept;
                    sSnapshot.keptOnly += done && !live ? 1 : 0;
                }
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
        bool done = map.isCompleteMap(c.index);
        if (!sProgress.key.empty()) {
            std::vector<bool>::reference kept = sProgress.salvaged[&c - kIslandCharts];
            if (done && !kept) {
                kept = true;
                sProgress.dirty = true;
            }
            done = kept;
        }
        if (done) {
            sSnapshot.salvaged++;
            out += first ? "" : ", ";
            first = false;
            jsonString(out, c.name);
        }
    }
    out += ']';
}

// Where Link is for the tracker screen: on the sea, the room he stays in is the sector (rooms 1-49, A1 to
// G7 row by row); elsewhere the last sector stays (the island he went into), or a dungeon's stages say it
void whereIsLink(const char* stage) {
    static int sLastSector = -1;
    if (strcmp(stage, "sea") == 0) {
        const int room = dComIfGp_roomControl_getStayNo();
        if (room >= 1 && room <= 49) {
            sLastSector = room - 1;
            sSnapshot.atSea = true;
        }
        sSnapshot.where = sLastSector;
        return;
    }
    // the dungeons' stages (and their boss rooms), in the tracker's order: DRC, FW, TotG, FF, ET, WT, GT
    static const struct {
        const char* prefix;
        int dungeon;
    } kDungeons[] = {
        {"M_NewD2", 0}, {"M_DragB", 0}, {"kindan", 1}, {"kinBOSS", 1}, {"Siren", 2}, {"majroom", 3},
        {"MajyuE", 3},  {"ma2room", 3}, {"ma3room", 3}, {"Mjtower", 3}, {"M_Dai", 4}, {"kaze", 5},
        {"Ganon", 6},   {"GTower", 6},  {"Xboss", 6},
    };
    for (const auto& d : kDungeons) {
        if (strncmp(stage, d.prefix, strlen(d.prefix)) == 0) {
            sSnapshot.where = 49 + d.dungeon;
            return;
        }
    }
    sSnapshot.where = sLastSector;
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
    if (const std::string key = progressKey(); key != sProgress.key) {
        loadProgress(key);
    }
    whereIsLink(dComIfGp_getStartStageName());
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
    out += ", \"keptChecks\": " + std::to_string(sSnapshot.keptOnly);
    out += '}';
    if (sProgress.dirty) {
        saveProgress();
    }
    return out;
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

// The game's message font, read from the JUTResFont the game keeps (m_Do_ext.cpp's mDoExt_font0, a
// JUTResFont outside Japan): each glyph page decoded from its GX texture format, each character's cell as
// JUTResFont::loadImage finds it and its width entry as getWidthEntry does.
namespace {
// One texel of a GX intensity texture (I4, I8, IA4, IA8: the formats fonts use) as the alpha of a white
// RGBA texel; false for another format
bool decodeFontPage(const u8* src, int w, int h, int format, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * h * 4, 255);
    int bw, bh; // block size in texels
    switch (format) {
    case GX_TF_I4: bw = 8; bh = 8; break;
    case GX_TF_I8: bw = 8; bh = 4; break;
    case GX_TF_IA4: bw = 8; bh = 4; break;
    case GX_TF_IA8: bw = 4; bh = 4; break;
    default: return false;
    }
    const auto put = [&](int x, int y, int a) {
        if (x < w && y < h) {
            out[(size_t(y) * w + x) * 4 + 3] = uint8_t(a);
        }
    };
    size_t at = 0;
    for (int by = 0; by < h; by += bh) {
        for (int bx = 0; bx < w; bx += bw) {
            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    int a = 0;
                    if (format == GX_TF_I4) {
                        const u8 b = src[at + (y * bw + x) / 2];
                        const int v = (x & 1) == 0 ? b >> 4 : b & 15;
                        a = v * 17;
                    } else if (format == GX_TF_I8) {
                        a = src[at + y * bw + x];
                    } else if (format == GX_TF_IA4) {
                        const u8 b = src[at + y * bw + x];
                        a = ((b >> 4) * 17) * ((b & 15) * 17) / 255; // alpha times intensity
                    } else {
                        const u8* p = src + at + (y * bw + x) * 2;
                        a = p[0] * p[1] / 255;
                    }
                    put(bx + x, by + y, a);
                }
            }
            at += 32; // every block is 32 bytes
        }
    }
    return true;
}
} // namespace

bool gameFont(GameFont& out) {
    JUTResFont* font = static_cast<JUTResFont*>(mDoExt_font0);
    if (font == nullptr || !font->isValid() || font->mpGlyphBlocks == nullptr || font->mGlyphBlockNum == 0) {
        return false;
    }
    out = GameFont{};
    out.ascent = font->getAscent();
    out.descent = font->getDescent();
    out.cellWidth = font->getCellWidth();
    out.cellHeight = font->getCellHeight();
    std::map<std::pair<int, int>, int> pageOf; // (glyph block, page in it) -> index into out.pages
    for (int c = 0x20; c < 0x7F; c++) {
        const int code = font->getFontCode(c);
        int i = 0, cell = code;
        for (; i < font->mGlyphBlockNum; i++) {
            const ResFONT::GLY1* g = font->mpGlyphBlocks[i];
            if (g->startCode <= code && code <= g->endCode) {
                cell = code - g->startCode;
                break;
            }
        }
        if (i == font->mGlyphBlockNum) {
            continue;
        }
        const ResFONT::GLY1* g = font->mpGlyphBlocks[i];
        const int perPage = g->numRows * g->numColumns;
        if (perPage <= 0) {
            continue;
        }
        const int page = cell / perPage, inPage = cell % perPage;
        auto it = pageOf.find({i, page});
        if (it == pageOf.end()) {
            std::vector<uint8_t> rgba;
            if (!decodeFontPage(&g->data[size_t(page) * g->textureSize], g->textureWidth, g->textureHeight,
                                int(u16(g->textureFormat)), rgba)) {
                return false;
            }
            out.pageWidth = g->textureWidth;
            out.pageHeight = g->textureHeight;
            out.pages.push_back(std::move(rgba));
            it = pageOf.emplace(std::make_pair(i, page), int(out.pages.size()) - 1).first;
        }
        FontGlyph& glyph = out.glyphs[c - 0x20];
        glyph.valid = true;
        glyph.page = it->second;
        glyph.x = (inPage % g->numRows) * g->cellWidth; // as JUTResFont::loadImage
        glyph.y = (inPage / g->numRows) * g->cellHeight;
        JUTFont::TWidth width;
        font->getWidthEntry(c, &width);
        glyph.left = width.field_0x0;
        glyph.advance = width.field_0x1;
    }
    return !out.pages.empty();
}

const Snapshot& snapshot() {
    return sSnapshot;
}

} // namespace pc::tracker

extern "C" void pc_tracker_frame_end(unsigned int frame) {
    using namespace pc::tracker;
    // the JSON is the host's memory, not the game's current heap
    JKRPcHostAllocScope hostAlloc;
    // a rando disc: always, for the logic (pc_tracker_logic.cpp), which follows the game's state the way
    // the tracker's autotracking does (small keys gained between two states, ...), and to keep the
    // progress on disk
    const bool logic = pc::gConfig.randoDisc != nullptr;
    if ((!overlayShown() && !logic) || frame % kEveryFrames != 0) {
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
}
