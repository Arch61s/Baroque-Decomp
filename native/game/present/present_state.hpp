#pragma once
// PORT presentation snapshots (PRESENTATION_PLAN.md 2.2 / 2.3). The Game scene's tick-side render pass
// (renderTickPass, end of FUN_0011bec0) captures what the 3-D pass of that tick shows: the active camera, every
// character's actor matrix, bone-local animation result, part actors, class extras (E19 shell) and object transform
// time. The presentation draws between the last two snapshots A (tick N-1) and B (tick N) at alpha in [0, 1]:
// alpha = 1 is exactly tick N (the original frame); cuts / teleports / hard animation sets show B.
// Invariant I1: nothing here writes Chr / ActorAnim / combiner / camera / event state or draws game RNG. The only
// writes outside this module are SceneModel skin poses (GPU-side presentation state the tick also sets).
#include "alchemy/animation.hpp"
#include "game/collision.hpp"
#include "game/present/present_draw.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace baroque::game {
class ActorAnim;
}

namespace baroque::game::present {

enum : int { kCamGameplay = 0, kCamEvent = 1, kCamTalk = 2 };

struct CamSnap {
    bool valid = false;
    int source = kCamGameplay;        // the active camera *0x4e7b18
    float world[4][4] = {};           // rows right / up / forward / eye (camera +0x18)
    uint32_t cutSerial = 0;           // Camera::cutSerial (gameplay) / EventManager::camCutSerial (event)
    bool lightFollowsCamera = false;  // FUN_0013e340 tail: main light = -forward of the gameplay camera
};

struct PoseSnap {                     // one actor's combiner result as its tick pose used it
    bool valid = false;
    uint64_t setTick = 0;             // ActorAnim::lastSetTick (hard set FUN_0010a7f0 between snapshots = cut)
    const alchemy::Skeleton* skel = nullptr;
    alchemy::LocalPose local;
};

struct PartSnap {
    const void* model = nullptr;      // SceneModel identity (compared, never dereferenced from A)
    bool attached = false;            // drawn on a joint of the body (attachBone / attachBoneIndex)
    PoseSnap actor;                   // separate actor (player HEAD PartActor, NPC extra actor)
};

struct ChrSnap {
    const Chr* key = nullptr;         // compared only (A); B entries are re-validated against the live list
    uint32_t serial = 0, id = 0;
    int type = 0;
    bool object = false, drawable = false, jitter = false;
    bool still = false;               // prevPos == pos at capture (teleports write both)
    alchemy::Mat4 actor = alchemy::Mat4::identity();
    PoseSnap body;
    std::vector<PartSnap> parts;
    bool hasShell = false;            // E19 shell
    alchemy::Mat4 shell = alchemy::Mat4::identity();
    PoseSnap shellPose;
    int64_t objAnimTime = -1;         // O00J model time
};

struct FrameSnap {
    uint64_t tick = 0;
    int64_t clock = 0;                // engine clock lRam004e78e0 (igTransform inputs)
    CamSnap cam;
    uint32_t camPrevSerial = 0;       // the previous snapshot's camera cut serial / source (a cut seen one tick late)
    int camPrevSource = -1;
    std::vector<ChrSnap> chrs;
    size_t chrCount = 0;
    std::unordered_map<const Chr*, size_t> index;
    bool hasPlayer = false;
    Vec3f playerPos{};
};

struct DrawChr {
    const Chr* c = nullptr;
    ChrDrawState state;
};

class Presenter {
public:
    void reset();                     // scene init / exit / floor load: no A, no B
    // tick side (renderTickPass): snapshot of the state the tick's 3-D pass shows
    void capture(const CamSnap& cam, uint64_t tick);
    bool ready() const { return count_ > 0; }
    // presentation side (const with respect to the game)
    bool camera(float alpha, float out[4][4], int* source) const;   // false: nothing captured
    const CamSnap* latestCamera() const { return count_ > 0 ? &snaps_[b_].cam : nullptr; }
    // interpolated draw states of this frame; applies the skin poses (call once per presented frame)
    const std::vector<DrawChr>& prepare(float alpha);
    int64_t sceneClock(float alpha) const;
    bool playerPos(float alpha, Vec3f* out) const;
    uint64_t latestTick() const { return count_ > 0 ? snaps_[b_].tick : 0; }

private:
    FrameSnap snaps_[2];
    int b_ = 0;                       // index of B
    int count_ = 0;                   // snapshots taken since reset (A exists when >= 2)
    bool dirty_ = false;              // a frame since the last capture applied an interpolated (alpha < 1) pose
    std::vector<DrawChr> draw_;
    std::vector<std::vector<alchemy::Mat4>> bodies_;
    std::vector<alchemy::Mat4> scratch_;
    alchemy::LocalPose lerped_;
};
Presenter& presenter();

// helpers (also used by the Game scene for the camera)
alchemy::Mat4 lerpActorMatrix(const alchemy::Mat4& a, const alchemy::Mat4& b, float t);
void lerpCameraRows(const float a[4][4], const float b[4][4], float t, float out[4][4]);

}  // namespace baroque::game::present
