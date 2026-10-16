#pragma once
// EXI particle-effect runtime (DATA/EXI/BQ.EXI + DATA/EXI/BQ.PNG), translated from the manager at 0x4e7ca0
// (FUN_001e7b80 ctor .. FUN_001ed190). Report: work/reports/EFFECTS_NATIVE.md.
//
// Structure (original offsets kept in the field comments):
//   EffectManager  = manager 0x4e7ca0 (+i*0x150; the game only uses index 0x4e7df0 = 0).
//   ExiInstance    = 0xa0-byte effect instance from the pool at mgr+0 (0x8000 bytes / 0xa0 = 204 entries).
//   ExiNode        = 0x70-byte node from the pool at mgr+4 (0xe000 / 0x70 = 512): sequence nodes (kind 1 object,
//                    2 emitter, 0 group) and emitted particles (kind 1) share this pool.
// The per-tick update only runs inside the draw pass (FUN_001e7e90 draws an instance, then calls FUN_001e95c0
// and frees it when dead), exactly like the original; drawPass() with a null sink is the headless tick.
#include "data/exi.hpp"
#include "game/effects/exi_math.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace baroque::game {

inline constexpr int kExiInstanceCapacity = 0x8000 / 0xa0;  // FUN_00111e30 pool 1 (UNVERIFIED: pool overhead)
inline constexpr int kExiNodeCapacity = 0xe000 / 0x70;      // FUN_00111e30 pool 2 / FUN_001e7c80 mgr+0xec
inline constexpr int kExiMaxVertices = 0x1400;              // FUN_001e7c80 mgr+0xe0 (per frame)

struct ExiNode {
    ExiMat matrix;                 // +0x00 (copied from SEQS +0x00 for sequence nodes; built by FUN_001e9820)
    float start = 0.0f;            // +0x40 sequence start frame / particle age origin (0)
    float end = 0.0f;              // +0x44 sequence end frame / particle length (OBJS s16 +2)
    float t = 0.0f;                // +0x48 local time
    float scale = 0.0f;            // +0x4c time scale (SEQS +0x48) / particle 1/life
    bool refIsEmitter = false;     // +0x50 points at an EMTS (true) or OBJS record
    int ref = -1;                  //       record index
    int kind = 0;                  // +0x54 0 group, 1 object, 2 emitter
    int childCount = 0;            // +0x58
    ExiNode* firstChild = nullptr; // +0x5c
    ExiNode* lastChild = nullptr;  // +0x60
    ExiNode* next = nullptr;       // +0x64
    float accum = 0.0f;            // +0x68 emitter spawn accumulator
    bool inUse = false;            // pool bookkeeping (not in the original)
};

struct ExiInstance {
    ExiInstance* prev = nullptr;   // +0x00
    ExiInstance* next = nullptr;   // +0x04
    int effect = -1;               // +0x08 EFCS index (pointer in the original)
    ExiNode* seq = nullptr;        // +0x0c first sequence node
    int particles = 0;             // +0x10 live particle count (FUN_001e93a0 result)
    ExiMat matrix;                 // +0x14 scale * rotation (* direction) with translation
    float scale[4] = {};           // +0x54 (FUN_001eba20 writes 4 floats; +0x60 is not set by FUN_001eb0d0)
    float rot[3] = {};             // +0x64 degrees
    int time = 0;                  // +0x74 frame
    int prevTime = 0;              // +0x78
    int rsqIndex = 0;              // +0x7c
    int loopCount = 0;             // +0x80
    uint16_t ovEnd = 0xffff;       // +0x84 loop-out record {end, restart, count} (FUN_001ebbe0)
    uint16_t ovRestart = 0xffff;   // +0x86
    int16_t ovCount = 0;           // +0x88
    uint16_t flags8a = 0;          // +0x8a bit0 = use loop-out record
    int rate = 0x10000;            // +0x8c 16.16 frames per tick
    uint32_t frac = 0;             // +0x90 16.16 accumulator
    int id = 0;                    // +0x94
    uint32_t flags98 = 0;          // +0x98 bit0 (FUN_001eb690; no reader in 0x1e7c80..0x1ed200)
    float tag = 0.0f;              // +0x9c camera-depth offset passed to FUN_001ebd90 (<0: camera-aligned parent)
    bool inUse = false;
};

// Camera / pass inputs of FUN_001e7e90 / FUN_001ebd90.
struct ExiView {
    ExiMat view;                   // *(mgr+0x1c) = 0x4e7b30 world->view
    ExiMat projection;             // igVisualContext matrix 0 (vt+0x310(0)), used by the screen-UV mode 8
    int viewport[4] = {0, 0, 512, 448};  // vt+0x2c0: x, y, w, h
    float fogStart = 0.0f;         // *(0x4e790c)+0x10
    float fogEnd = 1500.0f;        // *(0x4e790c)+0x14
};

struct ExiDrawVertex {
    float x, y, z;                 // mgr+0x10c stream (component 0)
    float u, v;                    // mgr+0x144 stream (component 3)
    uint32_t rgba;                 // mgr+0x128 stream (component 2): R | G<<8 | B<<16 | A<<24
};

// Render state record = OBJS + 4 (FUN_001eaa10).
struct ExiRenderState {
    int object = -1;
    bool depthTest = false;        // OBJS +7 bit0 -> vt+0x368
    bool depthWrite = false;       // OBJS +7 bit1 -> vt+0x378
    int blend = 0;                 // OBJS +6 & 0xf: 2 -> (4,5), 1 -> subtractive GS (2,0,0,1,0x80), else (4,1)
    bool textureOff = false;       // OBJS +7 bit2 -> vt+0x1b8(0,0)
    int textureIndex = 0;          // OBJS +0xe (0xffff when +7 bit2, FUN_001e8b20); 1 = screen copy, else mgr+0xa0[i]
    int uvWindow[4] = {};          // OBJS +0x10..+0x16 u0,u1,v0,v1 (texels) -> REGION_REPEAT FUN_0038afc0/FUN_0038af40
    bool triangleList = false;     // OBJS +0xc == 1 (FUN_001ecec0)
    int vertsPerPrim = 0;          // OBJS +4
};

class ExiBatchSink {
public:
    virtual ~ExiBatchSink() = default;
    virtual void setState(const ExiRenderState& state) = 0;  // FUN_001eaa10
    // FUN_001ecec0: triangle list (count/3) or `count / vertsPerPrim` strips of vertsPerPrim vertices.
    virtual void draw(const ExiRenderState& state, const ExiDrawVertex* verts, int count) = 0;
};

// PORT (presentation split, effects_gl.hpp): one FUN_001ebd90 call of the tick's pass, kept so the presentation can
// rebuild the particle against the camera it actually draws with (billboards, tag offset, distance fade and the
// UV-mode-8 screen UVs all read the camera). The node / parent values are the ones the pass drew (before the update).
struct ExiParticleJob {
    float tag = 0.0f;
    ExiNode node;                  // ref / t / matrix as drawn (links are not followed by FUN_001ebd90)
    bool hasParent = false;
    ExiMat parent;
};
struct ExiRecordedCall {
    bool isState = false;          // FUN_001eaa10 call
    ExiRenderState state;
    std::vector<ExiParticleJob> jobs;  // FUN_001ecec0 batch: the build attempts in pass order
};
using ExiRecording = std::vector<ExiRecordedCall>;

struct ExiPassStats {
    int instances = 0;
    int vertices = 0;
    int primitives = 0;
    int batches = 0;
};

class EffectManager {
public:
    EffectManager();  // orig FUN_001e7b80

    bool load(std::span<const uint8_t> exiBytes);        // orig FUN_001e8b20 via data::parseExi
    void unload();                                       // orig FUN_001e7e00 / FUN_001e9230
    bool loaded() const { return loaded_; }
    const data::ExiFile& file() const { return file_; }
    int effectCount() const { return static_cast<int>(file_.effects.size()); }

    // Manager-level functions (addresses of the mgr-taking originals).
    int create(int effectId, const float* pos, const float* rotDeg, const float* scale, const float* dir);  // FUN_001eb0d0
    ExiInstance* find(int id);                           // FUN_001e9750
    void setTag(float tag, int id);                      // FUN_001ebc60
    void setMatrix(int id, const ExiMat& m);             // FUN_001ebc90
    bool exists(int id) { return find(id) != nullptr; }  // FUN_001eb6f0
    void kill(int id);                                   // FUN_001eb650
    void setFlag98(int id, bool on);                     // FUN_001eb690
    void setPosition(int id, const float pos[3]);        // FUN_001eb710
    void getPosition(int id, float out[3]);              // FUN_001eb760
    void setPositionRotation(int id, const float pos[3], const float rotDeg[3]);  // FUN_001eb7b0
    void setRotation(int id, const float rotDeg[3]);     // FUN_001eb870
    void addRotation(int id, const float rotDeg[3]);     // FUN_001eb940
    void setScale(int id, const float scale[4]);         // FUN_001eba20
    bool setTime(int id, int frame);                     // FUN_001ebaf0
    int sequenceLength(int id);                          // FUN_001ebb30
    void setRate(int id, int rate16);                    // FUN_001ebbb0
    bool loopOut(int id, int16_t count, int16_t end, int restart);  // FUN_001ebbe0
    void killAll();                                      // FUN_001eae80
    void killFinished();                                 // FUN_001eaed0
    void setPaused(bool paused);                         // FUN_001e8a70
    bool paused() const { return (flagsD0_ & 1u) != 0; }

    // orig FUN_001e7e90: draws every instance (sink may be null = headless) and updates it afterwards.
    // `record` (PORT): when set, the pass also appends its state changes and build attempts (see ExiRecording).
    ExiPassStats drawPass(const ExiView& view, ExiBatchSink* sink, ExiRecording* record = nullptr);
    // PORT: draws a recording with another camera: the FUN_001e7e90 build / budget / submit steps without the update.
    // With the recording's own view the output is identical to the recorded pass (Baroque-EffectsCheck).
    ExiPassStats replay(const ExiView& view, const ExiRecording& record, ExiBatchSink* sink);

    int instanceCount() const;
    int liveNodeCount() const;
    const ExiInstance* firstInstance() const { return head_; }
    uint32_t seed() const { return seed_; }

    // Record helpers (also used by the renderer and the check).
    ExiRenderState renderState(int object) const;

private:
    struct Track { int16_t type; int16_t count; uint32_t key; };
    Track emitterTrack(int emitter, int off) const;
    Track objectTrack(int object, int off) const;
    float evalTrack(float t, const Track& tr) const;     // FUN_001eac20
    float emitterF32(int e, int off) const;
    uint8_t emitterByte(int e, int off) const;
    int16_t emitterS16(int e, int off) const;
    uint8_t objectByte(int o, int off) const;
    int refByte0(bool isEmitter, int index) const;
    int rawVertexCount(int object) const;           // OBJS +4

    ExiNode* allocNode();                                // pool vt+0x74
    void freeNode(ExiNode* n);                           // FUN_001e9790 (pool vt+0x90)
    void freeTree(ExiNode* n);                           // FUN_001e97b0
    ExiInstance* allocInstance();
    void freeInstance(ExiInstance* inst);                // FUN_001e9310
    ExiNode* makeSequenceNode(int seq);                  // FUN_001eaf30
    ExiNode* makeGroupNode();                            // FUN_001eb020
    bool sameRef(int seqA, int seqB) const;              // FUN_001eb070
    void rebuildMatrix(ExiInstance& inst, bool keepTranslation);  // shared tail of FUN_001eb7b0..FUN_001eba20
    bool updateInstance(ExiInstance& inst);              // FUN_001e95c0
    int updateNodes(ExiInstance& inst, ExiNode* node);   // FUN_001e93a0
    void emit(float t, ExiNode& node, const ExiMat& instanceMatrix);  // FUN_001e9820
    float rand01();                                      // inline LCG of FUN_001e9820
    // FUN_001ebd90: fills `out` (vertex count = OBJS +4) and returns false when the alpha fade reaches 0.
    bool buildParticle(float tag, const ExiView& view, const ExiNode& node, const ExiMat* parent,
                       ExiDrawVertex* out) const;

    data::ExiFile file_;
    bool loaded_ = false;
    std::vector<ExiNode> nodes_;
    std::vector<ExiInstance> instances_;
    std::vector<ExiNode*> freeNodes_;
    std::vector<ExiInstance*> freeInstances_;
    ExiInstance* head_ = nullptr;  // +0xcc
    uint32_t flagsD0_ = 0;         // +0xd0 bit0 = paused
    int idCounter_ = 0;            // +0xd4
    uint32_t seed_ = 0x0badface;   // +0xd8 (FUN_001e7b80)
    ExiMat camera_;                // +0x20 inverse(view) (+0x60 copy)
    std::vector<ExiDrawVertex> scratch_;
};

// Game-side wrappers 0x112190..0x112770 (manager index 0x4e7df0 = 0) and the per-frame hook FUN_001120b0.
class Effects {
public:
    EffectManager& manager() { return mgr_; }
    bool init();                                         // FUN_00111e30 + FUN_001120c0 (BQ.EXI); texture: renderer
    // FUN_00112190(tag f12, effectId, pos, rot, scale): position (x, y + 0.15, z) or (0, 0.15, 0) when null; rot in degrees,
    // scale xyz; returns the instance id (0 = not created). pos4 needs 3 floats.
    int spawn(float tag, int effectId, const float* pos4, const float* rotDeg, const float* scale);
    void getPosition(int id, float out[3]) { mgr_.getPosition(id, out); }            // FUN_00112290
    void setPosition(int id, const float pos[3]);                                     // FUN_001122d0 (+0.15 y)
    void setRotation(int id, const float rotDeg[3]) { mgr_.setRotation(id, rotDeg); } // FUN_00112370
    void addRotation(int id, const float rotDeg[3]) { mgr_.addRotation(id, rotDeg); } // FUN_001123b0
    void setPositionRotation(int id, const float pos[3], const float rotDeg[3]);      // FUN_001123f0 (+0.15 y)
    void setScale(int id, const float scale[4]) { mgr_.setScale(id, scale); }        // FUN_00112490
    void setTag(int id, float tag) { mgr_.setTag(tag, id); }                          // FUN_001124d0
    bool exists(int id) { return mgr_.exists(id); }                                   // FUN_00112500
    void kill(int id) { mgr_.kill(id); }                                              // FUN_00112530
    void stop(int id);                                                                // FUN_00112560
    void killFinished() { mgr_.killFinished(); }                                      // FUN_00112600
    void killAll() { mgr_.killAll(); }                                                // FUN_00112610
    void setPaused(bool p) { mgr_.setPaused(p); }                                     // FUN_00112620
    void setFlag98(int id, bool on) { mgr_.setFlag98(id, on); }                       // FUN_00112630
    int sequenceLength(int id) { return mgr_.sequenceLength(id); }                    // FUN_00112670
    void setRate(int id, int rate16) { mgr_.setRate(id, rate16); }                    // FUN_001126a0
    void loopOut(int id, int16_t count, int16_t end, int restart) { mgr_.loopOut(id, count, end, restart); }  // FUN_001126e0
    void setTime(int id, int frame) { mgr_.setTime(id, frame); }                      // FUN_00112730
    void setMatrix(int id, const ExiMat& m) { mgr_.setMatrix(id, m); }                // FUN_00112770
private:
    EffectManager mgr_;
};
Effects& effects();

}  // namespace baroque::game
