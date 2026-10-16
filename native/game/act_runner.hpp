#pragma once
// ACT action player (the per-character "player" object of GAME_FORMATS.md §1), translated subset:
//   FUN_00139f30 request (only if different, FUN_00139ee0), FUN_00143aa0 lookup/start, FUN_00143bf0 reset,
//   FUN_00143bc0 stop, FUN_00143ca0 anim reset, FUN_00143fe0 tick, FUN_00146950 dispatch, FUN_00146a10 nested list.
// Handlers translated: End, Action, Initial (FUN_00146ac0), Frame (FUN_00146af0), AnmSet (FUN_00146c50 core),
//   AnmChg (FUN_00146d50), AnmChgEx (FUN_00146e60), AnmSpeed (FUN_001473b0), AnmFrame (FUN_001473d0),
//   Pad (FUN_00147b90), PadEnd (FUN_00147bf0) + the pad latch of FUN_00143fe0,
//   Move (FUN_001479d0), FpsView (FUN_0014a340), NrmView (FUN_0014a380), Frame_T (FUN_00146bb0),
//   CollsRes (FUN_00149630), BGM (FUN_00147d20), Voice (FUN_00147d40), SE (FUN_00147d60), SEvol (FUN_00147d90),
//   SEatr (FUN_00147db0), attack-rate scale FUN_00146f90 after AnmSet/AnmChg/AnmChgEx.
// Exi_a/Exi_b/ExiZofs/ExiLpOut/ExiEnd go to actRunnerHooks().effectCommand (the effect runtime plugs in there).
// Commands of the character module (Hit_*, MoveAcc, SetFlag, ChkFlag, Destroy, ...) go to `extension`.
#include "data/act.hpp"
#include "game/actor_anim.hpp"
#include "game/collision.hpp"
#include "game/player_move.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace baroque::game {

class ActRunner;

// Services shared by every ACT player, installed by the game build (unset = no side effect, as in the test tools).
struct ActRunnerHooks {
    // sound manager 0x4e9790: FUN_00115470(vol, snd, id, pos) / FUN_00115970(1.0, snd, id, 0) / FUN_00115d70(snd, id, 0, 1)
    std::function<void(float vol, int id, const Vec3f& pos)> se3d;
    // FUN_00115470 returning the sound-manager handle / FUN_00115820(snd, handle, time) (ACT SEadx / SEadxEnd)
    std::function<uint32_t(float vol, int id, const Vec3f& pos)> se3dHandle;
    std::function<void(uint32_t handle, int time)> seStop;
    std::function<void(int id)> voice;
    std::function<void(int id)> bgm;
    // FUN_00112190(0, fx, pos, rotDeg, (s, s, s, 1)) free effect (SEatr floor puffs 0xd2 / 0xd3)
    std::function<void(int fx, const Vec3f& pos, const Vec3f* rotDeg, float scale)> effect;
    // Exi_a (FUN_00148120), Exi_b (FUN_001481f0), ExiZofs (FUN_00148320), ExiLpOut (FUN_00148330), ExiEnd
    // (FUN_00148340): the action's 8 effect slots (FUN_00144b30 / FUN_00144ac0) belong to the effect runtime.
    std::function<void(ActRunner&, const data::ActCmnd&)> effectCommand;
    // FUN_00144a70 (FUN_00143bf0 reset): FUN_00144ac0(act, slot, 0) for the 8 effect slots
    std::function<void(ActRunner&)> effectReset;
    // ACT Event (FUN_00149b70): FUN_001d1d30(n)
    std::function<void(int n)> event;
    // Pad latch of FUN_00143fe0 / PadEnd FUN_00147bf0: pad pressed word uRam004e6288, button configuration slot
    // *(0x4e65c0 + slot*8) (slot 1 = 0x4e65c8 attack, slot 2 = 0x4e65d0 special), the player controller 0x50b620
    // (+0x14 attacked, +0x18 combo, +0x1c charge word) and FUN_00179570(0x53c5c0, id, delta). Unset = no pad input /
    // no controller / no counters (the test tools).
    std::function<uint32_t()> padPressed;
    std::function<uint32_t(int slot)> buttonSlot;
    std::function<PlayerControl*()> playerControl;
    std::function<void(int id, int delta)> counterAdd;
};
ActRunnerHooks& actRunnerHooks();

class ActRunner {
public:
    void init(const data::ActFile* file, ActorAnim* anim, Phys* phys, const PlayerControl* playerCtl,
              const uint32_t* statusMask);
    bool request(const std::string& name8);   // orig FUN_00139f30
    bool start(const std::string& name8);     // orig FUN_00143aa0
    bool has(const std::string& name8) const; // orig FUN_00143e80 -> FUN_00143ee0 (action exists in the loaded ACT)
    void stop();                              // orig FUN_00143bc0
    void tick();                              // orig FUN_00143fe0
    void runList(const std::vector<data::ActCmnd>& list);  // orig FUN_00146a10 (nested list; used by `extension`)
    const char* currentName() const;          // FUN_00143d30(player, -1): null when idle
    uint32_t flags() const { return flags_; }
    void orFlags(uint32_t f) { flags_ |= f; }
    void clearFlags(uint32_t f) { flags_ &= ~f; }

    // Character-side services, installed by the character module: commands executed outside the runner
    // (Hit_a/b/c, HitEnd, ...; return value ignored) and the hit-volume release of FUN_00143ca0
    // (FUN_0014ded0(0x50c780, chr, 1)).
    std::function<void(ActRunner&, const data::ActCmnd&)> extension;
    std::function<void()> releaseHits;
    // FUN_00143bf0 sub-object teardown done by the character module (thunk_FUN_001460d0: RotYAct turn object)
    std::function<void()> onReset;
    // FUN_00146f90 inputs: equipment of the inventory 0x53d910 (iRam0053d9b4 / iRam0053d9b8) and the owner's status
    // block (+0x5c parasite, +0x3c, +0x54 brand, +0x58 hidden stats). Unset = no equipment, zero hidden stat.
    std::function<void(AttackRateInputs&)> attackRateInputs;
    void* owner = nullptr;                    // +0x04 the character (Chr*), for the hooks
    // effect slot records *(act +0x30 + i*4) (0x24 bytes each): +0 handle (-1 none), +4 joint, +8 offset, +0x14 rotation
    // (degrees; +0x18 yaw added), +0x20 flags (bit0 follow, bit1 on a joint, bit3 joint matrix)
    struct ExiSlot {
        int handle = -1;
        int joint = 0;
        Vec3f ofs{};
        Vec3f rot{};
        uint32_t flags = 0;
    };
    std::array<ExiSlot, 8> exi{};
    std::array<uint32_t, 4> seSlot{};         // +0x20..+0x2c SEadx handles (FUN_00144550 / FUN_00144500)
    Vec3f objectPos{};                        // +0x50 spawn / release point written by Object (FUN_00148350)
    int animScale = 0;                        // +0x6c AnmScale mode (FUN_00149df0; 1 = squash step FUN_00146220)
    Vec3f ownerPos() const;                   // *(owner +0x20) +0xc (through the physics object's position pointer)

private:
    void reset();                             // orig FUN_00143bf0
    void animReset();                         // orig FUN_00143ca0
    bool dispatch(const data::ActCmnd& c);    // orig FUN_00146950
    void padEnd(const data::ActCmnd& c);      // orig FUN_00147bf0
    int animFrame() const;                    // FUN_0010aaf0 / 0x1fca055
    int stateFrame() const;                   // current animation state's local time / 0x1fca055
    void attackRate();                        // orig FUN_00146f90
    void seByFloor(const data::ActCmnd& c);   // orig FUN_00147db0
    void seSlotStop(int slot);                // orig FUN_00144500

    const data::ActFile* file_ = nullptr;
    ActorAnim* anim_ = nullptr;
    Phys* phys_ = nullptr;
    const PlayerControl* ctl_ = nullptr;      // non-null for the player (0x50b144 == chr)
    const uint32_t* statusMask_ = nullptr;    // StatusBlock +0x3c
    const data::ActAction* cur_ = nullptr;    // +0x0c
    int lastFrame_ = -1;                      // +0x10
    int lastTrackFrame_ = -1;                 // +0x14
    int generation_ = -1;                     // +0x18 (actor's current animation when the action set it)
    uint32_t flags_ = 0;                      // +0x1c
    std::string curName_;
};

}  // namespace baroque::game
