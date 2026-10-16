#pragma once
// Pause (ITEMS_HUD_MENUS.md 8): FUN_0013af80 START handling and FUN_0013b000 toggle, both on the player-control
// object 0x50b620, called from FUN_0013aaa0 in the game task FUN_0011d850 (game_scene.cpp). The paused state is the
// engine flag bit 0 (uRam004e6640, read through FUN_00102020); the overlay is FUN_00160af0 (hudPauseOverlay).
#include "game/items/item_screen.hpp"

namespace baroque::game {

struct PauseEnv {
    bool enginePaused = false;      // FUN_00102020(0x4e6640)
    bool eventRunning = false;      // iRam0051b620 != 0
    bool systemMenuOpen = false;    // FUN_00167d70(0x51b140)
    bool itemScreenOpen = false;    // iRam00517b68 (0x517b60 +8)
};

// Sound / engine / effect services the toggle drives (engine 0x4e6640, effects FUN_00112620, sound manager 0x4e9790,
// event manager 0x51b620).
struct PauseHooks {
    virtual ~PauseHooks() = default;
    virtual void setEnginePaused(bool on) = 0;       // uRam004e6640 bit 0
    virtual void freezeEffects(bool on) = 0;         // FUN_00112620(on)
    virtual void stopAllSe() = 0;                    // FUN_00115900(0x4e9790) = FUN_00115820(snd, 0xfffe, 2)
    virtual void voicePause(bool on) = 0;            // FUN_00115c50(0x4e9790, on)
    virtual void bgmVolumeTarget(float target, int ticks) = 0;   // FUN_00115f00(target, 0x4e9790, ticks)
    virtual void skipEvent() = 0;                    // FUN_0016d1a0(0x51b620, 0)
};

void pauseInput(uint32_t padPressed, const PauseEnv& env, PauseHooks& hooks);   // orig FUN_0013af80
// PORT: hold-to-skip input gate (game/event/event_skip_prompt.cpp installs it; null without that file)
extern void (*pauseEventHoldHook)();
void pauseToggle(bool on, const PauseEnv& env, PauseHooks& hooks);              // orig FUN_0013b000

}  // namespace baroque::game
