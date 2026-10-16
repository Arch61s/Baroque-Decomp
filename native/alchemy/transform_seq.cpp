// igTransformSequence1_5 playback (see transform_seq.hpp).
#include "alchemy/transform_seq.hpp"

#include "alchemy/animation.hpp"
#include "alchemy/ps2_float.hpp"

#include <algorithm>
#include <cstring>

#pragma fenv_access(on)

namespace baroque::alchemy {

using namespace ps2;

namespace {

constexpr float kEps = 9.99999997e-07f;   // DAT_004e5bf4

template <size_t N>
std::vector<std::array<float, N>> readVecList(const IgbFile& f, const IgObject* list) {
    std::vector<std::array<float, N>> out;
    if (list == nullptr) return out;
    const IgMemory* m = f.memory(list->getRef("_data"));
    if (m == nullptr) return out;
    const int count = list->getInt("_count", int(m->size / (N * 4)));
    for (int i = 0; i < count && size_t(i + 1) * N * 4 <= m->size; ++i) {
        std::array<float, N> v{};
        std::memcpy(v.data(), &m->data[size_t(i) * N * 4], N * 4);
        out.push_back(v);
    }
    return out;
}

// (float)(uint32)(ns >> 18): the dsra 0x12 / cvt.s.w / unsigned fix-up of FUN_003fccb0 / FUN_003fca50
float units(int64_t ns) { return float(uint32_t(uint64_t(ns >> 0x12) & 0xffffffffu)); }

// orig FUN_003a02d0: (0 + a) + t * (b - a)
std::array<float, 3> lerp3(float t, const std::array<float, 3>& a, const std::array<float, 3>& b) {
    std::array<float, 3> o{};
    for (size_t k = 0; k < 3; ++k) o[k] = fadd(fadd(0.0f, a[k]), fmul(t, fsub(b[k], a[k])));
    return o;
}

// orig FUN_003a0000: b flipped to a's hemisphere, lerp, normalised (same as the Enbaya sampler's copy)
std::array<float, 4> nlerp4(float t, const std::array<float, 4>& a, std::array<float, 4> b) {
    float d = fmul(a[1], b[1]);
    d = fmadd(d, a[0], b[0]);
    d = fmadd(d, a[2], b[2]);
    d = fmadd(d, a[3], b[3]);
    if (d < 0.0f)
        for (float& x : b) x = fmul(-1.0f, x);
    const float omt = fsub(1.0f, t);
    std::array<float, 4> o{};
    for (size_t k = 0; k < 4; ++k) o[k] = fmadd(fmul(t, b[k]), omt, a[k]);
    float acc = fmul(o[1], o[1]);
    acc = fmadd(acc, o[0], o[0]);
    acc = fmadd(acc, o[2], o[2]);
    acc = fmadd(acc, o[3], o[3]);
    const float inv = fdiv(1.0f, fsqrt(acc));
    for (float& x : o) x = fmul(x, inv);
    return o;
}

}  // namespace

bool TransformSequence::load(const IgbFile& f, const IgObject* seq, std::vector<std::string>* log) {
    valid = false;
    if (seq == nullptr || !seq->isA("igTransformSequence1_5")) {
        if (log && seq) log->push_back("transform input " + std::string(seq->cls()) + " not supported");
        return false;
    }
    xlate = readVecList<3>(f, f.object(seq->getRef("_xlateList")));
    quat = readVecList<4>(f, f.object(seq->getRef("_quatList")));
    scale = readVecList<3>(f, f.object(seq->getRef("_scaleList")));
    times.clear();
    if (const IgObject* tl = f.object(seq->getRef("_timeListLong"))) {
        if (const IgMemory* m = f.memory(tl->getRef("_data"))) {
            const int count = tl->getInt("_count", int(m->size / 8));
            for (int i = 0; i < count && size_t(i + 1) * 8 <= m->size; ++i) {
                int64_t v;
                std::memcpy(&v, &m->data[size_t(i) * 8], 8);
                times.push_back(v);
            }
        }
    }
    uniformTime = seq->getBool("_uniformTimeEnabled", false);
    playMode = seq->getInt("_playMode", 0);
    float c[3] = {0, 0, 0};
    if (seq->getFloats("_centerOfRotation", c, 3)) center = {c[0], c[1], c[2]};
    channels = uint8_t(seq->getUInt("_drivenChannels", 0));
    if (const Value* im = seq->find("_interpolationMethod"))
        for (size_t k = 0; k < 3 && k < im->raw.size(); ++k) interp[k] = im->raw[k];
    keyOffset = seq->getLong("_keyframeTimeOffset", 0);
    duration = seq->getLong("_animationDuration", 0);
    lastKey = 1;
    if (uniformTime || (channels & 4) != 0 || interp[0] > 1 || interp[2] > 1 || times.empty()) {
        // UNVERIFIED (not in the corpus): FUN_003fcbd0 uniform time, the Euler channel (FUN_0039dff0) and the
        // Hermite interpolation data (FUN_003a04d0) are not translated.
        if (log) log->push_back("igTransformSequence1_5 layout not supported (uniform/euler/hermite/no keys)");
        return false;
    }
    valid = true;
    return true;
}

// orig FUN_003fc680(t, seq, out, k0, k1)
Mat4 TransformSequence::keyMatrix(float t, int k0, int k1) const {
    Mat4 m = Mat4::identity();   // FUN_0039d2e0
    auto at = [](const auto& v, int i) { return v[size_t(std::min<int>(std::max(i, 0), int(v.size()) - 1))]; };
    if ((channels & 2) != 0 && !quat.empty()) {
        std::array<float, 4> q;   // FUN_003fc9b0
        if (interp[1] == 0) q = at(quat, k0);
        else if (interp[1] == 3) q = quatSlerpPoly(t, at(quat, k0), at(quat, k1));   // FUN_0039fe10
        else q = nlerp4(t, at(quat, k0), at(quat, k1));                               // FUN_003a0000
        m = quatToMatrix(q, {0.0f, 0.0f, 0.0f});                                      // FUN_0039fb80
        // FUN_003a0330(c', c, m): translation = c - c * R (rotation about the centre)
        const float* r = m.m.data();
        float cr[3];
        for (int j = 0; j < 3; ++j)
            cr[j] = fadd(fadd(fmul(center[0], r[j]), fmul(center[1], r[4 + j])), fmul(center[2], r[8 + j]));
        m.m[12] = fsub(center[0], cr[0]);
        m.m[13] = fsub(center[1], cr[1]);
        m.m[14] = fsub(center[2], cr[2]);
    }
    if ((channels & 1) != 0 && !xlate.empty()) {   // FUN_003fc590
        const std::array<float, 3> v = interp[0] == 0 ? at(xlate, k0) : lerp3(t, at(xlate, k0), at(xlate, k1));
        m.m[12] = fadd(m.m[12], v[0]);
        m.m[13] = fadd(m.m[13], v[1]);
        m.m[14] = fadd(m.m[14], v[2]);
    }
    if ((channels & 8) != 0 && !scale.empty()) {   // FUN_0039dc30: rows 0..2 scaled by s.x, s.y, s.z
        const std::array<float, 3> s = interp[2] == 0 ? at(scale, k0) : lerp3(t, at(scale, k0), at(scale, k1));
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) m.m[size_t(r * 4 + c)] = fmul(m.m[size_t(r * 4 + c)], s[size_t(r)]);
    }
    return m;
}

// orig FUN_003fccb0 (work/decompiled/003fccb0.c); vt+0xf0(seq, i, out) = the matrix of key i.
Mat4 TransformSequence::evaluate(int64_t time) const {
    Ps2FloatScope fp;
    if (!valid) return Mat4::identity();
    const int n = int(times.size());
    time -= keyOffset;
    const int64_t first = times[0];
    const int64_t last = times[size_t(n - 1)];
    const int64_t end = first + duration;
    if (duration == 0) return keyMatrix(0.0f, 0, 0);
    if (end < time) {
        if (playMode == 2) {   // FUN_003fb270: quotient / remainder of (time - first) by the duration
            const int64_t q = (time - first) / duration;
            const int64_t rem = (time - first) - q * duration;
            time = (q & 1) == 0 ? first + rem : end - rem;
        } else if (playMode == 0 || playMode == 3) {
            const int64_t q = (time - first) / duration;
            time = first + ((time - first) - q * duration);
            if (lastKey != 1 && lastKey - 1 < n && time < times[size_t(lastKey - 1)]) lastKey = 1;
        } else if (playMode == 1) {
            return keyMatrix(0.0f, n - 1, n - 1);
        } else {
            return keyMatrix(0.0f, 0, 0);
        }
    } else if (time < first) {
        return keyMatrix(0.0f, 0, 0);
    }
    int k0 = 0, k1 = 0;
    float frac = 0.0f;
    if (last < time) {
        if (playMode != 3) return keyMatrix(0.0f, n - 1, n - 1);   // vt+0x94 = key count
        k0 = n - 1;
        k1 = 0;
        const float span = units(end - last);
        frac = span < kEps ? 0.0f : fdiv(units(time - last), span);
    } else {   // orig FUN_003fca50: bracket search from the cached key
        int idx = std::min(std::max(lastKey, 1), n - 1);
        if (n == 1) return keyMatrix(0.0f, 0, 0);
        if (times[size_t(idx)] < time) {
            while (idx + 1 < n && times[size_t(idx)] < time) ++idx;
        } else {
            while (idx > 1 && time < times[size_t(idx - 1)]) --idx;
        }
        lastKey = idx;
        k0 = idx - 1;
        k1 = idx;
        const float span = units(times[size_t(k1)] - times[size_t(k0)]);
        frac = span < kEps ? 0.0f : fdiv(units(time - times[size_t(k0)]), span);
    }
    return keyMatrix(frac, k0, k1);
}

}  // namespace baroque::alchemy
