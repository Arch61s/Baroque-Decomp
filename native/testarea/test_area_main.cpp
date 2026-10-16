// Baroque-TestArea: developer playground (port-only). A tiled world with stairs, a ramp, a platform and obstacles,
// the protagonist (C00A) driven by the translated player code, and a dummy enemy (E01A) standing idle.
//
// Translated game code in the loop (30 Hz tick, PS2 float rounding via Ps2FloatScope):
//   pad FUN_0020b860 -> movement FUN_0013cf90 -> ACT tick FUN_00143fe0 -> physics FUN_00142e60 (sweep/probe vs CLS,
//   solid characters via FUN_00140a10, push FUN_00142290) -> camera FUN_0013d970 + FUN_0010da30/FUN_0013e340 ->
//   clock advance + igAnimationCombiner update (FUN_001075a0 / FUN_00108d20). Shapes come from PRMCHAR (FUN_001295e0).
//   Rendering interpolates between ticks (presentation only).
// --dummy ID picks the dummy, --dummy-at X Z places it (port-only, for captures).
// Controls: WASD move (Shift = walk), mouse look, arrows = D-pad (strafe), F1 toggles the mouse cursor,
//   F2 debug overlay, R respawn, Esc quit.
// --selftest runs a scripted walk headless and prints positions.
#include "alchemy/animation.hpp"
#include "alchemy/igb.hpp"
#include "alchemy/ps2_float.hpp"
#include "alchemy/scene_render.hpp"
#include "alchemy/texture.hpp"
#include "data/act.hpp"
#include "data/common.hpp"
#include "data/prm.hpp"
#include "game/act_runner.hpp"
#include "game/actor_anim.hpp"
#include "game/camera.hpp"
#include "data/archive.hpp"
#include "game/collision.hpp"
#include "game/dungeon.hpp"
#include "game/floor_world.hpp"
#include "game/pad.hpp"
#include "game/player_move.hpp"
#include "platform/controls.hpp"   // PORT: setMouseLookCaptured
#include "platform/input.hpp"
#include "platform/platform.hpp"
#include "testarea/test_world.hpp"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque;
using namespace baroque::alchemy;

namespace {

constexpr int64_t kTickNs = enbaya::kFrameNs;  // one 30 Hz tick

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------------------------
// A character: model parts + skeleton + clips + ACT + physics + transform
// ---------------------------------------------------------------------------------------------
struct ModelPart {
    IgbFile file;
    std::unique_ptr<TextureLibrary> lib;
    std::unique_ptr<SceneModel> model;
    std::string attachBone;  // non-empty: placed at this bone (HEAD part)
};

struct Character {
    std::vector<std::unique_ptr<ModelPart>> parts;
    std::vector<std::unique_ptr<AnimClip>> clips;
    data::ActFile act;
    game::ActorAnim anim;
    game::ActRunner runner;
    game::ChrTransform xf;
    game::Phys phys;
    game::Shape shape;
    std::string id;              // chr +0x1c
    int type = 0;                // chr +0x18: 0 player, 1 NPC, 2 enemy
    uint32_t* cell = nullptr;    // chr +0x30 (FUN_00124c70), +0x34 / +0x38 cell coordinates
    int cellX = 0, cellY = 0;
    uint32_t statusMask = 0;
    game::Vec3f prevPos{};
    std::vector<Mat4> pose;
    Skeleton cpuSkeleton;        // headless: bones only (combiner bone lists)
    bool gpu = false;

    bool load(const fs::path& root, const std::string& chrId, const std::vector<std::string>& extraParts, bool gpuUpload) {
        gpu = gpuUpload;
        id = chrId;
        std::vector<std::pair<std::string, std::string>> files{{"DATA/CHR/" + id.substr(0, 3) + "/" + id + ".IGB", ""}};
        for (const std::string& p : extraParts)
            files.push_back({"DATA/CHR/" + id.substr(0, 3) + "/" + id + "_" + p + ".IGB", p == "HEAD" ? "Head" : ""});
        for (const auto& [path, bone] : files) {
            auto part = std::make_unique<ModelPart>();
            std::string err;
            if (!part->file.load(root / path, &err)) {
                std::printf("load %s: %s\n", path.c_str(), err.c_str());
                return false;
            }
            part->attachBone = bone;
            if (gpu) {
                part->lib = std::make_unique<TextureLibrary>();
                part->lib->addFile(part->file, nullptr);
                part->model = std::make_unique<SceneModel>();
                part->model->build(part->file, {part->lib.get()}, nullptr);
            }
            parts.push_back(std::move(part));
        }
        std::vector<std::string> log;
        clips = loadClips(parts[0]->file, primarySkeleton(parts[0]->file), &log);
        if (!gpu) cpuSkeleton = loadSkeletonBones(parts[0]->file, primarySkeleton(parts[0]->file));
        for (const auto& l : log) std::printf("  note: %s\n", l.c_str());
        std::string err;
        auto actFile = data::parseAct(readFile(root / ("DATA/ACTION/" + id + ".ACT")), &err);
        if (!actFile) {
            std::printf("ACT %s: %s\n", id.c_str(), err.c_str());
            return false;
        }
        act = std::move(*actFile);
        return true;
    }

    const Skeleton* skeleton() const { return gpu ? &parts[0]->model->skeleton() : &cpuSkeleton; }
};

Mat4 chrMatrix(const game::ChrTransform& t, const game::Vec3f& pos) {
    Mat4 m = Mat4::identity();
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = t.right[size_t(k)];
        m.m[size_t(4 + k)] = t.up[size_t(k)];
        m.m[size_t(8 + k)] = t.fwd[size_t(k)];
        m.m[size_t(12 + k)] = pos[size_t(k)];
    }
    return m;
}

// ---------------------------------------------------------------------------------------------
// World rendering (prototype grid)
// ---------------------------------------------------------------------------------------------
Texture2D makeGridTexture() {
    const int N = 256;
    Image img = GenImageColor(N, N, Color{200, 200, 200, 255});
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool major = x < 3 || y < 3 || x >= N - 3 || y >= N - 3;
            const bool minor = (x % 64) < 1 || (y % 64) < 1;
            const unsigned char v = major ? 255 : minor ? 225 : ((x / 128 + y / 128) % 2 ? 176 : 188);
            ImageDrawPixel(&img, x, y, Color{v, v, v, 255});
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

::Mesh makeWorldMesh(const testarea::TestWorld& w) {
    static const Color kMat[5] = {{58, 64, 78, 255}, {92, 84, 104, 255}, {214, 120, 52, 255}, {70, 110, 128, 255}, {150, 70, 90, 255}};
    ::Mesh m{};
    const int n = int(w.tris.size()) * 3;
    m.vertexCount = n;
    m.triangleCount = int(w.tris.size());
    m.vertices = static_cast<float*>(MemAlloc(sizeof(float) * 3 * size_t(n)));
    m.normals = static_cast<float*>(MemAlloc(sizeof(float) * 3 * size_t(n)));
    m.texcoords = static_cast<float*>(MemAlloc(sizeof(float) * 2 * size_t(n)));
    m.colors = static_cast<unsigned char*>(MemAlloc(4 * size_t(n)));
    const Vector3 light = Vector3Normalize({0.45f, 0.8f, -0.35f});
    int v = 0;
    for (const auto& t : w.tris) {
        const Vector3 a{t.v[0][0], t.v[0][1], t.v[0][2]}, b{t.v[1][0], t.v[1][1], t.v[1][2]}, c{t.v[2][0], t.v[2][1], t.v[2][2]};
        const Vector3 nrm = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        const float shade = 0.55f + 0.45f * std::fmax(0.0f, Vector3DotProduct(nrm, light));
        const Color base = kMat[t.material < 0 ? 0 : t.material % 5];
        for (int k = 0; k < 3; ++k, ++v) {
            const auto& p = t.v[size_t(k)];
            m.vertices[v * 3 + 0] = p[0];
            m.vertices[v * 3 + 1] = p[1];
            m.vertices[v * 3 + 2] = p[2];
            m.normals[v * 3 + 0] = nrm.x;
            m.normals[v * 3 + 1] = nrm.y;
            m.normals[v * 3 + 2] = nrm.z;
            float u, vv;
            if (std::fabs(nrm.y) > 0.7f) { u = p[0] / 200.0f; vv = p[2] / 200.0f; }
            else if (std::fabs(nrm.x) > std::fabs(nrm.z)) { u = p[2] / 200.0f; vv = p[1] / 200.0f; }
            else { u = p[0] / 200.0f; vv = p[1] / 200.0f; }
            m.texcoords[v * 2 + 0] = u;
            m.texcoords[v * 2 + 1] = vv;
            m.colors[v * 4 + 0] = (unsigned char)(base.r * shade);
            m.colors[v * 4 + 1] = (unsigned char)(base.g * shade);
            m.colors[v * 4 + 2] = (unsigned char)(base.b * shade);
            m.colors[v * 4 + 3] = 255;
        }
    }
    UploadMesh(&m, false);
    return m;
}

// ---------------------------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------------------------
struct Game {
    testarea::TestWorld world;
    game::CollisionMesh mesh;
    game::CollisionScene scene;
    Character player, dummy;
    game::PlayerControl ctl;
    game::Rng rng;
    game::CameraWorld camWorld;
    game::EngineProjection proj;
    std::unique_ptr<game::Camera> cam;
    float prevEye[3]{}, prevTarget[3]{};
    float prevCam[4][4]{};
    int tick = 0;
    bool walkModifier = false;
    bool debugNoBlend = false;   // --no-blend: skip the world's blended pass (render debugging)
    std::string dummyId = "E02A";
    bool dummyAtSet = false;     // --dummy-at X Z (port-only placement for captures)
    float dummyAtX = 0.0f, dummyAtZ = 0.0f;
    int floorId = -1;          // --floor: real dungeon floor instead of the tiled world
    uint32_t floorSeed = 1;
    std::unique_ptr<data::Archive> archive;
    std::unique_ptr<game::Rng> genRng;
    std::unique_ptr<game::Dungeon> dungeon;
    std::unique_ptr<game::FloorWorld> floorWorld;
    game::Vec3f spawnPos{};
    float spawnAngle = 0.0f;
    data::PrmCharFile prm;       // DATA/PARAM/PRMCHAR.PRM

    // Physics and collision shape from PRMCHAR as FUN_001295e0 / FUN_00142e10 / FUN_00142200 / FUN_00142230 set them
    // (same rules as game/character/character.cpp chrInitById).
    void initChar(Character& c, const game::Vec3f& pos, bool isPlayer, const char* startAction) {
        c.xf = game::ChrTransform{};
        c.xf.pos = pos;
        c.prevPos = pos;
        c.type = isPlayer ? 0 : (c.id[0] == 'E' ? 2 : 1);
        uint32_t idw = 0;
        std::memcpy(&idw, c.id.data(), 4);
        const data::PrmChar* p = data::findPrmChar(prm, idw);
        const float radius = p != nullptr ? p->radius : 0.0f;                     // PRMCHAR +0x98
        game::physInit(c.phys, &c.xf.pos, &c.shape, radius);
        c.phys.ownerForward = c.xf.fwd.data();
        if (p != nullptr) {
            c.shape.owner = &c;
            if (p->f9c == 0.0f || p->fa4 < 0.0f) {   // FUN_00142200: cylinder
                c.shape.radius = p->f9c;
                c.shape.height = p->height;
                c.shape.a = -1.0f;
                c.shape.b = -1.0f;
            } else {                                  // FUN_00142230: box half extents (f9c, fa4)
                c.shape.height = p->height;
                c.shape.a = p->f9c;
                c.shape.b = p->fa4;
                c.shape.radius = std::sqrt(p->f9c * p->f9c + p->fa4 * p->fa4);
            }
            c.shape.top = c.shape.height + c.xf.pos[1];                           // FUN_00142270
        }
        // UNVERIFIED (port): character.cpp also zeroes gravity for its gravityFree() id list; not needed for the
        // ids used here.
        if (radius == 0.0f) c.phys.gravity = 0.0f;
        refreshCell(c);
        c.anim.init(&c.clips, c.skeleton());
        c.runner.init(&c.act, &c.anim, &c.phys, isPlayer ? &ctl : nullptr, &c.statusMask);
        if (!c.runner.request(startAction) && !c.clips.empty()) c.anim.set(0);
    }

    void respawn() {
        initChar(player, spawnPos, true, "WT01_H1");
        if (spawnAngle != 0.0f) game::rotateAboutUp(player.xf, spawnAngle);   // UNVERIFIED: start angle application
        cam->applyPreset(game::kCameraPresetDungeon);
        refreshCamWorld();
        cam->init();
    }

    // orig FUN_00124c70 via FUN_00129c60: grid cell under the character (dungeon floors only).
    void refreshCell(Character& c) {
        c.cell = dungeon != nullptr ? dungeon->cellAtWorld(c.xf.pos.data(), &c.cellX, &c.cellY) : nullptr;
    }

    // List 0x50a7b0 as FUN_00140250 / FUN_00142290 read it: the player and the dummy.
    void listCharacters(std::vector<game::CollBody>& out) {
        for (Character* c : {&player, &dummy}) {
            game::CollBody b;
            b.owner = c;
            b.phys = &c->phys;
            b.pos = &c->xf.pos;
            b.right = &c->xf.right;
            b.fwd = &c->xf.fwd;
            b.id0 = c->id[0];
            b.active = true;
            b.removed = false;
            // type 2 "E19*" with FUN_001f1730(ai, 0x1000008) == 0 (the dummy has no AI: flags 0), type 1 != "C18A"
            b.solid = (c->type == 2 && c->id.compare(0, 3, "E19") == 0) || (c->type == 1 && c->id != "C18A");
            b.cell = c->cell;
            b.cellX = c->cellX;
            b.cellY = c->cellY;
            out.push_back(b);
        }
    }

    void hookScene(game::CollisionScene& sc) {
        sc.playerPos = &player.xf.pos;
        sc.characters = [this](std::vector<game::CollBody>& out) { listCharacters(out); };
        sc.characterPush = [](game::Phys& p) { return game::characterPush(p); };   // FUN_00142290 from FUN_001416b0
        if (dungeon != nullptr)
            sc.cellAt = [this](const game::Vec3f& pos) -> const uint32_t* {
                int cx = 0, cy = 0;
                return dungeon->cellAtWorld(pos.data(), &cx, &cy);
            };
        game::setCollisionScene(&sc);
    }

    void refreshCamWorld() {
        for (int k = 0; k < 3; ++k) {
            camWorld.playerPos[k] = player.xf.pos[size_t(k)];
            camWorld.playerForward[k] = player.xf.fwd[size_t(k)];
        }
        camWorld.controlFlags = ctl.flags;
        camWorld.controlRotLock = int32_t(ctl.lock);
    }

    bool init(const fs::path& root, bool gpu) {
        world = testarea::buildTestWorld();
        spawnPos = world.spawn;
        if (floorId < 0) {
            mesh = game::CollisionMesh::fromCls(world.cls);
            scene.pieces.push_back(game::CollisionPiece{&mesh, &mesh, {0, 0, 0}, false});
            hookScene(scene);
        } else {
            // real floor: FUN_0011f960 load -> FUN_0011fab0 layout -> FUN_0011fb30 collision -> placement
            archive = std::make_unique<data::Archive>(root);
            archive->setPack(data::Archive::packNameForFloor(floorId));
            genRng = std::make_unique<game::Rng>();
            genRng->seed(floorSeed);
            dungeon = std::make_unique<game::Dungeon>(*archive, *genRng);
            std::string err;
            if (!dungeon->load(floorId, 0, &err)) { std::printf("floor %d: %s\n", floorId, err.c_str()); return false; }
            dungeon->generateLayout();
            if (!dungeon->loadCollision(&err)) { std::printf("floor %d collision: %s\n", floorId, err.c_str()); return false; }
            dungeon->placeContents();
            floorWorld = std::make_unique<game::FloorWorld>();
            if (!floorWorld->build(*dungeon, *archive, gpu, &err)) return false;
            for (const auto& m : floorWorld->missing) std::printf("  missing: %s\n", m.c_str());
            std::printf("[floor] %d seed %u: %d rooms, %zu pieces, %d models, %d collision pieces, player (%.0f %.0f %.0f)\n", floorId,
                        floorSeed, dungeon->roomCount(), dungeon->pieces().size(), floorWorld->modelCount(),
                        floorWorld->collisionPieces(), dungeon->player().x, dungeon->player().y, dungeon->player().z);
            hookScene(floorWorld->scene());
            if (dungeon->player().valid) {
                spawnPos = {dungeon->player().x, dungeon->player().y, dungeon->player().z};
                spawnAngle = dungeon->player().angleRad;
            }
            world.dummyPos = {spawnPos[0] + 150.0f, spawnPos[1], spawnPos[2] + 250.0f};
        }
        {
            std::string err;
            auto pf = data::parsePrmChar(readFile(root / "DATA/PARAM/PRMCHAR.PRM"), &err);
            if (!pf) {
                std::printf("PRMCHAR: %s\n", err.c_str());
                return false;
            }
            prm = std::move(*pf);
        }
        if (!player.load(root, "C00A", {"BODY", "HAIR", "HEAD"}, gpu)) return false;
        if (!dummy.load(root, dummyId, {}, gpu)) return false;
        if (gpu) {
            const Skeleton& sk = player.parts[0]->model->skeleton();
            player.parts[1]->model->adoptSkeleton(sk);
            player.parts[2]->model->adoptSkeleton(sk);
        }
        rng.seed(0x12345678u);
        game::setProjection(proj, game::kProjectionDungeon.farZ, game::kProjectionDungeon.fovDeg);
        game::CameraHooks hooks;
        hooks.raycast = [](const float from[3], const float delta[3], float radius, float* dist) {
            return game::raycast(from, delta, radius, dist);
        };
        hooks.rotatePlayer = [this](float rad) {
            game::rotateAboutUp(player.xf, rad);
            for (int k = 0; k < 3; ++k) camWorld.playerForward[k] = player.xf.fwd[size_t(k)];
        };
        hooks.requestScreenTransition = [] {};
        hooks.setLightDirection = [](const float*) {};
        hooks.markPlayerModelOnReset = [] {};
        cam = std::make_unique<game::Camera>(camWorld, proj, hooks);
        respawn();
        if (dummyAtSet) world.dummyPos = {dummyAtX, spawnPos[1], dummyAtZ};
        initChar(dummy, world.dummyPos, false, "WT01");
        game::snapForward(dummy.xf, {0.0f, 0.0f, -1.0f});
        std::memcpy(prevEye, cam->eye, sizeof prevEye);
        std::memcpy(prevTarget, cam->target, sizeof prevTarget);
        std::memcpy(prevCam, cam->camWorld, sizeof prevCam);
        return true;
    }

    // Replace the dummy by another character id (selftest).
    bool reloadDummy(const fs::path& root, const std::string& id, const game::Vec3f& pos) {
        dummy.parts.clear();
        dummy.clips.clear();
        if (!dummy.load(root, id, {}, false)) return false;
        initChar(dummy, pos, false, "WT01");
        game::snapForward(dummy.xf, {0.0f, 0.0f, -1.0f});
        return true;
    }

    // One 30 Hz game tick (PLAYER.md §2 order for the player; camera task afterwards).
    void step(const game::PadPort& port) {
        alchemy::ps2::Ps2FloatScope fp;
        game::ActorAnim::setTickId(tick);
        player.prevPos = player.xf.pos;
        dummy.prevPos = dummy.xf.pos;
        std::memcpy(prevEye, cam->eye, sizeof prevEye);
        std::memcpy(prevTarget, cam->target, sizeof prevTarget);
        std::memcpy(prevCam, cam->camWorld, sizeof prevCam);

        game::MoveInputs in;
        in.stickX = port.lstick[0];
        in.stickY = port.lstick[1];
        in.stickMag = port.lstick[2];
        if (walkModifier && in.stickMag > 0.0f) in.stickMag = 0.5f;  // port-only keyboard walk (analog half tilt)
        in.held = port.held;
        in.camFwd = {cam->forward[0], cam->forward[1], cam->forward[2]};
        in.camRight = {cam->right[0], cam->right[1], cam->right[2]};
        in.statusMask = player.statusMask;
        in.hidden0 = 0;
        in.hp = 100;
        in.maxHp = 100;
        in.currentAction = player.runner.currentName();
        in.weaponClass = -1;
        game::MoveOutputs out;
        out.requestAction = [this](const std::string& n) { player.runner.request(n); };
        ctl.flags &= ~0x10u;
        game::playerMovement(ctl, player.xf, player.phys, in, out, rng, false);
        for (Character* c : {&player, &dummy}) {   // FUN_00129c60 order: physics, cell, ACT, shape top
            game::physTick(c->phys);
            refreshCell(*c);
            c->runner.tick();
            c->shape.top = c->shape.height + c->xf.pos[1];   // FUN_00142270
        }
        refreshCamWorld();
        cam->inputFromPad(port);
        cam->applyMouseInput();
        cam->tick();
        game::ActorAnim::advanceClock(kTickNs);
        // FUN_001075a0 -> FUN_00108d20: the 3-D pass updates every actor's igAnimationCombiner at the new clock.
        player.anim.update();
        dummy.anim.update();
        ++tick;
    }
};

int selftest(const fs::path& root) {
    Game g;
    if (!g.init(root, false)) return 1;
    std::printf("[selftest] world: %zu render tris, %u collision faces, grid %ux%u\n", g.world.tris.size(),
                g.world.cls.faceCount, g.world.cls.nx, g.world.cls.nz);
    auto run = [&](const char* label, int ticks, int lx, int ly) {
        for (int i = 0; i < ticks; ++i) {
            platform::PadSample s;
            s.lx = lx;
            s.ly = ly;
            game::pad().update(s);
            g.step(game::pad().port[0]);
        }
        const auto& p = g.player.xf.pos;
        std::printf("[selftest] %-28s pos (%8.2f %8.2f %8.2f) fwd (%5.2f %5.2f) act %-8s air %u res 0x%02x\n", label, p[0], p[1], p[2],
                    g.player.xf.fwd[0], g.player.xf.fwd[2], g.player.runner.currentName() ? g.player.runner.currentName() : "-",
                    g.player.phys.flags & 1, g.player.phys.result);
    };
    run("idle", 10, 128, 128);
    run("run forward (stick up)", 30, 128, 0);
    run("run right", 30, 255, 128);
    run("run to pillar/wall", 90, 0, 128);
    run("idle", 5, 128, 128);
    g.respawn();
    // head for the stair ramp foot (x 700, z -320) then climb it toward +z
    for (int i = 0; i < 200; ++i) {
        const float dx = 700.0f - g.player.xf.pos[0], dz = -380.0f - g.player.xf.pos[2];
        if (std::sqrt(dx * dx + dz * dz) < 20.0f) break;
        // drive the stick in camera space toward the target
        const float* f = g.cam->forward;
        const float* r = g.cam->right;
        const float sy = dx * f[0] + dz * f[2], sx = dx * r[0] + dz * r[2];  // mv = fwd*sy + right*sx
        const float l = std::sqrt(sx * sx + sy * sy);
        platform::PadSample s;
        s.lx = 128 + int(127.0f * sx / l);
        s.ly = 128 + int(127.0f * sy / l);
        game::pad().update(s);
        g.step(game::pad().port[0]);
    }
    run("at stair foot", 1, 128, 128);
    for (int i = 0; i < 120; ++i) {
        const float* f = g.cam->forward;
        const float* r = g.cam->right;
        const float sy = f[2], sx = r[2];
        const float l = std::sqrt(sx * sx + sy * sy);
        platform::PadSample s;
        s.lx = 128 + int(127.0f * sx / l);
        s.ly = 128 + int(127.0f * sy / l);
        game::pad().update(s);
        g.step(game::pad().port[0]);
        if (i % 20 == 19) run("climbing +z", 0, 128, 128);
    }

    // ---- character vs character: FUN_00140250 tail (FUN_00140a10 -> FUN_00140ad0 / FUN_00140f80) and the push
    // FUN_00142290. The player runs straight at a dummy 300 units ahead; the contact distance follows from PRMCHAR:
    // E02A (enemy, not solid): push, shape radii 30 + 65 = 95; E19A (solid enemy box, half extents 95 x 30 across x /
    // along z): swept stop at its face, 30 + 30 = 60 from the centre; C02A (solid NPC cylinder r 30): 30 + 30 = 60.
    struct Case { const char* id; float contact; };
    bool collOk = true;
    for (const Case& cs : {Case{"E02A", 95.0f}, Case{"E19A", 60.0f}, Case{"C02A", 60.0f}}) {
        g.respawn();
        const game::Vec3f dpos{g.player.xf.pos[0], 0.0f, g.player.xf.pos[2] + 300.0f};
        if (!g.reloadDummy(root, cs.id, dpos)) {
            collOk = false;
            continue;
        }
        float minDist = 1e9f;
        uint32_t resOr = 0;
        for (int i = 0; i < 75; ++i) {
            const float dx = g.dummy.xf.pos[0] - g.player.xf.pos[0], dz = g.dummy.xf.pos[2] - g.player.xf.pos[2];
            const float* f = g.cam->forward;
            const float* r = g.cam->right;
            const float sy = dx * f[0] + dz * f[2], sx = dx * r[0] + dz * r[2];
            const float l = std::sqrt(sx * sx + sy * sy);
            platform::PadSample s;
            s.lx = l > 0.0f ? 128 + int(127.0f * sx / l) : 128;
            s.ly = l > 0.0f ? 128 + int(127.0f * sy / l) : 128;
            game::pad().update(s);
            g.step(game::pad().port[0]);
            const float ex = g.dummy.xf.pos[0] - g.player.xf.pos[0], ez = g.dummy.xf.pos[2] - g.player.xf.pos[2];
            minDist = std::min(minDist, std::sqrt(ex * ex + ez * ez));
            resOr |= g.player.phys.result;
        }
        const float ex = g.dummy.xf.pos[0] - g.player.xf.pos[0], ez = g.dummy.xf.pos[2] - g.player.xf.pos[2];
        const float dist = std::sqrt(ex * ex + ez * ez);
        const float moved = std::sqrt((g.dummy.xf.pos[0] - dpos[0]) * (g.dummy.xf.pos[0] - dpos[0]) +
                                      (g.dummy.xf.pos[2] - dpos[2]) * (g.dummy.xf.pos[2] - dpos[2]));
        // stopped at contact: never closer than the contact distance (float tolerance), and actually touching
        const bool pass = minDist >= cs.contact - 0.05f && dist <= cs.contact + 1.0f;
        collOk = collOk && pass;
        std::printf("[selftest] chr-collision %s: player (%7.2f %7.2f) dummy (%7.2f %7.2f) dist %.3f min %.3f "
                    "contact %.0f dummy moved %.3f result|=0x%02x %s\n",
                    cs.id, g.player.xf.pos[0], g.player.xf.pos[2], g.dummy.xf.pos[0], g.dummy.xf.pos[2], dist, minDist,
                    cs.contact, moved, resOr, pass ? "PASS" : "FAIL");
    }

    // ---- animation blend (igAnimationCombiner via FUN_0010a8e0): the player's states over the last run
    {
        const auto& st = g.player.anim.combiner().states();
        std::printf("[selftest] player combiner: %zu state(s)", st.size());
        for (const auto& s : st)
            std::printf("  [%s mode %d status %d w %.4f t %lld]", g.player.clips[size_t(s->clip)]->name.c_str(), s->combineMode,
                        s->status, double(s->blendRatio), static_cast<long long>(s->localTime));
        std::printf("\n");
    }
    std::printf("[selftest] character collision: %s\n", collOk ? "PASS" : "FAIL");
    return collOk ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path root = platform::assetRoot();
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest(root);

    // --shot OUT.png [ticks] [lx ly]: scripted stick input for `ticks` ticks, save a frame, quit (verification).
    const char* shotPath = nullptr;
    int shotTicks = 60, shotLx = 128, shotLy = 0;
    if (argc > 2 && std::strcmp(argv[1], "--shot") == 0) {
        shotPath = argv[2];
        if (argc > 3) shotTicks = std::atoi(argv[3]);
        if (argc > 5 && argv[4][0] != '-') { shotLx = std::atoi(argv[4]); shotLy = std::atoi(argv[5]); }
    }
    // --shot runs with a hidden window so scripted verification never pops a window on the desktop
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT |
                   (shotPath != nullptr ? FLAG_WINDOW_HIDDEN : 0u));
    InitWindow(1600, 900, "Baroque - test area");
    SetExitKey(KEY_ESCAPE);
    SetTraceLogLevel(LOG_WARNING);
    Game g;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--dummy") == 0) g.dummyId = argv[i + 1];
        if (std::strcmp(argv[i], "--floor") == 0) g.floorId = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--seed") == 0) g.floorSeed = uint32_t(std::atol(argv[i + 1]));
        if (std::strcmp(argv[i], "--no-blend") == 0) g.debugNoBlend = true;
        if (std::strcmp(argv[i], "--dummy-at") == 0 && i + 2 < argc) {
            g.dummyAtSet = true;
            g.dummyAtX = float(std::atof(argv[i + 1]));
            g.dummyAtZ = float(std::atof(argv[i + 2]));
        }
    }
    if (!g.init(root, true)) {
        CloseWindow();
        return 1;
    }
    SceneRenderer renderer;
    renderer.init();
    Texture2D grid = makeGridTexture();
    ::Mesh worldMesh = makeWorldMesh(g.world);
    Material mat = LoadMaterialDefault();
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = grid;
    if (shotPath == nullptr) { DisableCursor(); platform::setMouseLookCaptured(true); }
    bool cursorLocked = shotPath == nullptr, overlay = true;
    double acc = 0.0;
    auto last = std::chrono::steady_clock::now();
    const double tickSec = double(kTickNs) / 1e9;
    while (!WindowShouldClose()) {
        const auto now = std::chrono::steady_clock::now();
        acc += std::chrono::duration<double>(now - last).count();
        last = now;
        if (acc > 0.25) acc = 0.25;
        if (IsKeyPressed(KEY_F1)) {
            cursorLocked = !cursorLocked;
            if (cursorLocked) DisableCursor(); else EnableCursor();
            platform::setMouseLookCaptured(cursorLocked);   // PORT: mouse buttons are game inputs only while captured
        }
        if (IsKeyPressed(KEY_F2)) overlay = !overlay;
        if (IsKeyPressed(KEY_R)) g.respawn();
        if (cursorLocked) {
            const Vector2 md = GetMouseDelta();
            // port-only free mouse look
            g.cam->addMouseDelta(md.x * 0.15f, md.y * 0.12f);  // degrees per pixel
        }
        g.walkModifier = IsKeyDown(KEY_LEFT_SHIFT);
        if (shotPath != nullptr) {
            if (g.tick < shotTicks) {
                platform::PadSample s;
                s.lx = shotLx;
                s.ly = shotLy;
                game::pad().update(s);
                g.step(game::pad().port[0]);
            }
            acc = 0.0;
        }
        while (shotPath == nullptr && acc >= tickSec) {
            acc -= tickSec;
            game::pad().update(platform::pollInput());  // sampled at the end of the previous tick (RUNTIME.md)
            g.step(game::pad().port[0]);
        }
        const float a = float(acc / tickSec);

        // presentation: interpolate positions and camera between ticks
        float eye[3], target[3];
        for (int k = 0; k < 3; ++k) {
            eye[k] = g.prevEye[k] + (g.cam->eye[k] - g.prevEye[k]) * a;
            target[k] = g.prevTarget[k] + (g.cam->target[k] - g.prevTarget[k]) * a;
        }
        const float up[3] = {0.0f, 1.0f, 0.0f};
        // FUN_0039e800: the 48 degree FOV is horizontal at 4:3; keep that vertical FOV for any aspect.
        const float fovY = 2.0f * std::atan(std::tan(g.proj.fovDeg * 0.5f * DEG2RAD) / (4.0f / 3.0f)) * RAD2DEG;
        // The game's view is inverse(camera +0x18) (FUN_0010da30): eye level with the target, pitch in the basis.
        // Presentation-only interpolation of that matrix between ticks (rows lerped and renormalised).
        Mat4 camM = Mat4::identity();
        for (int r = 0; r < 4; ++r) {
            float row[3];
            for (int k = 0; k < 3; ++k) row[k] = g.prevCam[r][k] + (g.cam->camWorld[r][k] - g.prevCam[r][k]) * a;
            if (r < 3) {
                const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
                if (l > 0.0f)
                    for (float& x : row) x /= l;
            }
            for (int k = 0; k < 3; ++k) camM.m[size_t(r * 4 + k)] = row[k];
        }
        Mat4 view;
        if (!invert(camM, view)) view = Mat4::lookAt(eye, target, up);

        std::vector<Mat4> pose;
        g.player.anim.pose(pose);
        const Skeleton& sk = g.player.parts[0]->model->skeleton();
        for (int i = 0; i < 3; ++i) g.player.parts[size_t(i)]->model->setPose(pose);
        Mat4 headLocal = Mat4::identity();
        for (size_t b = 0; b < sk.bones.size(); ++b)
            if (sk.bones[b].name == "Head" && b < pose.size()) headLocal = pose[b];
        std::vector<Mat4> dpose;
        g.dummy.anim.pose(dpose);
        if (!dpose.empty()) g.dummy.parts[0]->model->setPose(dpose);

        game::Vec3f ppos, dpos;
        for (int k = 0; k < 3; ++k) {
            ppos[size_t(k)] = g.player.prevPos[size_t(k)] + (g.player.xf.pos[size_t(k)] - g.player.prevPos[size_t(k)]) * a;
            dpos[size_t(k)] = g.dummy.xf.pos[size_t(k)];
        }
        const Mat4 pm = chrMatrix(g.player.xf, ppos);
        const Mat4 dm = chrMatrix(g.dummy.xf, dpos);

        auto draw3D = [&](int vw, int vh) {
            const Mat4 projM = Mat4::perspective(fovY, float(vw) / float(vh), g.proj.nearZ, 20000.0f);
            rlDrawRenderBatchActive();
            rlViewport(0, 0, vw, vh);
            const Matrix savedProj = rlGetMatrixProjection(), savedView = rlGetMatrixModelview();
            rlSetMatrixProjection(toRaylib(projM));
            rlSetMatrixModelview(toRaylib(view));
            rlEnableDepthTest();
            if (g.floorWorld == nullptr) DrawMesh(worldMesh, mat, MatrixIdentity());
            rlDrawRenderBatchActive();
            renderer.begin(view, projM);
            for (int pass = 0; pass < 2; ++pass) {
                if (g.floorWorld != nullptr && !(pass == 1 && g.debugNoBlend)) g.floorWorld->draw(renderer, pass);
                auto submit = [&](const SceneModel& m, const Mat4& w) { pass == 0 ? renderer.drawOpaque(m, w) : renderer.drawBlended(m, w); };
                for (int i = 0; i < 3; ++i) submit(*g.player.parts[size_t(i)]->model, pm);
                submit(*g.player.parts[3]->model, mul(headLocal, pm));
                submit(*g.dummy.parts[0]->model, dm);
            }
            renderer.end();
            rlDrawRenderBatchActive();
            rlSetMatrixProjection(savedProj);
            rlSetMatrixModelview(savedView);
            rlDisableDepthTest();
        };
        auto drawOverlay = [&]() {
            if (!overlay) return;
            const auto& p = g.player.xf.pos;
            DrawRectangle(10, 10, 640, 118, Color{0, 0, 0, 150});
            DrawText("BAROQUE native - test area (dev)", 20, 18, 20, Color{255, 170, 90, 255});
            DrawText(TextFormat("pos %.1f %.1f %.1f   action %s   clip %s", p[0], p[1], p[2],
                                g.player.runner.currentName() ? g.player.runner.currentName() : "-",
                                g.player.anim.currentClip() >= 0 ? g.player.clips[size_t(g.player.anim.currentClip())]->name.c_str() : "-"),
                     20, 46, 18, RAYWHITE);
            const float bx = g.cam->eye[0] - g.cam->target[0], by = g.cam->eye[1] - g.cam->target[1], bz = g.cam->eye[2] - g.cam->target[2];
            DrawText(TextFormat("phys flags 0x%x  result 0x%02x  airborne %d   tick %d   %d fps   boom %.0f  eye %.0f %.0f %.0f", g.player.phys.flags,
                                g.player.phys.result, g.player.phys.flags & 1, g.tick, GetFPS(), std::sqrt(bx * bx + by * by + bz * bz),
                                g.cam->eye[0], g.cam->eye[1], g.cam->eye[2]),
                     20, 70, 18, RAYWHITE);
            DrawText("WASD move  Shift walk  Mouse look  Arrows strafe  R respawn  F1 cursor  F2 overlay", 20, 100, 16,
                     Color{180, 180, 190, 255});
        };
        if (shotPath != nullptr && g.tick >= shotTicks) {
            RenderTexture2D rt = LoadRenderTexture(1920, 1080);
            BeginTextureMode(rt);
            ClearBackground(Color{18, 20, 28, 255});
            draw3D(1920, 1080);
            drawOverlay();
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ExportImage(img, shotPath);
            UnloadImage(img);
            UnloadRenderTexture(rt);
            break;
        }
        BeginDrawing();
        ClearBackground(Color{18, 20, 28, 255});
        draw3D(GetRenderWidth(), GetRenderHeight());
        rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
        drawOverlay();
        EndDrawing();
    }
    UnloadMesh(worldMesh);
    UnloadTexture(grid);
    renderer.shutdown();
    CloseWindow();
    return 0;
}
