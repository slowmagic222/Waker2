// Randomizer+ (pc/pc_rplus.h, docs/RANDOMIZER_PLUS.md): each island's own day and night and its own weather.
//
// Every land mass of the generated world is an island, and every cell of the sea belongs to the island nearest it
// (its climate zone). From the world's seed each island gets a clock offset (up to six hours either way) and a day
// length (sunrise to sunset, 9 to 15 hours), and for each third of a day a weather: clear, rain, snow or storm,
// picked with weights from its biome (snow only falls on snow islands, storms are likeliest on volcanic ones). The
// same seed always gives the same skies.
//
// Only what is seen changes: the save's clock (dComIfGs_getTime) stays the one clock the game's rules read, and
// the island's time is put in g_env_light.mCurTime, which the sun, moon, stars and palette schedule are drawn from.
// Crossing into another island's zone eases the sky to its time and weather over a few seconds.
#include "pc/game_hooks.h"
#include "pc/pc_rplus.h"
#include "pc_rplus_world.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_kankyo_wether.h"
#include "f_op/f_op_actor_mng.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr float ROOM_UNITS = 100000.0f;
constexpr float PERIOD = 120.0f;  // degrees of the clock one weather lasts (8 hours, about 3 minutes at 30 fps)

enum Weather { CLEAR, RAIN, SNOW, STORM };
const char* const kWeatherName[] = {"clear", "rain", "snow", "storm"};

struct Island {
    char biome = 'g';
    float offset = 0.0f;   // clock offset, degrees (15 a game hour)
    float dayLen = 180.0f; // sunrise to sunset, degrees
    float phase = 0.0f;    // where its weather periods start, degrees
};

struct Zones {
    bool built = false;
    int n = 0;
    float x0 = 0.0f, z0 = 0.0f, cell = 1.0f;
    std::vector<int> zone;  // per cell: its island, -1 when the world has no land
    std::vector<Island> islands;
};

unsigned int mix(unsigned int a, unsigned int b) {
    unsigned int h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

float unit(unsigned int h) {
    return (h & 0xFFFFFF) / 16777216.0f;
}

float wrap360(float a) {
    a = fmodf(a, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}

// The shortest turn from a to b, in (-180, 180].
float turn(float a, float b) {
    float d = wrap360(b - a);
    return d > 180.0f ? d - 360.0f : d;
}

// Land masses (4-neighbour flood fill), then every water cell to its nearest land's island (a breadth-first walk
// out from all the land at once).
Zones& zones() {
    static Zones Z;
    if (Z.built || !pc_rplus_active()) {
        return Z;
    }
    Z.built = true;
    Z.n = pc_rplus_squares() * pc_rplus_cells();
    Z.cell = ROOM_UNITS / pc_rplus_cells();
    rplus::roomCorner(1, &Z.x0, &Z.z0);
    const int n = Z.n;
    Z.zone.assign((size_t)n * n, -1);
    std::vector<int> queue;
    queue.reserve((size_t)n * n);
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (Z.zone[(size_t)y * n + x] >= 0 || pc_rplus_cell_height(x, y) <= 0) {
                continue;
            }
            const int id = (int)Z.islands.size();
            Island is;
            is.biome = pc_rplus_cell_biome(x, y);
            const unsigned int h = mix(pc_rplus_seed(), (unsigned int)id + 1);
            is.offset = (unit(h) * 2.0f - 1.0f) * 90.0f;
            is.dayLen = 135.0f + unit(mix(h, 1)) * 90.0f;
            is.phase = unit(mix(h, 2)) * PERIOD;
            Z.islands.push_back(is);
            size_t head = queue.size();
            Z.zone[(size_t)y * n + x] = id;
            queue.push_back(y * n + x);
            while (head < queue.size()) {
                const int c = queue[head++];
                for (int k = 0; k < 4; k++) {
                    const int nx = c % n + dx[k], ny = c / n + dy[k];
                    if (nx < 0 || ny < 0 || nx >= n || ny >= n || Z.zone[(size_t)ny * n + nx] >= 0 ||
                        pc_rplus_cell_height(nx, ny) <= 0) {
                        continue;
                    }
                    Z.zone[(size_t)ny * n + nx] = id;
                    queue.push_back(ny * n + nx);
                }
            }
        }
    }
    // The queue holds every land cell: walk out over the water from all of them.
    for (size_t head = 0; head < queue.size(); head++) {
        const int c = queue[head];
        for (int k = 0; k < 4; k++) {
            const int nx = c % n + dx[k], ny = c / n + dy[k];
            if (nx < 0 || ny < 0 || nx >= n || ny >= n || Z.zone[(size_t)ny * n + nx] >= 0) {
                continue;
            }
            Z.zone[(size_t)ny * n + nx] = Z.zone[c];
            queue.push_back(ny * n + nx);
        }
    }
    fprintf(stderr, "[rplus] weather: %d islands with their own skies\n", (int)Z.islands.size());
    return Z;
}

// The island whose zone (x, z) is in, -1 outside the grid or without islands.
int islandAt(float x, float z) {
    Zones& Z = zones();
    if (Z.islands.empty()) {
        return -1;
    }
    const int cx = (int)floorf((x - Z.x0) / Z.cell), cy = (int)floorf((z - Z.z0) / Z.cell);
    if (cx < 0 || cy < 0 || cx >= Z.n || cy >= Z.n) {
        return -1;
    }
    return Z.zone[(size_t)cy * Z.n + cx];
}

// The island's time for the save's clock t: its offset, then its day stretched or squeezed so sunrise (6:00, 90)
// and sunset (18:00, 270) fall where its day length puts them.
float islandTime(const Island& is, float t) {
    const float u = wrap360(t + is.offset);
    const float rise = 180.0f - is.dayLen * 0.5f;  // centred on noon
    const float set = rise + is.dayLen;
    if (u >= rise && u < set) {
        return 90.0f + (u - rise) / is.dayLen * 180.0f;
    }
    const float night = 360.0f - is.dayLen;
    const float into = wrap360(u - set);
    return wrap360(270.0f + into / night * 180.0f);
}

// The weather and its strength (0.6 to 1) on island id for the save's day and time.
Weather islandWeather(int id, const Island& is, float t, int day, float* strength) {
    const float abs = day * 360.0f + t + is.phase;
    const unsigned int period = (unsigned int)floorf(abs / PERIOD);
    const unsigned int h = mix(mix(pc_rplus_seed(), (unsigned int)id + 101), period);
    // Weights: clear, rain, snow, storm.
    float w[4];
    switch (is.biome) {
    case 's': w[0] = 20; w[1] = 0; w[2] = 65; w[3] = 15; break;
    case 'f': w[0] = 35; w[1] = 45; w[2] = 0; w[3] = 20; break;
    case 'r': w[0] = 45; w[1] = 25; w[2] = 10; w[3] = 20; break;
    case 'v': w[0] = 45; w[1] = 15; w[2] = 0; w[3] = 40; break;
    case 'd': w[0] = 75; w[1] = 10; w[2] = 0; w[3] = 15; break;
    default: w[0] = 50; w[1] = 35; w[2] = 0; w[3] = 15; break;
    }
    float pick = unit(h) * (w[0] + w[1] + w[2] + w[3]);
    int k = 0;
    while (k < 3 && pick >= w[k]) {
        pick -= w[k];
        k++;
    }
    *strength = 0.6f + 0.4f * unit(mix(h, 7));
    return (Weather)k;
}

bool inGeneratedSea() {
    return pc_rplus_active() && strcmp(dComIfGp_getStartStageName(), "sea") == 0;
}

// What the sky is easing toward, and where it is now.
struct Sky {
    float rain = 0, snow = 0;    // particle counts
    float dark = 0;              // 0 clear .. 1 storm-dark
    float fog = 0;               // fog pull, 0..1
    bool thunder = false, cloudy = false;
};

bool sActive = false;      // last frame was in the generated sea
float sShownOffset = 0.0f; // the sky's clock against the save's, degrees
int sIsland = -2;
Weather sWeather = CLEAR;
Sky sNow;
float sBoost = 1.0f;

float toward(float v, float target, float step) {
    return v < target ? fminf(target, v + step) : fmaxf(target, v - step);
}

fopAc_ac_c* player() {
    return dComIfGp_getPlayer(0);
}

} // namespace

void pc_rplus_sky_time(f32* curTime) {
    if (!inGeneratedSea() || player() == nullptr) {
        // Out of the generated sea (a new stage clears the sky itself): the next visit starts clear.
        sActive = false;
        sNow = Sky{};
        sBoost = 1.0f;
        sIsland = -2;
        return;
    }
    const int id = islandAt(player()->current.pos.x, player()->current.pos.z);
    if (id < 0) {
        return;
    }
    const Island& is = zones().islands[id];
    const float t = *curTime;
    const float want = turn(t, islandTime(is, t));
    if (!sActive) {
        sShownOffset = want;  // a new scene starts on the island's own time
    } else {
        sShownOffset += fmaxf(-0.75f, fminf(0.75f, turn(sShownOffset, want)));
    }
    sActive = true;
    *curTime = wrap360(t + sShownOffset);
}

int pc_rplus_weather_frame(void) {
    if (!inGeneratedSea() || player() == nullptr || g_env_light.mInitAnimTimer != 0 || dKy_checkEventNightStop() ||
        dKy_pship_existense_chk()) {
        return 0;
    }
    const int id = islandAt(player()->current.pos.x, player()->current.pos.z);
    if (id < 0) {
        return 0;
    }
    float strength;
    const Weather w = islandWeather(id, zones().islands[id], dComIfGs_getTime(), dComIfGs_getDate(), &strength);
    if (id != sIsland || w != sWeather) {
        fprintf(stderr, "[rplus] weather: island %d (%c) %s %.2f\n", id, zones().islands[id].biome,
                kWeatherName[w], strength);
        sIsland = id;
        sWeather = w;
    }
    const bool blizzard = w == STORM && zones().islands[id].biome == 's';
    Sky want;
    switch (w) {
    case CLEAR:
        break;
    case RAIN:
        want.rain = 200.0f + 220.0f * strength;
        want.dark = 0.45f * strength;
        want.fog = 0.25f * strength;
        want.cloudy = true;
        break;
    case SNOW:
        want.snow = 250.0f + 250.0f * strength;
        want.dark = 0.2f;
        want.fog = 0.45f * strength;
        want.cloudy = true;
        break;
    case STORM:
        if (blizzard) {
            want.snow = 500.0f;
            want.fog = 0.7f;
            want.dark = 0.6f;
        } else {
            want.rain = 500.0f;
            want.fog = 0.5f;
            want.dark = 0.8f + 0.2f * strength;
            want.thunder = true;
        }
        want.cloudy = true;
        break;
    }
    // Ease: counts a few a frame (rain stops before snow starts), shades over about three seconds.
    if (want.snow > 0.0f && sNow.rain > 0.0f) {
        want.snow = 0.0f;
    }
    if (want.rain > 0.0f && sNow.snow > 0.0f) {
        want.rain = 0.0f;
    }
    sNow.rain = toward(sNow.rain, want.rain, 3.0f);
    sNow.snow = toward(sNow.snow, want.snow, 3.0f);
    sNow.dark = toward(sNow.dark, want.dark, 0.01f);
    sNow.fog = toward(sNow.fog, want.fog, 0.008f);
    sBoost = 1.0f + 0.9f * sNow.dark;

    dKyw_rain_set((int)sNow.rain);
    g_env_light.mSnowCount = (int)sNow.snow;
    g_env_light.mThunderEff.mMode = want.thunder ? 1 : 0;

    const u8 colpat = want.cloudy ? 1 : 0;
    if (g_env_light.mColPatMode == 0 && g_env_light.mColPatModeGather == 0 && g_env_light.mColpatWeather != colpat) {
        g_env_light.mColpatWeather = colpat;
        g_env_light.mColpatCurrGather = colpat;
    }

    // Darker clouds, sky, land and people; fog pulled in close. The gather ratios are reset to 1 each frame
    // (exeKankyo), so multiplying keeps what other actors asked for.
    const float d = sNow.dark;
    g_env_light.mVrboxKumoColGatherRatio *= 1.0f - 0.5f * d;
    g_env_light.mVrboxSoraColGatherRatio *= 1.0f - 0.4f * d;
    g_env_light.mBgColGatherRatio *= 1.0f - 0.3f * d;
    g_env_light.mActColGatherRatio *= 1.0f - 0.22f * d;
    g_env_light.mFogColGatherRatio *= 1.0f - 0.3f * d;
    dKy_fog_startendz_set(1500.0f, 20000.0f, sNow.fog);
    return 1;
}

float pc_rplus_precip_boost(void) {
    return inGeneratedSea() ? sBoost : 1.0f;
}

