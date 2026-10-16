#include "game/ui/texture_registry.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/ui_tables.hpp"
#include "platform/platform.hpp"
#include <cstring>
namespace baroque::game::ui {
namespace { TextureRegistry gTextures; }
TextureRegistry& textures() { return gTextures; }

void TextureRegistry::init() {}
void TextureRegistry::shutdown() {
    for (int i = 0; i < kTexRegistryCount + kTexDynamicCount; ++i) if (entries_[i].loaded) release(i);
}
bool TextureRegistry::isLoaded(int id) const {
    return id >= 0 && id < kTexRegistryCount + kTexDynamicCount && entries_[id].loaded;
}
const char* TextureRegistry::path(int id) const {
    if (id < 0 || id >= kTexRegistryCount) return "";
    return kTexturePaths[id][language() == 1 ? 1 : 0];
}
// orig FUN_00106090 (work/decompiled/00106090.c): path = table[id*16 + lang*8]; loads into slot 0x4e6a70[id]
void TextureRegistry::load(int id) {
    if (id < 0 || id >= kTexRegistryCount) return;
    Entry& e = entries_[id];
    if (e.loaded) return;   // callers test isLoaded() first; the original would reload (leaking); keep idempotent
    const char* rel = path(id);
    if (rel[0] == '\0') { platform::log("texture: no path for id " + std::to_string(id)); return; }
    std::filesystem::path p = platform::assetRoot() / rel;
    if (!std::filesystem::exists(p)) p = platform::assetRoot() / "PACKS" / "FONT" / rel;   // FONT pack members
    Image img = LoadImage(p.string().c_str());
    if (img.data == nullptr) { platform::log("texture: failed to load " + p.string()); return; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    e.w = img.width; e.h = img.height; e.dynamic = false;
    if (!headless) {
        e.tex = LoadTextureFromImage(img);
        SetTextureFilter(e.tex, TEXTURE_FILTER_BILINEAR);   // UNVERIFIED: GS filter mode of the UI sprites
        SetTextureWrap(e.tex, TEXTURE_WRAP_CLAMP);          // GS CLAMP: no bleed across tile edges (title 2x2 BG)
    }
    UnloadImage(img);
    e.loaded = true;
}
// orig FUN_00106120 (work/decompiled/00106120.c)
void TextureRegistry::loadRange(int first, int last) { for (int i = first; i <= last; ++i) load(i); }
// orig FUN_00105f00 (work/decompiled/00105f00.c): slot = -1; dynamic ids also free the pixel object
void TextureRegistry::release(int id) {
    if (id < 0 || id >= kTexRegistryCount + kTexDynamicCount) return;
    Entry& e = entries_[id];
    if (!e.loaded) return;
    if (!headless && e.tex.id != 0) UnloadTexture(e.tex);
    e = Entry{};
}
// orig FUN_00106350 (not translated: Alchemy texture object creation). Picks the first free dynamic id.
int TextureRegistry::createDynamic(int w, int h) {
    for (int id = kTexDynamicFirst; id < kTexDynamicFirst + kTexDynamicCount; ++id) {
        Entry& e = entries_[id];
        if (e.loaded) continue;
        e.w = w; e.h = h; e.dynamic = true; e.loaded = true;
        e.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0u);
        if (!headless) {
            Image img{};
            img.data = e.pixels.data(); img.width = w; img.height = h; img.mipmaps = 1; img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            e.tex = LoadTextureFromImage(img);
            SetTextureFilter(e.tex, TEXTURE_FILTER_BILINEAR);
        }
        return id;
    }
    return -1;
}
uint32_t* TextureRegistry::dynamicPixels(int id) {
    if (!isLoaded(id) || !entries_[id].dynamic) return nullptr;
    return entries_[id].pixels.data();
}
// orig FUN_00106650: re-upload. PS2 alpha (0x80 = opaque) is expanded to 8-bit here so the GPU texture is plain RGBA.
void TextureRegistry::dynamicUpload(int id) {
    if (!isLoaded(id) || !entries_[id].dynamic || headless) return;
    Entry& e = entries_[id];
    std::vector<uint32_t> tmp(e.pixels.size());
    for (size_t i = 0; i < tmp.size(); ++i) {
        const uint32_t p = e.pixels[i];
        uint32_t a = (p >> 24) * 255u / 128u; if (a > 255u) a = 255u;
        tmp[i] = (p & 0x00ffffffu) | (a << 24);
    }
    UpdateTexture(e.tex, tmp.data());
}
int TextureRegistry::width(int id) const { return isLoaded(id) ? entries_[id].w : 0; }
int TextureRegistry::height(int id) const { return isLoaded(id) ? entries_[id].h : 0; }
const Texture2D* TextureRegistry::texture(int id) const {
    if (!isLoaded(id) || headless) return nullptr;
    return &entries_[id].tex;
}
}
