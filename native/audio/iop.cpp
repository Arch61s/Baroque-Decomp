#include "audio/iop.hpp"
#include <algorithm>
#include <cstring>

namespace baroque::audio {

// IOPSND.IRX .data 0x6258 (101 entries): volume percent -> curve percent.
const int32_t kIopVolumeCurve[101] = {
    0, 0, 0, 0, 0, 10, 20, 30, 40, 50, 60, 67, 67, 67, 67, 67, 68, 68, 68, 68, 68, 69, 69, 69, 69, 69,
    70, 70, 70, 70, 70, 71, 71, 71, 71, 71, 72, 72, 72, 72, 72, 73, 73, 73, 73, 73, 74, 74, 74, 74, 74,
    75, 75, 75, 75, 76, 76, 76, 76, 77, 77, 77, 77, 78, 78, 78, 78, 79, 79, 79, 79, 80, 80, 80, 81, 81,
    81, 82, 82, 82, 83, 83, 84, 84, 85, 85, 86, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99};

// IOPSND.IRX .data 0x5f00 (100 entries): game pan 0..99 -> modsein pan 0..126.
const int32_t kIopPanTable[100] = {
    0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 64, 66, 68, 70, 72, 74, 76,
    78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
    113, 114, 115, 116, 117, 117, 118, 118, 119, 119, 120, 120, 121, 121, 122, 122, 123, 124, 125, 126};

// IOPSND.IRX .data 0x6244: SPU core-1 voice bound to each stream slot (EE sends the same via 0x980+slot).
const int32_t kIopStreamVoice[5] = {18, 20, 21, 22, 23};

namespace {
constexpr uint16_t kStreamAdsr1 = 0x000f; // stream voices (SOUND.md §2)
constexpr uint16_t kStreamAdsr2 = 0x1fc1;
constexpr uint32_t kDeferredVolume = 0x7b;
} // namespace

// ---------------------------------------------------------------- setup
void IopSound::init(const SfsArchive* sfs, const HdBank* bank) {
    sfs_ = sfs;
    bank_ = bank;
    reinit();
    for (int i = 0; i < kStreamSlots; ++i) {
        sources_[size_t(i)].owner = this;
        sources_[size_t(i)].sfs = sfs;
    }
}

void IopSound::reinit() {
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& s = streams_[size_t(i)];
        if (s.handle) { mixer_.voice(s.voice).stop(); if (s.flags & 0x100) mixer_.voice(s.voice + 1).stop(); }
        s = StreamSlot{};
        s.voice = kIopStreamVoice[i];
    }
    for (int i = 0; i < kSynthSlots; ++i) {
        mixer_.voice(SpuMixer::kSynthVoiceBase + i).stop();
        synths_[size_t(i)] = SynthSlot{};
    }
    suspended_ = false;
}

void IopSound::archiveReopened() {
    for (int i = 0; i < kSynthSlots; ++i) {
        mixer_.voice(SpuMixer::kSynthVoiceBase + i).stop();
        synths_[size_t(i)] = SynthSlot{};
    }
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& s = streams_[size_t(i)];
        if (!s.handle) continue;
        if (s.vag) continue;   // mono VAG: already in memory
        const uint32_t entry = s.handle & 0xffff;
        EntrySource& src = sources_[size_t(i)];
        if (s.ster && sfs_ && entry < sfs_->entryCount() && sfs_->span(entry).size == src.entrySize) {
            src.base = sfs_->span(entry).offset;
            continue;
        }
        mixer_.voice(s.voice).stop();
        if (s.flags & 0x100) mixer_.voice(s.voice + 1).stop();
        const int voice = s.voice;
        s = StreamSlot{};
        s.voice = voice;
    }
}

void IopSound::setMasterVolume(uint16_t l, uint16_t r) { mixer_.setMasterVolume(l, r); }

void IopSound::setEffect(uint32_t idx, uint32_t modeFlags, int depthL, int depthR, int delay, int feedback, int vol) {
    // func 0x17ec: sceSdSetEffectAttr on both cores with EFFECT_ENABLE, EVOLL/R = vol. Hook only (dry).
    (void)idx;
    effect_.mode = modeFlags & 0xff;
    effect_.enabled = (modeFlags & 0x100) != 0 && effect_.mode != 0;
    effect_.depthL = depthL;
    effect_.depthR = depthR;
    effect_.delay = delay;
    effect_.feedback = feedback;
    effect_.volume = vol;
    mixer_.setEffect(effect_);
}

void IopSound::bindStreamVoice(int slot, int voice) {
    if (slot < 0 || slot >= kStreamSlots) return;
    streams_[size_t(slot)].voice = voice; // func 0x3908: zero VOLL/R, set ADSR and SSA for the voice
}

// ---------------------------------------------------------------- volume law (func 0x346c)
void IopSound::volumeLaw(int vol, int pan, bool stereoStream, bool stereoMode, uint32_t& voice0, uint32_t& voice1) {
    const int v = std::clamp(vol, 0, 100);
    const int lin = v * 0x3ff0 / 100;                // vol% * 0x3ff0 / 100
    const int curve = kIopVolumeCurve[v];
    uint32_t l0, r0, l1 = 0, r1 = 0;
    if (stereoStream) {
        if (stereoMode) {
            l0 = uint32_t(lin * 100 * curve / 10000); // = lin*curve/100
            r0 = uint32_t(lin * curve / 10000);       // = gain/100
            l1 = r0;                                   // mirrored for the R voice (SOUND.md §4)
            r1 = l0;
        } else {
            l0 = r0 = l1 = r1 = uint32_t(lin * 50 * curve / 10000); // gain/2 everywhere
        }
    } else {
        int fl = 100 - pan;
        if (fl <= 0) fl = 1;
        else if (fl >= 50) fl = 50;
        int fr = pan;
        if (fr <= 0) fr = 1;
        else if (fr >= 50) fr = 50;
        l0 = uint32_t(lin * fl * (curve * 2) / 10000);
        r0 = uint32_t(lin * fr * (curve * 2) / 10000);
    }
    voice0 = ((l0 & 0xffff) << 16) | (r0 & 0xffff);
    voice1 = ((l1 & 0xffff) << 16) | (r1 & 0xffff);
}

void IopSound::applyStreamVolume(StreamSlot& s) {
    if (!s.volumeAllowed) return; // deferred (0x7b): +0x2a == 0, nothing applied until 0x950
    uint32_t v0 = 0, v1 = 0;
    volumeLaw(s.vol, s.pan, (s.flags & 0x100) != 0, stereoMode_, v0, v1);
    mixer_.voice(s.voice).setVolume(int32_t(v0 >> 16), int32_t(v0 & 0xffff));
    if (s.flags & 0x100) mixer_.voice(s.voice + 1).setVolume(int32_t(v1 >> 16), int32_t(v1 & 0xffff));
}

// ---------------------------------------------------------------- streams
size_t IopSound::EntrySource::readEntry(uint64_t offset, void* dst, size_t size) {
    if (owner) ++owner->readCounter_; // gp-0x7f44 increments once per reader invocation
    return sfs ? sfs->readAt(base + offset, dst, size) : 0;
}

void IopSound::streamStart(int slot, uint32_t idFlags, uint32_t vol, uint32_t pan) {
    if (slot < 0 || slot >= kStreamSlots || !sfs_) return;
    StreamSlot& s = streams_[size_t(slot)];
    const int voice = s.voice;
    // 0x31a4: 0xd10f0[slot] = -1, 0xd1130[slot] = 0 then func 0x3008 (re)initialises the slot.
    s = StreamSlot{};
    s.voice = voice;
    s.looped = false;
    const uint32_t entry = idFlags & 0xffff;
    if (entry >= sfs_->entryCount()) return;
    const SfsArchive::Span span = sfs_->span(entry);
    // func 0x3008: +0x14 = vol (raw), +0x18 = pan, +0x8 = 1, +0x20/+4/+0x48/+0x4c = 0
    s.vol = int(vol);
    s.pan = int(pan);
    s.volTarget = 0;
    s.volPeriod = 0;
    s.panPeriod = 0;
    if (idFlags & 0x10000) s.flags |= 0x100;   // stereo
    if (idFlags & 0x20000) s.flags |= 0x1000;  // no-loop (never sent by this EE)
    s.volumeAllowed = vol != kDeferredVolume;  // 0x322c..: +0x2a = 0 when vol == 0x7b

    bool ok = false;
    uint8_t head[48];
    if (sfs_->readAt(span.offset, head, sizeof head) == sizeof head) {
        if (std::memcmp(head, "STER", 4) == 0) {
            SterHeader hdr;
            if (parseSterHeader(head, sizeof head, hdr)) {
                s.ster = std::make_unique<SterPairSource>();
                EntrySource& src = sources_[size_t(slot)];
                src.base = span.offset;
                src.entrySize = span.size;
                // reader 0x5170 first invocation: readCounter++ then (vol == 1) -> random start
                ++readCounter_;
                int k = 0;
                if (s.vol == 1) k = int(readCounter_ & 7) + 1;
                s.ster->reader.start(&src, hdr, span.size, k);
                s.pitch = streamPitchFromRate(hdr.sampleRate);
                ok = true;
            }
        } else if (std::memcmp(head, "VAGp", 4) == 0) {
            VagHeader vh;
            if (parseVagHeader(head, sizeof head, vh)) {
                // UNVERIFIED: the mono VAG path streams dataSize bytes once (no loop), pitch from the
                // header rate like the STER path.
                const size_t n = std::min<size_t>(vh.dataSize, span.size - kVagHeaderSize);
                s.vagData.resize(n);
                sfs_->readAt(span.offset + kVagHeaderSize, s.vagData.data(), n);
                s.vag = std::make_unique<MemoryVagSource>(s.vagData.data(), s.vagData.size());
                s.pitch = streamPitchFromRate(vh.sampleRate);
                ++readCounter_;
                ok = true;
            }
        }
    }
    if (!ok) return;
    s.handle = 0x10000u | entry;  // 0xd10f0[slot] = nonzero -> "started"
    s.primed = true;
    if (s.volumeAllowed) {
        // key on: ADSR1 0x000f / ADSR2 0x1fc1, pitch = rate*4096/48000
        if (s.ster) {
            mixer_.voice(s.voice).keyOn(&s.ster->left(), s.pitch, kStreamAdsr1, kStreamAdsr2);
            if (s.flags & 0x100) mixer_.voice(s.voice + 1).keyOn(&s.ster->right(), s.pitch, kStreamAdsr1, kStreamAdsr2);
        } else {
            mixer_.voice(s.voice).keyOn(s.vag.get(), s.pitch, kStreamAdsr1, kStreamAdsr2);
        }
        s.keyedOn = true;
        applyStreamVolume(s);
    }
}

void IopSound::streamSetVolume(int slot, uint32_t vol) {
    if (slot < 0 || slot >= kStreamSlots) return;
    StreamSlot& s = streams_[size_t(slot)];
    // func 0x3270 (dispatcher case 0x950 at 0x480): +0x14 = vol, +0x2c = 2, flags |= 1. The stream thread
    // (L_04bfc) then keys the voice on (func 0x428c) and applies the volume. Pan (+0x18) is not touched.
    s.vol = int(vol);
    if (!s.handle) return;
    if (!s.volumeAllowed) {
        // UNVERIFIED: modelled synchronously; on the IOP the key-on happens on the stream thread's next pass.
        s.volumeAllowed = true;
        if (s.ster) {
            mixer_.voice(s.voice).keyOn(&s.ster->left(), s.pitch, kStreamAdsr1, kStreamAdsr2);
            if (s.flags & 0x100) mixer_.voice(s.voice + 1).keyOn(&s.ster->right(), s.pitch, kStreamAdsr1, kStreamAdsr2);
        } else if (s.vag) {
            mixer_.voice(s.voice).keyOn(s.vag.get(), s.pitch, kStreamAdsr1, kStreamAdsr2);
        }
        s.keyedOn = true;
    }
    applyStreamVolume(s);
}

void IopSound::streamStop(int slot) {
    if (slot < 0 || slot >= kStreamSlots) return;
    StreamSlot& s = streams_[size_t(slot)];
    s.looped = false;                      // 0xd1130[slot] = 0
    if (!s.handle) return;
    if (s.keyedOn) {
        mixer_.voice(s.voice).keyOff();    // sceSdSetSwitch(core1 KOFF, voice bits)
        if (s.flags & 0x100) mixer_.voice(s.voice + 1).keyOff();
        s.keyedOn = false;                 // 0xd1110[slot] = 0
    }
    s.panPeriod = 0;                       // +0x20 = 0
    s.volPeriod = 0;                       // +0x4c = 0
    s.flags |= 1;                          // stop flag -> slot freed by the stream thread
    s.stopPending = true;
}

void IopSound::streamFade(int slot, int vol, int time, int pan, int panTime) {
    if (slot < 0 || slot >= kStreamSlots) return;
    StreamSlot& s = streams_[size_t(slot)];
    // func 0x3ab0
    s.looped = false;                      // 0xd1130[slot] = 0
    s.volTarget = vol;                     // +0x48
    s.volPeriod = time + 1;                // +0x4c
    if (s.volCount == 0) s.volCount = 1;   // +0x50
    s.panTarget = pan;                     // +0x1c
    s.panPeriod = panTime + 1;             // +0x20
    if (s.panCount == 0) s.panCount = 1;   // +0x24
}

void IopSound::streamPause(int slot, uint32_t v) {
    if (slot < 0 || slot >= kStreamSlots) return;
    StreamSlot& s = streams_[size_t(slot)];
    // func 0x338c: v == 0 -> PITCH = 0 and flag 0x10000; else restore the header pitch.
    // Only the slot's first voice is touched (literal).
    if (v == 0) {
        s.flags |= 0x10000;
        mixer_.voice(s.voice).setPitch(0);
    } else {
        s.flags &= ~0x10000u;
        mixer_.voice(s.voice).setPitch(s.pitch);
    }
}

void IopSound::freeStream(int slot) {
    StreamSlot& s = streams_[size_t(slot)];
    mixer_.voice(s.voice).stop();
    if (s.flags & 0x100) mixer_.voice(s.voice + 1).stop();
    const int voice = s.voice;
    s = StreamSlot{};
    s.voice = voice;
}

// func 0x3b1c / 0x2a8c common step: every `period` ticks move `cur` 2 toward `target`;
// reaching it clears the period. Returns true when a step happened.
bool IopSound::fadeStep(int& cur, int target, int& period, int& count) {
    if (period == 0) return false;
    if (cur == target) return false;
    if (--count != 0) return false;
    count = period;
    if (cur < target) {
        cur += 2;
        if (target < cur) { cur = target; period = 0; }
    } else {
        cur -= 2;
        if (cur < target) { cur = target; period = 0; }
    }
    if (period != 0) count = period;
    return true;
}

void IopSound::fadeTickStream(StreamSlot& s, int slot) {
    bool changed = false;
    if (fadeStep(s.pan, s.panTarget, s.panPeriod, s.panCount)) changed = true;
    if (fadeStep(s.vol, s.volTarget, s.volPeriod, s.volCount)) changed = true;
    if (changed) {
        if (s.vol == 0) {
            // L_03c98: volume reached 0 -> +0x2e = -1, flags |= 1 (stop)
            s.flags |= 1;
            s.stopPending = true;
            if (s.keyedOn) {
                mixer_.voice(s.voice).keyOff();
                if (s.flags & 0x100) mixer_.voice(s.voice + 1).keyOff();
                s.keyedOn = false;
            }
        } else {
            applyStreamVolume(s); // func 0x37a0 -> 0x346c
        }
    }
    (void)slot;
}

// ---------------------------------------------------------------- synth SE (modsein/modhsyn)
void IopSound::applySynthVolume(SynthSlot& s, int idx) {
    // UNVERIFIED (modhsyn internals not traced): amplitude = vol/127 * prog/127 * split/127 *
    // sample/127, SPU pan p (0..126): L = min(127-p, 64)/64, R = min(p, 64)/64.
    SpuVoice& v = mixer_.voice(SpuMixer::kSynthVoiceBase + idx);
    if (!s.res) { v.setVolume(0, 0); return; }
    const double amp = (double(std::clamp(s.vol, 0, 127)) / 127.0) * (double(s.res->progVolume) / 127.0) *
                       (double(s.res->splitVolume) / 127.0) * (double(s.res->volume) / 127.0);
    const int p = std::clamp(s.pan, 0, 126);
    const double l = double(std::min(127 - p, 64)) / 64.0;
    const double r = double(std::min(p, 64)) / 64.0;
    v.setVolume(int32_t(amp * l * 0x3fff), int32_t(amp * r * 0x3fff));
}

void IopSound::synthPlay(int seqSlot, uint32_t prog, uint32_t volNoteWord, uint32_t handle, uint32_t pan) {
    if (seqSlot < 0 || seqSlot >= kSynthSlots) return;
    SynthSlot& s = synths_[size_t(seqSlot)];
    // func 0x2480
    uint32_t vol = volNoteWord >> 16;
    const uint32_t duration = volNoteWord & 0xff;          // EE table field +0xc (tenths of a second)
    const uint32_t note = (volNoteWord >> 8) & 0xff;
    if (vol < 3) vol = 0;
    const int spuPan = int(kIopPanTable[std::min<uint32_t>(pan, 99)]); // lbu 0x5f00[pan*4]
    // UNVERIFIED: pan index 100 (EE can send (1+dot)*50 == 100) reads the byte after the table.
    s.handle = handle;
    s.prog = int(prog & 0x7f);
    s.note = int(note & 0x7f);
    s.pan = spuPan;        // +0x18
    s.panTarget = spuPan;  // +0x1c
    s.panPeriod = 0;       // +0x20
    s.vol = int(vol);      // +0x28
    s.volTarget = s.vol;
    s.volPeriod = 0;
    s.volCount = 0;
    s.panCount = 0;
    s.countdown = duration ? int(duration) * 24 + 1 : -1; // +0x00
    // modsein:7(env, slot, handle, vol<<24 | note<<16 | prog<<8 | 0xa0, pan) -> modhsyn note on
    SpuVoice& v = mixer_.voice(SpuMixer::kSynthVoiceBase + seqSlot);
    s.res = bank_ ? bank_->resolve(s.prog, s.note) : std::nullopt;
    if (s.res) {
        uint32_t size = 0;
        const uint8_t* data = bank_->vagData(s.res->vag, &size);
        if (data) {
            s.src = std::make_unique<MemoryVagSource>(data, size);
            const int transpose = int(int8_t(s.res->splitTranspose)) + int(int8_t(s.res->progTranspose));
            const int detune = int(int8_t(s.res->sampleDetune)) + int(int8_t(s.res->splitDetune)) + int(int8_t(s.res->progDetune));
            v.keyOn(s.src.get(), synthPitch(s.res->rate, s.note, s.res->baseNote, transpose, detune), s.res->adsr1, s.res->adsr2);
            applySynthVolume(s, seqSlot);
        }
    }
    // 0x25b4: vol < 5 -> fade command (slot, vol, 2, 0x32, 0)
    if (vol < 5) synthFade(seqSlot, int(vol), 2, 0x32, 0);
}

void IopSound::synthStop(int seqSlot) {
    if (seqSlot < 0 || seqSlot >= kSynthSlots) return;
    SynthSlot& s = synths_[size_t(seqSlot)];
    // func 0x26b8
    if (s.countdown != 0) {
        if (s.vol >= 5) {
            mixer_.voice(SpuMixer::kSynthVoiceBase + seqSlot).keyOff(); // modsein:11 (note off)
            s.countdown = 0x10;
        } else {
            s.countdown = 1;
        }
    }
    s.panPeriod = -1; // +0x20 = -1: further 0x450 fades ignored
}

void IopSound::synthFade(int seqSlot, int vol, int time, int pan, int panTime) {
    if (seqSlot < 0 || seqSlot >= kSynthSlots) return;
    SynthSlot& s = synths_[size_t(seqSlot)];
    // func 0x2a04
    if (s.panPeriod == -1) return;
    if (s.vol == vol) {        // force a re-apply: cur = target+1, countdown 2
        s.vol = vol + 1;
        s.volCount = 2;
    }
    s.volTarget = vol;         // +0x2c
    s.volPeriod = time + 1;    // +0x30
    if (s.volCount == 0) s.volCount = 1;
    s.panTarget = pan;         // +0x1c (game pan units here, literal)
    s.panPeriod = panTime + 1; // +0x20
    if (s.panCount == 0) s.panCount = 1;
}

void IopSound::fadeTickSynth(SynthSlot& s, int idx) {
    if (s.panPeriod < 0) return;
    bool changed = false;
    if (fadeStep(s.pan, s.panTarget, s.panPeriod, s.panCount)) changed = true;
    if (fadeStep(s.vol, s.volTarget, s.volPeriod, s.volCount)) changed = true;
    if (changed) applySynthVolume(s, idx); // modhsyn:10 port volume
}

void IopSound::synthExpire(int idx) {
    // func 0x27d4: modsein:7(note off, velocity 0) + modhsyn:13(all notes off on the port), +0 = 0
    // UNVERIFIED: modelled as an immediate voice stop; whether modhsyn's all-notes-off releases through the
    // sample's ADSR2 release rate (0x5fcf: Rr 0xf) instead was not traced.
    SynthSlot& s = synths_[size_t(idx)];
    mixer_.voice(SpuMixer::kSynthVoiceBase + idx).stop();
    s.countdown = 0;
}

// ---------------------------------------------------------------- tick (0xef0) and status (0x1288)
void IopSound::tick() {
    if (suspended_) return;
    // stream thread / NAX polling (func 0x42ec): detect ended voices, free stopped slots
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& s = streams_[size_t(i)];
        if (!s.handle) continue;
        const SpuVoice& v0 = mixer_.voice(s.voice);
        const bool stereo = (s.flags & 0x100) != 0;
        bool anyActive = v0.active() || (stereo && mixer_.voice(s.voice + 1).active());
        if (s.ster && s.ster->reader.looped()) s.looped = true;      // 0xd1130[slot] -> status word 3
        if (s.stopPending) {
            if (!anyActive) freeStream(i);
            continue;
        }
        if (s.keyedOn && v0.endReached()) {
            // stream ended (loop == -1 or VAG): key off, free the slot
            mixer_.voice(s.voice).keyOff();
            if (stereo) mixer_.voice(s.voice + 1).keyOff();
            s.keyedOn = false;
            s.stopPending = true;
            if (!anyActive) freeStream(i);
        }
    }
    // status loop 0x1190..0x1284
    for (int i = 0; i < kSynthSlots; ++i) {
        SynthSlot& s = synths_[size_t(i)];
        if (s.countdown == 0) continue;
        if (s.countdown == 1) { synthExpire(i); continue; }
        if (s.countdown != -1) --s.countdown;
        fadeTickSynth(s, i);
    }
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& s = streams_[size_t(i)];
        if (s.handle) fadeTickStream(s, i); // only started slots tick (0x1210)
    }
}

IopStatus IopSound::status() const {
    IopStatus st;
    for (int i = 0; i < kSynthSlots; ++i) {
        const SynthSlot& s = synths_[size_t(i)];
        if (s.countdown != 0 && s.countdown != 1) st.word[0] |= 1u << (2 + i);
    }
    for (int i = 0; i < kStreamSlots; ++i) {
        const StreamSlot& s = streams_[size_t(i)];
        const uint32_t bit = 1u << (24 + i);
        if (s.handle) st.word[0] |= bit;
        if (s.keyedOn) st.word[1] |= bit;
        if (s.primed) st.word[2] |= bit;
        if (s.looped) st.word[3] |= bit;
    }
    if (suspended_) st.word[2] |= 1;
    return st;
}

IopSound::StreamSlotView IopSound::streamSlot(int slot) const {
    const StreamSlot& s = streams_[size_t(slot)];
    return {s.handle != 0, s.keyedOn, s.primed, s.looped, (s.flags & 0x10000) != 0,
            s.vol, s.pan, s.volTarget, s.volPeriod, s.panTarget};
}

IopSound::SynthSlotView IopSound::synthSlot(int idx) const {
    const SynthSlot& s = synths_[size_t(idx)];
    return {s.countdown, s.vol, s.pan, s.volTarget, mixer_.voice(SpuMixer::kSynthVoiceBase + idx).active()};
}

} // namespace baroque::audio
