#include "audio/spu.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace baroque::audio {

// ---------------------------------------------------------------- pitch helpers
uint32_t streamPitchFromRate(uint32_t rate) {
    // IOPSND L_05354..L_0537c: only the low 16 bits of the big-endian rate are used, then
    // (rate << 12) * 0x57619f1 (signed 32x32 -> 64), hi >> 10 minus sign  ==  product >> 42.
    const uint64_t r = rate & 0xffffu;
    const uint64_t product = (r << 12) * 0x57619f1ull;
    return static_cast<uint32_t>(product >> 42);
}

uint32_t synthPitch(uint32_t vagRate, int note, int baseNote, int transpose, int detune) {
    // UNVERIFIED: modhsyn/libsd sceSdNote2Pitch not traced. For note == baseNote (every split of
    // this bank) this is exactly vagRate*4096/48000 like the stream formula.
    const double semis = double(note - baseNote + transpose) + double(detune) / 128.0;
    const double ratio = double(vagRate) / 48000.0 * std::pow(2.0, semis / 12.0);
    double p = std::floor(ratio * 4096.0);
    if (p < 0) p = 0;
    if (p > 0x3fff) p = 0x3fff; // SPU2 pitch register is 14 bits
    return static_cast<uint32_t>(p);
}

// ---------------------------------------------------------------- MemoryVagSource
void MemoryVagSource::reset(const uint8_t* data, size_t len) {
    data_ = data;
    len_ = len;
    pos_ = 0;
    loopStart_ = SIZE_MAX;
    st_ = AdpcmState{};
    done_ = false;
}

bool MemoryVagSource::nextBlock(int16_t out[28], uint8_t& flags) {
    if (done_ || !data_ || pos_ + 16 > len_) { done_ = true; return false; }
    const uint8_t fl = data_[pos_ + 1];
    if (fl == kAdpcmFlagEndMute) { done_ = true; return false; } // flag 7: silent terminator
    if (fl & 4) loopStart_ = pos_;                                // flag bit2: loop start (LSAX)
    flags = decodeAdpcmBlock(data_ + pos_, st_, out);
    pos_ += 16;
    if (fl & 1) {
        if ((fl & 2) && loopStart_ != SIZE_MAX) {
            pos_ = loopStart_;      // flag 3: loop end -> jump to LSAX, keep playing
        } else {
            done_ = true;           // flag 1: end -> release/mute after this block
        }
    }
    return true;
}

// ---------------------------------------------------------------- SterPairSource
SterPairSource::SterPairSource() {
    left_.owner = this;
    left_.isRight = false;
    right_.owner = this;
    right_.isRight = true;
}

bool SterPairSource::pump() {
    if (ended_) return false;
    Block l{}, r{};
    if (!reader.nextPair(l.data(), r.data())) { ended_ = true; return false; }
    qL_.push_back(l);
    qR_.push_back(r);
    return true;
}

bool SterPairSource::View::nextBlock(int16_t out[28], uint8_t& flags) {
    SterPairSource& o = *owner;
    std::deque<Block>& q = isRight ? o.qR_ : o.qL_;
    if (q.empty() && !o.pump()) return false;
    std::memcpy(out, q.front().data(), sizeof(int16_t) * 28);
    q.pop_front();
    flags = kAdpcmFlagRepeat; // the IOP rewrites block flags for its SPU ring buffer
    return true;
}

// ---------------------------------------------------------------- SpuEnvelope
void SpuEnvelope::setRate(int shift, int step, bool exponential, bool decrease) {
    const int shl = std::max(0, 11 - shift);
    const int cyc = std::max(0, shift - 11);
    cycles_ = cyc >= 31 ? 0x7fffffff : (1 << cyc);
    const int mag = (step < 0 ? -step : step) << shl;
    stepValue_ = step < 0 ? -mag : mag;
    exp_ = exponential;
    decrease_ = decrease;
    counter_ = 0;
}

void SpuEnvelope::keyOn(uint16_t adsr1, uint16_t adsr2) {
    adsr1_ = adsr1;
    adsr2_ = adsr2;
    level_ = 0;
    phase_ = Attack;
    const int ar = (adsr1 >> 8) & 0x7f;
    setRate(ar >> 2, 7 - (ar & 3), (adsr1 & 0x8000) != 0, false);
}

void SpuEnvelope::enterSustain() {
    phase_ = Sustain;
    const int sr = (adsr2_ >> 6) & 0x7f;
    const bool dec = (adsr2_ & 0x4000) != 0;
    setRate(sr >> 2, dec ? -8 + (sr & 3) : 7 - (sr & 3), (adsr2_ & 0x8000) != 0, dec);
}

void SpuEnvelope::keyOff() {
    if (phase_ == Off) return;
    phase_ = Release;
    const int rr = adsr2_ & 0x1f;
    setRate(rr, -8, (adsr2_ & 0x20) != 0, true); // rate = Rr*4 -> shift = Rr
}

int32_t SpuEnvelope::step() {
    if (phase_ == Off) return 0;
    // Decay ends as soon as the level is at or below the sustain level ((Sl+1)*0x800). Checked before the
    // step: with Sl = 0xf (stream voices ADSR1 0x000f, bank ADSR1 0x80ff/0x80cf) the target 0x8000 is above
    // the maximum level, so the voice enters sustain without any decay step (psx-spx "Sustain Level").
    if (phase_ == Decay && level_ <= (((adsr1_ & 0xf) + 1) << 11)) enterSustain();
    int32_t cycles = cycles_;
    int32_t stepv = stepValue_;
    if (exp_ && !decrease_ && level_ > 0x6000) cycles = cycles > 0x1fffffff ? 0x7fffffff : cycles * 4;
    if (exp_ && decrease_) stepv = static_cast<int32_t>((int64_t(stepv) * level_) / 0x8000);
    if (++counter_ >= cycles) {
        counter_ = 0;
        level_ = std::clamp(level_ + stepv, 0, 0x7fff);
        switch (phase_) {
        case Attack:
            if (level_ >= 0x7fff) {
                phase_ = Decay;
                const int dr = (adsr1_ >> 4) & 0xf;
                setRate(dr, -8, true, true); // decay: always exponential decrease, rate = Dr*4
            }
            break;
        case Decay:
            if (level_ <= (((adsr1_ & 0xf) + 1) << 11)) enterSustain();
            break;
        case Release:
            if (level_ <= 0) { phase_ = Off; level_ = 0; }
            break;
        default:
            break;
        }
    }
    return level_;
}

// ---------------------------------------------------------------- SpuVoice
void SpuVoice::keyOn(BlockSource* src, uint32_t pitch, uint16_t adsr1, uint16_t adsr2) {
    src_ = src;
    pitch_ = pitch;
    fifoRead_ = fifoCount_ = 0;
    frac_ = 0;
    srcDone_ = endReached_ = pendingKeyOff_ = false;
    tailSamples_ = 0;
    prev_ = 0;
    keyedOn_ = true;
    env_.keyOn(adsr1, adsr2);
    fetchBlock();
    // prime: prev_ = sample[0], fifo front = sample[1]
    if (fifoCount_ > 0) {
        prev_ = fifo_[fifoRead_];
        fifoRead_ = (fifoRead_ + 1) & 63;
        --fifoCount_;
    }
}

void SpuVoice::keyOff() {
    keyedOn_ = false;
    env_.keyOff();
}

void SpuVoice::stop() {
    keyedOn_ = false;
    env_.stop();
    src_ = nullptr;
}

bool SpuVoice::fetchBlock() {
    if (!src_ || srcDone_) return false;
    if (fifoCount_ + 28 > 64) return true; // room check (never hit: we fetch one block at a time)
    int16_t tmp[28];
    uint8_t flags = 0;
    if (!src_->nextBlock(tmp, flags)) { srcDone_ = true; return false; }
    for (int i = 0; i < 28; ++i) {
        fifo_[(fifoRead_ + fifoCount_) & 63] = tmp[i];
        ++fifoCount_;
    }
    return true;
}

void SpuVoice::render(int32_t* mixL, int32_t* mixR, int frames) {
    if (!active()) return;
    for (int i = 0; i < frames; ++i) {
        if (env_.off()) break;
        // output the current position (sample index + 12-bit fraction), then advance by pitch/0x1000 samples:
        // the first output sample after key-on is source sample 0, and at pitch 0x1000 every source sample is
        // output exactly once.
        const int32_t cur = prev_;
        const int32_t nxt = fifoCount_ > 0 ? fifo_[fifoRead_] : cur;
        const int32_t s = cur + (((nxt - cur) * int32_t(frac_)) >> 12);
        const int32_t env = env_.step();
        const int32_t v = (s * env) >> 15;
        mixL[i] += (v * volL_) >> 14;
        mixR[i] += (v * volR_) >> 14;
        frac_ += pitch_;
        while (frac_ >= 0x1000) {
            frac_ -= 0x1000;
            if (fifoCount_ == 0 && !fetchBlock()) {
                // source exhausted: NAX passed the end
                if (!endReached_) { endReached_ = true; env_.stop(); }
                break;
            }
            prev_ = fifo_[fifoRead_];
            fifoRead_ = (fifoRead_ + 1) & 63;
            --fifoCount_;
            if (fifoCount_ == 0) fetchBlock();
        }
    }
}

// ---------------------------------------------------------------- SpuMixer
void SpuMixer::render(int16_t* out, int frames) {
    if (int(mixL_.size()) < frames) { mixL_.resize(size_t(frames)); mixR_.resize(size_t(frames)); }
    std::fill(mixL_.begin(), mixL_.begin() + frames, 0);
    std::fill(mixR_.begin(), mixR_.begin() + frames, 0);
    for (SpuVoice& v : voices_) v.render(mixL_.data(), mixR_.data(), frames);
    for (int i = 0; i < frames; ++i) {
        const int32_t l = std::clamp((mixL_[size_t(i)] * int32_t(mvolL_)) >> 14, -32768, 32767);
        const int32_t r = std::clamp((mixR_[size_t(i)] * int32_t(mvolR_)) >> 14, -32768, 32767);
        out[2 * i] = static_cast<int16_t>(l);
        out[2 * i + 1] = static_cast<int16_t>(r);
    }
}

// ---------------------------------------------------------------- AudioOutput
AudioOutput* AudioOutput::instance_ = nullptr;

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open() {
    if (open_) return true;
    if (!IsAudioDeviceReady()) return false;
    if (instance_) return false; // single output
    ring_.assign(size_t(kRingFrames) * 2, 0);
    readPos_ = writePos_ = count_ = 0;
    auto* s = new AudioStream(LoadAudioStream(kSpuRate, 16, 2));
    if (!IsAudioStreamValid(*s)) { delete s; return false; }
    stream_ = s;
    instance_ = this;
    SetAudioStreamCallback(*s, &AudioOutput::callback);
    PlayAudioStream(*s);
    open_ = true;
    return true;
}

void AudioOutput::close() {
    if (!open_) return;
    auto* s = static_cast<AudioStream*>(stream_);
    StopAudioStream(*s);
    UnloadAudioStream(*s);
    delete s;
    stream_ = nullptr;
    instance_ = nullptr;
    open_ = false;
}

void AudioOutput::push(const int16_t* stereo, int frames) {
    if (!open_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < frames; ++i) {
        if (count_ >= kRingFrames) break; // overrun: drop (tick pacing keeps this from happening)
        ring_[size_t(writePos_) * 2] = stereo[2 * i];
        ring_[size_t(writePos_) * 2 + 1] = stereo[2 * i + 1];
        writePos_ = (writePos_ + 1) % kRingFrames;
        ++count_;
    }
}

int AudioOutput::queuedFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

void AudioOutput::pull(int16_t* dst, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < frames; ++i) {
        if (count_ > 0) {
            dst[2 * i] = ring_[size_t(readPos_) * 2];
            dst[2 * i + 1] = ring_[size_t(readPos_) * 2 + 1];
            readPos_ = (readPos_ + 1) % kRingFrames;
            --count_;
        } else {
            dst[2 * i] = dst[2 * i + 1] = 0; // underrun: silence
        }
    }
}

void AudioOutput::callback(void* buffer, unsigned int frames) {
    if (instance_) instance_->pull(static_cast<int16_t*>(buffer), int(frames));
}

} // namespace baroque::audio
