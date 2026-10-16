#pragma once
// Sound manager 0x4e9790 interface in the original API shape (SCENES.md 0.6, RUNTIME.md). This stub logs; the audio
// module will implement the same interface against BRQSOUND.SFS.
#include <cstdint>
#include <string>
namespace baroque::game {
class SoundManager {
public:
    virtual ~SoundManager() = default;
    virtual void init();                                   // FUN_00114fd0 (lazy init, +0x344 = 1, pitch 0)
    virtual bool initialized() const { return inited_; }
    virtual void playBgm(int bgmId, int vol, long flag);   // FUN_00115d70(snd, id, vol, flag): internal id = id+2 (|0x20000 when flag==0)
    virtual void stopBgm(long fadeFrames);                 // FUN_00115e10(snd, fade) (fade forced 0 when BGM volume is 0)
    // SE calls return the sound-manager handle (slot<<28 | serial<<20 | id; 0 = not started), as the originals do.
    virtual uint32_t playSe(float vol, int seId);          // FUN_00115390(vol, snd, id): internal id = id+1
    virtual uint32_t playSe3d(float vol, int seId, const float pos[3]) { (void)pos; return playSe(vol, seId); }   // FUN_00115470
    virtual void stopSe(uint32_t handle, int time) { (void)handle; (void)time; }   // FUN_00115820(snd, handle, time) (0xfffe = all)
    virtual uint32_t findSe(int seId) { (void)seId; return 0; }                    // FUN_00115930(snd, id): handle of a playing SE
    virtual void setSePosition(uint32_t handle, const float pos[3]) { (void)handle; (void)pos; }   // FUN_00116260
    virtual uint32_t updateSe3d(float vol, uint32_t handle, const float pos[3]) { (void)vol; (void)pos; return handle; }   // FUN_001162e0
    virtual void playVoice(float vol, int voiceId);        // FUN_00115ce0(vol, snd, id): internal id = id+0xba
    virtual void setReverb(int type, int vol) { (void)type; (void)vol; }   // FUN_00115f40(snd, type, vol)
    virtual void setPitch(long pitch);                     // FUN_00115f70: clamp [-0x3000, 0x4000]
    virtual void setMono(bool mono);                       // FUN_001161e0
    virtual void setBgmVolume(float v);                    // FUN_001160e0 (+0x4a0)
    virtual void setSeVolume(float v);                     // FUN_00116110 (+0x49c)
    virtual void setVoiceVolume(float v);                  // FUN_00116140 (+0x4a4)
    virtual void setBgmFadeTarget(float vol, int frames);  // FUN_00115f00 (+0x488 / +0x48c)
    virtual void setMenuDuck(int on);                      // FUN_001161d0 (+0x4b0)
    virtual void stopAll();                                // FUN_00115900 + FUN_00115c10 (Game scene exit)
    virtual void seOverrideBegin() {}                      // FUN_001161a0 (+0x4ac = 1, +0x4b8 = 0)
    // BGM stream queries. The logging stub has no stream: entry follows currentBgm(), no frames, state 0.
    virtual int bgmEntry() { return currentBgm_ == -1 ? -1 : currentBgm_ + 2; }   // FUN_00115e80 (SFS entry or -1)
    virtual int bgmFrames() { return 0; }                  // FUN_00115ec0 (frames since the BGM stream started)
    virtual int bgmState() { return 0; }                   // FUN_00115f10 (FUN_00116510(snd, +0x478) when +0x344)
    int currentBgm() const { return currentBgm_; }         // +0x47c (-1 = none)
    float bgmVolume() const { return bgmVolume_; }
    // test hooks
    std::string lastLog;
protected:
    bool inited_ = false;
    int currentBgm_ = -1;
    float bgmVolume_ = 1.0f, seVolume_ = 1.0f, voiceVolume_ = 1.0f;
    long pitch_ = 0;
    bool mono_ = false;
};
SoundManager& sound();
void setSoundManager(SoundManager* impl);   // audio module hook; nullptr restores the logging stub
}
