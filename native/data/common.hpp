// Shared byte-reading helpers for the game-data loaders (namespace baroque::data).
// Loaders throw FormatError internally (mirroring the asserts in tools/game_formats.py) and expose
// std::optional wrappers for game code; --data-check lets the exceptions propagate to fail fast.
#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baroque::data {

using Bytes = std::span<const uint8_t>;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string hex(uint64_t v);

// Little-endian scalar reads with bounds checks (PS2 is little-endian; all files are).
inline void needBytes(Bytes b, size_t o, size_t n) {
    if (o + n > b.size()) throw FormatError("read of " + std::to_string(n) + " bytes past end at " + hex(o));
}
inline uint32_t u32le(Bytes b, size_t o) { needBytes(b, o, 4); uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; }
inline int32_t s32le(Bytes b, size_t o) { needBytes(b, o, 4); int32_t v; std::memcpy(&v, b.data() + o, 4); return v; }
inline float f32le(Bytes b, size_t o) { needBytes(b, o, 4); float v; std::memcpy(&v, b.data() + o, 4); return v; }
inline uint16_t u16le(Bytes b, size_t o) { needBytes(b, o, 2); uint16_t v; std::memcpy(&v, b.data() + o, 2); return v; }
inline int16_t s16le(Bytes b, size_t o) { needBytes(b, o, 2); int16_t v; std::memcpy(&v, b.data() + o, 2); return v; }
inline float bitsToFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
inline float bitsToFloat(int32_t i) { float f; std::memcpy(&f, &i, 4); return f; }

// A 4-char tag as the game compares it: the little-endian word of the bytes ('ACTN' at 0x44b2f0 etc.).
constexpr uint32_t tag4(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
inline uint32_t id4(std::string_view s) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4 && i < s.size(); ++i) v |= static_cast<uint32_t>(static_cast<uint8_t>(s[i])) << (8 * i);
    return v;
}
std::string id4ToString(uint32_t id);  // trailing NULs removed

// NUL-terminated string at offset o (asserts the terminator exists, like Python cstr()).
Bytes cstringAt(Bytes b, size_t o);
std::string asciiString(Bytes b);  // bytes copied as chars
std::string fixedString(Bytes b, size_t o, size_t n);  // up to first NUL within n bytes

// Raw Shift-JIS text plus a UTF-8 rendering through the game's own glyph set (text_tables::sjisToUtf8).
struct SjisString {
    std::vector<uint8_t> bytes;  // without the terminator
    std::string utf8;
};
SjisString makeSjis(Bytes raw);

std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path);

// Runs a throwing parser and converts FormatError into nullopt (+ message).
template <class F>
auto guarded(F&& f, std::string* error) -> std::optional<decltype(f())> {
    try {
        return f();
    } catch (const FormatError& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

}  // namespace baroque::data
