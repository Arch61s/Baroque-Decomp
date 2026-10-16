#include "game/ui/sprite.hpp"
#include "game/ui/texture_registry.hpp"
namespace baroque::game::ui {
namespace { SpriteLayer gSprites; }
SpriteLayer& sprites() { return gSprites; }

// orig FUN_001070a0 (work/decompiled/001070a0.c): flag != 0 -> +4 list; 0x129 -> +0xc; else bucket matching texId or first empty
SpriteBucket* SpriteLayer::bucketFor(int texId, int flag, bool allocate) {
    if (flag != 0) return &list1_;
    if (texId == kTexNone) return &untextured_;
    for (SpriteBucket& b : textured_) if (b.head != nullptr && b.head->texId == texId) return &b;
    if (!allocate) return nullptr;
    for (SpriteBucket& b : textured_) if (b.head == nullptr) { b.texId = texId; return &b; }
    return nullptr;
}
// orig FUN_00166820 head: FUN_00107130 (texture loaded?), FUN_001070a0, FUN_00107180 (alloc 0x44), FUN_00107070 (link at head)
Sprite* SpriteLayer::alloc(int texId, int flag) {
    const bool ok = (texId == kTexNone) || (texId >= 0 && textures().isLoaded(texId));
    if (!ok) return nullptr;
    SpriteBucket* b = bucketFor(texId, flag, true);
    if (b == nullptr) return nullptr;
    Sprite* s = new Sprite();
    b->count += 1;
    // FUN_00107070: prepend
    s->prev = nullptr;
    s->next = b->head;
    if (b->head != nullptr) b->head->prev = s;
    b->head = s;
    s->texId = texId;
    s->layerFlag = flag;
    s->anchor = anchorScope_;   // PORT: widescreen anchor (presentation only)
    return s;
}
// orig FUN_00166820 (work/decompiled/00166820.c)
Sprite* SpriteLayer::create(float x, float y, float z, float w, float h, float u0, float v0, float u1, float v1, int texId, int flag) {
    Sprite* s = alloc(texId, flag);
    if (s == nullptr) return nullptr;
    s->cx = w / 2.0f + x;
    s->cy = h / 2.0f + y;
    s->z = z;
    s->hw = w / 2.0f;
    s->hh = h / 2.0f;
    s->color = 0x7f7f7f;
    float tw = 1.0f, th = 1.0f;
    if (texId != kTexNone) { tw = static_cast<float>(textures().width(texId)); th = static_cast<float>(textures().height(texId)); }
    if (tw <= 0.0f) tw = 1.0f;
    if (th <= 0.0f) th = 1.0f;
    s->u0 = u0 / tw; s->v0 = v0 / th; s->u1 = u1 / tw; s->v1 = v1 / th;
    return s;
}
// the inline destroy idiom (e.g. FUN_00117e90 state 5/9): find bucket, unlink, FUN_001053d0, count--
void SpriteLayer::destroy(Sprite* s) {
    if (s == nullptr) return;
    SpriteBucket* b = bucketFor(s->texId, s->layerFlag, false);
    if (s->prev == nullptr) { if (b != nullptr) b->head = s->next; }
    else s->prev->next = s->next;
    if (s->next != nullptr) s->next->prev = s->prev;
    if (b != nullptr) b->count -= 1;
    delete s;
}
void SpriteLayer::build(std::vector<SpriteDraw>& out) const {
    auto emit = [&](const SpriteBucket& b) {
        for (const Sprite* s = b.head; s != nullptr; s = s->next)
            out.push_back(SpriteDraw{s->texId, s->cx, s->cy, s->z, s->hw, s->hh, s->u0, s->v0, s->u1, s->v1, s->color, s->layerFlag, s->anchor, s->wide, s->keepGlyph});
    };
    emit(list1_);
    // flag-0 lists in the batch order of FUN_00103ad0 (work/decompiled/00103ad0.c, loop at 0x103c74): entries +0xc
    // (0x129) and +0x14.. (20 texture slots) are visited in slot order and each non-empty list is pushed at the head
    // of the batch chain 0x4e6750 (FUN_00102890), so FUN_001032e0 draws texture slot 19 first .. slot 0, then the
    // untextured list last; inside a list from its head (newest first).
    for (int i = 19; i >= 0; --i) emit(textured_[i]);
    emit(untextured_);
}
int SpriteLayer::liveCount() const {
    int n = list1_.count + untextured_.count;
    for (const SpriteBucket& b : textured_) n += b.count;
    return n;
}
void SpriteLayer::clearAll() {
    auto wipe = [&](SpriteBucket& b) { while (b.head != nullptr) destroy(b.head); b.count = 0; };
    wipe(list1_); wipe(untextured_);
    for (SpriteBucket& b : textured_) wipe(b);
}
}
