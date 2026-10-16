// Enbaya decoder, native port of tools/enbaya.py (see enbaya.hpp for the translated functions).
#include "alchemy/enbaya.hpp"

#include "alchemy/ps2_float.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#pragma fenv_access(on)

namespace baroque::alchemy::enbaya {

using namespace ps2;

namespace {

float bits(uint32_t u) { return std::bit_cast<float>(u); }

// Constants read from the ELF.
const float kTimeScale = bits(0x3989705f);     // 0x4e5c28: float(2**18 / 1e9)
const float kEps1e6 = bits(0x358637bd);        // 0x4e5bf4
const float kEps1e5 = bits(0x3727c5ac);        // 0x4e5c2c
const float kSlerpLerpEps = bits(0x3da3d70a);  // 0x4e5c10: linear blend when 1 - dot <= this
constexpr int kShift2[4] = {6, 4, 2, 0};       // 0x489ae0 / 0x489af0 / 0x4899c0: 2-bit codes, MSB first
constexpr int kShift4[2] = {4, 0};             // 0x4899d0: nibbles, high first
constexpr int32_t kDelta2[4] = {0, 1, 0, -1};  // 0x4c1760 (code 2 = escape to nibble)
constexpr int32_t kDelta4[16] = {0, 8, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -9};  // 0x4c1770 (0 = escape)
// Polynomial slerp weight tables, rows A0..A3 at 0x4851b0 and B0..B3 at 0x4851f0 (FUN_0039fe10).
constexpr uint32_t kSlerpABits[4][4] = {{0xbfd6becc, 0x3f6ecfa5, 0xbe9a73ac, 0x3d3e7c98},
                                        {0x4010cd11, 0xbf9147b3, 0x3eb5c6cd, 0xbfbdc423},
                                        {0xbf15b6ab, 0x3e4eff03, 0xbd5a990e, 0x3edf40f7},
                                        {0x3b80de19, 0xbb84242b, 0x3ac021e3, 0x3f7fa67b}};
constexpr uint32_t kSlerpBBits[4][4] = {{0xbf8be375, 0x3f3b0fdb, 0xbe7e40fd, 0x3fce23a7},
                                        {0x3f02103a, 0xbf07500b, 0x3e479a91, 0xbe329b4d},
                                        {0x3f15b6b0, 0xbe4eff44, 0x3d5a99b9, 0xbedf40f6},
                                        {0x3b80de25, 0xbb842430, 0x3ac021c7, 0xbab30999}};

// File order of the sections after the header (FUN_003fe450 pointer arithmetic).
constexpr Header::Section kSectionOrder[Header::SectionCount] = {
    Header::InitS32, Header::DeltaS32, Header::MaskU32, Header::InitS16, Header::DeltaS16, Header::MaskU16,
    Header::InitCodes, Header::InitS8, Header::DeltaCodes, Header::DeltaNib, Header::DeltaS8,
    Header::MaskCodes, Header::MaskU8};

template <typename T>
T readLE(const std::vector<uint8_t>& d, size_t o) {
    T v;
    std::memcpy(&v, d.data() + o, sizeof(T));
    return v;
}

using Q4 = std::array<float, 4>;
using V3 = std::array<float, 3>;

// mula/madda/madd order of FUN_0039fe10 and FUN_003a0000: y, x, z, w
float quatDot(const Q4& a, const Q4& b) {
    float acc = fmul(a[1], b[1]);
    acc = fmadd(acc, a[0], b[0]);
    acc = fmadd(acc, a[2], b[2]);
    return fmadd(acc, a[3], b[3]);
}

// orig FUN_003a02d0: out = (0 + a) + t * (b - a)
V3 vecLerp(float t, const V3& a, const V3& b) {
    V3 o;
    for (int k = 0; k < 3; ++k) o[size_t(k)] = fmadd(fadd(0.0f, a[size_t(k)]), t, fsub(b[size_t(k)], a[size_t(k)]));
    return o;
}

// orig FUN_003a0000: flip b to a's hemisphere, lerp, normalise with 1/sqrt.
Q4 quatNlerp(float t, const Q4& a, Q4 b) {
    if (quatDot(a, b) < 0.0f)
        for (float& x : b) x = fmul(-1.0f, x);
    const float omt = fsub(1.0f, t);
    Q4 out;
    for (int k = 0; k < 4; ++k) out[size_t(k)] = fmadd(fmul(t, b[size_t(k)]), omt, a[size_t(k)]);
    float acc = fmul(out[1], out[1]);
    acc = fmadd(acc, out[0], out[0]);
    acc = fmadd(acc, out[2], out[2]);
    acc = fmadd(acc, out[3], out[3]);
    const float inv = fdiv(1.0f, fsqrt(acc));
    for (float& x : out) x = fmul(x, inv);
    return out;
}

// P = rows[0]*t + rows[1]*t^2 + rows[2]*t^3 + rows[3]   (vmulax/vmadday/vmaddaz/vmaddw)
Q4 poly4(const uint32_t rows[4][4], float t) {
    const float t2 = fmul(t, t);
    const float t3 = fmul(t2, t);
    Q4 out;
    for (int lane = 0; lane < 4; ++lane) {
        float acc = fmul(bits(rows[0][lane]), t);
        acc = fmadd(acc, bits(rows[1][lane]), t2);
        acc = fmadd(acc, bits(rows[2][lane]), t3);
        out[size_t(lane)] = fmadd(acc, bits(rows[3][lane]), 1.0f);
    }
    return out;
}

// orig FUN_0039fe10: polynomial slerp (VU0). a = previous frame, b = current frame.
Q4 quatSlerp(float t, const Q4& a, Q4 b) {
    float dot = quatDot(a, b);
    if (dot < 0.0f) {
        for (float& x : b) x = fmul(-1.0f, x);
        dot = -dot;
    }
    Q4 out;
    if (fsub(1.0f, dot) <= kSlerpLerpEps) {
        const float omt = fsub(1.0f, t);
        for (int k = 0; k < 4; ++k) out[size_t(k)] = fmadd(fmul(t, b[size_t(k)]), omt, a[size_t(k)]);
        return out;
    }
    const float c2 = fmul(dot, dot);
    const float c3 = fmul(c2, dot);
    const Q4 pa = poly4(kSlerpABits, t);
    const Q4 pb = poly4(kSlerpBBits, t);
    // weights: ((P.w*1 + P.x*c) + P.y*c^2) + P.z*c^3
    const float wa = fadd(fadd(fadd(fmul(pa[3], 1.0f), fmul(pa[0], dot)), fmul(pa[1], c2)), fmul(pa[2], c3));
    const float wb = fadd(fadd(fadd(fmul(pb[3], 1.0f), fmul(pb[0], dot)), fmul(pb[1], c2)), fmul(pb[2], c3));
    for (int k = 0; k < 4; ++k) out[size_t(k)] = fmadd(fmul(a[size_t(k)], wa), b[size_t(k)], wb);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------------------------
std::optional<Header> Header::parse(const std::vector<uint8_t>& data, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<Header> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (data.size() < kHeaderSize) return fail("stream shorter than header");
    Header h;
    h.word0 = readLE<uint32_t>(data, 0);
    h.boneCount = readLE<int32_t>(data, 4);
    h.scale = readLE<float>(data, 8);
    h.duration = readLE<float>(data, 0xc);
    h.fps = readLE<int32_t>(data, 0x10);
    for (int i = 0; i < SectionCount; ++i) h.sizes[size_t(i)] = readLE<uint32_t>(data, 0x14 + size_t(i) * 4);
    h.flagsCount = readLE<uint32_t>(data, 0x48);
    h.dataPtr = readLE<uint32_t>(data, 0x4c);
    uint64_t pos = kHeaderSize;
    for (Section s : kSectionOrder) {
        h.offsets[s] = uint32_t(pos);
        pos += h.sizes[s];
    }
    h.flagsOffset = uint32_t(pos);
    const uint64_t total = pos + h.flagsCount;
    if (h.boneCount <= 0) return fail("bad bone count " + std::to_string(h.boneCount));
    if (total > data.size())
        return fail("stream truncated: need " + std::to_string(total) + " bytes, have " + std::to_string(data.size()));
    h.totalSize = uint32_t(total);
    Ps2FloatScope fp;
    h.dt = fdiv(1.0f, fint(h.fps));  // FUN_003fe450: ctx+0x24 = 1.0f / (float)fps
    return h;
}

// ---------------------------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------------------------
Decoder::Decoder(const std::vector<uint8_t>* data, const Header* header)
    : data_(data), hdr_(header), n_(header->boneCount), bones_(size_t(header->boneCount)) {
    resetCursors();
}

// The runtime reads straight from memory; out-of-range reads return 0 here (counted).
#define BAROQUE_ENBAYA_READ(Name, T)                                         \
    T Decoder::Name(int64_t o) {                                             \
        if (o < 0 || uint64_t(o) + sizeof(T) > data_->size()) {              \
            ++oob_;                                                          \
            return 0;                                                        \
        }                                                                    \
        return readLE<T>(*data_, size_t(o));                                 \
    }
BAROQUE_ENBAYA_READ(s8, int8_t)
BAROQUE_ENBAYA_READ(u8, uint8_t)
BAROQUE_ENBAYA_READ(s16, int16_t)
BAROQUE_ENBAYA_READ(u16, uint16_t)
BAROQUE_ENBAYA_READ(s32, int32_t)
BAROQUE_ENBAYA_READ(u32, uint32_t)
#undef BAROQUE_ENBAYA_READ

// orig FUN_003fe3d0: cursors back to the section starts.
void Decoder::resetCursors() {
    const auto& o = hdr_->offsets;
    ic_ = o[Header::InitCodes]; icBit_ = 0;
    i8_ = o[Header::InitS8]; i16_ = o[Header::InitS16]; i32_ = o[Header::InitS32];
    dc_ = o[Header::DeltaCodes]; dcBit_ = 0; dn_ = o[Header::DeltaNib]; dnNib_ = 0;
    d8_ = o[Header::DeltaS8]; d16_ = o[Header::DeltaS16]; d32_ = o[Header::DeltaS32];
    mc_ = o[Header::MaskCodes]; mcBit_ = 0;
    m8_ = o[Header::MaskU8]; m16_ = o[Header::MaskU16]; m32_ = o[Header::MaskU32];
}

int Decoder::initCode() {
    if (icBit_ == 4) { icBit_ = 0; ++ic_; }
    const int c = (s8(ic_) >> kShift2[icBit_]) & 3;
    ++icBit_;
    return c;
}

int Decoder::maskCodeFwd() {
    if (mcBit_ == 4) { mcBit_ = 0; ++mc_; }
    const int c = (s8(mc_) >> kShift2[mcBit_]) & 3;
    ++mcBit_;
    return c;
}

int Decoder::maskCodeBack() {
    --mcBit_;
    if (mcBit_ == -1) { mcBit_ = 3; --mc_; }
    return (s8(mc_) >> kShift2[mcBit_]) & 3;
}

uint32_t Decoder::maskValueFwd(int c) {
    uint32_t v = 0;
    if (c == 3) { v = u32(m32_); m32_ += 4; }
    else if (c == 2) { v = u16(m16_); m16_ += 2; }
    else if (c == 1) { v = u8(m8_); m8_ += 1; }
    return v;
}

uint32_t Decoder::maskValueBack(int c) {
    uint32_t v = 0;
    if (c == 3) { m32_ -= 4; v = u32(m32_); }
    else if (c == 2) { m16_ -= 2; v = u16(m16_); }
    else if (c == 1) { m8_ -= 1; v = u8(m8_); }
    return v;
}

// vmul; vaddw.z; vaddz.y; vaddy.x; vrsqrt Q=1/sqrt; vmulq  (FUN_003fe990 / FUN_003fea90)
void Decoder::normalize(std::array<float, 4>& q) {
    const float x = fmul(q[0], q[0]), y = fmul(q[1], q[1]), z = fmul(q[2], q[2]), w = fmul(q[3], q[3]);
    const float dot = fadd(x, fadd(y, fadd(z, w)));
    const float inv = frsqrt(dot);
    for (float& c : q) c = fmul(c, inv);
}

// orig FUN_003ff8c0 / FUN_003ff0b0 / FUN_003fea90: reset to frame 0.
void Decoder::reset() {
    frame_ = 0;
    tCur_ = 0.0f;
    tPrev_ = 0.0f;
    resetCursors();
    direction_ = 0;
    run_ = maskValueFwd(maskCodeFwd());  // FUN_003ff8c0: first run length
    consumed_ = 0;
    // FUN_003ff0b0: 7 signed initial values per bone (quat x,y,z,w, pos x,y,z) as float32
    for (Bone& b : bones_) {
        for (int c = 0; c < 7; ++c) {
            const int code = initCode();
            int32_t v = 0;
            if (code == 3) { v = s32(i32_); i32_ += 4; }
            else if (code == 2) { v = s16(i16_); i16_ += 2; }
            else if (code == 1) { v = s8(i8_); i8_ += 1; }
            b.state[size_t(c)] = fint(v);
        }
    }
    // FUN_003fea90: scale, normalise, zero the accumulators, load the initial channel masks
    const float sc = hdr_->scale;
    for (size_t i = 0; i < bones_.size(); ++i) {
        Bone& b = bones_[i];
        std::array<float, 4> q{};
        std::array<float, 3> p{};
        for (int k = 0; k < 4; ++k) q[size_t(k)] = fmul(b.state[size_t(k)], sc);
        for (int k = 0; k < 3; ++k) p[size_t(k)] = fmul(b.state[size_t(4 + k)], sc);
        normalize(q);
        b.q[0] = q; b.q[1] = q;
        b.p[0] = p; b.p[1] = p;
        b.t = {0.0f, 0.0f};
        b.state.fill(0.0f);
        b.mask = u8(int64_t(hdr_->flagsOffset) + int64_t(i));
    }
    cur_ = 0;
    initialised_ = true;
}

// orig FUN_003ff990: forward channel-mask run lengths (toggle one channel at the end of each run).
void Decoder::maskForward() {
    const int64_t N = int64_t(n_) * 7;
    int64_t pos = 0;
    while (true) {
        while (pos < N) {
            if (run_ == 0) break;
            const int64_t take = std::min<int64_t>(run_, N - pos);
            pos += take;
            run_ -= uint32_t(take);
            consumed_ += uint32_t(take);
        }
        if (pos >= N) return;
        bones_[size_t(pos / 7)].mask ^= 1u << (pos % 7);
        run_ = maskValueFwd(maskCodeFwd());
        ++pos;
        consumed_ = 0;
    }
}

// orig FUN_003ffbe0: backward channel-mask run lengths.
void Decoder::maskBackward() {
    const int64_t N = int64_t(n_) * 7;
    int64_t pos = N - 1;
    while (true) {
        while (pos >= 0) {
            if (consumed_ == 0) break;
            const int64_t take = std::min<int64_t>(consumed_, pos + 1);
            pos -= take;
            run_ += uint32_t(take);
            consumed_ -= uint32_t(take);
        }
        if (pos < 0) return;
        bones_[size_t(pos / 7)].mask ^= 1u << (pos % 7);
        consumed_ = maskValueBack(maskCodeBack());
        --pos;
        run_ = 0;
    }
}

// Forward step after a backward one (FUN_003feb40): step over the current run's code.
void Decoder::maskSkipCode() {
    const int c = maskCodeFwd();
    if (c == 3) m32_ += 4;
    else if (c == 2) m16_ += 2;
    else if (c == 1) m8_ += 1;
}

// Backward step after a forward one (FUN_003feb40): back up over the current run's code.
void Decoder::maskUnreadCode() {
    const int c = maskCodeBack();
    if (c == 3) m32_ -= 4;
    else if (c == 2) m16_ -= 2;
    else if (c == 1) m8_ -= 1;
}

int32_t Decoder::deltaFwd() {
    if (dcBit_ == 4) { dcBit_ = 0; ++dc_; }
    const int c = (s8(dc_) >> kShift2[dcBit_]) & 3;
    ++dcBit_;
    if (c != 2) return kDelta2[c];
    if (dnNib_ == 2) { dnNib_ = 0; ++dn_; }
    const int nib = (s8(dn_) >> kShift4[dnNib_]) & 0xf;
    ++dnNib_;
    if (nib != 0) return kDelta4[nib];
    int32_t v = s8(d8_);
    d8_ += 1;
    if (v == 0) {
        v = s16(d16_);
        d16_ += 2;
        if (v == 0) {
            v = s32(d32_);
            d32_ += 4;
        }
        return v;
    }
    if (v >= 1 && v <= 8) return v + 0x7f;
    if (v >= -8 && v <= -1) return v - 0x80;
    return v;
}

int32_t Decoder::deltaBack() {
    --dcBit_;
    if (dcBit_ == -1) { dcBit_ = 3; --dc_; }
    const int c = (s8(dc_) >> kShift2[dcBit_]) & 3;
    if (c != 2) return kDelta2[c];
    --dnNib_;
    if (dnNib_ == -1) { dnNib_ = 1; --dn_; }
    const int nib = (s8(dn_) >> kShift4[dnNib_]) & 0xf;
    if (nib != 0) return kDelta4[nib];
    d8_ -= 1;
    int32_t v = s8(d8_);
    if (v == 0) {
        d16_ -= 2;
        v = s16(d16_);
        if (v == 0) {
            d32_ -= 4;
            v = s32(d32_);
        }
        return v;
    }
    if (v >= 1 && v <= 8) return v + 0x7f;
    if (v >= -8 && v <= -1) return v - 0x80;
    return v;
}

// orig FUN_003ff1d0
void Decoder::deltasForward() {
    for (Bone& b : bones_) {
        const uint32_t m = b.mask;
        if (m == 0) continue;
        for (int c = 0; c < 7; ++c)
            if (m & (1u << c)) b.state[size_t(c)] = fadd(b.state[size_t(c)], fint(deltaFwd()));
    }
}

// orig FUN_003ff3f0: iterates bones n-1 .. 1 (bgtzl) -- bone 0 is never reversed (original quirk).
void Decoder::deltasBackward() {
    for (int i = n_ - 1; i > 0; --i) {
        Bone& b = bones_[size_t(i)];
        const uint32_t m = b.mask;
        if (m == 0) continue;
        for (int c = 6; c >= 0; --c)
            if (m & (1u << c)) b.state[size_t(c)] = fsub(b.state[size_t(c)], fint(deltaBack()));
    }
}

// orig FUN_003fe990: integrate one frame into the other buffer.
void Decoder::integrate(float scale, float time, bool forward) {
    cur_ ^= 1;
    const int dst = forward ? cur_ : (cur_ ^ 1);
    const int src = forward ? (cur_ ^ 1) : cur_;
    for (Bone& b : bones_) {
        std::array<float, 4> q{};
        std::array<float, 3> p{};
        const auto& sq = b.q[size_t(src)];
        const auto& sp = b.p[size_t(src)];
        for (int k = 0; k < 4; ++k) q[size_t(k)] = fadd(sq[size_t(k)], fmul(b.state[size_t(k)], scale));
        for (int k = 0; k < 3; ++k) p[size_t(k)] = fadd(sp[size_t(k)], fmul(b.state[size_t(4 + k)], scale));
        normalize(q);
        b.q[size_t(dst)] = q;
        b.p[size_t(dst)] = p;
        b.t[size_t(dst)] = time;
    }
}

// orig FUN_003feb40: move the context so that tPrev <= t <= tCur.
void Decoder::seek(float t) {
    const float last = lastT_;
    bool reset_;
    const bool first = (last == -1.0f) || (t < kEps1e6);
    if (first) {
        reset_ = true;
    } else if (fsub(last, t) <= t) {
        reset_ = (hdr_->dt <= last) && (t < hdr_->dt);
    } else {
        reset_ = true;
    }
    if (reset_) reset();
    if (t >= kEps1e6) {
        const float dur = hdr_->duration, dt = hdr_->dt, sc = hdr_->scale;
        while (tCur_ < t && fsub(dur, tCur_) > kEps1e5) {
            if (direction_ == 2) {
                maskSkipCode();
                direction_ = 1;
            } else if (frame_ > 0) {
                maskForward();
                direction_ = 1;
            }
            ++frame_;
            deltasForward();
            float tf = fmul(fint(frame_), dt);
            if (dur <= tf) tf = dur;
            integrate(sc, tf, true);
            tCur_ = fmul(fint(frame_), dt);
            tPrev_ = fmul(fint(frame_ - 1), dt);
        }
        while (t < tPrev_) {
            if (direction_ == 1) {
                maskUnreadCode();
                direction_ = 2;
            } else {
                maskBackward();
            }
            --frame_;
            deltasBackward();
            const float tf = fmul(fint(frame_ - 1), dt);
            integrate(-sc, tf, false);
            tCur_ = fmul(fint(frame_), dt);
            tPrev_ = tf;
        }
    }
    lastT_ = t;
}

// orig FUN_003fefc0
Decoder::FramePair Decoder::framesAt(float t, int bone) {
    if (!(tPrev_ <= t && t <= tCur_)) seek(t);
    const Bone& b = bones_[size_t(bone)];
    const int c = cur_, o = cur_ ^ 1;
    return {&b.q[size_t(o)], &b.p[size_t(o)], b.t[size_t(o)], &b.q[size_t(c)], &b.p[size_t(c)], b.t[size_t(c)]};
}

bool Decoder::stepForward() {
    if (!initialised_) reset();
    const float dur = hdr_->duration, dt = hdr_->dt, sc = hdr_->scale;
    if (fsub(dur, tCur_) <= kEps1e5) return false;
    if (direction_ == 2) {
        maskSkipCode();
        direction_ = 1;
    } else if (frame_ > 0) {
        maskForward();
        direction_ = 1;
    }
    ++frame_;
    deltasForward();
    float tf = fmul(fint(frame_), dt);
    if (dur <= tf) tf = dur;
    integrate(sc, tf, true);
    tCur_ = fmul(fint(frame_), dt);
    tPrev_ = fmul(fint(frame_ - 1), dt);
    return true;
}

std::array<std::array<uint32_t, 2>, Header::SectionCount> Decoder::cursorReport() const {
    const auto& o = hdr_->offsets;
    const auto& s = hdr_->sizes;
    std::array<std::array<uint32_t, 2>, Header::SectionCount> r{};
    auto put = [&](Header::Section sec, int64_t consumed) { r[sec] = {uint32_t(consumed), s[sec]}; };
    put(Header::InitCodes, ic_ - o[Header::InitCodes] + (icBit_ ? 1 : 0));
    put(Header::InitS8, i8_ - o[Header::InitS8]);
    put(Header::InitS16, i16_ - o[Header::InitS16]);
    put(Header::InitS32, i32_ - o[Header::InitS32]);
    put(Header::DeltaCodes, dc_ - o[Header::DeltaCodes] + (dcBit_ ? 1 : 0));
    put(Header::DeltaNib, dn_ - o[Header::DeltaNib] + (dnNib_ ? 1 : 0));
    put(Header::DeltaS8, d8_ - o[Header::DeltaS8]);
    put(Header::DeltaS16, d16_ - o[Header::DeltaS16]);
    put(Header::DeltaS32, d32_ - o[Header::DeltaS32]);
    put(Header::MaskCodes, mc_ - o[Header::MaskCodes] + (mcBit_ ? 1 : 0));
    put(Header::MaskU8, m8_ - o[Header::MaskU8]);
    put(Header::MaskU16, m16_ - o[Header::MaskU16]);
    put(Header::MaskU32, m32_ - o[Header::MaskU32]);
    return r;
}

// ---------------------------------------------------------------------------------------------
// Source (igEnbayaAnimationSource)
// ---------------------------------------------------------------------------------------------
float timeNsToSeconds(int64_t timeNs) {
    Ps2FloatScope fp;
    const int32_t v = int32_t(uint32_t(uint64_t(timeNs) >> 18));
    return fmul(kTimeScale, fint(v));
}

Source::Source(std::vector<uint8_t> stream, const Header& h, int playMode, std::array<int, 3> interp)
    : data_(std::move(stream)), header_(h), playMode_(playMode), interp_(interp), decoder_(&data_, &header_) {}

Source::Source(Source&& o) noexcept
    : data_(std::move(o.data_)), header_(o.header_), playMode_(o.playMode_), interp_(o.interp_),
      decoder_(&data_, &header_), fmod_() {}

std::optional<Source> Source::create(std::vector<uint8_t> stream, int playMode, std::array<int, 3> interpolation,
                                     std::string* error) {
    const std::optional<Header> h = Header::parse(stream, error);
    if (!h) return std::nullopt;
    return Source(std::move(stream), *h, playMode, interpolation);
}

int Source::frameCount() const {
    Ps2FloatScope fp;
    int n = 0;
    const float dt = header_.dt, dur = header_.duration;
    float t = 0.0f;
    while (fsub(dur, t) > kEps1e5) {
        ++n;
        t = fmul(fint(n), dt);
        if (n > (1 << 20)) break;
    }
    return n + 1;
}

// orig FUN_003da790: cached float modulo.
void Source::fmodf(float t, float d, int32_t& n, float& rem) {
    if (fmod_.valid && fmod_.t == t && fmod_.d == d) {
        n = fmod_.n;
        rem = fmod_.rem;
        return;
    }
    const float q = fdiv(t, d);
    n = ftoi(q);
    rem = fsub(fadd(t, 0.0f), fmul(fint(n), d));
    if (rem <= 0.0f) rem = 0.0f;
    fmod_ = {true, t, d, rem, n};
}

// orig FUN_003da850
float Source::localTime(int64_t timeNs) {
    const float t = timeNsToSeconds(timeNs);
    Ps2FloatScope fp;
    const float dur = header_.duration;
    int32_t n = 0;
    float rem = 0.0f;
    switch (playMode_) {
    case kPlayPingPong:
        fmodf(t, dur, n, rem);
        return (n & 1) ? fsub(dur, rem) : rem;
    case kPlayLoop:
    case kPlayLoop2:
        fmodf(t, dur, n, rem);
        return rem;
    case kPlayClamp:
        if (t <= dur) {
            if (t < 0.0f) return 0.0f;
            fmodf(t, dur, n, rem);
            return rem;
        }
        return dur;
    default:
        return 0.0f;
    }
}

// orig FUN_003da570
void Source::sample(float t, int track, Quat& rot, bool& hasRot, Vec3& trans, bool& hasTrans) {
    Ps2FloatScope fp;
    const Decoder::FramePair f = decoder_.framesAt(t, track);
    const float frac = fdiv(fsub(t, f.tPrev), header_.dt);
    const int tm = interp_[0], rm = interp_[1];
    hasTrans = true;
    V3 tv{};
    if (tm == kInterpStep) tv = *f.pCur;
    else if (tm == kInterpLerp) tv = vecLerp(frac, *f.pPrev, *f.pCur);
    else hasTrans = false;
    hasRot = true;
    Q4 qv{};
    if (rm == kInterpStep) qv = *f.qCur;
    else if (rm == kInterpLerp) qv = quatNlerp(frac, *f.qPrev, *f.qCur);
    else if (rm == kInterpSlerp) qv = quatSlerp(frac, *f.qPrev, *f.qCur);
    else hasRot = false;
    trans = {tv[0], tv[1], tv[2]};
    rot = {qv[0], qv[1], qv[2], qv[3]};
}

void Source::evaluate(int64_t timeNs, int track, Quat& rot, bool& hasRot, Vec3& trans, bool& hasTrans) {
    sample(localTime(timeNs), track, rot, hasRot, trans, hasTrans);
}

}  // namespace baroque::alchemy::enbaya
