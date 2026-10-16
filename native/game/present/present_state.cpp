// PORT presentation snapshots and interpolation (see present_state.hpp, PRESENTATION_PLAN.md 2.2 / 2.3).
#include "game/present/present_state.hpp"

#include "game/actor_anim.hpp"
#include "game/character/character.hpp"
#include "game/character/npc_class.hpp"
#include "game/objects/objects.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_set>

namespace baroque::game::present {

namespace {

Presenter gPresenter;

using Quat = std::array<float, 4>;   // x, y, z, w

constexpr float kCutDistance = 300.0f;        // per-tick character move treated as a teleport (2.3)
constexpr float kCamCutDistance = 1000.0f;    // per-tick eye move treated as a camera cut (2.3)
constexpr float kCamCutCos = 0.5f;            // forward turned by more than 60 degrees in one tick

float len3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

Quat nlerpShortest(const Quat& a, Quat b, float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0f) {
        for (float& x : b) x = -x;
        d = -d;
    }
    Quat r;
    if (d > 0.9995f) {
        for (size_t i = 0; i < 4; ++i) r[i] = a[i] + (b[i] - a[i]) * t;
    } else {
        const float th = std::acos(std::min(d, 1.0f));
        const float s = std::sin(th);
        const float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
        for (size_t i = 0; i < 4; ++i) r[i] = a[i] * wa + b[i] * wb;
    }
    const float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    if (l > 0.0f)
        for (float& x : r) x /= l;
    return r;
}

// rows (row-vector convention: rows = images of the x / y / z axes) <-> quaternion; R = rows^T is the column form
bool rowsToQuat(const float r0[3], const float r1[3], const float r2[3], Quat* q) {
    float R[3][3];
    for (int i = 0; i < 3; ++i) {
        R[i][0] = r0[i];
        R[i][1] = r1[i];
        R[i][2] = r2[i];
    }
    const float det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                      R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    if (!(det > 0.5f)) return false;   // mirrored or degenerate: no rotation
    const float tr = R[0][0] + R[1][1] + R[2][2];
    Quat r;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        r = {(R[2][1] - R[1][2]) / s, (R[0][2] - R[2][0]) / s, (R[1][0] - R[0][1]) / s, 0.25f * s};
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        const float s = std::sqrt(1.0f + R[0][0] - R[1][1] - R[2][2]) * 2.0f;
        r = {0.25f * s, (R[0][1] + R[1][0]) / s, (R[0][2] + R[2][0]) / s, (R[2][1] - R[1][2]) / s};
    } else if (R[1][1] > R[2][2]) {
        const float s = std::sqrt(1.0f + R[1][1] - R[0][0] - R[2][2]) * 2.0f;
        r = {(R[0][1] + R[1][0]) / s, 0.25f * s, (R[1][2] + R[2][1]) / s, (R[0][2] - R[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + R[2][2] - R[0][0] - R[1][1]) * 2.0f;
        r = {(R[0][2] + R[2][0]) / s, (R[1][2] + R[2][1]) / s, 0.25f * s, (R[1][0] - R[0][1]) / s};
    }
    *q = r;
    return true;
}

void quatToRows(const Quat& q, float r0[3], float r1[3], float r2[3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float R[3][3] = {{1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
                           {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
                           {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)}};
    for (int i = 0; i < 3; ++i) {
        r0[i] = R[i][0];
        r1[i] = R[i][1];
        r2[i] = R[i][2];
    }
}

// Gram-Schmidt on three rows (keeps row 2's direction, then row 1)
void orthonormal(float r0[3], float r1[3], float r2[3]) {
    float l = len3(r2);
    if (l > 0.0f)
        for (int k = 0; k < 3; ++k) r2[k] /= l;
    float d = r1[0] * r2[0] + r1[1] * r2[1] + r1[2] * r2[2];
    for (int k = 0; k < 3; ++k) r1[k] -= r2[k] * d;
    l = len3(r1);
    if (l > 0.0f)
        for (int k = 0; k < 3; ++k) r1[k] /= l;
    const float c[3] = {r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]};
    if (c[0] * r0[0] + c[1] * r0[1] + c[2] * r0[2] < 0.0f) {
        for (int k = 0; k < 3; ++k) r0[k] = -c[k];
    } else {
        for (int k = 0; k < 3; ++k) r0[k] = c[k];
    }
}

// rows of a camera / actor transform at t between a and b (rotation slerped, `scaled` rows keep their lengths)
bool lerpRows(const float a[3][3], const float b[3][3], float t, float out[3][3]) {
    float sa[3], sb[3], ra[3][3], rb[3][3];
    for (int i = 0; i < 3; ++i) {
        sa[i] = len3(a[i]);
        sb[i] = len3(b[i]);
        if (!(sa[i] > 1e-6f) || !(sb[i] > 1e-6f)) return false;
        for (int k = 0; k < 3; ++k) {
            ra[i][k] = a[i][k] / sa[i];
            rb[i][k] = b[i][k] / sb[i];
        }
    }
    orthonormal(ra[0], ra[1], ra[2]);
    orthonormal(rb[0], rb[1], rb[2]);
    Quat qa, qb;
    if (!rowsToQuat(ra[0], ra[1], ra[2], &qa) || !rowsToQuat(rb[0], rb[1], rb[2], &qb)) return false;
    const Quat q = nlerpShortest(qa, qb, t);
    float r[3][3];
    quatToRows(q, r[0], r[1], r[2]);
    for (int i = 0; i < 3; ++i) {
        const float s = sa[i] + (sb[i] - sa[i]) * t;
        for (int k = 0; k < 3; ++k) out[i][k] = r[i][k] * s;
    }
    return true;
}

void lerpLocal(const alchemy::LocalPose& a, const alchemy::LocalPose& b, float t, alchemy::LocalPose& out) {
    out.q.resize(b.q.size());
    out.t.resize(b.t.size());
    for (size_t i = 0; i < b.q.size(); ++i) out.q[i] = nlerpShortest(a.q[i], b.q[i], t);
    for (size_t i = 0; i < b.t.size(); ++i)
        for (size_t k = 0; k < 3; ++k) out.t[i][k] = a.t[i][k] + (b.t[i][k] - a.t[i][k]) * t;
}

bool sameShape(const PoseSnap& a, const PoseSnap& b) {
    return a.valid && b.valid && a.setTick == b.setTick && a.skel == b.skel && a.local.q.size() == b.local.q.size() &&
           a.local.t.size() == b.local.t.size();
}

void capturePose(PoseSnap& p, const ActorAnim* a) {
    const alchemy::LocalPose* lp = a != nullptr ? a->localPose() : nullptr;
    p.valid = lp != nullptr && a->skeleton() != nullptr && !lp->q.empty();
    p.setTick = a != nullptr ? a->lastSetTick() : 0;
    p.skel = a != nullptr ? a->skeleton() : nullptr;
    if (p.valid) {
        p.local.q.assign(lp->q.begin(), lp->q.end());
        p.local.t.assign(lp->t.begin(), lp->t.end());
    } else {
        p.local.q.clear();
        p.local.t.clear();
    }
}

alchemy::Mat4 publishedMatrix(const ObjData& o) {   // objectDraw's matrix (FUN_00109d40 publish)
    alchemy::Mat4 m = alchemy::Mat4::identity();
    const ChrTransform& t = o.pubXf;
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = t.right[size_t(k)] * o.pubScale[0];
        m.m[size_t(4 + k)] = t.up[size_t(k)] * o.pubScale[1];
        m.m[size_t(8 + k)] = t.fwd[size_t(k)] * o.pubScale[2];
        m.m[size_t(12 + k)] = t.pos[size_t(k)];
    }
    return m;
}

// the pose an actor shows at t: B's local result, or the A -> B blend when both are comparable
const alchemy::LocalPose& poseAt(const PoseSnap* a, const PoseSnap& b, float t, alchemy::LocalPose& scratch) {
    if (a == nullptr || t >= 1.0f || !sameShape(*a, b)) return b.local;
    lerpLocal(a->local, b.local, t, scratch);
    return scratch;
}

}  // namespace

Presenter& presenter() { return gPresenter; }

alchemy::Mat4 lerpActorMatrix(const alchemy::Mat4& a, const alchemy::Mat4& b, float t) {
    if (t >= 1.0f) return b;
    alchemy::Mat4 r = b;
    float ra[3][3], rb[3][3], ro[3][3];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) {
            ra[i][k] = a.m[size_t(i * 4 + k)];
            rb[i][k] = b.m[size_t(i * 4 + k)];
        }
    if (lerpRows(ra, rb, t, ro)) {
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) r.m[size_t(i * 4 + k)] = ro[i][k];
    } else {
        for (size_t i = 0; i < 12; ++i) r.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;   // degenerate / mirrored: plain lerp
    }
    for (size_t k = 12; k < 15; ++k) r.m[k] = a.m[k] + (b.m[k] - a.m[k]) * t;
    return r;
}

void lerpCameraRows(const float a[4][4], const float b[4][4], float t, float out[4][4]) {
    std::copy(&b[0][0], &b[0][0] + 16, &out[0][0]);
    if (t >= 1.0f) return;
    float ra[3][3], rb[3][3], ro[3][3];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) {
            ra[i][k] = a[i][k];
            rb[i][k] = b[i][k];
        }
    if (lerpRows(ra, rb, t, ro)) {
        for (int i = 0; i < 3; ++i) {
            const float l = len3(ro[i]);   // camera rows are unit vectors
            for (int k = 0; k < 3; ++k) out[i][k] = l > 0.0f ? ro[i][k] / l : ro[i][k];
        }
    }
    for (int k = 0; k < 3; ++k) out[3][k] = a[3][k] + (b[3][k] - a[3][k]) * t;
}

void Presenter::reset() {
    count_ = 0;
    dirty_ = false;
    for (FrameSnap& s : snaps_) {
        s.chrCount = 0;
        s.index.clear();
        s.cam = CamSnap{};
        s.hasPlayer = false;
    }
    draw_.clear();
}

void Presenter::capture(const CamSnap& cam, uint64_t tick) {
    const int prev = b_;
    const int next = count_ == 0 ? b_ : (b_ ^ 1);
    FrameSnap& s = snaps_[next];
    const FrameSnap& p = snaps_[prev];
    s.tick = tick;
    s.clock = ActorAnim::clock();
    s.cam = cam;
    s.camPrevSerial = count_ > 0 && p.cam.valid ? p.cam.cutSerial : cam.cutSerial;
    s.camPrevSource = count_ > 0 && p.cam.valid ? p.cam.source : cam.source;
    static const bool trace = [] {   // port diagnostics: BAROQUE_DEBUG_PRESENT=1 logs the camera cuts the presenter sees
        char* v = nullptr;
        size_t len = 0;
        const bool on = _dupenv_s(&v, &len, "BAROQUE_DEBUG_PRESENT") == 0 && v != nullptr;
        std::free(v);
        return on;
    }();
    if (trace && (s.camPrevSerial != cam.cutSerial || s.camPrevSource != cam.source))
        platform::log("present: camera cut at tick " + std::to_string(tick) + " source " + std::to_string(cam.source) +
                      " serial " + std::to_string(cam.cutSerial));
    const std::vector<Chr*>& list = characters().list();
    if (s.chrs.size() < list.size()) s.chrs.resize(list.size());
    s.index.clear();
    size_t n = 0;
    for (const Chr* c : list) {
        ChrSnap& e = s.chrs[n];
        s.index[c] = n;
        ++n;
        e.key = c;
        e.serial = c->serial;
        e.id = c->id;
        e.type = c->type;
        e.object = c->obj != nullptr;
        e.jitter = c->jitter;
        e.still = c->prevPos == c->xf.m.pos;
        e.hasShell = false;
        e.objAnimTime = -1;
        e.shellPose.valid = false;
        const bool posed = c->model.loaded() && !c->model.parts.empty();
        if (e.object) {
            const ObjData& o = *c->obj;
            e.drawable = o.published && !o.hidden && !c->model.parts.empty();
            e.actor = publishedMatrix(o);
            if (o.cls == ObjClass::O00J) e.objAnimTime = o.animTimeNs;
            const bool animated = posed && c->model.skeleton() != nullptr && !c->model.clips.empty();   // objectPose
            capturePose(e.body, animated ? &c->model.anim : nullptr);
        } else {
            e.drawable = !c->model.parts.empty();
            e.actor = chrActorMatrix(*c);
            capturePose(e.body, posed ? &c->model.anim : nullptr);
            e.hasShell = chrClassShellMatrix(*c, &e.shell);
            if (e.hasShell) capturePose(e.shellPose, &c->cd.shell->anim);
        }
        e.parts.resize(c->model.parts.size());
        for (size_t i = 0; i < c->model.parts.size(); ++i) {
            const ChrModelPart& part = c->model.parts[i];
            PartSnap& ps = e.parts[i];
            ps.model = part.model.get();
            ps.attached = !part.attachBone.empty() || part.attachBoneIndex >= 0;
            const ActorAnim* pa = nullptr;
            if (!e.object) {
                if (part.actor) pa = &part.actor->anim;                          // player HEAD (chrPose)
                else pa = npcExtraActorAnim(*c, static_cast<int>(i));           // NPC heads (poseExtraActors)
            }
            capturePose(ps.actor, pa);
        }
    }
    s.chrCount = n;
    const Chr* pl = characters().player();
    s.hasPlayer = pl != nullptr;
    if (pl != nullptr) s.playerPos = pl->xf.m.pos;
    b_ = next;
    ++count_;
    dirty_ = false;
}

bool Presenter::camera(float alpha, float out[4][4], int* source) const {
    if (count_ == 0 || !snaps_[b_].cam.valid) return false;
    const FrameSnap& B = snaps_[b_];
    if (source != nullptr) *source = B.cam.source;
    const FrameSnap* A = count_ > 1 ? &snaps_[b_ ^ 1] : nullptr;
    bool cut = A == nullptr || !A->cam.valid || alpha >= 1.0f;
    if (!cut) {
        cut = A->cam.source != B.cam.source || A->cam.cutSerial != B.cam.cutSerial ||   // cut this tick
              A->cam.cutSerial != A->camPrevSerial || A->cam.source != A->camPrevSource;   // seen one tick late
    }
    if (!cut) {
        const float d[3] = {B.cam.world[3][0] - A->cam.world[3][0], B.cam.world[3][1] - A->cam.world[3][1],
                            B.cam.world[3][2] - A->cam.world[3][2]};
        const float fa = len3(A->cam.world[2]), fb = len3(B.cam.world[2]);
        const float cosF = fa > 0.0f && fb > 0.0f
                               ? (A->cam.world[2][0] * B.cam.world[2][0] + A->cam.world[2][1] * B.cam.world[2][1] +
                                  A->cam.world[2][2] * B.cam.world[2][2]) / (fa * fb)
                               : 1.0f;
        cut = len3(d) > kCamCutDistance || cosF < kCamCutCos;
    }
    if (cut) {
        std::copy(&B.cam.world[0][0], &B.cam.world[0][0] + 16, &out[0][0]);
        return true;
    }
    lerpCameraRows(A->cam.world, B.cam.world, alpha, out);
    return true;
}

int64_t Presenter::sceneClock(float alpha) const {
    if (count_ == 0) return ActorAnim::clock();
    const FrameSnap& B = snaps_[b_];
    if (count_ < 2 || alpha >= 1.0f) return B.clock;
    const FrameSnap& A = snaps_[b_ ^ 1];
    return A.clock + static_cast<int64_t>(static_cast<double>(B.clock - A.clock) * static_cast<double>(alpha));
}

bool Presenter::playerPos(float alpha, Vec3f* out) const {
    if (count_ == 0 || !snaps_[b_].hasPlayer) return false;
    const FrameSnap& B = snaps_[b_];
    *out = B.playerPos;
    if (count_ < 2 || alpha >= 1.0f || !snaps_[b_ ^ 1].hasPlayer) return true;
    const FrameSnap& A = snaps_[b_ ^ 1];
    float d[3];
    for (size_t k = 0; k < 3; ++k) d[k] = B.playerPos[k] - A.playerPos[k];
    if (len3(d) > kCutDistance) return true;
    for (size_t k = 0; k < 3; ++k) (*out)[k] = A.playerPos[k] + d[k] * alpha;
    return true;
}

const std::vector<DrawChr>& Presenter::prepare(float alpha) {
    draw_.clear();
    if (count_ == 0) return draw_;
    const FrameSnap& B = snaps_[b_];
    const FrameSnap* A = count_ > 1 ? &snaps_[b_ ^ 1] : nullptr;
    // alpha 1 right after a capture: the tick's own poses (chrPose) are on the models already
    const bool full = alpha < 1.0f || dirty_;
    if (alpha < 1.0f) dirty_ = true;
    std::unordered_set<const Chr*> live;   // B keys are dereferenced only when still listed (I1: A never is)
    live.reserve(characters().list().size());
    for (const Chr* c : characters().list()) live.insert(c);
    if (bodies_.size() < B.chrCount) bodies_.resize(B.chrCount);
    for (size_t i = 0; i < B.chrCount; ++i) {
        const ChrSnap& s = B.chrs[i];
        if (live.find(s.key) == live.end() || s.key->serial != s.serial) continue;
        const Chr& c = *s.key;
        DrawChr d;
        d.c = &c;
        d.state.actor = s.actor;
        d.state.hasShell = s.hasShell;
        d.state.shell = s.shell;
        d.state.objAnimTime = s.objAnimTime;
        d.state.body = nullptr;
        if (!full) {
            draw_.push_back(d);
            continue;
        }
        // A match and the cut rules of PRESENTATION_PLAN.md 2.3
        const ChrSnap* a = nullptr;
        if (A != nullptr && alpha < 1.0f) {
            const auto it = A->index.find(s.key);
            if (it != A->index.end()) a = &A->chrs[it->second];
        }
        bool snap = a == nullptr || a->serial != s.serial || a->type != s.type || a->id != s.id ||
                    a->object != s.object || a->drawable != s.drawable || a->jitter || s.jitter ||
                    a->parts.size() != s.parts.size() || a->hasShell != s.hasShell;
        if (!snap) {
            for (size_t p = 0; p < s.parts.size() && !snap; ++p) snap = a->parts[p].model != s.parts[p].model;
            const float dp[3] = {s.actor.m[12] - a->actor.m[12], s.actor.m[13] - a->actor.m[13], s.actor.m[14] - a->actor.m[14]};
            const float dist = len3(dp);
            if (dist > kCutDistance || (s.still && dist > 0.0f)) snap = true;   // teleport (prevPos = pos writes)
        }
        const float t = snap ? 1.0f : alpha;
        if (t < 1.0f) {
            d.state.actor = lerpActorMatrix(a->actor, s.actor, t);
            if (s.hasShell) d.state.shell = lerpActorMatrix(a->shell, s.shell, t);
            if (s.objAnimTime >= 0 && a->objAnimTime >= 0)
                d.state.objAnimTime = a->objAnimTime + static_cast<int64_t>(static_cast<double>(s.objAnimTime - a->objAnimTime) * t);
        }
        // body: composeWorld (+ joint modifiers for characters) and the skins posed by it (chrPose / objectPose)
        std::vector<alchemy::Mat4>& world = bodies_[i];
        if (s.body.valid) {
            const alchemy::LocalPose& lp = poseAt(a != nullptr ? &a->body : nullptr, s.body, t, lerped_);
            alchemy::composeWorld(lp, *s.body.skel, world);
            if (!s.object) chrApplyBoneMods(c.model, world);
            d.state.body = &world;
            if (!world.empty()) {
                for (size_t p = 0; p < c.model.parts.size() && p < s.parts.size(); ++p) {
                    const ChrModelPart& part = c.model.parts[p];
                    if (!part.model || s.parts[p].attached || s.parts[p].actor.valid) continue;
                    if (!s.object && part.actor) continue;
                    part.model->setPose(world);
                    if (part.lodModel) part.lodModel->setPose(world);   // the "lod" appearance skin (FUN_00108d20)
                }
            }
        }
        // separate actors drawn on joints (player HEAD in chrPose after the body, NPC heads in poseExtraActors)
        if (!s.object && s.body.valid && !world.empty()) {
            for (size_t p = 0; p < c.model.parts.size() && p < s.parts.size(); ++p) {
                const PartSnap& ps = s.parts[p];
                const ChrModelPart& part = c.model.parts[p];
                if (!ps.actor.valid || !part.model) continue;
                const PartSnap* pa = a != nullptr && p < a->parts.size() ? &a->parts[p] : nullptr;
                const alchemy::LocalPose& lp = poseAt(pa != nullptr ? &pa->actor : nullptr, ps.actor, t, lerped_);
                alchemy::composeWorld(lp, *ps.actor.skel, scratch_);
                if (!scratch_.empty()) part.model->setPose(scratch_);
            }
        }
        // E19 shell (chrClassPose)
        if (s.hasShell && s.shellPose.valid && c.cd.shell && !c.cd.shell->parts.empty() && c.cd.shell->parts[0].model) {
            const alchemy::LocalPose& lp = poseAt(a != nullptr && a->hasShell ? &a->shellPose : nullptr, s.shellPose, t, lerped_);
            alchemy::composeWorld(lp, *s.shellPose.skel, scratch_);
            if (!scratch_.empty()) c.cd.shell->parts[0].model->setPose(scratch_);
        }
        draw_.push_back(d);
    }
    return draw_;
}

}  // namespace baroque::game::present
