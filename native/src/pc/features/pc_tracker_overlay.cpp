// The tracker inside the game (pc_tracker.h, docs/TRACKER.md): the state pc_tracker.cpp reads from the
// save, drawn with Aurora's ImGui over the game. L3+R3 (both sticks pressed together, on any controller)
// or F2 shows the items page, a second press the locations page, a third hides it. The game keeps
// running and keeps its input (the default button map gives neither stick click to the game); with the
// options menu open the overlay is hidden.
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include <imgui.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" struct SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index);

namespace pc::tracker {

namespace {

enum Page { HIDDEN, ITEMS, LOCATIONS, PAGES };

Page sPage = HIDDEN;
bool sHeld = false; // the toggle was down last frame

const ImVec4 kAccent(0.55f, 0.85f, 1.0f, 1.0f);
const ImVec4 kHave(1.0f, 1.0f, 1.0f, 1.0f);
const ImVec4 kFull(0.55f, 0.95f, 0.55f, 1.0f);
const ImVec4 kDim(0.55f, 0.55f, 0.6f, 1.0f);

struct ItemCell {
    const char* tracker; // the tracker's name
    const char* label;
    int max;
};

const ItemCell kItems[] = {
    {"Progressive Sword", "Sword", 4},
    {"Progressive Shield", "Shield", 2},
    {"Progressive Bow", "Bow", 3},
    {"Bombs", "Bombs", 1},
    {"Progressive Bomb Bag", "Bomb Bag", 2},
    {"Progressive Quiver", "Quiver", 2},
    {"Grappling Hook", "Grappling Hook", 1},
    {"Hookshot", "Hookshot", 1},
    {"Boomerang", "Boomerang", 1},
    {"Deku Leaf", "Deku Leaf", 1},
    {"Skull Hammer", "Skull Hammer", 1},
    {"Power Bracelets", "Power Bracelets", 1},
    {"Iron Boots", "Iron Boots", 1},
    {"Bait Bag", "Bait Bag", 1},
    {"Spoils Bag", "Spoils Bag", 1},
    {"Delivery Bag", "Delivery Bag", 1},
    {"Empty Bottle", "Bottles", 4},
    {"Progressive Picto Box", "Picto Box", 2},
    {"Magic Armor", "Magic Armor", 1},
    {"Hero's Charm", "Hero's Charm", 1},
    {"Progressive Magic Meter", "Magic", 2},
    {"Progressive Wallet", "Wallet", 2},
    {"Telescope", "Telescope", 1},
    {"Boat's Sail", "Sail", 1},
    {"Wind Waker", "Wind Waker", 1},
    {"Tingle Tuner", "Tingle Tuner", 1},
    {"Hurricane Spin", "Hurricane Spin", 1},
    {"Ghost Ship Chart", "Ghost Ship Chart", 1},
    {"Triforce Shard", "Triforce Shards", 8},
    {"Tingle Statue", "Tingle Statues", 5},
    {"Nayru's Pearl", "Nayru's Pearl", 1},
    {"Din's Pearl", "Din's Pearl", 1},
    {"Farore's Pearl", "Farore's Pearl", 1},
    {"Wind's Requiem", "Wind's Requiem", 1},
    {"Ballad of Gales", "Ballad of Gales", 1},
    {"Command Melody", "Command Melody", 1},
    {"Earth God's Lyric", "Earth God's Lyric", 1},
    {"Wind God's Aria", "Wind God's Aria", 1},
    {"Song of Passing", "Song of Passing", 1},
    {"Note to Mom", "Note to Mom", 1},
    {"Maggie's Letter", "Maggie's Letter", 1},
    {"Moblin's Letter", "Moblin's Letter", 1},
    {"Cabana Deed", "Cabana Deed", 1},
};

struct DungeonRow {
    const char* shortName; // the tracker's prefix ("DRC Big Key")
    const char* area;      // the general location of its checks
    bool keys;
};

const DungeonRow kDungeonRows[] = {
    {"DRC", "Dragon Roost Cavern", true}, {"FW", "Forbidden Woods", true}, {"TotG", "Tower of the Gods", true},
    {"FF", "Forsaken Fortress", false},   {"ET", "Earth Temple", true},    {"WT", "Wind Temple", true},
};

int countOf(const std::vector<NamedCount>& list, const std::string& name) {
    for (const NamedCount& n : list) {
        if (n.name == name) {
            return n.count;
        }
    }
    return 0;
}

bool togglePressed() {
    bool down = false;
    for (u32 port = 0; port < 4 && !down; port++) {
        SDL_Gamepad* gp = PADGetSDLGamepadForIndex(port);
        down = gp != nullptr && SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
               SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    }
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    down = down || (keys != nullptr && count > SDL_SCANCODE_F2 && keys[SDL_SCANCODE_F2]);
    const bool pressed = down && !sHeld;
    sHeld = down;
    return pressed;
}

void label(const ImVec4& color, const char* text) {
    ImGui::TextColored(color, "%s", text);
}

void drawItems(const Snapshot& s) {
    constexpr int kColumns = 4;
    if (ImGui::BeginTable("##items", kColumns, ImGuiTableFlags_SizingFixedFit)) {
        for (const ItemCell& c : kItems) {
            ImGui::TableNextColumn();
            const int n = countOf(s.items, c.tracker);
            char text[48];
            if (c.max > 1) {
                snprintf(text, sizeof(text), "%s %d/%d", c.label, n, c.max);
            } else {
                snprintf(text, sizeof(text), "%s", c.label);
            }
            label(n <= 0 ? kDim : n >= c.max ? kFull : kHave, text);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("##dungeons", 6, ImGuiTableFlags_SizingFixedFit)) {
        for (const DungeonRow& d : kDungeonRows) {
            const std::string prefix = d.shortName;
            ImGui::TableNextColumn();
            label(kAccent, d.shortName);
            ImGui::TableNextColumn();
            label(countOf(s.items, prefix + " Dungeon Map") > 0 ? kHave : kDim, "Map");
            ImGui::TableNextColumn();
            label(countOf(s.items, prefix + " Compass") > 0 ? kHave : kDim, "Compass");
            ImGui::TableNextColumn();
            if (d.keys) {
                label(countOf(s.items, prefix + " Big Key") > 0 ? kHave : kDim, "Big Key");
            }
            ImGui::TableNextColumn();
            if (d.keys) {
                const int held = countOf(s.smallKeysHeld, prefix + " Small Key");
                ImGui::TextColored(held > 0 ? kHave : kDim, "Keys %d", held);
            }
            ImGui::TableNextColumn();
            for (const Area& a : s.areas) {
                if (strcmp(a.name, d.area) == 0) {
                    ImGui::TextColored(a.checked == a.total ? kFull : kHave, "%d/%d", a.checked, a.total);
                }
            }
        }
        ImGui::EndTable();
    }
}

void drawLocations(const Snapshot& s) {
    constexpr int kColumns = 3;
    if (ImGui::BeginTable("##areas", kColumns * 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const Area& a : s.areas) {
            const ImVec4& color = a.checked == a.total ? kFull : a.checked > 0 ? kHave : kDim;
            ImGui::TableNextColumn();
            label(color, a.name);
            ImGui::TableNextColumn();
            ImGui::TextColored(color, "%d/%d", a.checked, a.total);
        }
        ImGui::TableNextColumn();
        label(s.salvaged == 49 ? kFull : s.salvaged > 0 ? kHave : kDim, "Sunken treasures");
        ImGui::TableNextColumn();
        ImGui::TextColored(s.salvaged == 49 ? kFull : kHave, "%d/49", s.salvaged);
        ImGui::EndTable();
    }
}

} // namespace

bool overlayShown() {
    return sPage != HIDDEN;
}

} // namespace pc::tracker

extern "C" void pc_tracker_overlay_frame(void) {
    using namespace pc::tracker;
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    if (togglePressed() && !pc::menuOpen()) {
        sPage = Page((sPage + 1) % PAGES);
    }
    if (sPage == HIDDEN || pc::menuOpen()) {
        return;
    }
    const Snapshot& s = snapshot();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f * 1.25f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8.0f, 8.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.78f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##cos_tracker_overlay", nullptr, kFlags)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(scale);
    int checked = 0;
    int total = 0;
    for (const Area& a : s.areas) {
        checked += a.checked;
        total += a.total;
    }
    if (!s.playing) {
        label(kAccent, "Tracker");
        label(kDim, "Start or load a file.");
    } else {
        ImGui::TextColored(kAccent, "Tracker  %d/%d checks  +%d/49 sunken", checked, total, s.salvaged);
        ImGui::SameLine();
        label(kDim, sPage == ITEMS ? "  (L3+R3 / F2: locations)" : "  (L3+R3 / F2: hide)");
        ImGui::Separator();
        if (sPage == ITEMS) {
            drawItems(s);
        } else {
            drawLocations(s);
        }
    }
    ImGui::End();
}
