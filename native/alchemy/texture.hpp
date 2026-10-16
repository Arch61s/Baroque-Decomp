#pragma once
// igImage + igClut decoding (exactly like tools/igb.py image_rgba/clut_colors) and raylib upload.
#include "alchemy/igb.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace baroque::alchemy {

struct DecodedImage {
    std::string name;             // igImage._pNameString (e.g. "parts_00.tga")
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;    // width*height*4, row 0 = top row as stored in the file
};

// PS2 alpha: 0x80 = opaque. Scaled to 8-bit (tools/igb.py ps2_alpha).
inline uint8_t ps2Alpha(uint8_t a) { return a >= 0x80 ? uint8_t(255) : uint8_t(a * 2); }

// RGBA entries of an igClut (CSM1 de-swizzle for 256-entry CLUTs). Returns false on an unsupported CLUT.
bool clutColors(const IgbFile& f, const IgObject& clut, std::vector<uint8_t>& rgbaOut, std::string* error);
// Decode an igImage (4/8-bit indexed via _pClut, or 16/24/32-bit direct) to RGBA8.
bool decodeImage(const IgbFile& f, const IgObject& image, DecodedImage& out, std::string* error);

// igTextureAttr sampler parameters. Enum meaning verified in the ELF: filters pass through FUN_00383110
// table 5 (identity -> GS TEX1 MMAG/MMIN order: 0 NEAREST, 1 LINEAR, 2..5 mipmap variants); wrap modes
// pass through table 6 = {1,0,2,3} -> GS CLAMP WMS/WMT (0 REPEAT, 1 CLAMP, 2 REGION_CLAMP, 3 REGION_REPEAT),
// i.e. attr enum 0 = CLAMP, 1 = REPEAT (FUN_002b3910, words at 0x4845c0).
struct TextureParams {
    int magFilter = 1;
    int minFilter = 1;
    int wrapS = 1;
    int wrapT = 1;
    int mipmapMode = 0;
    static TextureParams fromAttr(const IgObject& textureAttr);
};

struct GpuTexture {
    unsigned id = 0;
    int width = 0, height = 0;
    int mipmaps = 1;
    std::string name;
    std::string sourceFile;   // IGB path the texture came from
    int attrSlot = -1;        // igTextureAttr slot in sourceFile
};

// Upload RGBA8 (plus optional extra mip levels, largest first) and apply sampler parameters.
GpuTexture uploadTexture(const DecodedImage& image, const std::vector<DecodedImage>& mips, const TextureParams& params);
void unloadTexture(GpuTexture& tex);
// PORT diagnostics: live textures (uploadTexture - unloadTexture) and SceneModel meshes (uploadMesh - ~SceneModel)
void gpuLiveCounts(int* textures, int* meshes);
extern int gLiveMeshes;

// All textures of one or more .IGB files, addressable by image name (external-info keys such as
// "parts_00.tga 0 1 1 1 1 0 0 0" resolve through FUN_002f3bf0 by the igTextureInfo's texture list) or by
// (file, igTextureAttr slot) for textures internal to a scene file.
class TextureLibrary {
public:
    TextureLibrary() = default;
    ~TextureLibrary();
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    // Decodes and uploads every igTextureAttr in `f`. Returns the number of textures added.
    int addFile(const IgbFile& f, std::vector<std::string>* log = nullptr);
    const GpuTexture* findByName(std::string_view imageName) const;
    const GpuTexture* findByAttr(const IgbFile& f, int attrSlot) const;
    size_t count() const { return textures_.size(); }
    const std::vector<std::unique_ptr<GpuTexture>>& textures() const { return textures_; }

private:
    std::vector<std::unique_ptr<GpuTexture>> textures_;
    std::map<std::string, GpuTexture*> byName_;
    std::map<std::string, GpuTexture*> byAttr_;  // "<path>#<slot>"
};

}  // namespace baroque::alchemy
