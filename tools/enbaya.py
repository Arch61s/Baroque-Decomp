"""Enbaya compressed skeletal animation decoder (Intrinsic Alchemy, Baroque PS2 SLUS_217.14).

Bit-exact re-implementation of the runtime decoder found in the ELF:
  FUN_003fe450 context init, FUN_003feb40 seek, FUN_003ff0b0 initial pose, FUN_003fea90 pose
  setup, FUN_003ff990/FUN_003ffbe0 channel-mask run-lengths, FUN_003ff1d0/FUN_003ff3f0 delta
  decode, FUN_003fe990 integrate (VU0), FUN_003fefc0 frame lookup, FUN_003da850/FUN_003da570
  igEnbayaAnimationSource sampling (play mode, lerp FUN_003a02d0, nlerp FUN_003a0000,
  polynomial slerp FUN_0039fe10).  See work/reports/ENBAYA_FORMAT.md.

All arithmetic is done in emulated PS2 float32: round-toward-zero, denormals flushed to zero,
overflow saturates to FLT_MAX (EE FPU / VU0 behaviour).  Multiply-adds are treated as two
separately rounded operations.  Only the hardware RSQRT/DIV/SQRT units may differ from this
emulation by an ulp.

Stdlib only.  Usage from Python:

    from enbaya import EnbayaSource, decode_track
    src = EnbayaSource(stream_bytes)             # or EnbayaSource.from_object(igEnbayaAnimationSource)
    keys = decode_track(src, track_id)            # [{time, translation, rotation, scale}, ...]
    q, t = src.evaluate(time_ns, track_id)        # exactly what igEnbayaTransformSource returns

Command line:  python -I tools/enbaya.py FILE.IGB [anim_index] [track_id]
"""
import math
import os
import struct
import sys

# ----------------------------------------------------------------------------------------------
# PS2 float32 emulation
# ----------------------------------------------------------------------------------------------
FLT_MAX = 3.4028234663852886e38
_MIN_NORMAL = 2.0 ** -126


def f32(x):
    """Truncate a Python float toward zero to float32; flush denormals; saturate overflow."""
    if x == 0.0:
        return 0.0
    if x != x:
        return 0.0
    m, e = math.frexp(x)          # x = m * 2**e, 0.5 <= |m| < 1
    if e > 128:
        return FLT_MAX if x > 0 else -FLT_MAX
    if e < -125:
        return 0.0
    mi = int(m * 16777216.0)      # exact; int() truncates toward zero -> 24-bit mantissa
    return math.ldexp(mi, e - 24)


def _split(x):
    """float32 value -> (integer mantissa, exponent) with x == mant * 2**exp exactly."""
    m, e = math.frexp(x)
    return int(m * 16777216.0), e - 24


def _from_int(n, e):
    """Truncate the exact value n * 2**e toward zero to float32."""
    if n == 0:
        return 0.0
    neg = n < 0
    if neg:
        n = -n
    bl = n.bit_length()
    if bl > 24:
        n >>= (bl - 24)
        e += bl - 24
    v = math.ldexp(n, e)
    if v < _MIN_NORMAL:
        return 0.0
    if v > FLT_MAX:
        v = FLT_MAX
    return -v if neg else v


def fadd(a, b):
    if a == 0.0:
        return b if b != 0.0 else 0.0
    if b == 0.0:
        return a
    ma, ea = _split(a)
    mb, eb = _split(b)
    if ea < eb:
        mb <<= (eb - ea)
        e = ea
    else:
        ma <<= (ea - eb)
        e = eb
    return _from_int(ma + mb, e)


def fsub(a, b):
    return fadd(a, -b)


def fmul(a, b):
    return f32(a * b)             # product of two 24-bit mantissas is exact in a double


def fdiv(a, b):
    if b == 0.0:
        if a == 0.0:
            return 0.0
        return FLT_MAX if (a > 0) == (b >= 0) else -FLT_MAX
    if a == 0.0:
        return 0.0
    ma, ea = _split(a)
    mb, eb = _split(b)
    neg = (ma < 0) != (mb < 0)
    q = (abs(ma) << 64) // abs(mb)
    return _from_int(-q if neg else q, ea - eb - 64)


def fsqrt(a):
    if a <= 0.0:
        return 0.0
    ma, ea = _split(a)
    k = 50 if (ea - 50) % 2 == 0 else 51
    r = math.isqrt(ma << k)
    return _from_int(r, (ea - k) // 2)


def frsqrt(a):
    """VU0 VRSQRT with numerator 1.0 (Q = 1 / sqrt(a))."""
    return fdiv(1.0, fsqrt(a))


def fint(n):
    """cvt.s.w: signed 32-bit integer -> float32 (truncating)."""
    return f32(float(n))


def fmadd(acc, a, b):
    """ACC + a*b with the product rounded first (non-fused, as emulated here)."""
    return fadd(acc, fmul(a, b))


# ----------------------------------------------------------------------------------------------
# Constants read from the ELF
# ----------------------------------------------------------------------------------------------
TIME_SCALE = 0.00026214399258606136   # 0x4e5c28: float(2**18 / 1e9); t = TIME_SCALE * (int)(ns >> 18)
EPS_1E6 = 9.999999974752427e-07       # 0x4e5bf4
EPS_1E5 = 9.999999747378752e-06       # 0x4e5c2c
SLERP_LERP_EPS = 0.07999999821186066  # 0x4e5c10: use linear blend when 1 - dot <= this
SHIFT2 = (6, 4, 2, 0)                 # 0x489ae0 / 0x489af0 / 0x4899c0: 2-bit code shifts, MSB first
SHIFT4 = (4, 0)                       # 0x4899d0: nibble shifts, high nibble first
DELTA2 = (0, 1, 0, -1)                # 0x4c1760: 2-bit delta codes (code 2 = escape to nibble)
DELTA4 = (0, 8, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -9)   # 0x4c1770 (nibble 0 = escape)
# Polynomial slerp weight tables, rows A0..A3 at 0x4851b0 and B0..B3 at 0x4851f0 (FUN_0039fe10)
SLERP_A = ((-1.6776976585388184, 0.9328559041023254, -0.30166375637054443, 0.04650554060935974),
           (2.2625162601470947, -1.1350005865097046, 0.355032354593277, -1.4825481176376343),
           (-0.5848185420036316, 0.20214466750621796, -0.053368620574474335, 0.43604251742362976),
           (0.003932726103812456, -0.004032631870359182, 0.0014658536529168487, 0.9986340403556824))
SLERP_B = ((-1.0928789377212524, 0.7307106852531433, -0.24829478561878204, 1.61046302318573),
           (0.508060097694397, -0.5285651087760925, 0.19492556154727936, -0.1744205504655838),
           (0.5848188400268555, -0.20214563608169556, 0.05336925759911537, -0.4360424876213074),
           (0.003932731691747904, -0.004032634198665619, 0.0014658503932878375, -0.0013659476535394788))

# igEnbayaAnimationSource._playMode (FUN_003da850)
PLAY_LOOP = 0
PLAY_CLAMP = 1
PLAY_PINGPONG = 2
PLAY_LOOP2 = 3
# igEnbayaAnimationSource._interpolationMethod entries [translation, rotation, scale]
INTERP_STEP = 0
INTERP_LERP = 1       # translation: lerp; rotation: normalised lerp
INTERP_NONE = 2       # channel not written
INTERP_SLERP = 3      # rotation: polynomial slerp; translation: not written

HEADER_SIZE = 0x50


class EnbayaError(Exception):
    pass


# ----------------------------------------------------------------------------------------------
# Stream header
# ----------------------------------------------------------------------------------------------
class EnbayaHeader:
    """0x50-byte header at the start of igEnbayaAnimationSource._enbayaAnimationStream.

    +0x00 u32 (overwritten by a vtable pointer at runtime, FUN_003ffe20; stale in the files)
    +0x04 s32 boneCount      +0x08 f32 quantisation step  +0x0c f32 duration (s)  +0x10 s32 fps
    +0x14.. u32 section sizes (see SECTION_ORDER)  +0x48 u32 flags count (= boneCount)
    +0x4c u32 data pointer (runtime: header+0x50; stale in the files)
    """
    SIZE_FIELDS = ('init_codes', 'init_s8', 'init_s16', 'init_s32',
                   'delta_codes', 'delta_nib', 'delta_s8', 'delta_s16', 'delta_s32',
                   'mask_codes', 'mask_u8', 'mask_u16', 'mask_u32')
    # file order of the sections after the header (FUN_003fe450 pointer arithmetic)
    SECTION_ORDER = ('init_s32', 'delta_s32', 'mask_u32', 'init_s16', 'delta_s16', 'mask_u16',
                     'init_codes', 'init_s8', 'delta_codes', 'delta_nib', 'delta_s8',
                     'mask_codes', 'mask_u8')

    def __init__(self, data):
        if len(data) < HEADER_SIZE:
            raise EnbayaError('stream shorter than header')
        w = struct.unpack_from('<Iiffi15I', data, 0)
        self.word0 = w[0]
        self.bone_count = w[1]
        self.scale = w[2]
        self.duration = w[3]
        self.fps = w[4]
        self.sizes = dict(zip(self.SIZE_FIELDS, w[5:18]))
        self.flags_count = w[18]
        self.data_ptr = w[19]
        self.offsets = {}
        pos = HEADER_SIZE
        for name in self.SECTION_ORDER:
            self.offsets[name] = pos
            pos += self.sizes[name]
        self.offsets['flags'] = pos
        self.total_size = pos + self.flags_count     # FUN_003fff30
        if self.bone_count <= 0:
            raise EnbayaError('bad bone count %d' % self.bone_count)
        if self.total_size > len(data):
            raise EnbayaError('stream truncated: need %d bytes, have %d' % (self.total_size, len(data)))
        # ctx+0x24 = 1.0f / (float)fps  (FUN_003fe450)
        self.dt = fdiv(1.0, fint(self.fps))


class _Bone:
    __slots__ = ('q', 'p', 't', 'state', 'mask')

    def __init__(self):
        self.q = [[0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0]]   # two frame buffers (+0x00 / +0x20)
        self.p = [[0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]              # (+0x10 / +0x30), .w = frame time
        self.t = [0.0, 0.0]
        self.state = [0.0] * 7                                   # +0x40: accumulated deltas (ints as f32)
        self.mask = 0                                            # +0x5c: active channel bits


class EnbayaDecoder:
    """Decoding context (the 0xb0 + boneCount*0x60 block managed by igEnbayaContextPool)."""

    def __init__(self, data, header=None):
        self.data = bytes(data)
        self.hdr = header or EnbayaHeader(self.data)
        self.n = self.hdr.bone_count
        self.bones = [_Bone() for _ in range(self.n)]
        self.oob_reads = 0
        self.frame = -1          # ctx+0x00 (FUN_003fe450 initialises to -1)
        self.t_cur = -1.0        # ctx+0x04 time of frame `frame`
        self.t_prev = -1.0       # ctx+0x08 time of frame-1
        self.last_t = -1.0       # ctx+0x20
        self.direction = 0       # ctx+0xa8: 0 reset, 1 forward, 2 backward
        self.cur = 0             # ctx+0xa9: frame buffer holding frame `frame`
        self.run = 0             # ctx+0x28
        self.consumed = 0        # ctx+0x2c
        self._reset_cursors()
        self.initialised = False

    # -- raw reads (the runtime reads straight from memory; out-of-range reads return 0 here) --
    def _rd(self, fmt, off, size):
        if off < 0 or off + size > len(self.data):
            self.oob_reads += 1
            return 0
        return struct.unpack_from(fmt, self.data, off)[0]

    def _s8(self, o): return self._rd('<b', o, 1)
    def _u8(self, o): return self._rd('<B', o, 1)
    def _s16(self, o): return self._rd('<h', o, 2)
    def _u16(self, o): return self._rd('<H', o, 2)
    def _s32(self, o): return self._rd('<i', o, 4)
    def _u32(self, o): return self._rd('<I', o, 4)

    def _reset_cursors(self):
        """FUN_003fe3d0: cursors back to the section starts."""
        o = self.hdr.offsets
        self.ic, self.ic_bit, self.i8, self.i16, self.i32 = o['init_codes'], 0, o['init_s8'], o['init_s16'], o['init_s32']
        self.dc, self.dc_bit, self.dn, self.dn_nib = o['delta_codes'], 0, o['delta_nib'], 0
        self.d8, self.d16, self.d32 = o['delta_s8'], o['delta_s16'], o['delta_s32']
        self.mc, self.mc_bit, self.m8, self.m16, self.m32 = o['mask_codes'], 0, o['mask_u8'], o['mask_u16'], o['mask_u32']

    # -- 2-bit code readers --------------------------------------------------------------------
    def _init_code(self):
        if self.ic_bit == 4:
            self.ic_bit = 0
            self.ic += 1
        c = (self._s8(self.ic) >> SHIFT2[self.ic_bit]) & 3
        self.ic_bit += 1
        return c

    def _mask_code_fwd(self):
        if self.mc_bit == 4:
            self.mc_bit = 0
            self.mc += 1
        c = (self._s8(self.mc) >> SHIFT2[self.mc_bit]) & 3
        self.mc_bit += 1
        return c

    def _mask_code_back(self):
        self.mc_bit -= 1
        if self.mc_bit == -1:
            self.mc_bit = 3
            self.mc -= 1
        return (self._s8(self.mc) >> SHIFT2[self.mc_bit]) & 3

    def _mask_value_fwd(self, c):
        if c == 3:
            v = self._u32(self.m32); self.m32 += 4
        elif c == 2:
            v = self._u16(self.m16); self.m16 += 2
        elif c == 1:
            v = self._u8(self.m8); self.m8 += 1
        else:
            v = 0
        return v

    def _mask_value_back(self, c):
        if c == 3:
            self.m32 -= 4; v = self._u32(self.m32)
        elif c == 2:
            self.m16 -= 2; v = self._u16(self.m16)
        elif c == 1:
            self.m8 -= 1; v = self._u8(self.m8)
        else:
            v = 0
        return v

    # -- FUN_003ff8c0 / FUN_003ff0b0 / FUN_003fea90: reset ------------------------------------
    def reset(self):
        self.frame = 0
        self.t_cur = 0.0
        self.t_prev = 0.0
        self._reset_cursors()
        self.direction = 0
        # FUN_003ff8c0: first run length
        self.run = self._mask_value_fwd(self._mask_code_fwd())
        self.consumed = 0
        # FUN_003ff0b0: 7 signed initial values per bone (quat x,y,z,w, pos x,y,z) as float32
        for b in self.bones:
            for c in range(7):
                code = self._init_code()
                if code == 3:
                    v = self._s32(self.i32); self.i32 += 4
                elif code == 2:
                    v = self._s16(self.i16); self.i16 += 2
                elif code == 1:
                    v = self._s8(self.i8); self.i8 += 1
                else:
                    v = 0
                b.state[c] = fint(v)
        # FUN_003fea90: scale, normalise, zero the accumulators, load the initial channel masks
        sc = self.hdr.scale
        flags = self.hdr.offsets['flags']
        for i, b in enumerate(self.bones):
            q = [fmul(b.state[k], sc) for k in range(4)]
            p = [fmul(b.state[4 + k], sc) for k in range(3)]
            q = self._normalize(q)
            b.q[0] = list(q); b.q[1] = list(q)
            b.p[0] = list(p); b.p[1] = list(p)
            b.t = [0.0, 0.0]
            b.state = [0.0] * 7
            b.mask = self._u8(flags + i)
        self.cur = 0
        self.initialised = True

    @staticmethod
    def _normalize(q):
        # vmul; vaddw.z; vaddz.y; vaddy.x; vrsqrt Q=1/sqrt; vmulq  (FUN_003fe990 / FUN_003fea90)
        x, y, z, w = (fmul(q[0], q[0]), fmul(q[1], q[1]), fmul(q[2], q[2]), fmul(q[3], q[3]))
        dot = fadd(x, fadd(y, fadd(z, w)))
        inv = frsqrt(dot)
        return [fmul(q[0], inv), fmul(q[1], inv), fmul(q[2], inv), fmul(q[3], inv)]

    # -- FUN_003ff990 / FUN_003ffbe0: channel-mask run-length stream --------------------------
    def _mask_forward(self):
        N = self.n * 7
        pos = 0
        while True:
            while pos < N:
                if self.run == 0:
                    break
                take = min(self.run, N - pos)
                pos += take
                self.run -= take
                self.consumed += take
            if pos >= N:
                return
            self.bones[pos // 7].mask ^= 1 << (pos % 7)
            self.run = self._mask_value_fwd(self._mask_code_fwd())
            pos += 1
            self.consumed = 0

    def _mask_backward(self):
        N = self.n * 7
        pos = N - 1
        while True:
            while pos >= 0:
                if self.consumed == 0:
                    break
                take = min(self.consumed, pos + 1)
                pos -= take
                self.run += take
                self.consumed -= take
            if pos < 0:
                return
            self.bones[pos // 7].mask ^= 1 << (pos % 7)
            self.consumed = self._mask_value_back(self._mask_code_back())
            pos -= 1
            self.run = 0

    def _mask_skip_code(self):
        """Forward step after a backward one (FUN_003feb40): step over the current run's code."""
        c = self._mask_code_fwd()
        if c == 3:
            self.m32 += 4
        elif c == 2:
            self.m16 += 2
        elif c == 1:
            self.m8 += 1

    def _mask_unread_code(self):
        """Backward step after a forward one (FUN_003feb40): back up over the current run's code."""
        c = self._mask_code_back()
        if c == 3:
            self.m32 -= 4
        elif c == 2:
            self.m16 -= 2
        elif c == 1:
            self.m8 -= 1

    # -- FUN_003ff1d0 / FUN_003ff3f0: delta stream ---------------------------------------------
    def _delta_fwd(self):
        if self.dc_bit == 4:
            self.dc_bit = 0
            self.dc += 1
        c = (self._s8(self.dc) >> SHIFT2[self.dc_bit]) & 3
        self.dc_bit += 1
        if c != 2:
            return DELTA2[c]
        if self.dn_nib == 2:
            self.dn_nib = 0
            self.dn += 1
        nib = (self._s8(self.dn) >> SHIFT4[self.dn_nib]) & 0xf
        self.dn_nib += 1
        if nib != 0:
            return DELTA4[nib]
        v = self._s8(self.d8); self.d8 += 1
        if v == 0:
            v = self._s16(self.d16); self.d16 += 2
            if v == 0:
                v = self._s32(self.d32); self.d32 += 4
            return v
        if 1 <= v <= 8:
            return v + 0x7f
        if -8 <= v <= -1:
            return v - 0x80
        return v

    def _delta_back(self):
        self.dc_bit -= 1
        if self.dc_bit == -1:
            self.dc_bit = 3
            self.dc -= 1
        c = (self._s8(self.dc) >> SHIFT2[self.dc_bit]) & 3
        if c != 2:
            return DELTA2[c]
        self.dn_nib -= 1
        if self.dn_nib == -1:
            self.dn_nib = 1
            self.dn -= 1
        nib = (self._s8(self.dn) >> SHIFT4[self.dn_nib]) & 0xf
        if nib != 0:
            return DELTA4[nib]
        self.d8 -= 1
        v = self._s8(self.d8)
        if v == 0:
            self.d16 -= 2
            v = self._s16(self.d16)
            if v == 0:
                self.d32 -= 4
                v = self._s32(self.d32)
            return v
        if 1 <= v <= 8:
            return v + 0x7f
        if -8 <= v <= -1:
            return v - 0x80
        return v

    def _deltas_forward(self):
        for b in self.bones:
            m = b.mask
            if m == 0:
                continue
            for c in range(7):
                if m & (1 << c):
                    b.state[c] = fadd(b.state[c], fint(self._delta_fwd()))

    def _deltas_backward(self):
        # FUN_003ff3f0 iterates bones n-1 .. 1 (bgtzl): bone 0 is never reversed (original quirk)
        for i in range(self.n - 1, 0, -1):
            b = self.bones[i]
            m = b.mask
            if m == 0:
                continue
            for c in range(6, -1, -1):
                if m & (1 << c):
                    b.state[c] = fsub(b.state[c], fint(self._delta_back()))

    # -- FUN_003fe990: integrate one frame -----------------------------------------------------
    def _integrate(self, scale, time, forward):
        self.cur ^= 1
        if forward:
            dst, src = self.cur, self.cur ^ 1
        else:
            dst, src = self.cur ^ 1, self.cur
        for b in self.bones:
            sq, sp, st = b.q[src], b.p[src], b.state
            q = [fadd(sq[k], fmul(st[k], scale)) for k in range(4)]
            p = [fadd(sp[k], fmul(st[4 + k], scale)) for k in range(3)]
            b.q[dst] = self._normalize(q)
            b.p[dst] = p
            b.t[dst] = time

    # -- FUN_003feb40: seek -----------------------------------------------------------------------
    def seek(self, t):
        """Move the context so that t_prev <= t <= t_cur (t is a float32 seconds value)."""
        last = self.last_t
        first = (last == -1.0) or (t < EPS_1E6)
        if first:
            reset = True
        elif fsub(last, t) <= t:
            reset = (self.hdr.dt <= last) and (t < self.hdr.dt)
        else:
            reset = True
        if reset:
            self.reset()
        if t >= EPS_1E6:
            dur = self.hdr.duration
            dt = self.hdr.dt
            sc = self.hdr.scale
            while self.t_cur < t and fsub(dur, self.t_cur) > EPS_1E5:
                if self.direction == 2:
                    self._mask_skip_code()
                    self.direction = 1
                elif self.frame > 0:
                    self._mask_forward()
                    self.direction = 1
                self.frame += 1
                self._deltas_forward()
                tf = fmul(fint(self.frame), dt)
                if dur <= tf:
                    tf = dur
                self._integrate(sc, tf, True)
                self.t_cur = fmul(fint(self.frame), dt)
                self.t_prev = fmul(fint(self.frame - 1), dt)
            while t < self.t_prev:
                if self.direction == 1:
                    self._mask_unread_code()
                    self.direction = 2
                else:
                    self._mask_backward()
                self.frame -= 1
                self._deltas_backward()
                tf = fmul(fint(self.frame - 1), dt)
                self._integrate(-sc, tf, False)
                self.t_cur = fmul(fint(self.frame), dt)
                self.t_prev = tf
        self.last_t = t

    # -- FUN_003fefc0: frame pair for a bone at time t ------------------------------------------
    def frames_at(self, t, bone):
        """Return ((q_prev, p_prev, t_prev), (q_cur, p_cur, t_cur)) bracketing float32 time t."""
        if not (self.t_prev <= t <= self.t_cur):
            self.seek(t)
        b = self.bones[bone]
        c, o = self.cur, self.cur ^ 1
        return (b.q[o], b.p[o], b.t[o]), (b.q[c], b.p[c], b.t[c])

    def cursor_report(self):
        """Bytes consumed per section vs. section size (structural check after a full decode)."""
        o, s = self.hdr.offsets, self.hdr.sizes
        rep = {}
        rep['init_codes'] = (self.ic - o['init_codes'] + (1 if self.ic_bit else 0), s['init_codes'])
        rep['init_s8'] = (self.i8 - o['init_s8'], s['init_s8'])
        rep['init_s16'] = (self.i16 - o['init_s16'], s['init_s16'])
        rep['init_s32'] = (self.i32 - o['init_s32'], s['init_s32'])
        rep['delta_codes'] = (self.dc - o['delta_codes'] + (1 if self.dc_bit else 0), s['delta_codes'])
        rep['delta_nib'] = (self.dn - o['delta_nib'] + (1 if self.dn_nib else 0), s['delta_nib'])
        rep['delta_s8'] = (self.d8 - o['delta_s8'], s['delta_s8'])
        rep['delta_s16'] = (self.d16 - o['delta_s16'], s['delta_s16'])
        rep['delta_s32'] = (self.d32 - o['delta_s32'], s['delta_s32'])
        rep['mask_codes'] = (self.mc - o['mask_codes'] + (1 if self.mc_bit else 0), s['mask_codes'])
        rep['mask_u8'] = (self.m8 - o['mask_u8'], s['mask_u8'])
        rep['mask_u16'] = (self.m16 - o['mask_u16'], s['mask_u16'])
        rep['mask_u32'] = (self.m32 - o['mask_u32'], s['mask_u32'])
        return rep

    def step_forward(self):
        """Advance exactly one frame (used by decode_track); returns False at the end."""
        if not self.initialised:
            self.reset()
        dur, dt, sc = self.hdr.duration, self.hdr.dt, self.hdr.scale
        if fsub(dur, self.t_cur) <= EPS_1E5:
            return False
        if self.direction == 2:
            self._mask_skip_code()
            self.direction = 1
        elif self.frame > 0:
            self._mask_forward()
            self.direction = 1
        self.frame += 1
        self._deltas_forward()
        tf = fmul(fint(self.frame), dt)
        if dur <= tf:
            tf = dur
        self._integrate(sc, tf, True)
        self.t_cur = fmul(fint(self.frame), dt)
        self.t_prev = fmul(fint(self.frame - 1), dt)
        return True


# ----------------------------------------------------------------------------------------------
# igEnbayaAnimationSource behaviour (FUN_003da850 / FUN_003da570 / FUN_003da790)
# ----------------------------------------------------------------------------------------------
class _FmodCache:
    def __init__(self):
        self.t = None
        self.d = None
        self.rem = 0.0
        self.n = 0


def time_ns_to_seconds(time_ns):
    """float t = TIME_SCALE * (float)(int32)(time_ns >> 18)."""
    v = (time_ns >> 18) & 0xffffffff
    if v & 0x80000000:
        v -= 1 << 32
    return fmul(TIME_SCALE, fint(v))


def quat_dot(a, b):
    # mula/madda/madd order of FUN_0039fe10 and FUN_003a0000: y, x, z, w
    acc = fmul(a[1], b[1])
    acc = fmadd(acc, a[0], b[0])
    acc = fmadd(acc, a[2], b[2])
    return fmadd(acc, a[3], b[3])


def vec_lerp(t, a, b):
    """FUN_003a02d0: out = (0 + a) + t * (b - a)."""
    return [fmadd(fadd(0.0, a[k]), t, fsub(b[k], a[k])) for k in range(3)]


def quat_nlerp(t, a, b):
    """FUN_003a0000: flip b to the same hemisphere, lerp, normalise with 1/sqrt."""
    if quat_dot(a, b) < 0.0:
        b = [fmul(-1.0, x) for x in b]
    omt = fsub(1.0, t)
    out = [fmadd(fmul(t, b[k]), omt, a[k]) for k in range(4)]
    acc = fmul(out[1], out[1])
    acc = fmadd(acc, out[0], out[0])
    acc = fmadd(acc, out[2], out[2])
    acc = fmadd(acc, out[3], out[3])
    inv = fdiv(1.0, fsqrt(acc))
    return [fmul(x, inv) for x in out]


def _poly4(rows, t):
    # P = rows[0]*t + rows[1]*t^2 + rows[2]*t^3 + rows[3]   (vmulax/vmadday/vmaddaz/vmaddw)
    t2 = fmul(t, t)
    t3 = fmul(t2, t)
    out = []
    for lane in range(4):
        acc = fmul(rows[0][lane], t)
        acc = fmadd(acc, rows[1][lane], t2)
        acc = fmadd(acc, rows[2][lane], t3)
        out.append(fmadd(acc, rows[3][lane], 1.0))
    return out


def quat_slerp(t, a, b):
    """FUN_0039fe10: polynomial slerp (VU0).  a = previous frame, b = current frame."""
    dot = quat_dot(a, b)
    if dot < 0.0:
        b = [fmul(-1.0, x) for x in b]
        dot = -dot
    if fsub(1.0, dot) <= SLERP_LERP_EPS:
        omt = fsub(1.0, t)
        return [fmadd(fmul(t, b[k]), omt, a[k]) for k in range(4)]
    c2 = fmul(dot, dot)
    c3 = fmul(c2, dot)
    pa = _poly4(SLERP_A, t)
    pb = _poly4(SLERP_B, t)
    # weights: ((P.w*1 + P.x*c) + P.y*c^2) + P.z*c^3
    wa = fadd(fadd(fadd(fmul(pa[3], 1.0), fmul(pa[0], dot)), fmul(pa[1], c2)), fmul(pa[2], c3))
    wb = fadd(fadd(fadd(fmul(pb[3], 1.0), fmul(pb[0], dot)), fmul(pb[1], c2)), fmul(pb[2], c3))
    return [fmadd(fmul(a[k], wa), b[k], wb) for k in range(4)]


class EnbayaSource:
    """An igEnbayaAnimationSource: stream + play mode + interpolation methods."""

    def __init__(self, data, play_mode=PLAY_LOOP, interpolation=(1, 3, 1)):
        self.data = bytes(data)
        self.header = EnbayaHeader(self.data)
        self.play_mode = play_mode
        self.interpolation = tuple(interpolation)
        self.decoder = EnbayaDecoder(self.data, self.header)
        self._fmod = _FmodCache()

    @classmethod
    def from_object(cls, obj):
        """Build from an igb.py object: igEnbayaAnimationSource, igEnbayaTransformSource,
        igAnimationTrack or igAnimation (first track).  Returns (source, track_id_or_None)."""
        track_id = None
        o = obj
        if getattr(o, 'cls', None) == 'igAnimation':
            o = o.fields['_trackList'].fields['_data'].refs[0]
        if getattr(o, 'cls', None) == 'igAnimationTrack':
            o = o.fields['_source']
        if getattr(o, 'cls', None) == 'igEnbayaTransformSource':
            track_id = o.fields['_trackId']
            o = o.fields['_enbayaAnimationSource']
        if getattr(o, 'cls', None) != 'igEnbayaAnimationSource':
            raise EnbayaError('unsupported object %r' % (obj,))
        mem = o.fields['_enbayaAnimationStream']
        src = cls(mem.data, o.fields.get('_playMode', PLAY_LOOP),
                  tuple(o.fields.get('_interpolationMethod', (1, 3, 1))))
        return src, track_id

    @property
    def bone_count(self):
        return self.header.bone_count

    @property
    def frame_count(self):
        """Number of stored frames (frame indices 0..frame_count-1), from the seek stop rule."""
        n = 0
        dt, dur = self.header.dt, self.header.duration
        t = 0.0
        while fsub(dur, t) > EPS_1E5:
            n += 1
            t = fmul(fint(n), dt)
            if n > 1 << 20:
                break
        return n + 1

    # FUN_003da790
    def _fmodf(self, t, d):
        c = self._fmod
        if c.t == t and c.d == d:
            return c.n, c.rem
        q = fdiv(t, d)
        if q >= 2147483648.0:
            n = 0x7fffffff           # cvt.w.s overflow
        elif q <= -2147483648.0:
            n = -0x80000000
        else:
            n = int(q)
        rem = fsub(fadd(t, 0.0), fmul(fint(n), d))
        if rem <= 0.0:
            rem = 0.0
        c.t, c.d, c.rem, c.n = t, d, rem, n
        return n, rem

    def local_time(self, time_ns):
        """Play-mode wrapped float32 time for an animation-local time in ns (FUN_003da850)."""
        t = time_ns_to_seconds(time_ns)
        dur = self.header.duration
        pm = self.play_mode
        if pm == PLAY_PINGPONG:
            n, rem = self._fmodf(t, dur)
            return fsub(dur, rem) if (n & 1) else rem
        if pm == PLAY_LOOP or pm == PLAY_LOOP2:
            return self._fmodf(t, dur)[1]
        if pm == PLAY_CLAMP:
            if t <= dur:
                if t < 0.0:
                    return 0.0
                return self._fmodf(t, dur)[1]
            return dur
        return 0.0

    def sample(self, t, track_id, interpolation=None):
        """Sample bone `track_id` at float32 time t (already play-mode wrapped).

        Returns (rotation [x,y,z,w] or None, translation [x,y,z] or None)."""
        interp = self.interpolation if interpolation is None else interpolation
        (qp, pp, tp), (qc, pc, tc) = self.decoder.frames_at(t, track_id)
        frac = fdiv(fsub(t, tp), self.header.dt)
        tm, rm = interp[0], interp[1]
        if tm == INTERP_STEP:
            trans = list(pc)
        elif tm == INTERP_LERP:
            trans = vec_lerp(frac, pp, pc)
        else:
            trans = None
        if rm == INTERP_STEP:
            rot = list(qc)
        elif rm == INTERP_LERP:
            rot = quat_nlerp(frac, qp, qc)
        elif rm == INTERP_SLERP:
            rot = quat_slerp(frac, qp, qc)
        else:
            rot = None
        return rot, trans

    def evaluate(self, time_ns, track_id):
        """igEnbayaTransformSource::getTransform: (rotation, translation) at animation time ns."""
        return self.sample(self.local_time(time_ns), track_id)

    def frame_time(self, frame):
        return fmul(fint(frame), self.header.dt)


# ----------------------------------------------------------------------------------------------
# Convenience API
# ----------------------------------------------------------------------------------------------
def _as_source(source):
    if isinstance(source, EnbayaSource):
        return source, None
    if isinstance(source, (bytes, bytearray, memoryview)):
        return EnbayaSource(bytes(source)), None
    return EnbayaSource.from_object(source)


def decode_all(source):
    """Decode every stored frame.  Returns (header, frames) where frames[k] is a list of
    (rotation[4], translation[3]) per bone at time header.dt*k (last frame clamped to duration)."""
    src, _ = _as_source(source)
    dec = EnbayaDecoder(src.data, src.header)
    dec.reset()
    frames = []
    times = []

    def snapshot():
        c = dec.cur
        frames.append([(list(b.q[c]), list(b.p[c])) for b in dec.bones])
        times.append(dec.bones[0].t[c] if dec.bones else 0.0)
    snapshot()
    while dec.step_forward():
        snapshot()
    return src.header, frames, times, dec.oob_reads


def decode_track(source, track_id=None):
    """Keyframes of one bone: [{'time', 'translation', 'rotation', 'scale'}, ...].

    `source` may be the raw stream bytes, an EnbayaSource, or an igb.py object
    (igEnbayaAnimationSource, igEnbayaTransformSource, igAnimationTrack, igAnimation).
    The keyframes are the stored frames themselves (what the runtime interpolates between);
    frame times are header.dt * k with the last one clamped to the duration.  Scale is always
    (1,1,1): the stream carries only rotation and translation (_drivenChannels == 3)."""
    src, tid = _as_source(source)
    if track_id is None:
        track_id = tid if tid is not None else 0
    if not 0 <= track_id < src.bone_count:
        raise EnbayaError('track %d out of range (%d bones)' % (track_id, src.bone_count))
    _, frames, times, _ = decode_all(src)
    return [{'time': times[k], 'rotation': frames[k][track_id][0], 'translation': frames[k][track_id][1],
             'scale': [1.0, 1.0, 1.0]} for k in range(len(frames))]


def _main(argv):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from igb import IGBFile
    if len(argv) < 2:
        print(__doc__)
        return 1
    f = IGBFile(argv[1])
    anims = f.find('igAnimation')
    ai = int(argv[2]) if len(argv) > 2 else 0
    tid = int(argv[3]) if len(argv) > 3 else 1
    a = anims[ai]
    src, _ = EnbayaSource.from_object(a)
    h = src.header
    print('%s: %d animations; [%d] %s duration %.6f s, %d bones, fps %d, dt %.9f, %d frames, scale %g, interp %s, play %d'
          % (os.path.basename(argv[1]), len(anims), ai, a.fields['_name'], h.duration, h.bone_count, h.fps, h.dt,
             src.frame_count, h.scale, src.interpolation, src.play_mode))
    for k in decode_track(src, tid):
        print('t=%.6f  q=(%+.6f %+.6f %+.6f %+.6f)  p=(%+.4f %+.4f %+.4f)' % ((k['time'],) + tuple(k['rotation']) + tuple(k['translation'])))
    return 0


if __name__ == '__main__':
    sys.exit(_main(sys.argv))
