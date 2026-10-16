#pragma once
// Enbaya compressed skeletal animation (Intrinsic Alchemy), native port of tools/enbaya.py.
//
// Runtime functions translated (all in SLUS_217.14):
//   FUN_003fe450 context init, FUN_003fe3d0 cursor reset, FUN_003feb40 seek, FUN_003ff8c0/FUN_003ff0b0 initial
//   pose, FUN_003fea90 pose setup, FUN_003ff990/FUN_003ffbe0 channel-mask run lengths, FUN_003ff1d0/FUN_003ff3f0
//   delta decode, FUN_003fe990 integrate (VU0), FUN_003fefc0 frame lookup, FUN_003da850/FUN_003da570/FUN_003da790
//   igEnbayaAnimationSource sampling, FUN_003a02d0 lerp, FUN_003a0000 nlerp, FUN_0039fe10 polynomial slerp.
// All float math runs in PS2 float32 semantics (ps2_float.hpp); the public entry points open the scope.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace baroque::alchemy::enbaya {

// igEnbayaAnimationSource._playMode (FUN_003da850)
enum PlayMode : int { kPlayLoop = 0, kPlayClamp = 1, kPlayPingPong = 2, kPlayLoop2 = 3 };
// igEnbayaAnimationSource._interpolationMethod entries [translation, rotation, scale]
enum Interp : int { kInterpStep = 0, kInterpLerp = 1, kInterpNone = 2, kInterpSlerp = 3 };

constexpr uint32_t kHeaderSize = 0x50;
constexpr int64_t kFrameNs = 0x1fca055;  // 33,333,333 ns = one 30 Hz frame

// 0x50-byte header at the start of igEnbayaAnimationStream.
struct Header {
    enum Section {
        InitCodes, InitS8, InitS16, InitS32, DeltaCodes, DeltaNib, DeltaS8, DeltaS16, DeltaS32,
        MaskCodes, MaskU8, MaskU16, MaskU32, SectionCount
    };
    uint32_t word0 = 0;        // +0x00 vtable at runtime (FUN_003ffe20), stale in files
    int32_t boneCount = 0;     // +0x04
    float scale = 0.0f;        // +0x08 quantisation step
    float duration = 0.0f;     // +0x0c seconds
    int32_t fps = 0;           // +0x10
    std::array<uint32_t, SectionCount> sizes{};    // +0x14.. in Section order
    uint32_t flagsCount = 0;   // +0x48 (= boneCount)
    uint32_t dataPtr = 0;      // +0x4c runtime pointer, stale
    std::array<uint32_t, SectionCount> offsets{};  // stream offsets (FUN_003fe450 pointer arithmetic)
    uint32_t flagsOffset = 0;
    uint32_t totalSize = 0;    // FUN_003fff30
    float dt = 0.0f;           // ctx+0x24 = 1.0f / (float)fps

    static std::optional<Header> parse(const std::vector<uint8_t>& data, std::string* error);
};

struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Vec3 { float x = 0, y = 0, z = 0; };

// Decoding context (the 0xb0 + boneCount*0x60 block managed by igEnbayaContextPool).
class Decoder {
public:
    struct Bone {
        std::array<std::array<float, 4>, 2> q{};  // +0x00 / +0x20 two frame buffers
        std::array<std::array<float, 3>, 2> p{};  // +0x10 / +0x30
        std::array<float, 2> t{};                 // .w of p = frame time
        std::array<float, 7> state{};             // +0x40 accumulated deltas (ints as f32)
        uint32_t mask = 0;                        // +0x5c active channel bits
    };
    struct FramePair {
        const std::array<float, 4>* qPrev; const std::array<float, 3>* pPrev; float tPrev;
        const std::array<float, 4>* qCur;  const std::array<float, 3>* pCur;  float tCur;
    };

    Decoder(const std::vector<uint8_t>* data, const Header* header);
    // These require an active ps2::Ps2FloatScope.
    void reset();                      // FUN_003ff8c0 / FUN_003ff0b0 / FUN_003fea90
    void seek(float t);                // FUN_003feb40
    bool stepForward();                // one stored frame forward (decode_track helper)
    FramePair framesAt(float t, int bone);  // FUN_003fefc0

    const std::vector<Bone>& bones() const { return bones_; }
    int cur() const { return cur_; }
    int frame() const { return frame_; }
    bool initialised() const { return initialised_; }
    uint32_t outOfRangeReads() const { return oob_; }
    // Bytes consumed per section after a full decode: {consumed, size}.
    std::array<std::array<uint32_t, 2>, Header::SectionCount> cursorReport() const;

private:
    int8_t s8(int64_t o);
    uint8_t u8(int64_t o);
    int16_t s16(int64_t o);
    uint16_t u16(int64_t o);
    int32_t s32(int64_t o);
    uint32_t u32(int64_t o);
    void resetCursors();               // FUN_003fe3d0
    int initCode();
    int maskCodeFwd();
    int maskCodeBack();
    uint32_t maskValueFwd(int c);
    uint32_t maskValueBack(int c);
    static void normalize(std::array<float, 4>& q);
    void maskForward();                // FUN_003ff990
    void maskBackward();               // FUN_003ffbe0
    void maskSkipCode();
    void maskUnreadCode();
    int32_t deltaFwd();                // FUN_003ff1d0 element
    int32_t deltaBack();               // FUN_003ff3f0 element
    void deltasForward();
    void deltasBackward();
    void integrate(float scale, float time, bool forward);  // FUN_003fe990

    const std::vector<uint8_t>* data_;
    const Header* hdr_;
    int n_;
    std::vector<Bone> bones_;
    uint32_t oob_ = 0;
    int32_t frame_ = -1;     // ctx+0x00
    float tCur_ = -1.0f;     // ctx+0x04
    float tPrev_ = -1.0f;    // ctx+0x08
    float lastT_ = -1.0f;    // ctx+0x20
    int direction_ = 0;      // ctx+0xa8: 0 reset, 1 forward, 2 backward
    int cur_ = 0;            // ctx+0xa9
    uint32_t run_ = 0;       // ctx+0x28
    uint32_t consumed_ = 0;  // ctx+0x2c
    bool initialised_ = false;
    int64_t ic_ = 0, i8_ = 0, i16_ = 0, i32_ = 0; int icBit_ = 0;
    int64_t dc_ = 0, dn_ = 0, d8_ = 0, d16_ = 0, d32_ = 0; int dcBit_ = 0, dnNib_ = 0;
    int64_t mc_ = 0, m8_ = 0, m16_ = 0, m32_ = 0; int mcBit_ = 0;
};

// An igEnbayaAnimationSource: stream + play mode + interpolation methods + its decoding context.
class Source {
public:
    static std::optional<Source> create(std::vector<uint8_t> stream, int playMode, std::array<int, 3> interpolation,
                                        std::string* error);
    Source(Source&& o) noexcept;
    Source& operator=(Source&&) = delete;
    Source(const Source&) = delete;

    const Header& header() const { return header_; }
    int playMode() const { return playMode_; }
    const std::array<int, 3>& interpolation() const { return interp_; }
    int frameCount() const;                          // stored frames, from the seek stop rule
    float localTime(int64_t timeNs);                 // FUN_003da850 play-mode wrapped seconds
    // FUN_003da570: sample bone `track` at wrapped time t. Missing channels (INTERP_NONE) leave has* false.
    void sample(float t, int track, Quat& rot, bool& hasRot, Vec3& trans, bool& hasTrans);
    // igEnbayaTransformSource::getTransform at animation time (ns).
    void evaluate(int64_t timeNs, int track, Quat& rot, bool& hasRot, Vec3& trans, bool& hasTrans);
    Decoder& decoder() { return decoder_; }

private:
    Source(std::vector<uint8_t> stream, const Header& h, int playMode, std::array<int, 3> interp);
    struct FmodCache { bool valid = false; float t = 0, d = 0, rem = 0; int32_t n = 0; };
    void fmodf(float t, float d, int32_t& n, float& rem);  // FUN_003da790

    std::vector<uint8_t> data_;
    Header header_;
    int playMode_;
    std::array<int, 3> interp_;
    Decoder decoder_;
    FmodCache fmod_;
};

// float t = TIME_SCALE * (float)(int32)(ns >> 18)
float timeNsToSeconds(int64_t timeNs);

}  // namespace baroque::alchemy::enbaya
