/*
 * pc_seedgen.h - the in-game seed generator (docs/SEEDGEN.md): the options menu's Randomizer tab,
 * which makes Wind Waker Randomizer seeds on the device and picks the one the game plays.
 *
 * The generator is LagoLunatic's wwrando itself, unchanged, run by a bundled Python through
 * scripts/seedgen/waker2_seedgen.py (the folder scripts/seedgen/bundle.sh builds: seedgen/ next to
 * the executable, or COS_SEEDGEN=<dir>). The tab's rows come from wwrando's own option list
 * ("describe"), so the menu follows the bundled version and the custom player models in
 * seedgen/models/. The choices are kept in <user dir>/rando/seedgen.ini; seeds are written to
 * <user dir>/rando/seeds/. Generating runs where a process can be started (Linux, the Mac); the
 * Switch has no Python, so there the tab only picks among the seeds copied to the SD card.
 *
 * Which seed the game plays: the tab's "Seed to play" row, kept in seedgen.ini (play=). It wins over
 * COS_RANDO_DISC from the environment (the Steam Deck launcher picks an .iso of its folder), since it
 * is the player's later choice; its "Launcher's choice" value leaves COS_RANDO_DISC alone.
 */
#ifndef PC_SEEDGEN_H
#define PC_SEEDGEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* From pc_harness_init, after pc_settings_load_early and before COS_RANDO_DISC is read: applies the
 * "Seed to play" choice to the environment (sets or unsets COS_RANDO_DISC). */
void pc_seedgen_boot(void);

#ifdef __cplusplus
}

#include <cstdint>
#include <string>

/* The options menu's side (pc_menu.cpp): the Randomizer tab's rows, by index. The game thread
 * calls these; the generator's output is read on a thread of its own. */
namespace pc::seedgen {

enum class RowStyle {
    Header, /* a section title: no value, nothing to change */
    Action, /* A does something (generate, restart, import) */
    Value,  /* left/right (and A) change a value */
};

int rowCount();
RowStyle rowStyle(int row);
std::string rowLabel(int row);
std::string rowValue(int row);
std::string rowHelp(int row);
/* A color row's current color as 0xRRGGBB, or -1 for other rows. */
int32_t rowColor(int row);
/* dir 0: A (confirm); -1 / 1: left / right. */
void activate(int row, int dir);
/* The menu shows the tab: reads wwrando's options the first time (in the background). */
void tabShown();
/* A line for the menu's toast when something finished (a seed, an error), then empty. */
std::string takeToast();
/* True once after a seed was made (it is now the seed to play). */
bool takeSeedReady();
/* Restarts the game into the seed to play (Linux); elsewhere, or if that fails, a toast says to. */
void restartIntoSeed();

} // namespace pc::seedgen
#endif

#endif /* PC_SEEDGEN_H */
