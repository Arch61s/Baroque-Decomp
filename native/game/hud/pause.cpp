#include "game/hud/pause.hpp"

namespace baroque::game {

// orig FUN_0013af80 (work/decompiled/0013af80.c): START (pressed 0x800 of 0x4e6288)
void (*pauseEventHoldHook)() = nullptr;   // PORT: hold-to-skip (event_skip_prompt.cpp)

void pauseInput(uint32_t padPressed, const PauseEnv& env, PauseHooks& hooks) {
    if (env.eventRunning && pauseEventHoldHook != nullptr) pauseEventHoldHook();   // PORT: hold-to-skip gate
    if ((padPressed & PadInput::kStart) == 0) return;
    if (env.eventRunning) {
        hooks.skipEvent();
        return;
    }
    if (env.systemMenuOpen) return;
    pauseToggle(!env.enginePaused, env, hooks);   // FUN_0013b000(ctl, FUN_00102020(0x4e6640) ^ 1)
}

// orig FUN_0013b000 (work/decompiled/0013b000.c)
void pauseToggle(bool on, const PauseEnv& env, PauseHooks& hooks) {
    hooks.setEnginePaused(on);
    hooks.freezeEffects(on);
    if (!env.itemScreenOpen && on) hooks.stopAllSe();
    hooks.voicePause(on);
    if (!on) hooks.bgmVolumeTarget(1.0f, 4);         // 0x3f800000
    else hooks.bgmVolumeTarget(0.7f, 4);             // 0x3f333333
}

}  // namespace baroque::game
