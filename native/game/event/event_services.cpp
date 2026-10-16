// Services of the event manager in other objects (see event_services.hpp).
#include "game/event/event_services.hpp"

#include "alchemy/animation.hpp"
#include "alchemy/igb.hpp"
#include "game/character/character.hpp"
#include "game/engine.hpp"
#include "game/event/event_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/objects/objects.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <cstdio>
#include <memory>
#include <vector>

namespace baroque::game {

namespace {

uint32_t gSavedStatusMask = 0;        // uRam0050b1e8
int32_t gSavedTimers[19] = {};        // 0x50b1f0[19]
Item* gSavedEquip[4] = {};            // 0x53dbc0..0x53dbcc
int gEventMode = 0;                   // uRam0053c6a8

// player +0x54 animation-set table entry {first igAnimation, count, set id} (slot stride 0xc)
struct ActorSetSlot {
    std::unique_ptr<alchemy::IgbFile> file;   // kept alive while its clips are in the database
    Chr* owner = nullptr;
    size_t first = 0;
    size_t count = 0;
    uint32_t id = 0;
    bool loaded = false;
};
ActorSetSlot gSets[4];
// PORT: clips removed from the database are parked here instead of freed: the igAnimationCombiner states keep a
// reference in the original (refcount), so a state that still plays an event clip stays valid.
std::vector<std::unique_ptr<alchemy::AnimClip>> gRetiredClips;
std::vector<std::unique_ptr<alchemy::IgbFile>> gRetiredFiles;

uint32_t id4(const char* s) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}

}  // namespace

// orig FUN_00138c50 (work/decompiled/00138c50.c)
void eventPlayerStatusSave() {
    Chr* p = characters().player();
    if (p == nullptr) return;                                // PORT: the original dereferences iRam0050b144
    StatusBlock& b = p->status.block;
    gSavedStatusMask = b.statusMask;
    b.statusMask = 0;
    for (int i = 0; i < 0x13; ++i) {
        gSavedTimers[i] = b.timers[static_cast<size_t>(i)];
        b.timers[static_cast<size_t>(i)] = 0;
        if (eventHooks().playerStatusVisuals) eventHooks().playerStatusVisuals(i, false);   // FUN_00137f40(status, i)
    }
}

// orig FUN_00138d00 (work/decompiled/00138d00.c)
void eventPlayerStatusRestore() {
    Chr* p = characters().player();
    if (p == nullptr) return;
    StatusBlock& b = p->status.block;
    b.statusMask = gSavedStatusMask;
    for (int i = 0; i < 0x13; ++i) {
        b.timers[static_cast<size_t>(i)] = gSavedTimers[i];
        if (b.timers[static_cast<size_t>(i)] != 0 && eventHooks().playerStatusVisuals)
            eventHooks().playerStatusVisuals(i, true);       // FUN_00137d40(status, i)
    }
}

// orig FUN_0012cb90 (work/decompiled/0012cb90.c). Character part: 'E' actors whose model node is visible get it
// hidden and the update-skip bit. Item part: every linked, live item of the list 0x50a830 (FUN_0012c1e0 count read
// once, FUN_0012c1f0 array) gets +0x70 |= 0x10000000, one vt+0xc tick (FUN_001d5180) and FUN_001d3b30 (leaves the
// task list, model released).
void eventRegistryHide() {
    for (Chr* c : characters().list()) {
        if ((c->id & 0xffu) != 'E') continue;
        if (c->model.parts.empty()) continue;                // node list count 0
        ChrModelPart& part = c->model.parts[0];
        if (part.visible) {
            part.visible = false;
            c->flags24 |= 0x10000000u;
        }
    }
    GameAdapter& g = gameAdapter();
    const int n = g.floorItemCount();                        // FUN_0012c1e0
    for (int i = 0; i < n; ++i) {
        Item* it = g.floorItem(i);                           // FUN_0012c1f0()[i]
        if (it == nullptr) continue;                         // PORT: the original reads its array past a shrunk count
        if ((it->taskFlags & 1u) == 0 || (it->taskFlags & 0x10000000u) != 0) continue;
        it->flags |= 0x10000000u;
        if (Chr* rec = itemEntityOf(it)) itemEntityTick(*rec);   // vt+0xc = FUN_001d5180
        itemEntityUnlink(*it);                               // FUN_001d3b30
    }
}

// orig FUN_0012ccf0 (work/decompiled/0012ccf0.c). Item part: the count FUN_0012c1e0 is re-read every iteration; an
// item carrying +0x70 0x10000000 loses it, re-enters the world (FUN_001d3a30) and its model node is published at
// its transform (FUN_00109d40(+0x2c, pos, scale, fwd, up, right)).
void eventRegistryRestore() {
    for (Chr* c : characters().list()) {
        if ((c->flags24 & 0x10000000u) == 0) continue;
        c->flags24 &= 0xefffffffu;
        if (!c->model.parts.empty()) c->model.parts[0].visible = true;
    }
    GameAdapter& g = gameAdapter();
    for (int i = 0; i < g.floorItemCount(); ++i) {
        Item* it = g.floorItem(i);
        if (it == nullptr || (it->flags & 0x10000000u) == 0) continue;
        it->flags &= 0xefffffffu;
        g.itemEnterWorld(*it);                               // FUN_001d3a30
        Item::ModelXf& m = it->modelXf;                      // FUN_00109d40
        m.pos = it->pos;
        m.right = it->right;
        m.up = it->up;
        m.fwd = it->fwd;
        m.scale = it->scale;
        m.valid = true;
        if (Chr* rec = itemEntityOf(it); rec != nullptr && rec->model.loaded()) modelPublishFx(rec->model);   // FUN_0010a160
    }
}

// orig FUN_001b2620 (work/decompiled/001b2620.c) + load callback FUN_001b0f10 (work/decompiled/001b0f10.c):
// "DATA/CHR/C00/C00A_%c%c%c%c.IGB" (0x462a80); its igAnimationDatabase's animations are appended to the actor's
// database (FUN_0031c3b0), first/count kept in the +0x54 table. The port loads synchronously (FUN_0010cf60 async).
void eventActorSetLoad(Chr& c, int slot, const char* setId) {
    if (slot < 0 || slot >= 4) return;
    if (gSets[slot].loaded) eventActorSetRelease(c, slot);
    ActorSetSlot& s = gSets[slot];
    s.id = id4(setId);
    char path[0x50];
    std::snprintf(path, sizeof path, "DATA/CHR/C00/C00A_%c%c%c%c.IGB", setId[0], setId[1], setId[2], setId[3]);
    auto file = std::make_unique<alchemy::IgbFile>();
    std::string err;
    if (!file->load(platform::assetRoot() / path, &err)) {
        platform::log(std::string("event: actor set ") + path + ": " + err);
        return;
    }
    std::vector<std::string> log;
    auto clips = alchemy::loadClips(*file, alchemy::primarySkeleton(*file), &log);
    s.first = c.model.clips.size();
    s.count = clips.size();
    for (auto& clip : clips) c.model.clips.push_back(std::move(clip));
    s.file = std::move(file);
    s.owner = &c;
    s.loaded = true;
    // FUN_001b14e0(chr): equipment model refresh after the set arrived (items module, CharacterAccess) UNVERIFIED
    platform::log(std::string("event: actor set ") + setId + " " + std::to_string(s.count) + " animations");
}

// orig FUN_001b2730 (work/decompiled/001b2730.c): the set's animations are removed from the database again
void eventActorSetRelease(Chr& c, int slot) {
    if (slot < 0 || slot >= 4) return;
    ActorSetSlot& s = gSets[slot];
    if (!s.loaded) return;
    // The original's combiner states hold the igAnimation objects (reference counted), so a state still playing one
    // of the removed animations keeps it after FUN_0031c3b0 took it out of the database. The port's states hold clip
    // indices: erasing shifted / emptied them (null clip in evaluateLocals right after E10 / E15 ended). The clips
    // stay in the actor's list, renamed so that no name lookup (FUN_0010a7a0 / findClip) finds them any more.
    if (s.owner == &c && s.first + s.count <= c.model.clips.size()) {
        for (size_t i = s.first; i < s.first + s.count; ++i)
            if (c.model.clips[i]) c.model.clips[i]->name.clear();
    }
    gRetiredFiles.push_back(std::move(s.file));              // FUN_0010cd50 releases the file
    s = ActorSetSlot{};
}

// orig FUN_001d7dc0 (work/decompiled/001d7dc0.c): the event costume. The four equipment words are parked in
// 0x53dbc0.. and replaced by items created through the factory (FUN_0012ce70(.., 0): not placed in the world).
void eventEquipOverride(int kind) {
    Inventory& inv = inventory();
    for (int i = 0; i < 4; ++i) {
        gSavedEquip[i] = inv.equip[i];
        inv.equip[i] = nullptr;
    }
    GameAdapter& g = gameAdapter();
    if (kind == 4) {
        inv.equip[2] = g.createItem(id4("CT06"), false);     // DAT_00464358
        inv.equip[3] = g.createItem(id4("WG07"), false);     // DAT_00464368
    } else if (kind == 3) {
        inv.equip[2] = g.createItem(id4("CT06"), false);
        inv.equip[3] = g.createItem(id4("WG09"), false);     // DAT_00464360
    } else if (kind == 2) {
        inv.equip[2] = g.createItem(id4("CT16"), false);     // DAT_00464350
        inv.equip[3] = g.createItem(id4("WG04"), false);     // DAT_00464248
    } else if (kind == 1) {
        inv.equip[2] = gSavedEquip[2];
    }
    g.player().refreshModelAttachments();                    // FUN_001b1540(player)
    if (eventActive()) g.player().gunEquipped(false, true);  // FUN_001b2060(player, 1) when iRam0051b620 == 1
}

namespace {
// orig FUN_001d7d10 (work/decompiled/001d7d10.c): hand layout of the equipped weapon
int weaponGrip(const Inventory& inv) {
    const Item* w = inv.equip[0];
    if (w == nullptr) return -1;
    if (w->cls != 0) return 3;
    const int32_t g = itemSystem().word(kClsSD, w->idx, 0x9c);   // *(iRam0053d860 +0x44) + idx*0xa0 + 0x9c
    if (g == 2) return inv.equip[1] == nullptr ? 0 : 2;
    if (g == 1) return 1;
    if (g == 0) return 0;
    return -1;
}
}  // namespace

// orig FUN_001d8060 (work/decompiled/001d8060.c)
void eventEquipRestore() {
    Inventory& inv = inventory();
    GameAdapter& g = gameAdapter();
    for (int i = 0; i < 4; ++i) {
        if (inv.equip[i] != nullptr && inv.equip[i] != gSavedEquip[i]) g.freeItem(inv.equip[i]);   // FUN_001b7570
        inv.equip[i] = gSavedEquip[i];
    }
    g.player().refreshModelAttachments();                    // FUN_001b1540(player)
    const int guns = inv.countId(id4("GN00"));               // FUN_001d9310(inv, 'GN00')
    if (weaponGrip(inv) == 3) {
        if (1 < guns) g.player().gunEquipped(true);          // FUN_001b1f20(player)
    } else if (guns != 0) {
        g.player().gunEquipped(true);
    }
}

// orig FUN_0011e6c0 / FUN_0011e740 (work/decompiled/0011e6c0.c, 0011e740.c)
float eventMapOrigin(int room, int axis) {
    EventHooks& h = eventHooks();
    const bool generated = h.generatedFloor && h.generatedFloor();   // FUN_0011c0a0(0x50a2a0)
    if (!generated || !h.mapRoomOrigin) return 0.0f;
    return static_cast<float>(h.mapRoomOrigin(room, axis) * 0x640 + 100);
}

// orig FUN_0017a0c0 (work/decompiled/0017a0c0.c)
void eventMode(int on) {
    gEventMode = on;                                         // uRam0053c6a8
    hudWorld().talkBusy = on != 0;                           // the HUD's mirror of uRam0053c6a8
    if (on == 0) engine().flags &= 0xfffffffeu;              // uRam004e6640
    else engine().flags |= 1u;
    EventHooks& h = eventHooks();
    if (h.effectsPause) h.effectsPause(on != 0);             // FUN_00112620
    if (on != 0 && h.seStop) h.seStop(0xfffe, 2);            // FUN_00115900
    if (h.voicePause) h.voicePause(on);                      // FUN_00115c50(snd, on)
    if (on == 0) sound().setBgmFadeTarget(1.0f, 4);          // FUN_00115f00(1.0, snd, 4)
    else sound().setBgmFadeTarget(0.7f, 4);                  // 0x3f333333
}
int eventModeFlag() { return gEventMode; }

}  // namespace baroque::game
