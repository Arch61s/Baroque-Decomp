#pragma once
// igAnimation clips (Enbaya-sourced) and single-clip skeletal pose evaluation, native port of
// tools/anim_export.py pose_at:
//   * igAnimationBinding._boneTrackIdxArray[bone] -> track; track._source._trackId -> stream bone.
//   * translation: the animated one if bit[track] of igAnimation._useAnimationTransBoolArray is set,
//     otherwise igSkeleton._boneTranslationArray[bone] (igAnimationCombiner, FUN_003c6240).
//   * local matrix = FUN_0039fc50(quat) with the translation in row 3 (FUN_003c5550).
//   * world[i] = local[i] x world[parent] for bones 1..n-1; bones whose parent is bone 0 (or none) use
//     local directly (igSkeleton FUN_003f66a0, FUN_0039d3d0 row-vector multiply).
// AnimCombiner below translates the igAnimationCombiner paths the game's actor code uses (FUN_0010a7f0 /
// FUN_0010a8e0). Not translated: igAnimationTransitionDefinition lookups (FUN_003c4670, only reached for blended
// states without an explicit start, which the game never creates), cycle matching (+0x78/+0x7c), the
// cleanCombinerStates option (FUN_003c5220, flag 0x4e61b8 in .bss = off) and _reflectTrack sign flips.
#include "alchemy/enbaya.hpp"
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace baroque::alchemy {

struct AnimTrack {
    std::string name;
    int sourceIndex = -1;   // index into AnimClip::sources, -1 = constant track
    int streamBone = -1;    // igEnbayaTransformSource._trackId
    std::array<float, 4> constantQuaternion{0, 0, 0, 1};
    std::array<float, 3> constantTranslation{0, 0, 0};
};

struct AnimClip {
    std::string name;
    int priority = 0;
    int64_t keyFrameTimeOffsetNs = 0;  // +0x20 _keyFrameTimeOffset
    int64_t startTimeNs = 0;           // +0x28 _startTime
    int64_t durationNs = 0;            // +0x30 _duration
    std::vector<AnimTrack> tracks;
    std::vector<int32_t> boneTrack;          // binding for the skeleton: bone -> track (-1 none)
    std::vector<uint32_t> useAnimTrans;      // igBitMask words
    std::vector<std::unique_ptr<enbaya::Source>> sources;  // distinct igEnbayaAnimationSource objects

    bool useAnimationTranslation(int track) const;
    // orig FUN_0010ae70: _playMode of the first track source (igEnbayaAnimationSource +0xc); 0 when none.
    int firstSourcePlayMode() const;
    int frameCount30() const { return int(durationNs / enbaya::kFrameNs) + 1; }
};

// All igAnimation objects of `file` bound to `skeleton` (the igSkeleton object), in file order.
std::vector<std::unique_ptr<AnimClip>> loadClips(const IgbFile& file, const IgObject* skeleton,
                                                 std::vector<std::string>* log);
// The skeleton object used by SceneModel::build (igAnimationDatabase._skeletonList[0]).
const IgObject* primarySkeleton(const IgbFile& file);

// Pose of `clip` at animation time `timeNs` (clip time; the caller adds startTimeNs like the runtime).
// Outputs bone world matrices in actor space (row-vector convention, same as SceneModel::setPose).
void evaluatePose(AnimClip& clip, int64_t timeNs, const Skeleton& skeleton, std::vector<Mat4>& world);

// Split form of evaluatePose: per-bone local rotation/translation, then the hierarchy composition.
struct LocalPose {
    std::vector<std::array<float, 4>> q;
    std::vector<std::array<float, 3>> t;
};
void evaluateLocals(AnimClip& clip, int64_t timeNs, const Skeleton& skeleton, LocalPose& out);
void composeWorld(const LocalPose& pose, const Skeleton& skeleton, std::vector<Mat4>& world);

// Bones of an igSkeleton (names, parents, _boneTranslationArray) without GPU data: the CPU half of
// SceneModel::loadSkeleton, for headless users of AnimCombiner.
Skeleton loadSkeletonBones(const IgbFile& file, const IgObject* skeleton);

// ---------------------------------------------------------------------------------------------
// igAnimationCombiner (vtable 0x4e15c8) as the game's actor code drives it. Field names and offsets from the ELF
// registration tables (tools/igb_elf_classes.json):
//   igAnimationState (0xa0 B, vtable 0x4e1470)       igAnimationCombiner (0x60 B)
//     +0x08 _animation          +0x38 _timeScale         +0x30 _boneInfoListList (one list per bone, priority order)
//     +0x0c _combineMode        +0x40 _timeBias          +0x34 _boneInfoListBase (per bone: first entry evaluated)
//     +0x10 _transitionMode     +0x48 _blendStartTime    +0x38 _animationStateList
//     +0x14 _status             +0x50 _blendStartRatio   +0x3c _resultQuaternionArray, +0x40 cache matrices
//     +0x1c _new                +0x54 _blendRatioRange   +0x48 _cacheTime, +0x50 _cacheValid
//     +0x20 _currentBlendRatio  +0x58 _blendDuration
//     +0x28 _localTime          +0x60 _animationStartTime
//   igAnimationCombinerBoneInfo: +0x0c state, +0x10 transform source, +0x14 quaternion, +0x24 translation,
//     +0x30 priority, +0x34 _useAnimationTranslation.
// Blending (FUN_003c5550): per bone, the entry at the base index is copied, then every later entry is blended in
// with its state's _currentBlendRatio w: quaternion = FUN_0039fe10 polynomial slerp(w, result, entry),
// translation = w*entry + result*(1-w); rotation matrix FUN_0039fc50 with the translation in row 3.
// _currentBlendRatio (FUN_003c48c0) ramps linearly in the combiner clock: (now - _blendStartTime) / _blendDuration
// (int64 ns converted with __floatdisf), times _blendRatioRange plus _blendStartRatio, clamped to 1.
// ---------------------------------------------------------------------------------------------
struct AnimState {
    int clip = -1;                  // +0x08 _animation (index into the clip list)
    int combineMode = 0;            // +0x0c 0 = base (copied at full weight), 1 = blended in
    int transitionMode = 0;         // +0x10 4 (FUN_0010a7f0), 5 (FUN_0010a8e0: on completion remove the states below)
    int status = 0;                 // +0x14 0 active, 2 waiting for its first combiner update, 3 to be removed
    bool isNew = false;             // +0x1c _new
    float blendRatio = 0.0f;        // +0x20 _currentBlendRatio
    int64_t localTime = 0;          // +0x28 _localTime
    float timeScale = 1.0f;         // +0x38 _timeScale
    int64_t timeBias = 0;           // +0x40 _timeBias
    int64_t blendStartTime = 0;     // +0x48 (1 = "start at the next update", set by FUN_003c6c70)
    float blendStartRatio = 0.0f;   // +0x50
    float blendRatioRange = 0.0f;   // +0x54
    int64_t blendDuration = 0;      // +0x58
    int64_t animationStartTime = 0; // +0x60
    int priority = 0;               // bone info +0x30 (igAnimation._priority, FUN_003c6d60 / FUN_0010a8e0)
};

class AnimCombiner {
public:
    // clips: the actor's igAnimation list (not owned); skeleton: its bones (not owned, may be null = no bones).
    void init(std::vector<std::unique_ptr<AnimClip>>* clips, const Skeleton* skeleton);

    // orig FUN_003c6640 -> FUN_003c6240: append a state for `clip` (combine mode forced to 0 when the combiner is
    // empty) and one bone info per bound bone. Returns null when the clip has no binding (FUN_003c41e0 fails).
    AnimState* add(int clip, int combineMode, int transitionMode, int status, int priority);
    // orig FUN_003c6c70: add, then the LAST state of the list gets the blend parameters (even when add failed).
    AnimState* addWithTransition(float startRatio, float endRatio, int clip, int combineMode, int transitionMode,
                                 int status, int priority, int64_t blendDuration, int64_t animationStartTime);
    void removeAll();                         // orig FUN_003c66f0
    bool remove(const AnimState* state);      // orig FUN_003c6900
    // orig FUN_003c48c0: advance local times, start pending blends, ramp blend ratios, retire replaced states.
    int update(int64_t time);
    // orig FUN_003c5550: per-bone result at `time` (update + blend when the cache is stale).
    const LocalPose& evaluate(int64_t time);
    // orig FUN_003c7a30: _localTime = (int64)((float)(time >> 18) * _timeScale) << 18 + _timeBias.
    static void updateLocalTime(AnimState& s, int64_t time);

    std::vector<std::unique_ptr<AnimState>>& states() { return states_; }
    const std::vector<std::unique_ptr<AnimState>>& states() const { return states_; }
    int64_t cacheTime() const { return cacheTime_; }
    bool cacheValid() const { return cacheValid_; }
    int boneEntryCount(int bone) const { return bone < int(bones_.size()) ? int(bones_[size_t(bone)].size()) : 0; }
    int boneBase(int bone) const { return bone < int(base_.size()) ? base_[size_t(bone)] : 0; }

private:
    struct Entry {                            // igAnimationCombinerBoneInfo
        AnimState* state = nullptr;           // +0x0c
        int priority = 0;                     // +0x30
        std::array<float, 4> q{0, 0, 0, 1};   // +0x14
        std::array<float, 3> t{0, 0, 0};      // +0x24
    };
    void insertEntry(int bone, const Entry& e);     // orig FUN_003c5e90
    void recomputeBases();                          // orig FUN_003c6050
    void retireBelow(AnimState* state);             // orig FUN_003c47e0
    void sample(Entry& e, int bone, std::vector<std::pair<const AnimState*, LocalPose>>& cache);  // FUN_003c53c0

    std::vector<std::unique_ptr<AnimClip>>* clips_ = nullptr;
    const Skeleton* skeleton_ = nullptr;
    std::vector<std::vector<Entry>> bones_;          // +0x30
    std::vector<int> base_;                          // +0x34
    std::vector<std::unique_ptr<AnimState>> states_; // +0x38
    LocalPose result_;                               // +0x3c / +0x40
    int64_t cacheTime_ = 0;                          // +0x48
    bool cacheValid_ = false;                        // +0x50
};

// orig FUN_002467f0 (__floatdisf, soft-float: round to nearest even, independent of the PS2 float scope).
float floatdisf(int64_t v);
// orig FUN_0039fe10: VU0 polynomial slerp from a to b by t (shared with the Enbaya sampler).
std::array<float, 4> quatSlerpPoly(float t, const std::array<float, 4>& a, std::array<float, 4> b);

// igEnbayaAnimationSource._interpolationMethod (igUnsignedCharArrayMetaField[3]); default {1,3,1}.
std::array<int, 3> interpolationMethod(const IgObject* enbayaSource);
// FUN_0039fc50 (rotation) + translation row, PS2 float order.
Mat4 quatToMatrix(const std::array<float, 4>& q, const std::array<float, 3>& t);
// FUN_0039d3d0 row-vector multiply (a then b), PS2 float order.
Mat4 mulPs2(const Mat4& a, const Mat4& b);

}  // namespace baroque::alchemy
