// The tracker inside the game (pc_tracker.h, docs/TRACKER.md "In the game"), drawn with Aurora's ImGui
// over the game from the state pc_tracker.cpp reads from the save and the tracker's logic
// (pc_tracker_logic.cpp). L3+R3 (both sticks pressed together, on any controller) or F2:
//   1. the tracker screen: the game pauses, and the screen shows the tracker's items, its sea chart (each
//      island's checks in logic / not checked) and the dungeons, in wooferzfg's tracker's own art
//      (tracker-logic/art.rgba); the stick or the D-pad moves the cursor over the islands and dungeons
//      and the list on the right names the one under it, each check coloured as the tracker colours it
//      (blue: in logic, red: not yet, grey: checked). B closes it.
//   2. a small panel in a corner while the game runs: the counts.
//   3. hidden.
// The default button map gives neither stick click to the game. With the options menu open it is hidden.
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include <aurora/imgui.h>
#include <imgui.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" struct SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index);
extern "C" void PADBlockInput(bool block);

namespace pc::tracker {

namespace {

enum Page { HIDDEN, SCREEN, PANEL };

Page sPage = HIDDEN;

// ---- the art: the tracker's images in one atlas (scripts/tracker/pack_art.py) ----------------------

struct Sprite {
    ImVec2 uv0, uv1;
    ImVec2 size; // pixels
};

struct Art {
    bool tried = false;
    ImTextureID texture{};
    std::map<std::string, Sprite> sprites;
} sArt;

void loadArt() {
    sArt.tried = true;
    const std::string dir = logicDir();
    std::ifstream in(dir + "/art.rgba", std::ios::binary);
    char magic[4] = {};
    uint32_t wh[2] = {};
    if (!in.read(magic, 4) || memcmp(magic, "CTA1", 4) != 0 || !in.read(reinterpret_cast<char*>(wh), 8)) {
        writef(STDERR_FILENO, "[cos] tracker: no art in %s (art.rgba)\n", dir.c_str());
        return;
    }
    std::vector<uint8_t> pixels(size_t(wh[0]) * wh[1] * 4);
    if (!in.read(reinterpret_cast<char*>(pixels.data()), std::streamsize(pixels.size()))) {
        return;
    }
    std::ifstream index(dir + "/art.txt");
    std::string line;
    while (std::getline(index, line)) {
        const size_t tab = line.find('\t');
        int x = 0, y = 0, w = 0, h = 0;
        if (tab == std::string::npos || sscanf(line.c_str() + tab + 1, "%d\t%d\t%d\t%d", &x, &y, &w, &h) != 4) {
            continue;
        }
        sArt.sprites[line.substr(0, tab)] = {ImVec2(float(x) / wh[0], float(y) / wh[1]),
                                             ImVec2(float(x + w) / wh[0], float(y + h) / wh[1]),
                                             ImVec2(float(w), float(h))};
    }
    sArt.texture = aurora_imgui_add_texture(wh[0], wh[1], pixels.data());
}

const Sprite* sprite(const std::string& key) {
    const auto it = sArt.sprites.find(key);
    return it == sArt.sprites.end() ? nullptr : &it->second;
}

// ---- input -----------------------------------------------------------------------------------------

enum Key : uint32_t { kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kBack = 16, kToggle = 32, kConfirm = 64 };

// The buttons down now, of every controller and the keyboard
uint32_t keysDown() {
    uint32_t keys = 0;
    for (u32 port = 0; port < 4; port++) {
        SDL_Gamepad* gp = PADGetSDLGamepadForIndex(port);
        if (gp == nullptr) {
            continue;
        }
        const auto button = [gp](SDL_GamepadButton b) { return SDL_GetGamepadButton(gp, b); };
        const int ax = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
        const int ay = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
        constexpr int kDead = 16000;
        keys |= button(SDL_GAMEPAD_BUTTON_DPAD_UP) || ay < -kDead ? kUp : 0;
        keys |= button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ay > kDead ? kDown : 0;
        keys |= button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || ax < -kDead ? kLeft : 0;
        keys |= button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || ax > kDead ? kRight : 0;
        // B: east on an Xbox-style pad, and the Switch's B (Aurora's shim maps it to east)
        keys |= button(SDL_GAMEPAD_BUTTON_EAST) ? kBack : 0;
        keys |= button(SDL_GAMEPAD_BUTTON_SOUTH) ? kConfirm : 0; // A, the same way
        keys |= button(SDL_GAMEPAD_BUTTON_LEFT_STICK) && button(SDL_GAMEPAD_BUTTON_RIGHT_STICK) ? kToggle : 0;
    }
    int count = 0;
    const bool* k = SDL_GetKeyboardState(&count);
    const auto key = [k, count](SDL_Scancode s) { return k != nullptr && int(s) < count && k[s]; };
    keys |= key(SDL_SCANCODE_UP) ? kUp : 0;
    keys |= key(SDL_SCANCODE_DOWN) ? kDown : 0;
    keys |= key(SDL_SCANCODE_LEFT) ? kLeft : 0;
    keys |= key(SDL_SCANCODE_RIGHT) ? kRight : 0;
    keys |= key(SDL_SCANCODE_ESCAPE) ? kBack : 0;
    keys |= key(SDL_SCANCODE_RETURN) ? kConfirm : 0;
    keys |= key(SDL_SCANCODE_F2) ? kToggle : 0;
    return keys;
}

// Pressed this frame, with the directions repeating while held
uint32_t sPrevKeys = 0;
int sRepeat = 0;

uint32_t keysPressed() {
    const uint32_t down = keysDown();
    uint32_t pressed = down & ~sPrevKeys;
    constexpr uint32_t kDirs = kUp | kDown | kLeft | kRight;
    if ((down & kDirs) != 0 && (down & kDirs) == (sPrevKeys & kDirs)) {
        if (++sRepeat >= 18 && sRepeat % 5 == 0) {
            pressed |= down & kDirs;
        }
    } else {
        sRepeat = 0;
    }
    sPrevKeys = down;
    return pressed;
}

// ---- the cursor: the 7x7 sea chart, then a row of the dungeons and the others ------------------------

constexpr int kSeaSide = 7;
constexpr int kExtras = 10; // 7 dungeons, Mailbox, The Great Sea, Hyrule
int sCursor = 43;           // Outset Island
bool sInList = false;       // A on a place: the cursor is in its list of checks
int sListCursor = 0;

void moveCursor(uint32_t pressed) {
    if (sCursor < kSeaSide * kSeaSide) {
        int row = sCursor / kSeaSide, col = sCursor % kSeaSide;
        if (pressed & kLeft) col = (col + kSeaSide - 1) % kSeaSide;
        if (pressed & kRight) col = (col + 1) % kSeaSide;
        if (pressed & kUp) row = std::max(0, row - 1);
        if (pressed & kDown) {
            if (row == kSeaSide - 1) {
                sCursor = kSeaSide * kSeaSide + std::min(col * kExtras / kSeaSide, kExtras - 1);
                return;
            }
            row++;
        }
        sCursor = row * kSeaSide + col;
    } else {
        int i = sCursor - kSeaSide * kSeaSide;
        if (pressed & kLeft) i = (i + kExtras - 1) % kExtras;
        if (pressed & kRight) i = (i + 1) % kExtras;
        if (pressed & kUp) {
            sCursor = (kSeaSide - 1) * kSeaSide + std::min(i * kSeaSide / kExtras, kSeaSide - 1);
            return;
        }
        sCursor = kSeaSide * kSeaSide + i;
    }
}

// ---- drawing ---------------------------------------------------------------------------------------

const ImU32 kAvailable = IM_COL32(120, 190, 255, 255);
const ImU32 kUnavailable = IM_COL32(255, 105, 105, 255);
const ImU32 kChecked = IM_COL32(150, 150, 150, 255);
const ImU32 kNonProgress = IM_COL32(235, 235, 235, 255);
const ImU32 kText = IM_COL32(240, 240, 240, 255);
const ImU32 kDimText = IM_COL32(170, 175, 185, 255);
const ImU32 kAccent = IM_COL32(255, 214, 92, 255);

ImU32 colorOf(char c) {
    switch (c) {
    case LogicColor::AVAILABLE: return kAvailable;
    case LogicColor::CHECKED: return kChecked;
    case LogicColor::NON_PROGRESS: return kNonProgress;
    default: return kUnavailable;
    }
}

// Everything is laid out on a 1280x720 screen, scaled to the real one
struct Canvas {
    ImDrawList* dl;
    ImVec2 origin;
    float scale;
    ImFont* font;
    float fontSize; // the font's size at scale 1 for 1280x720

    ImVec2 at(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }

    void image(const std::string& key, float x, float y, float w = 0, float h = 0, ImU32 tint = IM_COL32_WHITE) const {
        const Sprite* s = sprite(key);
        if (s == nullptr || sArt.texture == ImTextureID{}) {
            return;
        }
        if (w <= 0) {
            w = s->size.x;
            h = s->size.y;
        }
        dl->AddImage(sArt.texture, at(x, y), at(x + w, y + h), s->uv0, s->uv1, tint);
    }

    void rect(float x, float y, float w, float h, ImU32 color, float rounding = 0) const {
        dl->AddRectFilled(at(x, y), at(x + w, y + h), color, rounding * scale);
    }

    void frame(float x, float y, float w, float h, ImU32 color, float thickness, float rounding = 0) const {
        dl->AddRect(at(x, y), at(x + w, y + h), color, rounding * scale, 0, thickness * scale);
    }

    float textWidth(const char* text, float size) const {
        return font->CalcTextSizeA(size * scale, FLT_MAX, 0, text).x / scale;
    }

    // Text with a dark outline, readable over the art; align 0 left, 0.5 centred, 1 right
    void text(const char* text, float x, float y, ImU32 color, float size = 0, float align = 0, bool outline = true) const {
        if (size <= 0) {
            size = fontSize;
        }
        x -= textWidth(text, size) * align;
        if (outline) {
            const ImU32 shadow = IM_COL32(0, 0, 0, 220);
            for (const ImVec2 d : {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1), ImVec2(1, 1)}) {
                dl->AddText(font, size * scale, ImVec2(at(x, y).x + d.x * scale, at(x, y).y + d.y * scale), shadow, text);
            }
        }
        dl->AddText(font, size * scale, at(x, y), color, text);
    }
};

int countOf(const std::vector<NamedCount>& list, const std::string& name) {
    for (const NamedCount& n : list) {
        if (n.name == name) {
            return n.count;
        }
    }
    return 0;
}

// The tracker's item art for a count: ITEMS/<item>/<count>, the highest it has at most
std::string itemKey(const std::string& item, int count) {
    for (int c = count; c > 0; c--) {
        const std::string key = "ITEMS/" + item + "/" + std::to_string(c);
        if (sprite(key) != nullptr) {
            return key;
        }
    }
    return "ITEMS/" + item + "/0";
}

// The item table, in the tracker's own arrangement (src/ui/items-table.jsx): four rows of eight, then
// the pearls, the Triforce and a row of seven
const char* const kItemRows[4][8] = {
    {"Telescope", "Boat's Sail", "Wind Waker", "Grappling Hook", "Spoils Bag", "Boomerang", "Deku Leaf",
     "Progressive Sword"},
    {"Tingle Tuner", "Progressive Picto Box", "Iron Boots", "Magic Armor", "Bait Bag", "Progressive Bow", "Bombs",
     "Progressive Shield"},
    {"Cabana Deed", "Maggie's Letter", "Moblin's Letter", "Note to Mom", "Delivery Bag", "Hookshot", "Skull Hammer",
     "Power Bracelets"},
    {"Empty Bottle", "Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric", "Wind God's Aria",
     "Song of Passing", "Hero's Charm"},
};
const char* const kItemBottom[7] = {"Tingle Statue", "Ghost Ship Chart", "Hurricane Spin", "Progressive Bomb Bag",
                                    "Progressive Quiver", "Progressive Wallet", "Progressive Magic Meter"};

void drawItems(const Canvas& c, const Snapshot& s, float x0, float y0) {
    c.image("ITEMS_TABLE_BACKGROUND", x0, y0);
    constexpr float kCell = 52.5f, kIcon = 46.0f;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            const char* item = kItemRows[row][col];
            const int n = countOf(s.items, item);
            c.image(itemKey(item, n), x0 + 18 + col * kCell, y0 + 13 + row * kCell, kIcon, kIcon);
            if (n > 1 && strcmp(item, "Empty Bottle") == 0) {
                char t[8];
                snprintf(t, sizeof t, "%d", n);
                c.text(t, x0 + 18 + col * kCell + kIcon, y0 + 13 + row * kCell + kIcon - 16, kText, 0, 1);
            }
        }
    }
    // the pearls on their holder and the Triforce under the first four columns, two rows of the rest at
    // the right (as the tracker places them)
    const float py = y0 + 13 + 4 * kCell + 2;
    c.image("PEARL_HOLDER", x0 + 14, py, 100, 100);
    c.image(itemKey("Nayru's Pearl", countOf(s.items, "Nayru's Pearl")), x0 + 46, py + 4, 36, 36);
    c.image(itemKey("Din's Pearl", countOf(s.items, "Din's Pearl")), x0 + 20, py + 52, 36, 36);
    c.image(itemKey("Farore's Pearl", countOf(s.items, "Farore's Pearl")), x0 + 72, py + 52, 36, 36);
    c.image(itemKey("Triforce Shard", countOf(s.items, "Triforce Shard")), x0 + 122, py + 2, 96, 96);
    for (int i = 0; i < 7; i++) {
        const char* item = kItemBottom[i];
        const int n = countOf(s.items, item);
        const int col = i < 3 ? 5 + i : 4 + (i - 3);
        const float ix = x0 + 18 + col * kCell, iy = py + (i < 3 ? 0 : kCell);
        c.image(itemKey(item, n), ix, iy, kIcon, kIcon);
        if (strcmp(item, "Tingle Statue") == 0 && n > 0) {
            char t[8];
            snprintf(t, sizeof t, "%d", n);
            c.text(t, ix + kIcon, iy + kIcon - 16, kText, 0, 1);
        }
    }
}

// The 7x7 sea chart: each island's checks in logic / not checked, the cursor's square framed
void drawSeaChart(const Canvas& c, const LogicResult& logic, float x0, float y0, float w, float h) {
    c.image("SEA_CHART", x0, y0, w, h);
    const float cw = w / kSeaSide, ch = h / kSeaSide;
    for (int i = 0; i < kSeaSide * kSeaSide; i++) {
        const float x = x0 + (i % kSeaSide) * cw, y = y0 + (i / kSeaSide) * ch;
        if (logic.ready && i < int(logic.areas.size())) {
            const LogicArea& a = logic.areas[i];
            char t[16];
            snprintf(t, sizeof t, "%d/%d", a.available, a.remaining);
            c.text(t, x + cw * 0.5f, y + ch - c.fontSize - 3, colorOf(a.color), 0, 0.5f);
        }
        if (i == sCursor) {
            c.frame(x + 1, y + 1, cw - 2, ch - 2, kAccent, 3, 4);
        }
    }
}

// The dungeons, each with its map, compass, big key and small keys held, and the three other places
void drawExtras(const Canvas& c, const Snapshot& s, const LogicResult& logic, float x0, float y0) {
    static const char* const kNames[kExtras] = {"Dragon Roost Cavern", "Forbidden Woods", "Tower of the Gods",
                                                 "Forsaken Fortress", "Earth Temple", "Wind Temple", "Ganon's Tower",
                                                 "Mailbox", "The Great Sea", "Hyrule"};
    static const char* const kShort[7] = {"DRC", "FW", "TotG", "FF", "ET", "WT", "GT"};
    constexpr float kCell = 85;
    c.rect(x0 - 8, y0 - 8, kCell * kExtras + 12, 150, IM_COL32(20, 34, 54, 210), 8);
    for (int i = 0; i < kExtras; i++) {
        const float x = x0 + i * kCell;
        const std::string name = kNames[i];
        if (i < 7) {
            const std::string prefix = kShort[i];
            const bool keys = i != 3 && i != 6; // the Forsaken Fortress and Ganon's Tower have none
            if (keys) {
                const int held = countOf(s.smallKeysHeld, prefix + " Small Key");
                c.image("SMALL_KEYS/" + std::to_string(std::min(held, 4)), x + 2, y0, 20, 20);
                c.image("BIG_KEYS/" + std::to_string(countOf(s.items, prefix + " Big Key") > 0 ? 1 : 0), x + 22, y0, 20, 20);
            }
            if (i != 6) {
                c.image("DUNGEON_MAPS/" + std::to_string(countOf(s.items, prefix + " Dungeon Map") > 0 ? 1 : 0), x + 42, y0, 20, 20);
                c.image("COMPASSES/" + std::to_string(countOf(s.items, prefix + " Compass") > 0 ? 1 : 0), x + 62, y0, 20, 20);
            }
            c.image("DUNGEONS/" + name + "/false", x + 4, y0 + 22, 76, 76);
        } else {
            c.image("MISC_LOCATIONS/" + name, x + 4, y0 + 22, 76, 76);
        }
        const int index = kSeaSide * kSeaSide + i;
        if (logic.ready && index < int(logic.areas.size())) {
            const LogicArea& a = logic.areas[index];
            char t[16];
            snprintf(t, sizeof t, "%d/%d", a.available, a.remaining);
            c.text(t, x + 42, y0 + 104, colorOf(a.color), 0, 0.5f);
        }
        if (index == sCursor) {
            c.frame(x - 2, y0 - 4, kCell - 4, 132, kAccent, 3, 6);
        }
    }
}

// The list of the area under the cursor
void drawList(const Canvas& c, const LogicResult& logic, float x0, float y0, float w, float h) {
    c.rect(x0, y0, w, h, IM_COL32(20, 34, 54, 225), 8);
    if (!logic.ready) {
        c.text("Logic", x0 + 14, y0 + 12, kAccent, 22);
        const std::string why = logic.error.empty() ? "starting..." : logic.error;
        // wrapped by hand: the canvas draws one line at a time
        std::string line;
        float y = y0 + 44;
        for (size_t i = 0; i <= why.size(); i++) {
            if (i == why.size() || why[i] == ' ') {
                if (c.textWidth(line.c_str(), c.fontSize) > w - 28 || i == why.size()) {
                    c.text(line.c_str(), x0 + 14, y, kDimText);
                    y += c.fontSize + 4;
                    line.clear();
                } else {
                    line += ' ';
                }
                continue;
            }
            line += why[i];
        }
        return;
    }
    if (sCursor >= int(logic.areas.size())) {
        return;
    }
    const LogicArea& a = logic.areas[sCursor];
    c.text(a.name.c_str(), x0 + 14, y0 + 12, kAccent, 22);
    char t[48];
    snprintf(t, sizeof t, "%d in logic, %d not checked", a.available, a.remaining);
    c.text(t, x0 + 14, y0 + 40, kDimText);
    float y = y0 + 70;
    if (a.locations.empty()) {
        c.text("No progress locations here.", x0 + 14, y, kDimText);
    }
    // scrolled so the cursor stays in view
    const int rows = int((h - 70 - 10) / (c.fontSize + 7));
    const int first = sInList ? std::max(0, std::min(sListCursor - rows / 2, int(a.locations.size()) - rows)) : 0;
    for (int i = first; i < int(a.locations.size()); i++) {
        const LogicLocation& l = a.locations[i];
        if (y > y0 + h - 24) {
            c.text("...", x0 + 14, y, kDimText);
            break;
        }
        if (sInList && i == sListCursor) {
            c.frame(x0 + 8, y - 3, w - 16, c.fontSize + 6, kAccent, 2, 4);
        }
        c.rect(x0 + 14, y + 5, 8, 8, colorOf(l.color), 2);
        c.text(l.name.c_str(), x0 + 30, y, colorOf(l.color));
        y += c.fontSize + 7;
    }
}

void drawScreen() {
    const Snapshot& s = snapshot();
    const LogicResult& logic = logicResult();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(4, 10, 22, 225));
    Canvas c{dl,
             ImVec2((io.DisplaySize.x - 1280 * scale) * 0.5f, (io.DisplaySize.y - 720 * scale) * 0.5f),
             scale, ImGui::GetFont(), 17.0f};

    c.text("Tracker", 40, 18, kAccent, 30);
    if (logic.ready) {
        char t[128];
        snprintf(t, sizeof t, "%d in logic   %d not checked   %d checked", logic.available, logic.remaining,
                 logic.checked);
        c.text(t, 1240, 26, kText, 20, 1);
    } else if (!s.playing) {
        c.text("Start or load a file.", 1240, 26, kDimText, 20, 1);
    }
    if (s.playing && s.keptOnly > 0) {
        // progress kept on disk (pc_tracker.cpp) that this save does not have
        char t[96];
        snprintf(t, sizeof t, "%d checked before a crash or in a later save: kept", s.keptOnly);
        c.text(t, 1240, 48, kDimText, 15, 1);
    }

    drawItems(c, s, 40, 66);
    drawSeaChart(c, logic, 516, 66, 382, 363);
    drawList(c, logic, 918, 66, 322, 520);
    drawExtras(c, s, logic, 48, 456);

    c.text(sInList ? "Stick / D-pad: move     A: mark checked / not     B: back"
                   : "Stick / D-pad: move     A: its checks     B: close     L3+R3: small panel",
           40, 680, kDimText, 16);
    c.text("Art and logic: wooferzfg's TWW Randomizer Tracker", 1240, 680, kDimText, 14, 1);
}

void drawPanel() {
    const Snapshot& s = snapshot();
    const LogicResult& logic = logicResult();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f * 1.25f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8.0f, 8.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.78f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##cos_tracker_panel", nullptr, kFlags)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(scale);
    const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(kAccent);
    if (!s.playing) {
        ImGui::TextColored(accent, "Tracker");
        ImGui::TextDisabled("Start or load a file.");
    } else if (logic.ready) {
        ImGui::TextColored(accent, "Tracker");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAvailable), "%d in logic", logic.available);
        ImGui::SameLine();
        ImGui::Text("/ %d left", logic.remaining);
        if (sCursor < int(logic.areas.size())) {
            const LogicArea& a = logic.areas[sCursor];
            ImGui::TextDisabled("%s: %d / %d", a.name.c_str(), a.available, a.remaining);
        }
    } else {
        int checked = 0, total = 0;
        for (const Area& a : s.areas) {
            checked += a.checked;
            total += a.total;
        }
        ImGui::TextColored(accent, "Tracker  %d/%d checks  +%d/49 sunken", checked, total, s.salvaged);
    }
    ImGui::TextDisabled("L3+R3 / F2: hide");
    ImGui::End();
}

void handleScreenKeys(uint32_t pressed) {
    const LogicResult& logic = logicResult();
    const LogicArea* area = logic.ready && sCursor < int(logic.areas.size()) ? &logic.areas[sCursor] : nullptr;
    if (!sInList) {
        moveCursor(pressed);
        if ((pressed & kConfirm) != 0 && area != nullptr && !area->locations.empty()) {
            sInList = true;
            sListCursor = 0;
        }
        return;
    }
    if (area == nullptr || area->locations.empty()) {
        sInList = false;
        return;
    }
    const int n = int(area->locations.size());
    if (pressed & kUp) sListCursor = (sListCursor + n - 1) % n;
    if (pressed & kDown) sListCursor = (sListCursor + 1) % n;
    sListCursor = std::min(sListCursor, n - 1);
    if (pressed & kConfirm) {
        logicToggle(area->name, area->locations[sListCursor].name);
    }
}

void setPage(Page page) {
    if (page == sPage) {
        return;
    }
    sInList = false;
    if (page == SCREEN) {
        PADBlockInput(true);
        holdGame(true);
    } else if (sPage == SCREEN) {
        PADBlockInput(false);
    }
    sPage = page;
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
    if (pc::menuOpen()) {
        // the options menu has the controller (and blocks the game's input itself)
        sPrevKeys = keysDown();
        return;
    }
    uint32_t pressed = keysPressed();
    if (pressed & kToggle) {
        setPage(sPage == HIDDEN ? SCREEN : sPage == SCREEN ? PANEL : HIDDEN);
    } else if (sPage == SCREEN && (pressed & kBack) != 0) {
        if (sInList) {
            sInList = false;
        } else {
            setPage(HIDDEN);
        }
        pressed &= ~kBack;
    }
    if (sPage == SCREEN) {
        if (!sArt.tried) {
            loadArt();
        }
        PADBlockInput(true); // again: the options menu unblocks on closing
        holdGame(false);
        handleScreenKeys(pressed);
        drawScreen();
    } else if (sPage == PANEL) {
        drawPanel();
    }
}

extern "C" void pc_tracker_overlay_draw(void) {
    using namespace pc::tracker;
    if (ImGui::GetCurrentContext() == nullptr || pc::menuOpen()) {
        return;
    }
    if (sPage == SCREEN && sArt.tried) {
        drawScreen();
    } else if (sPage == PANEL) {
        drawPanel();
    }
}
