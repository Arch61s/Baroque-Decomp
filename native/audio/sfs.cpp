#include "audio/sfs.hpp"
#include "platform/platform.hpp"
#include <cstring>
#include <fstream>
#include <vector>

namespace baroque::audio {

namespace {
bool gEnglishVoice = true;

std::filesystem::path discArchive(const char* folder) {
    return baroque::platform::assetRoot().parent_path() / "disc" / folder / "BRQSOUND.SFS";
}
bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}
// FNV-1a 64 of the 5-sector table of the Japanese archive (Japan disc SOUND/BRQSOUND.SFS = the undub's SOUND_EN)
constexpr uint64_t kJapaneseTableHash = 0x86e05b32472052b3ull;

// disc/SOUND_EN holds the Japanese voices (an undubbed USA image); checked once per path
bool usaArchiveIsJapanese() {
    static std::filesystem::path checked;
    static bool japanese = false;
    const auto p = discArchive("SOUND_EN");
    if (p == checked) return japanese;
    std::ifstream f(p, std::ios::binary);
    std::vector<unsigned char> t(5 * SfsArchive::kSector);
    if (!f || !f.read(reinterpret_cast<char*>(t.data()), std::streamsize(t.size()))) return false;   // not set up yet
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char b : t) h = (h ^ b) * 0x100000001b3ull;
    checked = p;
    japanese = h == kJapaneseTableHash;
    return japanese;
}
}  // namespace

void setEnglishVoiceArchive(bool english) { gEnglishVoice = english; }

bool voicesAvailable(bool english) {
    if (usaArchiveIsJapanese() != english) return true;
    return fileExists(discArchive(english ? "SOUND_EN_DUB" : "SOUND_JP"));
}

std::filesystem::path soundArchivePath() {
    const bool usaJapanese = usaArchiveIsJapanese();
    if (gEnglishVoice && usaJapanese && fileExists(discArchive("SOUND_EN_DUB"))) return discArchive("SOUND_EN_DUB");
    if (!gEnglishVoice && !usaJapanese && fileExists(discArchive("SOUND_JP"))) return discArchive("SOUND_JP");
    return discArchive("SOUND_EN");
}

bool japaneseVoicesActive() {
    const auto p = soundArchivePath();
    return p == discArchive("SOUND_JP") || (p == discArchive("SOUND_EN") && usaArchiveIsJapanese());
}

bool europeVoicesActive() { return soundArchivePath() == discArchive("SOUND_EN_DUB"); }

SfsArchive::~SfsArchive() {
    if (file_) std::fclose(file_);
}

static std::FILE* openRead(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.wstring().c_str(), L"rb") != 0) return nullptr;
    return f;
#else
    return std::fopen(path.string().c_str(), "rb");
#endif
}

static bool seek64(std::FILE* f, uint64_t off) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(off), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}

bool SfsArchive::open(const std::filesystem::path& path) {
    error_.clear();
    if (file_) { std::fclose(file_); file_ = nullptr; }
    table_.clear();
    std::error_code ec;
    fileSize_ = std::filesystem::file_size(path, ec);
    if (ec) { error_ = "cannot stat " + path.string(); return false; }
    file_ = openRead(path);
    if (!file_) { error_ = "cannot open " + path.string(); return false; }

    // IOP (0x204c / 0x2114..0x2160): read sector 0, word[0] = table size in sectors, read that many.
    uint8_t first[kSector];
    if (readAt(0, first, kSector) != kSector) { error_ = "short read of table sector"; return false; }
    uint32_t nsec = 0;
    std::memcpy(&nsec, first, 4);
    if (nsec == 0 || nsec > 64) { error_ = "implausible table sector count"; return false; }
    std::vector<uint8_t> hdr(static_cast<size_t>(nsec) * kSector);
    if (readAt(0, hdr.data(), hdr.size()) != hdr.size()) { error_ = "short read of table"; return false; }
    const size_t words = hdr.size() / 4;
    table_.reserve(words);
    table_.push_back(nsec);                     // table[0] = table sector count (= first entry start)
    for (size_t i = 1; i < words; ++i) {
        uint32_t w = 0;
        std::memcpy(&w, hdr.data() + i * 4, 4);
        if (w == 0) break;                      // zero terminated
        table_.push_back(w);
    }
    if (table_.size() < 2) { error_ = "empty table"; return false; }
    // Format assertion (like the Python tool): ascending, last word = end-of-file sector.
    for (size_t i = 1; i < table_.size(); ++i) {
        if (table_[i] < table_[i - 1]) { error_ = "table not ascending"; return false; }
    }
    if (static_cast<uint64_t>(table_.back()) * kSector != fileSize_) {
        error_ = "table end sector does not match file size";
        return false;
    }
    return true;
}

SfsArchive::Span SfsArchive::span(size_t i) const {
    if (i + 1 >= table_.size()) return {0, 0};
    const uint64_t off = static_cast<uint64_t>(table_[i]) * kSector;
    const uint64_t end = static_cast<uint64_t>(table_[i + 1]) * kSector;
    return {off, static_cast<uint32_t>(end - off)};
}

size_t SfsArchive::readAt(uint64_t offset, void* dst, size_t size) const {
    if (!file_ || size == 0) return 0;
    if (offset >= fileSize_) return 0;
    if (offset + size > fileSize_) size = static_cast<size_t>(fileSize_ - offset);
    if (!seek64(file_, offset)) return 0;
    return std::fread(dst, 1, size, file_);
}

std::vector<uint8_t> SfsArchive::read(size_t i, size_t limit) const {
    const Span s = span(i);
    const size_t n = static_cast<size_t>(s.size) < limit ? s.size : limit;
    std::vector<uint8_t> out(n);
    const size_t got = readAt(s.offset, out.data(), n);
    out.resize(got);
    return out;
}

std::string SfsArchive::kind(size_t i) const {
    if (i == 0) return "BD";
    const auto m = read(i, 4);
    if (m.size() < 4) return "";
    const std::string s(reinterpret_cast<const char*>(m.data()), 4);
    if (s == "VAGp" || s == "STER" || s == "IECS") return s;
    char hex[9];
    std::snprintf(hex, sizeof hex, "%02x%02x%02x%02x", m[0], m[1], m[2], m[3]);
    return hex;
}

} // namespace baroque::audio
