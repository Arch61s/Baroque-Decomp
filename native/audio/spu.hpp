#pragma once
// SPU2 voice model: ADPCM block sources, SPU2 ADSR envelope, pitch resampling, 15-bit voice
// volumes, master volume, 48 kHz stereo mixing into a raylib AudioStream.
//
// Scope: only what IOPSND/libsd/modhsyn use for this game — stream voices (core 1 voices 18..23)
// and one voice per synth SE. Formulas implemented:
//  * pitch register p (0x1000 = 48000 Hz output rate, 1 source sample per output sample):
//      stream voices  p = (rate << 12) * 0x57619f1 >> 42   (IOPSND L_05354..0537c, == rate*4096/48000)
//      synth voices   p = round(vagRate * 4096 / 48000 * 2^((note - baseNote + transposes + detune/128)/12))
//                     UNVERIFIED: modhsyn's note->pitch (libsd sceSdNote2Pitch) not traced; every split
//                     of this bank is a single note == baseNote so the ratio is exactly rate/48000.
//  * volume registers: 15-bit (0x3fff ~ unity), factor = v / 0x4000 (SPU2 VOLL/VOLR fixed mode).
//  * ADSR: SPU2 semantics (same as PSX SPU): ADSR1 = Am<<15 | Ar<<8 | Dr<<4 | Sl,
//    ADSR2 = Sm<<15 | Sd<<14 | Sr<<6 | Rm<<5 | Rr. Rate -> (shift, step) per the documented SPU
//    envelope algorithm (cycles = 1 << max(0, shift-11), step <<= max(0, 11-shift), exponential
//    increase slows x4 above 0x6000, exponential decrease scales step by level/0x8000).
//    UNVERIFIED against LIBSD.IRX; the bank only uses attack rate 0 (instant), sustain 0x7f (hold),
//    release 0/0xf, and stream voices use ADSR1 0x000f / ADSR2 0x1fc1.
//  * Interpolation: linear between source samples. UNVERIFIED: the SPU uses a 4-tap Gaussian
//    filter; at pitch 0x1000 (all streams) both are exact.
//  * Reverb: effect attributes are stored (hook) and the output is dry. UNVERIFIED.
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include "audio/adpcm.hpp"

namespace baroque::audio {

constexpr int kSpuRate = 48000;

// ---------------------------------------------------------------- block sources
struct BlockSource {
    virtual ~BlockSource() = default;
    // Produces the next 28 samples. Returns false when no more data (voice should stop).
    // `flags` receives the block flag (1/3 -> key off after this block, 7 -> stop).
    virtual bool nextBlock(int16_t out[28], uint8_t& flags) = 0;
};

// Decodes an in-memory ADPCM body (BD VAG or SFS VAGp data) sequentially, honouring loop flags:
// flag 6 records the loop start, flag 3 jumps back to it, flag 1 ends (the block is still output),
// flag 7 stops immediately.
class MemoryVagSource final : public BlockSource {
public:
    MemoryVagSource() = default;
    MemoryVagSource(const uint8_t* data, size_t len) { reset(data, len); }
    void reset(const uint8_t* data, size_t len);
    bool nextBlock(int16_t out[28], uint8_t& flags) override;
    size_t position() const { return pos_; }
private:
    const uint8_t* data_ = nullptr;
    size_t len_ = 0, pos_ = 0;
    size_t loopStart_ = SIZE_MAX;
    AdpcmState st_;
    bool done_ = false;
};

// Splits a SterStreamReader into two mono sources (L, R). The two SPU voices pull blocks independently (one voice
// renders a whole IOP tick before the other), so each side keeps a FIFO of the decoded blocks the other side
// pulled ahead of it; on the IOP both halves land in their own SPU buffers (de-interleave loop 0x4604).
class SterPairSource {
public:
    SterStreamReader reader;
    BlockSource& left() { return left_; }
    BlockSource& right() { return right_; }
    SterPairSource();
private:
    using Block = std::array<int16_t, 28>;
    struct View final : BlockSource {
        SterPairSource* owner = nullptr;
        bool isRight = false;
        bool nextBlock(int16_t out[28], uint8_t& flags) override;
    };
    bool pump();
    View left_, right_;
    std::deque<Block> qL_, qR_;
    bool ended_ = false;
};

// ---------------------------------------------------------------- envelope
class SpuEnvelope {
public:
    enum Phase { Off, Attack, Decay, Sustain, Release };
    void keyOn(uint16_t adsr1, uint16_t adsr2);
    void keyOff();
    void stop() { phase_ = Off; level_ = 0; }
    // Advances one output sample; returns the level 0..0x7fff.
    int32_t step();
    int32_t level() const { return level_; }
    Phase phase() const { return phase_; }
    bool off() const { return phase_ == Off; }
private:
    void setRate(int shift, int step, bool exponential, bool decrease);
    void enterSustain();
    Phase phase_ = Off;
    int32_t level_ = 0;
    uint16_t adsr1_ = 0, adsr2_ = 0;
    int32_t cycles_ = 1, counter_ = 0, stepValue_ = 0;
    bool exp_ = false, decrease_ = false;
};

// ---------------------------------------------------------------- voice
class SpuVoice {
public:
    void keyOn(BlockSource* src, uint32_t pitch, uint16_t adsr1, uint16_t adsr2);
    void keyOff();               // -> release
    void stop();                 // immediate silence
    void setVolume(int32_t volL, int32_t volR) { volL_ = volL; volR_ = volR; }
    void setPitch(uint32_t pitch) { pitch_ = pitch; }
    uint32_t pitch() const { return pitch_; }
    bool active() const { return src_ != nullptr && !env_.off(); }
    bool keyedOn() const { return keyedOn_; }
    // True once the source's last block has been consumed (NAX passed the end).
    bool endReached() const { return endReached_; }
    // Accumulates `frames` stereo samples into the int32 mix buffers.
    void render(int32_t* mixL, int32_t* mixR, int frames);
private:
    bool fetchBlock();
    BlockSource* src_ = nullptr;
    SpuEnvelope env_;
    uint32_t pitch_ = 0x1000;
    int32_t volL_ = 0, volR_ = 0;
    // decoded sample FIFO
    int16_t fifo_[64] = {};
    int fifoRead_ = 0, fifoCount_ = 0;
    int16_t prev_ = 0;           // current sample (interpolation start point)
    uint32_t frac_ = 0;          // 12-bit position fraction
    bool srcDone_ = false, endReached_ = false, pendingKeyOff_ = false, keyedOn_ = false;
    int tailSamples_ = 0;        // samples left to drain after the source ended
};

// ---------------------------------------------------------------- effect hook
struct EffectAttr {        // sceSdSetEffectAttr parameters (EE table 0x46dc10 rows)
    uint32_t mode = 0;     // 0 off, 1 ROOM, 2 STUDIO_A, 3 STUDIO_B, 4 STUDIO_C, 5 HALL
    int32_t depthL = 0, depthR = 0, delay = 0, feedback = 0;
    int32_t volume = 0;    // EVOLL/EVOLR (raw SPU volume, EE clamps to [-0x3000, 0x4000])
    bool enabled = false;
};

// ---------------------------------------------------------------- mixer + output
class SpuMixer {
public:
    static constexpr int kVoices = 24; // core 1 voices 0..23 (18..23 used by streams) + synth voices
    static constexpr int kSynthVoiceBase = 0; // synth SE n -> voice n (0..7)
    SpuVoice& voice(int i) { return voices_[i]; }
    const SpuVoice& voice(int i) const { return voices_[i]; }
    void setMasterVolume(uint16_t l, uint16_t r) { mvolL_ = l; mvolR_ = r; }
    void setEffect(const EffectAttr& e) { effect_ = e; }
    const EffectAttr& effect() const { return effect_; }
    // Renders `frames` stereo int16 frames (interleaved).
    void render(int16_t* out, int frames);
private:
    SpuVoice voices_[kVoices];
    uint16_t mvolL_ = 0x3ff0, mvolR_ = 0x3ff0;
    EffectAttr effect_;
    std::vector<int32_t> mixL_, mixR_;
};

// raylib AudioStream output with a ring buffer fed by the game thread (tick) and drained by the
// audio callback. Headless (no audio device): push() discards.
class AudioOutput {
public:
    ~AudioOutput();
    bool open();               // requires IsAudioDeviceReady(); returns false otherwise
    void close();
    bool isOpen() const { return open_; }
    void push(const int16_t* stereo, int frames);
    int queuedFrames() const;
    static constexpr int kRingFrames = 48000 / 2; // 0.5 s
private:
    static void callback(void* buffer, unsigned int frames);
    void pull(int16_t* dst, int frames);
    bool open_ = false;
    std::vector<int16_t> ring_;
    int readPos_ = 0, writePos_ = 0, count_ = 0;
    mutable std::mutex mutex_;
    static AudioOutput* instance_;
    void* stream_ = nullptr; // raylib AudioStream (opaque here)
};

// Helpers shared with the IOP model.
// (rate << 12) * 0x57619f1 >> 42  == rate*4096/48000 (IOPSND L_05354).
uint32_t streamPitchFromRate(uint32_t rate);
// Synth pitch (see header comment). UNVERIFIED beyond the base-note case.
uint32_t synthPitch(uint32_t vagRate, int note, int baseNote, int transpose, int detune);

} // namespace baroque::audio
