// igAnimation clip loading and pose evaluation (see animation.hpp).
#include "alchemy/animation.hpp"

#include "alchemy/ps2_float.hpp"

#include <bit>
#include <cstring>

#pragma fenv_access(on)

namespace baroque::alchemy {

using namespace ps2;

bool AnimClip::useAnimationTranslation(int track) const {
    if (useAnimTrans.empty() || track < 0) return false;
    const size_t w = size_t(track) >> 5;
    return w < useAnimTrans.size() && ((useAnimTrans[w] >> (track & 31)) & 1u) == 1u;
}

std::array<int, 3> interpolationMethod(const IgObject* es) {
    std::array<int, 3> interp{1, 3, 1};
    if (const Value* v = es->find("_interpolationMethod"); v != nullptr && v->raw.size() >= 3)
        for (size_t i = 0; i < 3; ++i) interp[i] = v->raw[i];
    return interp;
}

const IgObject* primarySkeleton(const IgbFile& file) {
    for (const IgObject* db : file.find("igAnimationDatabase")) {
        const auto skels = file.listObjects(file.object(db->getRef("_skeletonList")));
        if (!skels.empty()) return skels[0];
    }
    const auto all = file.find("igSkeleton");
    return all.empty() ? nullptr : all[0];
}

namespace {

template <typename T>
std::vector<T> memArray(const IgMemory* m) {
    std::vector<T> out;
    if (m == nullptr) return out;
    out.resize(m->size / sizeof(T));
    if (!out.empty()) std::memcpy(out.data(), m->data.data(), out.size() * sizeof(T));
    return out;
}

// tools/anim_export.py binding_for: the binding whose _hierarchy is the skeleton (or null), else the first.
const IgObject* bindingFor(const IgbFile& f, const IgObject* anim, const IgObject* skeleton) {
    const auto binds = f.listObjects(f.object(anim->getRef("_bindingList")));
    for (const IgObject* b : binds) {
        const int h = b->getRef("_hierarchy");
        if (h < 0 || (skeleton != nullptr && h == skeleton->slot)) return b;
    }
    return binds.empty() ? nullptr : binds[0];
}

}  // namespace

std::vector<std::unique_ptr<AnimClip>> loadClips(const IgbFile& f, const IgObject* skeleton, std::vector<std::string>* log) {
    std::vector<std::unique_ptr<AnimClip>> clips;
    for (const IgObject* a : f.find("igAnimation")) {
        auto clip = std::make_unique<AnimClip>();
        clip->name = std::string(a->getString("_name"));
        clip->priority = a->getInt("_priority");
        clip->keyFrameTimeOffsetNs = a->getLong("_keyFrameTimeOffset");
        clip->startTimeNs = a->getLong("_startTime");
        clip->durationNs = a->getLong("_duration");
        if (const IgObject* bm = f.object(a->getRef("_useAnimationTransBoolArray")))
            clip->useAnimTrans = memArray<uint32_t>(f.memory(bm->getRef("_data")));
        std::map<int, int> sourceBySlot;
        bool ok = true;
        for (const IgObject* t : f.listObjects(f.object(a->getRef("_trackList")))) {
            AnimTrack tr;
            tr.name = std::string(t->getString("_name"));
            t->getFloats("_constantQuaternion", tr.constantQuaternion.data(), 4);
            t->getFloats("_constantTranslation", tr.constantTranslation.data(), 3);
            const IgObject* ts = f.object(t->getRef("_source"));
            if (ts != nullptr && ts->isA("igEnbayaTransformSource")) {
                tr.streamBone = ts->getInt("_trackId");
                const IgObject* es = f.object(ts->getRef("_enbayaAnimationSource"));
                if (es == nullptr) {
                    ok = false;
                } else {
                    auto it = sourceBySlot.find(es->slot);
                    if (it == sourceBySlot.end()) {
                        const IgMemory* stream = f.memory(es->getRef("_enbayaAnimationStream"));
                        std::array<int, 3> interp{1, 3, 1};
                        interp = interpolationMethod(es);
                        std::string err;
                        auto src = stream != nullptr
                                       ? enbaya::Source::create(stream->data, es->getInt("_playMode", 0), interp, &err)
                                       : std::nullopt;
                        if (!src) {
                            if (log) log->push_back(clip->name + ": enbaya source rejected: " + err);
                            ok = false;
                        } else {
                            clip->sources.push_back(std::make_unique<enbaya::Source>(std::move(*src)));
                            it = sourceBySlot.emplace(es->slot, int(clip->sources.size()) - 1).first;
                        }
                    }
                    if (it != sourceBySlot.end()) tr.sourceIndex = it->second;
                }
            } else if (ts != nullptr) {
                if (log) log->push_back(clip->name + ": unsupported track source " + std::string(ts->cls()));
                ok = false;
            }
            clip->tracks.push_back(std::move(tr));
        }
        if (const IgObject* b = bindingFor(f, a, skeleton)) clip->boneTrack = memArray<int32_t>(f.memory(b->getRef("_boneTrackIdxArray")));
        if (ok) clips.push_back(std::move(clip));
    }
    return clips;
}

Mat4 quatToMatrix(const std::array<float, 4>& q, const std::array<float, 3>& t) {
    Ps2FloatScope fp;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float y2 = fmul(y, 2.0f);
    const float xw2 = fmul(fmul(x, 2.0f), w);
    const float xx2 = fmul(fmul(x, 2.0f), x);
    const float z2 = fmul(z, 2.0f);
    const float zz2 = fmul(z2, z);
    Mat4 r;
    auto& m = r.m;
    m.fill(0.0f);
    m[0] = fsub(1.0f, fadd(fmul(y2, y), zz2));
    m[1] = fsub(fmul(y2, x), fmul(z2, w));
    m[2] = fadd(fmul(z2, x), fmul(y2, w));
    m[4] = fadd(fmul(y2, x), fmul(z2, w));
    m[5] = fsub(1.0f, fadd(xx2, zz2));
    m[6] = fsub(fmul(z2, y), xw2);
    m[8] = fsub(fmul(z2, x), fmul(y2, w));
    m[9] = fadd(fmul(z2, y), xw2);
    m[10] = fsub(1.0f, fadd(xx2, fmul(y2, y)));
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    m[15] = 1.0f;
    return r;
}

Mat4 mulPs2(const Mat4& a, const Mat4& b) {
    Ps2FloatScope fp;
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        const float ax = a.m[size_t(4 * r)], ay = a.m[size_t(4 * r + 1)], az = a.m[size_t(4 * r + 2)], aw = a.m[size_t(4 * r + 3)];
        for (int c = 0; c < 4; ++c) {
            float acc = fmul(b.m[size_t(c)], ax);
            acc = fmadd(acc, b.m[size_t(4 + c)], ay);
            acc = fmadd(acc, b.m[size_t(8 + c)], az);
            out.m[size_t(4 * r + c)] = fmadd(acc, b.m[size_t(12 + c)], aw);
        }
    }
    return out;
}

void evaluateLocals(AnimClip& clip, int64_t timeNs, const Skeleton& skeleton, LocalPose& out) {
    const size_t n = skeleton.bones.size();
    out.q.assign(n, {0.0f, 0.0f, 0.0f, 1.0f});
    out.t.assign(n, {0.0f, 0.0f, 0.0f});
    for (size_t i = 0; i < n; ++i) {
        const Bone& b = skeleton.bones[i];
        std::array<float, 4> q{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> t = b.translation;
        const int ti = i < clip.boneTrack.size() ? clip.boneTrack[i] : -1;
        if (ti >= 0 && size_t(ti) < clip.tracks.size()) {
            const AnimTrack& tr = clip.tracks[size_t(ti)];
            if (tr.sourceIndex < 0) {
                q = tr.constantQuaternion;
                if (clip.useAnimationTranslation(ti)) t = tr.constantTranslation;
            } else {
                enbaya::Quat rq;
                enbaya::Vec3 rt;
                bool hasRot = false, hasTrans = false;
                clip.sources[size_t(tr.sourceIndex)]->evaluate(timeNs, tr.streamBone, rq, hasRot, rt, hasTrans);
                if (hasRot) q = {rq.x, rq.y, rq.z, rq.w};
                if (clip.useAnimationTranslation(ti) && hasTrans) t = {rt.x, rt.y, rt.z};
            }
        }
        out.q[i] = q;
        out.t[i] = t;
    }
}

void composeWorld(const LocalPose& pose, const Skeleton& skeleton, std::vector<Mat4>& world) {
    const size_t n = skeleton.bones.size();
    world.assign(n, Mat4::identity());
    if (n == 0) return;
    std::vector<Mat4> locals(n);
    for (size_t i = 0; i < n; ++i) locals[i] = quatToMatrix(pose.q[i], pose.t[i]);
    world[0] = locals[0];
    for (size_t i = 1; i < n; ++i) {
        const int p = skeleton.bones[i].parent;
        world[i] = (p <= 0) ? locals[i] : mulPs2(locals[i], world[size_t(p)]);
    }
}

void evaluatePose(AnimClip& clip, int64_t timeNs, const Skeleton& skeleton, std::vector<Mat4>& world) {
    LocalPose pose;
    evaluateLocals(clip, timeNs, skeleton, pose);
    composeWorld(pose, skeleton, world);
}


int AnimClip::firstSourcePlayMode() const {
    for (const AnimTrack& tr : tracks)
        if (tr.sourceIndex >= 0 && size_t(tr.sourceIndex) < sources.size()) return sources[size_t(tr.sourceIndex)]->playMode();
    return 0;
}

Skeleton loadSkeletonBones(const IgbFile& f, const IgObject* sk) {
    Skeleton out;
    if (sk == nullptr) return out;
    out.name = std::string(sk->getString("_name"));
    const auto infos = f.listObjects(f.object(sk->getRef("_boneInfoList")));
    const IgMemory* tr = f.memory(sk->getRef("_boneTranslationArray"));
    for (size_t i = 0; i < infos.size(); ++i) {
        Bone b;
        b.name = std::string(infos[i]->getString("_name"));
        b.parent = infos[i]->getInt("_parentIdx", -1);
        b.bmIdx = infos[i]->getInt("_bmIdx", -1);
        b.flags = infos[i]->getInt("_flags", 0);
        if (tr != nullptr && tr->size >= (i + 1) * 12)
            for (size_t k = 0; k < 3; ++k) std::memcpy(&b.translation[k], &tr->data[i * 12 + k * 4], 4);
        out.bones.push_back(std::move(b));
    }
    out.valid = !out.bones.empty();
    return out;
}

// ---------------------------------------------------------------------------------------------
// FUN_0039fe10 (also in enbaya.cpp, where the Enbaya sampler uses it; same tables 0x4851b0 / 0x4851f0)
// ---------------------------------------------------------------------------------------------
namespace {

constexpr uint32_t kSlerpA[4][4] = {{0xbfd6becc, 0x3f6ecfa5, 0xbe9a73ac, 0x3d3e7c98},
                                    {0x4010cd11, 0xbf9147b3, 0x3eb5c6cd, 0xbfbdc423},
                                    {0xbf15b6ab, 0x3e4eff03, 0xbd5a990e, 0x3edf40f7},
                                    {0x3b80de19, 0xbb84242b, 0x3ac021e3, 0x3f7fa67b}};
constexpr uint32_t kSlerpB[4][4] = {{0xbf8be375, 0x3f3b0fdb, 0xbe7e40fd, 0x3fce23a7},
                                    {0x3f02103a, 0xbf07500b, 0x3e479a91, 0xbe329b4d},
                                    {0x3f15b6b0, 0xbe4eff44, 0x3d5a99b9, 0xbedf40f6},
                                    {0x3b80de25, 0xbb842430, 0x3ac021c7, 0xbab30999}};
const float kSlerpLerpEps = std::bit_cast<float>(0x3da3d70au);   // 0x4e5c10

std::array<float, 4> poly4(const uint32_t rows[4][4], float t) {
    const float t2 = fmul(t, t);
    const float t3 = fmul(t2, t);
    std::array<float, 4> out{};
    for (size_t lane = 0; lane < 4; ++lane) {
        float acc = fmul(std::bit_cast<float>(rows[0][lane]), t);
        acc = fmadd(acc, std::bit_cast<float>(rows[1][lane]), t2);
        acc = fmadd(acc, std::bit_cast<float>(rows[2][lane]), t3);
        out[lane] = fmadd(acc, std::bit_cast<float>(rows[3][lane]), 1.0f);
    }
    return out;
}

// FUN_003c7a30's float/int64 step: FUN_00246b90 (__fixsfdi) of (float)(uint32)(time >> 18) * scale.
int64_t scaledClock(int64_t time, float scale) {
    const float units = float(uint32_t(uint64_t(time) >> 0x12));
    return int64_t(fmul(units, scale));
}

}  // namespace

// orig FUN_002467f0: libgcc __floatdisf through soft-float doubles (hi * 2^32 + lo, then __truncdfsf2), i.e. round to
// nearest even; the sticky-bit fix-up for |v| >= 2^53 avoids double rounding, which an exact int64 -> double -> float
// conversion in nearest mode reproduces for the magnitudes used here. Runs with MXCSR RC = nearest.
float floatdisf(int64_t v) {
    const unsigned saved = _mm_getcsr();
    _mm_setcsr(saved & ~0x6000u);
    const float r = float(v);
    _mm_setcsr(saved);
    return r;
}

std::array<float, 4> quatSlerpPoly(float t, const std::array<float, 4>& a, std::array<float, 4> b) {
    Ps2FloatScope fp;
    float dot = fmul(a[1], b[1]);
    dot = fmadd(dot, a[0], b[0]);
    dot = fmadd(dot, a[2], b[2]);
    dot = fmadd(dot, a[3], b[3]);
    if (dot < 0.0f) {
        for (float& x : b) x = fmul(-1.0f, x);
        dot = -dot;
    }
    std::array<float, 4> out{};
    if (fsub(1.0f, dot) <= kSlerpLerpEps) {
        const float omt = fsub(1.0f, t);
        for (size_t k = 0; k < 4; ++k) out[k] = fmadd(fmul(t, b[k]), omt, a[k]);
        return out;
    }
    const float c2 = fmul(dot, dot);
    const float c3 = fmul(c2, dot);
    const auto pa = poly4(kSlerpA, t);
    const auto pb = poly4(kSlerpB, t);
    const float wa = fadd(fadd(fadd(fmul(pa[3], 1.0f), fmul(pa[0], dot)), fmul(pa[1], c2)), fmul(pa[2], c3));
    const float wb = fadd(fadd(fadd(fmul(pb[3], 1.0f), fmul(pb[0], dot)), fmul(pb[1], c2)), fmul(pb[2], c3));
    for (size_t k = 0; k < 4; ++k) out[k] = fmadd(fmul(a[k], wa), b[k], wb);
    return out;
}

// ---------------------------------------------------------------------------------------------
// igAnimationCombiner
// ---------------------------------------------------------------------------------------------
void AnimCombiner::init(std::vector<std::unique_ptr<AnimClip>>* clips, const Skeleton* skeleton) {
    clips_ = clips;
    skeleton_ = skeleton;
    states_.clear();
    const size_t n = skeleton != nullptr ? skeleton->bones.size() : 0;
    bones_.assign(n, {});
    base_.assign(n, 0);
    // FUN_003c59b0 sizes the result arrays; bones that never receive an entry keep this content.
    // UNVERIFIED: initial contents (identity rotation, skeleton translation assumed).
    result_.q.assign(n, {0.0f, 0.0f, 0.0f, 1.0f});
    result_.t.assign(n, {0.0f, 0.0f, 0.0f});
    for (size_t i = 0; i < n; ++i) result_.t[i] = skeleton->bones[i].translation;
    cacheTime_ = 0;
    cacheValid_ = false;
}

// orig FUN_003c7a30
void AnimCombiner::updateLocalTime(AnimState& s, int64_t time) {
    s.localTime = scaledClock(time, s.timeScale) * 0x40000 + s.timeBias;
}

// orig FUN_003c5e90: insert by priority (after every entry of lower or equal priority) and keep the base index.
void AnimCombiner::insertEntry(int bone, const Entry& e) {
    std::vector<Entry>& list = bones_[size_t(bone)];
    int& base = base_[size_t(bone)];
    const int n = int(list.size());
    cacheValid_ = false;
    if (n == 0) {
        list.push_back(e);
        base = 0;
        return;
    }
    int k = 0;
    while (list[size_t(k)].priority <= e.priority) {
        ++k;
        if (k == n) break;
    }
    if (k == n) {
        list.push_back(e);
    } else {
        list.insert(list.begin() + k, e);
        if (k <= base) base = base + 1;
    }
    if (e.state->combineMode == 0 && base < k) base = k;
}

// orig FUN_003c6050: base = topmost entry (index >= 1) whose state is in combine mode 0, else 0.
void AnimCombiner::recomputeBases() {
    for (size_t b = 0; b < bones_.size(); ++b) {
        const std::vector<Entry>& list = bones_[b];
        int j = int(list.size());
        int found = 0;
        while (true) {
            --j;
            if (j < 1) break;
            if (list[size_t(j)].state->combineMode == 0) {
                found = j;
                break;
            }
        }
        base_[b] = found;
    }
    cacheValid_ = false;
}

// orig FUN_003c6640 (FUN_003c41e0 binding lookup) -> FUN_003c6240
AnimState* AnimCombiner::add(int clip, int combineMode, int transitionMode, int status, int priority) {
    if (clips_ == nullptr || clip < 0 || size_t(clip) >= clips_->size()) return nullptr;   // no binding
    AnimClip& c = *(*clips_)[size_t(clip)];
    cacheValid_ = false;
    if (states_.empty()) combineMode = 0;
    auto st = std::make_unique<AnimState>();
    st->clip = clip;
    st->combineMode = combineMode;
    st->transitionMode = transitionMode;
    st->status = status;
    st->isNew = true;
    st->blendStartTime = 0;
    st->priority = priority;
    AnimState* s = st.get();
    states_.push_back(std::move(st));
    for (size_t b = 0; b < bones_.size(); ++b) {
        const int ti = b < c.boneTrack.size() ? c.boneTrack[b] : -1;
        if (ti < 0 || size_t(ti) >= c.tracks.size()) continue;   // _boneTrackIdxArray -1: no bone info
        Entry e;
        e.state = s;
        e.priority = priority;
        const AnimTrack& tr = c.tracks[size_t(ti)];
        const bool useTrans = c.useAnimationTranslation(ti);
        if (tr.sourceIndex < 0) {                                  // constant track (source null)
            e.q = tr.constantQuaternion;
            e.t = useTrans ? tr.constantTranslation : skeleton_->bones[b].translation;
        } else if (!useTrans) {
            e.t = skeleton_->bones[b].translation;
        }
        insertEntry(int(b), e);
    }
    return s;
}

// orig FUN_003c6c70
AnimState* AnimCombiner::addWithTransition(float startRatio, float endRatio, int clip, int combineMode, int transitionMode,
                                           int status, int priority, int64_t blendDuration, int64_t animationStartTime) {
    Ps2FloatScope fp;
    AnimState* added = add(clip, combineMode, transitionMode, status, priority);
    if (states_.empty()) return added;   // the original writes through a null last state here
    AnimState& last = *states_.back();
    last.animationStartTime = animationStartTime;
    last.blendStartRatio = startRatio;
    last.blendRatioRange = fsub(endRatio, startRatio);
    last.blendDuration = blendDuration;
    last.blendStartTime = 1;
    return added;
}

// orig FUN_003c66f0
void AnimCombiner::removeAll() {
    if (states_.empty()) return;
    cacheValid_ = false;
    states_.clear();
    for (auto& l : bones_) l.clear();
}

// orig FUN_003c6900
bool AnimCombiner::remove(const AnimState* state) {
    size_t idx = states_.size();
    for (size_t i = 0; i < states_.size(); ++i)
        if (states_[i].get() == state) {
            idx = i;
            break;
        }
    if (idx == states_.size()) return false;
    cacheValid_ = false;   // (+0x7c cycle-match bookkeeping FUN_003c6140: never set by the game)
    for (size_t b = 0; b < bones_.size(); ++b) {
        std::vector<Entry>& list = bones_[b];
        for (size_t j = 0; j < list.size(); ++j) {
            if (list[j].state != state) continue;
            list.erase(list.begin() + std::ptrdiff_t(j));
            if (!list.empty()) {
                int found = 0;
                for (int k = int(list.size()) - 1; k >= 0; --k)
                    if (list[size_t(k)].state->combineMode == 0) {
                        found = k;
                        break;
                    }
                base_[b] = found;
            }
            break;
        }
    }
    states_.erase(states_.begin() + std::ptrdiff_t(idx));
    if (states_.size() == 1) states_[0]->combineMode = 0;
    return true;
}

// orig FUN_003c47e0: every state below `state` in a bone list is marked for removal; `state` becomes a base state.
void AnimCombiner::retireBelow(AnimState* state) {
    for (auto& list : bones_) {
        bool found = false;
        for (int j = int(list.size()) - 1; j >= 0; --j) {
            AnimState* s = list[size_t(j)].state;
            if (s == state) found = true;
            else if (found) s->status = 3;
        }
    }
    state->combineMode = 0;   // (+0x18 _baseState = null)
    recomputeBases();
}

// orig FUN_003c48c0 (work/decompiled/003c48c0.c)
int AnimCombiner::update(int64_t time) {
    Ps2FloatScope fp;
    const int count = int(states_.size());
    if (count == 0) return 0;
    for (auto& s : states_)
        if (s->status == 0 || s->isNew) updateLocalTime(*s, time);
    for (int i = 0; i < count && size_t(i) < states_.size(); ++i) {
        AnimState& s = *states_[size_t(i)];
        const int mode = s.combineMode, tmode = s.transitionMode;
        if (mode == 0 || s.status == 3) {
            if (s.status == 2) s.status = 0;
            continue;
        }
        if (unsigned(mode - 1) < 2u && unsigned(tmode - 3) < 2u) {
            if (s.status == 2) s.status = 0;
            continue;
        }
        if (s.isNew) {
            if (s.blendStartTime == 0) {
                // FUN_003c4670 igAnimationTransitionDefinition lookup: not translated (the game gives every blended
                // state an explicit start through FUN_003c6c70). UNVERIFIED: modelled as the lookup-failure branch
                // (ratio 0..1 over duration / 3 from now).
                const AnimClip& c = *(*clips_)[size_t(s.clip)];
                s.animationStartTime = 0;
                s.blendStartRatio = 0.0f;
                s.blendRatioRange = 1.0f;
                s.blendDuration = c.durationNs / 3;   // FUN_00244f38(duration, 3)
                s.timeBias = scaledClock(time, s.timeScale) * -0x40000;
                updateLocalTime(s, time);
                s.blendStartTime = time;
                s.status = 0;
                continue;
            }
            s.timeBias = s.animationStartTime + scaledClock(time, s.timeScale) * -0x40000;
            updateLocalTime(s, time);
            s.blendStartTime = time;
            s.status = 0;
            // combine mode 2 (FUN_003c4670 base-state lookup): not produced by the game
        }
        if (s.status != 0) continue;   // status-2 waits only come from the FUN_003c4670 path
        float w;
        const int64_t start = s.blendStartTime;
        const int64_t dur = s.blendDuration;
        if (time < start || start + dur < time) {
            w = 1.0f;
        } else {
            float r;
            if (dur == 0) {
                r = 1.0f;
            } else {
                r = fdiv(floatdisf(time - start), floatdisf(dur));
                if (1.0f < r) r = 1.0f;
            }
            w = fadd(fadd(fmul(r, s.blendRatioRange), s.blendStartRatio), 0.0f);
        }
        if (tmode == 2) {
            w = fsub(1.0f, w);
            if (0.0f < w) {
                s.blendRatio = w;
                continue;
            }
            s.status = 3;
        } else if (1.0f <= w) {
            if (tmode == 0) {
                s.combineMode = 0;
                recomputeBases();
            } else if (tmode == 1 || tmode == 5) {
                retireBelow(&s);   // (transition mode 1 with a _baseState: not produced by the game)
            }
        }
        s.blendRatio = w;
    }
    // (cycle-match loop over +0x7c / +0x78: never set by the game)
    for (size_t i = 0; i < states_.size();) {
        if (states_[i]->status == 3) remove(states_[i].get());
        else ++i;
    }
    return count;
}

// orig FUN_003c53c0: the bone info's transform source sampled at its state's _localTime.
void AnimCombiner::sample(Entry& e, int bone, std::vector<std::pair<const AnimState*, LocalPose>>& cache) {
    const AnimState* s = e.state;
    LocalPose* pose = nullptr;
    for (auto& c : cache)
        if (c.first == s) pose = &c.second;
    if (pose == nullptr) {
        cache.emplace_back(s, LocalPose{});
        pose = &cache.back().second;
        evaluateLocals(*(*clips_)[size_t(s->clip)], s->localTime, *skeleton_, *pose);
    }
    e.q = pose->q[size_t(bone)];
    e.t = pose->t[size_t(bone)];
}

// orig FUN_003c5550 (work/decompiled/003c5550.c; blend arithmetic from disasm 0x3c5874..0x3c5928)
const LocalPose& AnimCombiner::evaluate(int64_t time) {
    if (cacheValid_ && time == cacheTime_) return result_;
    if (update(time) == 0) return result_;
    Ps2FloatScope fp;
    std::vector<std::pair<const AnimState*, LocalPose>> cache;
    cache.reserve(states_.size());
    if (states_.size() == 1) {
        AnimState* s = states_[0].get();
        if (!s->isNew && s->status != 0) {
            s->isNew = false;
        } else {
            for (size_t b = 0; b < bones_.size(); ++b) {
                if (bones_[b].empty()) continue;
                Entry& e = bones_[b][0];
                sample(e, int(b), cache);
                result_.q[b] = e.q;
                result_.t[b] = e.t;
            }
            s->isNew = false;
        }
    } else {
        for (size_t b = 0; b < bones_.size(); ++b) {
            std::vector<Entry>& list = bones_[b];
            if (list.empty()) continue;
            int k = base_[b];
            Entry& e0 = list[size_t(k)];
            AnimState* s0 = e0.state;
            const int st0 = s0->status;
            s0->isNew = false;
            if (st0 == 0) sample(e0, int(b), cache);
            std::array<float, 4> q = e0.q;
            std::array<float, 3> t = e0.t;
            for (++k; k < int(list.size()); ++k) {
                Entry& e = list[size_t(k)];
                AnimState* s = e.state;
                const int st = s->status;
                if (st == 0 || (s->isNew && st == 1)) sample(e, int(b), cache);
                if (st == 2) {
                    s->isNew = false;
                } else {
                    const float w = s->blendRatio;
                    q = quatSlerpPoly(w, q, e.q);   // FUN_0039fe10(w, result, result, entry)
                    const float omw = fsub(1.0f, w);
                    for (size_t c = 0; c < 3; ++c) t[c] = fmadd(fmul(w, e.t[c]), t[c], omw);   // mula w,e; madd r,1-w
                    s->isNew = false;
                }
            }
            result_.q[b] = q;   // FUN_0039fc50 builds the rotation from it; row 3 keeps t
            result_.t[b] = t;
        }
    }
    cacheTime_ = time;
    cacheValid_ = true;
    return result_;
}

}  // namespace baroque::alchemy
