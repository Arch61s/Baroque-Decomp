#pragma once
// BRQSOUND.SFS archive index, exactly as IOPSND.IRX indexes it (work/reports/SOUND.md §2,
// tools/sound_formats.py class Sfs).
//
// Sectors 0..4 hold a u32 table: word[0] = 5 (table length in 2048-byte sectors), then ascending
// start sectors, zero terminated. The IOP uses the raw words: entry i spans sectors
// word[i] .. word[i+1]. Hence entry 0 = sectors 5..945 = the BD (raw ADPCM body of the HD bank),
// entry 1 = IECS HD, 2..28 = 27 STER BGM streams, 29..185 = 157 VAGp SE, 186..2119 = 1934 VAGp voices.
//
// Reads are lazy (pread-style on an open FILE*), the whole 360 MB file is never loaded.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace baroque::audio {

// Location of the original disc file. Not under work/assets (DATA.BIN only), so it is resolved as
// <assetRoot parent>/disc/SOUND_EN/BRQSOUND.SFS, i.e. work/disc/SOUND_EN/BRQSOUND.SFS when
// baroque::platform::assetRoot() is work/assets. A packaged build puts the file next to the EXE
// at <exe>/assets/../disc/SOUND_EN/BRQSOUND.SFS. (FUN_00114c90 selects "SOUND" vs "SOUND_EN";
// only SOUND_EN exists on this disc.)
std::filesystem::path soundArchivePath();
// PORT (voice language): disc/SOUND_EN is the USA disc's archive: English voices on a retail disc, Japanese on an
// undubbed image (recognised by its table, identical to the Japan disc's SOUND/BRQSOUND.SFS). The other language
// comes from an optional second disc (platform/data_setup.cpp, same 2120-entry layout):
//   SOUND_JP/BRQSOUND.SFS     the Japan disc (SLPM-66747) voices, for a retail USA disc
//   SOUND_EN_DUB/BRQSOUND.SFS the Europe disc (SLES-55216) English dub, for an undubbed USA image
// soundArchivePath() picks the wanted language when it is available and falls back to SOUND_EN otherwise. Read when
// the sound manager opens the archive (FUN_00114f30, reloadSoundArchive), so a change applies on the next open.
void setEnglishVoiceArchive(bool english);
bool voicesAvailable(bool english);
bool japaneseVoicesActive();     // the archive soundArchivePath() returns holds the Japanese voices
bool europeVoicesActive();       // English voices from SOUND_EN_DUB (movies come from MOVIE_EN_DUB where present)

class SfsArchive {
public:
    static constexpr uint32_t kSector = 2048;

    SfsArchive() = default;
    ~SfsArchive();
    SfsArchive(const SfsArchive&) = delete;
    SfsArchive& operator=(const SfsArchive&) = delete;

    // Opens the file and reads the 5-sector table. Returns false (with lastError set) on failure.
    bool open(const std::filesystem::path& path);
    bool isOpen() const { return file_ != nullptr; }
    const std::string& lastError() const { return error_; }

    // Number of entries (table words - 1); 2120 for the retail disc.
    size_t entryCount() const { return table_.empty() ? 0 : table_.size() - 1; }
    // Raw table words (word[0] = table sector count).
    const std::vector<uint32_t>& table() const { return table_; }

    struct Span { uint64_t offset; uint32_t size; };
    // Byte span of entry i (IOP numbering). Size is a multiple of 2048.
    Span span(size_t i) const;

    // Reads up to `limit` bytes of entry i (whole entry by default).
    std::vector<uint8_t> read(size_t i, size_t limit = SIZE_MAX) const;
    // Reads `size` bytes at absolute file offset `offset`. Returns bytes actually read.
    size_t readAt(uint64_t offset, void* dst, size_t size) const;

    // 4-byte magic of entry i ("VAGp", "STER", "IECS"), "BD" for entry 0.
    std::string kind(size_t i) const;

private:
    std::FILE* file_ = nullptr;
    uint64_t fileSize_ = 0;
    std::vector<uint32_t> table_;
    std::string error_;
};

} // namespace baroque::audio
