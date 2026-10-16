#pragma once
// Typed, non-owning views over IgObject for the classes the renderer and animation code need.
// Field names/types are the ones recorded in work/reports/igb_classes.json (layouts come from the file).
// Every view is valid only while its IgbFile lives; `ok()` is false when the object is null or of
// another class.
#include "alchemy/igb.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace baroque::alchemy::views {

struct View {
    const IgObject* o = nullptr;
    View() = default;
    explicit View(const IgObject* obj) : o(obj) {}
    bool ok() const { return o != nullptr; }
    const IgbFile& file() const { return *o->file; }
    const IgObject* ref(std::string_view f) const { return o->file->object(o->getRef(f)); }
    const IgMemory* mem(std::string_view f) const { return o->file->memory(o->getRef(f)); }
    std::vector<const IgObject*> list(std::string_view f) const { return o->file->listObjects(ref(f)); }
    std::array<float, 16> matrix(std::string_view f) const {
        std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        o->getFloats(f, m.data(), 16);
        return m;
    }
    std::array<float, 4> vec4(std::string_view f) const {
        std::array<float, 4> v{};
        o->getFloats(f, v.data(), 4);
        return v;
    }
    std::array<float, 3> vec3(std::string_view f) const {
        std::array<float, 3> v{};
        o->getFloats(f, v.data(), 3);
        return v;
    }
};

#define BAROQUE_IGB_VIEW(Name, Class)                                                   \
    struct Name : View {                                                                \
        static constexpr std::string_view kClass = Class;                               \
        Name() = default;                                                               \
        explicit Name(const IgObject* obj) : View(obj != nullptr && obj->isA(Class) ? obj : nullptr) {} \
    }

// --- scene graph ---------------------------------------------------------------------------------
struct Node : View {
    static constexpr std::string_view kClass = "igNode";
    Node() = default;
    explicit Node(const IgObject* obj) : View(obj != nullptr && obj->isA("igNode") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    int flags() const { return o->getInt("_flags"); }
    const IgObject* bound() const { return ref("_bound"); }                 // igAABox
    std::vector<const IgObject*> children() const { return list("_childList"); }  // igGroup+
    std::vector<const IgObject*> attributes() const { return list("_attributes"); } // igAttrSet+
    bool isTransform() const { return o->isA("igTransform"); }
    std::array<float, 16> transformMatrix() const { return matrix("_m"); }  // igTransform._m
    const IgObject* transformGraph() const { return ref("_transformGraph"); }   // igStaticGraph
    bool trigger() const { return o->getBool("_trigger"); }                 // igAttrSet
};
BAROQUE_IGB_VIEW(SceneInfo, "igSceneInfo");
inline const IgObject* sceneGraph(const SceneInfo& s) { return s.ref("_sceneGraph"); }
inline std::vector<const IgObject*> sceneGraphList(const SceneInfo& s) { return s.list("_sceneGraphList"); }
inline std::vector<const IgObject*> sceneTextures(const SceneInfo& s) { return s.list("_textures"); }
inline std::array<float, 3> upVector(const SceneInfo& s) { return s.vec3("_upVector"); }

struct AABox : View {
    explicit AABox(const IgObject* obj) : View(obj != nullptr && obj->isA("igAABox") ? obj : nullptr) {}
    std::array<float, 3> min() const { return vec3("_min"); }
    std::array<float, 3> max() const { return vec3("_max"); }
};

// --- geometry --------------------------------------------------------------------------------------
struct GeometryAttr : View {
    explicit GeometryAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igGeometryAttr") ? obj : nullptr) {}
    const IgObject* vertexArray() const { return ref("_vertexArray"); }
    const IgObject* indexArray() const { return ref("_indexArray"); }  // always null in Baroque
    int primType() const { return o->getInt("_primType"); }            // 4 triangle strip, 3 triangle list
    uint32_t numPrims() const { return o->getUInt("_numPrims"); }
    uint32_t offset() const { return o->getUInt("_offset"); }
    const IgObject* stripLengths() const {                              // igPrimLengthArray1_1
        const IgObject* s = ref("_stripLengths");
        return s != nullptr ? s : ref("_primLengths");
    }
};
struct GeometrySetAttr : View {
    explicit GeometrySetAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igGeometrySetAttr") ? obj : nullptr) {}
    std::vector<const IgObject*> geometryAttrs() const { return list("_geometryAttrList"); }
    bool drawState() const { return o->getBool("_drawState", true); }
};
struct PrimLengthArray : View {
    explicit PrimLengthArray(const IgObject* obj) : View(obj != nullptr && obj->isA("igPrimLengthArray") ? obj : nullptr) {}
    uint32_t numStrips() const { return o->getUInt("_numStrips"); }
    const IgMemory* lengthData() const { return mem("_lengthData"); }  // u32[numStrips]
};
struct VertexArray : View {
    static constexpr int kStreamPosition = 0, kStreamNormal = 1, kStreamColor = 2, kStreamTexCoord0 = 11, kStreamTexCoord1 = 12;
    explicit VertexArray(const IgObject* obj) : View(obj != nullptr && obj->isA("igVertexArray") ? obj : nullptr) {}
    uint32_t numVerts() const { return o->getUInt("_numVerts"); }
    uint32_t vertexFormat() const {                                    // igVertexArray1_1._vertexFormat bit mask
        const Value* v = o->find("_vertexFormat");
        return v != nullptr ? v->u32() : 0;
    }
    const IgMemory* streamTable() const { return mem("_vdata"); }      // 20 igMemoryRef slots
    const IgMemory* stream(int index) const {
        const IgMemory* t = streamTable();
        if (t == nullptr || index < 0 || size_t(index) >= t->refSlots.size()) return nullptr;
        return o->file->memory(t->refSlots[size_t(index)]);
    }
    const IgMemory* blendWeights() const { return mem("_blendWeights"); }  // float[k] per vertex
    const IgMemory* blendIndices() const { return mem("_blendIndices"); }  // u8[k] per vertex
};

// --- material / state attributes ------------------------------------------------------------------
struct MaterialAttr : View {
    explicit MaterialAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igMaterialAttr") ? obj : nullptr) {}
    std::array<float, 4> diffuse() const { return vec4("_diffuse"); }
    std::array<float, 4> ambient() const { return vec4("_ambient"); }
    std::array<float, 4> emission() const { return vec4("_emission"); }
    std::array<float, 4> specular() const { return vec4("_specular"); }
    float shininess() const { return o->getFloat("_shininess"); }
};
struct TextureBindAttr : View {
    explicit TextureBindAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igTextureBindAttr") ? obj : nullptr) {}
    int textureSlot() const { return o->getRef("_texture"); }  // igTextureAttr object or IgExternal slot
    int unitID() const { return o->getInt("_unitID"); }
};
struct TextureAttr : View {
    explicit TextureAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igTextureAttr") ? obj : nullptr) {}
    const IgObject* image() const { return ref("_image"); }
    std::vector<const IgObject*> mipMaps() const { return list("_imageMipMaps"); }
    int magFilter() const { return o->getInt("_magFilter"); }
    int minFilter() const { return o->getInt("_minFilter"); }
    int wrapS() const { return o->getInt("_wrapS"); }  // 0 CLAMP, 1 REPEAT (table at 0x4845c0)
    int wrapT() const { return o->getInt("_wrapT"); }
    int mipmapMode() const { return o->getInt("_mipmapMode"); }
};
struct Image : View {
    explicit Image(const IgObject* obj) : View(obj != nullptr && obj->isA("igImage") ? obj : nullptr) {}
    uint32_t width() const { return o->getUInt("_px"); }
    uint32_t height() const { return o->getUInt("_py"); }
    uint32_t pixelFormat() const { return o->getUInt("_pfmt"); }  // 0x10000 8-bit idx, 0x10001 4-bit idx
    uint32_t bitsIdx() const { return o->getUInt("_bitsIdx"); }
    int bytesPerRow() const { return o->getInt("_bytesPerRow"); }
    const IgMemory* pixels() const { return mem("_pImage"); }
    const IgObject* clut() const { return ref("_pClut"); }
    std::string_view name() const { return o->getString("_pNameString"); }
};
struct Clut : View {
    explicit Clut(const IgObject* obj) : View(obj != nullptr && obj->isA("igClut") ? obj : nullptr) {}
    uint32_t numEntries() const { return o->getUInt("_numEntries"); }
    int stride() const { return o->getInt("_stride", 4); }
    int format() const { return o->getInt("_fmt"); }
    const IgMemory* data() const { return mem("_pData"); }
};
struct EnableAttr : View {  // igBlendStateAttr, igAlphaStateAttr, igDepthWriteStateAttr, igTextureStateAttr,
                            // igVertexBlendStateAttr, igTextureMatrixStateAttr, igFogStateAttr, igLightingStateAttr
    explicit EnableAttr(const IgObject* obj) : View(obj) {}
    bool enabled() const { return o->getBool("_enabled"); }
    int unitID() const { return o->getInt("_unitID"); }
};
struct BlendFunctionAttr : View {
    explicit BlendFunctionAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igBlendFunctionAttr") ? obj : nullptr) {}
    int src() const { return o->getInt("_src"); }
    int dst() const { return o->getInt("_dst"); }
    int eq() const { return o->getInt("_eq"); }
    uint8_t blendConstant() const { return uint8_t(o->getUInt("_blendConstant")); }
};
struct AlphaFunctionAttr : View {
    explicit AlphaFunctionAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igAlphaFunctionAttr") ? obj : nullptr) {}
    int func() const { return o->getInt("_func"); }
    float refValue() const { return o->getFloat("_refValue"); }
};
struct CullFaceAttr : View {
    explicit CullFaceAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igCullFaceAttr") ? obj : nullptr) {}
    bool enabled() const { return o->getBool("_enabled"); }
    int mode() const { return o->getInt("_mode"); }
};
struct TextureMatrixAttr : View {
    explicit TextureMatrixAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igTextureMatrixAttr") ? obj : nullptr) {}
    std::array<float, 16> m() const { return matrix("_m"); }
    int unitID() const { return o->getInt("_unitID"); }
};
struct ShadeModelAttr : View {
    explicit ShadeModelAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igShadeModelAttr") ? obj : nullptr) {}
    int mode() const { return o->getInt("_mode"); }  // 1 = Gouraud (PRIM.IIP)
};
struct ColorAttr : View {
    explicit ColorAttr(const IgObject* obj) : View(obj != nullptr && obj->isA("igColorAttr") ? obj : nullptr) {}
    std::array<float, 4> color() const { return vec4("_color"); }
};

// --- skinning / actors -----------------------------------------------------------------------------
struct SkeletonBoneInfo : View {
    explicit SkeletonBoneInfo(const IgObject* obj) : View(obj != nullptr && obj->isA("igSkeletonBoneInfo") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    int parentIdx() const { return o->getInt("_parentIdx", -1); }
    int bmIdx() const { return o->getInt("_bmIdx", -1); }
    int flags() const { return o->getInt("_flags"); }
};
struct SkeletonV : View {
    explicit SkeletonV(const IgObject* obj) : View(obj != nullptr && obj->isA("igSkeleton") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    std::vector<const IgObject*> boneInfos() const { return list("_boneInfoList"); }
    const IgMemory* boneTranslations() const { return mem("_boneTranslationArray"); }  // vec3f[bones]
    const IgMemory* inverseJoints() const { return mem("_invJointArray"); }            // matrix44f[jointCount]
    int jointCount() const { return o->getInt("_jointCount"); }
};
struct Skin : View {
    explicit Skin(const IgObject* obj) : View(obj != nullptr && obj->isA("igSkin") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    const IgObject* skinnedGraph() const { return ref("_skinnedGraph"); }
};
struct BlendMatrixSelect : View {
    explicit BlendMatrixSelect(const IgObject* obj) : View(obj != nullptr && obj->isA("igBlendMatrixSelect") ? obj : nullptr) {}
    const IgObject* blendMatrixIndices() const { return ref("_blendMatrixIndices"); }  // igIntList
    std::array<float, 16> skeletonTransform() const { return matrix("_skeletonTransform"); }
    std::array<float, 16> skeletonInverseTransform() const { return matrix("_skeletonInverseTransform"); }
};
struct ModelViewMatrixBoneSelect : View {
    explicit ModelViewMatrixBoneSelect(const IgObject* obj) : View(obj != nullptr && obj->isA("igModelViewMatrixBoneSelect") ? obj : nullptr) {}
    int matrixIndex() const { return o->getInt("_matrixIndex", -1); }  // skeleton bone index
};
struct Appearance : View {
    explicit Appearance(const IgObject* obj) : View(obj != nullptr && obj->isA("igAppearance") ? obj : nullptr) {}
    const IgObject* skin() const { return ref("_skin"); }
    std::vector<const IgObject*> attachedSkins() const { return list("_attachedSkins"); }
    std::vector<const IgObject*> attachedGraphs() const { return list("_attachedGraphs"); }
};
struct Actor : View {
    explicit Actor(const IgObject* obj) : View(obj != nullptr && obj->isA("igActor") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    std::array<float, 16> transform() const { return matrix("_transform"); }
    const IgObject* appearance() const { return ref("_appearance"); }
    const IgObject* animationSystem() const { return ref("_animationSystem"); }  // igAnimationCombiner
    const IgObject* animationDatabase() const { return ref("_animationDatabase"); }
    std::vector<const IgObject*> children() const { return list("_childList"); }
};
struct AnimationDatabase : View {
    explicit AnimationDatabase(const IgObject* obj) : View(obj != nullptr && obj->isA("igAnimationDatabase") ? obj : nullptr) {}
    std::vector<const IgObject*> skeletons() const { return list("_skeletonList"); }
    std::vector<const IgObject*> animations() const { return list("_animationList"); }
    std::vector<const IgObject*> skins() const { return list("_skinList"); }
    std::vector<const IgObject*> appearances() const { return list("_appearanceList"); }
    std::vector<const IgObject*> combiners() const { return list("_combinerList"); }
};
struct Animation : View {  // loaded only; playback belongs to the animation module
    explicit Animation(const IgObject* obj) : View(obj != nullptr && obj->isA("igAnimation") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    std::vector<const IgObject*> tracks() const { return list("_trackList"); }      // igAnimationTrack
    std::vector<const IgObject*> bindings() const { return list("_bindingList"); }  // igAnimationBinding
    int64_t startTime() const { return o->getLong("_startTime"); }  // ns
    int64_t duration() const { return o->getLong("_duration"); }    // ns
};
struct AnimationTrack : View {
    explicit AnimationTrack(const IgObject* obj) : View(obj != nullptr && obj->isA("igAnimationTrack") ? obj : nullptr) {}
    std::string_view name() const { return o->getString("_name"); }
    const IgObject* source() const { return ref("_source"); }  // igEnbayaTransformSource
    std::array<float, 4> constantQuaternion() const { return vec4("_constantQuaternion"); }
    std::array<float, 3> constantTranslation() const { return vec3("_constantTranslation"); }
};

#undef BAROQUE_IGB_VIEW

}  // namespace baroque::alchemy::views
