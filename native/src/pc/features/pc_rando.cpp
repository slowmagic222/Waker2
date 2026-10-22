// Rando mode (native/include/pc/pc_rando.h, docs/RANDO.md): the items the randomizer wrote into
// main.dol and the RELs, read from its disc (the game's disc, through Aurora's DVD layer).
#include "pc_internal.h"

#include "pc/pc_rando.h"

#include <cstdio>
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

unsigned char pc_rando_dol_item(unsigned int address, unsigned char vanilla) {
    if (!pc_rando_active()) {
        return vanilla;
    }
    const uint8_t* p = pc::dolBytes(address, 1);
    return pick("main.dol", address, p != nullptr ? *p : -1, vanilla);
}

} // extern "C"
