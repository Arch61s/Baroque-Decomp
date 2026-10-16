#pragma once
// Host binding of the event sound hooks (EventHooks::se3d .. voiceStop) to audio::SoundSystem.
namespace baroque::audio { class SoundSystem; }
namespace baroque::game {
void eventBindSoundSystem(audio::SoundSystem* snd);   // null clears the hooks
void eventBindEffects();                              // EXI hooks -> game/effects (event_effects.cpp)
// PORT (test log): 4 while the current voice plays (FUN_00115cd0), 0 otherwise or without a sound system.
int eventVoiceStatus();
}
