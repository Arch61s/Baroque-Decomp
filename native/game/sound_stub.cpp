#include "game/sound_stub.hpp"
#include "platform/platform.hpp"
namespace baroque::game {
namespace {
SoundManager gStub;
SoundManager* gImpl = &gStub;
}
SoundManager& sound() { return *gImpl; }
void setSoundManager(SoundManager* impl) { gImpl = impl ? impl : &gStub; }
void SoundManager::init() { if (!inited_) { inited_ = true; setPitch(0); } }
void SoundManager::playBgm(int bgmId, int vol, long flag) {
    if (!inited_) return;
    if (bgmId == -1) { stopBgm(0); return; }
    currentBgm_ = bgmId;
    lastLog = "bgm play " + std::to_string(bgmId) + " vol " + std::to_string(vol) + (flag == 0 ? " loop|0x20000" : "");
    platform::log("[sound-stub] " + lastLog);
}
void SoundManager::stopBgm(long fadeFrames) {
    if (!inited_) return;
    if (bgmVolume_ == 0.0f) fadeFrames = 0;
    currentBgm_ = -1;
    lastLog = "bgm stop fade " + std::to_string(fadeFrames);
    platform::log("[sound-stub] " + lastLog);
}
uint32_t SoundManager::playSe(float vol, int seId) {
    if (!inited_) return 0;
    lastLog = "se " + std::to_string(seId) + " vol " + std::to_string(vol);
    platform::log("[sound-stub] " + lastLog);
    return 0;   // the stub plays nothing: no handle
}
void SoundManager::playVoice(float vol, int voiceId) {
    if (!inited_) return;
    lastLog = "voice " + std::to_string(voiceId) + " vol " + std::to_string(vol);
    platform::log("[sound-stub] " + lastLog);
}
void SoundManager::setPitch(long pitch) {
    if (!inited_) return;
    if (pitch > 0x4000) pitch = 0x4000;
    if (pitch < -0x3000) pitch = -0x3000;
    pitch_ = pitch;
}
void SoundManager::setMono(bool mono) { mono_ = mono; }
void SoundManager::setBgmVolume(float v) { if (v <= 1.0f) bgmVolume_ = v; }
void SoundManager::setSeVolume(float v) { if (v <= 1.0f) seVolume_ = v; }
void SoundManager::setVoiceVolume(float v) { if (v <= 1.0f) voiceVolume_ = v; }
void SoundManager::setBgmFadeTarget(float, int) {}
void SoundManager::setMenuDuck(int) {}
void SoundManager::stopAll() { currentBgm_ = -1; platform::log("[sound-stub] stop all"); }
}
