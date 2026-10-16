// Game binding of the talk manager (see talk_wiring.hpp). Each TalkWorld method reads the port object that holds the
// original global named in talk_manager.hpp.
#include "game/talk/talk_wiring.hpp"

#include "audio/sound_system.hpp"
#include "data/archive.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/character/npc_class.hpp"
#include "game/dungeon.hpp"
#include "game/effects/effects.hpp"
#include "game/engine.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_services.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item.hpp"
#include "game/items/item_screen.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/message_box.hpp"
#include "platform/platform.hpp"

#include <cstring>
#include <memory>

namespace baroque::game {

void talkDevTickInternal();   // talk_dev.cpp

namespace {

// TalkObj handles: an item (Item*) carries bit 0, any other registry entry is its Chr*.
TalkObj encodeItem(Item* it) { return it == nullptr ? nullptr : reinterpret_cast<TalkObj>(reinterpret_cast<uintptr_t>(it) | 1u); }
Item* asItem(TalkObj o) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(o);
    return (v & 1u) != 0 ? reinterpret_cast<Item*>(v & ~uintptr_t{1}) : nullptr;
}
Chr* asChr(TalkObj o) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(o);
    return (v & 1u) == 0 ? reinterpret_cast<Chr*>(v) : nullptr;
}
Chr* chr(TalkActor a) { return static_cast<Chr*>(a); }

audio::SoundSystem* soundSystem() {
    auto* m = dynamic_cast<audio::GameSoundManager*>(&sound());
    return m != nullptr ? &m->system() : nullptr;
}

// exact-name lookup in an animation list (FUN_0023f3fc strcmp over igAnimation names; null name -> "" 0x48b460)
int clipByName(const std::vector<std::unique_ptr<alchemy::AnimClip>>& clips, const char* name) {
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i]->name == name) return static_cast<int>(i);
    return -1;
}

struct GameTalkWorld final : TalkWorld {
    bool sceneIsGame() override { return scenes().isCurrent("Game...."); }
    bool itemScreenOpen() override { return hudItemScreenOpen(); }
    bool systemMenuOpen() override { return ui::menu().isOpen(); }
    bool enginePaused() override { return engine().paused(); }
    bool messageBoxOpen() override { return ui::messageBox().isOpen(); }
    uint32_t floorFlags() override {
        const Dungeon* d = gameWorld().dungeon;
        if (d == nullptr) return 0;
        const ItemWorldState& w = hudWorld();
        return (d->floorFlags() & ~w.clearedFloorFlags) | w.portFloorFlags;   // uRam0050a610 (game_scene floorFlagWord)
    }
    bool eventRunning() override { return eventActive(); }
    int eventMode() override { return eventModeFlag(); }
    int floorId() override { return gameProgress().floor; }
    int floorNumber() override { return game::floorNumber(gameProgress().floor); }
    uint32_t playerControlFlags() override { return playerCtl() != nullptr ? playerCtl()->flags : 0u; }
    bool fadeRunning() override { return fade().frames != 0; }
    bool optionBit4() override { return (options().camera & 4u) != 0; }   // FUN_0015dcb0: *0x51af68 & 4
    uint32_t padPressed() override { return pad().port[0].pressed; }
    uint32_t talkButton() override { return buttons().slot[0]; }          // uRam004e65c0
    float lingerGate() override { return pad().port[0].lstick[2]; }       // fRam004e62d8 = port 0 +0x54
    int actorCount() override { return static_cast<int>(characters().list().size()); }
    TalkActor actorAt(int i) override { return characters().list()[static_cast<size_t>(i)]; }
    TalkActor player() override { return characters().player(); }
    int actorType(TalkActor a) override { return chr(a)->type; }
    uint32_t actorId(TalkActor a) override { return a == nullptr ? 0u : chr(a)->id; }
    void actorPos(TalkActor a, float out[3]) override {
        for (int k = 0; k < 3; ++k) out[k] = chr(a)->xf.m.pos[size_t(k)];
    }
    void setActorPos(TalkActor a, const float p[3]) override {
        for (int k = 0; k < 3; ++k) chr(a)->xf.m.pos[size_t(k)] = p[k];
    }
    void actorForward(TalkActor a, float out[3]) override {
        for (int k = 0; k < 3; ++k) out[k] = chr(a)->xf.m.fwd[size_t(k)];
    }
    int actorRoom(TalkActor a) override {
        const Chr* c = chr(a);
        if (c->cell == nullptr) return -2;
        return (*c->cell & 0xf000u) == 0x1000u ? static_cast<int>(*c->cell & 0xfu) : -1;
    }
    int actorHp(TalkActor a) override { return chr(a)->status.block.hp; }
    int actorHpMax(TalkActor a) override { return chr(a)->status.block.hpMax; }
    int actorVt(TalkActor a) override { return chr(a)->status.block.vt; }
    int actorVtMax(TalkActor a) override { return chr(a)->status.block.vtMax; }
    TalkObj carriedItem(TalkActor a) override { return encodeItem(static_cast<Item*>(chr(a)->status.block.carried)); }
    TalkObj objOfActor(TalkActor a) override {
        Chr* c = chr(a);
        if (c != nullptr && c->item != nullptr) return encodeItem(c->item);   // item entity: the item is the actor
        return c;
    }
    TalkActorRef objRef(TalkObj o) override {
        TalkActorRef r;
        if (const Item* it = asItem(o)) {
            r.type = it->type;
            r.id = it->id;
            r.f24 = it->cls;
            r.f54 = it->atkBase;
            r.f58 = it->defBase;
            r.f60 = it->atkMod;
            r.f64 = it->defMod;
            r.flags70 = it->flags;
        } else if (const Chr* c = asChr(o)) {
            r.type = c->type;
            r.id = c->id;
        }
        return r;
    }
    void objOrFlags70(TalkObj o, uint32_t bits) override {
        if (Item* it = asItem(o)) it->flags |= bits;
        // UNVERIFIED: +0x70 of a non-item registry entry (only items reach this call: the "IS**" test)
    }
    bool partAnim(TalkActor a, int part, const char* name, float blend, float speed) override {
        Chr* c = chr(a);
        if (part == 0) {
            const int clip = clipByName(c->model.clips, name);
            // UNVERIFIED: a name missing from the list hands FUN_0010a8e0 a null animation; the port skips the change
            if (clip >= 0) c->model.anim.change(blend, speed, clip, 0);
            else platform::log(std::string("talk: body animation ") + name + " not in " + c->idString());   // diagnostics
            return true;
        }
        return npcPartAnim(*c, part, name, blend, speed);                 // vt+0x28 / vt+0x2c (npc_class.cpp)
    }
    void actAction(TalkActor a, const char* name8) override {
        char buf[9] = {};
        for (int k = 0; k < 8 && name8[k] != '\0'; ++k) buf[k] = name8[k];   // FUN_0023fca0(buf, name, strlen(name))
        if (!chr(a)->act.start(buf))                                       // FUN_00143aa0(act, buf)
            platform::log(std::string("talk: ACT action ") + buf + " not started on " + chr(a)->idString());   // diagnostics
    }
    int hitRecords(TalkActor a, TalkHitRecord out[32]) override {
        // orig FUN_0014e090 (work/decompiled/0014e090.c + disasm 0x14e0b8..0x14e170): the hurt volume of `a`; its first
        // registration is tested 32 times (the record pointer is never advanced), so the list is 0 or 32 copies of
        // registration 0 when it is new (bit0) and not damage-free (bit1).
        for (const HurtVolume& v : hitManager().victims) {
            if (v.ent != chr(a)) continue;
            const HurtReg& r = v.regs[0];
            if (r.attacker == nullptr || (r.flags & 1u) == 0 || (r.flags & 2u) != 0) return 0;
            for (int i = 0; i < 32; ++i) out[i] = TalkHitRecord{r.attacker, r.m1, r.flags};
            return 32;
        }
        return 0;
    }
    int roomMapId(int room) override {
        const Dungeon* d = gameWorld().dungeon;
        if (d == nullptr || room < 0 || room >= d->roomCount()) return -1;
        return static_cast<int>(d->room(room).rdt.roomId);                 // **(0x50a640[room] + 4)
    }
    int floorRoomByte() override {
        const Dungeon* d = gameWorld().dungeon;
        return d != nullptr ? static_cast<int>(d->fdt().mapSet) : 0;      // *pbRam0050a620 (FDT byte 0, see hud_api.cpp)
    }
    float roomOrigin(int room, int axis) override { return eventMapOrigin(room, axis); }
    uint32_t giftTarget() override { return itemScreen().giftTarget(); }  // FUN_0015c2c0 (item screen +0x534)
    TalkObj giftItem() override { return itemScreen().giftItem(); }        // FUN_0015c2d0 (+0x538)
    void giftClear() override { itemScreen().giftClear(); }                // FUN_0015c2e0
    int databaseCount() override { return collectorBag().count; }         // iRam0053dcc8 (0x53dc00 +200)
    int listFlagCount() override {
        // orig FUN_001f8c60 (work/decompiled/001f8c60.c): set flags of the -1 terminated list 0x49b950
        int n = 0;
        for (int i = 0; i < 0x143 && ui::kItemFlags[i] != -1; ++i)
            if (flags().test(static_cast<uint32_t>(ui::kItemFlags[i]))) ++n;
        return n;
    }
    void voice3d(int id) override {
        if (audio::SoundSystem* s = soundSystem()) s->playVoice(1.0f, id, false);   // FUN_00115970(1.0, snd, id, 0)
        else sound().playVoice(1.0f, id);
    }
    void voice(int id) override {
        if (audio::SoundSystem* s = soundSystem()) s->playVoiceDeferred(1.0f, id);  // FUN_00115a60(1.0, snd, id)
        else sound().playVoice(1.0f, id);
    }
    void bgmVolume(float v, int frames) override { sound().setBgmFadeTarget(v, frames); }   // FUN_00115f00
    int voiceStatus() override {
        if (audio::SoundSystem* s = soundSystem()) return s->voiceStatus();  // FUN_00115cd0
        return 0;   // UNVERIFIED: no sound system (stub) -> stopped
    }
    void voiceStop() override { if (audio::SoundSystem* s = soundSystem()) s->stopCurrentVoice(); }   // FUN_00115c10
    void voiceVolume(float v) override { if (audio::SoundSystem* s = soundSystem()) s->setVoiceVolume(v); }   // FUN_00115af0
    void se(int id) override { sound().playSe(1.0f, id); }
    void effect(int id, const float pos[4]) override {
        const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float scale[4] = {100.0f, 100.0f, 100.0f, 1.0f};
        effects().spawn(0.0f, id, pos, rot, scale);                        // FUN_00112190(0, id, pos, rot, scale)
    }
    void mainLightDir(float out[3]) override {
        const LightSlot& l = renderLights().slot[renderLights().mainSlot];
        for (int k = 0; k < 3; ++k) out[k] = l.dir[k];
    }
    void setMainLightDir(const float d[3]) override {
        LightSlot& l = renderLights().slot[renderLights().mainSlot];
        for (int k = 0; k < 3; ++k) l.dir[k] = d[k];
    }
    bool activeCameraExists() override { return gameActive(); }           // iRam004e7b18 != 0 (the Game camera task)
    void transition() override { eventManager().requestTransition(0.0f, 3); }   // FUN_00177cc0(0, 0x51b620, 3)
    bool loadFile(const char* dataPath, std::vector<uint8_t>& out) override {
        // FUN_0010cf60: the floor pack F%03d is consulted before the main tree
        data::Archive ar(platform::assetRoot());
        if (gameWorld().dungeon != nullptr) ar.setPack(data::Archive::packNameForFloor(gameProgress().floor));
        auto bytes = ar.load(dataPath);
        if (!bytes) {
            platform::log(std::string("talk: cannot open ") + dataPath);
            return false;
        }
        out = std::move(*bytes);
        return true;
    }
};

GameTalkWorld gGameWorld;
bool gInstalled = false;

}  // namespace

void talkInstallGame() {
    if (gInstalled) return;
    gInstalled = true;
    setTalkWorld(&gGameWorld);
    talkManager().construct();                                             // FUN_001a1b70 (static ctor FUN_004c7ed0)
    talkSelectHooks().npcExists = [](uint32_t id) { return talkManager().actorById(id) != nullptr; };   // FUN_001a9810
    talkSelectHooks().databaseCount = [] { return collectorBag().count; };   // 0x53dc00 +200
    std::function<void(bool)> prev = engine().eventFrame;
    engine().eventFrame = [prev](bool paused) {
        if (prev) prev(paused);                                            // FUN_0016d3b0(0x51b620, FUN_00102020())
        talkManager().taskUpdate();                                        // FUN_001a1dc0(0x53c6e0)
        // the readers' mirrors of the manager (HUD FUN_00150ba0, item screen FUN_00157a60 / FUN_0015c070)
        ItemWorldState& w = hudWorld();
        w.talkActive = talkManager().cameraAllowed();                      // FUN_001a2950(0x53c6e0)
        w.talkChoice = talkManager().state == 1;                           // iRam0053c6c8 == 1
        w.talkWindow = talkManager().noVoice != 0;                         // iRam0053c710 (+0x30)
        talkDevTickInternal();                                             // PORT: --talk (talk_dev.cpp)
    };
}

void talkItemTaken(Item* item) { talkNoteItemTaken(encodeItem(item)); }
void talkItemHandOver(Item* item, bool accepted) { talkNoteHandOver(encodeItem(item), accepted ? 1 : 0); }
void talkDatabasePut(Item* item) {
    // FUN_001a5330(0x53c6e0, item): +0xfc0 = item (the word is tested for != 0 only, FUN_001a3cc0 category 0x3c)
    talkNoteDatabase(item != nullptr ? 1u : 0u);
}

}  // namespace baroque::game
