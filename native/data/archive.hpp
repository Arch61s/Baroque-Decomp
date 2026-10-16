#pragma once
// baroque::data::Archive - DATA.DIR/DATA.BIN tree view over the unpacked work/assets directory.
//
// Original: every map file load goes through FUN_0010cf60(task, path, packName, cb, arg, flags): the path
// (task+0x14) and, when non-null, the pack name (task+0x18, "F%03d" built by FUN_0011df00 into 0x50a330)
// are handed to the file-system object 0x53f814 by FUN_0010d110 (vtable+0xc open(path, packName, ...)).
// DATA.DIR is a forest of trees (tools/unpack_data.py): tree 0 is the main DATA tree, every other root is a
// pack listed as DATA/<name>.pack whose nested files are relative to the pack. The unpacked layout mirrors
// that: <root>/DATA/... for the main tree and <root>/PACKS/<pack>/DATA/... for the packs.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baroque::data {

class Archive {
public:
    explicit Archive(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }

    // orig FUN_0011df00 (work/decompiled/0011df00.c): sprintf(0x50a330, "F%03d" (0x449ff0), floorId).
    static std::string packNameForFloor(int floorId);

    // Set/clear the pack consulted before the main tree (the original passes 0x50a330 for every floor file).
    void setPack(std::string packName) { pack_ = std::move(packName); }
    void clearPack() { pack_.clear(); }
    const std::string& pack() const { return pack_; }

    // Resolve an original "DATA/..." path to a file on disk. With a pack set, PACKS/<pack>/<path> is tried
    // first and the main tree second (floor packs lack the room MR*.IGB models, so the fallback is required).
    // UNVERIFIED: the exact lookup order inside the file-system object 0x53f814 was not traced
    // (MAP_FORMATS.md section 11); pack-first-then-root is the only order consistent with the pack contents.
    std::optional<std::filesystem::path> resolve(std::string_view dataPath) const;
    bool exists(std::string_view dataPath) const { return resolve(dataPath).has_value(); }

    // Whole-file load. Main-tree map files are sector padded (size multiple of 2048 with garbage tail);
    // parsers return their parsed end so callers can account for it.
    std::optional<std::vector<uint8_t>> load(std::string_view dataPath) const;

    // Read a file given its resolved on-disk path.
    static std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& p);

private:
    std::filesystem::path root_;
    std::string pack_;
};

} // namespace baroque::data
