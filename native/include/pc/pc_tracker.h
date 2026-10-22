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
 *                            tracker's autotracking (tools/tracker/); a number other than 1 is the port
 * COS_TRACKER_BIND=<ipv4>    the address it listens on: 127.0.0.1 by default, 0.0.0.0 on the Switch
 *                            (the tracker runs on another machine)
 * COS_TRACKER_WEB=<dir>      also serves the files of <dir> (the patched tracker's build,
 *                            scripts/tracker/build_tracker.sh): http://127.0.0.1:6544/ is then the tracker
 *
 * Off by default. The state is built only while something reads it (the server).
 */
#ifndef PC_TRACKER_H
#define PC_TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* After each game frame, on the game thread (pc_frame.cpp): starts the server the first time if asked,
 * and rebuilds the state every 15 frames while it runs. */
void pc_tracker_frame_end(unsigned int frame);

#ifdef __cplusplus
}

#include <string>

namespace pc::tracker {
// The last state as JSON (any thread); before the first build, {"version": 1, "playing": false}.
std::string stateJson();
// pc_tracker_server.cpp: starts the HTTP server on its own thread; false (and a log line) if it cannot.
bool startServer(const char* bindAddress, int port);
} // namespace pc::tracker
#endif

#endif /* PC_TRACKER_H */
