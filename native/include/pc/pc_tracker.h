/*
 * pc_tracker.h - the randomizer tracker's state, read from the game (docs/TRACKER.md).
 *
 * Every half second of play the game thread reads the save (items, the stages' chest, switch and item
 * bits, event bits, charts) and writes what wooferzfg's TWW Randomizer Tracker tracks, in the tracker's
 * own names: item counts ("Progressive Sword": 2), the wwrando locations checked ("Outset Island" /
 * "Underneath Link's House"), the charts owned and salvaged. The location flags come from Archipelago's
 * Wind Waker client (native/tools/gen_tracker_tables.py, pc_tracker_tables.inc).
 *
 * COS_TRACKER_SERVER=1       serves that state as JSON over HTTP (GET /state) on port 6544, for the
 *                            tracker's autotracking (scripts/tracker/); a number other than 1 is the port
 * COS_TRACKER_BIND=<ipv4>    the address it listens on: 127.0.0.1 by default, 0.0.0.0 on the Switch
 *                            (the tracker runs on another machine)
 * COS_TRACKER_WEB=<dir>      also serves the files of <dir> (the patched tracker's build,
 *                            scripts/tracker/build_tracker.sh): http://127.0.0.1:6544/ is then the tracker
 *
 * The overlay: L3+R3 (both sticks pressed together) or F2 shows the tracker over the game, a second press
 * its locations page, a third hides it. Nothing to turn on.
 *
 * The server is off by default. The state is built only while something reads it (the server, the overlay).
 */
#ifndef PC_TRACKER_H
#define PC_TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* After each game frame, on the game thread (pc_frame.cpp): starts the server the first time if asked,
 * and rebuilds the state every 15 frames while it or the overlay runs. */
void pc_tracker_frame_end(unsigned int frame);
/* In each frame's ImGui frame, on the game thread (pc_frame.cpp): the overlay's toggle and window
 * (pc_tracker_overlay.cpp). */
void pc_tracker_overlay_frame(void);

#ifdef __cplusplus
}

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
};
const Snapshot& snapshot();

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
    char color = LogicColor::UNAVAILABLE;
    std::vector<LogicLocation> locations;
};
struct LogicResult {
    bool ready = false;
    std::string error; // why there is no logic (no permalink, no files ...); empty while it starts
    int checked = 0, available = 0, remaining = 0;
    std::vector<LogicArea> areas; // the 49 islands (sea chart order), the 7 dungeons, the 3 others
    int millis = 0;               // how long the last one took
};
// The logic's folder: COS_TRACKER_LOGIC, else tracker-logic/ next to the game (the Switch: next to user/)
std::string logicDir();
// Game thread: work out this state (stateJson's JSON) next; the first call starts the thread.
void logicSubmit(const std::string& stateJson);
// Game thread: mark a check by hand (or unmark it), as a click in the tracker does
void logicToggle(const std::string& generalLocation, const std::string& detailedLocation);
// Game thread: the newest result
const LogicResult& logicResult();
// pc_tracker_server.cpp: the rando disc's permalink from the text next to it; empty if none
std::string seedPermalink();
// pc_tracker.cpp: keeps the PLAY scene paused while the tracker screen is open (call each frame;
// first: the frame it opened)
void holdGame(bool first);
// pc_tracker_overlay.cpp: the overlay is on screen (the state is then built even without the server)
bool overlayShown();
// The last state as JSON (any thread); before the first build, {"version": 1, "playing": false}.
std::string stateJson();
// pc_tracker_server.cpp: starts the HTTP server on its own thread; false (and a log line) if it cannot.
bool startServer(const char* bindAddress, int port);
} // namespace pc::tracker
#endif

#endif /* PC_TRACKER_H */
