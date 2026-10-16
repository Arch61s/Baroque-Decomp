#pragma once
// Engine object 0x4e6640 and its per-frame tick FUN_00101bd0 (RUNTIME.md §1-4). Every scene's Update calls tick()
// exactly where the original calls FUN_00101bd0. The port separates presentation: tick() runs the logic part of
// the original frame (memory card, task list, fades, managers, pad read at the end, sound per-frame) and the host
// loop draws the resulting state afterwards (render passes of RUNTIME.md §4, currently the 2-D sprite layer).
#include <cstdint>
#include <functional>

namespace baroque::game {

struct Engine {
    int pauseFrames = 0;      // +0x98 (camera pause frames; FUN_00102020 reads it)
    uint32_t flags = 0;       // +0x0 bit0 paused
    uint64_t tickCount = 0;   // port: number of ticks run
    bool paused() const;      // FUN_00102020: (flags & 1) || 0 < pauseFrames < 4

    void tick();              // orig FUN_00101bd0 (translated subset, see engine.cpp)
    std::function<void()> soundFrame;   // FUN_001150b0(0x4e9790), installed by the host (audio module)
    std::function<void(bool paused)> eventFrame;   // FUN_0016d3b0(0x51b620, FUN_00102020()), installed by the event module
};
Engine& engine();             // 0x4e6640

}  // namespace baroque::game
