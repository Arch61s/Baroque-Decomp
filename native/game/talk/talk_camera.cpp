// Conversation camera of the talk manager 0x53c6e0 (EVENTS_EFFECTS_PROGRESSION.md 1.7): FUN_001a2950 allowed,
// FUN_001a2b40 per tick (button = key-assign slot 2 "Conversation Camera", uRam004e65c0), FUN_001a2cd0 release,
// FUN_001aa280 / FUN_001a1610 the fixed camera task (vtable 0x4cde20: FUN_001a1b00 dtor, FUN_0010da30 update =
// publish when active, FUN_0010da80, FUN_001a1b60 empty).
#include "game/talk/talk_manager.hpp"

#include "game/camera.hpp"

#include <cmath>

namespace baroque::game {

namespace {

constexpr uint32_t kC22A = data::tag4("C22A");   // 0x462280
constexpr uint32_t kC22B = data::tag4("C22B");   // 0x453f38
constexpr uint32_t kC18A = data::tag4("C18A");   // 0x4538e8

// 0x4989c0: per speaker-name index (FUN_001a98a0, list 0x4625c0) {distance, eye height, target height, yaw degrees}
constexpr float kCamPresets[52][4] = {
    {0, 0, 0, 0},            {0, 0, 0, 0},            {250, 160, 125, -5},     {250, 155, 115, 5},
    {240, 160, 120, -10},    {180, 170, 160, 0},      {240, 160, 120, -10},    {250, 165, 125, 2},
    {250, 165, 125, -2},     {250, 165, 125, 0},      {250, 165, 125, 2},      {250, 165, 125, -2},
    {250, 165, 125, 0},      {250, 165, 125, 2},      {250, 165, 125, -2},     {250, 165, 125, 0},
    {250, 165, 125, 2},      {250, 165, 125, -2},     {300, 175, 140, -5},     {250, 150, 105, 10},
    {250, 170, 120, 5},      {250, 170, 120, -5},     {250, 170, 120, 10},     {250, 170, 120, 5},
    {240, 155, 150, 10},     {240, 155, 100, 10},     {250, 160, 105, 10},     {250, 160, 125, -5},
    {240, 160, 140, 10},     {240, 160, 90, -5},      {240, 160, 90, 5},       {230, 155, 85, 15},
    {230, 155, 85, -5},      {250, 170, 110, -15},    {250, 155, 90, 10},      {0, 0, 0, 0},
    {250, 155, 105, -5},     {0, 0, 0, 0},            {250, 155, 100, 15},     {250, 155, 115, 10},
    {250, 155, 115, -10},    {0, 0, 0, 0},            {250, 155, 105, 10},     {250, 155, 105, 5},
    {250, 155, 105, 0},      {250, 155, 105, -5},     {250, 155, 105, 10},     {0, 0, 0, 0},
    {0, 0, 0, 0},            {0, 0, 0, 0},            {0, 0, 0, 0},            {340, 185, 150, 5}};

TalkCamera gCam;            // the pool object FUN_00132b20(0) hands out (one conversation camera at a time)

// What the talk camera task last copied into the renderer view 0x4e7b30 (FUN_0010da30 -> FUN_0039f1d0(.., +0x18)).
// The copy happens only in the task's update during the task-list walk while it is *0x4e7b18; FUN_0010da00 /
// FUN_0010d9c0 (push / pop) only move the pointer. The push / pop run in the talk task after the walk, so the tick of
// the push still renders the gameplay camera and the tick of the pop still renders this one (the screen transition
// FUN_00177cc0 of both captures that picture and fades it over the new camera).
struct Published {
    bool valid = false;
    float world[4][4] = {};
} gPublished;

// orig FUN_003a01f0 (work/decompiled/003a01f0.c): out = a x b
void cross(float out[3], const float a[3], const float b[3]) {
    const float r0 = a[1] * b[2] - a[2] * b[1];
    const float r1 = a[2] * b[0] - a[0] * b[2];
    const float r2 = a[0] * b[1] - a[1] * b[0];
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
}

}  // namespace

// orig FUN_001a2950 (work/decompiled/001a2950.c): the conversation camera may be switched on
bool TalkManager::cameraAllowed() const {
    TalkWorld& w = *talkWorld();
    TalkActor pl = w.player();
    if (pl == nullptr || w.itemScreenOpen() || active == 0) return false;
    if (w.fadeRunning() || w.optionBit4()) return false;                  // iRam004e7898, FUN_0015dcb0()
    if (skippable == 0 || facing == 0 || noVoice != 0) return false;
    const uint32_t spk = speaker;
    if (spk == 0) return false;
    // the inline "dead" test: player type 0 or 2 with status HP 0
    const int t = w.actorType(pl);
    bool dead = t == 0 || t == 2;
    if (dead) dead = w.actorHp(pl) == 0;
    if (dead) return false;
    if (spk == kC22A || spk == kC22B || spk == kC18A) return false;
    return static_cast<uint32_t>(state - 3) < 3u;
}

// orig FUN_001a2b40 (work/decompiled/001a2b40.c)
void TalkManager::cameraTick() {
    TalkWorld& w = *talkWorld();
    if (w.enginePaused()) return;
    const uint32_t pressed = w.padPressed();                               // uRam004e6288
    if (cameraAllowed()) {
        if ((pressed & w.talkButton()) != 0 && camera == nullptr) {
            TalkCamera* cam = &gCam;                                         // FUN_001aa280 -> FUN_00132b20(0)
            cameraBuild(*cam);                                               // FUN_001a1610
            cam->pushed = true;                                              // FUN_0010da00 (+ task list FUN_00207450)
            camera = cam;
            float d[3];
            w.mainLightDir(d);                                               // FUN_00108c90(0x4e78e0, uRam0050a2cc)
            savedLight[0] = d[0];
            savedLight[1] = d[1];
            savedLight[2] = d[2];
            w.transition();                                                  // FUN_00177cc0(0, 0x51b620, 3)
            return;
        }
    }
    if (camera != nullptr && ((pressed & w.talkButton()) != 0 || facing == 0 || active == 0 || speaker == 0))
        cameraRelease();                                                     // FUN_001a2cd0
}

// orig FUN_001a2cd0 (work/decompiled/001a2cd0.c)
void TalkManager::cameraRelease() {
    if (camera == nullptr) return;
    camera->pushed = false;                                                  // FUN_0010d9c0(camera): pop + unlink
    camera = nullptr;
    talkWorld()->setMainLightDir(savedLight);                               // FUN_00108b70(.., +0x74)
    talkWorld()->transition();                                               // FUN_00177cc0(0, 0x51b620, 3)
}

// orig FUN_001a1610 (work/decompiled/001a1610.c): fixed camera in front of the speaker (+0x18), preset by the
// speaker-name index +0x1018 (table 0x4989c0; -1 = default distance 300 (DAT_00462268) at height 170)
void TalkManager::cameraBuild(TalkCamera& cam) {
    TalkWorld& w = *talkWorld();
    TalkActor spk = actorById(speaker);                                     // FUN_001a9810(0x53c6e0, uRam0053c6f8)
    const int preset = nameIndex;                                            // iRam0053d6f8
    if (spk == nullptr || !w.activeCameraExists()) return;                  // iRam004e7b18 != 0
    float p[3];
    float f[3];
    w.actorPos(spk, p);
    w.actorForward(spk, f);
    float eye[3];
    float targetH;
    if (preset == -1) {
        eye[0] = f[0] * 300.0f + p[0] + 0.0f;
        eye[1] = f[1] * 300.0f + p[1] + 0.0f + 170.0f;
        eye[2] = f[2] * 300.0f + p[2] + 0.0f;
        targetH = 170.0f;
    } else {
        const float* t = kCamPresets[preset];
        float v[3] = {(f[0] * t[0] + p[0] + 0.0f) - p[0], (f[1] * t[0] + p[1] + 0.0f + t[1]) - p[1],
                      (f[2] * t[0] + p[2] + 0.0f) - p[2]};
        rotateY(t[3] * 0.017453292f, v);                                     // FUN_00206be0
        eye[0] = v[0] + p[0];
        eye[1] = v[1] + p[1];
        eye[2] = v[2] + p[2];
        targetH = t[2];
    }
    // forward = normalize(eye - target), right = normalize(up x forward) in the XZ plane, up = forward x right
    float fwd[3] = {eye[0] - p[0], eye[1] - (p[1] + targetH), eye[2] - p[2]};
    const float inv = 1.0f / std::sqrt(fwd[2] * fwd[2] + fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    fwd[0] *= inv;
    fwd[1] *= inv;
    fwd[2] *= inv;
    const float upW[3] = {0.0f, 1.0f, 0.0f};
    float right[3];
    cross(right, upW, fwd);                                                  // FUN_003a01f0(&right, &up, &fwd)
    const float ir = 1.0f / std::sqrt(right[2] * right[2] + right[0] * right[0] + 0.0f);
    right[0] *= ir;
    right[1] = ir * 0.0f;
    right[2] *= ir;
    float up[3];
    cross(up, fwd, right);                                                   // FUN_003a01f0(&up, &fwd, &right)
    const float iu = 1.0f / std::sqrt(up[2] * up[2] + up[0] * up[0] + up[1] * up[1]);
    up[0] *= iu;
    up[1] *= iu;
    up[2] *= iu;
    for (int k = 0; k < 3; ++k) {
        cam.world[0][k] = right[k];                                          // +0x18
        cam.world[1][k] = up[k];                                             // +0x28
        cam.world[2][k] = fwd[k];                                            // +0x38
        cam.world[3][k] = eye[k];                                            // +0x48
        camRight[k] = right[k];                                              // FUN_001a1d80 (+0x50)
        camUp[k] = up[k];                                                    // FUN_001a1da0 (+0x5c)
        camFwd[k] = fwd[k];                                                  // FUN_001a1d90 (+0x68)
        camPos[k] = eye[k];                                                  // FUN_001a1db0 (+0x44)
    }
}

bool talkCameraActive() { return gCam.pushed; }

// orig FUN_0010da30 (work/decompiled/0010da30.c) for the talk camera task (vtable 0x4cde20, update vt+0x14 empty):
// publish +0x18 when it is the active camera *0x4e7b18. Called once per task-list walk (Game camera task step).
void talkCameraPublish() {
    gPublished.valid = gCam.pushed;
    if (!gCam.pushed) return;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) gPublished.world[r][c] = gCam.world[r][c];
}

// the view this tick renders through the talk camera: its last publish (not the push state, see gPublished)
bool talkCameraMatrix(float out[4][4]) {
    if (!gPublished.valid) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r][c] = gPublished.world[r][c];
    return true;
}

// 0x53c6e8 != 0 && 0x53c720 != 0 -> FUN_001a1d90 / FUN_001a1d80 (the player controller's movement axes)
bool talkCameraAxes(float fwd[3], float right[3]) {
    const TalkManager& m = talkManager();
    if (m.active == 0 || m.camera == nullptr) return false;
    for (int k = 0; k < 3; ++k) {
        fwd[k] = m.camFwd[k];
        right[k] = m.camRight[k];
    }
    return true;
}

}  // namespace baroque::game
