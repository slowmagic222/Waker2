// The in-game seed generator (native/include/pc/pc_seedgen.h, docs/SEEDGEN.md): the options menu's
// Randomizer tab. wwrando runs as a child process (scripts/seedgen/waker2_seedgen.py, bundled
// Python); its standard output is one tab-separated record per line, read on a thread of its own.
// One child at a time: reading the options (describe), making a seed (generate) or reading a
// permalink. The rows are rebuilt only when the options or a value change (sRowsVersion).
#include "pc_internal.h"

#include "pc/pc_seedgen.h"
#include "pc/pc_settings.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <signal.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if !defined(__SWITCH__)
#include <SDL3/SDL_clipboard.h>
#include <sys/wait.h>
#endif
#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace pc::seedgen {

namespace {

#if defined(__SWITCH__)
constexpr bool kCanGenerate = false; // no Python on the console: seeds are made elsewhere
#else
constexpr bool kCanGenerate = true;
#endif

const char* T(const char* es, const char* en) {
    return pc_ui_spanish() ? es : en;
}

// ---- paths ----------------------------------------------------------------------------------------

// <user dir>: the folder of the options menu's settings.ini.
std::string userDir() {
    const std::string p = pc_settings_path();
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}
std::string randoDir() { return userDir() + "/rando"; }
std::string iniPath() { return randoDir() + "/seedgen.ini"; }
std::string seedsDir() { return randoDir() + "/seeds"; }

// COS_SEEDGEN, else seedgen/ next to the executable.
std::string genDir() {
    if (const char* d = getenv("COS_SEEDGEN"); d != nullptr && d[0] != '\0') {
        return d;
    }
    char exe[PATH_MAX] = {};
    char real[PATH_MAX];
    std::string dir = ".";
    if (executablePath(exe, sizeof(exe))) {
        const char* path = realpath(exe, real) != nullptr ? real : exe;
        if (const char* slash = strrchr(path, '/'); slash != nullptr) {
            dir.assign(path, (size_t)(slash - path));
        }
    }
    return dir + "/seedgen";
}
// COS_SEEDGEN_PYTHON (a Python 3.12 with wwrando's requirements), else the bundled one.
std::string pythonPath() {
    if (const char* p = getenv("COS_SEEDGEN_PYTHON"); p != nullptr && p[0] != '\0') {
        return p;
    }
    return genDir() + "/python/bin/python3";
}

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

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t at = line.find(sep, start);
        out.push_back(line.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            return out;
        }
        start = at + 1;
    }
}

int parseRgb(const std::string& hex) {
    if (hex.size() != 6) {
        return -1;
    }
    char* end = nullptr;
    const long v = strtol(hex.c_str(), &end, 16);
    return end != nullptr && *end == '\0' ? (int)v : -1;
}

std::string rgbHex(int rgb) {
    char s[8];
    snprintf(s, sizeof(s), "%06X", rgb & 0xFFFFFF);
    return s;
}

// ---- seedgen.ini ----------------------------------------------------------------------------------

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

// ---- wwrando's options (describe) -----------------------------------------------------------------

struct Choice {
    std::string value;
    std::string help;
};

struct Option {
    std::string name, kind, def, label, help, section;
    int lo = 0, hi = 0;
    std::vector<Choice> choices; // kind "enum"
};

struct Gear {
    std::string item;
    int max = 1, def = 0;
};

using Colors = std::vector<std::pair<std::string, int>>; // name, 0xRRGGBB

struct Model {
    std::string name;
    bool casual = true;  // casual clothes allowed
    Colors colors[2];    // hero, casual
    std::vector<std::pair<std::string, Colors>> presets[2];
};

struct Schema {
    std::string version;
    std::vector<Option> options;
    std::vector<Gear> gear;
    std::vector<Model> models;
};

enum class SchemaState { NotLoaded, Loading, Ready, Failed };

// ---- rows -----------------------------------------------------------------------------------------

enum class RowType {
    Header,
    Generate,
    Play,
    SeedName,
    Permalink,
    Defaults,
    Option,
    Color,
    RandomColors,
    DefaultColors,
    Gear,
};

struct Row {
    RowType type;
    int index = -1;    // Option: into options; Gear: into gear; Color: into the model's colors
    std::string text;  // Header: the title
};

// ---- state (sMu) ----------------------------------------------------------------------------------

std::mutex sMu;
bool sLoaded = false;
std::map<std::string, std::string> sValues; // seedgen.ini
Schema sSchema;
SchemaState sSchemaState = SchemaState::NotLoaded;
std::string sSchemaError;
std::atomic<bool> sBusy{false};             // a child runs
std::atomic<int> sChild{-1};                // its pid
bool sGenerating = false;
int sPercent = 0;
std::string sProgressText;
std::string sGenError;
std::string sLastSeed, sLastPermalink, sLastHash, sLastIso;
bool sSeedReady = false;
std::string sToast;
unsigned int sVersion = 1; // bumped when the rows may change
std::vector<Row> sRows;
unsigned int sRowsVersion = 0;
std::vector<std::string> sPlayChoices; // "", "none", then the seeds' paths
unsigned int sPlayVersion = 0;

void loadLocked() {
    if (!sLoaded) {
        sLoaded = true;
        sValues = readIni(iniPath());
    }
}

void saveLocked() {
    makeDirs(randoDir());
    const std::string path = iniPath();
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f == nullptr) {
        writef(STDERR_FILENO, "[cos] seedgen: cannot write %s: %s\n", tmp.c_str(), strerror(errno));
        return;
    }
    fputs("# The options menu's Randomizer tab (docs/SEEDGEN.md). NAME=value: a wwrando option,\n"
          "# seed, gear.<item> (how many to start with), color.<color> (RRGGBB), play (the seed to play).\n",
          f);
    for (const auto& [k, v] : sValues) {
        fprintf(f, "%s=%s\n", k.c_str(), v.c_str());
    }
    fclose(f);
    rename(tmp.c_str(), path.c_str());
    sVersion++;
}

const Option* findOption(const std::string& name) {
    for (const Option& o : sSchema.options) {
        if (o.name == name) {
            return &o;
        }
    }
    return nullptr;
}

std::string valueOf(const std::string& name) {
    if (auto it = sValues.find(name); it != sValues.end()) {
        return it->second;
    }
    const Option* o = findOption(name);
    return o != nullptr ? o->def : "";
}

const Model* currentModel() {
    const std::string name = valueOf("custom_player_model");
    for (const Model& m : sSchema.models) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

// 0 hero, 1 casual (wwrando's prefix): casual clothes on and the model has them.
int clothes() {
    const Model* m = currentModel();
    return m != nullptr && m->casual && valueOf("player_in_casual_clothes") == "1" ? 1 : 0;
}

const Colors& currentColors() {
    static const Colors kNone;
    const Model* m = currentModel();
    return m != nullptr ? m->colors[clothes()] : kNone;
}

void clearColors() {
    for (auto it = sValues.begin(); it != sValues.end();) {
        it = it->first.rfind("color.", 0) == 0 ? sValues.erase(it) : std::next(it);
    }
}

void buildPlayChoices() {
    std::vector<std::pair<time_t, std::string>> isos;
    if (DIR* d = opendir(seedsDir().c_str()); d != nullptr) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() < 5 || strcasecmp(name.c_str() + name.size() - 4, ".iso") != 0) {
                continue;
            }
            const std::string path = seedsDir() + "/" + name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                isos.push_back({st.st_mtime, path});
            }
        }
        closedir(d);
    }
    std::sort(isos.begin(), isos.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    sPlayChoices = {"", "none"};
    const std::string play = valueOf("play");
    if (!play.empty() && play != "none" &&
        std::none_of(isos.begin(), isos.end(), [&](const auto& i) { return i.second == play; })) {
        sPlayChoices.push_back(play); // chosen elsewhere (or gone): still shown
    }
    for (const auto& i : isos) {
        sPlayChoices.push_back(i.second);
    }
    sPlayVersion = sVersion;
}

void buildRowsLocked() {
    if (sRowsVersion == sVersion && !sRows.empty()) {
        return;
    }
    sRowsVersion = sVersion;
    std::vector<Row>& r = sRows;
    r.clear();
    r.push_back({RowType::Header, -1, T("Semilla", "Seed")});
    r.push_back({RowType::Generate});
    r.push_back({RowType::Play});
    if (kCanGenerate) {
        r.push_back({RowType::SeedName});
        r.push_back({RowType::Permalink});
        r.push_back({RowType::Defaults});
    }
    if (sSchemaState == SchemaState::Loading) {
        r.push_back({RowType::Header, -1, T("Leyendo las opciones del randomizer...", "Reading the randomizer's options...")});
    } else if (sSchemaState == SchemaState::Failed) {
        r.push_back({RowType::Header, -1, sSchemaError});
    }
    if (sSchemaState != SchemaState::Ready) {
        return;
    }
    std::string section;
    auto pushGear = [&]() {
        for (int i = 0; i < (int)sSchema.gear.size(); i++) {
            r.push_back({RowType::Gear, i});
        }
    };
    for (int i = 0; i < (int)sSchema.options.size(); i++) {
        const Option& o = sSchema.options[i];
        if (o.section != section) {
            if (section == "Starting items") {
                pushGear();
            }
            section = o.section;
            r.push_back({RowType::Header, -1, section});
        }
        r.push_back({RowType::Option, i});
        if (o.kind == "preset") {
            const Colors& colors = currentColors();
            for (int c = 0; c < (int)colors.size(); c++) {
                r.push_back({RowType::Color, c});
            }
            if (!colors.empty()) {
                r.push_back({RowType::RandomColors});
                r.push_back({RowType::DefaultColors});
            }
        }
    }
    if (section == "Starting items") {
        pushGear();
    }
}

// ---- the child process ----------------------------------------------------------------------------

// Runs `args` (args[0] the program) with its standard output read line by line into onLine, on a
// thread of its own; `onExit` gets the exit status (-1 if it could not start). False if a child
// runs already or this host cannot start one.
bool startChild(const std::vector<std::string>& args, std::function<void(const std::vector<std::string>&)> onLine,
                std::function<void(int)> onExit) {
#if defined(__SWITCH__)
    (void)args;
    (void)onLine;
    (void)onExit;
    return false;
#else
    bool expected = false;
    if (!sBusy.compare_exchange_strong(expected, true)) {
        return false;
    }
    std::string cmd;
    for (const std::string& a : args) {
        cmd += (cmd.empty() ? "" : " ") + a;
    }
    writef(STDERR_FILENO, "[cos] seedgen: run %s\n", cmd.c_str());
    std::thread([args, onLine, onExit]() {
        int fds[2];
        if (pipe(fds) != 0) {
            onExit(-1);
            sBusy = false;
            return;
        }
        std::vector<char*> argv;
        for (const std::string& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        const pid_t pid = fork();
        if (pid == 0) {
            // The child: only async-signal-safe calls until exec.
#if defined(__linux__)
            prctl(PR_SET_PDEATHSIG, SIGTERM); // the game quits: the generator stops too
#endif
            dup2(fds[1], STDOUT_FILENO);
            for (int fd = 3; fd < 4096; fd++) {
                close(fd); // the game's sockets (the tracker's port) and files stay the game's
            }
            execv(argv[0], argv.data());
            _exit(127);
        }
        close(fds[1]);
        if (pid < 0) {
            close(fds[0]);
            onExit(-1);
            sBusy = false;
            return;
        }
        sChild = pid;
        FILE* f = fdopen(fds[0], "r");
        std::string line;
        char buf[4096];
        while (f != nullptr && fgets(buf, sizeof(buf), f) != nullptr) {
            line += buf;
            if (line.empty() || line.back() != '\n') {
                continue; // a longer line: the rest is in the next read
            }
            line.pop_back();
            onLine(split(line, '\t'));
            line.clear();
        }
        if (f != nullptr) {
            fclose(f);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        sChild = -1;
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        writef(STDERR_FILENO, "[cos] seedgen: %s exited %d\n", args.size() > 2 ? args[2].c_str() : "child", code);
        onExit(code);
        sBusy = false;
    }).detach();
    return true;
#endif
}

std::vector<std::string> baseArgs(const char* command) {
    const std::string dir = genDir();
    return {pythonPath(), dir + "/waker2_seedgen.py", command, "--wwrando", dir + "/wwrando", "--models",
            dir + "/models"};
}

// Why this host cannot generate, or empty.
std::string unavailable() {
    if (!kCanGenerate) {
        return T("En la Switch no se pueden generar semillas: créalas en un PC o la Steam Deck y copia el .iso a "
                 "user/rando/seeds/.",
                 "Seeds cannot be made on the Switch: make them on a PC or the Steam Deck and copy the .iso to "
                 "user/rando/seeds/.");
    }
    if (!isFile(genDir() + "/waker2_seedgen.py") || !isFile(pythonPath())) {
        return std::string(T("Falta el generador (", "The generator is missing (")) + genDir() +
               T("/): esta compilación no lo incluye (scripts/seedgen/bundle.sh).",
                 "/): this build does not include it (scripts/seedgen/bundle.sh).");
    }
    return "";
}

void startDescribeLocked() {
    const std::string why = unavailable();
    if (!why.empty()) {
        sSchemaState = SchemaState::Failed;
        sSchemaError = why;
        sVersion++;
        return;
    }
    auto schema = std::make_shared<Schema>();
    auto error = std::make_shared<std::string>();
    const bool started = startChild(
        baseArgs("describe"),
        [schema, error](const std::vector<std::string>& f) {
            const std::string& k = f[0];
            if (k == "version" && f.size() >= 2) {
                schema->version = f[1];
            } else if (k == "section" && f.size() >= 2) {
                schema->options.push_back({"", "section", "", "", "", f[1]});
            } else if (k == "option" && f.size() >= 8) {
                Option o;
                o.name = f[1];
                o.kind = f[2];
                o.def = f[3];
                o.lo = atoi(f[4].c_str());
                o.hi = atoi(f[5].c_str());
                o.label = f[6];
                o.help = f[7];
                // The section of the "section" record before it.
                if (!schema->options.empty()) {
                    o.section = schema->options.back().section;
                }
                if (!schema->options.empty() && schema->options.back().kind == "section") {
                    schema->options.pop_back();
                }
                schema->options.push_back(o);
            } else if (k == "choice" && f.size() >= 4 && !schema->options.empty() &&
                       schema->options.back().name == f[1]) {
                schema->options.back().choices.push_back({f[2], f[3]});
            } else if (k == "gear" && f.size() >= 4) {
                schema->gear.push_back({f[1], atoi(f[2].c_str()), atoi(f[3].c_str())});
            } else if (k == "model" && f.size() >= 3) {
                Model m;
                m.name = f[1];
                m.casual = f[2] == "1";
                schema->models.push_back(m);
            } else if ((k == "color" || k == "preset") && f.size() >= 5) {
                for (Model& m : schema->models) {
                    if (m.name != f[1]) {
                        continue;
                    }
                    const int c = f[2] == "casual" ? 1 : 0;
                    if (k == "color") {
                        m.colors[c].push_back({f[3], parseRgb(f[4])});
                    } else {
                        Colors colors;
                        for (const std::string& kv : split(f[4], ',')) {
                            const size_t eq = kv.find('=');
                            if (eq != std::string::npos) {
                                colors.push_back({kv.substr(0, eq), parseRgb(kv.substr(eq + 1))});
                            }
                        }
                        m.presets[c].push_back({f[3], colors});
                    }
                }
            } else if (k == "error" && f.size() >= 2) {
                *error = f[1];
            }
        },
        [schema, error](int code) {
            std::lock_guard<std::mutex> lock(sMu);
            // A "section" record with no option after it.
            if (!schema->options.empty() && schema->options.back().kind == "section") {
                schema->options.pop_back();
            }
            if (code == 0 && !schema->options.empty()) {
                sSchema = *schema;
                sSchemaState = SchemaState::Ready;
                writef(STDERR_FILENO, "[cos] seedgen: wwrando %s, %zu options, %zu models\n", sSchema.version.c_str(),
                       sSchema.options.size(), sSchema.models.size());
            } else {
                sSchemaState = SchemaState::Failed;
                sSchemaError = std::string(T("No se pudieron leer las opciones: ", "Could not read the options: ")) +
                               (error->empty() ? "exit " + std::to_string(code) : *error);
            }
            sVersion++;
        });
    if (started) {
        sSchemaState = SchemaState::Loading;
        sVersion++;
    }
}

void startGenerateLocked() {
    if (const std::string why = unavailable(); !why.empty()) {
        sToast = why;
        return;
    }
    if (gConfig.disc == nullptr || !isFile(gConfig.disc)) {
        sToast = T("Falta el disco original (COS_DISC): el randomizer parte de él.",
                   "The clean disc (COS_DISC) is missing: the randomizer starts from it.");
        return;
    }
    makeDirs(seedsDir());
    saveLocked();
    std::vector<std::string> args = baseArgs("generate");
    args.insert(args.end(), {"--settings", iniPath(), "--clean-iso", gConfig.disc, "--output", seedsDir()});
    if (const char* dry = getenv("COS_SEEDGEN_DRY"); dry != nullptr && strcmp(dry, "1") == 0) {
        args.push_back("--dry"); // a test: the logs only, no disc read or written
    }
    auto error = std::make_shared<std::string>();
    const bool started = startChild(
        args,
        [error](const std::vector<std::string>& f) {
            std::lock_guard<std::mutex> lock(sMu);
            if (f[0] == "progress" && f.size() >= 3) {
                sPercent = atoi(f[1].c_str());
                sProgressText = f[2];
            } else if (f[0] == "done" && f.size() >= 5) {
                sLastIso = f[1];
                sLastPermalink = f[2];
                sLastSeed = f[3];
                sLastHash = f[4];
            } else if (f[0] == "error" && f.size() >= 2) {
                *error = f[1];
            }
        },
        [error](int code) {
            std::lock_guard<std::mutex> lock(sMu);
            sGenerating = false;
            if (code == 0 && !sLastIso.empty()) {
                sValues["play"] = sLastIso;
                saveLocked();
                sSeedReady = true;
                writef(STDERR_FILENO, "[cos] seedgen: seed %s (%s) -> %s, permalink %s\n", sLastSeed.c_str(),
                       sLastHash.c_str(), sLastIso.c_str(), sLastPermalink.c_str());
                // The permalink next to the seed, and on the clipboard, to share it.
                const std::string txt = sLastIso.substr(0, sLastIso.size() - 4) + " - Permalink.txt";
                if (FILE* p = fopen(txt.c_str(), "w"); p != nullptr) {
                    fprintf(p, "%s\n", sLastPermalink.c_str());
                    fclose(p);
                }
#if !defined(__SWITCH__)
                SDL_SetClipboardText(sLastPermalink.c_str());
#endif
            } else {
                sGenError = !error->empty() ? *error
                            : code == 128 + SIGTERM ? T("cancelado", "cancelled")
                                                    : "exit " + std::to_string(code);
                sToast = std::string(T("Error al generar: ", "Generation failed: ")) + sGenError;
            }
            sVersion++;
        });
    if (started) {
        sGenerating = true;
        sPercent = 0;
        sProgressText = T("Empezando...", "Starting...");
        sGenError.clear();
        sLastIso.clear();
        sVersion++;
    } else {
        sToast = T("El generador está ocupado, espera un momento.", "The generator is busy, wait a moment.");
    }
}

void importPermalinkLocked() {
#if defined(__SWITCH__)
    sToast = unavailable();
#else
    if (const std::string why = unavailable(); !why.empty()) {
        sToast = why;
        return;
    }
    char* clip = SDL_GetClipboardText();
    std::string text = clip != nullptr ? clip : "";
    SDL_free(clip);
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }),
               text.end());
    if (text.empty()) {
        sToast = T("El portapapeles está vacío: copia antes un permalink.", "The clipboard is empty: copy a permalink first.");
        return;
    }
    std::vector<std::string> args = baseArgs("permalink");
    args.insert(args.end(), {"--settings", iniPath(), "--permalink", text});
    auto sets = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
    auto error = std::make_shared<std::string>();
    if (!startChild(
            args,
            [sets, error](const std::vector<std::string>& f) {
                if (f[0] == "set" && f.size() >= 3) {
                    sets->push_back({f[1], f[2]});
                } else if (f[0] == "error" && f.size() >= 2) {
                    *error = f[1];
                }
            },
            [sets, error](int code) {
                std::lock_guard<std::mutex> lock(sMu);
                if (code != 0 || sets->empty()) {
                    sToast = std::string(T("Permalink no válido: ", "Invalid permalink: ")) +
                             (error->empty() ? "exit " + std::to_string(code) : *error);
                    return;
                }
                for (const auto& [k, v] : *sets) {
                    sValues[k] = v;
                }
                saveLocked();
                sToast = std::string(T("Permalink importado: semilla ", "Permalink imported: seed ")) + sValues["seed"];
            })) {
        sToast = T("El generador está ocupado, espera un momento.", "The generator is busy, wait a moment.");
    }
#endif
}

const int kPalette[] = {0xFFFFFF, 0xC0C0C0, 0x606060, 0x000000, 0xE52B36, 0xFF7F00, 0xFFD700, 0xFFEF10,
                        0x9CF543, 0x5AB24A, 0x1E7B34, 0x40E0D0, 0x4A75AD, 0x1E3FBF, 0x8A2BE2, 0xFF69B4,
                        0x8B4513, 0xF7DB9C};

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

// Cycles `cur` through `values` by dir (0 counts as 1).
template <class V>
int cycleIndex(const std::vector<V>& values, int cur, int dir) {
    const int n = (int)values.size();
    if (n == 0) {
        return -1;
    }
    return cur < 0 ? 0 : ((cur + (dir < 0 ? -1 : 1)) % n + n) % n;
}

void setOptionLocked(const Option& o, int dir) {
    const std::string cur = valueOf(o.name);
    std::string next = cur;
    if (o.kind == "bool") {
        next = cur == "1" ? "0" : "1";
    } else if (o.kind == "int") {
        int v = atoi(cur.c_str()) + (dir < 0 ? -1 : 1);
        v = v > o.hi ? o.lo : v < o.lo ? o.hi : v;
        next = std::to_string(v);
    } else if (o.kind == "enum") {
        std::vector<std::string> values;
        for (const Choice& c : o.choices) {
            values.push_back(c.value);
        }
        const int at = (int)(std::find(values.begin(), values.end(), cur) - values.begin());
        const int i = cycleIndex(values, at < (int)values.size() ? at : -1, dir);
        next = i >= 0 ? values[i] : cur;
    } else if (o.kind == "model") {
        std::vector<std::string> names;
        for (const Model& m : sSchema.models) {
            names.push_back(m.name);
        }
        const int at = (int)(std::find(names.begin(), names.end(), cur) - names.begin());
        const int i = cycleIndex(names, at < (int)names.size() ? at : -1, dir);
        next = i >= 0 ? names[i] : cur;
        // Another model has other colors and presets (wwrando's window resets them too).
        clearColors();
        sValues.erase("custom_color_preset");
    } else if (o.kind == "preset") {
        const Model* m = currentModel();
        std::vector<std::string> names = {"Default"};
        if (m != nullptr) {
            for (const auto& p : m->presets[clothes()]) {
                names.push_back(p.first);
            }
        }
        const int at = (int)(std::find(names.begin(), names.end(), cur) - names.begin());
        const int i = cycleIndex(names, at < (int)names.size() ? at : -1, dir);
        next = names[i];
        clearColors();
        if (m != nullptr && i > 0) {
            for (const auto& [name, rgb] : m->presets[clothes()][i - 1].second) {
                sValues["color." + name] = rgbHex(rgb);
            }
        }
    }
    if (o.name == "player_in_casual_clothes") {
        clearColors(); // the casual clothes have colors of their own
        sValues.erase("custom_color_preset");
    }
    sValues[o.name] = next;
    writef(STDERR_FILENO, "[cos] seedgen: %s -> %s\n", o.name.c_str(), next.c_str());
    saveLocked();
}

std::string optionValueText(const Option& o) {
    const std::string v = valueOf(o.name);
    if (o.kind == "bool") {
        return v == "1" ? T("Activado", "On") : T("Desactivado", "Off");
    }
    if (o.kind == "preset" && v == "Default") {
        return T("Por defecto", "Default");
    }
    return v;
}

std::string playText(const std::string& play) {
    if (play.empty()) {
        return T("La del lanzador", "Launcher's choice");
    }
    if (play == "none") {
        return T("Juego original", "Original game");
    }
    return seedOfIso(play) + (isFile(play) ? "" : T(" (no está)", " (missing)"));
}

// The disc this run plays (COS_RANDO_DISC as pc_seedgen_boot left it).
std::string runningPlay() {
    return gConfig.randoDisc != nullptr ? gConfig.randoDisc : "";
}

// Linux: the game again, from the start, with the same command line (pc_seedgen_boot then applies
// the "Seed to play" choice). Returns only if that failed.
void restartGame() {
#if defined(__linux__)
    std::vector<std::string> args;
    if (FILE* f = fopen("/proc/self/cmdline", "rb"); f != nullptr) {
        std::string arg;
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c == '\0') {
                args.push_back(arg);
                arg.clear();
            } else {
                arg += (char)c;
            }
        }
        fclose(f);
    }
    if (args.empty()) {
        args.push_back("switchwaker");
    }
    std::vector<char*> argv;
    for (std::string& a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    writef(STDERR_FILENO, "[cos] seedgen: restarting to play %s\n", playText(valueOf("play")).c_str());
    // Every file but stdin/stdout/stderr closes at exec (the tracker's port must be free again).
    if (DIR* d = opendir("/proc/self/fd"); d != nullptr) {
        while (dirent* e = readdir(d)) {
            const int fd = atoi(e->d_name);
            if (fd > 2) {
                fcntl(fd, F_SETFD, FD_CLOEXEC);
            }
        }
        closedir(d);
    }
    execv("/proc/self/exe", argv.data());
    writef(STDERR_FILENO, "[cos] seedgen: restart failed: %s\n", strerror(errno));
#endif
    sToast = T("Reinicia el juego para jugar esa semilla.", "Restart the game to play that seed.");
}

} // namespace

// ---- the menu's side ------------------------------------------------------------------------------

void tabShown() {
    std::lock_guard<std::mutex> lock(sMu);
    loadLocked();
    if (sSchemaState == SchemaState::NotLoaded && !sBusy) {
        startDescribeLocked();
    }
    if (sPlayVersion == 0) {
        buildPlayChoices();
    }
}

int rowCount() {
    std::lock_guard<std::mutex> lock(sMu);
    loadLocked();
    buildRowsLocked();
    return (int)sRows.size();
}

RowStyle rowStyle(int row) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size()) {
        return RowStyle::Header;
    }
    switch (sRows[row].type) {
    case RowType::Header: return RowStyle::Header;
    case RowType::Generate:
    case RowType::Permalink:
    case RowType::Defaults:
    case RowType::RandomColors:
    case RowType::DefaultColors: return RowStyle::Action;
    default: return RowStyle::Value;
    }
}

std::string rowLabel(int row) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size()) {
        return "";
    }
    const Row& r = sRows[row];
    switch (r.type) {
    case RowType::Header: return r.text;
    case RowType::Generate: return T("Generar semilla", "Generate seed");
    case RowType::Play: return T("Semilla a jugar", "Seed to play");
    case RowType::SeedName: return T("Nombre de la semilla", "Seed name");
    case RowType::Permalink: return T("Importar permalink (portapapeles)", "Import permalink (clipboard)");
    case RowType::Defaults: return T("Opciones por defecto de wwrando", "wwrando's default options");
    case RowType::Option: return sSchema.options[r.index].label;
    case RowType::Color: return std::string("  ") + T("Color: ", "Color: ") + currentColors()[r.index].first;
    case RowType::RandomColors: return std::string("  ") + T("Colores al azar", "Random colors");
    case RowType::DefaultColors: return std::string("  ") + T("Colores por defecto", "Default colors");
    case RowType::Gear: return std::string(T("Empezar con: ", "Start with: ")) + sSchema.gear[r.index].item;
    }
    return "";
}

std::string rowValue(int row) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size()) {
        return "";
    }
    const Row& r = sRows[row];
    switch (r.type) {
    case RowType::Header: return "";
    case RowType::Generate: {
        if (sGenerating) {
            char s[256];
            snprintf(s, sizeof(s), "%d%% %s (%s)", sPercent, sProgressText.c_str(), T("A: cancelar", "A: cancel"));
            return s;
        }
        if (!sLastIso.empty()) {
            return std::string(T("Lista: ", "Ready: ")) + sLastSeed + " (" + sLastHash + ")";
        }
        if (!sGenError.empty()) {
            return std::string(T("Error: ", "Error: ")) + sGenError;
        }
        return kCanGenerate ? T("A: generar", "A: generate") : T("no en la Switch", "not on the Switch");
    }
    case RowType::Play: {
        const std::string play = valueOf("play");
        std::string text = playText(play);
        if (play != "" && (play == "none" ? "" : play) != runningPlay()) {
            text += T("  (A: reiniciar)", "  (A: restart)");
        }
        return text;
    }
    case RowType::SeedName: {
        const std::string seed = valueOf("seed");
        return seed.empty() ? T("Al azar", "Random") : seed;
    }
    case RowType::Permalink: return T("A: importar", "A: import");
    case RowType::Defaults:
    case RowType::DefaultColors: return T("A: restaurar", "A: restore");
    case RowType::RandomColors: return T("A: sortear", "A: roll");
    case RowType::Option: return optionValueText(sSchema.options[r.index]);
    case RowType::Color: {
        const auto& [name, def] = currentColors()[r.index];
        const int rgb = parseRgb(valueOf("color." + name));
        return rgb < 0 || rgb == def ? std::string(T("Por defecto", "Default")) : "#" + rgbHex(rgb);
    }
    case RowType::Gear: {
        const Gear& g = sSchema.gear[r.index];
        const std::string v = valueOf("gear." + g.item);
        return v.empty() ? std::to_string(g.def) : v;
    }
    }
    return "";
}

int32_t rowColor(int row) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size() || sRows[row].type != RowType::Color) {
        return -1;
    }
    const auto& [name, def] = currentColors()[sRows[row].index];
    const int rgb = parseRgb(valueOf("color." + name));
    return rgb < 0 ? def : rgb;
}

std::string rowHelp(int row) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size()) {
        return "";
    }
    const Row& r = sRows[row];
    switch (r.type) {
    case RowType::Header:
        return sSchemaState == SchemaState::Ready && !sSchema.version.empty()
                   ? std::string("Wind Waker Randomizer ") + sSchema.version
                   : "";
    case RowType::Generate:
        if (const std::string why = unavailable(); !why.empty()) {
            return why;
        }
        if (!sLastPermalink.empty() && !sGenerating) {
            return std::string(T("Permalink (copiado al portapapeles): ", "Permalink (copied to the clipboard): ")) +
                   sLastPermalink;
        }
        return T("Crea una semilla con estas opciones a partir del disco original (1-3 minutos). Se guarda en "
                 "user/rando/seeds/ y, en la pantalla de título, el juego se reinicia en ella.",
                 "Makes a seed with these options from the clean disc (1-3 minutes). It is saved in "
                 "user/rando/seeds/ and, at the title screen, the game restarts into it.");
    case RowType::Play:
        return T("Izquierda/derecha: la semilla que carga el juego (se guarda); A: reiniciar en ella. Guarda la "
                 "partida antes: lo no guardado se pierde. Todas las semillas comparten las partidas de GZLE99.",
                 "Left/right: the seed the game loads (kept); A: restart into it. Save first: anything unsaved is "
                 "lost. Every seed shares the GZLE99 save files.");
    case RowType::SeedName:
        return T("Al azar: un nombre nuevo cada vez. Izquierda/derecha: repetir el de la última semilla.",
                 "Random: a new name each time. Left/right: reuse the last seed's name.");
    case RowType::Permalink:
        return T("Copia un permalink de wwrando (de una carrera, por ejemplo) y pulsa A: sus opciones y su semilla "
                 "pasan a esta pestaña. Debe ser de la versión incluida.",
                 "Copy a wwrando permalink (from a race, say) and press A: its options and seed come into this tab. "
                 "It must be for the bundled version.");
    case RowType::Defaults:
        return T("Vuelve a poner todas las opciones de wwrando como vienen (la semilla a jugar no cambia).",
                 "Puts every wwrando option back as it comes (the seed to play does not change).");
    case RowType::Option: {
        const Option& o = sSchema.options[r.index];
        std::string help = o.help;
        if (o.kind == "enum") {
            const std::string v = valueOf(o.name);
            for (const Choice& c : o.choices) {
                if (c.value == v && !c.help.empty()) {
                    help = c.help;
                }
            }
        }
        return help;
    }
    case RowType::Color:
        return T("Izquierda/derecha: colores de la paleta; A: uno al azar. El modelo se tiñe al generar.",
                 "Left/right: palette colors; A: a random one. The model is recolored when the seed is made.");
    case RowType::RandomColors:
        return T("Un color al azar para cada parte.", "A random color for every part.");
    case RowType::DefaultColors:
        return T("Los colores del modelo, sin cambios.", "The model's own colors, unchanged.");
    case RowType::Gear:
        return T("Cuántos de este objeto tienes al empezar; los demás se reparten por el mundo.",
                 "How many of this item you start with; the rest are placed in the world.");
    }
    return "";
}

void activate(int row, int dir) {
    std::lock_guard<std::mutex> lock(sMu);
    buildRowsLocked();
    if (row < 0 || row >= (int)sRows.size()) {
        return;
    }
    const Row r = sRows[row];
    switch (r.type) {
    case RowType::Header: break;
    case RowType::Generate:
        if (dir != 0) {
            break;
        }
        if (sGenerating) {
#if !defined(__SWITCH__)
            if (const int pid = sChild; pid > 0) {
                kill(pid, SIGTERM);
            }
#endif
            break;
        }
        startGenerateLocked();
        break;
    case RowType::Play: {
        if (sPlayVersion != sVersion) {
            buildPlayChoices();
        }
        const std::string play = valueOf("play");
        if (dir == 0) {
            if ((play == "none" ? "" : play) != runningPlay() && play != "") {
                restartGame();
            }
            break;
        }
        const int at = (int)(std::find(sPlayChoices.begin(), sPlayChoices.end(), play) - sPlayChoices.begin());
        const int i = cycleIndex(sPlayChoices, at < (int)sPlayChoices.size() ? at : -1, dir);
        sValues["play"] = sPlayChoices[i];
        if (sValues["play"].empty()) {
            sValues.erase("play");
        }
        saveLocked();
        break;
    }
    case RowType::SeedName:
        if (!valueOf("seed").empty()) {
            sValues.erase("seed");
        } else if (!sLastSeed.empty()) {
            sValues["seed"] = sLastSeed;
        }
        saveLocked();
        break;
    case RowType::Permalink:
        if (dir == 0) {
            importPermalinkLocked();
        }
        break;
    case RowType::Defaults:
        if (dir == 0) {
            const std::string play = valueOf("play");
            sValues.clear();
            if (!play.empty()) {
                sValues["play"] = play;
            }
            saveLocked();
            sToast = T("Opciones de wwrando por defecto", "wwrando's default options");
        }
        break;
    case RowType::Option: setOptionLocked(sSchema.options[r.index], dir); break;
    case RowType::Color: {
        const auto& [name, def] = currentColors()[r.index];
        const int cur = parseRgb(valueOf("color." + name));
        int next;
        if (dir == 0) {
            next = (int)(rng()() & 0xFFFFFF);
        } else {
            const std::vector<int> palette(std::begin(kPalette), std::end(kPalette));
            const int at = (int)(std::find(palette.begin(), palette.end(), cur) - palette.begin());
            next = palette[cycleIndex(palette, at < (int)palette.size() ? at : -1, dir)];
        }
        sValues["color." + name] = rgbHex(next);
        saveLocked();
        break;
    }
    case RowType::RandomColors:
        if (dir == 0) {
            for (const auto& c : currentColors()) {
                sValues["color." + c.first] = rgbHex((int)(rng()() & 0xFFFFFF));
            }
            sValues.erase("custom_color_preset");
            saveLocked();
        }
        break;
    case RowType::DefaultColors:
        if (dir == 0) {
            clearColors();
            sValues.erase("custom_color_preset");
            saveLocked();
        }
        break;
    case RowType::Gear: {
        const Gear& g = sSchema.gear[r.index];
        const std::string v = valueOf("gear." + g.item);
        int n = (v.empty() ? g.def : atoi(v.c_str())) + (dir < 0 ? -1 : 1);
        n = n > g.max ? 0 : n < 0 ? g.max : n;
        sValues["gear." + g.item] = std::to_string(n);
        saveLocked();
        break;
    }
    }
}

std::string takeToast() {
    std::lock_guard<std::mutex> lock(sMu);
    std::string t;
    t.swap(sToast);
    return t;
}

bool takeSeedReady() {
    std::lock_guard<std::mutex> lock(sMu);
    const bool ready = sSeedReady;
    sSeedReady = false;
    return ready;
}

void restartIntoSeed() {
    std::lock_guard<std::mutex> lock(sMu);
    restartGame();
}

} // namespace pc::seedgen

extern "C" void pc_seedgen_boot(void) {
    using namespace pc::seedgen;
    // What the launcher (or settings.ini's [dev]) gave, kept across a restart from the menu.
    const char* launcher = getenv("COS_SEEDGEN_LAUNCHER_DISC");
    if (launcher == nullptr) {
        const char* cur = getenv("COS_RANDO_DISC");
        setenv("COS_SEEDGEN_LAUNCHER_DISC", cur != nullptr ? cur : "", 1);
        launcher = getenv("COS_SEEDGEN_LAUNCHER_DISC");
    }
    const std::map<std::string, std::string> values = readIni(iniPath());
    const auto it = values.find("play");
    const std::string play = it != values.end() ? it->second : "";
    std::string disc = launcher;
    if (play == "none") {
        disc.clear();
    } else if (!play.empty()) {
        if (isFile(play)) {
            disc = play;
        } else {
            pc::writef(STDERR_FILENO, "[cos] seedgen: the seed to play (%s) is gone; the launcher's choice stays\n",
                       play.c_str());
        }
    }
    if (disc.empty()) {
        unsetenv("COS_RANDO_DISC");
    } else {
        setenv("COS_RANDO_DISC", disc.c_str(), 1);
    }
    if (!play.empty()) {
        pc::writef(STDERR_FILENO, "[cos] seedgen: seed to play from %s: %s\n", iniPath().c_str(),
                   disc.empty() ? "none (the original game)" : disc.c_str());
    }
}
