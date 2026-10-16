// EXI runtime math helpers (see exi_math.hpp). UNVERIFIED (all functions): the PS2 FPU rounds toward zero and the
// VU0 / madd.s paths accumulate differently from IEEE round-to-nearest; the port uses plain float arithmetic in the
// original operation order, so results can differ in the last bit from the console.
#include "game/effects/exi_math.hpp"

#include <cmath>
#include <cstring>

namespace baroque::game {

namespace {
#include "game/effects/exi_tables.inc"

float tableValue(const uint32_t* table, int index) { return exiBitsToFloat(table[index]); }
}  // namespace

float exiBitsToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// orig FUN_0039d2e0 (work/decompiled/0039d2e0.c)
void exiIdentity(ExiMat& out) {
    for (int i = 0; i < 16; ++i) out.m[i] = 0.0f;
    out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
}

// orig FUN_0039dbc0 (work/decompiled/0039dbc0.c)
void exiScaleMatrix(ExiMat& out, const float s[3]) {
    exiIdentity(out);
    out.m[0] = s[0];
    out.m[5] = s[1];
    out.m[10] = s[2];
}

// orig FUN_0039d450 (work/decompiled/0039d450.c; disasm 0x39d468: vmulax ACC = b.row0 * a.row_i.x, vmadday b.row1,
// vmaddaz b.row2, vmaddw b.row3)
void exiMul(ExiMat& out, const ExiMat& a, const ExiMat& b) {
    ExiMat r;
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 4; ++c) {
            float acc = b.m[c] * a.m[i * 4 + 0];
            acc += b.m[4 + c] * a.m[i * 4 + 1];
            acc += b.m[8 + c] * a.m[i * 4 + 2];
            acc += b.m[12 + c] * a.m[i * 4 + 3];
            r.m[i * 4 + c] = acc;
        }
    }
    out = r;
}

// orig FUN_0039d640 (work/decompiled/0039d640.c; disasm 0x39d650: vf1..4 = b rows (broadcast), vf5..8 = m rows)
void exiPreMul(ExiMat& m, const ExiMat& b) { exiMul(m, b, m); }

// orig FUN_0039d980 (work/decompiled/0039d980.c)
void exiSetTranslation(ExiMat& m, const float v[3]) {
    m.m[12] = v[0];
    m.m[13] = v[1];
    m.m[14] = v[2];
}

// orig FUN_0039d9a0 (work/decompiled/0039d9a0.c)
void exiGetTranslation(const ExiMat& m, float v[3]) {
    v[0] = m.m[12];
    v[1] = m.m[13];
    v[2] = m.m[14];
}

// orig FUN_0039da90 (work/decompiled/0039da90.c)
void exiPostTranslate(ExiMat& m, const float v[3]) {
    for (int r = 0; r < 4; ++r) {
        const float w = m.m[r * 4 + 3];
        m.m[r * 4 + 0] = w * v[0] + m.m[r * 4 + 0] + 0.0f;
        m.m[r * 4 + 1] = w * v[1] + m.m[r * 4 + 1] + 0.0f;
        m.m[r * 4 + 2] = w * v[2] + m.m[r * 4 + 2] + 0.0f;
    }
}

// orig FUN_0039ddd0 (work/decompiled/0039ddd0.c) -> FUN_0039ded0 (work/decompiled/0039ded0.c)
void exiAxisAngle(float angle, ExiMat& m, const float axis[3]) {
    const float s = std::sin(angle);  // FUN_002386a0 (newlib sinf)
    const float c = std::cos(angle);  // FUN_00238400 (newlib cosf)
    const float t = 1.0f - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    float tx = t * x;
    m.m[0] = tx * x + c + 0.0f;
    m.m[1] = tx * y + s * z + 0.0f;
    m.m[2] = tx * z - s * y;
    const float ty = t * y;
    m.m[4] = ty * x - s * z;
    m.m[5] = ty * y + c + 0.0f;
    m.m[6] = ty * z + s * x + 0.0f;
    tx = t * z;
    m.m[8] = tx * x + s * y + 0.0f;
    m.m[9] = tx * y - s * x;
    m.m[10] = tx * z + c + 0.0f;
    // FUN_0039ddd0 tail: row 3 = DAT_004bc5b0.. = (0,0,0,1), column 3 = DAT_004bc448.. = 0.
    m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
    m.m[3] = 0.0f; m.m[7] = 0.0f; m.m[11] = 0.0f;
}

// orig FUN_0039de50 (work/decompiled/0039de50.c) -> FUN_0039dff0 (work/decompiled/0039dff0.c)
void exiEuler(float x, float y, float z, ExiMat& m) {
    float sx = 0.0f, cx = 1.0f;
    if (x != 0.0f) { sx = std::sin(x); cx = std::cos(x); }
    float sy = 0.0f, cy = 1.0f, sxsy = 0.0f;
    if (y != 0.0f) { sy = std::sin(y); cy = std::cos(y); sxsy = sx * sy; }
    float sz = 0.0f, cz = 1.0f, cxsz = 0.0f;
    if (z != 0.0f) { sz = std::sin(z); cz = std::cos(z); cxsz = cx * sz; }
    m.m[0] = cy * cz;
    m.m[1] = cy * sz;
    m.m[2] = -sy;
    m.m[4] = sxsy * cz - cxsz;
    m.m[5] = sxsy * sz + cx * cz + 0.0f;
    m.m[6] = sx * cy;
    m.m[8] = cx * cz * sy + sx * sz;
    m.m[9] = cxsz * sy - sx * cz;
    m.m[10] = cx * cy;
    m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
    m.m[3] = 0.0f; m.m[7] = 0.0f; m.m[11] = 0.0f;
}

// orig FUN_0039e160 (work/decompiled/0039e160.c). UNVERIFIED: argument registers of the atan2f calls taken from the
// pseudocode (the tail at 0x39e47c loads f12/f13 from the re-orthonormalised rows as shown there).
void exiExtractEuler(const ExiMat& mat, float* a, float* b, float* c) {
    const float* p = mat.m;
    float x0 = p[0], x1 = p[1], x2 = p[2];
    float y0 = p[4], y1 = p[5], y2 = p[6];
    float z0 = p[8], z1 = p[9], z2 = p[10];
    if (p[15] != 1.0f) {
        const float r = 1.0f / p[15];
        y0 *= r; y1 *= r; y2 *= r;
        z0 *= r; z1 *= r; z2 *= r;
        x0 *= r; x1 *= r; x2 *= r;
    }
    float r = 1.0f / std::sqrt(x2 * x2 + x0 * x0 + x1 * x1);
    x1 *= r; x0 *= r; x2 *= r;
    float d = -(x2 * y2 + x0 * y0 + x1 * y1);
    y0 = d * x0 + y0 + 0.0f;
    y1 = d * x1 + y1 + 0.0f;
    y2 = d * x2 + y2 + 0.0f;
    r = 1.0f / std::sqrt(y2 * y2 + y0 * y0 + y1 * y1);
    y0 *= r; y1 *= r; y2 *= r;
    d = -(x2 * z2 + x0 * z0 + x1 * z1);
    float t0 = d * x0 + z0 + 0.0f;
    float t1 = d * x1 + z1 + 0.0f;
    float t2 = d * x2 + z2 + 0.0f;
    const float e = -(y2 * t2 + y0 * t0 + y1 * t1);
    z0 = e * y0 + t0 + 0.0f;
    z1 = e * y1 + t1 + 0.0f;
    z2 = e * y2 + t2 + 0.0f;
    r = 1.0f / std::sqrt(z2 * z2 + z0 * z0 + z1 * z1);
    z0 *= r; z1 *= r; z2 *= r;
    const float yv[3] = {y0, y1, y2}, zv[3] = {z0, z1, z2};
    float cr[3];
    exiCross(cr, yv, zv);
    if (x2 * cr[2] + x0 * cr[0] + x1 * cr[1] < 0.0f) {
        y0 = -y0; y2 = -y2; z0 = -z0; y1 = -y1; z1 = -z1; z2 = -z2;
        x0 = -x0; x1 = -x1; x2 = -x2;
    }
    const float nz = -x2;
    float clamped = nz;
    float v5 = nz;
    if (nz <= -1.0f) v5 = -1.0f;
    clamped = 1.0f;
    if (v5 < 1.0f) {
        clamped = nz;
        if (nz <= -1.0f) clamped = -1.0f;
    }
    const float as = std::asin(clamped);  // FUN_00238860
    *b = as;
    if (std::cos(as) == 0.0f) {           // FUN_00238400
        *a = std::atan2(y0, y1);           // FUN_00238878
        *c = 0.0f;
    } else {
        *a = std::atan2(y2, z2);
        *c = std::atan2(x1, x0);
    }
}

// orig FUN_003a01f0 (work/decompiled/003a01f0.c)
void exiCross(float out[3], const float a[3], const float b[3]) {
    const float b2 = b[2], a1 = a[1], b1 = b[1], a2 = a[2], b0 = b[0], a0 = a[0];
    out[0] = a1 * b2 - a2 * b1;
    out[1] = a2 * b0 - a0 * b2;
    out[2] = a0 * b1 - a1 * b0;
}

// orig FUN_003a03a0 (work/decompiled/003a03a0.c)
void exiTransformPoint(float out[3], const float v[3], const ExiMat& mat) {
    const float* m = mat.m;
    const float y = v[1], x = v[0], z = v[2];
    float o0 = m[12] + z * m[8] + x * m[0] + y * m[4];
    float o1 = m[13] + z * m[9] + x * m[1] + y * m[5];
    float o2 = m[14] + z * m[10] + x * m[2] + y * m[6];
    const float w = m[15] + z * m[11] + x * m[3] + y * m[7];
    if (w != 1.0f) {
        float div = exiBitsToFloat(0x350637bdu);  // DAT_004e5bb8
        if (div <= std::fabs(w)) div = w;
        const float r = 1.0f / div;
        o0 *= r;
        o1 *= r;
        o2 *= r;
    }
    out[0] = o0;
    out[1] = o1;
    out[2] = o2;
}

// orig FUN_003a0330 (work/decompiled/003a0330.c)
void exiTransformVector(float out[3], const float v[3], const ExiMat& mat) {
    const float* m = mat.m;
    const float y = v[1], x = v[0], z = v[2];
    const float o0 = z * m[8] + x * m[0] + y * m[4];
    const float o1 = z * m[9] + x * m[1] + y * m[5];
    const float o2 = z * m[10] + x * m[2] + y * m[6];
    out[0] = o0;
    out[1] = o1;
    out[2] = o2;
}

// orig FUN_0039f1d0 (work/decompiled/0039f1d0.c). UNVERIFIED: the adjugate FUN_0039f2d0 / determinant FUN_0039f4f0
// are implemented as the textbook cofactor expansion (their exact operation order was not traced).
bool exiInvert(ExiMat& out, const ExiMat& in) {
    const float* a = in.m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1.1754944e-38f) return false;
    const float r = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = r * inv[i];
    return true;
}

// orig FUN_001ecf50 (work/decompiled/001ecf50.c; constant 0x4322f983 = 162.97466f, cvt.w.s truncates)
float exiSin(float a) { return tableValue(kExiSinTable, static_cast<int>(a * exiBitsToFloat(0x4322f983u)) & 0x3ff); }

// orig FUN_001ecf90 (work/decompiled/001ecf90.c): table base 0x464bc0 = kExiSinTable + 256.
float exiCos(float a) {
    return tableValue(kExiSinTable + 256, static_cast<int>(a * exiBitsToFloat(0x4322f983u)) & 0x3ff);
}

// orig FUN_001ecfd0 (work/decompiled/001ecfd0.c)
float exiAcos(float x) {
    if (x <= -1.0f) return exiBitsToFloat(0x40490fdbu);
    if (1.0f <= x) return 0.0f;
    return tableValue(kExiAcosTable, static_cast<int>((x + 1.0f) * 256.0f) & 0x1ff);
}

// orig FUN_001ed060 (work/decompiled/001ed060.c)
void exiEulerTable(ExiMat& out, const float a[3]) {
    const float s0 = exiSin(a[0]), s1 = exiSin(a[1]), s2 = exiSin(a[2]);
    const float c0 = exiCos(a[0]), c1 = exiCos(a[1]), c2 = exiCos(a[2]);
    float* m = out.m;
    m[0] = s0 * s1 * s2 + c1 * c2 + 0.0f;
    m[1] = c0 * s2;
    m[2] = s0 * c1 * s2 - s1 * c2;
    m[3] = 0.0f;
    m[4] = s0 * s1 * c2 - c1 * s2;
    m[5] = c0 * c2;
    m[6] = s0 * c1 * c2 + s1 * s2 + 0.0f;
    m[7] = 0.0f;
    m[8] = c0 * s1;
    m[9] = -s0;
    m[10] = c0 * c1;
    m[11] = 0.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
}

// orig FUN_001ed190 (work/decompiled/001ed190.c)
void exiEulerTableDeg(ExiMat& out, const float deg[3]) {
    const float r[3] = {(deg[0] * 3.1415927f) / 180.0f, (deg[1] * 3.1415927f) / 180.0f, (deg[2] * 3.1415927f) / 180.0f};
    exiEulerTable(out, r);
}

}  // namespace baroque::game
