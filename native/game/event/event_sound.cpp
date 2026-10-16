// Binding of the event manager's sound hooks to the audio module's sound host 0x4e9790 (entries the
// game::SoundManager interface does not carry). Called by the host after installSoundSystem.
#include "game/event/event_sound.hpp"

#include "audio/sound_system.hpp"
#include "game/event/event_manager.hpp"

namespace baroque::game {

namespace { audio::SoundSystem* gEventSnd = nullptr; }   // PORT (test log)
int eventVoiceStatus() { return gEventSnd != nullptr ? gEventSnd->voiceStatus() : 0; }

void eventBindSoundSystem(audio::SoundSystem* snd) {
    EventHooks& h = eventHooks();
    gEventSnd = snd;
    if (snd == nullptr) {
        h.se3d = nullptr;
        h.seFind = nullptr;
        h.seStop = nullptr;
        h.voicePause = nullptr;
        h.voicePlay = nullptr;
        h.voiceStop = nullptr;
        h.seStopHandle = nullptr;
        h.sound137 = nullptr;
        h.sound138 = nullptr;
        return;
    }
    h.se3d = [snd](float vol, int id, const float pos[3]) { snd->playSe3d(vol, id, pos); };       // FUN_00115470
    h.seFind = [snd](int id) { return snd->findSe(id); };                                          // FUN_00115930
    h.seStop = [snd](uint32_t handle, int time) { snd->stopSe(handle, time); };                    // FUN_00115820
    h.voicePause = [snd](int v) { snd->voicePause(v != 0); };                                      // FUN_00115c50
    h.voicePlay = [snd](float vol, int id) { snd->playVoiceDeferred(vol, id); };                   // FUN_00115a60
    h.voiceStop = [snd](uint32_t handle, int time) { snd->stopVoice(handle, time); };              // FUN_00115bb0
    // FUN_00115910 (op 126) is `jr ra` and FUN_00115920 (op 137) / FUN_00115e70 (op 138) are `lw $v1,0x344($a0);
    // jr ra` (disasm 0x115910 / 0x115920 / 0x115e70): empty in this build, bound as such.
    h.seStopHandle = [](uint32_t) {};                                                              // FUN_00115910
    h.sound137 = [](int) {};                                                                       // FUN_00115920
    h.sound138 = [](int) {};                                                                       // FUN_00115e70
}

}  // namespace baroque::game
