#pragma once
// Texture manager 0x4e6a68 (RUNTIME.md 1.1, GAME_FORMATS.md 5): id table 0x4e6a70[], registry 0x48a280.
// FUN_00106090(mgr, id, 1) load, FUN_00106120(mgr, first, last) load range, FUN_00105f00(mgr, id) release.
// Ids >= 0x11e are dynamic (procedural) textures created with FUN_00106350(mgr, w, h, 0, 1); id 0x129 is the
// "no texture" sentinel for colour-only quads.
#include <cstdint>
#include <string>
#include <vector>
#include "raylib.h"
namespace baroque::game::ui {
constexpr int kTexNone = 0x129;
constexpr int kTexRegistryCount = 286;
constexpr int kTexDynamicFirst = 0x11e;
constexpr int kTexDynamicCount = 0x129 - 0x11e;   // 11 dynamic slots
class TextureRegistry {
public:
    void init();                               // nothing loaded; language from strings()
    void shutdown();
    bool isLoaded(int id) const;               // the inline `slot >= 0 && object != 0` test used everywhere
    void load(int id);                         // FUN_00106090(mgr, id, 1)
    void loadRange(int first, int last);       // FUN_00106120
    void release(int id);                      // FUN_00105f00
    int createDynamic(int w, int h);           // FUN_00106350: returns a new id >= 0x11e (-1 when none free)
    uint32_t* dynamicPixels(int id);           // FUN_00106630: RGBA32 pixel buffer (r | g<<8 | b<<16 | a<<24, PS2 alpha 0x80 = opaque)
    void dynamicUpload(int id);                // FUN_00106650: upload the buffer
    int width(int id) const;
    int height(int id) const;
    const Texture2D* texture(int id) const;    // nullptr when not loaded / sentinel
    const char* path(int id) const;            // registry path for the current language
    bool headless = false;                     // when true, textures are decoded for size only (no GPU upload)
private:
    struct Entry { Texture2D tex{}; int w = 0, h = 0; bool loaded = false; bool dynamic = false; std::vector<uint32_t> pixels; };
    Entry entries_[kTexRegistryCount + kTexDynamicCount];
};
TextureRegistry& textures();
}
