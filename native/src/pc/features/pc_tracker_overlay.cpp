// The tracker inside the game (pc_tracker.h, docs/TRACKER.md "In the game"), drawn with Aurora's ImGui
// over the game from the state pc_tracker.cpp reads from the save and the tracker's logic
// (pc_tracker_logic.cpp). L3+R3 (both sticks pressed together, on any controller) or F2:
//   1. the tracker screen: the game pauses, and the screen shows the tracker's items, its sea chart (each
//      island's checks in logic / not checked) and the dungeons, in wooferzfg's tracker's own art
//      (tracker-logic/art.rgba); the stick or the D-pad moves the cursor over the items, the islands and
//      the dungeons. On a place the list on the right names its checks, each coloured as the tracker
//      colours it (blue: in logic, red: not yet, grey, ticked and struck out: checked); A goes into the
//      list, where A marks a check by hand. On an item A gives one more, X one less (the tracker's
//      click), for what the game cannot tell. B goes back, then closes it.
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

// ---- the game's own font (pc_tracker.cpp's gameFont), ImGui's until the game has it -----------------

struct Font {
    bool ready = false;
    GameFont data;
    std::vector<ImTextureID> pages;
} sFont;

void loadFont() {
    if (sFont.ready || !gameFont(sFont.data)) {
        return;
    }
    for (const std::vector<uint8_t>& page : sFont.data.pages) {
        sFont.pages.push_back(aurora_imgui_add_texture(sFont.data.pageWidth, sFont.data.pageHeight, page.data()));
    }
    sFont.ready = true;
    writef(STDERR_FILENO, "[cos] tracker: the game's font, %d page(s)\n", int(sFont.pages.size()));
}

const Sprite* sprite(const std::string& key) {
    const auto it = sArt.sprites.find(key);
    return it == sArt.sprites.end() ? nullptr : &it->second;
}

// ---- input -----------------------------------------------------------------------------------------

enum Key : uint32_t { kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kBack = 16, kToggle = 32, kConfirm = 64, kLess = 128 };

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
        keys |= button(SDL_GAMEPAD_BUTTON_WEST) ? kLess : 0;     // X on an Xbox-style pad (the Deck)
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
    keys |= key(SDL_SCANCODE_BACKSPACE) ? kLess : 0;
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

// ---- the cursor: the items, the 7x7 sea chart, then a row of the dungeons and the others -------------

constexpr int kSeaSide = 7;
constexpr int kExtras = 10; // 7 dungeons, Mailbox, The Great Sea, Hyrule
int sCursor = 43;           // the area under the cursor (logic.areas' order): Outset Island
int sItem = -1;             // or the item under it (kItemSlots), -1: an area
bool sInList = false;       // A on a place: the cursor is in its list of checks
int sListCursor = 0;

// The item table, in the tracker's own arrangement (src/ui/items-table.jsx): four rows of eight, then
// the pearls on their holder and the Triforce under the first four columns, two rows of the rest at
// the right. Positions on the 1280x720 layout.
constexpr float kItemsX = 40, kItemsY = 66, kItemCell = 52.5f, kItemIcon = 46.0f;
struct ItemSlot {
    const char* name;
    float x, y, size;
};
const std::vector<ItemSlot>& itemSlots() {
    static const std::vector<ItemSlot> slots = [] {
        static const char* const kRows[4][8] = {
            {"Telescope", "Boat's Sail", "Wind Waker", "Grappling Hook", "Spoils Bag", "Boomerang", "Deku Leaf",
             "Progressive Sword"},
            {"Tingle Tuner", "Progressive Picto Box", "Iron Boots", "Magic Armor", "Bait Bag", "Progressive Bow",
             "Bombs", "Progressive Shield"},
            {"Cabana Deed", "Maggie's Letter", "Moblin's Letter", "Note to Mom", "Delivery Bag", "Hookshot",
             "Skull Hammer", "Power Bracelets"},
            {"Empty Bottle", "Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric",
             "Wind God's Aria", "Song of Passing", "Hero's Charm"},
        };
        static const char* const kBottom[7] = {"Tingle Statue", "Ghost Ship Chart", "Hurricane Spin",
                                               "Progressive Bomb Bag", "Progressive Quiver", "Progressive Wallet",
                                               "Progressive Magic Meter"};
        std::vector<ItemSlot> v;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 8; col++) {
                v.push_back({kRows[row][col], kItemsX + 18 + col * kItemCell, kItemsY + 13 + row * kItemCell, kItemIcon});
            }
        }
        const float py = kItemsY + 13 + 4 * kItemCell + 2;
        v.push_back({"Nayru's Pearl", kItemsX + 46, py + 4, 36});
        v.push_back({"Din's Pearl", kItemsX + 20, py + 52, 36});
        v.push_back({"Farore's Pearl", kItemsX + 72, py + 52, 36});
        v.push_back({"Triforce Shard", kItemsX + 122, py + 2, 96});
        for (int i = 0; i < 7; i++) {
            const int col = i < 3 ? 5 + i : 4 + (i - 3);
            v.push_back({kBottom[i], kItemsX + 18 + col * kItemCell, py + (i < 3 ? 0 : kItemCell), kItemIcon});
        }
        return v;
    }();
    return slots;
}

// The sea chart and the row of the others, on the same layout
constexpr float kSeaX = 516, kSeaY = 66, kSeaW = 382, kSeaH = 363;
constexpr float kExtrasX = 48, kExtrasY = 456, kExtrasCell = 85;

// Every place the cursor can be, by its centre: item i is -1 - i, an area its index
struct Target {
    int id;
    float x, y;
};
std::vector<Target> targets() {
    std::vector<Target> t;
    const std::vector<ItemSlot>& slots = itemSlots();
    for (size_t i = 0; i < slots.size(); i++) {
        t.push_back({-1 - int(i), slots[i].x + slots[i].size / 2, slots[i].y + slots[i].size / 2});
    }
    for (int i = 0; i < kSeaSide * kSeaSide; i++) {
        t.push_back({i, kSeaX + (i % kSeaSide + 0.5f) * kSeaW / kSeaSide, kSeaY + (i / kSeaSide + 0.5f) * kSeaH / kSeaSide});
    }
    for (int i = 0; i < kExtras; i++) {
        t.push_back({kSeaSide * kSeaSide + i, kExtrasX + i * kExtrasCell + 40, kExtrasY + 60});
    }
    return t;
}

// The nearest place in the direction pressed (straight ahead counts more than off to the side)
void moveCursor(uint32_t pressed) {
    const float dx = (pressed & kRight) ? 1.0f : (pressed & kLeft) ? -1.0f : 0.0f;
    const float dy = (pressed & kDown) ? 1.0f : (pressed & kUp) ? -1.0f : 0.0f;
    if (dx == 0 && dy == 0) {
        return;
    }
    const std::vector<Target> all = targets();
    const int here = sItem >= 0 ? -1 - sItem : sCursor;
    const Target* from = nullptr;
    for (const Target& t : all) {
        if (t.id == here) {
            from = &t;
        }
    }
    if (from == nullptr) {
        return;
    }
    const Target* best = nullptr;
    float bestScore = 0;
    for (const Target& t : all) {
        const float ox = t.x - from->x, oy = t.y - from->y;
        const float ahead = dx != 0 ? ox * dx : oy * dy;
        const float side = dx != 0 ? oy : ox;
        if (ahead < 8) {
            continue;
        }
        const float score = ahead + 2.5f * (side < 0 ? -side : side);
        if (best == nullptr || score < bestScore) {
            best = &t;
            bestScore = score;
        }
    }
    if (best != nullptr) {
        if (best->id < 0) {
            sItem = -1 - best->id;
        } else {
            sItem = -1;
            sCursor = best->id;
        }
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
const ImU32 kPanel = IM_COL32(10, 18, 30, 150);     // the panels, the game showing through
const ImU32 kLink = IM_COL32(90, 230, 110, 255);    // where Link is

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

    void line(float x1, float y1, float x2, float y2, ImU32 color, float thickness) const {
        dl->AddLine(at(x1, y1), at(x2, y2), color, thickness * scale);
    }

    // A tick in a box of side h at (x, y)
    void tick(float x, float y, float h, ImU32 color) const {
        const ImVec2 points[3] = {at(x, y + h * 0.55f), at(x + h * 0.38f, y + h * 0.9f), at(x + h, y + h * 0.1f)};
        dl->AddPolyline(points, 3, color, 0, 2.5f * scale);
    }

    // Where Link is: a green ring with a dot (dimmer for the sector he was last in)
    void linkMarker(float x, float y, float r, bool now) const {
        const ImU32 color = now ? kLink : IM_COL32(90, 230, 110, 150);
        dl->AddCircle(at(x, y), r * scale, IM_COL32(0, 0, 0, 200), 24, 5.0f * scale);
        dl->AddCircle(at(x, y), r * scale, color, 24, 3.0f * scale);
        dl->AddCircleFilled(at(x, y), r * 0.35f * scale, color, 12);
    }

    // The game's font: a line of size pixels (on the layout) is the font's ascent plus descent
    float gameFontScale(float size) const {
        const GameFont& f = sFont.data;
        return size / float(std::max(1, f.ascent + f.descent));
    }

    float textWidth(const char* text, float size) const {
        if (sFont.ready) {
            const float k = gameFontScale(size);
            float w = 0;
            for (const char* p = text; *p != '\0'; p++) {
                const unsigned char ch = static_cast<unsigned char>(*p);
                if (ch >= 0x20 && ch < 0x7F && sFont.data.glyphs[ch - 0x20].valid) {
                    w += sFont.data.glyphs[ch - 0x20].advance * k;
                }
            }
            return w;
        }
        return font->CalcTextSizeA(size * scale, FLT_MAX, 0, text).x / scale;
    }

    // One line in the game's font at (x, y) on the layout, its top at y
    void gameText(const char* text, float x, float y, float size, ImU32 color) const {
        const GameFont& f = sFont.data;
        const float k = gameFontScale(size);
        for (const char* p = text; *p != '\0'; p++) {
            const unsigned char ch = static_cast<unsigned char>(*p);
            if (ch < 0x20 || ch >= 0x7F || !f.glyphs[ch - 0x20].valid) {
                continue;
            }
            const FontGlyph& g = f.glyphs[ch - 0x20];
            if (ch != ' ') {
                const float gx = x - g.left * k;
                const ImVec2 uv0(float(g.x) / f.pageWidth, float(g.y) / f.pageHeight);
                const ImVec2 uv1(float(g.x + f.cellWidth) / f.pageWidth, float(g.y + f.cellHeight) / f.pageHeight);
                dl->AddImage(sFont.pages[g.page], at(gx, y), at(gx + f.cellWidth * k, y + f.cellHeight * k), uv0, uv1,
                             color);
            }
            x += g.advance * k;
        }
    }

    // Text with a dark outline, readable over the art; align 0 left, 0.5 centred, 1 right
    void text(const char* text, float x, float y, ImU32 color, float size = 0, float align = 0, bool outline = true) const {
        if (size <= 0) {
            size = fontSize;
        }
        x -= textWidth(text, size) * align;
        if (sFont.ready) {
            if (outline) {
                const ImU32 shadow = IM_COL32(0, 0, 0, 230);
                for (const ImVec2 d : {ImVec2(-1.2f, 0), ImVec2(1.2f, 0), ImVec2(0, -1.2f), ImVec2(0, 1.2f), ImVec2(1.5f, 1.5f)}) {
                    gameText(text, x + d.x, y + d.y, size, shadow);
                }
            }
            gameText(text, x, y, size, color);
            return;
        }
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

// An item's count: the tracker's (the game's and the ones set by hand) once its logic runs, else the game's
int itemCount(const Snapshot& s, const LogicResult& logic, const std::string& name) {
    if (logic.ready) {
        return countOf(logic.items, name);
    }
    return countOf(s.items, name);
}

void drawItems(const Canvas& c, const Snapshot& s, const LogicResult& logic) {
    c.image("ITEMS_TABLE_BACKGROUND", kItemsX, kItemsY);
    c.image("PEARL_HOLDER", kItemsX + 14, kItemsY + 13 + 4 * kItemCell + 2, 100, 100);
    const std::vector<ItemSlot>& slots = itemSlots();
    for (size_t i = 0; i < slots.size(); i++) {
        const ItemSlot& slot = slots[i];
        const int n = itemCount(s, logic, slot.name);
        c.image(itemKey(slot.name, n), slot.x, slot.y, slot.size, slot.size);
        if (n > 1 && (strcmp(slot.name, "Empty Bottle") == 0 || strcmp(slot.name, "Tingle Statue") == 0)) {
            char t[8];
            snprintf(t, sizeof t, "%d", n);
            c.text(t, slot.x + slot.size, slot.y + slot.size - 16, kText, 0, 1);
        }
        if (int(i) == sItem) {
            c.frame(slot.x - 3, slot.y - 3, slot.size + 6, slot.size + 6, kAccent, 3, 4);
        }
    }
}

// The 7x7 sea chart: each island's checks in logic / not checked, the cursor's square framed
void drawSeaChart(const Canvas& c, const Snapshot& s, const LogicResult& logic) {
    const float x0 = kSeaX, y0 = kSeaY, w = kSeaW, h = kSeaH;
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
        if (i == s.where) {
            c.linkMarker(x + cw * 0.5f, y + ch * 0.4f, 13, s.atSea);
        }
        if (i == sCursor && sItem < 0) {
            c.frame(x + 1, y + 1, cw - 2, ch - 2, kAccent, 3, 4);
        }
    }
}

// The dungeons, each with its map, compass, big key and small keys held, and the three other places
void drawExtras(const Canvas& c, const Snapshot& s, const LogicResult& logic) {
    const float x0 = kExtrasX, y0 = kExtrasY;
    static const char* const kNames[kExtras] = {"Dragon Roost Cavern", "Forbidden Woods", "Tower of the Gods",
                                                 "Forsaken Fortress", "Earth Temple", "Wind Temple", "Ganon's Tower",
                                                 "Mailbox", "The Great Sea", "Hyrule"};
    static const char* const kShort[7] = {"DRC", "FW", "TotG", "FF", "ET", "WT", "GT"};
    constexpr float kCell = kExtrasCell;
    c.rect(x0 - 8, y0 - 8, kCell * kExtras + 12, 150, kPanel, 8);
    for (int i = 0; i < kExtras; i++) {
        const float x = x0 + i * kCell;
        const std::string name = kNames[i];
        if (i < 7) {
            const std::string prefix = kShort[i];
            const bool keys = i != 3 && i != 6; // the Forsaken Fortress and Ganon's Tower have none
            if (keys) {
                // found (the tracker's count) once the logic runs, else held now
                const int held = logic.ready ? countOf(logic.items, prefix + " Small Key")
                                             : countOf(s.smallKeysHeld, prefix + " Small Key");
                c.image("SMALL_KEYS/" + std::to_string(std::min(held, 4)), x + 2, y0, 20, 20);
                c.image("BIG_KEYS/" + std::to_string(itemCount(s, logic, prefix + " Big Key") > 0 ? 1 : 0), x + 22, y0, 20, 20);
            }
            if (i != 6) {
                c.image("DUNGEON_MAPS/" + std::to_string(itemCount(s, logic, prefix + " Dungeon Map") > 0 ? 1 : 0), x + 42, y0, 20, 20);
                c.image("COMPASSES/" + std::to_string(itemCount(s, logic, prefix + " Compass") > 0 ? 1 : 0), x + 62, y0, 20, 20);
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
        if (index == s.where) {
            c.linkMarker(x + 70, y0 + 34, 11, true);
        }
        if (index == sCursor && sItem < 0) {
            c.frame(x - 2, y0 - 4, kCell - 4, 132, kAccent, 3, 6);
        }
    }
}

// The list of the area under the cursor
void drawList(const Canvas& c, const LogicResult& logic, float x0, float y0, float w, float h) {
    c.rect(x0, y0, w, h, kPanel, 8);
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
    char t[64];
    snprintf(t, sizeof t, "%d / %d checked", a.checked, int(a.locations.size()));
    c.text(t, x0 + 14, y0 + 40, a.checked == int(a.locations.size()) && a.checked > 0 ? kAccent : kText);
    snprintf(t, sizeof t, "%d in logic", a.available);
    c.text(t, x0 + w - 14, y0 + 40, kAvailable, 0, 1);
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
        if (l.color == LogicColor::CHECKED) {
            // ticked and struck out
            c.tick(x0 + 12, y + 2, 13, kAccent);
            c.text(l.name.c_str(), x0 + 30, y, kChecked);
            const float tw = std::min(c.textWidth(l.name.c_str(), c.fontSize), w - 44);
            c.line(x0 + 29, y + c.fontSize * 0.55f, x0 + 31 + tw, y + c.fontSize * 0.55f, kChecked, 1.5f);
        } else {
            c.rect(x0 + 14, y + 5, 8, 8, colorOf(l.color), 2);
            c.text(l.name.c_str(), x0 + 30, y, colorOf(l.color));
        }
        y += c.fontSize + 7;
    }
}

// The list's place when the cursor is on an item: its name and count
void drawItemInfo(const Canvas& c, const Snapshot& s, const LogicResult& logic, float x0, float y0, float w, float h) {
    c.rect(x0, y0, w, h, kPanel, 8);
    const ItemSlot& slot = itemSlots()[sItem];
    c.text(slot.name, x0 + 14, y0 + 12, kAccent, 22);
    const int n = itemCount(s, logic, slot.name);
    c.image(itemKey(slot.name, n), x0 + (w - 96) / 2, y0 + 56, 96, 96);
    char t[32];
    snprintf(t, sizeof t, n > 0 ? "Have: %d" : "Not yet", n);
    c.text(t, x0 + w / 2, y0 + 164, kText, 20, 0.5f);
    if (logic.ready) {
        c.text("A: one more   X: one less", x0 + w / 2, y0 + 200, kDimText, 0, 0.5f);
        c.text("(the game's count wins when it changes)", x0 + w / 2, y0 + 224, kDimText, 14, 0.5f);
    } else {
        c.text("Set by hand once the logic runs.", x0 + w / 2, y0 + 200, kDimText, 0, 0.5f);
    }
}

void drawScreen() {
    const Snapshot& s = snapshot();
    const LogicResult& logic = logicResult();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // a light shade only: the (paused) game shows through
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 70));
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
    if (s.playing && s.where >= 0 && s.where < int(logic.areas.size())) {
        char t[96];
        snprintf(t, sizeof t, s.where < 49 && !s.atSea ? "Link: last at %s" : "Link: %s",
                 logic.areas[s.where].name.c_str());
        c.linkMarker(184, 36, 8, true);
        c.text(t, 198, 26, kLink, 18);
    }
    if (s.playing && s.keptOnly > 0) {
        // progress kept on disk (pc_tracker.cpp) that this save does not have
        char t[96];
        snprintf(t, sizeof t, "%d checked before a crash or in a later save: kept", s.keptOnly);
        c.text(t, 1240, 48, kDimText, 15, 1);
    }

    drawItems(c, s, logic);
    drawSeaChart(c, s, logic);
    if (sItem >= 0) {
        drawItemInfo(c, s, logic, 918, 66, 322, 520);
    } else {
        drawList(c, logic, 918, 66, 322, 520);
    }
    drawExtras(c, s, logic);

    c.text(sInList    ? "Stick / D-pad: move     A: mark checked / not     B: back"
           : sItem >= 0 ? "Stick / D-pad: move     A: one more     X: one less     B: close"
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
        if (sItem >= 0) {
            if (logic.ready && (pressed & (kConfirm | kLess)) != 0) {
                logicItem(itemSlots()[sItem].name, (pressed & kConfirm) != 0 ? 1 : -1);
            }
            return;
        }
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
        if (snapshot().where >= 0) {
            sCursor = snapshot().where; // the cursor starts where Link is
            sItem = -1;
        }
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
        loadFont();
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
