#pragma once
#include <cstdint>
namespace baroque::game {
// Original game LCG (see work/reports/MAP_FORMATS.md §7). state*0x19660d+0x3c6ef35f; rand(n) in [0,n].
struct Rng {
    uint32_t state = 0;
    void seed(uint32_t s) { state = s; }
    uint32_t next() { state = state * 0x19660du + 0x3c6ef35fu; return state; }
    uint32_t rand(uint32_t n) { return static_cast<uint32_t>((static_cast<uint64_t>(next()) * (static_cast<uint64_t>(n) + 1u)) >> 32); }
};
}
