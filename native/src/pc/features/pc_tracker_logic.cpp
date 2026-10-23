// The tracker's logic inside the game (pc_tracker.h, docs/TRACKER.md "In the game"): which locations are
// in logic now, worked out by wooferzfg's tracker's own code (scripts/tracker/logic/, bundled into one
// script by scripts/tracker/build_logic.sh) run in QuickJS on a thread of its own, so the answers are the
// tracker's. Its folder (COS_TRACKER_LOGIC, else tracker-logic/ next to the game, on the Switch next to
// user/) holds that script and wwrando's two logic files for the version rando mode is pinned to:
//   logic.js  item_locations.txt  macros.txt
// The seed's settings come from its permalink (seedPermalink: the text next to the rando disc). The game
// thread hands over each state it builds (logicSubmit); the thread works out the newest one and the game
// thread takes the result at its next frame (logicResult). Without the folder, the permalink or a rando
// disc there is no logic, and the overlay says why.
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include "JSystem/JKernel/JKRHeap.h"

#include <quickjs.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#if defined(__SWITCH__)
#include "cos_switch.h"
#endif

namespace pc::tracker {

namespace {

// never destroyed: the thread still waits on them while the game exits (glibc's condition variable
// destructor would wait for it)
std::mutex& sLock = *new std::mutex;
std::condition_variable& sWake = *new std::condition_variable;
bool sStarted = false;
std::string sPending; // the newest state not yet worked out (empty: none)
LogicResult sDone;    // the thread's newest result
bool sDoneNew = false;
LogicResult sResult; // the game thread's copy

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

char colorOf(char letter) {
    switch (letter) {
    case 'a': return LogicColor::AVAILABLE;
    case 'c': return LogicColor::CHECKED;
    case 'n': return LogicColor::NON_PROGRESS;
    default: return LogicColor::UNAVAILABLE;
    }
}

// cosTracker.updateText's lines (scripts/tracker/logic/entry.js)
void parseResult(const std::string& text, LogicResult& out) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t start = 0;
        for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) {
            f.push_back(line.substr(start, tab - start));
        }
        f.push_back(line.substr(start));
        if (f[0] == "E" && f.size() >= 2) {
            out.error = f[1];
        } else if (f[0] == "T" && f.size() >= 4) {
            out.checked = atoi(f[1].c_str());
            out.available = atoi(f[2].c_str());
            out.remaining = atoi(f[3].c_str());
        } else if (f[0] == "A" && f.size() >= 6) {
            LogicArea a;
            a.dungeon = f[1] == "1";
            a.available = atoi(f[2].c_str());
            a.remaining = atoi(f[3].c_str());
            a.color = colorOf(f[4].empty() ? 'u' : f[4][0]);
            a.name = f[5];
            out.areas.push_back(std::move(a));
        } else if (f[0] == "L" && f.size() >= 3 && !out.areas.empty()) {
            LogicArea& a = out.areas.back();
            a.locations.push_back({f[2], colorOf(f[1].empty() ? 'u' : f[1][0])});
            a.checked += a.locations.back().color == LogicColor::CHECKED ? 1 : 0;
        } else if (f[0] == "I" && f.size() >= 3) {
            out.items.push_back({f[2], atoi(f[1].c_str())});
        }
    }
    out.ready = out.error.empty();
}

// A JS exception as one line
std::string exceptionText(JSContext* ctx) {
    JSValue e = JS_GetException(ctx);
    const char* s = JS_ToCString(ctx, e);
    std::string text = s != nullptr ? s : "unknown error";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, e);
    return text;
}

// api.name(args...) as a string; false and error on an exception
bool callString(JSContext* ctx, JSValueConst api, const char* name, int argc, JSValueConst* argv, std::string& out) {
    JSValue fn = JS_GetPropertyStr(ctx, api, name);
    JSValue r = JS_Call(ctx, fn, api, argc, argv);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(r)) {
        out = exceptionText(ctx);
        return false;
    }
    const char* s = JS_ToCString(ctx, r);
    out = s != nullptr ? s : "";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, r);
    return true;
}

void publish(LogicResult&& r) {
    std::lock_guard<std::mutex> g(sLock);
    sDone = std::move(r);
    sDoneNew = true;
}

void fail(const std::string& why) {
    writef(STDERR_FILENO, "[cos] tracker: no logic: %s\n", why.c_str());
    LogicResult r;
    r.error = why;
    publish(std::move(r));
}

void* run(void*) {
    // host memory, never the game's current JKRHeap (cos_debug.cpp's server thread does the same)
    JKRPcBeginHostAlloc();
    const std::string dir = logicDir();
    std::string script, itemLocations, macros;
    if (!readFile(dir + "/logic.js", script) || !readFile(dir + "/item_locations.txt", itemLocations) ||
        !readFile(dir + "/macros.txt", macros)) {
        fail("the logic files are not in " + dir);
        return nullptr;
    }
    const std::string permalink = seedPermalink();
    if (permalink.empty()) {
        fail(gConfig.randoDisc == nullptr ? "not a randomizer disc"
                                          : "no permalink next to the disc (\"<disc> - Permalink.txt\" or "
                                            "wwrando's \"<disc> - Non-Spoiler Log.txt\")");
        return nullptr;
    }

    JSRuntime* rt = JS_NewRuntime();
    JS_SetMaxStackSize(rt, 3 * 1024 * 1024);
    JSContext* ctx = JS_NewContext(rt);
    JSValue r = JS_Eval(ctx, script.c_str(), script.size(), "logic.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        fail("logic.js: " + exceptionText(ctx));
        return nullptr;
    }
    JS_FreeValue(ctx, r);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue api = JS_GetPropertyStr(ctx, global, "cosTracker");
    JS_FreeValue(ctx, global);

    std::string out;
    {
        JSValue args[3] = {JS_NewStringLen(ctx, permalink.data(), permalink.size()),
                           JS_NewStringLen(ctx, itemLocations.data(), itemLocations.size()),
                           JS_NewStringLen(ctx, macros.data(), macros.size())};
        const bool ok = callString(ctx, api, "init", 3, args, out);
        for (JSValue& a : args) {
            JS_FreeValue(ctx, a);
        }
        if (!ok || out.find("\"error\"") != std::string::npos) {
            fail("the tracker's logic did not start: " + out);
            return nullptr;
        }
    }
    writef(STDERR_FILENO, "[cos] tracker: logic from %s, %s\n", dir.c_str(), out.c_str());
    // the strings are no longer needed
    std::string().swap(itemLocations);
    std::string().swap(macros);
    std::string().swap(script);

    for (;;) {
        std::string state;
        {
            std::unique_lock<std::mutex> g(sLock);
            sWake.wait(g, [] { return !sPending.empty(); });
            state.swap(sPending);
        }
        const auto t0 = std::chrono::steady_clock::now();
        JSValue arg = JS_NewStringLen(ctx, state.data(), state.size());
        const bool ok = callString(ctx, api, "updateText", 1, &arg, out);
        JS_FreeValue(ctx, arg);
        LogicResult result;
        if (ok) {
            parseResult(out, result);
        } else {
            result.error = out;
        }
        result.millis = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count());
        publish(std::move(result));
    }
    return nullptr;
}

} // namespace

// The rando disc's permalink, from the text files next to it: "<disc> - Permalink.txt" (the game's own
// seed generator) or wwrando's "<disc> - Non-Spoiler Log.txt" / "<disc> - Spoiler Log.txt"
// ("Permalink: ..." line), <disc> being the disc's path without ".iso". Empty if none says.
std::string seedPermalink() {
    if (gConfig.randoDisc == nullptr) {
        return "";
    }
    std::string base = gConfig.randoDisc;
    if (const size_t dot = base.rfind('.'); dot != std::string::npos && base.find('/', dot) == std::string::npos) {
        base.resize(dot);
    }
    const auto trim = [](std::string v) {
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) {
            v.pop_back();
        }
        while (!v.empty() && v.front() == ' ') {
            v.erase(0, 1);
        }
        return v;
    };
    std::string line;
    if (std::ifstream in(base + " - Permalink.txt"); in && std::getline(in, line) && !trim(line).empty()) {
        return trim(line);
    }
    for (const char* log : {" - Non-Spoiler Log.txt", " - Spoiler Log.txt"}) {
        std::ifstream in(base + log);
        for (int i = 0; in && i < 40 && std::getline(in, line); i++) {
            if (line.rfind("Permalink:", 0) == 0) {
                return trim(line.substr(10));
            }
        }
    }
    return "";
}

std::string logicDir() {
    if (const char* d = getenv("COS_TRACKER_LOGIC"); d != nullptr && *d != '\0') {
        return d;
    }
#if defined(__SWITCH__)
    return COS_SWITCH_ROOT "/tracker-logic";
#else
    char exe[4096] = {};
    executablePath(exe, sizeof exe);
    std::string dir = exe;
    const size_t slash = dir.rfind('/');
    dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    return dir + "/tracker-logic";
#endif
}

void logicSubmit(const std::string& stateJson) {
    std::lock_guard<std::mutex> g(sLock);
    if (!sStarted) {
        sStarted = true;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        // the logic's expressions nest deeply (QuickJS gets 3 MB of it, JS_SetMaxStackSize)
        pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
        pthread_t thread;
        if (pthread_create(&thread, &attr, run, nullptr) == 0) {
            pthread_detach(thread);
        } else {
            sDone.error = "no thread for the logic";
            sDoneNew = true;
        }
        pthread_attr_destroy(&attr);
    }
    sPending = stateJson;
    sWake.notify_one();
}

const LogicResult& logicResult() {
    std::lock_guard<std::mutex> g(sLock);
    if (sDoneNew) {
        const int serial = sResult.serial + 1;
        sResult = sDone;
        sResult.serial = serial;
        sDoneNew = false;
    }
    return sResult;
}

} // namespace pc::tracker
