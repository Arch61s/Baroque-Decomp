// ACT action player subset (see act_runner.hpp).
#include "game/act_runner.hpp"

#include "game/game_world.hpp"

#include <cstring>

#pragma fenv_access(on)

namespace baroque::game {

using data::ActCommand;

ActRunnerHooks& actRunnerHooks() {
    static ActRunnerHooks h;
    return h;
}

void ActRunner::init(const data::ActFile* file, ActorAnim* anim, Phys* phys, const PlayerControl* playerCtl,
                     const uint32_t* statusMask) {
    file_ = file;
    anim_ = anim;
    phys_ = phys;
    ctl_ = playerCtl;
    statusMask_ = statusMask;
}

const char* ActRunner::currentName() const { return cur_ != nullptr ? curName_.c_str() : nullptr; }

// orig FUN_00139f30 / FUN_00139ee0: start only if no action or a different one (strncmp 8).
bool ActRunner::request(const std::string& name8) {
    if (cur_ != nullptr && std::strncmp(curName_.c_str(), name8.c_str(), 8) == 0) return false;
    return start(name8);
}

// orig FUN_00143ee0: the ACT cache slot is loaded and holds an ACTN with the same two name words
bool ActRunner::has(const std::string& name8) const {
    if (file_ == nullptr) return false;
    for (const data::ActAction& a : file_->actions)
        if (std::strncmp(a.name.c_str(), name8.c_str(), 8) == 0 && a.name.size() == name8.size()) return true;
    return false;
}

// orig FUN_00143aa0: find the ACTN by its two name words; reset and make it current.
bool ActRunner::start(const std::string& name8) {
    if (file_ == nullptr) return false;
    for (const data::ActAction& a : file_->actions) {
        if (std::strncmp(a.name.c_str(), name8.c_str(), 8) == 0 && a.name.size() == name8.size()) {
            reset();
            cur_ = &a;
            curName_ = a.name;
            return true;
        }
    }
    return false;
}

// orig FUN_00143ca0
void ActRunner::animReset() {
    lastFrame_ = -1;
    lastTrackFrame_ = -1;
    if (anim_ != nullptr) {
        anim_->setScale(1.0f, 1);
        generation_ = anim_->currentClip();
    }
    if (phys_ != nullptr) {
        phys_->actMoveSpeed = 0.0f;
        phys_->actMoveAcc = 0.0f;
    }
    if (releaseHits) releaseHits();   // FUN_0014ded0(0x50c780, chr, 1)
}

// orig FUN_00144500(act, slot): stop the SEadx sound of the slot (FUN_00115820(snd, handle, 2)) and clear it
void ActRunner::seSlotStop(int slot) {
    if (slot < 0 || slot >= 4) return;   // PORT: the original indexes without a check
    const uint32_t h = seSlot[size_t(slot)];
    if (h != 0) {
        if (actRunnerHooks().seStop) actRunnerHooks().seStop(h, 2);
        seSlot[size_t(slot)] = 0;
    }
}

// orig FUN_00143bf0: reset per-action state. FUN_00144a70 effect slots -> actRunnerHooks().effectReset, FUN_001444b0
// SEadx slots, FUN_00146210 (+0x6c = 0). FUN_00145660 (weapon trails), FUN_001459c0 (NTurn look-at release), the
// ShowItem prop / GetItem bit tail and thunk_FUN_001460d0 run in `onReset` (character module, act_props.cpp).
void ActRunner::reset() {
    animReset();
    if (actRunnerHooks().effectReset) actRunnerHooks().effectReset(*this);   // FUN_00144a70
    for (int i = 0; i < 4; ++i) seSlotStop(i);                                // FUN_001444b0
    if (onReset) onReset();   // thunk_FUN_001460d0 (turn object)
    animScale = 0;            // FUN_00146210
    flags_ = 0;
}

Vec3f ActRunner::ownerPos() const {
    if (phys_ == nullptr || phys_->pos == nullptr) return Vec3f{0.0f, 0.0f, 0.0f};
    return *phys_->pos;
}

// orig FUN_00146f90 (work/decompiled/00146f90.c): the current action's name starts with "AT" (0x44b770) ->
// FUN_0010ae50(attackRateScale(inputs), actor, 1)
void ActRunner::attackRate() {
    if (cur_ == nullptr || curName_.size() < 2 || curName_[0] != 'A' || curName_[1] != 'T') return;
    AttackRateInputs in;
    if (attackRateInputs) attackRateInputs(in);
    if (anim_ != nullptr) anim_->setScale(attackRateScale(in), 1);
}

// orig FUN_00147db0: SE chosen by the floor faces under the owner (phys +0x58 floors): 'F?D' / 'F?E' spawn the
// puffs 0xd2 / 0xd3 (yaw rand(0x167)); 'FF' -> arg 1, 'FW' -> arg 2, 'FI' -> arg 3, else arg 0; FUN_00115470 at the owner.
void ActRunner::seByFloor(const data::ActCmnd& c) {
    if (c.args.size() < 4) return;
    int id = c.args[0].i;
    const ActRunnerHooks& h = actRunnerHooks();
    if (phys_ != nullptr) {
        const GroundInfo& g = phys_->ground;
        for (uint32_t i = 0; i < g.nFloors && i < g.floors.size(); ++i) {
            const CollFace* f = g.floors[i];
            if (f == nullptr) continue;
            if (f->type == 'F' && f->attr[1] == 'D') {
                const Vec3f rot{0.0f, static_cast<float>(gameWorld().rng.rand(0x167)), 0.0f};   // FUN_002071e0(0x53eb20, 0x167)
                if (h.effect) h.effect(0xd2, ownerPos(), &rot, 100.0f);                       // FUN_00112190(0, 0xd2, ..)
            }
            if (f->type == 'F' && f->attr[1] == 'E') {
                const Vec3f rot{0.0f, static_cast<float>(gameWorld().rng.rand(0x167)), 0.0f};
                if (h.effect) h.effect(0xd3, ownerPos(), &rot, 100.0f);
            }
            if (f->type == 'F' && f->attr[0] == 'F') { id = c.args[1].i; break; }
            if (f->type == 'F' && f->attr[0] == 'W') { id = c.args[2].i; break; }
            if (f->type == 'F' && f->attr[0] == 'I') { id = c.args[3].i; break; }
        }
    }
    if (h.se3d) h.se3d(1.0f, id, ownerPos());
}

// orig FUN_00143bc0
void ActRunner::stop() {
    reset();
    cur_ = nullptr;
    curName_.clear();
    generation_ = 0;
}

int ActRunner::animFrame() const {
    if (anim_ == nullptr) return 0;
    return int(anim_->currentTimeNs() / alchemy::enbaya::kFrameNs);
}

// orig FUN_00146a10
void ActRunner::runList(const std::vector<data::ActCmnd>& list) {
    for (const data::ActCmnd& c : list)
        if (!dispatch(c)) return;
}

// orig FUN_00146950 + handlers. Returns false when the current list must stop (Action switched, flags 0x100).
bool ActRunner::dispatch(const data::ActCmnd& c) {
    switch (c.cmd) {
    case ActCommand::End:  // thunk -> FUN_00143bc0
        stop();
        break;
    case ActCommand::Action:  // FUN_00146a90
        if (!c.args.empty()) start(c.args[0].s);
        flags_ |= 0x100;
        break;
    case ActCommand::Initial:  // FUN_00146ac0
        if ((flags_ & 1) == 0) {
            flags_ |= 1;
            runList(c.body);
        }
        break;
    case ActCommand::Frame: {  // FUN_00146af0
        const int now = animFrame();
        const int prev = lastFrame_;
        const int op = c.args.empty() ? 0 : c.args[0].i;
        if (now != prev) {
            bool fire;
            if (now < prev) fire = prev < op;
            else fire = prev < op && !(now < op);
            if (fire) runList(c.body);
        }
        break;
    }
    case ActCommand::Frame_T: {  // FUN_00146bb0: frame of the current animation state's local time (+0x14 = last)
        const int now = stateFrame();
        const int prev = lastTrackFrame_;
        const int op = c.args.empty() ? 0 : c.args[0].i;
        if (now != prev && prev < op && op <= now) runList(c.body);
        break;
    }
    case ActCommand::CollsRes: {  // FUN_00149630: mask 1 ground (phys +0x6c & 1), 2 landed (& 0x10), 4 wall (& 2),
                                  // 8 a hit of this action dealt damage (+0x1c & 0x4000000)
        const uint32_t m = c.args.empty() ? 0u : static_cast<uint32_t>(c.args[0].i);
        const uint32_t r = phys_ != nullptr ? phys_->result : 0u;
        if (((m & 1) != 0 && (r & 1) != 0) || ((m & 2) != 0 && (r & 0x10) != 0) || ((m & 4) != 0 && (r & 2) != 0) ||
            ((m & 8) != 0 && (flags_ & 0x4000000u) != 0))
            runList(c.body);
        break;
    }
    case ActCommand::AnmSet: {  // FUN_00146c50 core: set without blend
        const int clip = anim_ != nullptr && !c.args.empty() ? anim_->findClip(c.args[0].s.c_str()) : -1;
        if (anim_ != nullptr) anim_->set(clip < 0 ? 0 : clip);
        animReset();
        attackRate();   // FUN_00146f90
        break;
    }
    case ActCommand::AnmChg:    // FUN_00146d50: blend 0.2 s, speed 1.0, flag 0
    case ActCommand::AnmChgEx: {  // FUN_00146e60: anim, blend, flag, speed
        if (anim_ != nullptr && !c.args.empty()) {
            int clip = anim_->findClip(c.args[0].s.c_str());
            // not found -> iVar9 = 0: a null animation is passed on (clip -1 here)
            if (c.cmd == ActCommand::AnmChg) anim_->change(0.2f, 1.0f, clip, 0);
            else anim_->change(c.args.size() > 1 ? c.args[1].f : 0.2f, c.args.size() > 3 ? c.args[3].f : 1.0f, clip,
                               c.args.size() > 2 ? c.args[2].i : 0);
        }
        animReset();
        attackRate();   // FUN_00146f90
        break;
    }
    case ActCommand::BGM:   // FUN_00147d20: FUN_00115d70(snd, id, 0, 1)
        if (actRunnerHooks().bgm && !c.args.empty()) actRunnerHooks().bgm(c.args[0].i);
        break;
    case ActCommand::Voice:   // FUN_00147d40: FUN_00115970(1.0, snd, id, 0)
        if (actRunnerHooks().voice && !c.args.empty()) actRunnerHooks().voice(c.args[0].i);
        break;
    case ActCommand::SE:   // FUN_00147d60: FUN_00115470(1.0, snd, id, owner pos)
        if (actRunnerHooks().se3d && !c.args.empty()) actRunnerHooks().se3d(1.0f, c.args[0].i, ownerPos());
        break;
    case ActCommand::SEvol:   // FUN_00147d90: FUN_00115470(vol, snd, id, owner pos)
        if (actRunnerHooks().se3d && c.args.size() > 1) actRunnerHooks().se3d(c.args[1].f, c.args[0].i, ownerPos());
        break;
    case ActCommand::SEatr:   // FUN_00147db0
        seByFloor(c);
        break;
    case ActCommand::SEadx:      // FUN_001480b0: FUN_00115470(1.0, snd, [1], owner pos) -> slot [0] (FUN_00144550)
        if (c.args.size() > 1) {
            const uint32_t h = actRunnerHooks().se3dHandle ? actRunnerHooks().se3dHandle(1.0f, c.args[1].i, ownerPos()) : 0u;
            const int slot = c.args[0].i;
            if (slot >= 0 && slot < 4) seSlot[size_t(slot)] = h;   // overwritten without a stop, as FUN_00144550
        }
        break;
    case ActCommand::SEadxEnd:   // FUN_00148110 -> FUN_00144500(act, [0])
        if (!c.args.empty()) seSlotStop(c.args[0].i);
        break;
    case ActCommand::Event:   // FUN_00149b70: FUN_001d1d30(n)
        if (actRunnerHooks().event && !c.args.empty()) actRunnerHooks().event(c.args[0].i);
        break;
    case ActCommand::Exi_a:
    case ActCommand::Exi_b:
    case ActCommand::ExiZofs:
    case ActCommand::ExiLpOut:
    case ActCommand::ExiEnd:
        if (actRunnerHooks().effectCommand) actRunnerHooks().effectCommand(*this, c);
        break;
    case ActCommand::AnmSpeed:  // FUN_001473b0 -> FUN_0010ad90
        if (anim_ != nullptr && !c.args.empty()) anim_->setSpeed(c.args[0].f);
        break;
    case ActCommand::Move: {  // FUN_001479d0
        if (ctl_ != nullptr && (ctl_->flags & 0x20) != 0) break;
        if (statusMask_ != nullptr && (*statusMask_ & 1) != 0) break;
        if (phys_ == nullptr || c.args.size() < 2) break;
        const float speed = c.args[0].f;
        if (speed == 0.0f) {
            phys_->actMoveSpeed = 0.0f;
            phys_->actMoveAcc = 0.0f;
        } else {
            phys_->actMoveAngle = c.args[1].f;
            phys_->actMoveSpeed = speed;
            phys_->actMoveAcc = 0.0f;
        }
        break;
    }
    case ActCommand::Pad: {  // FUN_00147b90: open the pad window i (latched by FUN_00143fe0)
        const int i = c.args.empty() ? 0 : c.args[0].i;
        if (i == 2) flags_ |= 0x10000u;
        else if (i == 1) flags_ |= 0x4000u;
        else if (i == 0) flags_ |= 0x1000u;
        break;
    }
    case ActCommand::PadEnd:  // FUN_00147bf0
        padEnd(c);
        break;
    case ActCommand::FpsView:  // FUN_0014a340
        if (ctl_ != nullptr && (ctl_->flags & 0x20) != 0) runList(c.body);
        break;
    case ActCommand::NrmView:  // FUN_0014a380
        if (ctl_ == nullptr || (ctl_->flags & 0x20) == 0) runList(c.body);
        break;
    default:
        if (extension) extension(*this, c);   // handlers living in the character module (combat, flags, ...)
        break;
    }
    if ((flags_ & 0x100) != 0) {
        flags_ &= ~0x100u;
        return false;
    }
    return true;
}

// orig FUN_00147bf0 (work/decompiled/00147bf0.c): close pad window i; run the list when it was latched (window 2:
// unless the controller's charge word +0x1c has 0x20000000) and the owner is not paralysed (status +0x3c & 1).
// A latched button press (windows 0/1) counts as a chained attack: FUN_0013de00(0x50b620) (+0x14 += 1, +0x18 = 0)
// and FUN_00179570(0x53c5c0, 0xb, 1).
void ActRunner::padEnd(const data::ActCmnd& c) {
    const ActRunnerHooks& h = actRunnerHooks();
    PlayerControl* pc = h.playerControl ? h.playerControl() : nullptr;   // the global 0x50b620
    uint32_t v = 0;
    const int i = c.args.empty() ? 0 : c.args[0].i;
    if (i == 2) {
        flags_ &= 0xfffeffffu;
        if (pc == nullptr || (pc->counter & 0x20000000u) == 0) v = 0x10000u;   // uRam0050b63c
    } else if (i == 1) {
        v = flags_ & 0x8000u;
        flags_ &= 0xffff3fffu;
    } else if (i == 0) {
        v = flags_ & 0x2000u;
        flags_ &= 0xffffcfffu;
    }
    if ((statusMask_ == nullptr || (*statusMask_ & 1u) == 0) && v != 0) {
        if (v != 0x10000u) {
            if (pc != nullptr) {   // FUN_0013de00(0x50b620)
                pc->attacked = pc->attacked + 1;
                pc->combo = 0;
            }
            if (h.counterAdd) h.counterAdd(0xb, 1);   // FUN_00179570(0x53c5c0, 0xb, 1)
        }
        runList(c.body);
    }
}

// orig FUN_00143fe0 (work/decompiled/00143fe0.c + disasm 0x143fe0..0x144240). FUN_001459f0 (NTurn object) and
// FUN_00146220 are not translated; FUN_00146120 and the 0x2000000 hit latch run in the caller (actTurnStep /
// actHitLatch, character.cpp).
void ActRunner::tick() {
    if (cur_ == nullptr) return;
    if (anim_ != nullptr && anim_->currentClip() != generation_) {
        stop();
        return;
    }
    if ((flags_ & 0x15000u) != 0) {   // pad latch: window 0 <- slot 0x4e65d0, window 1 <- slot 0x4e65c8
        const ActRunnerHooks& h = actRunnerHooks();
        const uint32_t pressed = h.padPressed ? h.padPressed() : 0u;   // uRam004e6288
        if ((flags_ & 0x1000u) != 0 && (pressed & (h.buttonSlot ? h.buttonSlot(2) : 0u)) != 0) flags_ |= 0x2000u;
        if ((flags_ & 0x4000u) != 0 && (pressed & (h.buttonSlot ? h.buttonSlot(1) : 0u)) != 0) flags_ |= 0x8000u;
    }
    if ((flags_ & 0x400) == 0) {
        // the walk stays on this record even when a nested Action replaced the current action (only a 0 return of
        // FUN_00146950, i.e. ACT flag 0x100 at this level, stops it); the record stays valid in the ACT file
        const data::ActAction* a = cur_;
        for (const data::ActCmnd& c : a->cmds)
            if (!dispatch(c)) break;
    } else {
        flags_ &= ~0x400u;
    }
    int f = animFrame();
    if (f < lastFrame_) f = -1;
    lastFrame_ = f;
    lastTrackFrame_ = stateFrame();   // FUN_00244f38(*(actor+0x40) +0x28 local time, 0x1fca055)
}

// *(actor +0x40) +0x28 (current igAnimationState local time) in 30 Hz frames
int ActRunner::stateFrame() const {
    if (anim_ == nullptr || anim_->currentState() == nullptr) return 0;
    return int(anim_->currentState()->localTime / alchemy::enbaya::kFrameNs);
}

}  // namespace baroque::game
