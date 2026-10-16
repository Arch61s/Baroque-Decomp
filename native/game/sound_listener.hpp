#pragma once
// Game side of the sound host's per-frame listener update (head of FUN_001150b0) and of the SE area test of
// FUN_00115470 (FUN_00124c70). See work/reports/AUDIO_NATIVE.md "Listener".
namespace baroque::audio { class SoundSystem; }

namespace baroque::game {

// Installs the listener source (camera task 0x50b624 / event manager 0x51b620 / player cell 0x50b144 +0x30) and the
// area lookup into `snd`. nullptr uninstalls nothing (call before uninstallSoundSystem is not needed).
void soundListenerInstall(audio::SoundSystem* snd);

}  // namespace baroque::game
