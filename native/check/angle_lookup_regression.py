#!/usr/bin/env python3
"""Sanitizer regression for the actual angle table functions, without renderer deps.

Extract the production table and functions verbatim; only the game's scalar types
and cM3d_IsZero dependency are supplied by this small host harness.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "game/src/SSystem/SComponent/c_math.cpp").read_text()
table = source[source.index("static u16 atntable[]"):source.index("};", source.index("static u16 atntable[]")) + 2]


def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


epsilon_source = (root / "game/src/SSystem/SComponent/c_m3d.cpp").read_text()
epsilon = re.search(r"G_CM3D_F_ABS_MIN = ([^;]+);", epsilon_source).group(1)
harness = r'''
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <limits>
using u16 = uint16_t; using s16 = int16_t; using u32 = uint32_t;
bool cM3d_IsZero(float f) { return std::fabs(f) < EPSILON; }
'''.replace("EPSILON", epsilon)
harness += table + "\n" + function("u16 U_GetAtanTable(") + "\n" + function("s16 cM_atan2s(")
harness += r'''
int main() {
    assert(cM_atan2s(0, 1) == 0);
    assert(cM_atan2s(1, 0) == 16384);
    assert(cM_atan2s(0, -1) == -32768);
    assert(cM_atan2s(-1, 0) == -16384);
    assert(cM_atan2s(0, 0) == 0);
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()};
    for (float v : bad) {
        assert(cM_atan2s(v, 1) == 0);
        assert(cM_atan2s(1, v) == 0);
        assert(U_GetAtanTable(v, 1) == 0);
    }
    assert(U_GetAtanTable(0, 0) == 0);
    assert(U_GetAtanTable(1, 0) == 0);
    assert(U_GetAtanTable(-1, 1) == 0);
    assert(U_GetAtanTable(2, 1) == 0);
    assert(U_GetAtanTable(1, 1) == 8192);
    // Valid vectors across every quadrant retain the original lookup accuracy.
    for (int y = -100; y <= 100; ++y) {
        for (int x = -100; x <= 100; ++x) {
            if (!x && !y) continue;
            int actual = cM_atan2s(static_cast<float>(y), static_cast<float>(x));
            int expected = std::lround(std::atan2(double(y), double(x)) * 32768 / std::acos(-1.0));
            int error = static_cast<int16_t>(actual - expected);
            assert(std::abs(error) <= 11);
        }
    }
    std::puts("angle lookup regression: 40,400 valid vectors and invalid-input cases passed");
}
'''
with tempfile.TemporaryDirectory(prefix="waker-angle-test-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(harness)
    subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-DTARGET_PC=1",
                    "-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
