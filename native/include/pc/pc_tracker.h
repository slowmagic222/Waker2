/*
 * pc_tracker.h - the randomizer tracker's state, read from the game (docs/TRACKER.md).
 *
 * Every half second of play the game thread reads the save (items, the stages' chest, switch and item
 * bits, event bits, charts) and writes what wooferzfg's TWW Randomizer Tracker tracks, in the tracker's
 * own names: item counts ("Progressive Sword": 2), the wwrando locations checked ("Outset Island" /
 * "Underneath Link's House"), the charts owned and salvaged. The location flags come from Archipelago's
 * Wind Waker client (native/tools/gen_tracker_tables.py, pc_tracker_tables.inc).
 *
 * The tracker screen: L3+R3 (both sticks pressed together) or F2 pauses the game and shows the tracker
 * (items, sea chart, dungeons, which checks are in logic), a second press a small panel over the game,
 * a third hides it. Nothing to turn on. Which checks are in logic: the tracker's own logic, run in the
 * game (pc_tracker_logic.cpp).
 *
 * The state is built only while something reads it (the screen), or with a randomizer disc (for the
 * logic), whose progress is also kept on disk so a crash does not lose it:
 * <user dir>/tracker/<disc file>-<size>-file<N>.txt (COS_TRACKER_SAVE_DIR=<dir> elsewhere). A location or
 * item once seen stays in the state even if the save in memory loses it.
 */
#ifndef PC_TRACKER_H
#define PC_TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* After each game frame, on the game thread (pc_frame.cpp): rebuilds the state every 15 frames on a
 * rando disc or while the tracker screen is shown. */
void pc_tracker_frame_end(unsigned int frame);
/* In each frame's ImGui frame, on the game thread (pc_frame.cpp): the overlay's toggle and window
 * (pc_tracker_overlay.cpp). */
void pc_tracker_overlay_frame(void);
/* The same drawing without the input, for paint A's frame at 60 fps (pc_frame_split): the ImGui of a
 * frame is drawn in the frame it ends, so the tracker drawn only at the frame's end would be in paint B
 * alone and flicker. */
void pc_tracker_overlay_draw(void);

#ifdef __cplusplus
}

#include <cstdint>
#include <string>
#include <vector>

namespace pc::tracker {
struct NamedCount {
    std::string name; // the tracker's name ("Progressive Sword", "DRC Big Key")
    int count;
};
struct Area {
    const char* name; // a general location ("Outset Island"), from the static table
    int checked;
    int total;
};
// The state of the last build, for the overlay (game thread only)
struct Snapshot {
    bool playing = false;
    std::vector<NamedCount> items;
    std::vector<NamedCount> smallKeysHeld;
    std::vector<Area> areas;
    int salvaged = 0; // sunken treasures (charts salvaged), of 49
    int keptOnly = 0; // checks kept on disk that the save in memory does not have (a crash, an older save)
    // Where Link is, as the tracker's areas (LogicResult::areas' order): the sea sector (0-48) he sails in
    // or was last in (on an island's own stages, inside), or a dungeon (49-55); -1 not known yet
    int where = -1;
    bool atSea = false; // where is the sector he is in now (not the last one)
};
const Snapshot& snapshot();

// The game's own font (the message font, rock_24_20_4i_usa.bfn), for the tracker screen: its glyph pages
// decoded to RGBA and the printable ASCII characters' cells. pc_tracker.cpp, game thread; false while the
// game has no message font loaded (then the screen uses ImGui's).
struct FontGlyph {
    bool valid = false;
    int page = 0;           // index into GameFont::pages
    int x = 0, y = 0;       // the cell in its page, pixels
    int left = 0;           // drawn this far left of the pen (the game's kerning)
    int advance = 0;        // the pen moves this far
};
struct GameFont {
    int pageWidth = 0, pageHeight = 0;
    int cellWidth = 0, cellHeight = 0;
    int ascent = 0, descent = 0;
    std::vector<std::vector<uint8_t>> pages; // RGBA: white, the glyph in alpha
    FontGlyph glyphs[95];                    // ' ' to '~'
};
bool gameFont(GameFont& out);

// pc_tracker_logic.cpp: the tracker's logic (which locations are in logic), on its own thread
namespace LogicColor {
enum : char { AVAILABLE = 'a', UNAVAILABLE = 'u', CHECKED = 'c', NON_PROGRESS = 'n' };
}
struct LogicLocation {
    std::string name; // a detailed location ("Underneath Link's House")
    char color;       // LogicColor
};
struct LogicArea {
    std::string name; // a general location: an island or a dungeon
    bool dungeon = false;
    int available = 0; // not checked and in logic
    int remaining = 0; // not checked
    int checked = 0;   // of locations.size()
    char color = LogicColor::UNAVAILABLE;
    std::vector<LogicLocation> locations;
};
struct LogicResult {
    bool ready = false;
    std::string error; // why there is no logic (no permalink, no files ...); empty while it starts
    int checked = 0, available = 0, remaining = 0;
    std::vector<LogicArea> areas; // the 49 islands (sea chart order), the 7 dungeons, the 3 others
    std::vector<NamedCount> items; // the tracker's item counts: the game's, and the ones set by hand
    int millis = 0;               // how long the last one took
};
// The logic's folder: COS_TRACKER_LOGIC, else tracker-logic/ next to the game (the Switch: next to user/)
std::string logicDir();
// Game thread: work out this state (stateJson's JSON) next; the first call starts the thread.
void logicSubmit(const std::string& stateJson);
// Game thread: mark a check by hand (or unmark it), as a click in the tracker does
void logicToggle(const std::string& generalLocation, const std::string& detailedLocation);
// Game thread: an item set by hand, one more (delta > 0) or one less, cycling as the tracker's click does
void logicItem(const std::string& item, int delta);
// Game thread: the newest result
const LogicResult& logicResult();
// pc_tracker_logic.cpp: the rando disc's permalink from the text next to it; empty if none
std::string seedPermalink();
// pc_tracker.cpp: keeps the PLAY scene paused while the tracker screen is open (call each frame;
// first: the frame it opened)
void holdGame(bool first);
// pc_tracker_overlay.cpp: the tracker is on screen (the state is then built even without a rando disc)
bool overlayShown();
} // namespace pc::tracker
#endif

#endif /* PC_TRACKER_H */
