// Character core (see character.hpp).
#include "game/character/character.hpp"
#include "game/items/chr_adapter.hpp"

#include "game/character/act_effects.hpp"
#include "game/character/act_props.hpp"
#include "game/character/combat.hpp"
#include "game/character/npc_class.hpp"
#include "game/character/player_control.hpp"

#include "alchemy/ps2_float.hpp"
#include "game/dungeon.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/items/item_screen.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/present/present_draw.hpp"
#include "game/respawn.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

#include "game/character/hidden_tables.inc"

ChrManager gChars;

uint32_t id4(const char* s) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(uint8_t(s[i])) << (8 * i);
    return v;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Player-class ids (factory table 0x44aa10 entries -> FUN_00130370, class vtable 0x4cccb0 on 0x4cde50)
bool isPlayerClass(uint32_t id) { return id == id4("C00A") || id == id4("C01A"); }

// Flying / gravity-free ids of FUN_001295e0 (strings 0x44a6c8.., compared as 3-char prefix or full id)
bool gravityFree(uint32_t id) {
    const uint32_t p3 = id & 0xffffff;
    return p3 == (id4("C02\0") & 0xffffff) || p3 == (id4("C03\0") & 0xffffff) || p3 == (id4("C05\0") & 0xffffff) ||
           p3 == (id4("C22\0") & 0xffffff) || id == id4("C04A") || id == id4("C04D") || id == id4("C04E") ||
           id == id4("C25B");
}

}  // namespace

ChrManager& characters() { return gChars; }
ThrowHooks& throwHooks() {
    static ThrowHooks h;
    return h;
}
std::function<void(Chr&)> playerControlHook;

std::string Chr::idString() const {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) s[size_t(i)] = char((id >> (8 * i)) & 0xff);
    return s;
}

const alchemy::Skeleton* ChrModel::skeleton() const {
    if (parts.empty() || !parts[0].model) return nullptr;
    return &parts[0].model->skeleton();
}

bool StateMachine::request(StateId s, int a, int prio) {
    // pattern used by every requester (FUN_001293f0, FUN_00129c60 knockback, FUN_0012a5e0): accept when nothing is
    // current/pending or the priority is high enough
    if ((curFactory == StateId::None && next == StateId::None) || priority < 1 || prio >= priority) {
        next = s;
        arg = a;
        priority = prio;
        priorityCopy = prio;
        return true;
    }
    return false;
}

// =============================================================================================================
// Status
// =============================================================================================================
// orig FUN_00133840: copy the PRMCHAR record into the StatusBlock
void statusCopyPrm(StatusObj& s) {
    StatusBlock& b = s.block;
    if (s.prm == nullptr) return;
    const data::PrmChar& p = *s.prm;
    b.hp = p.hpMax;
    b.hpMax = p.hpMax;
    b.vt = p.vtMax;
    b.vtMax = p.vtMax;
    b.atkMax = p.statA;
    b.atkBase = p.statA;
    b.atkLevel = 0;
    b.defMax = p.statB;
    b.defBase = p.statB;
    b.defLevel = 0;
    b.critical = p.f1c;
    b.expValue = p.f20;
    for (int i = 0; i < 3; ++i) b.element[size_t(i)] = p.v28[i];
    for (int i = 0; i < 3; ++i) b.directional[size_t(i)] = p.v34[i];
    for (int i = 0; i < 19; ++i) {
        b.resist[size_t(i)] = i < 0xe ? p.resist[i] : 0;
        b.timers[size_t(i)] = 0;
        b.effects[size_t(i)] = -1;
    }
    b.statusMask = 0;
    b.level = 0;
    b.exp = 0;
    b.expNext = 0;
    b.brand = 0;
}

// orig FUN_001339e0
void statusInit(StatusObj& s, Chr& owner) {
    s.owner = &owner;
    s.prm = characters().prm != nullptr ? data::findPrmChar(*characters().prm, owner.id) : nullptr;   // FUN_00133560
    s.block.hidden.fill(0);
    statusCopyPrm(s);
}

// orig FUN_001343f0 / FUN_00134470 (disasm 0x1343fc: FUN_00134310(base, 1) / (base, 2)): player =
// min(999, FUN_00134310(base, kind) + FUN_001d8290 / FUN_001d8330(0x53d910)) -- translated by the items module
// (game/items/chr_adapter.cpp statusHpMaxWithItems / statusVtMaxWithItems, equipment records + inventory terms)
int statusHpMaxTotal(const StatusObj& s) { return statusHpMaxWithItems(s); }
int statusVtMaxTotal(const StatusObj& s) { return statusVtMaxWithItems(s); }

// orig FUN_00138dc0: experience needed for level n
int statusExpForLevel(int n) {
    if (99 < n) n = 99;
    int k = 2, sum = 0, m = 6;
    do {
        sum += k * m;
        ++k;
        m += 3;
    } while (k <= n);
    return sum;
}

// orig FUN_00133a60 -> FUN_00133b10
void statusPlayerNew(StatusObj& s) {
    s.block.hidden.fill(0);
    StatusBlock& b = s.block;
    if (!flags().test(0x58)) {
        b.hp = 100;
        b.hpMax = 100;
        b.vt = 0x62;
        b.vtMax = 0x62;
        b.atkMax = 0x11;
        b.atkBase = 0x11;
        b.defMax = 0xb;
        b.defBase = 0xb;
    }
    statusAddHp(s, b.hpMax);
    statusAddVt(s, b.vtMax);
    b.atkLevel = 0x1a;
    b.defLevel = 0xe;
    b.level = 1;
    b.expNext = statusExpForLevel(b.level + 1);   // FUN_00138e00
}

// orig FUN_001395d0: hidden stats from achievement flags 0x483+k (table 0x48df70) and threshold flags (0x48e500)
void statusHiddenStats(StatusObj& s) {
    auto& h = s.block.hidden;
    h.fill(0);
    for (uint32_t k = 0; k < 0x40; ++k)
        if (flags().test(k + 0x483))
            for (int i = 0; i < 5; ++i) h[size_t(i)] += kHiddenBonus[k][i];
    for (const auto& t : kHiddenThreshold)
        if (t[1] < uint32_t(h[t[0]])) flags().set(t[2]);
}

// =============================================================================================================
// Factory / init
// =============================================================================================================
// orig FUN_0012d2f0 -> FUN_0012ce70 -> table 0x44aa10 ctor wrapper (FUN_0012e880 generic, FUN_00130370 player,
// FUN_00130060.. enemy classes, chrClassOf) -> FUN_001293f0, FUN_00128690, FUN_00128160
Chr* ChrManager::create(const char* idIn, bool addToList) {
    char s[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4 && idIn[i] != '\0'; ++i) {
        char ch = idIn[i];
        if ('`' < ch && ch < '{') ch = char(ch - 0x20);
        s[i] = ch;
    }
    if (s[3] == '\0') s[3] = 'A';
    return createTag(id4(s), addToList);
}

Chr* ChrManager::adopt(std::unique_ptr<Chr> c) {
    Chr* p = c.get();
    if (p == nullptr) return nullptr;
    if (p->serial == 0) p->serial = nextSerial_++;
    owned_.push_back(std::move(c));
    return p;
}

// orig FUN_0012ce70 (work/decompiled/0012ce70.c). Lists 0x50a7b0 (types 0..2, 0x20 entries, FUN_00128040) and
// 0x50a830 (items, 500, FUN_00128060 -> FUN_00128340 drops the item farthest from the player) are counted over the
// live entities of the manager; FUN_00128160 (per-type list insert) is implicit.
Chr* ChrManager::createTag(uint32_t id, bool addToList) {
    const char c0 = static_cast<char>(id & 0xff);
    Chr* c = nullptr;
    int type = 0;
    if (isItemId(id)) {                                     // FUN_001d38f0
        if (gameAdapter().floorItemCount() == 500) gameAdapter().dropFarthestItem();   // FUN_00128060 -> FUN_00128340 (list 0x50a830)
        auto e = itemEntityConstruct(id);                   // FUN_0012e810 (class 0x4cd960)
        c = adopt(std::move(e));
        type = kChrItem;                                    // unaff_s1 = 3
    } else {
        if (c0 != 'J' && c0 != 'O') {                       // FUN_00128040: list 0x50a7b0 full -> no entity
            int chars = 0;
            for (const auto& o : owned_)
                if (o->type <= kChrEnemy && o->obj == nullptr && o->item == nullptr && (o->taskFlags & 0x10000000u) == 0) ++chars;
            if (chars == 0x20) {
                // PORT diagnostics (sweep gap 9): the original drops the spawn silently
                platform::log("chr: spawn dropped " + tagString(id) + " (character list full, 0x20)");
                return nullptr;
            }
        }
        if (c0 == 'O' || c0 == 'J') {
            c = adopt(objectConstruct(id));                 // table 0x44aa10 J/O entries / 4th-letter classes
            type = kChrObject;                              // unaff_s1 = 4
        } else if (c0 == 'E' || c0 == 'C') {
            type = c0 == 'E' ? kChrEnemy : (isPlayerClass(id) ? kChrPlayerType : kChrNpc);
            if (type == kChrPlayerType) installCharacterRuntimeHooks();   // PORT: run-time hook installation
            auto owned = std::make_unique<Chr>();
            c = owned.get();
            owned_.push_back(std::move(owned));
            // FUN_001293f0 ctor: +0x24 = 0, +0x28 = 0x40000010, components, +0x30 = 0, +0x3c = -1, state -> Idle (LAB_001294f0)
            c->flags24 = 0;
            c->f28 = 0x40000010;
            c->serial = nextSerial_++;
            c->effectSlot = -1;
            physInit(c->phys, &c->xf.m.pos, &c->shape, 0.0f);
            c->sm.request(StateId::Idle, 0, 0);
            c->cls = chrClassOf(id);          // table 0x44aa10 -> ctor wrapper (class ctor fields: ChrClassData defaults)
        }
    }
    if (c == nullptr) {
        // PORT diagnostics (sweep gap 9): unknown prefix / no class constructed for the tag
        platform::log("chr: spawn dropped " + tagString(id) + " (no class for tag)");
        return nullptr;
    }
    if (addToList) link(c);                  // FUN_002073e0(ent, 0x50b134, 0x50b138)
    c->taskFlags |= 1u;                      // task linked / alive (FUN_002072b0 + FUN_002073e0; tested by FUN_00207350)
    c->type = type;                          // FUN_00128690: +0x18 type, +0x1c id, vt+0x1c
    c->id = id;
    if (type == kChrItem) itemEntityInit(*c);          // FUN_001d3d40
    else if (type == kChrObject) objectInit(*c);       // class init (vt+0x1c)
    else chrInitById(*c);                              // FUN_001295e0
    if (type <= kChrEnemy) floorTagInsert(id);         // FUN_00128160: kind 0 (types 0..2) -> FUN_00127ea0
    return c;
}


// orig FUN_0012bfd0: every listed character except the player is marked removed (FUN_00207350) and unlinked; the
// per-tag floor counts 0x50a710 and their model caches (FUN_0012b7f0) are released.
void ChrManager::reap() {
    std::vector<Chr*> keep;
    for (Chr* c : list_) {
        if ((c->taskFlags & 0x10000000u) != 0 && c != player_) {
            if (c->obj != nullptr) objectDestroy(*c);                          // class dtor (vtable +8)
            else if (c->type == kChrNpc) npcClassDestroy(*c);
            if (c->effectSlot >= 0) hitManager().removeVictim(c->effectSlot);   // FUN_0014ace0
            hitManager().forget(c);
            if (c->type >= kChrPlayerType && c->type <= kChrEnemy) floorTagRemove(c->id);   // FUN_00128610 -> FUN_00128230
            continue;
        }
        keep.push_back(c);
    }
    if (keep.size() == list_.size()) return;
    list_ = std::move(keep);
    std::erase_if(owned_, [this](const std::unique_ptr<Chr>& o) {
        return std::find(list_.begin(), list_.end(), o.get()) == list_.end() && o.get() != player_;
    });
}

void ChrManager::removeAllButPlayer() {
    std::vector<Chr*> keep;
    for (Chr* c : list_) {
        if (c == player_) {
            keep.push_back(c);
            continue;
        }
        if (c->obj != nullptr) objectDestroy(*c);
        else if (c->type == kChrNpc) npcClassDestroy(*c);
        if (c->effectSlot >= 0) hitManager().removeVictim(c->effectSlot);
        hitManager().forget(c);
        if (c->type >= kChrPlayerType && c->type <= kChrEnemy) floorTagRemove(c->id);   // FUN_00128610 -> FUN_00128230
    }
    list_ = std::move(keep);
    for (auto& o : owned_)
        if (o.get() != player_) o.reset();
    std::erase_if(owned_, [](const std::unique_ptr<Chr>& o) { return o == nullptr; });
}

void ChrManager::destroyAll() {
    hitManager().reset();
    list_.clear();
    owned_.clear();
    player_ = nullptr;
}

// orig FUN_001295e0
void chrInitById(Chr& c) {
    statusInit(c.status, c);                                                  // FUN_001339e0
    const data::PrmChar* p = c.status.prm;
    const float radius = p != nullptr ? p->radius : 0.0f;
    // FUN_00142e10: physics pointers, radius, centre offset (0, radius, 0)
    c.phys.pos = &c.xf.m.pos;
    c.phys.move = &c.phys.vel;
    c.phys.radius = radius;
    c.phys.centreOffset = {0.0f, radius, 0.0f};
    c.phys.result = 0;
    c.phys.shape = &c.shape;
    c.phys.ownerForward = c.xf.m.fwd.data();
    if (p != nullptr) {
        c.shape.owner = &c;
        if (p->f9c == 0.0f || p->fa4 < 0.0f) {
            // FUN_00142200: cylinder (radius f9c, height a0), a = b = -1
            c.shape.radius = p->f9c;
            c.shape.height = p->height;
            c.shape.a = -1.0f;
            c.shape.b = -1.0f;
        } else {
            // FUN_00142230: box half extents (f9c, fa4), radius = |(a,b)|
            c.shape.height = p->height;
            c.shape.a = p->f9c;
            c.shape.b = p->fa4;
            c.shape.radius = std::sqrt(p->f9c * p->f9c + p->fa4 * p->fa4);
        }
        c.shape.top = c.shape.height + c.xf.m.pos[1];                           // FUN_00142270
            if (0.0f <= p->throwSpread) {   // +0xa8 >= 0: hurt volume (+0xb0 < 0 cylinder, else box)
            if (p->fb0 < 0.0f) c.effectSlot = hitManager().addCylinder(p->throwSpread, p->iconHeight, &c);   // FUN_0014abb0
            else c.effectSlot = hitManager().addBox(p->iconHeight, p->throwSpread, p->fb0, &c);              // FUN_0014ac40
        }
    }
    if (c.id == id4("E03B") || c.id == id4("E09B")) c.xf.scale = {0.5f, 0.5f, 0.5f};
    // LAB_001299bc: no gravity when the push radius is 0 or for the gravity-free ids; every path then reaches the AI init
    if (radius == 0.0f || gravityFree(c.id)) c.phys.gravity = 0.0f;
    aiInit(c.ai, c);                                                          // FUN_001f0630(ai, chr)
}

// =============================================================================================================
// Loading (Idle state entry FUN_001cc390 / FUN_001cc480, player activation FUN_001b1350)
// =============================================================================================================
// orig FUN_001442a0 (ACT cache 0x50c6e0): DATA/ACTION/%c%c%c%c.ACT; the port loads synchronously
bool chrLoadAct(Chr& c) {
    if (c.actLoaded) return true;
    const auto path = characters().assetRoot / ("DATA/ACTION/" + c.idString() + ".ACT");
    std::string err;
    auto f = data::parseAct(readFile(path), &err);
    if (!f) {
        platform::log("chr " + c.idString() + ": ACT " + err);
        return false;
    }
    c.actFile = std::move(*f);
    c.actLoaded = true;
    c.act.init(&c.actFile, &c.model.anim, &c.phys, nullptr, &c.status.block.statusMask);
    Chr* self = &c;
    c.act.extension = [self](ActRunner& r, const data::ActCmnd& cmd) { actCombatCommand(*self, r, cmd); };
    c.act.releaseHits = [self] { hitManager().removeOwner(self, 1); };   // FUN_0014ded0(0x50c780, chr, 1)
    c.act.onReset = [self] {                                            // FUN_00143bf0 parts
        actPropsResetTrailsLook(*self);                                 //   FUN_00145660, FUN_001459c0
        self->actTurn = ActTurn{};                                      //   thunk_FUN_001460d0
        actPropsResetItems(*self, self->act.flags());                   //   ShowItem prop out, status +0x10 bit 0
    };
    c.act.owner = self;                                                 // ACT player +4
    c.act.attackRateInputs = [self](AttackRateInputs& in) {             // FUN_00146f90 inputs
        const StatusBlock& b = self->status.block;
        if (combatHooks().attackRateItems) combatHooks().attackRateItems(b, in);
        in.st19Brand = (b.statusMask & 0x10u) != 0 && b.brand == 0x39315453u;   // +0x3c & 0x10, +0x54 == "ST19" (0x44b788)
        in.hidden0 = b.hidden[0];                                       // *(+0x58)
    };
    return true;
}

bool chrLoadPart(ChrModelPart& part, const std::filesystem::path& path, const std::string& bone) {
    part.file = std::make_unique<alchemy::IgbFile>();
    std::string err;
    if (!part.file->load(path, &err)) {
        platform::log("chr model " + path.string() + ": " + err);
        return false;
    }
    part.lib = std::make_unique<alchemy::TextureLibrary>();
    part.lib->addFile(*part.file, nullptr);
    part.model = std::make_unique<alchemy::SceneModel>();
    part.model->build(*part.file, {part.lib.get()}, nullptr);
    part.attachBone = bone;
    return true;
}

// FUN_001cc480: FUN_001099e0(model, FUN_0012b620(id) = "DATA/CHR/%c%c%c/%c%c%c%c.IGB" (E03B/E09B/E18B use the A
// model), 1, chr+0x28)
bool chrLoadModel(Chr& c) {
    if (c.model.loaded()) return true;
    const std::string id = c.idString();
    const auto root = characters().assetRoot;
    ChrModelPart main;
    if (!chrLoadPart(main, root / chrModelPath(c.id), "")) return false;   // FUN_0012b620
    c.model.parts.push_back(std::move(main));
    std::vector<std::string> log;
    c.model.clips = alchemy::loadClips(*c.model.parts[0].file, alchemy::primarySkeleton(*c.model.parts[0].file), &log);
    c.model.anim.init(&c.model.clips, c.model.skeleton());
    actorLoadDefaultAnim(c.model.anim, c.model.clips, *c.model.parts[0].file);   // FUN_0010a4d0: first animation
    {   // the "lod" skin of the animation database (FUN_00108d20 swaps the appearance skin to it beyond 1200)
        ChrModelPart& body = c.model.parts[0];
        auto lod = std::make_unique<alchemy::SceneModel>();
        if (lod->buildSkin(*body.file, "lod", {body.lib.get()}, nullptr)) body.lodModel = std::move(lod);
    }
    modelShadowOn(c.model, false);    // FUN_001099e0 mode 1: FUN_00109f10(model, 0)
    c.model.lit = true;               // parent 0x4e78f4 (lit branch)
    c.model.light.exists = (c.f28 & 0x4000000u) == 0;   // own light group unless flag 0x4000000
    c.model.flags2c |= 0x80000000u;   // FUN_00109c80 load callback
    return true;
}

// Player class activation (vt+0x30 = FUN_001b1350): BODY (0x462830) and HAIR (0x462810) skins attached to the actor,
// HEAD (0x462850) as a separate model on the head bone. UNVERIFIED: bone table 0x462920 / FUN_001b14e0 not traced.
namespace {
void loadPlayerParts(Chr& c) {
    const std::string id = c.idString();
    const auto dir = characters().assetRoot / ("DATA/CHR/" + id.substr(0, 3));
    for (const char* suffix : {"_BODY", "_HAIR"}) {
        ChrModelPart p;
        if (chrLoadPart(p, dir / (id + suffix + ".IGB"), "")) {
            if (const alchemy::Skeleton* sk = c.model.skeleton()) p.model->adoptSkeleton(*sk);
            c.model.parts.push_back(std::move(p));
        }
    }
    ChrModelPart head;
    if (chrLoadPart(head, dir / (id + "_HEAD.IGB"), "Head")) {
        // the head is its own actor (FUN_001b1350: FUN_001099e0(+0x40, 0x462850, 1, 0x11)) with its face animations
        head.actor = std::make_shared<PartActor>();
        std::vector<std::string> log;
        head.actor->clips = alchemy::loadClips(*head.file, alchemy::primarySkeleton(*head.file), &log);
        head.actor->anim.init(&head.actor->clips, &head.model->skeleton());
        actorLoadDefaultAnim(head.actor->anim, head.actor->clips, *head.file);   // FUN_0010a4d0: first animation
        c.model.parts.push_back(std::move(head));
    }
}
}  // namespace

// orig FUN_001299f0 (vt+0x30 of the generic class and E23); the player class's vt+0x30 is FUN_001b1350, the NPC
// classes' FUN_001b39f0 (UNVERIFIED: NPC activation not translated, only the active bit is set). The bespoke enemy
// classes dispatch from chrClassActivate (enemy_classes.cpp).
void chrActivate(Chr& c) {
    if (c.cls == ChrClass::Player && c.model.parts.size() == 1) loadPlayerParts(c);
    c.flags24 |= 1u;
    if (c.type == kChrNpc) npcTypeActivate(c);   // type 1: FUN_001b3220
}

// =============================================================================================================
// States (Idle only so far)
// =============================================================================================================
namespace {
// Idle state (factory LAB_001294f0 -> FUN_0012e0c0, vtable 0x4cda90): enter FUN_001cc390, update PTMF chain
// FUN_001cc420 -> FUN_001cc480 -> FUN_001cc510.
void idleUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    if (sm.phase == 0) {                 // FUN_001cc390 (enter): load ACT (sync when chr+0x28 & 1)
        chrLoadAct(c);
        sm.phase = 1;
    }
    if (sm.phase == 1) {                 // FUN_001cc420: wait for the ACT cache slot, then FUN_001cc480
        if (!c.actLoaded) return;
        chrLoadModel(c);                 // FUN_001cc480 -> FUN_001099e0
        sm.phase = 2;
    }
    if (sm.phase == 2) {                 // FUN_001cc510: when the model is loaded
        if (!c.model.loaded()) return;
        sm.phase = 3;
        // state finished: if nothing pending, pop (+0x18 = -1)
        if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 1) sm.popRequested = true;
        chrClassActivate(c);             // vt+0x30
    }
}
}  // namespace

// =============================================================================================================
// Reaction and death states (factories LAB_0012a490 flinch -> class 0x4cda50, LAB_0012a4a0 knockback 0x4cda70,
// LAB_0012ab00 pull 0x4cd990, LAB_0012a700 death 0x4cd9e0, LAB_0012a6f0 vanish death 0x4cd9b0; enter = vt+0x1c,
// exit = vt+0x14, update = vt+0x18). Strings 0x463c38 "DM01", 0x463c40 "DM02", 0x463c60 "DD01", 0x463c68 "E23",
// 0x463c70 "DD02", 0x463c78 "E18", 0x463c80 "E03B", 0x463c88 "E09B"; 0x463c48 = 5.0f (pull speed).
// =============================================================================================================
namespace {

// "_E" + ('1' + MODE) for enemies (FUN_001f1730(ai, 0x1000008)), FUN_0013a4b0 weapon suffix for the player
std::string reactionName(Chr& c, const char* base4) {
    if (&c == characters().player()) {
        const int wc = combatHooks().weaponClass ? combatHooks().weaponClass() : -1;
        return std::string(base4) + actionSuffix(wc);
    }
    const uint32_t mode = (c.ai.flags & 6u) >> 1;
    return std::string(base4) + "_E" + static_cast<char>('1' + mode);
}

// state pop pattern (+0x18 = -1, +8/+0xc/+0x10 = 0) when nothing pending or the priority is < 1
void statePop(Chr& c) {
    StateMachine& sm = c.sm;
    if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 1) {
        sm.next = StateId::None;
        sm.popRequested = true;
        sm.arg = 0;
        sm.priority = 0;
        sm.priorityCopy = 0;
    }
}

void fxAt(int fx, const Vec3f& pos, const Vec3f* rot = nullptr) {
    if (combatHooks().effect) combatHooks().effect(fx, pos, rot, 100.0f);
}

// ---- flinch (FUN_001cc5d0 enter, FUN_001cc700 exit, FUN_001cc710 update) ----
void flinchEnter(Chr& c) {
    const bool pl = &c == characters().player();
    if (c.phys.knock[0] < 0.0f) {
        if (pl && playerCtl() != nullptr) playerCtl()->flags &= 0xdfffffffu;   // FUN_0013a800 (+ pad vibration)
    } else {
        if (pl && playerCtl() != nullptr) playerCtl()->flags &= 0xdfffffffu;   // FUN_0013a790 (+ pad vibration)
        c.act.start(reactionName(c, "DM01"));                                   // FUN_00143aa0
    }
    c.phys.moveVel = {0.0f, 0.0f, 0.0f};
    c.sm.timer = 10;
}

void flinchUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    const char* cur = c.act.currentName();
    if (cur == nullptr || std::strncmp(cur, "DM01", 4) != 0) {
        if (sm.timer == 0) statePop(c);
    }
    if (sm.timer == 0) return;
    const float t = static_cast<float>(static_cast<uint32_t>(sm.timer));
    Vec3f j;
    j[0] = t * (static_cast<float>(gameWorld().rng.rand(0x8000)) / 32768.0f + -0.5f);
    j[1] = t * (static_cast<float>(gameWorld().rng.rand(0x8000)) / 32768.0f + -0.5f) * 0.5f;
    j[2] = t * (static_cast<float>(gameWorld().rng.rand(0x8000)) / 32768.0f + -0.5f);
    // player: FUN_0013f7b0(camera, &j) camera shake (not translated)
    j = {j[0] + c.xf.m.pos[0], j[1] + c.xf.m.pos[1], j[2] + c.xf.m.pos[2]};
    if (--sm.timer == 0) statePop(c);
    if (&c == characters().player() && playerCtl() != nullptr && (playerCtl()->flags & 0x20u) != 0) return;
    if (c.model.loaded()) modelPublishFx(c.model);   // FUN_00109d40 -> FUN_0010a160
    c.jitter = true;                 // FUN_00109d40(model, jittered pos, axes) + flags24 |= 4
    c.jitterPos = j;
    c.flags24 |= 4u;
}

// ---- knockback (FUN_001ccb00 / FUN_001ccca0 / FUN_001ccd20) ----
void knockbackEnter(Chr& c) {
    c.act.start(reactionName(c, "DM02"));
    StateMachine& sm = c.sm;
    sm.dir = c.phys.knock;
    sm.speed = normalize(sm.dir);
    fxAt(0xdb, c.xf.m.pos);
    if (&c == characters().player() && playerCtl() != nullptr) playerCtl()->flags |= 0x20000000u;   // FUN_0013a870
    sound().playSe3d(1.0f, 0xaa, c.xf.m.pos.data());
}
void knockbackExit(Chr& c) {
    c.phys.moveVel = {0.0f, 0.0f, 0.0f};
    if (&c == characters().player() && playerCtl() != nullptr) playerCtl()->flags &= 0xcfffffffu;   // FUN_0013a940
    c.act.stop();
}
void knockbackUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    const float s = sm.speed;
    c.phys.moveVel = {sm.dir[0] * s, sm.dir[1] * s, sm.dir[2] * s};
    sm.speed = sm.speed - 1.0f;
    chrCommonTick(c);
    if (0.0f <= sm.speed) return;   // effect follows the character (FUN_001122d0)
    statePop(c);
}

// ---- pull (FUN_001cce20 / FUN_001ccee0 / FUN_001ccf00) ----
void pullEnter(Chr& c) {
    StateMachine& sm = c.sm;
    sm.dir = {c.phys.knock[0] * 5.0f, c.phys.knock[1] * 5.0f, c.phys.knock[2] * 5.0f};
    sm.timer = 10;
    fxAt(0xdb, c.xf.m.pos);
}
void pullExit(Chr& c) { c.phys.moveVel = {0.0f, 0.0f, 0.0f}; }
void pullUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    c.phys.moveVel = sm.dir;
    chrCommonTick(c);
    if (--sm.timer != 0) return;
    statePop(c);
}

// ---- death (FUN_001ccfe0 enter, FUN_001cd4b0 -> PTMF FUN_001cd4f0 update) ----
void deathEnter(Chr& c, bool vanish) {
    StateMachine& sm = c.sm;
    c.phys.moveVel = {0.0f, 0.0f, 0.0f};
    sm.noDrop = 0;
    sm.vanish = 0;
    sm.deathCont = 0;
    sm.gameOver = false;
    if (vanish) {                // vt+0x24 = FUN_001cdd60: +0x20 = 1, red glow FUN_0010a0a0(actor, 0x11, 3, from, to)
        sm.vanish = 1;
        const float from[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float to[4] = {1.0f, 0.15f, 0.05f, 0.0f};   // 0x3e19999a, 0x3d4ccccd
        modelFadeStart(c.model, 0x11, 3, from, to);
    }
    std::string name = "DD01";
    const uint32_t t3 = c.id & 0xffffffu;
    if (&c == characters().player()) {
        name = reactionName(c, "DD01");
        if (playerCtl() != nullptr) playerCtl()->flags &= 0xdfffffffu;   // FUN_0013a8d0
        sound().playSe(1.0f, 0x46);
        if (playerCtl() != nullptr && (playerCtl()->flags & 0x20u) != 0) {   // first person: model hidden
            const float k[4] = {1e-4f, 1e-4f, 1e-4f, 1e-4f};                // 0x38d1b717
            modelFadeStart(c.model, 0, 0, k, k);                            // FUN_0010a0a0(model, 0, 0, k, k)
        }
    } else {
        cureAllStatus(c.status);   // FUN_00138ac0
        if (t3 == (id4("E23\0") & 0xffffffu) && c.deathVariant != 0) {
            name = "DD02";
            sm.noDrop = 1;
        }
        const uint32_t mode = (c.ai.flags & 6u) >> 1;
        name += std::string("_E") + static_cast<char>('1' + mode);
        if (sm.vanish == 0 && sm.noDrop == 0) {
            float h = 0.0f;
            if (const data::PrmChar* p = c.status.prm) {
                if (t3 == (id4("E18\0") & 0xffffffu)) h = p->iconHeight / 1.5f;
                else if (t3 == (id4("E23\0") & 0xffffffu)) h = p->iconHeight / 8.0f;
                else h = p->iconHeight / 3.0f;
            }
            const Vec3f at{c.xf.m.pos[0], h + c.xf.m.pos[1] + 1.0f, c.xf.m.pos[2]};
            const Vec3f rot{0.0f, 0.0f, 0.0f};
            const bool small = c.id == id4("E03B") || c.id == id4("E09B") || t3 == (id4("E23\0") & 0xffffffu);
            fxAt(small ? 0xeb : 0x15, at, &rot);
        }
    }
    c.act.start(name);
    sm.timer = -3;
    sm.spinA = 0.0f;
    sm.spinB = 0.0f;
}

// orig FUN_0013de20: effect 0x68 at the player's "Head" joint (0x44b260): joint world matrix, diagonal set to 100
// (FUN_0039dc10)
void reviveFx(Chr& c) {
    if (!combatHooks().effect) return;
    const alchemy::Skeleton* sk = c.model.skeleton();
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    Vec3f at = c.xf.m.pos;
    if (sk != nullptr)
        for (size_t b = 0; b < sk->bones.size() && b < pose.size(); ++b)
            if (sk->bones[b].name == "Head") {
                const alchemy::Mat4 w = alchemy::mul(pose[b], chrActorMatrix(c));
                at = {w.m[12], w.m[13], w.m[14]};
            }
    // UNVERIFIED: the effect hook takes a position (the joint's rotation part of the matrix is not passed)
    combatHooks().effect(0x68, at, nullptr, 100.0f);
}

// orig FUN_001cd9d0, ST05 branch: brand -> "ST09" (FUN_001391d0), FUN_0013de20, action "DD02" + suffix (0x463c70),
// SE 0x47, every status cured (FUN_00138ac0), PTMF FUN_001cdbe0 (0x463ca8)
void reviveStart(Chr& c) {
    c.status.block.brand = 0x39305453u;   // "ST09" (0x463cd0); FUN_001391d0 sets flag 0x3cc only for "ST19"
    reviveFx(c);
    c.act.start(reactionName(c, "DD02"));
    sound().playSe(1.0f, 0x47);           // FUN_00115390(1.0, 0x4e9790, 0x47)
    cureAllStatus(c.status);
    c.sm.deathCont = 2;
}

// orig FUN_001cdbe0: when the action has ended: HP and VT to their maxima (FUN_001349e0 / FUN_00134b60 with
// FUN_001343f0 / FUN_00134470), then the state pops with priority 1 (+0x18 = -1, +8 = 0, +0xc = +0x10 = 1)
void reviveUpdate(Chr& c) {
    if (c.act.currentName() != nullptr) return;
    if (playerCtl() != nullptr && (playerCtl()->flags & 0x20u) != 0) modelFadeEnd(c.model);   // FUN_0010a130
    statusAddHp(c.status, statusHpMax(c.status));
    statusAddVt(c.status, statusVtMaxTotal(c.status));
    StateMachine& sm = c.sm;
    sm.deathCont = 0;
    if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority <= 1) {
        sm.next = StateId::None;
        sm.popRequested = true;
        sm.arg = 0;
        sm.priority = 1;
        sm.priorityCopy = 1;
    }
}

void deathUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    const bool pl = &c == characters().player();
    if (pl && sm.deathCont == 2) {        // PTMF FUN_001cdbe0
        reviveUpdate(c);
        return;
    }
    if (pl && sm.deathCont == 1) {        // PTMF FUN_001cd9d0 (0x463c90), set by the update below one tick earlier
        if (c.status.block.brand == 0x35305453u) {   // "ST05" (0x463cc8)
            reviveStart(c);
            return;
        }
        sm.gameOver = true;               // fade-out branch: Game scene gameOverTick (FUN_001cd9d0 / FUN_001cdcd0)
        return;
    }
    if (pl && sm.gameOver) return;
    if (c.act.currentName() == nullptr) {
        if (pl) {
            sm.deathCont = 1;
        } else if ((c.taskFlags & 0x10000000u) == 0) {
            if (sm.vanish == 0) {   // vt+0x20 = FUN_001cd9a0: drop unless +0x24
                if (sm.noDrop == 0 && combatHooks().dropItem) combatHooks().dropItem(c);   // FUN_00136d30
            } else {                // FUN_001cddd0: vanish effect 0x12
                const Vec3f at{c.xf.m.pos[0], c.xf.m.pos[1] + 1.0f, c.xf.m.pos[2]};
                const Vec3f rot{0.0f, 0.0f, 0.0f};
                fxAt(0x12, at, &rot);
            }
            c.taskFlags |= 0x10000000u;   // FUN_00207350
        }
    }
    if (!pl) {
        int f = sm.timer;
        if (f < 1) {
            if (f < 0) {
                sm.timer = f + 1;
                if (f + 1 == -1) {
                    if ((c.id & 0xffffffu) == (id4("E18\0") & 0xffffffu)) sm.timer = 0x2e;
                    else sm.timer = static_cast<int>(c.model.anim.durationNs() / alchemy::enbaya::kFrameNs);
                    if (sm.timer < 0xb) sm.timer = 1;
                    else sm.timer = sm.timer - 10;
                }
            }
        } else {
            sm.timer = f - 1;
            if (f - 1 == 0) {   // FUN_0010a0a0(actor, 0x10, 10, from, (0, 0, 0, 1)): flickering 10-frame fade out
                const float from[4] = {sm.vanish == 0 ? 0.5f : 1.0f, 0.75f, sm.vanish == 0 ? 1.0f : 0.5f, 1.0f};
                const float to[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                modelFadeStart(c.model, 0x10, 10, from, to);
            }
        }
        if (sm.timer == 0 && sm.vanish != 0) {
            Vec3f& s = c.xf.scale;
            s[0] = (s[0] + 0.0f) - s[0] * 0.25f;
            s[1] = s[1] * 1.1f;
            s[2] = (s[2] + 0.0f) - s[2] * 0.25f;
        }
    }
    if (sm.vanish != 0) {
        modelPublishFx(c.model);   // FUN_00109d40 (vanish branch publishes the spun model itself)
        // spin presentation (FUN_00206900 sin of +0x18/+0x1c into the model matrix): angles advance with two draws
        if (sm.timer != 0) {
            sm.spinA = static_cast<float>(alchemy::ps2::ftoi(sm.spinA + static_cast<float>(gameWorld().rng.rand(0x8000)) / 32768.0f + 0.05f));
            const float b = sm.spinB + static_cast<float>(gameWorld().rng.rand(0x8000)) / 32768.0f + 0.05f;
            sm.spinB = static_cast<float>(alchemy::ps2::ftoi(b));
            if (3.1415927f < b) sm.spinB = static_cast<float>(alchemy::ps2::ftoi(b - 6.2831855f));
            if (3.1415927f < sm.spinA) sm.spinA = static_cast<float>(alchemy::ps2::ftoi(sm.spinA - 6.2831855f));
        }
    }
}

// ---- ThrowLock (LAB_0013cf80 -> FUN_0013df10, vtable 0x4cdb50; strings 0x463d08 "TH00", 0x463cd8 "_00",
//      0x463cf0 "Right_WEAPON00_A") ----
// orig FUN_001ce310 -> FUN_001cdfe0(state, selected inventory item, "TH00") (+ FUN_001cdf80 / FUN_001b2980)
void throwLockEnter(Chr& c) {
    StateMachine& sm = c.sm;
    sm.throwItem = hooks::inventoryItem(hooks::itemSlotSelected());   // *(FUN_001573f0(0x517b60) * 4 + 0x53d914)
    const bool gun = sm.throwItem != nullptr && throwHooks().itemClass && throwHooks().itemClass(sm.throwItem) == 0xc;
    if (&c == characters().player()) {                                 // FUN_001cdf80
        hooks::heldItemModelHide(c, 0, gun ? 1 : 0);
        hooks::heldItemModelHide(c, 1, 0);
    }
    c.phys.moveVel = {0.0f, 0.0f, 0.0f};                               // phys +0x7c..+0x84
    c.act.start("TH00_00");                                            // FUN_00143aa0
    // item model FUN_001d34b0(class) under a joint attach on "Right_WEAPON00_A" (+8/+0xc) -- the item models are
    // drawn by the item system (UNVERIFIED: not attached here); +0xc non-null marks the item as still held
    sm.throwHeld = true;
}
// orig FUN_001ce370 -> FUN_001ce200(state, selected item, 0x50b644 aim direction): FUN_001d5640(item, ACT +0x50,
// dir, owner) at the ACT release frame
void throwLockUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    if (c.act.currentName() != nullptr) {                              // ACT +0xc
        if (!sm.throwHeld) return;
        if ((c.act.flags() & 0x100000u) == 0) return;                  // ACT +0x1c release frame
        const Vec3f dir = playerCtl() != nullptr ? playerCtl()->aimDir : Vec3f{};
        if (sm.throwItem != nullptr) {
            const Vec3f& at = c.act.objectPos;
            throwHeldItem(*const_cast<Item*>(static_cast<const Item*>(sm.throwItem)), Vec3{at[0], at[1], at[2]},
                          Vec3{dir[0], dir[1], dir[2]}, chrAdapter(&c), itemSystem());
        }
        sm.throwHeld = false;                                          // FUN_001ce180 (detach and release)
        return;
    }
    statePop(c);
}

// ---- item screen menu actions (vtables 0x4cede0 Throw / 0x4cee00 Place / 0x4cedc0 Give, base FUN_001cde50) ----
// enter FUN_001ce2c0 / FUN_001ce3c0 / FUN_001ce4f0 -> FUN_001cdfe0(state, uRam0053d9c4 (inventory +0xb4 item in use),
// "TH00" / "TH06" / "TH07") with the "_00" suffix (0x463cd8); exit thunk FUN_001cdf30 (= throwLockExit).
void menuActionEnter(Chr& c, const char* act4) {
    StateMachine& sm = c.sm;
    sm.throwItem = inventory().usingItem;
    const bool gun = sm.throwItem != nullptr && throwHooks().itemClass && throwHooks().itemClass(sm.throwItem) == 0xc;
    if (&c == characters().player()) {                                 // FUN_001cdf80
        hooks::heldItemModelHide(c, 0, gun ? 1 : 0);
        hooks::heldItemModelHide(c, 1, 0);
    }
    c.phys.moveVel = {0.0f, 0.0f, 0.0f};
    c.act.start(std::string(act4) + "_00");                            // FUN_00143aa0
    sm.throwHeld = true;                                               // +0xc (item model attached, UNVERIFIED as above)
}
// update FUN_001ce2f0 (Throw: FUN_001ce200(state, item, chr forward)), FUN_001ce3f0 (Place: FUN_001d5920(item, ACT
// +0x50, forward * 10.0 (0x463d18))), FUN_001ce520 (Give: first FUN_00128fc0(90, chr, dir to uRam0053d9c8, 1), then
// FUN_001d6840 + FUN_001d91a0). The release itself is MenuItemAction::tick of the item screen (items module).
void menuActionUpdate(Chr& c, bool give) {
    StateMachine& sm = c.sm;
    if (give) {
        if (const CharacterAccess* t = inventory().placeTarget) {      // iRam0053d9c8
            const Vec3 tp = t->position();
            Vec3f d{tp.x - c.xf.m.pos[0], 0.0f, tp.z - c.xf.m.pos[2]};
            normalize(d);                                              // FUN_00206ab0
            turnToward(90.0f, c.xf.m, d, true);                        // FUN_00128fc0(90.0, chr, dir, 1)
        }
    }
    if (c.act.currentName() != nullptr) {
        if (!sm.throwHeld) return;
        if ((c.act.flags() & 0x100000u) == 0) return;
        const Vec3f& at = c.act.objectPos;
        itemScreen().menuAction().tick(itemSystem(), true, Vec3{at[0], at[1], at[2]});
        sm.throwHeld = false;                                          // FUN_001ce180
        return;
    }
    statePop(c);
}
// orig FUN_001ce360 -> FUN_001cdf30 (exit): the hand models come back
void throwLockExit(Chr& c) {
    if (&c == characters().player()) {
        hooks::heldItemModelShow(c, 0);
        hooks::heldItemModelShow(c, 1);
    }
    c.sm.throwHeld = false;                                            // dtor FUN_001cdea0 -> FUN_001ce180
    c.sm.throwItem = nullptr;
}

void stateEnter(Chr& c, StateId s) {
    switch (s) {
    case StateId::ThrowLock: throwLockEnter(c); break;
    case StateId::MenuThrow: menuActionEnter(c, "TH00"); break;   // 0x463d08
    case StateId::MenuPlace: menuActionEnter(c, "TH06"); break;   // 0x463d10
    case StateId::MenuGive: menuActionEnter(c, "TH07"); break;    // 0x463d20
    case StateId::HitStagger: flinchEnter(c); break;
    case StateId::Knockback: knockbackEnter(c); break;
    case StateId::Pull: pullEnter(c); break;
    case StateId::DeathA: deathEnter(c, false); break;
    case StateId::DeathB: deathEnter(c, true); break;
    case StateId::Event: playerEventEnter(c); break;
    case StateId::TypeId: typeIdEnter(c); break;   // FUN_001b2f40 (player_event.cpp)
    default: break;
    }
}
void stateExit(Chr& c, StateId s) {
    switch (s) {
    case StateId::Knockback: knockbackExit(c); break;
    case StateId::Pull: pullExit(c); break;
    case StateId::ThrowLock:
    case StateId::MenuThrow:
    case StateId::MenuPlace:
    case StateId::MenuGive: throwLockExit(c); break;   // FUN_001cdf30 (+ dtor FUN_001ce180)
    case StateId::Event: playerEventExit(c); break;
    case StateId::DeathA:
    case StateId::DeathB:
        // death FUN_001cd4a0 is empty, but the state object is freed right after (FUN_001304b0) and with it the
        // death continuation PTMF (+8, 0x463c90 / 0x463cb8) the port keeps as sm.deathCont / sm.gameOver. Without this
        // the rebirth path FUN_0011ce60 (flag 0x21: FUN_0011a8a0 pops the state, floor 0x5f) kept gameOver set and
        // gameOverTick re-ran FUN_001cd9d0 / FUN_001cdcd0 on every new floor 0x5f: endless reload loop
        // (PLAYTHROUGH_FULL_2026-10-10.md B1).
        c.sm.deathCont = 0;
        c.sm.gameOver = false;
        break;
    default: break;   // flinch FUN_001cc700 is empty
    }
}
void stateUpdate(Chr& c) {
    switch (c.sm.cur) {
    case StateId::Idle: idleUpdate(c); break;
    case StateId::HitStagger: flinchUpdate(c); break;
    case StateId::Knockback: knockbackUpdate(c); break;
    case StateId::Pull: pullUpdate(c); break;
    case StateId::DeathA:
    case StateId::DeathB: deathUpdate(c); break;
    case StateId::ThrowLock: throwLockUpdate(c); break;
    case StateId::MenuThrow:
    case StateId::MenuPlace: menuActionUpdate(c, false); break;
    case StateId::MenuGive: menuActionUpdate(c, true); break;
    case StateId::Event: playerEventUpdate(c); break;
    case StateId::TypeId: typeIdUpdate(c); break;   // FUN_001b2ff0 (player_event.cpp)
    default: break;
    }
}

}  // namespace

// orig FUN_00129a30. The task walk calls vt+0xc; item entities (FUN_001d5180) and object classes have their own.
void chrUpdate(Chr& c) {
    if (c.item != nullptr) {
        itemEntityTick(c);
        return;
    }
    if (c.obj != nullptr) {
        objectTick(c);
        return;
    }
    if ((c.flags24 & 0x10000000u) != 0) return;
    StateMachine& sm = c.sm;
    if (sm.next != StateId::None || sm.popRequested) {
        sm.lastFactory = sm.curFactory;
        stateExit(c, sm.cur);        // exit current (vt+0x14) and free (FUN_001304b0)
        sm.cur = StateId::None;
        if (sm.popRequested && sm.next == StateId::None) {
            sm.curFactory = StateId::None;
            sm.popRequested = false;
        } else {
            sm.curFactory = sm.next;
            sm.next = StateId::None;
            sm.popRequested = false;
            sm.cur = sm.curFactory;
            sm.priority = sm.priorityCopy;
            sm.phase = 0;            // enter (vt+0x10 with arg -> vt+0x1c)
            stateEnter(c, sm.cur);
        }
    }
    stateUpdate(c);                  // vt+0x18 (Event/TypeId states: player/NPC only, not translated yet)

    if ((c.taskFlags & 0x20000000u) != 0) return;
    // FUN_00129a30 requires floor flags 0x50a610 & 3 (floor generated) -- the Game scene sets them before ticking
    if (c.active()) {
        if (&c == characters().player()) {
            if (playerControlHook) playerControlHook(c);   // FUN_0013b0d0(0x50b620, chr)
        } else {
            aiRun(c.ai, true);                             // FUN_001f07b0(ai, 1)
        }
        chrClassTick(c);                                   // vt+0x34 (FUN_00129c50 empty for the generic class)
        chrCommonTick(c);
        const bool fighter = c.type == 0 || c.type == 2;
        if (fighter && c.status.block.hp == 0) requestDeath(c, 0);   // FUN_0012a5e0(chr, 0)
    }
    c.flags24 &= ~2u;
}

bool playerMenuActionRequest(const char* act4) {
    StateId s;
    if (std::strncmp(act4, "TH00", 4) == 0) s = StateId::MenuThrow;
    else if (std::strncmp(act4, "TH06", 4) == 0) s = StateId::MenuPlace;
    else if (std::strncmp(act4, "TH07", 4) == 0) s = StateId::MenuGive;
    else return false;
    Chr* p = characters().player();
    if (p == nullptr) return false;
    StateMachine& sm = p->sm;
    if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 1) {
        sm.next = s;
        sm.popRequested = false;
        sm.arg = 0;   // +8 = the player (the port's states know their owner)
        sm.priority = 0;
        sm.priorityCopy = 0;
    }
    return true;
}

// orig FUN_00140050: a 'W' face whose first attribute is 'D' is a damage wall while game flag 0x49 is set
bool chrDamageWall(const CollFace* f) {
    if (f == nullptr) return false;
    bool v = false;
    if (f->type == 'W') v = f->attr[0] == 'D';
    if (v) return flags().test(0x49);
    return false;
}

namespace {

// FUN_00129c60 wall part (r & 1: the move touched a wall). First tick of a contact: SE 0x6c when floor flag
// 0x1000000; first wall face 'WD' (FUN_00140050): damage (DAT_0044a718 = 5.0 knock scale), FX 1, Knockback, DM02.
void hazardWall(Chr& c) {
    if ((c.flags24 & 0x10000u) == 0 && (hudWorld().floorFlags & 0x1000000u) != 0)
        sound().playSe3d(1.0f, 0x6c, c.xf.m.pos.data());                       // FUN_00115470(1.0, snd, 0x6c, pos)
    const GroundInfo& g = c.phys.ground;                                        // *(phys +0x58)
    if (g.nWalls != 0) {
        const CollFace* wall = g.walls[0];
        if (chrDamageWall(wall)) {
            // order of the disasm (0x129dc8..0x129e54): red * ((1 + weak) * (0.1 * hpMax + floorNumber))
            const float red = victimReduction(c.status, nullptr);              // FUN_00135f00(st, 0)
            const float weak1 = 1.0f + elementWeakness(c.status, 1);           // FUN_00135c50(st, 1) + 1.0
            const int hpMax = statusHpMax(c.status);                           // FUN_001343f0
            const float a = 0.1f * static_cast<float>(hpMax);                  // 0x3dcccccd
            const int fid = gameWorld().dungeon != nullptr ? gameWorld().dungeon->floorId() : 0;
            const float fl = static_cast<float>(static_cast<uint32_t>(floorNumber(fid)));   // FUN_0011c3b0 (unsigned)
            int dmg = static_cast<int>(red * (weak1 * (a + fl)));
            if (dmg < 1) dmg = 1;
            applyDamage(c.status, nullptr, dmg, 0, nullptr, nullptr);         // FUN_00136580(st, 0, dmg, 0, 0, 0)
            hitElementFx(c, nullptr, 1, nullptr);                              // FUN_0014c580(chr, 0, 1, 0)
            const float k = 5.0f;                                              // DAT_0044a718
            c.phys.knock = {wall->normal[0] * k, wall->normal[1] * k, wall->normal[2] * k};   // phys +0x88
            c.sm.request(StateId::Knockback, 0, 0);                            // LAB_0012a4a0, +8 = chr, prio 0
            c.act.start(reactionName(c, "DM02"));                              // 0x44a720 + FUN_0013a4b0 / "_E%c"
        }
    }
    c.flags24 |= 0x10000u;
}

// FUN_00129c60 floor-attribute part over the floor faces under the body (phys +0x58 -> +0x14 / +0x18):
// 'F?D' poison status 4, 'F?E' status 2 (both play SE 0x36 once while standing on them), 'FD?' damage floor.
void hazardFloor(Chr& c) {
    const GroundInfo& g = c.phys.ground;
    bool statusFloor = false;
    if (g.nFloors != 0) {
        StatusObj& st = c.status;
        const uint32_t mask = st.block.statusMask;                             // status block +0x3c
        for (uint32_t i = 0; i < g.nFloors; ++i) {
            const CollFace* f = g.floors[i];
            if (f == nullptr) continue;
            const char c0 = f->type;
            if (c0 == 'F' && f->attr[1] == 'D') {
                statusFloor = true;
                if ((mask & 0x10000u) == 0 && (mask & 4u) == 0) inflictStatus(st, 4);   // FUN_00138660
                break;
            }
            if (c0 == 'F' && f->attr[1] == 'E') {
                statusFloor = true;
                if ((mask & 0x10000u) == 0 && (mask & 2u) == 0) inflictStatus(st, 2);
                break;
            }
            if (c0 == 'F' && f->attr[0] == 'D') {
                if ((mask & 0x10000u) == 0) {
                    // not while a flinch (LAB_0012a490) is current with nothing pending
                    const bool flinch = c.sm.next == StateId::None && !c.sm.popRequested && c.sm.curFactory == StateId::HitStagger;
                    if (!flinch) {
                        if (combatHooks().effect) combatHooks().effect(0xd4, c.xf.m.pos, nullptr, 100.0f);   // FUN_00112190(0, 0xd4, pos, 0, 100)
                        const float red = victimReduction(st, nullptr);        // FUN_00135f00(st, 0)
                        const int hpMax = statusHpMax(st);                     // FUN_001343f0
                        float d = (0.02f * static_cast<float>(hpMax)) * red;   // 0x3ca3d70a (disasm 0x12a1c4/0x12a1cc)
                        if (c.type == 2) d = d / 10.0f;
                        int dmg = static_cast<int>(d);
                        if (dmg < 1) dmg = 1;
                        applyDamage(st, nullptr, dmg, 0, nullptr, nullptr);    // FUN_00136580(st, 0, dmg, 0, 0, 0)
                        hitElementFx(c, nullptr, 4, nullptr);                  // FUN_0014c580(chr, 0, 4, 0)
                        const Vec3f zero{0.0f, 0.0f, 0.0f};
                        react(c, 0x1000, zero, false);                         // FUN_0012a710(chr, 0x1000, &0, 0)
                    }
                }
                break;
            }
        }
    }
    if (statusFloor) {
        if ((c.flags24 & 0x20000u) == 0) {
            c.flags24 |= 0x20000u;
            sound().playSe3d(1.0f, 0x36, c.xf.m.pos.data());                   // FUN_00115470(1.0, snd, 0x36, pos)
        }
    } else {
        c.flags24 &= ~0x20000u;
    }
}

}  // namespace

// orig FUN_00129c60 (work/decompiled/00129c60.c). FUN_0012a4b0 (no CLS load pending, counters 0x44b2b8/0x44b2c0
// of FUN_0013fdd0/FUN_0013fed0) is always true in the port (synchronous loads); the floor-flag gate (0x50a610 & 3)
// is kept by the callers (the Game scene ticks characters only on a generated floor; the test area has none).
void chrCommonTick(Chr& c) {
    if ((c.flags24 & 2u) != 0) return;
    c.flags24 |= 2u;
    alchemy::ps2::Ps2FloatScope fp;
    const uint32_t r = physTick(c.phys);           // FUN_00142e60
    if (c.type != 1) {
        const bool dead = (c.type == 0 || c.type == 2) && c.status.block.hp == 0;   // status block +4 == 0
        if (!dead) {
            if ((r & 1u) == 0) c.flags24 &= ~0x10000u;
            else hazardWall(c);
            hazardFloor(c);
        }
    }
    if (Dungeon* d = gameWorld().dungeon) c.cell = d->cellAtWorld(c.xf.m.pos.data(), &c.cellX, &c.cellY);   // FUN_00124c70
    if ((c.flags24 & 4u) == 0) {                   // FUN_00109d40 publishes the model matrix unless a state did
        c.jitter = false;
        if (c.model.loaded()) modelPublishFx(c.model);   // FUN_0010a160: material fader step
    }
    c.flags24 &= ~4u;
    if (c.actLoaded) {                             // FUN_00143fe0
        actNTurnStep(c);                           //   FUN_001459f0 (head look-at, act_props.cpp)
        actTurnStep(c);                            //   FUN_00146120
        actScaleStep(c);                           //   FUN_00146220
        if (c.act.currentName() != nullptr) actHitLatch(c);
        c.act.tick();
        // PORT: until the engine frame FUN_00101bd0 calls chrActEffectsFrame() (FUN_0012a4e0), the effect slots follow
        // here, right after the ACT tick (UNVERIFIED: one frame earlier than the original)
        if (!actEffectsFrameHooked() && static_cast<unsigned>(c.type) < 3u && c.active()) actEffectFollow(c);
    }
    c.shape.top = c.shape.height + c.xf.m.pos[1];  // FUN_00142270
    statusTick(c.status);                          // FUN_00133bf0
    // iRam00517b34 = floor 99 ? 99999 : floorNumber*100 - round(y)/10; FUN_00151af0(0x5179c0, 4)
    if (&c == characters().player() && gameWorld().dungeon != nullptr) hudPlayerDepth(c.xf.m.pos[1]);
}

// orig FUN_00129140 (work/decompiled/00129140.c): C19x touched by J030 -> FUN_001b2e10 when its current state is
// TypeId (factory 0x1293e0, player_event.cpp); C10A -> flag 0xde, counter 2 + 1; C05x touched by J030 ->
// flag 0x2e0 + variant, all of 0x321..0x32b set -> 0x32c. Strings 0x44a6b0 "C19", 0x44a6b8 "J030", 0x44a6c0 "C10A",
// 0x44a6c8 "C05". Returns type == 1.
bool chrContactDefault(Chr& self, const Chr* other) {
    const uint32_t id = self.id;
    const uint32_t id3 = id & 0xffffffu;
    const uint32_t j030 = id4("J030");
    if (id3 == (id4("C19\0") & 0xffffffu)) {
        // current state (+4) present with no request pending (+0x18 == 0) and its factory 0x1293e0 -> FUN_001b2e10
        if (other != nullptr && other->id == j030 && self.sm.cur == StateId::TypeId && self.sm.next == StateId::None &&
            !self.sm.popRequested)
            typeIdShot(self);
    } else if (id == id4("C10A")) {
        flags().set(0xde);
        counters().add(2, 1);
    } else if (id3 == (id4("C05\0") & 0xffffffu) && other != nullptr && other->id == j030) {
        flags().set((id >> 24) + 0x2e0);
        bool all = true;
        for (uint32_t f = 0x321; f < 0x32c; ++f)
            if (!flags().test(f)) {
                all = false;
                break;
            }
        if (all) flags().set(0x32c);
    }
    return self.type == 1;
}

// vt+0x24: object classes, C08 (FUN_00130900: chr +0x44), FUN_00129140 for every other class and the items
int chrContact(Chr& self, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3) {
    if (self.obj != nullptr) return objectContact(self, other, m1, m2, m3);
    if (self.cls == ChrClass::NpcC08) return self.deathVariant;
    return chrContactDefault(self, other) ? 1 : 0;
}

// =============================================================================================================
// Presentation
// =============================================================================================================
void chrPose(Chr& c) {
    if (c.obj != nullptr) {
        objectPose(c);
        return;
    }
    if (!c.model.loaded() || c.model.parts.empty()) return;
    c.model.anim.pose(c.model.pose);
    if (c.model.pose.empty()) return;
    chrApplyBoneMods(c.model, c.model.pose);
    for (ChrModelPart& part : c.model.parts) {
        if (part.model && part.actor) {   // separate actor: FUN_00108d20 + its own pose
            part.actor->anim.update();
            part.actor->anim.pose(part.actor->pose);
            if (!part.actor->pose.empty()) part.model->setPose(part.actor->pose);
        } else if (part.model && part.attachBone.empty() && part.attachBoneIndex < 0) {
            part.model->setPose(c.model.pose);
            if (part.lodModel) part.lodModel->setPose(c.model.pose);
        }
    }
    chrClassPose(c);
}

// The skin swaps of one 3-D pass (see character.hpp). Task list walk of each original function: tasks flagged
// 0x10000000 are skipped, type < 3 with the model loaded (+0x2c bit 0x80000000).
namespace {
bool lodTask(const Chr& c) {
    return (c.taskFlags & 0x10000000u) == 0 && static_cast<uint32_t>(c.type) < 3u && c.obj == nullptr && c.model.loaded();
}
}  // namespace

void chrSkinLodTick(const float eye[3], bool shadowPrePassOn) {
    for (Chr* c : characters().list()) {
        if (!lodTask(*c)) continue;
        c->model.lodCaster = c->model.lodSkin;   // PORT: the skin the pre-pass traverses (its "sh" swap aside)
        // orig FUN_001113d0 (work/decompiled/001113d0.c, loop after the caster traversal): FUN_00326280(appearance +0xc,
        // _skinList[0]) for every loaded character; only while the pass is on (renderer +0xd4 +0x10)
        if (shadowPrePassOn) c->model.lodSkin = false;
    }
    // orig FUN_00108d20 (work/decompiled/00108d20.c): eye = translation of inverse(0x4e796c) (the tick camera), distance
    // to the character position *(chr+0x20)+0xc
    for (Chr* c : characters().list()) {
        if (!lodTask(*c)) continue;
        const float dy = eye[1] - c->xf.m.pos[1];
        const float dx = eye[0] - c->xf.m.pos[0];
        const float dz = eye[2] - c->xf.m.pos[2];
        float d = 0.0f;
        const float d2 = dz * dz + dx * dx + dy * dy;
        if (d2 != 0.0f) d = std::sqrt(d2);
        if (600.0f < d) {
            if ((c->flags24 & 8u) == 0) {             // latch +0x24 bit 3: the first far pass still sets the flag
                c->flags24 |= 8u;
                c->model.actorFlag10 = true;           // vt+0x70(actor, 0x10, 1)
            } else {
                c->model.actorFlag10 = false;          // vt+0x70(actor, 0x10, 0)
            }
        } else {
            c->model.actorFlag10 = true;
        }
        // beyond 1200: appearance _skin := the _skinList entry named "lod" (FUN_0023f3fc(0x42c918, name) == 0)
        if (1200.0f < d && c->model.parts[0].lodModel) c->model.lodSkin = true;
        // (the combiner part of FUN_00108d20 is ActorAnim::update, run by chrPose)
    }
    for (Chr* c : characters().list())
        if (lodTask(*c)) c->model.lodDraw = c->model.lodSkin;   // PORT: the scene traversal (vt+0x78 -> vt+0x60)
    // orig FUN_001092a0 (work/decompiled/001092a0.c): after the scene, beyond 1200 measured from the model's published
    // matrix (*(model+0x10)+0x20 translation; FUN_00109d40 published the jittered position on a flinch) the appearance
    // gets _skinList[0] back
    for (Chr* c : characters().list()) {
        if (!lodTask(*c)) continue;
        const Vec3f& p = c->jitter ? c->jitterPos : c->xf.m.pos;
        const float dz = p[2] - eye[2], dx = p[0] - eye[0], dy = p[1] - eye[1];
        if (1200.0f < std::sqrt(dz * dz + dx * dx + dy * dy)) c->model.lodSkin = false;
    }
}

bool chrSubActorAnim(Chr& c, const char* name, float blend, float speed) {
    if (c.cls == ChrClass::Player) {   // Player class vt+0x28: the +0x40 head actor (FUN_001b1350)
        for (ChrModelPart& p : c.model.parts) {
            if (!p.actor) continue;
            int idx = -1;
            for (size_t i = 0; i < p.actor->clips.size(); ++i)
                if (std::strcmp(p.actor->clips[i]->name.c_str(), name) == 0) { idx = static_cast<int>(i); break; }
            p.actor->anim.change(blend, speed, idx, 0);   // FUN_0010a8e0 (null animation when not found)
            return true;
        }
        return false;
    }
    return npcPartAnim(c, 1, name, blend, speed);
}

// Joint modifiers (FUN_003bf580 objects in the actor's list): world_j' = M * world_j, descendants follow.
void chrApplyBoneMods(const ChrModel& m, std::vector<alchemy::Mat4>& world) {
    if (m.boneMods.empty()) return;
    const alchemy::Skeleton* sk = m.skeleton();
    if (sk == nullptr) return;
    const size_t n = std::min(world.size(), sk->bones.size());
    for (const BoneMod& b : m.boneMods) {
        if (b.joint < 0 || static_cast<size_t>(b.joint) >= n) continue;
        const alchemy::Mat4 wj = world[size_t(b.joint)];
        alchemy::Mat4 inv;
        if (!alchemy::invert(wj, inv)) continue;
        const alchemy::Mat4 delta = alchemy::mul(inv, alchemy::mul(b.m, wj));
        for (size_t d = 0; d < n; ++d) {
            int p = static_cast<int>(d);
            while (p >= 0 && p != b.joint) p = sk->bones[size_t(p)].parent;
            if (p == b.joint) world[d] = alchemy::mul(world[d], delta);
        }
    }
}

void chrDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha) {
    if (c.obj != nullptr) {
        objectDraw(c, r, pass, alpha);
        return;
    }
    if (c.model.parts.empty()) return;
    alchemy::Mat4 m = alchemy::Mat4::identity();
    const ChrTransform& t = c.xf.m;
    for (int k = 0; k < 3; ++k) {
        const float p = c.jitter ? c.jitterPos[size_t(k)]
                                 : c.prevPos[size_t(k)] + (t.pos[size_t(k)] - c.prevPos[size_t(k)]) * alpha;
        m.m[size_t(k)] = t.right[size_t(k)] * c.xf.scale[0];
        m.m[size_t(4 + k)] = t.up[size_t(k)] * c.xf.scale[1];
        m.m[size_t(8 + k)] = t.fwd[size_t(k)] * c.xf.scale[2];
        m.m[size_t(12 + k)] = p;
    }
    const alchemy::Skeleton* sk = c.model.skeleton();
    modelBeginDraw(c.model, r);
    for (const ChrModelPart& part : c.model.parts) {
        if (!part.model || !part.visible) continue;
        alchemy::Mat4 w = m;
        if (!part.attachBone.empty() && sk != nullptr)
            for (size_t b = 0; b < sk->bones.size() && b < c.model.pose.size(); ++b)
                if (sk->bones[b].name == part.attachBone) w = alchemy::mul(c.model.pose[b], m);
        if (part.attachBoneIndex >= 0) {
            if (static_cast<size_t>(part.attachBoneIndex) >= c.model.pose.size()) continue;
            w = alchemy::mul(part.local, alchemy::mul(c.model.pose[size_t(part.attachBoneIndex)], m));
        } else if (part.attachBone.empty()) {
            w = alchemy::mul(part.local, w);
        }
        modelSubmitPart(c.model, part, w, r, pass);   // part override sets, glow copy, material fader
    }
    // player +0x6c coat shadow skin: submitted by the shadow pre-pass only (modelSubmitPart filters it)
    if (c.model.coatShadow) modelSubmitPart(c.model, *c.model.coatShadow, m, r, pass);
    modelEndDraw(r);
    chrClassDraw(c, r, pass, alpha);
}

// PORT: chrDraw from a presenter snapshot (game/present/present_state.hpp): same part walk, the actor matrix / body
// pose / shell matrix come from the interpolated snapshot instead of prevPos and Chr::model.pose
void chrDrawAt(const Chr& c, const ChrDrawState& s, alchemy::SceneRenderer& r, int pass) {
    if (c.obj != nullptr) {
        objectDrawAt(c, s.actor, s.objAnimTime, r, pass);
        return;
    }
    if (c.model.parts.empty()) return;
    const alchemy::Mat4& m = s.actor;
    const std::vector<alchemy::Mat4>& pose = s.body != nullptr ? *s.body : c.model.pose;
    const alchemy::Skeleton* sk = c.model.skeleton();
    modelBeginDraw(c.model, r);
    for (const ChrModelPart& part : c.model.parts) {
        if (!part.model || !part.visible) continue;
        alchemy::Mat4 w = m;
        if (!part.attachBone.empty() && sk != nullptr)
            for (size_t b = 0; b < sk->bones.size() && b < pose.size(); ++b)
                if (sk->bones[b].name == part.attachBone) w = alchemy::mul(pose[b], m);
        if (part.attachBoneIndex >= 0) {
            if (static_cast<size_t>(part.attachBoneIndex) >= pose.size()) continue;
            w = alchemy::mul(part.local, alchemy::mul(pose[size_t(part.attachBoneIndex)], m));
        } else if (part.attachBone.empty()) {
            w = alchemy::mul(part.local, w);
        }
        modelSubmitPart(c.model, part, w, r, pass);
    }
    if (c.model.coatShadow) modelSubmitPart(c.model, *c.model.coatShadow, m, r, pass);   // player +0x6c (pre-pass only)
    modelEndDraw(r);
    if (s.hasShell) chrClassDrawAt(c, s.shell, r, pass);
}

// =============================================================================================================
// Character push (FUN_00142290 and the pair tests FUN_00142850 / FUN_00142990 / FUN_00142b70)
// =============================================================================================================
// Strings 0x44b2d0 "C18A", 0x44b2d8 "E19".
bool chrSolidObstacle(const Chr& c) {
    if (c.type == kChrEnemy) {
        if ((c.id & 0xffffffu) != (id4("E19\0") & 0xffffffu)) return false;
        return ((c.ai.flags & 6u) >> 1) == 0;              // FUN_001f1730(ai, 0x1000008) == 0
    }
    if (c.type == kChrNpc) return c.id != id4("C18A");
    return false;
}

namespace {

int cellRoom(const uint32_t* cell) {
    if (cell == nullptr) return -1;
    return (*cell & 0xf000u) == 0x1000u ? static_cast<int>(*cell & 0xfu) : -1;
}

// orig FUN_00142850: cylinder vs cylinder (also used for box vs box)
bool pushCylinders(const Phys& p, Vec3f& out, const Chr& self, const Chr& other) {
    const ChrTransform& s = self.xf.m;
    const Vec3f& o = other.xf.m.pos;
    const float x0 = s.pos[0];
    const float dz = o[2] - s.pos[2];
    const float r = p.shape->radius + other.shape.radius;
    const float dx = o[0] - x0;
    const float d2 = dz * dz + dx * dx + 0.0f;
    if (r * r < d2) return false;
    if (!((*p.pos)[1] <= other.shape.top)) return false;
    if (p.shape->top < other.xf.m.pos[1]) return false;
    if (d2 == 0.0f) {
        out = {s.fwd[0] * r + x0 + 0.0f, s.fwd[1] * r + s.pos[1] + 0.0f, s.fwd[2] * r + s.pos[2] + 0.0f};
    } else {
        const float d = std::sqrt(d2);
        const float k = d - r;
        out = {(dx / d) * k + x0 + 0.0f, (0.0f / d) * k + s.pos[1] + 0.0f, (dz / d) * k + s.pos[2] + 0.0f};
    }
    return true;
}

// orig FUN_00142990: self cylinder vs other box (other's forward/right axes, half extents b/a)
bool pushCylinderBox(const Phys& p, Vec3f& out, const Chr& self, const Chr& other) {
    const ChrTransform& ot = other.xf.m;
    const Vec3f& sp = self.xf.m.pos;
    const float fy = ot.fwd[1], fx = ot.fwd[0], fz = ot.fwd[2];
    const float sy = sp[1], sx = sp[0], sz = sp[2];
    const float rad = p.shape->radius;
    float e6 = rad + other.shape.b;
    const float pf = (fz * sz + fx * sx + fy * sy) - (fz * ot.pos[2] + fx * ot.pos[0] + fy * ot.pos[1]);
    const float apf = pf < 0.0f ? -pf : pf;
    if (e6 < apf) return false;
    float e9 = rad + other.shape.a;
    const float pr = (ot.right[2] * sz + ot.right[0] * sx + ot.right[1] * sy) -
                     (ot.right[2] * ot.pos[2] + ot.right[0] * ot.pos[0] + ot.right[1] * ot.pos[1]);
    const float apr = pr < 0.0f ? -pr : pr;
    if (e9 < apr) return false;
    if (!((*p.pos)[1] <= other.shape.top)) return false;
    if (p.shape->top < other.xf.m.pos[1]) return false;
    e6 = e6 - apf;
    e9 = e9 - apr;
    if (e6 < e9) {
        e9 = 0.0f;
        if (pf < 0.0f) e6 = -e6;
    } else {
        e6 = 0.0f;
        if (pr < 0.0f) e9 = -e9;
    }
    out = {fx * e6 + sx + 0.0f, fy * e6 + sy + 0.0f, fz * e6 + sz + 0.0f};
    out[0] = out[0] + ot.right[0] * e9;
    out[1] = out[1] + ot.right[1] * e9;
    out[2] = out[2] + ot.right[2] * e9;
    return true;
}

// orig FUN_00142b70: self box vs other cylinder (self's axes)
bool pushBoxCylinder(const Phys& p, Vec3f& out, const Chr& self, const Chr& other) {
    const ChrTransform& st = self.xf.m;
    const Vec3f& op = other.xf.m.pos;
    const float fy = st.fwd[1], fx = st.fwd[0], fz = st.fwd[2];
    const float sy = st.pos[1], sx = st.pos[0], sz = st.pos[2];
    const float orad = other.shape.radius;
    float e17 = p.shape->b + orad;
    const float pf = (fz * op[2] + fx * op[0] + fy * op[1]) - (fz * sz + fx * sx + fy * sy);
    const float apf = pf < 0.0f ? -pf : pf;
    if (e17 < apf) return false;
    float e16 = p.shape->a + orad;
    const float pr = (st.right[2] * op[2] + st.right[0] * op[0] + st.right[1] * op[1]) -
                     (st.right[2] * sz + st.right[0] * sx + st.right[1] * sy);
    const float apr = pr < 0.0f ? -pr : pr;
    if (e16 < apr) return false;
    if (!((*p.pos)[1] <= other.shape.top)) return false;
    if (p.shape->top < other.xf.m.pos[1]) return false;
    e17 = e17 - apf;
    e16 = e16 - apr;
    if (e17 < e16) {
        e16 = 0.0f;
        if (pf < 0.0f) e17 = -e17;
    } else {
        e17 = 0.0f;
        if (pr < 0.0f) e16 = -e16;
    }
    e17 = -e17;
    out = {fx * e17 + sx + 0.0f, fy * e17 + sy + 0.0f, fz * e17 + sz + 0.0f};
    e16 = -e16;
    out[0] = out[0] + st.right[0] * e16;
    out[1] = out[1] + st.right[1] * e16;
    out[2] = out[2] + st.right[2] * e16;
    return true;
}

}  // namespace

// orig FUN_00142290 (work/decompiled/00142290.c): average push target of every overlapping character in the same room
// (or either in a corridor) within 4 cells; *move = average - pos. List 0x50a7b0 = characters of type 0..2.
bool chrPush(Phys& p) {
    Shape* sh = p.shape;
    if (sh == nullptr || sh->owner == nullptr || !(0.0f < sh->radius)) return false;
    const Chr& self = *static_cast<const Chr*>(sh->owner);
    if (chrSolidObstacle(self)) return false;
    sh->top = sh->height + (*p.pos)[1];                                       // FUN_00142270
    const int room = cellRoom(self.cell);
    std::array<Vec3f, 32> outs{};                                             // 0x50c550
    uint32_t n = 0;
    for (const Chr* o : characters().list()) {
        if (o->type > kChrEnemy || o == &self || !o->active()) continue;
        if (o->shape.owner == nullptr || !(0.0f < o->shape.radius)) continue;
        const int orm = cellRoom(o->cell);
        if (!(room < 0 || orm < 0 || room == orm)) continue;
        if (std::abs(o->cellX - self.cellX) >= 4 || std::abs(o->cellY - self.cellY) >= 4) continue;
        const float a = sh->a, oa = o->shape.a;
        bool hit = false;
        Vec3f out{};
        if (a < 0.0f && oa < 0.0f) hit = pushCylinders(p, out, self, *o);
        else if (a < 0.0f && 0.0f < oa) hit = pushCylinderBox(p, out, self, *o);
        else if (0.0f < a && oa < 0.0f) hit = pushBoxCylinder(p, out, self, *o);
        else if (0.0f < a && 0.0f < oa) hit = pushCylinders(p, out, self, *o);
        if (hit && n < outs.size()) outs[n++] = out;
    }
    if (n == 0) return false;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        sx = sx + outs[i][0];
        sy = sy + outs[i][1];
        sz = sz + outs[i][2];
    }
    const float fn = static_cast<float>(n);
    const Vec3f& pos = *p.pos;
    Vec3f& mv = *p.move;
    const float y = pos[1], z = pos[2];
    mv[0] = sx / fn - pos[0];
    mv[1] = sy / fn - y;
    mv[2] = sz / fn - z;
    return true;
}

// =============================================================================================================
// Tick-time model state, player control, task-list reaping
// =============================================================================================================
// FUN_00109d40: actor world matrix = axes x scale, position (the flinch publishes its jittered position instead)
alchemy::Mat4 chrActorMatrix(const Chr& c) {
    alchemy::Mat4 m = alchemy::Mat4::identity();
    const ChrTransform& t = c.xf.m;
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = t.right[size_t(k)] * c.xf.scale[0];
        m.m[size_t(4 + k)] = t.up[size_t(k)] * c.xf.scale[1];
        m.m[size_t(8 + k)] = t.fwd[size_t(k)] * c.xf.scale[2];
        m.m[size_t(12 + k)] = c.jitter ? c.jitterPos[size_t(k)] : t.pos[size_t(k)];
    }
    return m;
}

// Bone matrices at the current actor clock (evaluated once per clock value)
const std::vector<alchemy::Mat4>& chrTickPose(Chr& c) {
    const uint64_t stamp = static_cast<uint64_t>(ActorAnim::clock());
    if (c.tickPoseStamp != stamp) {
        c.tickPoseStamp = stamp;
        if (c.model.loaded()) {
            c.model.anim.pose(c.tickPose);
            chrApplyBoneMods(c.model, c.tickPose);
        } else {
            c.tickPose.clear();
        }
    }
    return c.tickPose;
}

namespace {
PlayerControl* gPlayerCtl = nullptr;
}
PlayerControl* playerCtl() { return gPlayerCtl; }
void setPlayerCtl(PlayerControl* ctl) { gPlayerCtl = ctl; }

// Task-list walk of the game task: removed characters (0x10000000) are unlinked (FUN_00207380) and deleted
// (0x20000000 / the character destructor FUN_00132190 -> FUN_00129500: hurt volume FUN_0014ace0, ACT hit slots).
void chrReap() {
    characters().reap();
}

}  // namespace baroque::game
