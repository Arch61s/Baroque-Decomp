// igImage/igClut decoding (tools/igb.py image_rgba / clut_colors) and raylib texture upload.
#include "alchemy/texture.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <cstring>

namespace baroque::alchemy {

bool clutColors(const IgbFile& f, const IgObject& clut, std::vector<uint8_t>& out, std::string* error) {
    const IgMemory* data = f.memory(clut.getRef("_pData"));
    const uint32_t n = clut.getUInt("_numEntries", 0);
    const int stride = clut.getInt("_stride", 4);
    if (data == nullptr) {
        if (error) *error = "igClut without _pData";
        return false;
    }
    if (stride != 4 && stride != 2) {
        if (error) *error = "unsupported clut stride " + std::to_string(stride);
        return false;
    }
    if (data->size < n * uint32_t(stride)) {
        if (error) *error = "igClut data too small";
        return false;
    }
    out.assign(size_t(n) * 4, 0);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t r, g, b, a;
        if (stride == 4) {
            r = data->data[i * 4 + 0];
            g = data->data[i * 4 + 1];
            b = data->data[i * 4 + 2];
            a = data->data[i * 4 + 3];
        } else {
            const uint16_t v = uint16_t(data->data[i * 2] | (data->data[i * 2 + 1] << 8));
            r = uint8_t((v & 31) << 3);
            g = uint8_t(((v >> 5) & 31) << 3);
            b = uint8_t(((v >> 10) & 31) << 3);
            a = (v & 0x8000) ? uint8_t(0x80) : uint8_t(0);
        }
        out[i * 4 + 0] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = ps2Alpha(a);
    }
    if (n == 256) {
        // PS2 CSM1 32-bit CLUT ordering: within each 32-entry group, entries 8..15 and 16..23 swap.
        std::vector<uint8_t> fixed(out);
        for (size_t base = 0; base < 256; base += 32) {
            std::memcpy(&fixed[(base + 8) * 4], &out[(base + 16) * 4], 8 * 4);
            std::memcpy(&fixed[(base + 16) * 4], &out[(base + 8) * 4], 8 * 4);
        }
        out.swap(fixed);
    }
    return true;
}

bool decodeImage(const IgbFile& f, const IgObject& img, DecodedImage& out, std::string* error) {
    const uint32_t w = img.getUInt("_px", 0);
    const uint32_t h = img.getUInt("_py", 0);
    uint32_t bits = img.getUInt("_bitsDepth", 0);
    if (bits == 0) bits = img.getUInt("_bitsIdx", 0);
    const IgMemory* pix = f.memory(img.getRef("_pImage"));
    const IgObject* clut = f.object(img.getRef("_pClut"));
    const uint32_t bidx = img.getUInt("_bitsIdx", 0);
    out.name = std::string(img.getString("_pNameString"));
    out.width = int(w);
    out.height = int(h);
    out.rgba.assign(size_t(w) * h * 4, 0);
    if (w == 0 || h == 0) return true;
    const size_t npix = size_t(w) * h;
    const std::vector<uint8_t> empty;
    const std::vector<uint8_t>& data = pix != nullptr ? pix->data : empty;
    if (clut != nullptr && bidx != 0) {
        std::vector<uint8_t> cols;
        if (!clutColors(f, *clut, cols, error)) return false;
        const size_t ncol = cols.size() / 4;
        if (bidx == 8) {
            if (data.size() < npix) {
                if (error) *error = "8-bit image data too small";
                return false;
            }
            for (size_t i = 0; i < npix; ++i) {
                const size_t idx = data[i];
                if (idx < ncol) std::memcpy(&out.rgba[i * 4], &cols[idx * 4], 4);
            }
        } else if (bidx == 4) {
            if (data.size() < (npix + 1) / 2) {
                if (error) *error = "4-bit image data too small";
                return false;
            }
            for (size_t i = 0; i < npix; ++i) {
                const uint8_t v = data[i >> 1];
                const size_t idx = (i & 1) == 0 ? (v & 15) : (v >> 4);  // low nibble = left pixel
                if (idx < ncol) std::memcpy(&out.rgba[i * 4], &cols[idx * 4], 4);
            }
        } else {
            if (error) *error = "unsupported index depth " + std::to_string(bidx);
            return false;
        }
    } else if (bits == 32) {
        if (data.size() < npix * 4) {
            if (error) *error = "32-bit image data too small";
            return false;
        }
        for (size_t i = 0; i < npix; ++i) {
            out.rgba[i * 4 + 0] = data[i * 4 + 0];
            out.rgba[i * 4 + 1] = data[i * 4 + 1];
            out.rgba[i * 4 + 2] = data[i * 4 + 2];
            out.rgba[i * 4 + 3] = ps2Alpha(data[i * 4 + 3]);
        }
    } else if (bits == 24) {
        if (data.size() < npix * 3) {
            if (error) *error = "24-bit image data too small";
            return false;
        }
        for (size_t i = 0; i < npix; ++i) {
            std::memcpy(&out.rgba[i * 4], &data[i * 3], 3);
            out.rgba[i * 4 + 3] = 255;
        }
    } else if (bits == 16) {
        if (data.size() < npix * 2) {
            if (error) *error = "16-bit image data too small";
            return false;
        }
        for (size_t i = 0; i < npix; ++i) {
            const uint16_t v = uint16_t(data[i * 2] | (data[i * 2 + 1] << 8));
            out.rgba[i * 4 + 0] = uint8_t((v & 31) << 3);
            out.rgba[i * 4 + 1] = uint8_t(((v >> 5) & 31) << 3);
            out.rgba[i * 4 + 2] = uint8_t(((v >> 10) & 31) << 3);
            out.rgba[i * 4 + 3] = (v & 0x8000) ? uint8_t(255) : uint8_t(0);
        }
    } else {
        if (error) *error = "unsupported image format bits=" + std::to_string(bits);
        return false;
    }
    return true;
}

TextureParams TextureParams::fromAttr(const IgObject& a) {
    TextureParams p;
    p.magFilter = a.getInt("_magFilter", 1);
    p.minFilter = a.getInt("_minFilter", 1);
    p.wrapS = a.getInt("_wrapS", 1);
    p.wrapT = a.getInt("_wrapT", 1);
    p.mipmapMode = a.getInt("_mipmapMode", 0);
    return p;
}

namespace {

// GS TEX1 filter order (FUN_00383110 table 5 is the identity) -> GL.
int glFilter(int alchemy, bool hasMips) {
    switch (alchemy) {
    case 0: return RL_TEXTURE_FILTER_NEAREST;
    case 1: return RL_TEXTURE_FILTER_LINEAR;
    case 2: return hasMips ? RL_TEXTURE_FILTER_MIP_NEAREST : RL_TEXTURE_FILTER_NEAREST;
    case 3: return hasMips ? RL_TEXTURE_FILTER_LINEAR_MIP_NEAREST : RL_TEXTURE_FILTER_LINEAR;
    case 4: return hasMips ? RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR : RL_TEXTURE_FILTER_NEAREST;
    case 5: return hasMips ? RL_TEXTURE_FILTER_MIP_LINEAR : RL_TEXTURE_FILTER_LINEAR;
    default: return RL_TEXTURE_FILTER_LINEAR;
    }
}

// Attr wrap enum -> GS CLAMP mode via table {1,0,2,3}: 0 CLAMP, 1 REPEAT, 2 REGION_CLAMP, 3 REGION_REPEAT.
// REGION_* need per-texture U/V ranges (igTextureAttr has none) -> treated as the plain variants (UNVERIFIED).
int glWrap(int alchemy) {
    switch (alchemy) {
    case 0: return RL_TEXTURE_WRAP_CLAMP;
    case 1: return RL_TEXTURE_WRAP_REPEAT;
    case 2: return RL_TEXTURE_WRAP_CLAMP;
    case 3: return RL_TEXTURE_WRAP_REPEAT;
    default: return RL_TEXTURE_WRAP_REPEAT;
    }
}

}  // namespace

// PORT diagnostics (PLAYTHROUGH_FULL memory trend): live GL objects created through this module
int gLiveTextures = 0;
int gLiveMeshes = 0;
void gpuLiveCounts(int* textures, int* meshes) {
    *textures = gLiveTextures;
    *meshes = gLiveMeshes;
}

GpuTexture uploadTexture(const DecodedImage& image, const std::vector<DecodedImage>& mips, const TextureParams& params) {
    GpuTexture t;
    ++gLiveTextures;
    t.name = image.name;
    t.width = image.width;
    t.height = image.height;
    // rlLoadTexture expects all mip levels contiguous, largest first.
    std::vector<uint8_t> all(image.rgba);
    int levels = 1;
    for (const DecodedImage& m : mips) {
        if (m.width <= 0 || m.height <= 0) break;
        all.insert(all.end(), m.rgba.begin(), m.rgba.end());
        ++levels;
    }
    t.mipmaps = levels;
    t.id = rlLoadTexture(all.data(), image.width, image.height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, levels);
    const bool hasMips = levels > 1;
    rlTextureParameters(t.id, RL_TEXTURE_MAG_FILTER, glFilter(params.magFilter, false));
    rlTextureParameters(t.id, RL_TEXTURE_MIN_FILTER, glFilter(params.minFilter, hasMips));
    rlTextureParameters(t.id, RL_TEXTURE_WRAP_S, glWrap(params.wrapS));
    rlTextureParameters(t.id, RL_TEXTURE_WRAP_T, glWrap(params.wrapT));
    return t;
}

void unloadTexture(GpuTexture& tex) {
    if (tex.id != 0) --gLiveTextures;
    if (tex.id != 0) rlUnloadTexture(tex.id);
    tex.id = 0;
}

TextureLibrary::~TextureLibrary() {
    for (auto& t : textures_) unloadTexture(*t);
}

int TextureLibrary::addFile(const IgbFile& f, std::vector<std::string>* log) {
    int added = 0;
    for (const IgObject* attr : f.find("igTextureAttr")) {
        const IgObject* img = f.object(attr->getRef("_image"));
        if (img == nullptr) {
            if (log) log->push_back("igTextureAttr#" + std::to_string(attr->slot) + " without _image");
            continue;
        }
        DecodedImage di;
        std::string err;
        if (!decodeImage(f, *img, di, &err)) {
            if (log) log->push_back("igTextureAttr#" + std::to_string(attr->slot) + ": " + err);
            continue;
        }
        std::vector<DecodedImage> mips;
        for (const IgObject* mip : f.listObjects(f.object(attr->getRef("_imageMipMaps")))) {
            DecodedImage dm;
            if (mip->isA("igImage") && decodeImage(f, *mip, dm, &err)) mips.push_back(std::move(dm));
        }
        auto tex = std::make_unique<GpuTexture>(uploadTexture(di, mips, TextureParams::fromAttr(*attr)));
        tex->sourceFile = f.path().string();
        tex->attrSlot = attr->slot;
        GpuTexture* raw = tex.get();
        textures_.push_back(std::move(tex));
        if (!raw->name.empty() && byName_.count(raw->name) == 0) byName_[raw->name] = raw;
        byAttr_[raw->sourceFile + "#" + std::to_string(attr->slot)] = raw;
        ++added;
    }
    return added;
}

const GpuTexture* TextureLibrary::findByName(std::string_view imageName) const {
    auto it = byName_.find(std::string(imageName));
    return it == byName_.end() ? nullptr : it->second;
}

const GpuTexture* TextureLibrary::findByAttr(const IgbFile& f, int attrSlot) const {
    auto it = byAttr_.find(f.path().string() + "#" + std::to_string(attrSlot));
    return it == byAttr_.end() ? nullptr : it->second;
}

}  // namespace baroque::alchemy
