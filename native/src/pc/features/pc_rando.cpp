// Rando mode (native/include/pc/pc_rando.h, docs/RANDO.md): the items the randomizer wrote into
// main.dol and the RELs, read from its disc (the game's disc, through Aurora's DVD layer).
#include "pc_internal.h"

#include "pc/pc_rando.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// A REL as the game would load it: files/rels/<rel>.rel, else inside RELS.arc (rels/mmem or
// rels/amem). Empty if the disc has neither.
struct Rel {
    bool read = false;
    std::vector<uint8_t> bytes;
};
std::map<std::string, Rel> sRels;

const std::vector<uint8_t>& rel(const char* name) {
    Rel& r = sRels[name];
    if (!r.read) {
        r.read = true;
        const std::string file = std::string("/rels/") + name + ".rel";
        const std::string mmem = std::string("rels/mmem/") + name + ".rel";
        const std::string amem = std::string("rels/amem/") + name + ".rel";
        if (!pc::readDiscFile(file.c_str(), nullptr, r.bytes) &&
            !pc::readDiscFile("/RELS.arc", mmem.c_str(), r.bytes) &&
            !pc::readDiscFile("/RELS.arc", amem.c_str(), r.bytes)) {
            pc::writef(STDERR_FILENO, "[cos] rando: %s.rel is not on the randomizer's disc\n", name);
            r.bytes.clear();
        }
    }
    return r.bytes;
}

// 0xFF is no item (dItemNo_NONE_e); anything else the randomizer wrote is taken as it is.
unsigned char pick(const char* what, unsigned int where, int got, unsigned char vanilla) {
    if (got < 0 || got == 0xFF) {
        pc::writef(STDERR_FILENO, "[cos] rando: no item at %s@%X; the original item 0x%02X stays\n", what, where,
                   (unsigned int)vanilla);
        return vanilla;
    }
    return (unsigned char)got;
}

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// The word at `address` of the randomizer's main.dol, or -1.
int64_t dolWord(unsigned int address) {
    const uint8_t* p = pc::dolBytes(address, 4);
    return p != nullptr ? (int64_t)be32(p) : -1;
}

// The word at `offset` of rels/<rel>.rel, or -1.
int64_t relWord(const char* name, unsigned int offset) {
    const std::vector<uint8_t>& bytes = rel(name);
    return offset + 4 <= bytes.size() ? (int64_t)be32(&bytes[offset]) : -1;
}

// A `b` (opcode 18) to the patch's new code, where the original game has another instruction.
bool isBranch(int64_t word) {
    return word >= 0 && ((uint32_t)word >> 26) == 18;
}

const char* const sOptionNames[PC_RANDO_OPTIONS] = {
    "remove music", "invert camera", "invert sea compass", "reveal sea chart", "swift sail", "hero mode", "swordless",
};

bool detect(PcRandoOption which) {
    switch (which) {
    case PC_RANDO_REMOVE_MUSIC:
        return dolWord(0x802A34A4) == 0x4E800020; // bgmStart__11JAIZelBasicFUlUll: blr
    case PC_RANDO_INVERT_CAMERA:
        return isBranch(dolWord(0x80162488)); // dCamera_c::updatePad: b invert_camera_horizontal_axis
    case PC_RANDO_INVERT_COMPASS:
        return dolWord(0x801FD37C) == 0x60000000; // dMeter_compassGetOnProc: nop for the neg
    case PC_RANDO_REVEAL_CHART:
        return dolWord(0x8005B2CC) == 0x38800001; // li r4, 1
    case PC_RANDO_SWIFT_SAIL:
        return relWord("d_a_ship", 0xDBE8) == 0x42DC0000; // the sailing speed, 110.0 (55.0)
    case PC_RANDO_HERO_MODE:
        return isBranch(dolWord(0x801102D0)); // daPy_lk_c::setDamagePoint: b multiply_damage_amount
    case PC_RANDO_SWORDLESS:
        return isBranch(dolWord(0x80235F10)); // phase_4: b give_temporary_sword_during_ganondorf_fight
    default:
        return false;
    }
}

int sOptions[PC_RANDO_OPTIONS];
bool sOptionsRead = false;

} // namespace

extern "C" {

int pc_rando_active(void) {
    return pc::gConfig.randoDisc != nullptr ? 1 : 0;
}

unsigned char pc_rando_rel_item(const char* name, unsigned int offset, unsigned char vanilla) {
    if (!pc_rando_active()) {
        return vanilla;
    }
    const std::vector<uint8_t>& bytes = rel(name);
    return pick(name, offset, offset < bytes.size() ? bytes[offset] : -1, vanilla);
}

const unsigned char* pc_rando_dol_bytes(unsigned int address, unsigned int size) {
    if (!pc_rando_active()) {
        return nullptr;
    }
    const uint8_t* p = pc::dolBytes(address, size);
    if (p == nullptr) {
        pc::writef(STDERR_FILENO, "[cos] rando: main.dol has no bytes at %X (+%u)\n", address, size);
    }
    return p;
}

unsigned char pc_rando_dol_item(unsigned int address, unsigned char vanilla) {
    if (!pc_rando_active()) {
        return vanilla;
    }
    const uint8_t* p = pc::dolBytes(address, 1);
    return pick("main.dol", address, p != nullptr ? *p : -1, vanilla);
}

int pc_rando_option(PcRandoOption which) {
    if (!pc_rando_active() || which < 0 || which >= PC_RANDO_OPTIONS) {
        return 0;
    }
    if (!sOptionsRead) {
        sOptionsRead = true;
        for (int i = 0; i < PC_RANDO_OPTIONS; i++) {
            sOptions[i] = detect((PcRandoOption)i) ? 1 : 0;
            if (sOptions[i]) {
                pc::writef(STDERR_FILENO, "[cos] rando: option %s\n", sOptionNames[i]);
            }
        }
    }
    return sOptions[which];
}

float pc_rando_damage_multiplier(void) {
    if (!pc_rando_option(PC_RANDO_HERO_MODE)) {
        return 1.0f;
    }
    const int64_t word = dolWord(0x803FE974); // damage_multiplier
    if (word < 0) {
        return 1.0f;
    }
    float f;
    const uint32_t bits = (uint32_t)word;
    memcpy(&f, &bits, sizeof(f));
    return f > 0.0f ? f : 1.0f;
}

} // extern "C"
