// Game actor animation wrapper (see actor_anim.hpp).
#include "game/actor_anim.hpp"

#include "alchemy/ps2_float.hpp"

#include <cstring>

#pragma fenv_access(on)

namespace baroque::game {

namespace {
int64_t gClock = 0;  // lRam004e78e0
int gTickId = 0;     // FUN_0010a7a0
uint64_t gSetCount = 0;   // PORT: hard-set counter behind ActorAnim::lastSetTick (presentation marker only)
// (float)(uint32)(clock >> 18): the dsra/cvt.s.w/unsigned fix-up sequence of FUN_0010a7f0 / FUN_0010a8e0 / FUN_0010ad90
float clockUnits() { return float(uint32_t(uint64_t(gClock) >> 0x12)); }
int64_t quantised(float units, float speed) { return int64_t(units * speed); }   // FUN_00246b90 (__fixsfdi)
}  // namespace

void ActorAnim::advanceClock(int64_t ns) { gClock += ns; }
int64_t ActorAnim::clock() { return gClock; }
void ActorAnim::setTickId(int id) { gTickId = id; }

void ActorAnim::init(std::vector<std::unique_ptr<alchemy::AnimClip>>* clips, const alchemy::Skeleton* skeleton) {
    clips_ = clips;
    skeleton_ = skeleton;
    comb_.init(clips, skeleton);
    clip_ = -1;
    cur_ = nullptr;
    speed_ = 1.0f;
    scale_[0] = scale_[1] = 1.0f;
    changeId_ = -1;
}

int ActorAnim::findClip(const char* name4) const {
    if (clips_ == nullptr) return -1;
    for (size_t i = 0; i < clips_->size(); ++i)
        if (std::strncmp((*clips_)[i]->name.c_str(), name4, 4) == 0 && (*clips_)[i]->name.size() == std::strlen(name4)) return int(i);
    return -1;
}

// orig FUN_0010a7f0 (work/decompiled/0010a7f0.c + disasm): replace every state by `clip` (combine mode 0,
// transition mode 4, status 0, priority = igAnimation._priority); time restarts at 0, rate 1.0.
void ActorAnim::set(int clip) {
    alchemy::ps2::Ps2FloatScope fp;
    const int64_t now = gClock;
    changeId_ = clip;   // +0x58 = FUN_0010a7a0(actor, anim): index of the clip in the actor's list (-1 = none)
    lastSetTick_ = ++gSetCount;                                           // PORT: presentation marker (hard cut)
    comb_.removeAll();                                                    // FUN_003c66f0
    clip_ = clip;
    const int prio = clips_ != nullptr && clip >= 0 && size_t(clip) < clips_->size() ? (*clips_)[size_t(clip)]->priority : 0;
    comb_.add(clip, 0, 4, 0, prio);                                       // FUN_003c6d60
    cur_ = comb_.states().empty() ? nullptr : comb_.states().front().get();   // **(+0x44)+0x10 [0]
    if (cur_ == nullptr) return;   // (the original dereferences an empty list here)
    cur_->timeBias = quantised(clockUnits(), cur_->timeScale) * -0x40000;
    setSpeed(1.0f);                                                       // FUN_0010ad90(1.0)
    alchemy::AnimCombiner::updateLocalTime(*cur_, now);                   // FUN_003c7a30
}

// orig FUN_0010a8e0 (work/decompiled/0010a8e0.c + disasm 0x10a8e0..0x10aaec)
void ActorAnim::change(float blendSec, float speed, int clip, int startFrame) {
    alchemy::ps2::Ps2FloatScope fp;
    const int64_t now = gClock;
    // +0x58 is FUN_0010a7a0(actor, anim) = the clip's index in the actor's animation list (disasm 0x10a91c..0x10a92c,
    // 0x10a7a0), not a tick id: a change to the clip of the previous set/change only restarts the newest state's
    // time (rate 1.0)
    if (clip == changeId_) {
        if (cur_ == nullptr) return;
        cur_->timeBias = quantised(clockUnits(), cur_->timeScale) * -0x40000;
        setSpeed(1.0f);
        alchemy::AnimCombiner::updateLocalTime(*cur_, now);
        return;
    }
    changeId_ = clip;
    if (3 < comb_.states().size()) comb_.remove(comb_.states().front().get());   // FUN_003c6900(oldest)
    clip_ = clip;
    const int prio = clips_ != nullptr && clip >= 0 && size_t(clip) < clips_->size() ? (*clips_)[size_t(clip)]->priority : 0;
    const int64_t blendNs = int64_t(alchemy::ps2::fmul(1e+09f, blendSec));  // FUN_00246b90(1e9f * blend)
    const int64_t startNs = int64_t(int32_t(uint32_t(startFrame) * 0x1fca055u));   // mult (32-bit) + sign extension
    // FUN_003c6c70(0.0, 1.0, combiner, anim, combine 1, transition 5, status 2, &priority, 0, blendNs; start)
    comb_.addWithTransition(0.0f, 1.0f, clip, 1, 5, 2, prio, blendNs, startNs);
    cur_ = comb_.states().empty() ? nullptr : comb_.states().back().get();   // last state of +0x38
    if (cur_ == nullptr) return;
    cur_->timeBias = quantised(clockUnits(), cur_->timeScale) * -0x40000;
    setSpeed(speed);
    alchemy::AnimCombiner::updateLocalTime(*cur_, now);
}

// orig FUN_0010ad90: keep the current time continuous across a rate change.
void ActorAnim::setSpeed(float speed) {
    alchemy::ps2::Ps2FloatScope fp;
    speed_ = speed;
    if (cur_ == nullptr) return;
    const float rate = speed * scale_[0] * scale_[1];
    const float c = clockUnits();
    cur_->timeBias = quantised(c, cur_->timeScale) * 0x40000 + cur_->timeBias;
    cur_->timeBias = cur_->timeBias + quantised(c, rate) * -0x40000;
    cur_->timeScale = rate;
}

// orig FUN_0010ae50
void ActorAnim::setScale(float v, int index) {
    scale_[index & 1] = v;
    setSpeed(speed_);
}

// orig FUN_0010aaf0 (work/decompiled/0010aaf0.c): the current state's _localTime folded into the clip.
// FUN_003fb270 = __divdi3 (truncating) quotient; the quotient is used zero-extended from 32 bits.
int64_t ActorAnim::currentTimeNs() const {
    if (clips_ == nullptr || cur_ == nullptr || cur_->clip < 0) return 0;
    const alchemy::AnimClip& c = *(*clips_)[size_t(cur_->clip)];
    const int64_t t = cur_->localTime;
    const int64_t dur = c.durationNs;
    uint32_t q = 0;   // UNVERIFIED: s1 is not initialised in the original when the duration is 0
    if (dur != 0) q = uint32_t(int32_t(((t - c.keyFrameTimeOffsetNs) - c.startTimeNs) / dur));
    // FUN_0010ae70: a clamped clip (first track source _playMode == 1) stays on its last frame past the end
    if (q != 0 && clip_ >= 0 && size_t(clip_) < clips_->size() && (*clips_)[size_t(clip_)]->firstSourcePlayMode() == 1)
        return dur;
    return t - int64_t(uint64_t(q) * uint64_t(dur));
}

// orig FUN_00108d20 (work/decompiled/00108d20.c), combiner part for one actor.
void ActorAnim::update() {
    if (gClock == comb_.cacheTime()) return;                         // `lVar3 != *(long *)(combiner + 0x48)`
    comb_.update(gClock);                                            // FUN_003c48c0
    for (auto& s : comb_.states()) s->isNew = false;
}

void ActorAnim::pose(std::vector<alchemy::Mat4>& world) {
    if (skeleton_ == nullptr || clips_ == nullptr) return;
    const alchemy::LocalPose& p = comb_.evaluate(gClock);              // FUN_003c5550
    lastLocal_ = &p;                                                   // PORT: presentation read-back (combiner +0x3c)
    poseOwner_ = this;
    alchemy::composeWorld(p, *skeleton_, world);
}

// orig FUN_0010a4d0 animation tail (see actor_anim.hpp)
void actorLoadDefaultAnim(ActorAnim& a, const std::vector<std::unique_ptr<alchemy::AnimClip>>& clips,
                          const alchemy::IgbFile& f) {
    const alchemy::IgObject* info = nullptr;   // FUN_00325420(info list, file, "igActorInfo", 0)
    for (const alchemy::IgObject* o : f.listObjects(f.infoList()))
        if (o != nullptr && o->isA("igActorInfo")) {
            info = o;
            break;
        }
    if (info == nullptr) return;   // UNVERIFIED: the original reads through a null info here (never in the corpus)
    const alchemy::IgObject* db = f.object(info->getRef("_animationDatabase"));   // wrapper +0x38
    if (db == nullptr) return;
    const auto anims = f.listObjects(f.object(db->getRef("_animationList")));    // +0x18 -> +0x10 [0]
    if (anims.empty() || anims[0] == nullptr) return;
    const std::string_view name = anims[0]->getString("_name");
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i] != nullptr && clips[i]->name == name) {
            a.set(static_cast<int>(i));                                  // FUN_0010a7f0(wrapper, animation)
            return;
        }
}

// orig FUN_00146f90 (rate arithmetic; see actor_anim.hpp)
float attackRateScale(const AttackRateInputs& in) {
    alchemy::ps2::Ps2FloatScope fp;
    using namespace alchemy::ps2;
    float f = in.weaponSd13 ? 1.1f : 1.0f;
    for (int i = 0; i < in.wm16Hits; ++i) f = fadd(f, 0.05f);
    if (in.st19Brand) f = fadd(f, 0.1f);
    const float h = fdiv(fint(in.hidden0), 255.0f);
    return fadd(fadd(0.0f, f), fmul(0.2f, h));   // adda 0, f; madd 0.2 * (h / 255)
}

}  // namespace baroque::game
