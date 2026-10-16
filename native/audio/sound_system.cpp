#include "audio/sound_system.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace baroque::audio {

#include "audio/sound_tables.inc"

namespace {
// Addresses of the two EE parameter buffers handed to FUN_00212540 (cmd & 0x1000 passes a buffer address).
constexpr uint32_t kBuf540580 = 0x540580;
constexpr uint32_t kBuf540640 = 0x540640;
constexpr uint32_t kBuf540600 = 0x540600;
// FUN_0022a2a0(0x8800) result (sceSifAllocIopHeap). The value is only forwarded to the IOP as an address and never
// dereferenced by the model; 0 stands for "the IOP heap block".
constexpr uint32_t kIopHeapBlock = 0;

float f32(uint32_t bits) { return std::bit_cast<float>(bits); }

// FUN_00206ab0: normalise in place (zero vector stays zero).
void normalize3(float v[3]) {
    const float sq = v[2] * v[2] + v[0] * v[0] + v[1] * v[1];
    float len = 0.0f;
    if (sq != 0.0f) len = std::sqrt(sq);
    if (len == 0.0f) {
        v[2] = 0.0f; v[1] = 0.0f; v[0] = 0.0f;
    } else {
        v[0] = v[0] / len; v[1] = v[1] / len; v[2] = v[2] / len;
    }
}

// PORT diagnostics (not original): BAROQUE_SE_LOG=1 logs every SE request (FUN_00115390 / FUN_00115470) with its
// distance attenuation, listener area and the returned handle (0 = not started).
bool seLogOn() {
    static const bool on = [] {
#ifdef _MSC_VER
        char* e = nullptr;
        size_t n = 0;
        if (_dupenv_s(&e, &n, "BAROQUE_SE_LOG") != 0 || e == nullptr) return false;
        const bool v = e[0] != '\0' && e[0] != '0';
        std::free(e);
        return v;
#else
        const char* e = std::getenv("BAROQUE_SE_LOG");
        return e != nullptr && e[0] != '\0' && e[0] != '0';
#endif
    }();
    return on;
}
void seLog(const char* kind, int seId, float vol, float att, uint32_t area, uint32_t listenerArea, long h) {
    if (!seLogOn()) return;
    char buf[160];
    std::snprintf(buf, sizeof buf, "[se] %s id 0x%x vol %.2f att %.3f area %d/%d -> handle 0x%08lx", kind, unsigned(seId),
                  double(vol), double(att), int(area), int(listenerArea), static_cast<unsigned long>(h));
    platform::log(buf);
}
} // namespace

float volumeStep(int step, int category) {
    if (step < 0 || step > 10 || category < 0 || category > 2) return 0.0f;
    return f32(kVolumeTableBits[step][category]);
}

uint16_t floatToU16(float f) {
    // `if (2.1474836e+09 <= f) f -= 2.1474836e+09; (int)f & 0xffff` (cvt.w.s truncates toward zero)
    if (2.1474836e+09f <= f) f -= 2.1474836e+09f;
    return static_cast<uint16_t>(static_cast<int32_t>(f) & 0xffff);
}

SoundSystem::SoundSystem() {
    for (int i = 0; i < kSlots; ++i) slots_[size_t(i)].id = static_cast<uint16_t>(0xaa00 + i);
    std::memset(seIdMap_, 0xff, sizeof seIdMap_);
}

SoundSystem::~SoundSystem() { shutdown(); }

void SoundSystem::shutdown() { output_.close(); }

void SoundSystem::buf640Set16(int off, uint16_t v) {
    buf640_[size_t(off)] = uint8_t(v);
    buf640_[size_t(off) + 1] = uint8_t(v >> 8);
}
void SoundSystem::buf640Set32(int off, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf640_[size_t(off + i)] = uint8_t(v >> (8 * i));
}
uint32_t SoundSystem::buf640Get32(int off) const {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(buf640_[size_t(off + i)]) << (8 * i);
    return v;
}

// ===================================================================================== host object 0x4e9790

// orig FUN_00114f30 (work/decompiled/00114f30.c)
bool SoundSystem::init(bool openDevice, int fromCd, int p3, int p4) {
    error_.clear();
    fromCd_ = fromCd;
    p3_ = p3;
    p4_ = p4;
    hostRequestReset();          // FUN_0020fc20(obj)
    command(0, uint32_t(fromCd));
    if (!sfs_.isOpen()) {        // port: the PS2 has no failure path (the IOP would hang on a missing file)
        if (error_.empty()) error_ = "BRQSOUND.SFS not found at " + soundArchivePath().string();
        return false;
    }
    command(1);
    command(0x10, 1, 0);
    field4a8_ = 8;
    bgmCat_ = f32(0x3f19999a);   // 0.6
    seCat_ = f32(0x3f666667);    // 0.9
    voiceCat_ = f32(0x3f333333); // 0.7
    resetHost();
    // FUN_0020fc90 -> FUN_00212790: RPC 0x8f00 (IOP memory statistics, debug output only).
    rpc(0x8f00, 0);
    if (openDevice && !output_.open()) platform::log("[audio] audio device not ready; running silent");
    return true;
}

// orig FUN_00114ea0 (work/decompiled/00114ea0.c)
void SoundSystem::resetHost() {
    listenerPos_[0] = 0.0f;
    listenerPos_[1] = 0.0f;
    listenerPos_[2] = f32(0xc1200000);   // -10.0
    listenerRight_[0] = 0.0f;
    listenerRight_[1] = 0.0f;
    listenerRight_[2] = f32(0x3f800000); // 1.0
    for (int i = 0; i < kSlots; ++i) tracked_[i] = 0;   // +0x360 .. +0x394 (2 x 7 words)
    voiceHandle_ = 0;
    voiceDeferredVol_ = 0;
    voiceVol_ = 0.0f;
    currentBgmId_ = -1;
    bgmScaleApplied_ = 1.0f;
    bgmScaleTarget_ = 1.0f;
    seOverride_ = 0;
    seOverrideFlag_ = 0;
    seOverrideVol_ = 1.0f;
    seOverrideTarget_ = 1.0f;
}

// orig FUN_0020fc20 (work/decompiled/0020fc20.c): host request record at obj+0 (+0x1c.. 3 x {0, 999} and
// +0x34.. 0x20 x 0x18-byte records are cleared too; nothing in this module reads them).
void SoundSystem::hostRequestReset() {
    bgmReqId_ = 0;
    bgmReqHandle_ = 0;
    lastSeHandle_ = 0;
}

// orig FUN_00114fd0 (work/decompiled/00114fd0.c)
void SoundSystem::enable() {
    if (enabled_ == 0) {
        resetHost();
        enabled_ = 1;
        setReverbVolume(0);
    }
}

// orig FUN_00114dd0 (work/decompiled/00114dd0.c)
void SoundSystem::disable() {
    enabled_ = 0;
    resetHost();
}

// orig FUN_00115020 (work/decompiled/00115020.c)
void SoundSystem::sceneChangeStop(int kind) {
    if (kind == 2 || kind == 1) stopBgm(0x14);
    else if (kind == 0) stopBgm(0x10);
    curBgmEntry_ = 0;
    prevBgmEntry_ = 0;
}

// orig FUN_001150b0 (work/decompiled/001150b0.c)
void SoundSystem::tick() {
    if (enabled_ != 0) {
        // The listener (FUN_00116220 with the camera task 0x50b624 +0x7c/+0x58, or the event manager 0x51b620
        // +0x40/+0x1c while an event runs) and the listener area (+0x334 from the player's cell) come from the game
        // through the listener source (setListener() / setListenerArea()).
        if (listenerSource_) listenerSource_(*this);
        if (seOverride_ == 0) {
            for (int i = 0; i < kSlots; ++i) {
                if (tracked_[i] != 0 && updateSe3d(trackedVol_[i], tracked_[i], trackedPos_[i]) == 0) tracked_[i] = 0;
            }
        } else if (seOverrideVol_ != seOverrideTarget_) {
            for (int i = 0; i < kSlots; ++i) {
                const SoundSlot& s = slots_[size_t(i)];
                if ((s.descFlags & 0x200000) == 0) fadeHandle(s.handle, floatToU16(seOverrideVol_), 2, 0x32, 0);
            }
            seOverrideVol_ = seOverrideTarget_;
        }
        if (bgmScaleApplied_ != bgmScaleTarget_ && bgmHandle_ != 0) {
            bgmFadeTo(floatToU16(bgmScaleTarget_ * 100.0f), bgmScaleTime_);
            bgmScaleApplied_ = bgmScaleTarget_;
        }
        command(0xf);
    }

    // IOP: 8 ticks of the 240 Hz timer thread per 1/30 s frame, each followed by 200 frames of SPU output.
    tickAudio_.clear();
    iopTicksLast_ = 0;
    if (!iopInited_) return;
    for (int i = 0; i < 8; ++i) runIopTick();
    // Port pacing (not original): keep at least two frames of audio queued in the device ring buffer.
    if (output_.isOpen() && output_.queuedFrames() < 2 * 8 * kIopTickFrames) runIopTick();
    if (output_.isOpen()) output_.push(tickAudio_.data(), int(tickAudio_.size() / 2));
}

void SoundSystem::runIopTick() {
    iop_.tick();
    const size_t base = tickAudio_.size();
    tickAudio_.resize(base + size_t(kIopTickFrames) * 2);
    iop_.render(tickAudio_.data() + base, kIopTickFrames);
    ++iopTicksLast_;
}

// orig FUN_00115390 (work/decompiled/00115390.c)
uint32_t SoundSystem::playSe(float vol, int seId) {
    if (enabled_ == 0) return 0;
    float f = 1.0f;
    if (seOverride_ != 0 && seOverrideFlag_ == 0) f = seOverrideVol_;
    uint16_t v;
    if (f == 0.0f) v = 4;
    else v = floatToU16(100.0f * (f * vol));
    // FUN_0020ff40: cmd 6 {id, vol, pan 0x32, pitch 0}; the handle is kept at +0x18
    lastSeHandle_ = uint32_t(command(6, uint32_t(seId + 1), v, 0x32, 0));
    seLog("2d", seId, vol, 1.0f, 0xffffffffu, listenerArea_, long(lastSeHandle_));   // PORT diagnostics
    return lastSeHandle_;
}

// orig FUN_00124c70 is the game's area lookup; the 0xf000 == 0x1000 test is FUN_00115470's.
uint32_t SoundSystem::areaOf(const float pos[3]) {
    uint32_t word = 0;
    if (!areaLookup_ || !areaLookup_(pos, word)) return 0xffffffffu;
    if ((word & 0xf000) == 0x1000) return word & 0xf;
    return 0xffffffffu;
}

// orig FUN_00115470 (work/decompiled/00115470.c)
uint32_t SoundSystem::playSe3d(float vol, int seId, const float pos[3]) {
    if (enabled_ == 0) return 0;
    float ovr = 1.0f;
    if (seOverride_ != 0 && seOverrideFlag_ == 0) ovr = seOverrideVol_;
    // FUN_00246d18 (float -> double) + FUN_0040cc50 (double >=): volumes >= 100 mean "forced + tracked".
    const bool force = double(vol) >= 100.0;
    if (force) vol = vol - 100.0f;
    const float dx = listenerPos_[0] / 10.0f - pos[0] / 10.0f;
    const float dy = listenerPos_[1] / 10.0f - pos[1] / 10.0f;
    const float dz = listenerPos_[2] / 10.0f - pos[2] / 10.0f;
    const float dist = std::sqrt(dz * dz + dx * dx + dy * dy);
    const float att = vol - (dist * dist) / 20000.0f;
    if (!(f32(0x3d4ccccd) <= att) && !force) {   // 0.05
        seLog("3d too far", seId, vol, att, 0xffffffffu, listenerArea_, 0);   // PORT diagnostics
        return 0;
    }
    const uint32_t area = areaOf(pos);
    if (!(listenerArea_ == area || listenerArea_ == 0xffffffffu || area == 0xffffffffu)) {
        seLog("3d other area", seId, vol, att, area, listenerArea_, 0);   // PORT diagnostics
        return 0;
    }
    float g = 1.0f;
    if (!(1.0f < att)) g = att;
    float right[3] = {listenerRight_[0], listenerRight_[1], listenerRight_[2]};
    normalize3(right);
    float dir[3] = {pos[0] - listenerPos_[0], pos[1] - listenerPos_[1], pos[2] - listenerPos_[2]};
    normalize3(dir);
    long h;
    if (ovr == 0.0f) {
        h = command(6, uint32_t(seId + 1), 2, 0x32, 0);
    } else {
        const uint16_t v = floatToU16(g * vol * 100.0f);
        const uint16_t pan = floatToU16((right[0] * dir[0] + right[2] * dir[2] + 1.0f) * 50.0f);
        h = command(6, uint32_t(seId + 1), v, pan, 0);
    }
    seLog("3d", seId, vol, att, area, listenerArea_, h);   // PORT diagnostics
    if (h == 0) return 0;
    if (force) {
        const SoundSlot* s = lookup(uint32_t(h));
        const int idx = s ? (s->id & 0xf) : 0;
        tracked_[idx] = uint32_t(h);
        trackedPos_[idx][0] = pos[0];
        trackedPos_[idx][1] = pos[1];
        trackedPos_[idx][2] = pos[2];
        trackedVol_[idx] = vol;
    }
    return uint32_t(h);
}

// orig FUN_001162e0 (work/decompiled/001162e0.c)
uint32_t SoundSystem::updateSe3d(float vol, uint32_t handle, const float pos[3]) {
    if (enabled_ == 0) return 0;
    const float dx = listenerPos_[0] / 10.0f - pos[0] / 10.0f;
    const float dy = listenerPos_[1] / 10.0f - pos[1] / 10.0f;
    const float dz = listenerPos_[2] / 10.0f - pos[2] / 10.0f;
    const float dist = std::sqrt(dz * dz + dx * dx + dy * dy);
    const float att = vol - (dist * dist) / 20000.0f;
    float g = 1.0f;
    if (att <= 1.0f) g = att;
    if (g < 0.0f) g = 0.0f;
    float right[3] = {listenerRight_[0], listenerRight_[1], listenerRight_[2]};
    normalize3(right);
    float dir[3] = {pos[0] - listenerPos_[0], pos[1] - listenerPos_[1], pos[2] - listenerPos_[2]};
    normalize3(dir);
    const uint16_t v = floatToU16(g * vol * 100.0f);
    const uint16_t pan = floatToU16((right[0] * dir[0] + right[2] * dir[2] + 1.0f) * 50.0f);
    // pan2 == 0: FUN_002115b0 keeps the slot's own pan (+0x21/+0x23), so `pan` has no effect (literal).
    return uint32_t(fadeHandle(handle, v, 0, pan, 0));
}

// orig FUN_00116260 (work/decompiled/00116260.c)
void SoundSystem::setSePosition(uint32_t handle, const float pos[3]) {
    if (enabled_ == 0) return;
    const SoundSlot* s = lookup(handle);
    if (!s) return;
    const int idx = s->id & 0xf;
    trackedPos_[idx][0] = pos[0];
    trackedPos_[idx][1] = pos[1];
    trackedPos_[idx][2] = pos[2];
}

// orig FUN_00116220 (work/decompiled/00116220.c)
void SoundSystem::setListener(const float pos[3], const float right[3]) {
    listenerPos_[0] = pos[0];
    listenerPos_[1] = pos[1];
    listenerPos_[2] = pos[2];
    listenerRight_[0] = right[0];
    listenerRight_[1] = right[1];
    listenerRight_[2] = right[2];
}

// orig FUN_00115820 (work/decompiled/00115820.c)
void SoundSystem::stopSe(uint32_t handle, int time) {
    if (enabled_ == 0 || handle == 0xffffffffu) return;
    if (handle == 0xfffe) {
        seStopReq(0xfffe, uint32_t(time));
        for (int i = 0; i < kSlots; ++i) tracked_[i] = 0;
    } else {
        const SoundSlot* s = lookup(handle);
        if (s) {
            const int idx = s->id & 0xf;
            seStopReq(handle, uint32_t(time));
            tracked_[idx] = 0;
            trackedVol_[idx] = 0.0f;
        }
    }
}

// orig FUN_00115930 (work/decompiled/00115930.c)
uint32_t SoundSystem::findSe(int seId) {
    if (enabled_ == 0) return 0;
    return findSlotHandle(uint32_t(seId + 1), 0x22000000);
}

// orig FUN_00115970 (work/decompiled/00115970.c)
void SoundSystem::playVoice(float vol, int voiceId, bool deferVol) {
    if (enabled_ == 0) return;
    float f = 1.0f;
    if (seOverride_ != 0 && seOverrideFlag_ == 0) f = seOverrideVol_;
    uint32_t v;
    if (f == 0.0f) v = 4;
    else v = floatToU16(100.0f * (f * vol));
    if (deferVol) {
        voiceDeferredVol_ = int16_t(v);
        v = 0x7b;
    }
    voiceVol_ = vol;
    // FUN_00210080: cmd 8 {id, vol, pan 0x32}
    voiceHandle_ = uint32_t(command(8, uint32_t(voiceId) + kVoiceBaseEntry, v, 0x32, 0));
}

// orig FUN_00115a60 (work/decompiled/00115a60.c)
void SoundSystem::playVoiceDeferred(float realVol, int voiceId) {
    playVoice(realVol, voiceId, true);
    voiceDeferredVol_ = int16_t(floatToU16(realVol * 100.0f));
}

// orig FUN_00115af0 (work/decompiled/00115af0.c)
void SoundSystem::setVoiceVolume(float vol) {
    if ((seOverride_ == 0 || seOverrideFlag_ != 0) && voiceHandle_ != 0) {
        fadeHandle(voiceHandle_, floatToU16(vol * 100.0f), 4, 0x32, 0);   // FUN_00210100
        voiceVol_ = vol;
    }
}

// orig FUN_00115bb0 (work/decompiled/00115bb0.c)
void SoundSystem::stopVoice(uint32_t handle, int time) {
    if (enabled_ == 0 || handle == 0xffffffffu) return;
    voiceStopReq(handle, uint32_t(time));   // 0xfffe goes through the same wrapper
}

// orig FUN_00115c10 (work/decompiled/00115c10.c)
void SoundSystem::stopCurrentVoice() {
    stopVoice(voiceHandle_, 0);
    voiceHandle_ = 0;
    voiceVol_ = 0.0f;
}

// orig FUN_00115c50 (work/decompiled/00115c50.c)
void SoundSystem::voicePause(bool pause) {
    const uint32_t h = voiceHandle_;
    if (h == 0) return;
    if (!pause) {
        if (voiceDeferredVol_ == 0) command(5, h, 100);
        else command(4, h, uint32_t(int32_t(voiceDeferredVol_)));
    } else {
        command(5, h, 0);
        voiceDeferredVol_ = 0;
    }
}

// orig FUN_00115cd0 (work/decompiled/00115cd0.c) + FUN_0020fff0
int SoundSystem::voiceStatus() {
    if (enabled_ == 0) return 0;
    if (voiceHandle_ == 0) return 0;
    return command(0x14, voiceHandle_) != 0 ? 4 : 0;
}

// orig FUN_00115ce0 (work/decompiled/00115ce0.c)
uint32_t SoundSystem::playVoiceSimple(float vol, int voiceId) {
    if (enabled_ == 0) return 0;
    return uint32_t(command(8, uint32_t(voiceId) + kVoiceBaseEntry, floatToU16(vol * 100.0f), 0x32, 0));
}

// orig FUN_00115d70 (work/decompiled/00115d70.c)
void SoundSystem::playBgm(int bgmId, int fadeIn, int loop) {
    if (enabled_ == 0) return;
    if (bgmId == -1) {
        stopBgm(0);
        return;
    }
    if (loop == 0) bgmPlayReq((uint32_t(bgmId) + 2) | 0x20000, 100, uint16_t(fadeIn));
    else bgmPlayReq(uint32_t(bgmId) + 2, 100, uint16_t(fadeIn));
    currentBgmId_ = bgmId;
    bgmScaleApplied_ = 1.0f;
    bgmScaleTarget_ = 1.0f;
    bgmScaleTime_ = 4;
}

// orig FUN_0020fd50 (work/decompiled/0020fd50.c)
uint32_t SoundSystem::bgmPlayReq(uint32_t req, uint16_t vol, uint16_t fade) {
    bgmReqHandle_ = uint32_t(command(3, req, vol, fade));
    bgmReqPrev_ = bgmReqId_;
    bgmReqId_ = int(req);
    return bgmReqHandle_;
}

// orig FUN_00115e10 (work/decompiled/00115e10.c)
void SoundSystem::stopBgm(int time) {
    if (enabled_ == 0) return;
    if (bgmCat_ == 0.0f) time = 0;
    bgmStopReq(uint32_t(time));
    currentBgmId_ = -1;
}

// orig FUN_0020fda0 (work/decompiled/0020fda0.c)
void SoundSystem::bgmStopReq(uint32_t time) {
    if (bgmHandle_ != 0) {
        if (time == 0) command(10, bgmHandle_);
        else if (bgmHandle_ != 0) command(0xe, bgmHandle_, 0, time & 0xffff, 0x32, 0);
    }
    bgmReqId_ = 999;
}

// orig FUN_0020fe20 (work/decompiled/0020fe20.c)
uint32_t SoundSystem::bgmAlive() {
    if (bgmHandle_ == 0) return 0;
    if (command(0x14, bgmHandle_) == 0) return 0;
    return bgmHandle_;
}

// orig FUN_00115e80 (work/decompiled/00115e80.c) + FUN_0020fe70
int SoundSystem::currentBgmEntry() {
    if (enabled_ == 0) return -1;
    const uint32_t r = bgmAlive() == 0 ? 0 : (curBgmEntry_ & 0x3ffff);
    return r == 0 ? -1 : int(r);
}

// orig FUN_00115ec0 (work/decompiled/00115ec0.c) + FUN_0020feb0
int SoundSystem::bgmFrames() {
    const uint32_t h = bgmAlive();
    if (h == 0) return 0;
    const SoundSlot* s = lookup(h);
    return s ? s->frames : 0;
}

// orig FUN_00115f10 (work/decompiled/00115f10.c). +0x478 is never written by the ELF (0 from BSS), so the result is
// always 0 in practice; SOUND TEST (0x1ffc40) treats 6 or 0 as "track finished".
int SoundSystem::bgmState() {
    if (enabled_ == 0) return 0;
    return streamState(field478_);
}

// orig FUN_00116510 (disassembled: a1 = handle is passed through to FUN_0020fff0, Ghidra drops it)
int SoundSystem::streamState(uint32_t handle) {
    if (enabled_ == 0) return 0;
    if (handle == 0) return 0;                                   // FUN_0020fff0: handle 0 -> 0
    return command(0x14, handle) != 0 ? 4 : 0;                   // FUN_00210170(0x14, ..) != 0 -> 1 -> 4
}

// orig FUN_0020fee0 (work/decompiled/0020fee0.c)
long SoundSystem::bgmFadeTo(uint16_t vol, uint16_t time) {
    if (vol < 3) vol = 2;
    if (bgmHandle_ == 0) return 0;
    return command(0xe, bgmHandle_, vol, time, 0x32, 0);
}

// orig FUN_00115f00 (work/decompiled/00115f00.c)
void SoundSystem::setBgmVolumeScale(float vol, int time) {
    bgmScaleTarget_ = vol;
    bgmScaleTime_ = uint16_t(time);
}

// orig FUN_00115f40 (work/decompiled/00115f40.c) + FUN_0020fcc0
void SoundSystem::setReverb(int type, int vol) {
    if (enabled_ == 0) return;
    command(0x13, uint32_t(type), uint32_t(vol));
    reverbType_ = type;
    reverbVol_ = vol;
}

// orig FUN_00115f70 (work/decompiled/00115f70.c) + FUN_0020fd10
void SoundSystem::setReverbVolume(long vol) {
    if (enabled_ == 0) return;
    long v = vol;
    if (0x4000 < vol) v = 0x4000;
    if (vol < -0x3000) v = -0x3000;
    command(0x13, uint32_t(reverbType_), uint32_t(v));
    reverbVol_ = int(v);
}

// orig FUN_00115fc0 (work/decompiled/00115fc0.c) + FUN_0020fca0
void SoundSystem::setCategoryVolumes(float bgm, float se, float voice) {
    if (enabled_ == 0) return;
    bgmCat_ = bgm;
    seCat_ = se;
    voiceCat_ = voice;
    command(0x12, floatToU16(bgm * 100.0f), floatToU16(se * 100.0f), floatToU16(voice * 100.0f));
}

// orig FUN_001160e0 (work/decompiled/001160e0.c)
void SoundSystem::setBgmCategory(float v) {
    if (v <= 1.0f) bgmCat_ = v;
    bgmScaleApplied_ = 0.0f;   // forces FUN_001150b0 to re-send the BGM volume next frame
    setCategoryVolumes(v, seCat_, voiceCat_);
}

// orig FUN_00116110 (work/decompiled/00116110.c)
void SoundSystem::setSeCategory(float v) {
    if (v <= 1.0f) seCat_ = v;
    setCategoryVolumes(bgmCat_, v, voiceCat_);
}

// orig FUN_00116140 (work/decompiled/00116140.c)
void SoundSystem::setVoiceCategory(float v) {
    if (v <= 1.0f) voiceCat_ = v;
    setCategoryVolumes(bgmCat_, seCat_, v);
    if (seOverride_ == 0) setVoiceVolume(voiceVol_);
}

// orig FUN_001161a0 (work/decompiled/001161a0.c)
void SoundSystem::seOverrideBegin() {
    seOverride_ = 1;
    seOverrideTarget_ = 0.0f;
}

// orig FUN_001161b0 (work/decompiled/001161b0.c)
void SoundSystem::seOverrideEnd() {
    seOverride_ = 0;
    seOverrideTarget_ = 1.0f;
}

// orig FUN_001161d0 (work/decompiled/001161d0.c)
void SoundSystem::setSeOverrideFlag(int v) { seOverrideFlag_ = v; }

// orig FUN_001161e0 (work/decompiled/001161e0.c)
void SoundSystem::setMono(bool mono) {
    if (mono) command(2);
    else command(1);
}

// orig FUN_00213840 (work/decompiled/00213840.c), the sound part: FUN_00210150(0x53f830) then cmd 10.
void SoundSystem::stopAllForMovie() {
    command(0xb);                // FUN_00210150: {+0x10 = 0, +0 = 999} of the movie object, then cmd 0xb
    // cmd 10 with a stale a1 (whatever FUN_00210150 left in it). After cmd 0xb every live slot is already stopping
    // (stop() is then a no-op), so the call has no effect; modelled with handle 0.
    command(10, 0);
}
void SoundSystem::suspendForMovie() { command(0x16); }
void SoundSystem::resumeAfterMovie() { command(0x15); }

// orig FUN_0020ff80 (work/decompiled/0020ff80.c)
void SoundSystem::seStopReq(uint32_t handle, uint32_t time) {
    if (handle == 0) return;
    if (handle == 0xfffe) command(0xc, time & 0xffff);
    else if (time == 0) command(10, handle);
    else command(0xe, handle, 0, time & 0xffff, 0x32, 0);
}

// orig FUN_00210090 (work/decompiled/00210090.c)
void SoundSystem::voiceStopReq(uint32_t handle, uint32_t time) {
    if (handle == 0) return;
    if (handle == 0xfffe) command(0xd, time & 0xffff);
    else if (time == 0) command(10, handle);
    else command(0xe, handle, 0, time & 0xffff, 0x32, 0);
}

// orig FUN_00210030 (work/decompiled/00210030.c) == FUN_00210100 (work/decompiled/00210100.c)
long SoundSystem::fadeHandle(uint32_t handle, uint16_t vol, uint16_t time, uint16_t pan, uint16_t pan2) {
    if (handle == 0) return 0;
    if (vol < 3) vol = 2;
    return command(0xe, handle, vol, time, pan, pan2);
}

// ===================================================================================== sound manager

// orig FUN_00210170 (work/decompiled/00210170.c)
long SoundSystem::command(int cmd, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) {
    long r = 0;
    switch (cmd) {
    case 0:
        managerInit(0, int(p2));
        break;
    case 1:
        mgrFlags_ |= 0x80000000u;
        rpc(0x10, 0);
        break;
    case 2:
        mgrFlags_ &= 0x7fffffffu;
        rpc(0x20, 0);
        break;
    case 3:
        r = playRequest(p2, uint16_t(p3), uint16_t(p4), 0, cmd, 0);
        break;
    case 4:
        r = setVolume(p2, uint16_t(p3));
        break;
    case 5:
        r = pause(p2, uint16_t(p3));
        break;
    case 6:
        r = playRequest(p2, uint16_t(p3), uint16_t(p4), int16_t(p5), cmd, 0);
        break;
    case 7:
        r = playRequest(p2, uint16_t(p3), uint16_t(p4), int16_t(p5), cmd, p6);
        break;
    case 8:
        r = playRequest(p2, uint16_t(p3), uint16_t(p4), 0, cmd, 0);
        break;
    case 10:
        r = stop(p2);
        break;
    case 0xb:
        driver(0, 0);
        break;
    case 0xc:
        driver(1, int16_t(p2 & 0xffff));
        break;
    case 0xd:
        driver(2, int16_t(p2 & 0xffff));
        break;
    case 0xe:
        r = fade(p2, uint16_t(p3), uint16_t(p4), uint16_t(p5), uint16_t(p6));
        break;
    case 0xf:
        std::memset(retriggerMap_, 0, sizeof retriggerMap_);
        // Delayed-BGM requests (DAT_0046dcd0 / DAT_0046dcd8): never armed by this build (dead code), kept as is.
        if (delayedBgm_ != -1 && (--delayedBgmTimer_, delayedBgmTimer_ == 0)) {
            command(3, uint32_t(delayedBgm_), delayedBgmVol_, 0);
            delayedBgm_ = -1;
        }
        if (delayedBgmTimer2_ != 0 && (--delayedBgmTimer2_, delayedBgmTimer2_ == 0))
            command(0xe, bgmHandle_, delayedBgmVol_, 1, 0x32, 0);
        if (delayedBgm2_ != -1 && bgmHandle_ == 0) {
            command(3, uint32_t(delayedBgm2_), delayedBgmVol_, 0);
            delayedBgm2_ = -1;
        }
        driver(3, 0);
        break;
    case 0x10:
        r = loadBank(int(p2), int(p3));
        break;
    case 0x11:
        rpc(0x30, p2);
        break;
    case 0x12: {
        // (int)(100.0 * (double)x / 100.0) through the soft-float helpers FUN_0040cbb0 (int->double) / FUN_00247210 /
        // FUN_00247498 / FUN_00247928 (double->int); stored with `sh`. UNVERIFIED: 0x247210 = __muldf3 and
        // 0x247498 = __divdf3 by libgcc dp-bit layout (the swap would give 10000/x, which the callers rule out).
        auto pct = [](uint32_t x) { return uint16_t(int32_t(100.0 * double(x & 0xffff) / 100.0)); };
        bgmPct_ = pct(p2);
        sePct_ = pct(p3);
        voicePct_ = pct(p4);
        break;
    }
    case 0x13: {
        const uint32_t idx = p2 & 0xffff;
        param580_[0] = idx;
        param580_[1] = 0;
        // Rows >= kEffectRows read past the table on the PS2; the game only passes 0..5.
        const EffectRow& e = kEffectTable[idx < uint32_t(kEffectRows) ? idx : 0];
        param580_[2] = e.mode | 0x100;
        param580_[3] = uint32_t(int32_t(e.depthL));
        param580_[4] = uint32_t(int32_t(e.depthR));
        param580_[5] = e.delay;
        param580_[6] = e.feedback;
        param580_[7] = p3 & 0xffff;
        rpc(0x1040, kBuf540580);
        break;
    }
    case 0x14: {
        // returns the slot address on the PS2; here slot index + 1 (only tested against 0 by the callers)
        const SoundSlot* s = lookup(p2);
        r = s ? long(s - slots_.data()) + 1 : 0;
        break;
    }
    case 0x15:
        rpc(0, 1);
        mgrFlags_ &= 0xfffffffeu;
        managerInit(1, 0);
        break;
    case 0x16:
        rpc(0, 2);
        mgrFlags_ |= 1;
        break;
    case 0x17:
        r = long(rpc(0x8f00, 0));   // FUN_00212790
        break;
    default:
        // 0x18..0x1c: host-file / IOP-memory debug helpers (RPC 0x9b10/0x8b20/0x9b30), no callers in the game
        // flow; not translated.
        break;
    }
    return r;
}

// orig FUN_00210b70 (work/decompiled/00210b70.c)
void SoundSystem::managerInit(int mode, int fromCd) {
    if (mode == 0) {
        mgrFlags_ = 0;
        serial_ = 0;
        for (int i = 0; i < kSlots; ++i) {
            slots_[size_t(i)].id = uint16_t(0xaa00 + i);
            clearSlot(slots_[size_t(i)]);
        }
        loadedHdEntry_[0] = -1;
        bankOfSlot_[0] = 0;
        loadedBdEntry_[0] = -1;
        // FUN_002124b0: sceSifInitRpc + bind RPC server 0x12346 (IOPSND loaded by the boot code)
        iop_.init(&sfs_, &bank_);
        iopInited_ = true;
        const uint32_t heap = kIopHeapBlock;          // FUN_0022a2a0(0x8800)
        bankHdAddr_[0] = heap + 0x2800;               // 0x5408c0
        bankSpuAddr_[0] = 0xb800;                     // 0x5408c8
        buf640Set32(0x0, 4);
        buf640Set32(0x10, 0x2800);
        buf640Set32(0x8, heap);
        // FUN_00114c90 (uRam004e9770, 0 at boot) selects SOUND_EN: "cdrom0:\SOUND_EN\%s" (fromCd) or
        // "host0:host/SOUND_EN/%s", %s = "BRQSOUND.SFS"; written at 0x540660 (+0x20).
        char path[0x60] = {};
        std::snprintf(path, sizeof path, fromCd != 0 ? "cdrom0:\\SOUND_EN\\%s" : "host0:host/SOUND_EN/%s", "BRQSOUND.SFS");
        std::memcpy(buf640_ + 0x20, path, sizeof path);
        rpc(0x1290, kBuf540640);
        voicePct_ = 100;
        sePct_ = 100;
        bgmPct_ = 100;
        std::memset(seIdMap_, 0xff, sizeof seIdMap_);   // 0x638 bytes
    }
    // puRam005404c0 = 0x5404b0 (the queue)
    bgmHandle_ = 0;
    prevBgmEntry_ = 0;
    curBgmEntry_ = 0;
    g5404a8_ = 0;
    g5404a0_ = 0;
    clearQueue();
    rpc(0xb00, kBuf540600);
    streamInit(mode);
    reverbType_ = 5;
    reverbVol_ = 0;
    command(0x13, 5, 0);
    command(0x11, 0x3ff03ff0);
    command(0x12, bgmPct_, sePct_, voicePct_);
}

// orig FUN_00212a20 (work/decompiled/00212a20.c)
void SoundSystem::streamInit(int mode) {
    if (mode == 0) rpc(0x900, 0x11660);
    rpc(0x980, 0x12);
    rpc(0x981, 0x14);
    rpc(0x982, 0x15);
    rpc(0x983, 0x16);
    rpc(0x984, 0x17);
}

// orig FUN_00210b20 (work/decompiled/00210b20.c)
void SoundSystem::clearQueue() { queue_ = Queued{}; }

// orig FUN_002123e0 (work/decompiled/002123e0.c): clears everything but the slot id (+0)
void SoundSystem::clearSlot(SoundSlot& s) {
    const uint16_t id = s.id;
    s = SoundSlot{};
    s.id = id;
}

void SoundSystem::bumpSerial() {
    if (serial_ == 0xff00000) serial_ = 0x100000;
    else serial_ += 0x100000;
}

// orig FUN_002122d0 (work/decompiled/002122d0.c)
SoundSlot* SoundSystem::lookup(uint32_t handle) {
    if (handle == 0) return nullptr;
    const uint32_t idx = handle >> 28;
    if (idx >= uint32_t(kSlots)) return nullptr;   // port guard: the PS2 reads past the 14 slots (never matches)
    SoundSlot& s = slots_[idx];
    return s.handle == handle ? &s : nullptr;
}

const SoundSlot* SoundSystem::slotForHandle(uint32_t handle) const {
    return const_cast<SoundSystem*>(this)->lookup(handle);
}

// orig FUN_00212350 (work/decompiled/00212350.c)
uint32_t SoundSystem::findSlotHandle(uint32_t gameId, uint32_t flagMask) {
    for (int i = 0; i <= 0xd; ++i) {
        const SoundSlot& s = slots_[size_t(i)];
        if ((s.descFlags & flagMask) != 0 && s.gameId == gameId) return s.handle;
    }
    return 0;
}

// orig FUN_00211ef0 (work/decompiled/00211ef0.c; control flow from the disassembly 0x211ef0..0x2122c4)
SoundSlot* SoundSystem::allocSlot(uint32_t handleBits, const SeDescriptor& desc, uint32_t gameId) {
    int first = 0, end = 0;   // s1 / s5
    if (desc.flags & 0x2000000) { first = 1; end = 9; }            // synth SE -> slots 1..8
    else if (desc.flags & 0x9000000) { first = 0; end = 1; }       // -> slot 0
    else if (desc.flags & 0x200000) { first = 9; end = 10; }       // BGM -> slot 9
    else if (desc.flags & 0x20400000) { first = 10; end = 14; }    // streamed SE / voice -> slots 10..13
    else return nullptr; // UNVERIFIED: s1/s5 are stale registers on the PS2 (only row 0, flags 0, reaches this)

    SoundSlot* steal = nullptr;    // s3
    SoundSlot* freeSlot = nullptr; // s4
    uint16_t best = desc.priority; // s6
    if (desc.mode == 3) {
        for (int i = first; i < end; ++i) {
            SoundSlot& s = slots_[size_t(i)];
            if (s.handle == 0) { freeSlot = &s; break; }
            if (s.priority < best) { best = s.priority; steal = &s; }
        }
    } else {
        for (int i = first; i < end; ++i) {
            SoundSlot& s = slots_[size_t(i)];
            if (s.gameId == gameId) {      // lhu +0xc compared with the full 32-bit id
                if (desc.mode == 1) return nullptr;
                if (desc.mode == 2) {
                    if (s.state == 4) return nullptr;
                    s.state = (s.noteProg & 0xff) != 0 ? 6 : 3;
                    return &s;
                }
                continue;
            }
            if (freeSlot == nullptr && s.handle == 0) { freeSlot = &s; continue; }
            if (s.handle != 0 && s.priority < best) { best = s.priority; steal = &s; }
        }
    }
    SoundSlot* use = freeSlot;
    if (use == nullptr) {
        if (steal == nullptr) return nullptr;
        stop(steal->handle);
        use = steal;
    }
    bumpSerial();
    const uint32_t idx = use->id & 0xf;
    use->iopSlot = uint16_t(idx - uint32_t(first));
    use->descX = uint16_t(desc.x);
    use->handle = (idx << 28) | handleBits | serial_;
    use->descFlags = desc.flags | 0x80000000u;
    use->field8 = desc.field8;
    use->priority = desc.priority;
    use->noteProg = desc.field12;
    use->defVolume = desc.defaultVolume;
    use->gameId = uint16_t(gameId);
    return use;
}

// orig FUN_00210f70 (work/decompiled/00210f70.c; owner loop from the disassembly 0x2111ac..0x211210)
long SoundSystem::playRequest(uint32_t id, uint16_t vol, uint16_t panOrFade, int16_t pitch, int cmd, uint32_t owner) {
    // s6 = vol & 0x7fff and s5 = vol & 0x8000 are computed at entry and never used.
    if (cmd == 3) {
        const SoundSlot* cur = lookup(bgmHandle_);
        if (cur != nullptr) {
            if (uint32_t(cur->gameId) == id) return 0;   // a play-once id (|0x20000) never compares equal
            fade(bgmHandle_, 0, panOrFade, 0x32, 0);
        }
        const uint32_t h = startStream(id, vol, panOrFade, nullptr, 3, 0);
        if (h == 0) {
            queue_.id = id;
            queue_.vol = vol;
            queue_.fade = panOrFade;
            queue_.pitch = pitch;
            queue_.cmd = 3;
        } else {
            bgmHandle_ = h;
        }
        return long(h);
    }
    if (cmd == 8) return long(startStream(id, vol, panOrFade, nullptr, 8, 0));

    if (id >= uint32_t(kSeTableRows)) return 0;   // port guard: the PS2 would read past the 398-row table
    uint32_t row = id;
    const SeDescriptor* desc = &kSeTable[row];
    if (desc->flags & 0x2000000) {
        if (seIdMap_[id] == -1) row = 1;
        desc = &kSeTable[row];
    }
    if (owner != 0) {
        // The loop counter advances by 2 per slot while the pointer advances by one slot: only slots 0..6 are
        // inspected (literal).
        int i = 0;
        for (int k = 0; k < 0xe; k += 2, ++i) {
            SoundSlot& s = slots_[size_t(i)];
            if (s.owner == owner && desc->priority < s.priority) stop(s.handle);
        }
    }
    if ((desc->flags & 0x2000000) == 0) return long(startStream(row, vol, panOrFade, desc, cmd, owner));
    if (retriggerMap_[row] == 0) {
        retriggerMap_[row] = 1;
        return long(startSynth(row, vol, uint8_t(panOrFade), pitch, desc, owner));
    }
    return 0;
}

// orig FUN_00212aa0 (work/decompiled/00212aa0.c; register use from the disassembly 0x212aa0..0x212cd4)
uint32_t SoundSystem::startStream(uint32_t id, uint32_t vol, uint32_t panOrFade, const SeDescriptor* desc, int cmd,
                                  uint32_t owner) {
    uint32_t v = vol & 0xffff;
    uint32_t onceFlag = 0;
    const uint32_t volFlag = (v & 0x8000) >> 8;
    SoundSlot* s;
    if (cmd == 3) {
        if (id & 0x20000) {
            onceFlag = 0x10000;
            id &= 0xfffdffffu;
        }
        s = allocSlot(id | 0x10000, kBgmDescriptor, id);
        v = uint32_t(int32_t(v * bgmPct_) / 100) & 0xffff;
    } else {
        if (status_.word[2] & 1) return 0;
        if (cmd == 8) {
            s = allocSlot(id, kVoiceDescriptor, id);
            if (v != 0x7b) v = uint32_t(int32_t(v * voicePct_) / 100) & 0xffff;
        } else {
            s = allocSlot(desc->x, *desc, id);
            if (v != 0x7b) v = uint32_t(int32_t(v * sePct_) / 100) & 0xffff;
        }
    }
    if (v < 3) v = 2;
    if (s == nullptr) return 0;
    if (s->descFlags & 0x20400000) s->iopSlot = uint16_t(s->iopSlot + 1);   // stream slots 1..4
    s->descFlags |= onceFlag;
    s->volume = uint8_t(v | volFlag);
    if (cmd == 3) {
        s->pan2 = 0x32;
        s->pan = 0x32;
        s->pan3 = 0;
        if (panOrFade != 0) {
            s->fadeInTime = uint8_t(panOrFade);
            s->fadeInVol = s->volume;
            s->volume = 1;
        }
    } else {
        s->pan2 = uint8_t(panOrFade);
        s->pan = uint8_t(panOrFade);
        s->pan3 = 0;
    }
    s->state = 4;
    s->owner = owner;
    return s->handle;
}

// orig FUN_00213300 (work/decompiled/00213300.c)
uint32_t SoundSystem::startSynth(uint32_t id, uint32_t vol, uint8_t pan, int16_t pitch, const SeDescriptor* desc,
                                 uint32_t owner) {
    SoundSlot* s = allocSlot(id & 0xffff, *desc, id);
    if (s == nullptr) return 0;
    auto bindBank = [&]() {
        // RPC 0x1440+seq {.., hd@+0xc, spu@+0x14}: modhsyn:6 binds the bank to synth port seq+1. The model has a
        // single bank, so the IOP side is a no-op. (bank slot = bits 16..19 of the id map; unmapped ids were
        // redirected to row 1 by FUN_00210f70, so the index is 0.)
        uint32_t bs = (uint32_t(seIdMap_[id]) >> 16) & 0xf;
        if (bs >= 1) bs = 0;   // port guard: 0x5408c0/0x5408c8 hold one entry
        buf640Set32(0xc, bankHdAddr_[bs]);
        buf640Set32(0x14, bankSpuAddr_[bs]);
        rpc(uint32_t(s->iopSlot) + 0x1440, kBuf540640);
    };
    if (s->state == 6) {
        bindBank();
    } else {
        if (s->state == 3) return s->handle;
        bindBank();
    }
    const uint32_t v = uint32_t(int32_t((vol & 0xffff) * uint32_t(sePct_)) / 100) & 0xffff;
    s->volume = v < 3 ? 1 : uint8_t(v);
    s->pan2 = pan;
    s->pan = pan;
    s->pan3 = 0;
    s->noteProg = uint16_t(s->noteProg | uint16_t((uint16_t(pitch) + ((desc->x >> 8) & 0xff)) * 0x100));
    s->state = 4;
    s->owner = owner;
    return s->handle;
}

// orig FUN_002113c0 (work/decompiled/002113c0.c)
long SoundSystem::setVolume(uint32_t handle, uint16_t vol) {
    SoundSlot* s = lookup(handle);
    if (s == nullptr) return 0;
    uint16_t v;
    if ((s->descFlags & 0x20000000) == 0) {
        if ((s->descFlags & 0x400000) == 0) v = uint16_t(int32_t(uint32_t(vol) * bgmPct_) / 100);
        else v = uint16_t(int32_t(uint32_t(vol) * voicePct_) / 100);
    } else {
        v = uint16_t(int32_t(uint32_t(vol) * sePct_) / 100);
    }
    if (v < 3) v = 1;
    s->state = 5;
    s->newVolume = v;
    return long(handle);
}

// orig FUN_00211540 (work/decompiled/00211540.c)
long SoundSystem::pause(uint32_t handle, uint16_t v) {
    SoundSlot* s = lookup(handle);
    if (s == nullptr) return 0;
    rpc(uint32_t(s->iopSlot) + 0xa10, v);
    return long(handle);
}

// orig FUN_002112f0 (work/decompiled/002112f0.c)
long SoundSystem::stop(uint32_t handle) {
    SoundSlot* s = lookup(handle);
    if (s == nullptr) return 0;
    if ((s->descFlags & 0x6000000) == 0) {
        if ((s->descFlags & 0x20400000) == 0) {
            if (s->descFlags & 0x200000) {
                stopStreamSlot(*s);
                clearQueue();
            }
        } else {
            stopStreamSlot(*s);
        }
    } else {
        stopSynthSlot(*s);
    }
    return 1;
}

// orig FUN_002115b0 (work/decompiled/002115b0.c)
long SoundSystem::fade(uint32_t handle, uint16_t vol, uint16_t time, uint16_t pan, uint16_t pan2) {
    SoundSlot* s = lookup(handle);
    if (s == nullptr) return 0;
    if (s->descFlags & 0x40000) return 1;   // muted (faded to 0): further fades ignored
    uint16_t p = pan, p2 = pan2;
    if (pan2 == 0) {
        p = s->pan;
        p2 = s->pan3;
    }
    if ((s->descFlags & 0x2000000) == 0) {
        if (s->descFlags & 0x40000000) return 0;
        uint16_t v = vol;
        if (vol != 0) {
            if ((s->descFlags & 0x20000000) == 0) {
                if ((s->descFlags & 0x400000) == 0) v = uint16_t(int32_t(uint32_t(vol) * bgmPct_) / 100);
                else v = uint16_t(int32_t(uint32_t(vol) * voicePct_) / 100);
            } else {
                v = uint16_t(int32_t(uint32_t(vol) * sePct_) / 100);
            }
            if (v < 3) v = 2;
        }
        streamFadeRpc(*s, v, time, uint8_t(p), uint8_t(p2));
        if (v == 0) s->descFlags |= 0x40000;
    } else {
        uint16_t v = uint16_t(int32_t(uint32_t(vol) * sePct_) / 100);
        if (v < 3) v = 2;
        synthFadeRpc(*s, v, uint8_t(time), uint8_t(p), uint8_t(p2));   // scales by the SE category again
    }
    return 1;
}

// orig FUN_00212d00 (work/decompiled/00212d00.c)
void SoundSystem::stopStreamSlot(SoundSlot& s) {
    if (s.state == 4) {
        if (s.descFlags & 0x200000) {
            g5404a0_ = 0;
            bgmHandle_ = 0;
            curBgmEntry_ = 0;
        }
        clearSlot(s);
    } else if ((s.descFlags & 0x40000000) == 0) {
        rpc(0x8970, s.iopSlot);
        s.descFlags |= 0x40000000;
        s.release = 4;
    }
}

// orig FUN_00213510 (work/decompiled/00213510.c): the slot keeps playing until the IOP reports it idle; the
// handle is replaced by a bare serial so lookups by the old handle fail.
void SoundSystem::stopSynthSlot(SoundSlot& s) {
    s.descFlags |= 0x40000000;
    bumpSerial();
    s.handle = serial_;
}

// orig FUN_00212db0 (work/decompiled/00212db0.c)
void SoundSystem::streamFadeRpc(SoundSlot& s, uint16_t vol, uint32_t time, uint8_t pan, uint8_t pan2) {
    param580_[1] = time & 0xffff;
    param580_[2] = pan;
    s.pan = pan;
    param580_[3] = pan2;
    s.pan3 = pan2;
    rpc(uint32_t(s.iopSlot) + 0xa00, vol);
}

// orig FUN_00213560 (work/decompiled/00213560.c)
void SoundSystem::synthFadeRpc(SoundSlot& s, uint32_t vol, uint8_t time, uint8_t pan, uint8_t pan2) {
    if (s.descFlags & 0x40000000) return;
    uint32_t v = uint32_t(int32_t(uint32_t(sePct_) * (vol & 0xffff)) / 100);
    if (v < 3) v = 2;
    s.fadeInTime = time;   // +0x25
    param580_[2] = pan;
    param580_[1] = time;
    s.pan = pan;
    param580_[3] = pan2;
    s.pan3 = pan2;
    rpc(uint32_t(s.iopSlot) + 0x450, v);
}

// orig FUN_00212ce0 (work/decompiled/00212ce0.c)
void SoundSystem::voiceVolumeRpc(SoundSlot& s) { rpc(uint32_t(s.iopSlot) + 0x950, s.volume); }

// orig FUN_00211860 (work/decompiled/00211860.c)
long SoundSystem::loadBank(int bank, int bankSlot) {
    if (bank < 0 || bank > 1 || bankSlot != 0) return 1;   // port guard: tables 0x46bcc0 (2 rows) / 0x5408c0 (1)
    reloadExcluded_ = 0;
    bankSlotOfBank_[bank] = int16_t(bankSlot);
    buf640Set32(0xc, bankHdAddr_[bankSlot]);
    buf640Set32(0x14, bankSpuAddr_[bankSlot]);
    const BankRow& row = bankTable_[bank];
    if (row.hdEntry != loadedHdEntry_[bankSlot]) {
        loadedHdEntry_[bankSlot] = row.hdEntry;
        bankOfSlot_[bankSlot] = bank;
        reloadBankSlot_ = bankSlot;
        driver(0xc, 0);
        buf640Set32(0x0, 2);
        buf640Set32(0x4, uint32_t(row.hdEntry));
        buf640Set32(0x8, bankHdAddr_[bankSlot]);
        rpc(0x1290, kBuf540640);
        loadedBdEntry_[bankSlot] = row.bdEntry;
        // FlushCache(0)
        buf640Set32(0x0, 3);
        buf640Set32(0x4, uint32_t(row.bdEntry));
        buf640Set32(0x10, rpc(0x1290, kBuf540640));
        std::memset(seIdMap_, 0xff, sizeof seIdMap_);
        for (int i = 0; i < 1; ++i) {
            if (bankOfSlot_[i] != 0) {
                const BankRow& r = bankTable_[bankOfSlot_[i]];
                for (uint32_t k = 0; k < r.count; ++k) seIdMap_[r.ids[k]] = int32_t((k + 1) | (uint32_t(i) << 16));
            }
        }
    }
    return 1;
}

// orig FUN_00211b30 (work/decompiled/00211b30.c)
long SoundSystem::driver(int what, int16_t t) {
    rpcRetries_ = 0;
    if (mgrFlags_ & 1) {
        rpcRetries_ = 0;
        return 0;
    }
    if (what == 3) {
        rpc(0x8200, 0);
        for (int i = 0; i < kSlots; ++i) {
            SoundSlot& s = slots_[size_t(i)];
            if ((s.descFlags & 0x2000000) == 0) {
                if ((s.descFlags & 0x20400000) == 0) {
                    if (s.descFlags & 0x200000) bgmSlotTick(s);
                } else {
                    streamSlotTick(s);
                }
            } else {
                synthSlotTick(s);
            }
        }
    } else if (what == 0) {
        for (int i = 0; i < kSlots; ++i) {
            const uint32_t h = slots_[size_t(i)].handle;
            if (h != 0) stop(h);
        }
        clearQueue();
        return 0;
    } else if (what == 1) {
        for (int i = 0; i < kSlots; ++i) {
            const SoundSlot& s = slots_[size_t(i)];
            if (s.handle != 0 && (s.descFlags & 0x200000) == 0 && (s.descFlags & 0x400000) == 0) {
                if (t == 0) stop(s.handle);
                else fade(s.handle, 0, uint16_t(t), 0x32, 0);
            }
        }
        return 0;
    } else if (what == 2) {
        for (int i = 0; i < kSlots; ++i) {
            const SoundSlot& s = slots_[size_t(i)];
            if (s.handle != 0 && (s.descFlags & 0x400000) != 0) {
                if (t == 0) stop(s.handle);
                else fade(s.handle, 0, uint16_t(t), 0x32, 0);
            }
        }
        return 0;
    } else if (what == 0xc) {
        for (int i = 0; i < kSlots; ++i) {
            const SoundSlot& s = slots_[size_t(i)];
            // `slot address != iRam005404d8` (always 0 here) and the bank-slot bits of desc +8
            if (s.handle != 0 && reloadExcluded_ == 0 && int(s.field8 & 0xf00) == reloadBankSlot_) stop(s.handle);
        }
        return 0;
    }
    if (bgmHandle_ == 0 && queue_.id != 0) {
        playRequest(queue_.id, queue_.vol, queue_.fade, queue_.pitch, int(queue_.cmd), 0);
        clearQueue();
    }
    return 0;
}

// orig FUN_00212df0 (work/decompiled/00212df0.c)
void SoundSystem::bgmSlotTick(SoundSlot& s) {
    const uint32_t bit = 1u << ((s.iopSlot + 0x18) & 0x1f);
    if (s.state == 4) {
        if (queue_.id == 0) {
            if ((status_.word[0] & bit) == 0) {
                buf640Set32(0x0, 5);
                buf640Set32(0x4, s.handle & 0x3ffff);   // (handle << 46) >> 46: entry | 0x10000 (stereo)
                buf640Set8(0x1a, s.pan);
                buf640Set8(0x1b, s.pan2);
                buf640Set8(0x1c, s.pan3);
                uint16_t vol = s.volume;
                buf640Set8(0x1d, s.fadeInVol);
                buf640Set8(0x1e, s.fadeInTime);
                // A fade-in start is only kept when the BGM returns to the entry played before the current one.
                if (s.fadeInTime != 0 && prevBgmEntry_ != s.gameId) {
                    vol = s.fadeInVol;
                    s.fadeInVol = 0;
                    buf640Set8(0x1d, 0);
                    s.fadeInTime = 0;
                    buf640Set8(0x1e, 0);
                }
                buf640Set16(0x18, vol);
                prevBgmEntry_ = curBgmEntry_;
                curBgmEntry_ = s.gameId;
                rpc(uint32_t(s.iopSlot) + 0x1960, kBuf540640);
                s.state = 3;
            }
        } else {
            if (s.descFlags & 0x200000) {
                g5404a0_ = 0;
                bgmHandle_ = 0;
            }
            clearSlot(s);
            bgmHandle_ = uint32_t(playRequest(queue_.id, queue_.vol, queue_.fade, queue_.pitch, int(queue_.cmd), 0));
            clearQueue();
        }
    } else if (s.state == 3) {
        ++s.frames;
        const uint32_t f = s.descFlags;
        if ((f & 0x20000) == 0) {
            if (status_.word[0] & bit) s.descFlags = f | 0x20000;
            const int16_t r = s.release;
            if (r != 0) {
                s.release = int16_t(r - 1);
                if (s.release == 0) {
                    if (s.descFlags & 0x200000) {
                        g5404a0_ = 0;
                        bgmHandle_ = 0;
                    }
                    clearSlot(s);
                }
            }
        } else if ((status_.word[0] & bit) == 0) {
            if (f & 0x200000) {
                g5404a0_ = 0;
                bgmHandle_ = 0;
            }
            clearSlot(s);
        } else if (s.fadeInTime == 0) {
            // play-once BGM: fade out (time 0x10) once the IOP reports the loop point was passed
            if ((f & 0x10000) && (status_.word[3] & bit)) {
                s.descFlags = f & 0xfffeffffu;
                streamFadeRpc(s, 0, 0x10, 0x32, 0);
            }
        } else {
            streamFadeRpc(s, s.fadeInVol, s.fadeInTime, 0x32, 0);
            s.fadeInVol = 0;
            s.fadeInTime = 0;
        }
    }
}

// orig FUN_00213110 (work/decompiled/00213110.c): streamed VAG SE and voice slots (IOP stream slots 1..4)
void SoundSystem::streamSlotTick(SoundSlot& s) {
    g540910_ = 0;
    const uint32_t bit = 1u << ((s.iopSlot + 0x18) & 0x1f);
    if (s.state == 4) {
        if ((status_.word[0] & bit) == 0) {
            buf640Set32(0x0, 5);
            buf640Set32(0x4, s.handle & 0x3ffff);
            buf640Set8(0x1a, s.pan);
            buf640Set8(0x1b, s.pan2);
            buf640Set8(0x1c, s.pan3);
            const uint8_t v = s.volume;
            if ((v & 0x80) == 0) {
                buf640Set16(0x18, v);
            } else {
                buf640Set16(0x18, 4);
                s.volume = v & 0x7f;
            }
            rpc(uint32_t(s.iopSlot) + 0x1960, kBuf540640);
            s.state = 3;
            s.release = 4;
        }
    } else if (s.state == 5) {
        if (status_.word[2] & bit) {             // first buffer primed
            if ((status_.word[1] & bit) == 0) {  // not keyed on yet (deferred-volume start)
                s.volume = uint8_t(s.newVolume);
                voiceVolumeRpc(s);
            }
            s.state = 3;
        }
    } else if (s.state == 3) {
        if ((s.descFlags & 0x20000) == 0) {
            if ((status_.word[0] & bit) == 0) {
                s.release = int16_t(s.release - 1);
                if (s.release == 0) clearSlot(s);
            } else {
                s.descFlags |= 0x20000;
            }
        } else if ((status_.word[0] & bit) == 0) {
            clearSlot(s);
        }
    }
}

// orig FUN_00213610 (work/decompiled/00213610.c)
long SoundSystem::synthSlotTick(SoundSlot& s) {
    if (s.state == 4) {
        param580_[1] = uint8_t(s.descX);                          // program
        param580_[2] = (uint32_t(s.volume) << 16) | s.noteProg;   // vol<<16 | note<<8 | duration
        param580_[3] = s.handle;
        param580_[4] = s.pan;
        rpc(0x410, s.iopSlot);
        s.state = 3;
        return 1;
    }
    if (s.state == 3) {
        const uint32_t f = s.descFlags;
        if ((f & 0x20000) == 0) {
            s.descFlags = f | 0x20000;
        } else if ((status_.word[0] & (1u << ((s.iopSlot + 2) & 0x1f))) == 0) {
            clearSlot(s);
        } else if (f & 0x40000000) {
            rpc(0x420, s.iopSlot);
            s.descFlags &= 0xbfffffffu;
            return 1;
        }
    }
    return 0;
}

// orig FUN_00212540 (work/decompiled/00212540.c) + the IOPSND.IRX RPC dispatcher (func 0x158, SOUND.md §3).
// RPCs complete synchronously (UNVERIFIED: FUN_002126b0 spins while the previous asynchronous call is busy and the
// status block read by cmd 0xf is one round trip old on the PS2).
uint32_t SoundSystem::rpc(uint32_t cmd, uint32_t param) {
    if (cmd != 0x8200 && (cmd & 0x1000) == 0) param580_[0] = param;
    const uint32_t op = cmd & 0xfff0;
    const int sub = int(cmd & 0xf);
    switch (op) {
    case 0x0000:   // func 0x1538: mode 0 module init, 1 re-init SPU/synth (after a movie), 2 stop ticking
        if (param == 1) iop_.reinit();
        else if (param == 2) iop_.setSuspended(true);
        break;
    case 0x0010: iop_.setOutputMode(true); break;    // func 0x175c
    case 0x0020: iop_.setOutputMode(false); break;
    case 0x0030: iop_.setMasterVolume(uint16_t(param & 0xffff), uint16_t(param >> 16)); break;   // func 0x1798
    case 0x0410: iop_.synthPlay(int(param580_[0]), param580_[1], param580_[2], param580_[3], param580_[4]); break;
    case 0x0420: iop_.synthStop(int(param)); break;
    case 0x0450: iop_.synthFade(sub, int(param), int(param580_[1]), int(param580_[2]), int(param580_[3])); break;
    case 0x0900: break;                               // func 0x2e00: stream subsystem init (threads)
    case 0x0950: iop_.streamSetVolume(sub, param); break;
    case 0x0980: iop_.bindStreamVoice(sub, int(param)); break;
    case 0x0a00: iop_.streamFade(sub, int(param), int(param580_[1]), int(param580_[2]), int(param580_[3])); break;
    case 0x0a10: iop_.streamPause(sub, param); break;
    case 0x0b00: break;                               // func 0x4cc4: register the EE mailbox 0x540600
    case 0x1040:                                      // func 0x17ec: sceSdSetEffectAttr (param = 0x540580)
        iop_.setEffect(param580_[0], param580_[2], int(param580_[3]), int(param580_[4]), int(param580_[5]),
                       int(param580_[6]), int(int16_t(param580_[7])));
        break;
    case 0x1290: {                                    // func 0x204c (param = 0x540640)
        const uint32_t mode = buf640Get32(0x0);
        const uint32_t entry = buf640Get32(0x4);
        if (mode == 4) {
            // open the archive named at +0x20 and read its sector table; the port resolves the disc file through
            // soundArchivePath() (work/disc/SOUND_EN/BRQSOUND.SFS).
            if (!sfs_.open(soundArchivePath())) error_ = sfs_.lastError();
            platform::log("[audio] sound archive " + soundArchivePath().string() + (error_.empty() ? "" : ": " + error_));
        } else if (mode == 2 || mode == 1) {
            std::string err;
            if (!bank_.parse(sfs_.read(entry), &err)) {
                error_ = "HD entry " + std::to_string(entry) + ": " + err;
                platform::log("[audio] SE bank not loaded (" + error_ + "): synth sound effects are silent");
            }
        } else if (mode == 3) {
            bank_.setBody(sfs_.read(entry));          // BD -> SPU RAM 0xb800 (sceSdVoiceTrans in 0x4000 chunks)
        }
        break;
    }
    case 0x1440: break;                               // func 0x2944: modhsyn bank bind (single bank in the model)
    case 0x1960:                                      // func 0x31a4 (param = 0x540640)
        iop_.streamStart(sub, buf640Get32(0x4), uint32_t(buf640_[0x18] | (buf640_[0x19] << 8)), buf640_[0x1a]);
        break;
    case 0x8200: status_ = iop_.status(); break;      // func 0x1b50
    case 0x8970: iop_.streamStop(int(param)); break;  // func 0x32a4
    default: break;                                   // 0x8f00 memstat and the debug file helpers
    }
    return 0;   // reply word 0x5405c0 (only the 0x1290 {3} result is stored, at 0x540650; unused)
}

// ===================================================================================== GameSoundManager

void GameSoundManager::mirror() {
    inited_ = sys_.enabled();
    currentBgm_ = sys_.currentBgmId();
    bgmVolume_ = sys_.bgmCategory();
    seVolume_ = sys_.seCategory();
    voiceVolume_ = sys_.voiceCategory();
}

void GameSoundManager::init() {
    sys_.enable();
    pitch_ = 0;
    mirror();
}

void GameSoundManager::playBgm(int bgmId, int vol, long flag) {
    sys_.playBgm(bgmId, vol, int(flag));
    mirror();
}

void GameSoundManager::stopBgm(long fadeFrames) {
    sys_.stopBgm(int(fadeFrames));
    mirror();
}

uint32_t GameSoundManager::playSe(float vol, int seId) { return sys_.playSe(vol, seId); }

void GameSoundManager::playVoice(float vol, int voiceId) { sys_.playVoiceSimple(vol, voiceId); }

void GameSoundManager::setPitch(long pitch) {
    if (!sys_.enabled()) return;
    sys_.setReverbVolume(pitch);
    pitch_ = pitch > 0x4000 ? 0x4000 : pitch < -0x3000 ? -0x3000 : pitch;
}

void GameSoundManager::setMono(bool mono) {
    sys_.setMono(mono);
    mono_ = mono;
}

void GameSoundManager::setBgmVolume(float v) {
    sys_.setBgmCategory(v);
    mirror();
}

void GameSoundManager::setSeVolume(float v) {
    sys_.setSeCategory(v);
    mirror();
}

void GameSoundManager::setVoiceVolume(float v) {
    sys_.setVoiceCategory(v);
    mirror();
}

void GameSoundManager::setBgmFadeTarget(float vol, int frames) { sys_.setBgmVolumeScale(vol, frames); }

void GameSoundManager::setMenuDuck(int on) { sys_.setSeOverrideFlag(on); }

void GameSoundManager::stopAll() {
    sys_.stopAllSe();          // FUN_00115900
    sys_.stopCurrentVoice();   // FUN_00115c10
    mirror();
}

namespace {
std::unique_ptr<SoundSystem> gSystem;
std::unique_ptr<GameSoundManager> gManager;
} // namespace

// PORT (voice language): the IOP model runs on the game thread (the device callback only drains the output ring), so
// the archive can be swapped between ticks once every stream / synth voice is stopped.
void SoundSystem::reloadArchive() {
    // the music keeps playing (IopSound::archiveReopened); voices and SEs belong to the old archive and stop
    stopVoice(0xfffe, 0);
    stopSe(0xfffe, 0);
    error_.clear();
    if (!sfs_.open(soundArchivePath())) error_ = sfs_.lastError();
    platform::log("[audio] sound archive " + soundArchivePath().string() + (error_.empty() ? "" : ": " + error_));
    iop_.archiveReopened();
    // the bank in slot 0 as last loaded by loadBank (FUN_00211860), from the new archive
    if (loadedHdEntry_[0] >= 0) {
        std::string err;
        if (!bank_.parse(sfs_.read(uint32_t(loadedHdEntry_[0])), &err))
            platform::log("[audio] SE bank not loaded (HD entry " + std::to_string(loadedHdEntry_[0]) + ": " + err + ")");
    }
    if (loadedBdEntry_[0] >= 0) bank_.setBody(sfs_.read(uint32_t(loadedBdEntry_[0])));
}

void reloadSoundArchive() {
    if (gSystem) gSystem->reloadArchive();
}

SoundSystem* installSoundSystem(bool openDevice) {
    uninstallSoundSystem();
    auto sys = std::make_unique<SoundSystem>();
    if (!sys->init(openDevice, 1, 1, 1)) {
        platform::log("[audio] " + sys->lastError() + " (sound stub stays installed)");
        return nullptr;
    }
    gSystem = std::move(sys);
    gManager = std::make_unique<GameSoundManager>(*gSystem);
    game::setSoundManager(gManager.get());
    return gSystem.get();
}

void uninstallSoundSystem() {
    if (gManager) game::setSoundManager(nullptr);
    gManager.reset();
    gSystem.reset();
}

} // namespace baroque::audio
