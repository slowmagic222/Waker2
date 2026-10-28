// Randomizer+ (native/include/pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): the generated world, read from the
// world.rpw that scripts/rplus/worldgen.py writes. The terrain built from it is in
// game_hooks/pc_rplus_terrain.cpp.
#include "pc_internal.h"
#include "pc/pc_rplus.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

struct World {
    bool loaded = false;
    unsigned int seed = 0;
    int squares = 7;
    int cells = 12;
    int n = 84;
    std::vector<unsigned short> height;
    std::vector<char> biome;
    bool prefab[64] = {};
    int startX = 0;
    int startY = 0;
};

World& world() {
    static World w;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char* path = getenv("COS_RPLUS_WORLD");
        if (path == nullptr || path[0] == '\0') {
            return w;
        }
        FILE* f = fopen(path, "r");
        if (f == nullptr) {
            fprintf(stderr, "[rplus] can't open COS_RPLUS_WORLD %s: Randomizer+ is off\n", path);
            return w;
        }
        char line[4096];
        bool header = false;
        int rows = 0;
        while (fgets(line, sizeof(line), f) != nullptr) {
            char* p = line;
            if (strncmp(p, "rplus ", 6) == 0) {
                header = atoi(p + 6) == 1;
            } else if (strncmp(p, "seed ", 5) == 0) {
                w.seed = (unsigned int)strtoul(p + 5, nullptr, 10);
            } else if (strncmp(p, "grid ", 5) == 0) {
                if (sscanf(p + 5, "%d %d", &w.squares, &w.cells) != 2 || w.squares <= 0 || w.squares > 7 ||
                    w.cells <= 0 || w.cells > 64) {
                    break;
                }
                w.n = w.squares * w.cells;
                w.height.assign((size_t)w.n * w.n, 0);
                w.biome.assign((size_t)w.n * w.n, '.');
            } else if (strncmp(p, "prefab ", 7) == 0) {
                int room = atoi(p + 7);
                if (room > 0 && room < 64) {
                    w.prefab[room] = true;
                }
            } else if (strncmp(p, "h ", 2) == 0 && !w.height.empty()) {
                char* end;
                long row = strtol(p + 2, &end, 10);
                if (row < 0 || row >= w.n) {
                    continue;
                }
                for (int x = 0; x < w.n; x++) {
                    long v = strtol(end, &end, 10);
                    w.height[(size_t)row * w.n + x] = (unsigned short)(v < 0 ? 0 : v > 1000 ? 1000 : v);
                }
                rows++;
            } else if (strncmp(p, "b ", 2) == 0 && !w.biome.empty()) {
                char* end;
                long row = strtol(p + 2, &end, 10);
                while (*end == ' ') {
                    end++;
                }
                if (row < 0 || row >= w.n) {
                    continue;
                }
                for (int x = 0; x < w.n && end[x] != '\0' && end[x] != '\n'; x++) {
                    w.biome[(size_t)row * w.n + x] = end[x];
                }
            } else if (strncmp(p, "start ", 6) == 0) {
                sscanf(p + 6, "%d %d", &w.startX, &w.startY);
            }
        }
        fclose(f);
        w.loaded = header && rows == w.n;
        if (w.loaded) {
            fprintf(stderr, "[rplus] world %s: seed %u, %dx%d rooms of %d cells, start cell %d,%d\n", path, w.seed,
                    w.squares, w.squares, w.cells, w.startX, w.startY);
        } else {
            fprintf(stderr, "[rplus] %s is not a Randomizer+ world (rplus 1, %d of %d rows): Randomizer+ is off\n",
                    path, rows, w.n);
        }
    }
    return w;
}

} // namespace

extern "C" {

int pc_rplus_active(void) {
    return world().loaded ? 1 : 0;
}

int pc_rplus_sea_room_generated(int roomNo) {
    const World& w = world();
    if (!w.loaded || roomNo < 1 || roomNo > w.squares * w.squares) {
        return 0;
    }
    return w.prefab[roomNo] ? 0 : 1;
}

int pc_rplus_squares(void) {
    return world().squares;
}

int pc_rplus_cells(void) {
    return world().cells;
}

int pc_rplus_cell_height(int cx, int cy) {
    const World& w = world();
    if (!w.loaded || cx < 0 || cy < 0 || cx >= w.n || cy >= w.n) {
        return 0;
    }
    return w.height[(size_t)cy * w.n + cx];
}

char pc_rplus_cell_biome(int cx, int cy) {
    const World& w = world();
    if (!w.loaded || cx < 0 || cy < 0 || cx >= w.n || cy >= w.n) {
        return '.';
    }
    return w.biome[(size_t)cy * w.n + cx];
}

void pc_rplus_start_cell(int* cx, int* cy) {
    *cx = world().startX;
    *cy = world().startY;
}

unsigned int pc_rplus_seed(void) {
    return world().seed;
}

} // extern "C"
