#include "data/archive.hpp"

#include <cstdio>
#include <fstream>

namespace baroque::data {

Archive::Archive(std::filesystem::path root) : root_(std::move(root)) {}

// orig FUN_0011df00 (work/decompiled/0011df00.c)
std::string Archive::packNameForFloor(int floorId) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "F%03d", floorId);
    return buf;
}

std::optional<std::filesystem::path> Archive::resolve(std::string_view dataPath) const {
    // Original paths use '/' separators ("DATA/MAP/FLOOR/F%03d.FDT", 0x449fd0); the unpacked tree keeps them.
    std::string rel(dataPath);
    if (!pack_.empty()) {
        // orig FUN_0010cf60 / FUN_0010d110: pack name handed along with the path -> pack tree consulted first.
        std::filesystem::path p = root_ / "PACKS" / pack_ / rel;
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    std::filesystem::path p = root_ / rel;
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) return p;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> Archive::readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return std::nullopt;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(data.data()), n)) return std::nullopt;
    return data;
}

std::optional<std::vector<uint8_t>> Archive::load(std::string_view dataPath) const {
    const auto p = resolve(dataPath);
    if (!p) return std::nullopt;
    return readFile(*p);
}

} // namespace baroque::data
