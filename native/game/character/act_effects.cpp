// ACT effect slots of the character module (game build only; installed into actRunnerHooks() by
// player_control_wiring.cpp): Exi_a FUN_00148120, Exi_b FUN_001481f0, ExiZofs FUN_00148320, ExiLpOut FUN_00148330,
// ExiEnd FUN_00148340 with the slot helpers FUN_00144b30 / FUN_00144bc0 / FUN_00144d10 / FUN_00144d80 / FUN_00144b60 /
// FUN_00144ac0, the reset FUN_00144a70, and the per-frame follow FUN_0012a4e0 -> FUN_00144560. The effects are the EXI
// runtime's (game/effects/effects.hpp).
#include "game/character/act_effects.hpp"

#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/effects/effects.hpp"
#include "game/player_move.hpp"

#include <cmath>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr float kPi = 3.1415927f;

// actor+0x48 vt+0x70 (FUN_003f6990): joint by name, case-insensitive (FUN_0023f068); -1 when absent
int jointByName(const Chr& c, const std::string& name) {
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr) return -1;
    for (size_t i = 0; i < sk->bones.size(); ++i) {
        const std::string& b = sk->bones[i].name;
        if (b.size() != name.size()) continue;
        bool eq = true;
        for (size_t k = 0; k < b.size() && eq; ++k) {
            char x = b[k], y = name[k];
            if ('A' <= x && x <= 'Z') x = static_cast<char>(x + 0x20);
            if ('A' <= y && y <= 'Z') y = static_cast<char>(y + 0x20);
            eq = x == y;
        }
        if (eq) return static_cast<int>(i);
    }
    return -1;
}

// joint world matrix: FUN_0039d860(joint matrix of the current pose) x FUN_0039d450(model world +0x20)
alchemy::Mat4 jointWorld(Chr& c, int joint) {
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    const alchemy::Mat4 actor = chrActorMatrix(c);
    if (joint < 0 || static_cast<size_t>(joint) >= pose.size()) return actor;
    return alchemy::mul(pose[size_t(joint)], actor);
}

Chr* ownerOf(ActRunner& r) { return static_cast<Chr*>(r.owner); }

// orig FUN_00144b30(act, slot, follow): flags = follow == 1 ? 1 : 0
ActRunner::ExiSlot& slotInit(ActRunner& r, int slot, int follow) {
    ActRunner::ExiSlot& s = r.exi[size_t(slot & 7)];
    s.flags = 0;
    if (follow == 1) s.flags |= 1u;
    return s;
}

// orig FUN_00144bc0(act, out pos, slot): owner position (or the joint's world position when flags & 2) plus the slot
// offset rotated by the model matrix (FUN_003a0330 with the model world +0x20)
Vec3f slotPos(Chr& c, const ActRunner::ExiSlot& s) {
    Vec3f p;
    if ((s.flags & 2u) == 0) {
        p = c.xf.m.pos;
    } else {
        const alchemy::Mat4 w = jointWorld(c, s.joint);
        p = {w.m[12], w.m[13], w.m[14]};   // FUN_0039d9a0
    }
    const alchemy::Mat4 m = chrActorMatrix(c);
    const Vec3f o{s.ofs[0] * m.m[0] + s.ofs[1] * m.m[4] + s.ofs[2] * m.m[8],
                  s.ofs[0] * m.m[1] + s.ofs[1] * m.m[5] + s.ofs[2] * m.m[9],
                  s.ofs[0] * m.m[2] + s.ofs[1] * m.m[6] + s.ofs[2] * m.m[10]};
    return {p[0] + o[0], p[1] + o[1], p[2] + o[2]};
}

// orig FUN_00144d10(act, out rot, slot): (0, yaw of the owner's forward (FUN_00206d20 / FUN_00206ec0) + slot +0x18, 0)
Vec3f slotRot(const Chr& c, const ActRunner::ExiSlot& s) {
    const Vec3f& f = c.xf.m.fwd;
    float yaw = (std::atan2(f[0], f[2]) * 180.0f) / kPi;   // FUN_00238878(x, z)
    if (360.0f <= yaw) yaw = yaw - 360.0f;
    else if (yaw < 0.0f) yaw = yaw + 360.0f;
    return {0.0f, yaw + s.rot[1], 0.0f};
}

// orig FUN_00144ac0(act, slot, loopOut): the slot's effect ends (loop-out or FUN_00112560 stop), handle -1
void slotEnd(ActRunner& r, int slot, bool loopOut) {
    ActRunner::ExiSlot& s = r.exi[size_t(slot & 7)];
    if (s.handle == -1) return;
    if (loopOut) effects().loopOut(s.handle, 0, -1, -1);   // FUN_001126e0(h, 0, -1)
    else effects().stop(s.handle);                          // FUN_00112560
    s.handle = -1;
}

void spawnSlot(Chr& c, ActRunner::ExiSlot& s, int fx, float scale) {
    const Vec3f p = slotPos(c, s);
    const Vec3f rot = slotRot(c, s);
    const float sc[4] = {scale, scale, scale, 1.0f};
    const float p4[4] = {p[0], p[1], p[2], 0.0f};
    const float r4[4] = {rot[0], rot[1], rot[2], 1.0f};
    s.handle = effects().spawn(0.0f, fx, p4, r4, sc);   // FUN_00112190(0, fx, pos, rot, scale)
}

}  // namespace

// The ACT effect commands (operands: Exi_a "i i i f f f f f", Exi_b + joint name, ExiZofs "i f", others "i")
void actEffectCommand(ActRunner& r, const data::ActCmnd& c) {
    Chr* owner = ownerOf(r);
    if (owner == nullptr) return;
    auto ai = [&](size_t k) { return k < c.args.size() ? c.args[k].i : 0; };
    auto af = [&](size_t k) { return k < c.args.size() ? c.args[k].f : 0.0f; };
    switch (c.cmd) {
    case data::ActCommand::Exi_a: {   // FUN_00148120
        ActRunner::ExiSlot& s = slotInit(r, ai(0), ai(1));
        s.ofs = {af(5), af(6), af(7)};
        s.rot = {0.0f, af(3), 0.0f};
        spawnSlot(*owner, s, ai(2), af(4));
        s.flags &= ~8u;
        break;
    }
    case data::ActCommand::Exi_b: {   // FUN_001481f0
        ActRunner::ExiSlot& s = slotInit(r, ai(0), ai(1));
        s.flags |= 2u;
        s.joint = jointByName(*owner, c.args.size() > 8 ? c.args[8].s : std::string());
        s.ofs = {af(5), af(6), af(7)};
        if (af(3) < 0.0f) {
            s.flags |= 8u;
            s.rot = {0.0f, 0.0f, 0.0f};
        } else {
            s.flags &= ~8u;
            s.rot = {0.0f, af(3), 0.0f};
        }
        spawnSlot(*owner, s, ai(2), af(4));
        break;
    }
    case data::ActCommand::ExiZofs: {   // FUN_00148320 -> FUN_00144d80(tag, act, slot)
        const ActRunner::ExiSlot& s = r.exi[size_t(ai(0) & 7)];
        if (s.handle != -1 && effects().exists(s.handle)) effects().setTag(s.handle, af(1));   // FUN_001124d0
        break;
    }
    case data::ActCommand::ExiLpOut: {   // FUN_00148330 -> FUN_00144b60
        const ActRunner::ExiSlot& s = r.exi[size_t(ai(0) & 7)];
        if (s.handle != -1 && effects().exists(s.handle)) effects().loopOut(s.handle, 0, -1, -1);
        break;
    }
    case data::ActCommand::ExiEnd:   // FUN_00148340 -> FUN_00144ac0(act, slot, 0)
        slotEnd(r, ai(0), false);
        break;
    default: break;
    }
}

// orig FUN_00144a70 (FUN_00143bf0 reset)
void actEffectReset(ActRunner& r) {
    for (int i = 0; i < 8; ++i) slotEnd(r, i, false);
}

// orig FUN_00144560: following slots (flags & 1) are moved each frame: position + yaw, or the joint's matrix scaled
// by 100 (flags & 8, FUN_0039dc30); dead effects free their slot.
void actEffectFollow(Chr& c) {
    ActRunner& r = c.act;
    for (ActRunner::ExiSlot& s : r.exi) {
        if (s.handle == -1) continue;
        if (!effects().exists(s.handle)) {
            s.handle = -1;
            continue;
        }
        if ((s.flags & 1u) == 0) continue;
        if ((s.flags & 8u) == 0) {
            const Vec3f p = slotPos(c, s);
            const Vec3f rot = slotRot(c, s);
            effects().setPositionRotation(s.handle, p.data(), rot.data());   // FUN_001123f0
        } else {
            // UNVERIFIED: the first-person camera correction of the player (ctl flag 0x20, 0x50b624 +200 pitch) is not
            // applied
            alchemy::Mat4 w = jointWorld(c, s.joint);
            for (int k = 0; k < 4; ++k) {   // FUN_0039dc30(m, (100, 100, 100))
                w.m[size_t(k)] *= 100.0f;
                w.m[size_t(4 + k)] *= 100.0f;
                w.m[size_t(8 + k)] *= 100.0f;
            }
            ExiMat e;
            for (int k = 0; k < 16; ++k) e.m[size_t(k)] = w.m[size_t(k)];
            effects().setMatrix(s.handle, e);   // FUN_00112770
        }
    }
    // UNVERIFIED: the player's status-0x20000 aura (effect 0x98 at "Head" and the "FBLW" hit spheres, DAT_0044b320)
    // of FUN_00144560 is not translated
}

namespace {
bool gFrameHooked = false;
}
bool actEffectsFrameHooked() { return gFrameHooked; }

// orig FUN_0012a4e0 (engine frame, after the 3-D pass): characters of types 0..2 that are active with an ACT
void chrActEffectsFrame() {
    gFrameHooked = true;
    for (Chr* c : characters().list()) {
        if (c->obj != nullptr || c->item != nullptr || static_cast<unsigned>(c->type) >= 3u) continue;
        if (!c->active() || !c->actLoaded) continue;
        actEffectFollow(*c);
    }
}

}  // namespace baroque::game
