#pragma once
// PS2 EE FPU / VU0 float32 arithmetic on x64 SSE, matching tools/enbaya.py (the reference):
// round toward zero, denormals flushed to zero (results and inputs), overflow saturates to +-FLT_MAX
// (IEEE round-toward-zero already turns overflow into the largest finite value), multiply-add is two
// separately rounded operations, division by zero gives +-FLT_MAX (0/0 = 0).
//
// Usage: construct a Ps2FloatScope before running translated float code; it sets MXCSR (RC = toward zero,
// FTZ, DAZ) and restores the caller's mode on destruction. Translation units that do PS2 float math must
// use `#pragma fenv_access(on)` so MSVC neither constant-folds nor reorders under the default mode.
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <xmmintrin.h>

namespace baroque::alchemy::ps2 {

class Ps2FloatScope {
public:
    Ps2FloatScope() : saved_(_mm_getcsr()) {
        // RC bits 13..14 = 11 (toward zero), FTZ bit 15, DAZ bit 6.
        _mm_setcsr((saved_ & ~0x6000u) | 0x6000u | 0x8000u | 0x0040u);
    }
    ~Ps2FloatScope() { _mm_setcsr(saved_); }
    Ps2FloatScope(const Ps2FloatScope&) = delete;
    Ps2FloatScope& operator=(const Ps2FloatScope&) = delete;

private:
    unsigned saved_;
};

// The helpers below must only be called inside a Ps2FloatScope. They are plain float operations; the
// names document intent and keep the original operation order visible at call sites.
inline float fadd(float a, float b) { return a + b; }
inline float fsub(float a, float b) { return a - b; }
inline float fmul(float a, float b) { return a * b; }
inline float fdiv(float a, float b) {
    if (b == 0.0f) {
        if (a == 0.0f) return 0.0f;
        return a > 0.0f ? FLT_MAX : -FLT_MAX;  // a zero divisor counts as positive (tools/enbaya.py fdiv)
    }
    return a / b;
}
inline float fsqrt(float a) { return a <= 0.0f ? 0.0f : _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(a))); }
inline float frsqrt(float a) { return fdiv(1.0f, fsqrt(a)); }  // VU0 VRSQRT with numerator 1.0
inline float fint(int32_t n) { return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), n)); }  // cvt.s.w
inline float fmadd(float acc, float a, float b) { return fadd(acc, fmul(a, b)); }
// cvt.w.s: truncate, saturating on overflow.
inline int32_t ftoi(float q) {
    if (q >= 2147483648.0f) return 0x7fffffff;
    if (q <= -2147483648.0f) return int32_t(-0x7fffffff - 1);
    return int32_t(q);
}

}  // namespace baroque::alchemy::ps2
