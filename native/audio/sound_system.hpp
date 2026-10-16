#pragma once
// EE-side sound system: the game-facing sound host object 0x4e9790 (FUN_00114dd0..FUN_001162e0) and the sound
// manager it drives (FUN_0020fc20..FUN_00213840: 14 slots at 0x5401e0, command switch FUN_00210170, RPC
// FUN_00212540 -> IopSound). Everything the EE sends to the IOP goes through rpc(), which dispatches to IopSound
// (audio/iop.hpp) the way IOPSND.IRX's RPC dispatcher (func 0x158) does; IopSound mixes the SPU voices.
//
// Timing: tick() is the per-frame call of the main loop (FUN_00101bd0 -> FUN_001150b0). It runs the EE per-frame
// logic (FUN_001150b0, ending in FUN_00210170 cmd 0xf) once, then 8 IOP 240 Hz ticks, each rendering 200 frames
// of 48 kHz audio (8 x 200 = 1600 frames = 1/30 s). Port addition (not original): when the raylib output buffer
// runs low (the PS2 game frame is 1/29.97 s and the host frame pacing is not exact) one extra IOP tick is run.
//
// No dependency on other game modules: the listener (camera) position, the listener's area id and the 3D-area
// lookup (FUN_00124c70) are injected through setters/callbacks.
//
// GameSoundManager (bottom) adapts this object to the game's baroque::game::SoundManager interface.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "audio/bank.hpp"
#include "audio/iop.hpp"
#include "audio/sfs.hpp"
#include "audio/spu.hpp"
#include "game/sound_stub.hpp"

namespace baroque::audio {

// EE descriptor row (0x46bcf0 + id*0x14, 398 rows; also 0x46dd80 BGM / 0x46dda0 voice).
struct SeDescriptor {
    uint32_t flags;           // +0x0: 0x20000000 streamed VAG, 0x02000000 synth, 0x00200000 BGM, 0x00400000 voice
    uint32_t x;               // +0x4: SFS entry, or note<<8 | program
    uint16_t field8;          // +0x8 (1 for synth rows; bits 8..11 = bank slot, FUN_00211b30(0xc))
    uint16_t priority;        // +0xa
    uint16_t field12;         // +0xc: synth duration in 1/10 s (IOP countdown = *24+1); copied to slot +0x1c
    uint8_t defaultVolume;    // +0xe
    uint8_t mode;             // +0xf: 1 no-retrigger, 2 retrigger, 3 steal
    uint32_t user;            // +0x10
};
constexpr int kSeTableRows = 398;
constexpr int kEffectRows = 6;
constexpr int kStationSeCount = 397;
constexpr uint32_t kVoiceBaseEntry = 0xba;    // voice id + 0xba = SFS entry (FUN_00115970 / FUN_00115ce0)
constexpr int kBgmCount = 27;                  // bgm id + 2 = SFS entry (FUN_00115d70)
struct EffectRow { uint32_t mode; int16_t depthL; int16_t depthR; uint32_t delay; uint32_t feedback; };

// Verbatim SLUS_217.14 .data (audio/sound_tables.inc; Baroque-AudioCheck compares them with the ELF).
extern const SeDescriptor kSeTable[kSeTableRows];  // 0x46bcf0
extern const SeDescriptor kBgmDescriptor;           // 0x46dd80
extern const SeDescriptor kVoiceDescriptor;         // 0x46dda0
extern const EffectRow kEffectTable[kEffectRows];   // 0x46dc10 (0x20-byte rows, first 0x10 bytes used)
extern const int16_t kStationSeIds[kStationSeCount];// 0x46b990 (bank table row 1 id list)
extern const uint32_t kVolumeTableBits[11][3];      // 0x491cc0: option step -> {BGM, SE, voice} float bits
// Option volume step (0..10) -> float, category 0 = BGM, 1 = SE, 2 = voice (as read by FUN_0015da60).
float volumeStep(int step, int category);

// Sound manager slot (0x5401e0 + i*0x30; FUN_002123e0 clears +2..+0x2f).
struct SoundSlot {
    uint16_t id = 0;          // +0x00 0xaa00 + i (low nibble = index)
    uint16_t descX = 0;       // +0x02 desc.x low 16 (synth: note<<8|prog; FUN_00213610 sends the low byte)
    uint16_t iopSlot = 0;     // +0x04 IOP stream slot (0..4) / synth seq slot (0..7)
    uint16_t state = 0;       // +0x06 4 start pending, 3 playing, 5 volume pending, 6 retrigger
    uint16_t flags8 = 0;      // +0x08
    int16_t release = 0;      // +0x0a release countdown (frames)
    uint16_t gameId = 0;      // +0x0c
    uint16_t newVolume = 0;   // +0x0e
    uint32_t descFlags = 0;   // +0x10 desc flags | 0x80000000; 0x10000 play-once, 0x20000 seen playing,
                              //       0x40000 muted, 0x40000000 stopping
    uint32_t handle = 0;      // +0x14 slot<<28 | serial(bits 20..27) | id
    uint16_t field8 = 0;      // +0x18 desc +8
    uint16_t priority = 0;    // +0x1a desc +0xa
    uint16_t noteProg = 0;    // +0x1c desc +0xc | (note+pitch)<<8 (synth)
    uint8_t defVolume = 0;    // +0x1e desc +0xe
    uint8_t pad1f = 0;        // +0x1f
    uint8_t volume = 0;       // +0x20 (bit7: "send volume 4 first", FUN_00213110)
    uint8_t pan = 0;          // +0x21
    uint8_t pan2 = 0;         // +0x22
    uint8_t pan3 = 0;         // +0x23
    uint8_t fadeInVol = 0;    // +0x24
    uint8_t fadeInTime = 0;   // +0x25
    uint16_t pad26 = 0;       // +0x26
    uint32_t owner = 0;       // +0x28
    int32_t frames = 0;       // +0x2c frames since the stream started (FUN_00115ec0)
};

class SoundSystem {
public:
    static constexpr int kSlots = 14;
    // FUN_00124c70(pos, &a, &b): returns the area record at `pos` (nullptr = none). The callback returns false for
    // "no record", else the record's first word.
    using AreaLookup = std::function<bool(const float pos[3], uint32_t& areaWord)>;

    SoundSystem();
    ~SoundSystem();
    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    // FUN_00114f30(obj, fromCd, p3, p4) (boot, FUN_00100730 passes 1,1,1): opens the archive (RPC 0x1290 {4}),
    // stereo, loads bank 1 into bank slot 0, category floats BGM 0.6 / SE 0.9 / voice 0.7. openDevice: create the
    // raylib AudioStream (requires InitAudioDevice() by the platform). Returns false if the archive is missing.
    bool init(bool openDevice, int fromCd = 1, int p3 = 1, int p4 = 1);
    void shutdown();
    const std::string& lastError() const { return error_; }
    bool archiveOpen() const { return sfs_.isOpen(); }

    void tick();                                  // FUN_001150b0 (per frame) + IOP ticks + output

    // ---- game-facing API of object 0x4e9790 ----
    void enable();                                // FUN_00114fd0
    void disable();                               // FUN_00114dd0
    bool enabled() const { return enabled_ != 0; }
    void sceneChangeStop(int kind);               // FUN_00115020
    int param3() const { return p3_; }            // FUN_00115090
    int param4() const { return p4_; }            // FUN_001150a0
    uint32_t playSe(float vol, int seId);         // FUN_00115390 (returns the handle stored at +0x18)
    uint32_t playSe3d(float vol, int seId, const float pos[3]); // FUN_00115470
    void stopSe(uint32_t handle, int time);       // FUN_00115820 (handle 0xfffe = all)
    void stopAllSe() { stopSe(0xfffe, 2); }       // FUN_00115900
    uint32_t findSe(int seId);                    // FUN_00115930
    void playVoice(float vol, int voiceId, bool deferVol); // FUN_00115970
    void playVoiceDeferred(float realVol, int voiceId);    // FUN_00115a60
    void setVoiceVolume(float vol);               // FUN_00115af0
    void stopVoice(uint32_t handle, int time);    // FUN_00115bb0 (0xfffe = all)
    void stopCurrentVoice();                      // FUN_00115c10
    void voicePause(bool pause);                  // FUN_00115c50
    int voiceStatus();                            // FUN_00115cd0 (4 = playing)
    uint32_t playVoiceSimple(float vol, int voiceId); // FUN_00115ce0
    void playBgm(int bgmId, int fadeIn, int loop);// FUN_00115d70
    void stopBgm(int time);                       // FUN_00115e10
    int currentBgmEntry();                        // FUN_00115e80 (SFS entry = id+2, or -1)
    int currentBgmId() const { return currentBgmId_; } // +0x47c
    int bgmFrames();                              // FUN_00115ec0
    int bgmState();                               // FUN_00115f10 (FUN_00116510 on +0x478)
    int streamState(uint32_t handle);             // FUN_00116510 (4 = playing, else 0)
    void setBgmVolumeScale(float vol, int time);  // FUN_00115f00
    void setReverb(int type, int vol);            // FUN_00115f40
    void setReverbVolume(long vol);               // FUN_00115f70
    void setCategoryVolumes(float bgm, float se, float voice); // FUN_00115fc0
    void setBgmCategory(float v);                 // FUN_001160e0
    void setSeCategory(float v);                  // FUN_00116110
    void setVoiceCategory(float v);               // FUN_00116140
    float bgmCategory() const { return bgmCat_; }
    float seCategory() const { return seCat_; }
    float voiceCategory() const { return voiceCat_; }
    void seOverrideBegin();                       // FUN_001161a0
    void seOverrideEnd();                         // FUN_001161b0
    int seOverride() const { return seOverride_; }// FUN_001161c0
    void setSeOverrideFlag(int v);                // FUN_001161d0
    void setMono(bool mono);                      // FUN_001161e0
    void setListener(const float pos[3], const float right[3]); // FUN_00116220
    void setListenerArea(uint32_t area) { listenerArea_ = area; } // FUN_001150b0 (+0x334), 0xffffffff = none
    void setAreaLookup(AreaLookup fn) { areaLookup_ = std::move(fn); }
    // Head of FUN_001150b0 (game side): FUN_00116220 from the camera task 0x50b624 (+0x7c eye, +0x58 right) or, while
    // an event runs, the event manager 0x51b620 (+0x40 eye, +0x1c right); +0x334 from the player's cell (0x50b144
    // +0x30). tick() calls it while enabled, before the tracked 3-D SE updates. Unset = listener stays put.
    using ListenerSource = std::function<void(SoundSystem&)>;
    void setListenerSource(ListenerSource fn) { listenerSource_ = std::move(fn); }
    void setSePosition(uint32_t handle, const float pos[3]); // FUN_00116260
    uint32_t updateSe3d(float vol, uint32_t handle, const float pos[3]); // FUN_001162e0
    // Movie playback (FUN_00213840): stop everything before the movie, suspend/resume the IOP around it.
    void stopAllForMovie();                       // FUN_00210150 + cmd 10
    void suspendForMovie();                       // cmd 0x16
    void resumeAfterMovie();                      // cmd 0x15

    // Sound manager command switch (FUN_00210170). Public for checks/scripts.
    long command(int cmd, uint32_t p2 = 0, uint32_t p3 = 0, uint32_t p4 = 0, uint32_t p5 = 0, uint32_t p6 = 0);

    // ---- introspection ----
    void reloadArchive();             // PORT: see reloadSoundArchive()
    IopSound& iop() { return iop_; }
    const SfsArchive& archive() const { return sfs_; }
    const HdBank& bank() const { return bank_; }
    const SoundSlot& slot(int i) const { return slots_[size_t(i)]; }
    const SoundSlot* slotForHandle(uint32_t handle) const;   // FUN_002122d0
    uint32_t bgmHandle() const { return bgmHandle_; }
    const IopStatus& status() const { return status_; }
    uint16_t categoryPct(int i) const { return i == 0 ? bgmPct_ : i == 1 ? sePct_ : voicePct_; }
    int32_t seIdMap(int id) const { return seIdMap_[size_t(id)]; }
    // Audio rendered in the last tick (stereo int16, interleaved), for headless checks.
    const std::vector<int16_t>& lastTickAudio() const { return tickAudio_; }
    int iopTicksLastTick() const { return iopTicksLast_; }

private:
    // --- manager internals (FUN_0020fc20 .. FUN_00213840) ---
    void hostRequestReset();                                      // FUN_0020fc20
    void managerInit(int mode, int fromCd);                       // FUN_00210b70
    void streamInit(int mode);                                    // FUN_00212a20
    long loadBank(int bank, int bankSlot);                        // FUN_00211860
    long playRequest(uint32_t id, uint16_t vol, uint16_t panOrFade, int16_t pitch, int cmd, uint32_t owner); // FUN_00210f70
    uint32_t startStream(uint32_t id, uint32_t vol, uint32_t panOrFade, const SeDescriptor* desc, int cmd, uint32_t owner); // FUN_00212aa0
    uint32_t startSynth(uint32_t id, uint32_t vol, uint8_t pan, int16_t pitch, const SeDescriptor* desc, uint32_t owner); // FUN_00213300
    SoundSlot* allocSlot(uint32_t handleBits, const SeDescriptor& desc, uint32_t gameId); // FUN_00211ef0
    SoundSlot* lookup(uint32_t handle);                           // FUN_002122d0
    long setVolume(uint32_t handle, uint16_t vol);                // FUN_002113c0
    long pause(uint32_t handle, uint16_t v);                      // FUN_00211540
    long stop(uint32_t handle);                                   // FUN_002112f0
    long fade(uint32_t handle, uint16_t vol, uint16_t time, uint16_t pan, uint16_t pan2); // FUN_002115b0
    void stopStreamSlot(SoundSlot& s);                            // FUN_00212d00
    void stopSynthSlot(SoundSlot& s);                             // FUN_00213510
    void streamFadeRpc(SoundSlot& s, uint16_t vol, uint32_t time, uint8_t pan, uint8_t pan2); // FUN_00212db0
    void synthFadeRpc(SoundSlot& s, uint32_t vol, uint8_t time, uint8_t pan, uint8_t pan2);   // FUN_00213560
    void voiceVolumeRpc(SoundSlot& s);                            // FUN_00212ce0
    long driver(int what, int16_t t);                             // FUN_00211b30
    void bgmSlotTick(SoundSlot& s);                               // FUN_00212df0
    void streamSlotTick(SoundSlot& s);                            // FUN_00213110 (direct SE + voice)
    long synthSlotTick(SoundSlot& s);                             // FUN_00213610
    void clearSlot(SoundSlot& s);                                 // FUN_002123e0
    void clearQueue();                                            // FUN_00210b20
    uint32_t findSlotHandle(uint32_t gameId, uint32_t flagMask);  // FUN_00212350
    uint32_t rpc(uint32_t cmd, uint32_t param);                   // FUN_00212540 -> IOPSND func 0x158
    void resetHost();                                             // FUN_00114ea0
    uint32_t bgmAlive();                                          // FUN_0020fe20
    // host-side wrappers (FUN_0020fca0..FUN_00210100) used by more than one game-facing function
    uint32_t bgmPlayReq(uint32_t req, uint16_t vol, uint16_t fade); // FUN_0020fd50
    void bgmStopReq(uint32_t time);                               // FUN_0020fda0
    long bgmFadeTo(uint16_t vol, uint16_t time);                  // FUN_0020fee0
    void seStopReq(uint32_t handle, uint32_t time);               // FUN_0020ff80
    void voiceStopReq(uint32_t handle, uint32_t time);            // FUN_00210090
    long fadeHandle(uint32_t handle, uint16_t vol, uint16_t time, uint16_t pan, uint16_t pan2); // FUN_00210030 / FUN_00210100
    uint32_t areaOf(const float pos[3]);                          // FUN_00124c70 + the 0xf000 == 0x1000 test
    void bumpSerial();                                            // serial update shared by 0x211ef0/0x213510
    void runIopTick();
    // 0x540640 (0x80-byte command buffer) / 0x540580 (0x20-byte parameter block) byte accessors.
    void buf640Set8(int off, uint8_t v) { buf640_[size_t(off)] = v; }
    void buf640Set16(int off, uint16_t v);
    void buf640Set32(int off, uint32_t v);
    uint32_t buf640Get32(int off) const;

    // --- host object (0x4e9790) fields ---
    int bgmReqId_ = 999;          // +0x00 (FUN_0020fc20 struct: last requested BGM id, 999 = none)
    int bgmReqPrev_ = 0;          // +0x04
    uint32_t bgmReqHandle_ = 0;   // +0x10
    uint32_t lastSeHandle_ = 0;   // +0x18
    uint32_t listenerArea_ = 0xffffffffu; // +0x334
    int fromCd_ = 0, p3_ = 0, p4_ = 0;    // +0x338 +0x33c +0x340
    int enabled_ = 0;             // +0x344
    float listenerPos_[3] = {0, 0, -10.f};  // +0x348 (0xc1200000)
    float listenerRight_[3] = {0, 0, 1.f};  // +0x354
    uint32_t tracked_[kSlots] = {};         // +0x360 tracked "forced" 3D SE handles
    float trackedPos_[kSlots][3] = {};      // +0x398
    float trackedVol_[kSlots] = {};         // +0x440
    uint32_t field478_ = 0;       // +0x478 read by FUN_00115f10 only; no instruction of the ELF writes it (stays 0)
    int currentBgmId_ = -1;       // +0x47c
    float bgmScaleApplied_ = 1.f; // +0x484
    float bgmScaleTarget_ = 1.f;  // +0x488
    uint16_t bgmScaleTime_ = 4;   // +0x48c
    uint32_t voiceHandle_ = 0;    // +0x490
    int16_t voiceDeferredVol_ = 0;// +0x494
    float voiceVol_ = 0;          // +0x498
    float seCat_ = 0.9f;          // +0x49c
    float bgmCat_ = 0.6f;         // +0x4a0
    float voiceCat_ = 0.7f;       // +0x4a4
    uint16_t field4a8_ = 8;       // +0x4a8
    int seOverride_ = 0;          // +0x4ac
    int seOverrideFlag_ = 0;      // +0x4b0
    float seOverrideVol_ = 1.f;   // +0x4b4
    float seOverrideTarget_ = 1.f;// +0x4b8
    AreaLookup areaLookup_;
    ListenerSource listenerSource_;

    // --- manager globals ---
    uint32_t mgrFlags_ = 0;       // 0x540480: bit31 stereo, bit0 suspended
    uint32_t bgmHandle_ = 0;      // 0x540488
    uint32_t curBgmEntry_ = 0;    // 0x540490
    uint32_t prevBgmEntry_ = 0;   // 0x540498
    uint32_t g5404a0_ = 0, g5404a8_ = 0;
    struct Queued { uint32_t id = 0; uint16_t vol = 0; uint16_t fade = 0; int16_t pitch = 0; uint32_t cmd = 0; }; // 0x5404b0
    Queued queue_;                // *puRam005404c0
    uint32_t rpcRetries_ = 0;     // 0x5404c8 (FUN_002126b0 busy counter; always 0 here)
    int reloadBankSlot_ = 0;      // 0x5404d0
    int reloadExcluded_ = 0;      // 0x5404d8 (compared with slot addresses; 0 = none)
    uint32_t serial_ = 0;         // 0x5404e0
    int reverbType_ = 5;          // 0x5404f0
    int reverbVol_ = 0;           // 0x5404f8
    IopStatus status_;            // 0x540500..0x54050c
    uint32_t param580_[8] = {};   // 0x540580..0x54059c
    uint8_t buf640_[0x80] = {};   // 0x540640..0x5406bf
    uint8_t retriggerMap_[0x200] = {}; // 0x5406c0
    uint32_t delayedBgmVol_ = 0;  // 0x5406b8
    int16_t bankSlotOfBank_[2] = {0, 1};   // 0x46b988
    uint32_t bankHdAddr_[1] = {0};// 0x5408c0 (IOP heap + 0x2800)
    uint32_t bankSpuAddr_[1] = {0}; // 0x5408c8 (0xb800)
    int bankOfSlot_[1] = {0};     // 0x5408d0
    int loadedHdEntry_[1] = {-1}; // 0x5408d8
    int loadedBdEntry_[1] = {-1}; // 0x5408e0
    int32_t seIdMap_[kSeTableRows] = {}; // 0x53fb70
    uint16_t bgmPct_ = 100, sePct_ = 100, voicePct_ = 100; // 0x4e5e48/4c/50
    int32_t delayedBgm_ = -1;     // DAT_0046dcd0 (never written by this build: dead code)
    int32_t delayedBgm2_ = -1;    // DAT_0046dcd8 (never written: dead code)
    int8_t delayedBgmTimer_ = 0, delayedBgmTimer2_ = 0;    // 0x4e5e54 / 0x4e5e58
    uint32_t g540910_ = 0;        // 0x540910 (cleared by FUN_00213110, otherwise unused)
    std::array<SoundSlot, kSlots> slots_;
    struct BankRow { int hdEntry; int bdEntry; const char* name; const int16_t* ids; uint32_t count; }; // 0x46bcc0
    BankRow bankTable_[2] = {{0, 0, "NONE", nullptr, 0}, {1, 0, "STATION", kStationSeIds, kStationSeCount}};

    // --- objects ---
    SfsArchive sfs_;
    HdBank bank_;
    IopSound iop_;
    AudioOutput output_;
    bool iopInited_ = false;
    std::vector<int16_t> tickAudio_;
    int iopTicksLast_ = 0;
    std::string error_;
};

// Shared float -> u16 cast of the EE code: `if (2.1474836e+09 <= f) f -= 2.1474836e+09; (int)f & 0xffff`.
uint16_t floatToU16(float f);

// baroque::game::SoundManager implementation over SoundSystem (install with game::setSoundManager()).
// Method mapping (stub name -> original):
//   init -> FUN_00114fd0, playBgm(id, fadeIn, loop) -> FUN_00115d70 (the stub's second argument is the fade-in
//   time passed to FUN_0020fd50, not a volume), stopBgm -> FUN_00115e10, playSe -> FUN_00115390, playVoice ->
//   FUN_00115ce0, stopSe / findSe / setSePosition / updateSe3d -> FUN_00115820 / FUN_00115930 / FUN_00116260 /
//   FUN_001162e0, setPitch -> FUN_00115f70 (reverb EVOL, clamp [-0x3000, 0x4000]), setMono -> FUN_001161e0,
//   setBgm/Se/VoiceVolume -> FUN_001160e0/00116110/00116140, setBgmFadeTarget -> FUN_00115f00,
//   setMenuDuck -> FUN_001161d0, stopAll -> FUN_00115900 + FUN_00115c10.
class GameSoundManager final : public game::SoundManager {
public:
    explicit GameSoundManager(SoundSystem& sys) : sys_(sys) {}
    void init() override;
    bool initialized() const override { return sys_.enabled(); }
    void playBgm(int bgmId, int vol, long flag) override;
    void stopBgm(long fadeFrames) override;
    uint32_t playSe(float vol, int seId) override;
    uint32_t playSe3d(float vol, int seId, const float pos[3]) override { return sys_.playSe3d(vol, seId, pos); }
    void stopSe(uint32_t handle, int time) override { sys_.stopSe(handle, time); }                  // FUN_00115820
    uint32_t findSe(int seId) override { return sys_.findSe(seId); }                               // FUN_00115930
    void setSePosition(uint32_t handle, const float pos[3]) override { sys_.setSePosition(handle, pos); }   // FUN_00116260
    uint32_t updateSe3d(float vol, uint32_t handle, const float pos[3]) override { return sys_.updateSe3d(vol, handle, pos); }   // FUN_001162e0
    void playVoice(float vol, int voiceId) override;
    void setReverb(int type, int vol) override { sys_.setReverb(type, vol); }   // FUN_00115f40
    void setPitch(long pitch) override;
    void setMono(bool mono) override;
    void setBgmVolume(float v) override;
    void setSeVolume(float v) override;
    void setVoiceVolume(float v) override;
    void setBgmFadeTarget(float vol, int frames) override;
    void setMenuDuck(int on) override;
    void stopAll() override;
    void seOverrideBegin() override { sys_.seOverrideBegin(); }   // FUN_001161a0
    int bgmEntry() override { return sys_.currentBgmEntry(); }    // FUN_00115e80
    int bgmFrames() override { return sys_.bgmFrames(); }         // FUN_00115ec0
    int bgmState() override { return sys_.bgmState(); }           // FUN_00115f10
    SoundSystem& system() { return sys_; }
private:
    void mirror();               // keeps the base-class fields (currentBgm(), bgmVolume()) in sync
    SoundSystem& sys_;
};

// Convenience: boots a process-wide SoundSystem (FUN_00114f30 with the boot arguments 1,1,1), installs its
// GameSoundManager with game::setSoundManager() and returns it; nullptr (stub left installed) if the archive is
// missing. The main loop must call tick() once per game frame (FUN_001150b0 in FUN_00101bd0).
SoundSystem* installSoundSystem(bool openDevice);
// PORT (voice language): re-opens soundArchivePath() on the installed system (after setEnglishVoiceArchive); no-op
// without one. Stops every sound, reloads the HD/BD bank of the new archive and restarts the BGM that was playing.
void reloadSoundArchive();
void uninstallSoundSystem();

} // namespace baroque::audio
