// The options menu's Randomizer+ tab (native/include/pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): makes a new
// world (scripts/rplus/worldgen.py, bundled in seedgen/rplus/ and run by seedgen's Python, under a second),
// picks the world, the randomizer seed and the save files the game plays, and restarts into them. The
// choices are kept in <user dir>/rando/rplus.ini; pc_rplus_boot applies them at the next start.
#include "pc_internal.h"

#include "pc/pc_rplus.h"
#include "pc/pc_seedgen.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if !defined(__SWITCH__)
#include <sys/wait.h>
extern char** environ;
#endif

namespace pc::rplus_menu {

namespace {

const char* T(const char* es, const char* en) {
    return pc_ui_spanish() ? es : en;
}

// ---- files ---------------------------------------------------------------------------------------

std::string iniPath() { return seedgen::userDir() + "/rando/rplus.ini"; }
std::string worldsDir() { return seedgen::userDir() + "/rando/rplus"; }
std::string scriptPath() { return seedgen::genDir() + "/rplus/worldgen.py"; }

bool isFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            mkdir(path.substr(0, i).c_str(), 0755);
        }
    }
}

std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ".../rando/rplus/48213/world.rpw" -> "48213", "rplus/world-seed7.rpw" -> "world-seed7"
std::string worldName(const std::string& path) {
    if (baseName(path) == "world.rpw") {
        const size_t slash = path.rfind('/');
        return slash == std::string::npos || slash == 0 ? path : baseName(path.substr(0, slash));
    }
    std::string name = baseName(path);
    if (name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".rpw") == 0) {
        name.resize(name.size() - 4);
    }
    return name;
}

// "WW Random <seed>.iso" -> "<seed>"
std::string seedOfIso(const std::string& path) {
    std::string name = baseName(path);
    if (name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".iso") == 0) {
        name.resize(name.size() - 4);
    }
    if (name.rfind("WW Random ", 0) == 0) {
        name = name.substr(10);
    }
    return name;
}

std::map<std::string, std::string> readIni(const std::string& path) {
    std::map<std::string, std::string> values;
    FILE* f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        return values;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), f) != nullptr) {
        std::string line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) {
            continue;
        }
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    fclose(f);
    return values;
}

// ---- state ---------------------------------------------------------------------------------------

std::mutex sMu;
bool sLoaded = false;
std::map<std::string, std::string> sValues; // world= ("" launcher's, "none", a world.rpw), saves= ("", "own")
// What this run started with (pc_rplus_boot): a restart is needed when the choices differ.
std::string sBootWorld;
std::string sBootSaves;
std::vector<std::string> sWorlds; // "World to play" choices: "", "none", then the worlds, newest first
bool sWorldsListed = false;
bool sGenerating = false;
std::string sLastWorld;
std::string sGenError;
std::string sToast;
bool sWorldReady = false;

std::string valueOf(const std::string& key) {
    const auto it = sValues.find(key);
    return it != sValues.end() ? it->second : "";
}

void loadLocked() {
    if (!sLoaded) {
        sLoaded = true;
        sValues = readIni(iniPath());
    }
}

void saveLocked() {
    makeDirs(seedgen::userDir() + "/rando");
    const std::string path = iniPath();
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f == nullptr) {
        writef(STDERR_FILENO, "[rplus] cannot write %s: %s\n", tmp.c_str(), strerror(errno));
        return;
    }
    fputs("# The options menu's Randomizer+ tab (docs/RANDOMIZER_PLUS.md). world: the world to play (a world.rpw,\n"
          "# none, or empty for the launcher's COS_RPLUS_WORLD); saves: own (a memory card per seed and world).\n",
          f);
    for (const auto& [k, v] : sValues) {
        fprintf(f, "%s=%s\n", k.c_str(), v.c_str());
    }
    fclose(f);
    rename(tmp.c_str(), path.c_str());
}

void listWorldsLocked() {
    std::vector<std::pair<time_t, std::string>> found;
    if (DIR* d = opendir(worldsDir().c_str()); d != nullptr) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') {
                continue;
            }
            const std::string path = worldsDir() + "/" + e->d_name + "/world.rpw";
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                found.push_back({st.st_mtime, path});
            }
        }
        closedir(d);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    sWorlds = {"", "none"};
    for (const auto& w : found) {
        sWorlds.push_back(w.second);
    }
    const std::string cur = valueOf("world");
    if (!cur.empty() && std::find(sWorlds.begin(), sWorlds.end(), cur) == sWorlds.end()) {
        sWorlds.push_back(cur); // a world that is gone stays a choice until the player picks another
    }
    sWorldsListed = true;
}

bool pendingLocked() {
    return valueOf("world") != sBootWorld || valueOf("saves") != sBootSaves || seedgen::playPending();
}

std::string worldText(const std::string& world) {
    if (world.empty()) {
        const char* launcher = getenv("COS_RPLUS_LAUNCHER_WORLD");
        return launcher != nullptr && launcher[0] != '\0'
                   ? std::string(T("La del lanzador: ", "Launcher's: ")) + worldName(launcher)
                   : std::string(T("La del lanzador (ninguna)", "Launcher's (none)"));
    }
    if (world == "none") {
        return T("Desactivado", "Off");
    }
    return std::string(T("Mundo ", "World ")) + worldName(world) + (isFile(world) ? "" : T(" (no está)", " (missing)"));
}

// ---- making a world --------------------------------------------------------------------------------

std::string unavailable() {
    if (!seedgen::canGenerate()) {
        return T("En la Switch no se pueden crear mundos: cópialos a user/rando/rplus/<nombre>/world.rpw.",
                 "Worlds can't be made on the Switch: copy them to user/rando/rplus/<name>/world.rpw.");
    }
    if (!isFile(scriptPath()) || !isFile(seedgen::pythonPath())) {
        return std::string(T("Falta el generador (", "The generator is missing (")) + scriptPath() + ")";
    }
    return "";
}

void startGenerateLocked() {
#if defined(__SWITCH__)
    sGenError = unavailable();
#else
    if (const std::string why = unavailable(); !why.empty()) {
        sGenError = why;
        sToast = why;
        return;
    }
    std::random_device rd;
    const unsigned int seed = 1 + rd() % 999999;
    const std::string out = worldsDir() + "/" + std::to_string(seed);
    makeDirs(out);
    std::vector<std::string> args = {seedgen::pythonPath(), scriptPath(), "--seed", std::to_string(seed),
                                     "--out", out};
    // Built before fork: only async-signal-safe calls in the child.
    std::vector<std::string> env;
    for (char** e = environ; *e != nullptr; e++) {
        if (strncmp(*e, "WWRANDO_DIR=", 12) != 0) {
            env.push_back(*e);
        }
    }
    env.push_back("WWRANDO_DIR=" + seedgen::genDir() + "/wwrando");
    sGenerating = true;
    sGenError.clear();
    writef(STDERR_FILENO, "[rplus] making world %u in %s\n", seed, out.c_str());
    std::thread([args, env, seed, out]() mutable {
        std::vector<char*> argv, envp;
        for (std::string& a : args) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        for (std::string& e : env) {
            envp.push_back(e.data());
        }
        envp.push_back(nullptr);
        int status = -1;
        const pid_t pid = fork();
        if (pid == 0) {
            dup2(STDERR_FILENO, STDOUT_FILENO); // its report goes to the log
            execve(argv[0], argv.data(), envp.data());
            _exit(127);
        }
        if (pid > 0) {
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
        const std::string world = out + "/world.rpw";
        std::lock_guard<std::mutex> lock(sMu);
        sGenerating = false;
        if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && isFile(world)) {
            sValues["world"] = world;
            saveLocked();
            sLastWorld = std::to_string(seed);
            sWorldsListed = false;
            sWorldReady = true;
            sToast = std::string(T("Mundo listo: ", "World ready: ")) + sLastWorld;
        } else {
            sGenError = T("el generador falló (ver waker2.log)", "the generator failed (see waker2.log)");
            sToast = std::string("Randomizer+: ") + sGenError;
        }
    }).detach();
#endif
}

// ---- rows ----------------------------------------------------------------------------------------

enum class RowType { Header, Generate, World, Seed, Saves, Restart };
struct Row {
    RowType type;
    const char* es;
    const char* en;
};
const Row kRows[] = {
    {RowType::Header, "Mundo", "World"},
    {RowType::Generate, "Crear mundo", "Make a world"},
    {RowType::World, "Mundo a jugar", "World to play"},
    {RowType::Header, "Semilla y partidas", "Seed and saves"},
    {RowType::Seed, "Semilla a jugar", "Seed to play"},
    {RowType::Saves, "Partidas", "Save files"},
    {RowType::Restart, "Reiniciar con esto", "Restart with these"},
};
constexpr int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

const Row* rowAt(int row) {
    return row >= 0 && row < kRowCount ? &kRows[row] : nullptr;
}

} // namespace

int rowCount() {
    std::lock_guard<std::mutex> lock(sMu);
    loadLocked();
    if (!sWorldsListed) {
        listWorldsLocked();
    }
    return kRowCount;
}

RowStyle rowStyle(int row) {
    const Row* r = rowAt(row);
    if (r == nullptr || r->type == RowType::Header) {
        return RowStyle::Header;
    }
    return r->type == RowType::Generate || r->type == RowType::Restart ? RowStyle::Action : RowStyle::Value;
}

std::string rowLabel(int row) {
    const Row* r = rowAt(row);
    return r == nullptr ? "" : T(r->es, r->en);
}

std::string rowValue(int row) {
    const Row* r = rowAt(row);
    if (r == nullptr) {
        return "";
    }
    if (r->type == RowType::Seed) {
        return seedgen::rowValue(seedgen::playRow());
    }
    std::lock_guard<std::mutex> lock(sMu);
    loadLocked();
    switch (r->type) {
    case RowType::Header: return "";
    case RowType::Generate:
        if (sGenerating) {
            return T("Creando...", "Making...");
        }
        if (!sGenError.empty()) {
            return std::string("Error: ") + sGenError;
        }
        if (!sLastWorld.empty()) {
            return std::string(T("Listo: mundo ", "Ready: world ")) + sLastWorld;
        }
        return seedgen::canGenerate() ? T("A: crear", "A: make") : T("no en la Switch", "not on the Switch");
    case RowType::World: {
        const std::string world = valueOf("world");
        return worldText(world) + (world != sBootWorld ? T("  (al reiniciar)", "  (on restart)") : "");
    }
    case RowType::Saves: {
        const std::string saves = valueOf("saves");
        return std::string(saves == "own" ? T("Propias de cada semilla", "Own for each seed")
                                          : T("Compartidas", "Shared")) +
               (saves != sBootSaves ? T("  (al reiniciar)", "  (on restart)") : "");
    }
    case RowType::Restart:
        return pendingLocked() ? T("A: reiniciar (hay cambios)", "A: restart (changes waiting)")
                               : T("A: reiniciar", "A: restart");
    case RowType::Seed: break;
    }
    return "";
}

std::string rowHelp(int row) {
    const Row* r = rowAt(row);
    if (r == nullptr) {
        return "";
    }
    switch (r->type) {
    case RowType::Header: return "";
    case RowType::Generate: {
        std::lock_guard<std::mutex> lock(sMu);
        if (const std::string why = unavailable(); !why.empty()) {
            return why;
        }
        return T("Crea un mundo nuevo (islas, pueblos y dónde va cada cosa) en user/rando/rplus/ y lo pone como "
                 "mundo a jugar; en la pantalla de título el juego se reinicia en él. Por ahora solo es el terreno.",
                 "Makes a new world (islands, villages and where everything goes) in user/rando/rplus/ and makes it "
                 "the world to play; at the title screen the game restarts into it. For now it is only the land.");
    }
    case RowType::World:
        return T("Izquierda/derecha: el mundo que carga el juego (se guarda). Desactivado: el mar de siempre. "
                 "Se aplica al reiniciar.",
                 "Left/right: the world the game loads (kept). Off: the usual Great Sea. Applies on restart.");
    case RowType::Seed: return seedgen::rowHelp(seedgen::playRow());
    case RowType::Saves:
        return T("Compartidas: una sola tarjeta de memoria para todo. Propias: cada semilla (y cada mundo) tiene "
                 "su tarjeta con sus 3 partidas, en user/saves/. Se aplica al reiniciar.",
                 "Shared: one memory card for everything. Own: each seed (and each world) gets its own card with "
                 "its 3 files, in user/saves/. Applies on restart.");
    case RowType::Restart:
        return T("Reinicia el juego con el mundo, la semilla y las partidas de arriba. Guarda antes: lo no "
                 "guardado se pierde.",
                 "Restarts the game with the world, seed and save files above. Save first: anything unsaved is "
                 "lost.");
    }
    return "";
}

void activate(int row, int dir) {
    const Row* r = rowAt(row);
    if (r == nullptr) {
        return;
    }
    if (r->type == RowType::Seed) {
        seedgen::activate(seedgen::playRow(), dir);
        return;
    }
    if (r->type == RowType::Restart) {
        if (dir == 0) {
            seedgen::restartIntoSeed();
        }
        return;
    }
    std::lock_guard<std::mutex> lock(sMu);
    loadLocked();
    switch (r->type) {
    case RowType::Generate:
        if (dir == 0 && !sGenerating) {
            startGenerateLocked();
        }
        break;
    case RowType::World: {
        if (!sWorldsListed) {
            listWorldsLocked();
        }
        const std::string cur = valueOf("world");
        const int n = (int)sWorlds.size();
        int at = (int)(std::find(sWorlds.begin(), sWorlds.end(), cur) - sWorlds.begin());
        at = at < n ? at : 0;
        sValues["world"] = sWorlds[((at + (dir < 0 ? -1 : 1)) % n + n) % n];
        if (sValues["world"].empty()) {
            sValues.erase("world");
        }
        saveLocked();
        break;
    }
    case RowType::Saves:
        if (valueOf("saves") == "own") {
            sValues.erase("saves");
        } else {
            sValues["saves"] = "own";
        }
        saveLocked();
        break;
    default: break;
    }
}

std::string takeToast() {
    std::lock_guard<std::mutex> lock(sMu);
    std::string t;
    t.swap(sToast);
    return t;
}

bool takeWorldReady() {
    std::lock_guard<std::mutex> lock(sMu);
    const bool ready = sWorldReady;
    sWorldReady = false;
    return ready;
}

} // namespace pc::rplus_menu

extern "C" void pc_rplus_boot(void) {
    using namespace pc::rplus_menu;
    // What the launcher (or settings.ini's [dev]) gave, kept across a restart from the menu.
    const char* launcher = getenv("COS_RPLUS_LAUNCHER_WORLD");
    if (launcher == nullptr) {
        const char* cur = getenv("COS_RPLUS_WORLD");
        setenv("COS_RPLUS_LAUNCHER_WORLD", cur != nullptr ? cur : "", 1);
        launcher = getenv("COS_RPLUS_LAUNCHER_WORLD");
    }
    const std::map<std::string, std::string> values = readIni(iniPath());
    const auto get = [&](const char* key) {
        const auto it = values.find(key);
        return it != values.end() ? it->second : std::string();
    };
    sBootWorld = get("world");
    sBootSaves = get("saves");
    std::string world = launcher;
    if (sBootWorld == "none") {
        world.clear();
    } else if (!sBootWorld.empty()) {
        if (isFile(sBootWorld)) {
            world = sBootWorld;
        } else {
            pc::writef(STDERR_FILENO, "[rplus] the world to play (%s) is gone; the launcher's choice stays\n",
                       sBootWorld.c_str());
        }
    }
    if (world.empty()) {
        unsetenv("COS_RPLUS_WORLD");
    } else {
        setenv("COS_RPLUS_WORLD", world.c_str(), 1);
    }

    // Save files of their own: a memory card folder per randomizer seed and world.
    unsetenv("COS_SAVE_DIR");
    if (sBootSaves == "own") {
        const char* disc = getenv("COS_RANDO_DISC");
        std::string key = disc != nullptr && disc[0] != '\0' ? seedOfIso(disc) : "original";
        if (!world.empty()) {
            key += "+" + worldName(world);
        }
        for (char& c : key) {
            if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 0x20) {
                c = '_';
            }
        }
        const std::string dir = pc::seedgen::userDir() + "/saves/" + key;
        makeDirs(dir);
        setenv("COS_SAVE_DIR", dir.c_str(), 1);
        pc::writef(STDERR_FILENO, "[rplus] save files of their own: %s\n", dir.c_str());
    }
}
