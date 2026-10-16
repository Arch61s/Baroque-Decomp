#pragma once
// Runtime world of a loaded floor: room and corridor-piece models (render) and the collision pieces the physics
// sweeps against. Built from a loaded game::Dungeon (generated floors and the fixed town / event / 99 maps).
//   Map-model spawns J024 / J025 / J026 (FUN_00120f70 -> FUN_0011e2b0) are drawn at their spawn position.
//   Render: room scene IGB (FUN_0011e020 path) at the room origin (posX*1600+100, 0, posY*1600+100) (FUN_0011e520);
//           corridor pieces MT%02d_%02d.IGB at the cell centre rotated by PieceInstance::rotY (FUN_00123280);
//           textures of the floor's map set M%02d_TEX.IGB (FUN_0011dfe0).
//   Collision (FUN_00127210 / FUN_00127510): room cells (0x1000) -> room CLS (0x50c050[room]) at the room origin;
//           corridor cells (0x2000) -> MT CLS 0x50ba50[cell & 0xff] at the cell centre, y = 0 / 300, no rotation
//           (the 19 MT collision meshes are pre-oriented). Floors that are not generated (FUN_0011c0a0 == 0) use
//           room collision 0 at the origin.
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"
#include "alchemy/texture.hpp"
#include "data/archive.hpp"
#include "game/collision.hpp"
#include "game/dungeon.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace baroque::game {

class FloorWorld {
public:
    // gpu=false builds collision only (headless checks).
    bool build(const Dungeon& d, const data::Archive& archive, bool gpu, std::string* err);
    CollisionScene& scene() { return scene_; }
    void draw(alchemy::SceneRenderer& r, int pass = -1) const;   // pass 0 opaque, 1 blended, -1 all
    int modelCount() const { return int(instances_.size()); }
    // FUN_001b8440 on the map models: igSwitch nodes named "Toggle" draw only child `child` (0 or 1)
    void setToggle(int child);
    int collisionPieces() const { return int(scene_.pieces.size()); }
    // PORT (free mouse camera occlusion, game/free_camera_view.cpp): every drawn map instance (model, world matrix).
    template <class F> void forEachInstance(F&& f) const {
        for (const Instance& i : instances_) f(*i.model, i.world);
    }
    std::vector<std::string> missing;   // model files that could not be resolved

private:
    struct Loaded {
        std::unique_ptr<alchemy::IgbFile> file;
        std::unique_ptr<alchemy::SceneModel> model;
    };
    struct Instance {
        const alchemy::SceneModel* model;
        alchemy::Mat4 world;
    };
    const alchemy::SceneModel* loadModel(const std::string& dataPath, const data::Archive& archive,
                                         const alchemy::TextureLibrary* extraLib = nullptr);
    struct ExtraTex {                    // texture sets loaded by the J024 / J026 map-model spawns (FUN_00120f70)
        std::unique_ptr<alchemy::IgbFile> file;
        std::unique_ptr<alchemy::TextureLibrary> lib;
    };
    std::map<std::string, ExtraTex> extraTex_;

    std::unique_ptr<alchemy::IgbFile> texFile_;
    std::unique_ptr<alchemy::TextureLibrary> texLib_;
    std::map<std::string, Loaded> models_;
    std::vector<Instance> instances_;
    std::vector<std::unique_ptr<CollisionMesh>> meshes_;
    CollisionScene scene_;
};

}  // namespace baroque::game
