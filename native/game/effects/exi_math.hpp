#pragma once
// Math helpers used by the EXI particle runtime (work/reports/EFFECTS_NATIVE.md).
//
// Alchemy igMatrix44f is a row-vector matrix: v' = v * M, rows 0..2 = axes, row 3 (m[12..14]) = translation.
// Every helper below is a translation of the named ELF function; argument order is the original one and was
// checked against the disassembly (VU0 macro code for the multiplies).
#include <cstdint>

namespace baroque::game {

struct ExiMat {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// orig FUN_0039d2e0: identity.
void exiIdentity(ExiMat& out);
// orig FUN_0039d860: 64-byte copy (dst, src).
inline void exiCopy(ExiMat& dst, const ExiMat& src) { dst = src; }
// orig FUN_0039dbc0: identity, then m[0]=s.x, m[5]=s.y, m[10]=s.z.
void exiScaleMatrix(ExiMat& out, const float s[3]);
// orig FUN_0039d450(out, a, b): out = a * b (VU0: out.row_i = sum_j a[i][j] * b.row_j). out may alias a or b.
void exiMul(ExiMat& out, const ExiMat& a, const ExiMat& b);
// orig FUN_0039d640(m, b): m = b * m (VU0: loads b as the broadcast operand, m as the row operand).
void exiPreMul(ExiMat& m, const ExiMat& b);
// orig FUN_0039d980: m[12..14] = v.
void exiSetTranslation(ExiMat& m, const float v[3]);
// orig FUN_0039d9a0: v = m[12..14].
void exiGetTranslation(const ExiMat& m, float v[3]);
// orig FUN_0039da90: post-multiply by a translation (each row += row.w * v).
void exiPostTranslate(ExiMat& m, const float v[3]);
// orig FUN_0039ddd0 (+ FUN_0039ded0): axis-angle rotation (radians, newlib sinf/cosf FUN_002386a0/FUN_00238400)
// into the 3x3 part; column 3 = 0 and row 3 = (0,0,0,1) (constants 0x4bc448.. / 0x4bc5b0.. written by the static
// initialisers at 0x4cbc60 / 0x4cbe30).
void exiAxisAngle(float angle, ExiMat& m, const float axis[3]);
// orig FUN_0039de50 (+ FUN_0039dff0): Euler rotation (radians) x, y, z into the 3x3 part, column 3 = 0, row 3 = 0001.
void exiEuler(float x, float y, float z, ExiMat& m);
// orig FUN_0039e160: decompose a rotation matrix into angles (asinf FUN_00238860, atan2f FUN_00238878).
void exiExtractEuler(const ExiMat& m, float* a, float* b, float* c);
// orig FUN_003a01f0(out, a, b): out = a x b.
void exiCross(float out[3], const float a[3], const float b[3]);
// orig FUN_003a03a0(out, v, m): point transform with the homogeneous divide when w != 1 (|w| floored to the
// epsilon DAT_004e5bb8 = 0x350637bd). out may alias v.
void exiTransformPoint(float out[3], const float v[3], const ExiMat& m);
// orig FUN_003a0330(out, v, m): direction transform (3x3). out may alias v.
void exiTransformVector(float out[3], const float v[3], const ExiMat& m);
// orig FUN_0039f1d0(result, out, in): out = inverse(in) by adjugate / determinant (FUN_0039f2d0, FUN_0039f4f0);
// returns false and leaves out untouched when |det| < FLT_MIN (1.1754944e-38).
bool exiInvert(ExiMat& out, const ExiMat& in);

// orig FUN_001ecf50: table sine, index ((int)(a * 162.97466f) & 0x3ff) into 0x4647c0.
float exiSin(float a);
// orig FUN_001ecf90: table cosine (same index into 0x464bc0).
float exiCos(float a);
// orig FUN_001ecfd0: table acos, pi for x <= -1, 0 for x >= 1, else 0x465bd0[((int)((x + 1) * 256)) & 0x1ff].
float exiAcos(float x);
// orig FUN_001ed060: rotation matrix from three angles in radians (table sin/cos), full 16 entries.
void exiEulerTable(ExiMat& out, const float a[3]);
// orig FUN_001ed190: same with degrees (x * 3.1415927 / 180).
void exiEulerTableDeg(ExiMat& out, const float deg[3]);

float exiBitsToFloat(uint32_t u);

}  // namespace baroque::game
