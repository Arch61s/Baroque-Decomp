#pragma once
// Game actor animation wrapper (object **(chr+0x2c)) over Alchemy's igAnimationCombiner (alchemy::AnimCombiner):
//   FUN_0010a7f0 set (no blend), FUN_0010a8e0 change with blend, FUN_0010ad90 playback rate, FUN_0010ae50 rate
//   scale, FUN_0010aaf0 current clip time, FUN_00108d20 per-frame combiner update (3-D pass).
// Wrapper layout: +0x34 combiner, +0x3c current igAnimation, +0x40 current igAnimationState, +0x44 state list,
// +0x4c speed, +0x50 / +0x54 rate scales, +0x58 change id (FUN_0010a7a0).
// Time base: the engine clock lRam004e78e0 (ns), advanced by the 3-D pass FUN_001075a0 (+0x1fca055 per frame) right
// before FUN_00108d20 updates the combiners. UNVERIFIED: the combiner time source (*0x4e7968)->+0x68 is taken to be
// that clock (FUN_001075a0 hands it the clock through vtable +0x6c before FUN_00108d20).
#include "alchemy/animation.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace baroque::game {

class ActorAnim {
public:
    // clips: the character's igAnimation list (igAnimationDatabase order). Not owned. skeleton may be null
    // (no bones: states, times and blend ratios still run, the pose is empty).
    void init(std::vector<std::unique_ptr<alchemy::AnimClip>>* clips, const alchemy::Skeleton* skeleton);
    int findClip(const char* name4) const;   // AnmSet/AnmChgEx lookup by name (FUN_00146e60 strcmp)

    void set(int clip);                                             // orig FUN_0010a7f0
    // orig FUN_0010a8e0(blendSec, speed, actor, anim, startFrame): blends `clip` in over blendSec, starting its
    // local time at startFrame * 0x1fca055 ns at the next combiner update.
    void change(float blendSec, float speed, int clip, int startFrame);
    void setSpeed(float speed);                                     // orig FUN_0010ad90
    void setScale(float v, int index);                              // orig FUN_0010ae50 (+0x50 / +0x54)
    int64_t currentTimeNs() const;                                  // orig FUN_0010aaf0
    int currentClip() const { return clip_; }                       // +0x3c
    int64_t durationNs() const {                                    // orig FUN_0010abf0 (igAnimation +0x30)
        return clips_ != nullptr && clip_ >= 0 && static_cast<size_t>(clip_) < clips_->size()
                   ? (*clips_)[static_cast<size_t>(clip_)]->durationNs : 0;
    }
    float speed() const { return speed_; }                          // +0x4c

    // Engine clock lRam004e78e0 (ns), advanced by the game tick; the change id FUN_0010a7a0 is the tick number.
    static void advanceClock(int64_t ns);
    static int64_t clock();
    static void setTickId(int id);

    // orig FUN_00108d20 (per actor): when the clock differs from the combiner cache time, FUN_003c48c0 at the clock,
    // then every state's _new flag is cleared. Call once per frame after the clock advance.
    void update();
    // Pose for rendering: FUN_003c5550 at the clock, then the skeleton composition (FUN_003f66a0).
    void pose(std::vector<alchemy::Mat4>& world);

    const alchemy::AnimCombiner& combiner() const { return comb_; }
    const alchemy::AnimState* currentState() const { return cur_; }
    // PORT (presentation, read-only; PRESENTATION_PLAN.md 2.2): the bone-local result of the last pose() (null when
    // the actor has no skeleton / clips, i.e. pose() never evaluates), the skeleton, and lastSetTick: a PORT value
    // that changes at every hard set (FUN_0010a7f0), so two snapshots with different values saw a clip restart.
    const alchemy::LocalPose* localPose() const {
        return skeleton_ != nullptr && clips_ != nullptr && poseOwner_ == this ? lastLocal_ : nullptr;
    }
    const alchemy::Skeleton* skeleton() const { return skeleton_; }
    uint64_t lastSetTick() const { return lastSetTick_; }

private:
    std::vector<std::unique_ptr<alchemy::AnimClip>>* clips_ = nullptr;
    const alchemy::Skeleton* skeleton_ = nullptr;
    alchemy::AnimCombiner comb_;                 // +0x34
    int clip_ = -1;                              // +0x3c
    alchemy::AnimState* cur_ = nullptr;          // +0x40
    float speed_ = 1.0f;                         // +0x4c
    float scale_[2] = {1.0f, 1.0f};              // +0x50, +0x54
    int changeId_ = -1;                          // +0x58
    uint64_t lastSetTick_ = 0;                   // PORT: presentation marker written by set(), never read by logic
    const alchemy::LocalPose* lastLocal_ = nullptr;   // PORT: the combiner result (+0x3c) the last pose() composed
    const ActorAnim* poseOwner_ = nullptr;            // PORT: `this` at that pose() (a moved/copied wrapper re-poses)
};

// orig FUN_0010a4d0 (work/decompiled/0010a4d0.c, vt+0x10 of the actor wrapper vtable 0x4cc950 = the load callback
// FUN_00109c80 of FUN_001099e0), animation tail: wrapper +0x38 = the file's igActorInfo (FUN_00325420(info list, ..,
// "igActorInfo" 0x42c930, 0)) ._animationDatabase, then FUN_0010a7f0(wrapper, _animationDatabase._animationList[0]):
// every actor wrapper (FUN_0012dd10 -> FUN_0010a370: character body, player / NPC heads, E19 shell) starts on the
// first animation of its file as soon as it is loaded. `clips` is the list `a` was initialised with (loadClips of the
// same file); the animation is matched by name. No-op when the file has no igActorInfo / animation.
void actorLoadDefaultAnim(ActorAnim& a, const std::vector<std::unique_ptr<alchemy::AnimClip>>& clips,
                          const alchemy::IgbFile& f);

// orig FUN_00146f90 (work/decompiled/00146f90.c + disasm 0x147340..0x147380): playback-rate scale written to scale
// slot 1 (FUN_0010ae50(rate, actor, 1)) after AnmSet/AnmChg/AnmChgEx when the action name starts with "AT"
// (0x44b770). Inputs are the equipment/status facts the function reads (item ids from 0x44b778.. "SD13", "WM16",
// "ST19"); FUN_001d4c50(item, id) = item id == id or its +0x38 sub-item id == id.
struct AttackRateInputs {
    bool weaponSd13 = false;        // *0x53d9b4 != 0 && weapon id (+0x1c) == "SD13"  -> base 1.1
    int wm16Hits = 0;               // number of the six FUN_001d4c50(.., "WM16") tests that passed (+0.05 each, in order)
    bool st19Brand = false;         // status block +0x3c & 0x10 && block +0x54 == "ST19" -> +0.1
    int32_t hidden0 = 0;            // *(status block +0x58) (hidden stat 0)
};
float attackRateScale(const AttackRateInputs& in);

}  // namespace baroque::game
