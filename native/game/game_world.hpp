#pragma once
// Floor-level state that the original reaches through fixed addresses, owned by the Game scene:
//   the generated floor (0x50a610 floor flags, 0x50a640 room table, 0x50a680 room count, FUN_00124c70 cell lookup)
//   and the shared game RNG 0x53eb20 (FUN_002071b0 / FUN_002071e0), seeded at boot from the RTC (FUN_00100730).
#include "game/rng.hpp"

namespace baroque::game {

class Dungeon;

struct GameWorld {
    Dungeon* dungeon = nullptr;   // null outside the Game scene / before the floor exists
    Rng rng;                      // 0x53eb20
};

inline GameWorld& gameWorld() {
    static GameWorld w;
    return w;
}

}  // namespace baroque::game
