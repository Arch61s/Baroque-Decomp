#pragma once
#include <cstdint>
#include <unordered_map>
// Scene-graph traversal and GS-faithful rendering of Alchemy .IGB models on raylib/rlgl.
//
// Attribute enum -> GL state mapping (ELF evidence; see work/reports/ALCHEMY_NATIVE.md for the table):
//   igBlendFunctionAttr._src/_dst  : GL factor order (0 ZERO, 1 ONE, 2 SRC_COLOR, 3 1-SRC_COLOR, 4 SRC_ALPHA,
//                                    5 1-SRC_ALPHA, 6 DST_ALPHA, 7 1-DST_ALPHA, 8 DST_COLOR, 9 1-DST_COLOR,
//                                    10 SRC_ALPHA_SATURATE, 11 CONSTANT). FUN_003835c0 -> FUN_00383660 looks
//                                    the pair up in the table at 0x484cb0 and writes GS ALPHA {A,B,C,D,FIX}
//                                    (FUN_00248920): (4,5) -> (Cs-Cd)*As+Cd, (4,1) -> Cs*As+Cd, (1,1) -> Cs+Cd.
//   igAlphaFunctionAttr._func      : GL compare order (0 NEVER .. 7 ALWAYS); FUN_003837c0 maps through the table
//                                    at 0x484520 {0,2,4,3,6,7,5,1} = GS ATST, AREF = (int)(_refValue * 128.0).
//   igAlphaStateAttr._enabled      : FUN_00383700 -> cap 3 = GS TEST.ATE.
//   igBlendStateAttr._enabled      : FUN_00383510 -> cap 2 = GS PRIM.ABE.
//   igTextureStateAttr._enabled    : FUN_00388de0 -> cap 1 = GS PRIM.TME (per stage).
//   igDepthWriteStateAttr._enabled : FUN_00383ca0 (+0x504) -> ZBUF.ZMSK.
//   igShadeModelAttr._mode         : FUN_003839c0, table 0x484550 {1,2}: mode 1 -> PRIM.IIP = 1 (Gouraud).
//   igCullFaceAttr._enabled/_mode  : FUN_00383a20/FUN_00383aa0 -> caps {16,15}[mode] (VU1 microcode culling;
//                                    which cap is back/front is UNVERIFIED: mode 0 treated as BACK).
//   igTextureAttr wrap/filter      : see texture.hpp. All caps default to 0 at init (FUN_00247fc0).
#include "alchemy/igb.hpp"
#include "alchemy/texture.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Matrix;  // raylib

namespace baroque::alchemy {


// Row-vector 4x4 matrix as stored by Alchemy (igMatrix44f): v' = v * M, translation in m[12..14].
struct Mat4 {
    std::array<float, 16> m{};
    static Mat4 identity();
    static Mat4 translation(float x, float y, float z);
    static Mat4 fromFloats(const float* f);
    // Standard GL perspective / look-at expressed in the row-vector convention.
    static Mat4 perspective(float fovyDeg, float aspect, float zNear, float zFar);
    static Mat4 lookAt(const float eye[3], const float target[3], const float up[3]);
};
Mat4 mul(const Mat4& a, const Mat4& b);          // a then b (igb_geom.mat_mul(a, b))
bool invert(const Mat4& in, Mat4& out);          // general Gauss-Jordan inverse
void transformPoint(const Mat4& m, const float in[3], float out[3]);
void transformVector(const Mat4& m, const float in[3], float out[3]);
Matrix toRaylib(const Mat4& m);                  // same 16 floats, for rlSetUniformMatrix

struct RenderState {
    const GpuTexture* texture = nullptr;  // igTextureBindAttr unit 0
    std::string textureName;              // for diagnostics / missing-texture reports
    bool textureEnabled = false;          // igTextureStateAttr (GS default TME=0, caps cleared by FUN_00247fc0)
    bool blendEnabled = false;            // igBlendStateAttr
    int blendSrc = 4;                     // igBlendFunctionAttr._src  (default SRC_ALPHA: UNVERIFIED)
    int blendDst = 5;                     // igBlendFunctionAttr._dst  (default 1-SRC_ALPHA: UNVERIFIED)
    uint8_t blendConstant = 0x80;         // igBlendFunctionAttr._blendConstant (GS FIX)
    bool alphaTestEnabled = false;        // igAlphaStateAttr
    int alphaFunc = 7;                    // igAlphaFunctionAttr._func (7 = ALWAYS)
    float alphaRef = 0.0f;                // igAlphaFunctionAttr._refValue
    bool depthWrite = true;               // igDepthWriteStateAttr
    bool cullEnabled = false;             // igCullFaceAttr
    int cullMode = 0;                     // igCullFaceAttr._mode
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};  // igColorAttr (used when no colour stream)
    bool texMatrixEnabled = false;        // igTextureMatrixStateAttr unit 0
    Mat4 texMatrix = Mat4::identity();    // igTextureMatrixAttr._m
    bool vertexBlend = false;             // igVertexBlendStateAttr
    bool fogEnabled = false;              // igFogStateAttr
    // igMaterialAttr (+0x10 diffuse, +0x30 emission) / igMaterialModeAttr (+0xc) / igLightingStateAttr (+0xc).
    // Used by the lit branch (SceneRenderer::setLighting, the VU1 light stages in uploadLighting); the unlit branch
    // draws the vertex colours.
    std::array<float, 4> diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    std::array<float, 4> ambient{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> emission{0.0f, 0.0f, 0.0f, 1.0f};
    int materialMode = 3;                 // igMaterialModeAttr: 3 = ambient + diffuse track the vertex colour
    bool lighting = true;                 // UNVERIFIED: the game lights its models (no igLightingStateAttr in them)
    std::array<bool, 4> colorMask{true, true, true, true};   // igColorMaskAttr
    int texXform = -1;                    // SceneModel::XformNode (igTransform _target 9) driving the UVs, -1 none
};

// igOverrideAttrSet inserted by the game above a model (material fader FUN_0010af90 / FUN_0010c520, E02 overlay
// FUN_001c1140, E09 glow FUN_001c1f70). Every set field replaces the mesh's own attribute of that class.
// UNVERIFIED (Alchemy internals): nested overrides compose outermost-first (see compose()).
struct AttrOverride {
    bool colorMaskSet = false;    std::array<bool, 4> colorMask{true, true, true, true};   // igColorMaskAttr
    bool blendStateSet = false;   bool blendEnabled = false;                              // igBlendStateAttr
    bool blendFuncSet = false;    int blendSrc = 4, blendDst = 5;                         // igBlendFunctionAttr
    bool depthWriteSet = false;   bool depthWrite = true;                                 // igDepthWriteStateAttr
    bool alphaStateSet = false;   bool alphaTest = false;                                 // igAlphaStateAttr
    bool alphaFuncSet = false;    int alphaFunc = 7;     float alphaRef = 0.0f;           // igAlphaFunctionAttr
    bool materialSet = false;                                                              // igMaterialAttr
    std::array<float, 4> diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    std::array<float, 4> ambient{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> emission{0.0f, 0.0f, 0.0f, 1.0f};
    bool materialModeSet = false; int materialMode = 3;                                   // igMaterialModeAttr
    bool lightingSet = false;     bool lighting = true;                                   // igLightingStateAttr
    void apply(RenderState& st) const;
    // `inner` is below this override in the graph: this one wins where both set a class.
    AttrOverride compose(const AttrOverride& inner) const;
    bool touchesMaterial() const { return materialSet || materialModeSet || lightingSet; }
};

struct Bone {
    std::string name;
    int parent = -1;
    int bmIdx = -1;       // blend-matrix (inverse-bind) index, -1 none
    int flags = 0;
    std::array<float, 3> translation{};
};

struct Skeleton {
    bool valid = false;
    std::string name;
    std::vector<Bone> bones;
    std::vector<Mat4> invBind;     // igSkeleton._invJointArray[_jointCount]
    std::vector<Mat4> bindWorld;   // per bone: inverse(invBind[bmIdx]) or accumulated translations
    std::vector<int> bmToBone;     // blend-matrix index -> bone index
};

struct Mesh {
    std::string name;
    int geometrySlot = -1;
    int vertexArraySlot = -1;
    uint32_t vertexFormat = 0;
    int primType = 4;
    std::vector<float> positions;   // 3 per vertex (object space of the geometry node)
    std::vector<float> normals;     // 3 per vertex or empty
    std::vector<uint8_t> colors;    // RGBA8 per vertex or empty (0xFF = 1.0 under the default vertexColorScale)
    std::vector<float> uvs;         // 2 per vertex or empty
    std::vector<float> weights;     // influences per vertex
    std::vector<uint8_t> boneIndices;
    int influences = 0;
    std::vector<int> paletteToBm;   // igBlendMatrixSelect._blendMatrixIndices
    std::vector<uint16_t> indices;  // triangle list from the strips
    RenderState state;
    Mat4 world = Mat4::identity();
    bool skinned = false;
    std::vector<std::pair<int, int>> switchPath;   // (igSwitch node slot, child index) of every igSwitch ancestor
    int geoXform = -1;              // animated igTransform (_target 0) above the geometry: world = pre * T * post
    Mat4 geoPre = Mat4::identity(), geoPost = Mat4::identity();
    int selectBone = -1;            // igModelViewMatrixBoneSelect above a rigid geometry: follows that posed bone
    Mat4 selectLocal = Mat4::identity();   // transforms between the select node and the geometry
    // GPU
    unsigned vao = 0, vboPos = 0, vboNrm = 0, vboCol = 0, vboUv = 0, ebo = 0;
    std::vector<float> skinnedPositions, skinnedNormals;  // CPU skinning result (bind pose by default)
};

// igTransformSequence1_5 playback, implemented in alchemy/transform_seq.cpp (FUN_003fccb0 and callees).
struct TransformSequence {
    bool valid = false;
    std::vector<std::array<float, 3>> xlate;   // +0x08 _xlateList (igVec3fList)
    std::vector<std::array<float, 4>> quat;    // +0x0c _quatList (igQuaternionfList)
    std::vector<std::array<float, 3>> scale;   // +0x10 _scaleList (igVec3fList)
    std::vector<int64_t> times;                // +0x38 _timeListLong (ns)
    bool uniformTime = false;                  // +0x20 _uniformTimeEnabled
    int playMode = 0;                          // +0x24 _playMode (0/3 loop, 1 clamp, 2 bounce, 3 wraps last->first)
    std::array<float, 3> center{};             // +0x2c _centerOfRotation
    uint8_t channels = 0;                      // +0x48 _drivenChannels (1 translation, 2 quaternion, 4 euler, 8 scale)
    std::array<uint8_t, 3> interp{};           // +0x49 _interpolationMethod (translation, rotation, scale)
    int64_t keyOffset = 0;                     // +0x50 _keyframeTimeOffset
    int64_t duration = 0;                      // +0x58 _animationDuration
    mutable int lastKey = 1;                   // +0x28 _lastTime (bracket cache). UNVERIFIED: initial value 1

    // Reads the sequence; false (and a log line) for layouts the corpus does not use (uniform time, Euler
    // channel, Hermite interpolation data).
    bool load(const IgbFile& f, const IgObject* seq, std::vector<std::string>* log);
    // orig FUN_003fccb0(seq, out, time)
    Mat4 evaluate(int64_t timeNs) const;

private:
    Mat4 keyMatrix(float t, int k0, int k1) const;   // orig FUN_003fc680
};

class SceneModel {
public:
    struct Stats {
        int meshes = 0, vertices = 0, triangles = 0, texturesBound = 0;
        std::vector<std::string> missingTextures;
    };
    SceneModel() = default;
    ~SceneModel();
    SceneModel(const SceneModel&) = delete;
    SceneModel& operator=(const SceneModel&) = delete;

    // Traverses every scene graph of `file`, resolving textures through `libs` (in order). GPU buffers are
    // created immediately (requires an active GL context).
    bool build(const IgbFile& file, const std::vector<const TextureLibrary*>& libs, std::vector<std::string>* log);
    // Same traversal with the actor's appearance skin replaced by the igSkin named `skinName` from the file's
    // igAnimationDatabase._skinList: the game's FUN_00326280(appearance +0xc (_skin), skin) swap, e.g. the "sh" shadow
    // skin of the shadow pre-pass FUN_001113d0. The skeleton is the file's own (the actor skeleton), so setPose takes
    // the actor's bone world matrices. False (nothing built) when the file has no skin of that name.
    bool buildSkin(const IgbFile& file, const std::string& skinName, const std::vector<const TextureLibrary*>& libs,
                   std::vector<std::string>* log);
    // Bone world matrices (actor space, one per skeleton bone). The palette becomes invBind * boneWorld;
    // the default pose is the bind pose (identity palette).
    void setPose(const std::vector<Mat4>& boneWorld);
    void resetPose();
    // Part files (e.g. C00A_BODY/HAIR) carry skins but no skeleton; their blend-matrix indices refer to the
    // actor's skeleton. Call before setPose. UNVERIFIED: mirrors the runtime attaching part skins to the actor.
    void adoptSkeleton(const Skeleton& actorSkeleton);
    const Skeleton& skeleton() const { return skeleton_; }
    const std::vector<Mesh>& meshes() const { return meshes_; }
    Stats stats() const;
    bool bounds(float bmin[3], float bmax[3]) const;  // world-space bounds of the posed geometry
    // igSwitch nodes named `name`: only child `child` is drawn (-1 = every child, the build default). Used for the
    // "Toggle" switches of the map scenes (game FUN_001b8440 writes the node's _childMask bits 0 / 1).
    // The file's own _childMask (igBitMask) is the initial mask.
    int setSwitchChild(const std::string& name, int child);   // returns the number of switches matched
    bool meshVisible(size_t meshIndex) const;

    // igTransform nodes with a _transformInput (igTransformSequence1_5), evaluated at the engine clock by
    // animate() (the renderer calls it with sceneTime()). _target 9 = texture matrix, 0 = model matrix.
    struct XformNode {
        int slot = -1;
        int target = 0;                     // igTransform +0x60
        Mat4 m = Mat4::identity();          // igTransform +0x20 _m (written by FUN_0039d980 when detached)
        std::vector<int> path;              // child indices from the skinned graph / scene root down to the node
        int seq = -1;                       // index into seqs_ (-1: no input)
        bool detached = false;              // FUN_00326280(+0x68, 0): _transformInput removed by the game
        Mat4 current = Mat4::identity();
    };
    const std::vector<XformNode>& transforms() const { return xforms_; }
    int findTransformByPath(const std::vector<int>& path) const;
    void detachTransformInput(int index);                        // FUN_00326280(node +0x68, 0)
    void setTransformTranslation(int index, float x, float y, float z);   // FUN_0039d980(node +0x20, v)
    void animate(int64_t timeNs) const;                          // igTransform traversal at the clock
    // A model-owned time for its transform inputs instead of the engine clock (-1 = engine clock); the game's
    // model wrapper sets it for models whose animation it drives (O00J statue vt+0x9c).
    void setTimeOverride(int64_t ns) const { timeOverride_ = ns; }
    int64_t timeOverride() const { return timeOverride_; }
    const Mat4& meshTexMatrix(const Mesh& m) const;
    Mat4 meshWorld(const Mesh& m) const;

private:
    struct Ctx;
    void visit(const IgObject* node, Ctx ctx);
    void applyAttr(const IgObject* attr, Ctx& ctx, const IgObject* node);
    void addGeometry(const IgObject* node, const IgObject* geomAttr, const Ctx& ctx);
    void loadSkeleton(const IgObject* skel);
    const GpuTexture* resolveTexture(const IgObject* bind, std::string& nameOut);
    void uploadMesh(Mesh& mesh);
    void skinMesh(Mesh& mesh, const std::vector<Mat4>& boneWorld);

    const IgbFile* file_ = nullptr;
    std::vector<const TextureLibrary*> libs_;
    std::vector<std::string>* log_ = nullptr;
    std::vector<Mesh> meshes_;
    Skeleton skeleton_;
    std::vector<int> visited_;
    std::vector<std::string> missingTextures_;
    struct SwitchNode { int slot; std::string name; int activeChild = -1; uint32_t mask = 0xffffffffu; };
    std::vector<SwitchNode> switches_;
    mutable std::vector<XformNode> xforms_;
    std::vector<TransformSequence> seqs_;
    mutable int64_t animTime_ = -1;
    mutable int64_t timeOverride_ = -1;
    const IgObject* skinOverride_ = nullptr;   // buildSkin: igSkin drawn in place of the appearance's _skin
};

// Lights of the lit branch (render list 0x4e78e0: igAttrSet(igLightingStateAttr) -> igLightSet -> igLightStateSets ->
// +0x14 group, where FUN_001099e0 mode-1 models hang) as the Alchemy low-level light module holds them (0x555c70[8],
// FUN_00380520 / FUN_00380670..FUN_00380ba0): only the enabled lights, in slot order. Directions are the vectors the
// light travels along (igLightAttr._direction, world space); FUN_00380850 stores -direction (toward the light).
struct SceneLight {
    int type = 0;                                   // igLightAttr._lightType: 0 directional, 1 point, 2 spot (+0x98)
    std::array<float, 4> ambient{};                 // _ambient  (+0x10 low level)
    std::array<float, 4> diffuse{};                 // _diffuse  (+0x20)
    std::array<float, 4> specular{};                // _specular (+0x30)
    std::array<float, 3> direction{0.0f, 0.0f, -1.0f};   // _direction (+0x40 / -dir at +0x60)
    std::array<float, 3> position{};                // _position (+0x00)
    std::array<float, 3> attenuation{1.0f, 0.0f, 0.0f};  // _attenuation (+0x50)
};
struct LightEnv {
    // 0x555c30 (FUN_00259050). Only the visual-context state reset FUN_00374d10 (vt+0x1b0 = FUN_00381430 with
    // (0, 0, 0, 0)) writes it; no game or attribute code calls vt+0x1b0 otherwise, so it stays 0 in Baroque.
    std::array<float, 4> globalAmbient{0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<SceneLight> lights;
};

// Engine clock (lRam004e78e0, ns) used by the igTransform inputs; installed by the game (ActorAnim::clock).
void setSceneTimeSource(int64_t (*fn)());
int64_t sceneTime();

class SceneRenderer {
public:
    bool init();
    void shutdown();
    void begin(const Mat4& view, const Mat4& proj);
    void draw(const SceneModel& model, const Mat4& modelMatrix, const AttrOverride* ovr = nullptr);
    // Two-pass submission: every model's opaque meshes (blend off) first, then the blended ones.
    // UNVERIFIED: mirrors the Alchemy render list putting blended geometry after opaque; exact sort keys not traced.
    // With an override the opaque/blended split uses the overridden blend state.
    void drawOpaque(const SceneModel& model, const Mat4& modelMatrix, const AttrOverride* ovr = nullptr) {
        drawFiltered(model, modelMatrix, 0, ovr);
    }
    void drawBlended(const SceneModel& model, const Mat4& modelMatrix, const AttrOverride* ovr = nullptr) {
        drawFiltered(model, modelMatrix, 1, ovr);
    }
    void drawPass(const SceneModel& model, const Mat4& modelMatrix, int pass, const AttrOverride* ovr = nullptr) {
        drawFiltered(model, modelMatrix, pass, ovr);
    }
    void end();
    // Lit branch: the lights for the following draws (nullptr = the unlit branch: maps, mode-0 models). The per-vertex
    // lighting is the VU1 microcode's (see drawFiltered / work/reports/MATERIALS_NATIVE.md).
    void setLighting(const LightEnv* env) { lightEnv_ = env; }
    const LightEnv* lighting() const { return lightEnv_; }
    // Fog placeholder hook (igFogAttr: mode/near/far/colour). Linear fog on view depth; off by default.
    void setFog(bool enabled, const float color[4], float nearVal, float farVal);
    // Vertex colour scale, igVisualContext +0x5a0 (set by FUN_003841f0, uploaded to VU1 constant 0x1c by
    // FUN_00249320). Units: the vertex byte (0..255) is multiplied by this vector and the GS treats 0x80 as 1.0.
    // Engine default from FUN_003832e0: RGB 128/255 (DAT_004e5bbc = 0x3f008080), A 1.0 -> vertex 0xFF = 1.0.
    // The game's 2D state (FUN_001020a0) switches to (1,1,1,2) and FUN_001024b0 restores the saved value.
    // UNVERIFIED (VU1 microcode not translated): alpha is modelled as byte/255 * a/1.0, i.e. the VU path halves
    // alpha (consistent with 3D a=1.0 meaning opaque and 2D a=2.0 on GS-unit 0x80 alphas).
    std::array<float, 4> vertexColorScale{128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f};
    // PORT: see-through dither (presentation only). setCut sets the frame's parameters (enabled =
    // false: off). Object mode: a world mesh's strength comes from setCutMeshes (key instance << 32 | mesh index, the
    // instance set by setCutInstance; -1 = not a world draw), other draws use setCutUniform. Silhouette mode: the
    // player mask texture. setCutAllowed(false) exempts the following draws (the player); setMaskMode draws white.
    struct CutParams {
        bool enabled = false;
        bool silhouette = false;
        float silhouetteStrength = 0.0f;    // 0..1 (eased)
        float depth = 0.0f;                 // view depth: only nearer fragments
        float floorY = 0.0f;                // world y: only higher fragments (world meshes, silhouette)
        float dotPx = 1.0f;                 // pattern cell size in pixels
        float nearFade[2] = {0.0f, 0.0f};   // view depth: world geometry nearer than [0] is dropped ([1] unused)
        float chrNearFade[2] = {0.0f, 0.0f};  // view depth: characters fully dithered at [0], untouched from [1]
        unsigned maskTex = 0;               // silhouette: player mask (R = coverage), window sized
        float maskSize[2] = {1.0f, 1.0f};
    };
    void setCut(const CutParams& c) { cut_ = c; }
    void setCutAllowed(bool on) { cutAllowed_ = on; }
    void setCutInstance(int instance) { cutInstance_ = instance; }
    void setCutUniform(float strength) { cutUniform_ = strength; }
    void setCutMeshes(const std::unordered_map<uint64_t, float>* m) { cutMeshes_ = m; }
    void setMaskMode(bool on) { maskMode_ = on; }

private:
    void drawFiltered(const SceneModel& model, const Mat4& modelMatrix, int pass, const AttrOverride* ovr);   // pass -1 all, 0 opaque, 1 blended
    void uploadLighting(const RenderState& st, const Mat4& meshWorld, const Mat4& modelMatrix);   // FUN_00259100 + packers
    unsigned program_ = 0;
    int locMvp_ = -1, locTexMatrix_ = -1, locTexMatrixEnabled_ = -1, locTex_ = -1, locTexEnabled_ = -1;
    int locColorScale_ = -1, locAlphaTest_ = -1, locAlphaFunc_ = -1, locAlphaRef_ = -1;
    int locFogEnabled_ = -1, locFogColor_ = -1, locFogRange_ = -1, locModelView_ = -1;
    int locLit_ = -1, locLitMode_ = -1, locLDir_ = -1, locLCol_ = -1, locLBase_ = -1, locLEmis_ = -1, locLAlpha_ = -1;
    int locPCount_ = -1, locPPos_ = -1, locPAtt_ = -1, locPDif_ = -1, locPAmb_ = -1, locMA_ = -1, locMD_ = -1,
        locME_ = -1, locTrack_ = -1, locASum_ = -1, locPSingle_ = -1;
    int locModel_ = -1;
    int locCutMode_ = -1, locCutStrength_ = -1, locCutDepth_ = -1, locCutFloorY_ = -1, locCutDotPx_ = -1, locCutNear_ = -1,
        locCutMask_ = -1, locCutMaskSize_ = -1, locMaskMode_ = -1, locCutDither_ = -1, locCutChrNear_ = -1, locCutAlpha_ = -1,
        locCutPart_ = -1;
    CutParams cut_;
    bool cutAllowed_ = true;
    bool maskMode_ = false;
    int cutInstance_ = -1;
    float cutUniform_ = 0.0f;
    const std::unordered_map<uint64_t, float>* cutMeshes_ = nullptr;
    const LightEnv* lightEnv_ = nullptr;
    Mat4 view_ = Mat4::identity(), proj_ = Mat4::identity();
    bool fogEnabled_ = false;
    std::array<float, 4> fogColor_{0.0f, 0.0f, 0.0f, 1.0f};
    float fogNear_ = 0.0f, fogFar_ = 1.0f;
};

}  // namespace baroque::alchemy
