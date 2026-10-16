// Binding of the event manager's EXI hooks to the effect runtime's game wrappers 0x112190.. (game/effects/effects.hpp).
#include "game/effects/effects.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_sound.hpp"

namespace baroque::game {

void eventBindEffects() {
    EventHooks& h = eventHooks();
    h.effectSpawn = [](float tag, int id, const float pos[4], const float rot[4], const float scale[4]) {
        return effects().spawn(tag, id, pos, rot, scale);                 // FUN_00112190
    };
    h.effectAlive = [](int id) { return effects().exists(id); };          // FUN_00112500
    h.effectKill = [](int id) { effects().kill(id); };                    // FUN_00112530
    h.effectMove = [](int id, const float p[4]) { effects().setPosition(id, p); };    // FUN_001122d0
    h.effectSetRot = [](int id, const float r[4]) { effects().setRotation(id, r); };  // FUN_00112370
    h.effectAddRot = [](int id, const float r[4]) { effects().addRotation(id, r); };  // FUN_001123b0
    h.effectScale4 = [](int id, const float s[4]) { effects().setScale(id, s); };     // FUN_00112490
    h.effectLoopOut = [](int id, int count, int end, int restart) {       // FUN_001126e0
        effects().loopOut(id, static_cast<int16_t>(count), static_cast<int16_t>(end), restart);
    };
    h.effectRate = [](int id, int rate) { effects().setRate(id, rate); }; // FUN_001126a0
    h.effectFlag98 = [](int id, bool on) { effects().setFlag98(id, on); };   // FUN_00112630
    h.effectsPause = [](bool on) { effects().setPaused(on); };            // FUN_00112620
    h.effectsKillFinished = [] { effects().killFinished(); };             // FUN_00112600
}

}  // namespace baroque::game
