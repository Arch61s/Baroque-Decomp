#pragma once
// Model of MODULES/IOPSND.IRX ("sting_sound_system") + libsd/modhsyn/modsein behaviour that the
// EE sound manager observes: 5 stream slots, 8 synth SE slots, the 240 Hz tick (fades, status),
// the volume curve / pan law, and the status block of RPC 0x8200. Each public method corresponds to
// one RPC command (work/reports/SOUND.md §3); the function offsets quoted are into IOPSND.IRX .text.
//
// RPCs are modelled as synchronous and never dropped. UNVERIFIED: on the PS2 FUN_00212540 skips a
// command while the previous asynchronous RPC is still pending (FUN_002126b0), and the status block
// the EE reads each frame is one RPC round-trip old.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "audio/adpcm.hpp"
#include "audio/bank.hpp"
#include "audio/sfs.hpp"
#include "audio/spu.hpp"

namespace baroque::audio {

// IOPSND .data tables (sound_ids.json["iop"]).
extern const int32_t kIopVolumeCurve[101];   // .data 0x6258: percent -> percent
extern const int32_t kIopPanTable[100];      // .data 0x5f00: pan 0..99 -> SPU pan 0..126
extern const int32_t kIopStreamVoice[5];     // .data 0x6244: core-1 voice per stream slot
constexpr int kIopTickHz = 240;              // USec2SysClock(0x1047)
constexpr int kIopTickFrames = kSpuRate / kIopTickHz; // 200

struct IopStatus {                 // RPC 0x8200 reply (built at 0x1288)
    uint32_t word[4] = {0, 0, 0, 0};
    // word0 bit(2+n): synth SE slot n active; bit(24+n): stream slot n started (0xd10f0)
    // word1 bit(24+n): stream keyed on (0xd1110)
    // word2 bit(24+n): first buffer primed (0xd1150) | bit0 suspended
    // word3 bit(24+n): stream has looped (0xd1130)
};

class IopSound {
public:
    static constexpr int kStreamSlots = 5;
    static constexpr int kSynthSlots = 8;

    // cmd 0 mode 0: tables + bank are supplied by the EE's 0x1290 sequence (the EE reads entry 0/1).
    void init(const SfsArchive* sfs, const HdBank* bank);
    // cmd 0 mode 1 (re-init after movie) / mode 2 (stop ticking).
    void reinit();
    // PORT (voice language): after the archive was re-opened on another file. Synth voices stop (the bank changes);
    // a STER stream whose entry has the same size in the new archive keeps playing from the new offset (the music
    // entries are byte-identical between the Japanese and English sound archives), any other stream stops.
    void archiveReopened();
    void setSuspended(bool s) { suspended_ = s; }
    bool suspended() const { return suspended_; }

    // 0x10 / 0x20 (func 0x175c): stream pan law switch. UNVERIFIED: the modhsyn:18 output-mode call for synth SEs is
    // not modelled (synth voices keep their stereo pan in mono mode).
    void setOutputMode(bool stereo) { stereoMode_ = stereo; }
    void setMasterVolume(uint16_t l, uint16_t r);                   // 0x30 (func 0x1798)
    void setEffect(uint32_t idx, uint32_t modeFlags, int depthL, int depthR, int delay, int feedback, int vol); // 0x1040
    void bindStreamVoice(int slot, int voice);                      // 0x980+slot (func 0x3908)
    // 0x1960+slot (func 0x31a4 / 0x3008): idFlags = SFS entry | 0x10000 stereo | 0x20000 no-loop,
    // vol = u16 (0x7b = deferred), pan = u8.
    void streamStart(int slot, uint32_t idFlags, uint32_t vol, uint32_t pan);
    void streamStop(int slot);                                      // 0x8970 (func 0x32a4)
    void streamFade(int slot, int vol, int time, int pan, int panTime); // 0xa00+slot (func 0x3ab0)
    void streamPause(int slot, uint32_t v);                         // 0xa10+slot (func 0x338c)
    // 0x950+slot (func 0x3270): +0x14 = vol, +0x2c = 2 (key-on request for a deferred start). Pan untouched.
    void streamSetVolume(int slot, uint32_t vol);
    // 0x410: param words {seqSlot, prog, vol<<16|note<<8|duration, handle, pan} (func 0x2480)
    void synthPlay(int seqSlot, uint32_t prog, uint32_t volNoteWord, uint32_t handle, uint32_t pan);
    void synthStop(int seqSlot);                                    // 0x420 (func 0x26b8)
    void synthFade(int seqSlot, int vol, int time, int pan, int panTime); // 0x450+idx (func 0x2a04)

    IopStatus status() const;                                       // 0x8200
    void tick();                                                    // 240 Hz timer thread body
    void render(int16_t* stereoOut, int frames) { mixer_.render(stereoOut, frames); }
    SpuMixer& mixer() { return mixer_; }

    // Diagnostics for checks.
    struct StreamSlotView { bool started, keyedOn, primed, looped, paused; int vol, pan, volTarget, volPeriod, panTarget; };
    StreamSlotView streamSlot(int slot) const;
    struct SynthSlotView { int countdown, vol, pan, volTarget; bool voiceActive; };
    SynthSlotView synthSlot(int idx) const;
    uint32_t readCounter() const { return readCounter_; }

    // Volume law (func 0x346c). Returns packed VOLL<<16 | VOLR for the first voice and, for a stereo
    // stream, the second voice. Exposed for the check.
    static void volumeLaw(int vol, int pan, bool stereoStream, bool stereoMode, uint32_t& voice0, uint32_t& voice1);

private:
    struct StreamSlot {                        // 0xd1170 + slot*0x70
        uint32_t handle = 0;                   // +0x00 (also 0xd10f0[slot]: "started")
        uint32_t flags = 0;                    // +0x04: bit0 stop, 0x100 stereo, 0x1000 no-loop, 0x10000 paused
        int vol = 0;                           // +0x14 current volume (percent, or 0x7b deferred)
        int pan = 0;                           // +0x18 current pan
        int panTarget = 0;                     // +0x1c
        int panPeriod = 0;                     // +0x20 (0 = not fading)
        int panCount = 0;                      // +0x24
        bool applyVolume = false;              // +0x28 (set by func 0x3440; not reachable from this EE)
        bool volumeAllowed = true;             // +0x2a != 0 (false while deferred)
        int volTarget = 0;                     // +0x48
        int volPeriod = 0;                     // +0x4c
        int volCount = 0;                      // +0x50
        bool keyedOn = false;                  // 0xd1110
        bool primed = false;                   // 0xd1150
        bool looped = false;                   // 0xd1130
        int voice = 18;                        // core-1 voice (stereo: voice, voice+1)
        uint32_t pitch = 0x1000;               // reader +0x24
        // sources
        std::unique_ptr<SterPairSource> ster;
        std::unique_ptr<MemoryVagSource> vag;
        std::vector<uint8_t> vagData;
        bool stopPending = false;
    };
    struct SynthSlot {                         // 0xd0f30 + idx*0x38
        int countdown = 0;                     // +0x00: duration*24+1, -1 = until stopped, 0 = idle
        uint32_t handle = 0;                   // +0x08
        int prog = 0;                          // +0x10
        int note = 0;                          // +0x14
        int pan = 0;                           // +0x18 (SPU pan 0..126 from the table)
        int panTarget = 0;                     // +0x1c
        int panPeriod = 0;                     // +0x20 (-1 after a stop: fades disabled)
        int panCount = 0;                      // +0x24
        int vol = 0;                           // +0x28
        int volTarget = 0;                     // +0x2c
        int volPeriod = 0;                     // +0x30
        int volCount = 0;                      // +0x34
        std::unique_ptr<MemoryVagSource> src;
        std::optional<ResolvedNote> res;
    };
    struct EntrySource final : SterStreamReader::Source {
        IopSound* owner = nullptr;
        const SfsArchive* sfs = nullptr;
        uint64_t base = 0;
        uint32_t entrySize = 0;   // PORT: archiveReopened
        size_t readEntry(uint64_t offset, void* dst, size_t size) override;
    };

    void applyStreamVolume(StreamSlot& s);
    void freeStream(int slot);
    void fadeTickStream(StreamSlot& s, int slot);   // func 0x3b1c
    void fadeTickSynth(SynthSlot& s, int idx);      // func 0x2a8c
    void applySynthVolume(SynthSlot& s, int idx);
    void synthExpire(int idx);                      // func 0x27d4 (via 0x289c)
    static bool fadeStep(int& cur, int target, int& period, int& count);

    const SfsArchive* sfs_ = nullptr;
    const HdBank* bank_ = nullptr;
    SpuMixer mixer_;
    std::array<StreamSlot, kStreamSlots> streams_;
    std::array<SynthSlot, kSynthSlots> synths_;
    std::array<EntrySource, kStreamSlots> sources_;
    bool stereoMode_ = true;     // gp-0x7fe8 == 0x10
    bool suspended_ = false;
    uint32_t readCounter_ = 0;   // gp-0x7f44: reader invocations
    EffectAttr effect_;
};

} // namespace baroque::audio
