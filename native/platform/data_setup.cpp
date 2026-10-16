// PORT (packaging): first-start data setup from the player's disc image (see data_setup.hpp). The DATA.DIR walk is
// tools/unpack_data.py translated (same output tree as work/assets, verified file-by-file against its manifest).
#include "platform/data_setup.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
namespace baroque::platform {
namespace {
namespace fs = std::filesystem;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// ISO 9660 reader (PS2 DVD images carry an ISO 9660 tree next to UDF; 2048-byte logical blocks).
class IsoImage {
public:
    struct Entry { std::string name; uint32_t extent = 0, size = 0; bool dir = false; };
    bool open(const fs::path& p, std::string* err) {
        f_.open(p, std::ios::binary);
        if (!f_) { *err = "cannot open " + p.filename().string(); return false; }
        f_.seekg(0, std::ios::end);
        size_ = uint64_t(f_.tellg());
        uint8_t s[2048];
        for (uint32_t lba = 16; lba < 64; ++lba) {
            if (!read(uint64_t(lba) * 2048, s, sizeof s)) break;
            if (std::memcmp(s + 1, "CD001", 5) != 0) break;
            if (s[0] == 1) {
                block_ = uint32_t(s[128]) | uint32_t(s[129]) << 8;
                root_ = {"", le32(s + 156 + 2), le32(s + 156 + 10), true};
                return block_ == 2048;
            }
            if (s[0] == 255) break;
        }
        *err = p.filename().string() + " is not an ISO 9660 disc image (a 2048-byte-sector .iso is required)";
        return false;
    }
    bool read(uint64_t off, void* dst, size_t n) {
        if (off + n > size_) return false;
        f_.clear();
        f_.seekg(std::streamoff(off));
        f_.read(static_cast<char*>(dst), std::streamsize(n));
        return bool(f_);
    }
    uint64_t offsetOf(const Entry& e) const { return uint64_t(e.extent) * block_; }
    std::vector<Entry> list(const Entry& d) {
        std::vector<Entry> out;
        std::vector<uint8_t> buf(d.size);
        if (!read(offsetOf(d), buf.data(), buf.size())) return out;
        size_t pos = 0;
        while (pos < buf.size()) {
            const uint8_t len = buf[pos];
            if (len == 0) { pos = (pos / block_ + 1) * block_; continue; }
            if (pos + len > buf.size() || len < 34) break;
            const uint8_t nameLen = buf[pos + 32];
            std::string name(reinterpret_cast<const char*>(&buf[pos + 33]), std::min<size_t>(nameLen, len - 33));
            if (!(nameLen == 1 && (name[0] == 0 || name[0] == 1))) {
                if (const auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
                if (!name.empty() && name.back() == '.') name.pop_back();
                out.push_back({name, le32(&buf[pos + 2]), le32(&buf[pos + 10]), (buf[pos + 25] & 2) != 0});
            }
            pos += len;
        }
        return out;
    }
    std::optional<Entry> find(const std::string& path) {   // "DIR/FILE", case-insensitive
        Entry cur = root_;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            bool found = false;
            for (const Entry& e : list(cur)) {
                if (e.name.size() != part.size()) continue;
                if (!std::equal(e.name.begin(), e.name.end(), part.begin(),
                                [](char a, char b) { return std::toupper(uint8_t(a)) == std::toupper(uint8_t(b)); }))
                    continue;
                cur = e;
                found = true;
                break;
            }
            if (!found) return std::nullopt;
            if (slash == std::string::npos) return cur;
            start = slash + 1;
        }
        return std::nullopt;
    }
private:
    std::ifstream f_;
    uint64_t size_ = 0;
    uint32_t block_ = 2048;
    Entry root_;
};

// tools/unpack_data.py parse(): 32-byte directory records (name[16], type, span, file count, file table offset),
// depth-first; 28-byte file records (name[16], type, size, offset). Roots after the first are pack files.
struct DataFile { std::string path; uint32_t size = 0, offset = 0; int tree = 0; uint64_t abs = 0; std::string rel; };

std::string cstr16(const uint8_t* p) {
    size_t n = 0;
    while (n < 16 && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

bool parseDataDir(const std::vector<uint8_t>& d, uint64_t binSize, std::vector<DataFile>* out, std::string* err) {
    constexpr size_t kTable = 0x9480;
    if (d.size() < kTable) { *err = "DATA.DIR is too small"; return false; }
    bool ok = true;
    int tree = 0;
    auto readDir = [&](auto&& self, size_t index, const std::string& prefix, int depth) -> size_t {
        const size_t o = index * 32;
        if (o + 32 > kTable || depth > 64) { ok = false; return index + 1; }
        const std::string full = prefix + cstr16(&d[o]);
        const uint32_t span = le32(&d[o + 20]), files = le32(&d[o + 24]), table = le32(&d[o + 28]);
        for (uint32_t i = 0; i < files; ++i) {
            const size_t fo = size_t(table) + size_t(i) * 28;
            if (fo + 28 > d.size()) { ok = false; return index + 1; }
            out->push_back({full + "/" + cstr16(&d[fo]), le32(&d[fo + 20]), le32(&d[fo + 24]), tree, 0, {}});
        }
        size_t child = index + 1;
        const size_t end = index + span;
        if (span == 0) { ok = false; return index + 1; }
        while (ok && child < end) child = self(self, child, full + "/", depth + 1);
        if (child != end) ok = false;
        return end;
    };
    size_t index = 0;
    const size_t total = kTable / 32;
    while (ok && index < total && d[index * 32] != 0) {
        index = readDir(readDir, index, "", 0);
        ++tree;
    }
    if (!ok || index != total - 1) { *err = "DATA.DIR has an unexpected layout (not the Baroque USA disc?)"; return false; }
    std::map<std::string, const DataFile*> packs;   // tree-0 files directly under DATA/
    std::set<std::string> packRoots;
    for (const DataFile& e : *out) {
        if (e.tree == 0 && std::count(e.path.begin(), e.path.end(), '/') == 1) packs[e.path.substr(e.path.find('/') + 1)] = &e;
        if (e.tree != 0) packRoots.insert(e.path.substr(0, e.path.find('/')));
    }
    for (DataFile& e : *out) {
        if (e.tree == 0) {
            e.abs = e.offset;
            e.rel = e.path;
            if (std::count(e.path.begin(), e.path.end(), '/') == 1 && packRoots.count(e.path.substr(e.path.find('/') + 1)))
                e.rel += ".pack";
        } else {
            const auto it = packs.find(e.path.substr(0, e.path.find('/')));
            if (it == packs.end() || uint64_t(e.offset) + e.size > it->second->size) {
                *err = "DATA.DIR pack entry out of range: " + e.path;
                return false;
            }
            e.abs = uint64_t(it->second->offset) + e.offset;
            e.rel = "PACKS/" + e.path;
        }
        if (e.abs + e.size > binSize) { *err = "DATA.BIN is truncated (" + e.path + ")"; return false; }
    }
    return true;
}

bool copyRange(IsoImage& iso, uint64_t off, uint64_t size, const fs::path& dst, DataSetupProgress& prog,
               std::vector<char>& buf, std::string* err) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) { *err = "cannot write " + dst.string(); return false; }
    uint64_t left = size;
    while (left > 0) {
        if (prog.cancel.load()) { *err = "cancelled"; return false; }
        const size_t n = size_t(std::min<uint64_t>(left, buf.size()));
        if (!iso.read(off, buf.data(), n)) { *err = "read error in the disc image"; return false; }
        out.write(buf.data(), std::streamsize(n));
        if (!out) { *err = "write error (disk full?) at " + dst.string(); return false; }
        off += n;
        left -= n;
        prog.doneBytes += n;
    }
    return true;
}
}  // namespace

DataStatus gameDataStatus() {
    std::error_code ec;
    DataStatus s;
    s.assets = fs::exists(assetRoot() / "DATA", ec);
    const fs::path disc = discRoot();
    s.disc = fs::exists(disc / "SOUND_EN" / "BRQSOUND.SFS", ec) && fs::exists(disc / "MOVIE_EN" / "OPENING.PSS", ec) &&
             fs::exists(disc / "SLUS_217.14", ec);
    return s;
}

std::vector<fs::path> findDiscImages(const fs::path& dir) {
    std::vector<std::pair<uintmax_t, fs::path>> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
        if (ext == ".iso" && e.is_regular_file(ec)) found.push_back({e.file_size(ec), e.path()});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<fs::path> out;
    for (auto& f : found) out.push_back(f.second);
    return out;
}

bool extractGameData(const fs::path& isoPath, const fs::path& dest, bool wantAssets, bool wantDisc,
                     DataSetupProgress& prog, std::string* error) {
    std::string err;
    IsoImage iso;
    prog.phase = 0;
    if (!iso.open(isoPath, error)) return false;
    // SYSTEM.CNF names the boot ELF: the port is the North American release (SLUS-21714; undubbed images work too)
    const auto cnf = iso.find("SYSTEM.CNF");
    const auto boot = iso.find("SLUS_217.14");
    if (!cnf || !boot) {
        *error = isoPath.filename().string() + " is not Baroque (USA, SLUS-21714). The port needs the North American disc.";
        return false;
    }
    const auto dirE = iso.find("DATA.DIR"), binE = iso.find("DATA.BIN"), sfsE = iso.find("SOUND_EN/BRQSOUND.SFS");
    const auto movieDir = iso.find("MOVIE_EN");
    if (!dirE || !binE || !sfsE || !movieDir) { *error = "the disc image is missing DATA.BIN / MOVIE_EN / SOUND_EN"; return false; }

    std::vector<DataFile> files;
    if (wantAssets) {
        std::vector<uint8_t> dirBytes(dirE->size);
        if (!iso.read(iso.offsetOf(*dirE), dirBytes.data(), dirBytes.size())) { *error = "cannot read DATA.DIR"; return false; }
        if (!parseDataDir(dirBytes, binE->size, &files, error)) return false;
        std::sort(files.begin(), files.end(), [](const DataFile& a, const DataFile& b) { return a.abs < b.abs; });
    }
    struct DiscFile { std::string rel; IsoImage::Entry e; };
    std::vector<DiscFile> discFiles;
    if (wantDisc) {
        for (const auto& e : iso.list(*movieDir))
            if (!e.dir) discFiles.push_back({"MOVIE_EN/" + e.name, e});
        discFiles.push_back({"SLUS_217.14", *boot});              // the menu text table (game/ui/strings.cpp)
        discFiles.push_back({"SOUND_EN/BRQSOUND.SFS", *sfsE});   // last: its presence marks the disc set complete
    }

    std::error_code ec;
    uint64_t need = 0;
    for (const DataFile& f : files) need += f.size;
    for (const DiscFile& f : discFiles) {
        const fs::path dst = dest / "disc" / f.rel;
        if (!(fs::exists(dst, ec) && fs::file_size(dst, ec) == f.e.size)) need += f.e.size;
    }
    prog.totalBytes = need;
    prog.totalFiles = uint32_t(files.size() + discFiles.size());
    const auto space = fs::space(dest, ec);
    if (!ec && space.available < need + (64ull << 20)) {
        *error = "not enough free disk space: " + std::to_string((need >> 20) + 64) + " MB needed next to Baroque.exe, " +
                 std::to_string(space.available >> 20) + " MB free";
        return false;
    }

    std::vector<char> buf(4u << 20);
    if (wantAssets) {
        prog.phase = 1;
        const fs::path stage = dest / "assets.partial", target = dest / "assets";
        fs::remove_all(stage, ec);
        if (fs::exists(target, ec)) {
            if (!fs::is_empty(target, ec)) {
                *error = "the folder 'assets' next to Baroque.exe exists but has no DATA folder. Delete it and start again.";
                return false;
            }
            fs::remove(target, ec);
        }
        const uint64_t binBase = iso.offsetOf(*binE);
        for (const DataFile& f : files) {
            if (!copyRange(iso, binBase + f.abs, f.size, stage / fs::path(f.rel), prog, buf, error)) {
                fs::remove_all(stage, ec);
                return false;
            }
            ++prog.doneFiles;
        }
        fs::rename(stage, target, ec);
        if (ec) { *error = "cannot rename assets.partial to assets: " + ec.message(); return false; }
        log("data setup: " + std::to_string(files.size()) + " files extracted to " + target.string());
    }
    if (wantDisc) {
        prog.phase = 2;
        for (const DiscFile& f : discFiles) {
            const fs::path dst = dest / "disc" / fs::path(f.rel);
            if (fs::exists(dst, ec) && fs::file_size(dst, ec) == f.e.size) { ++prog.doneFiles; continue; }   // resumed run
            fs::path tmp = dst;
            tmp += ".partial";
            if (!copyRange(iso, iso.offsetOf(f.e), f.e.size, tmp, prog, buf, error)) {
                fs::remove(tmp, ec);
                return false;
            }
            fs::rename(tmp, dst, ec);
            if (ec) { *error = "cannot write " + dst.string() + ": " + ec.message(); return false; }
            ++prog.doneFiles;
        }
        log("data setup: " + std::to_string(discFiles.size()) + " movie / sound files copied to " + (dest / "disc").string());
    }
    prog.phase = 3;
    return true;
}

int identifyDisc(const fs::path& isoPath) {
    IsoImage iso;
    std::string err;
    if (!iso.open(isoPath, &err)) return 0;
    if (iso.find("SLUS_217.14")) return 1;
    if (iso.find("SLES_552.16")) return 2;
    if (iso.find("SLPM_667.47")) return 3;
    return 0;
}

bool englishDubPresent() {
    std::error_code ec;
    return fs::exists(discRoot() / "SOUND_EN_DUB" / "BRQSOUND.SFS", ec);
}

bool japaneseVoicesPresent() {
    std::error_code ec;
    return fs::exists(discRoot() / "SOUND_JP" / "BRQSOUND.SFS", ec);
}

bool extractJapaneseVoices(const fs::path& isoPath, DataSetupProgress& prog, std::string* error) {
    IsoImage iso;
    prog.phase = 0;
    if (!iso.open(isoPath, error)) return false;
    if (!iso.find("SLPM_667.47")) {
        *error = isoPath.filename().string() + " is not Baroque (Japan, SLPM-66747).";
        return false;
    }
    const auto sfsE = iso.find("SOUND/BRQSOUND.SFS");
    if (!sfsE) { *error = "the Japan disc image is missing SOUND/BRQSOUND.SFS"; return false; }
    const fs::path dst = discRoot() / "SOUND_JP" / "BRQSOUND.SFS";
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    const auto space = fs::space(dst.parent_path(), ec);
    if (!ec && space.available < uint64_t(sfsE->size) + (64ull << 20)) {
        *error = "not enough free disk space for the Japanese voices (" + std::to_string((sfsE->size >> 20) + 64) + " MB)";
        return false;
    }
    prog.totalBytes = sfsE->size;
    prog.totalFiles = 1;
    prog.phase = 2;
    std::vector<char> buf(4u << 20);
    fs::path tmp = dst;
    tmp += ".partial";
    if (!copyRange(iso, iso.offsetOf(*sfsE), sfsE->size, tmp, prog, buf, error)) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, dst, ec);   // its presence marks the Japanese voices as set up
    if (ec) { *error = "cannot write " + dst.string() + ": " + ec.message(); return false; }
    ++prog.doneFiles;
    log("voice setup: Japanese voices in " + dst.string());
    prog.phase = 3;
    return true;
}

namespace {
// MPEG-2 program stream walk of tools/pss_movie.py demux(): calls audio(offset, size) for the payload of every
// private stream 1 (0xBD) packet after its 4-byte FF A0 00 00 prefix. False on a malformed stream.
template <class F>
bool walkPssAudio(const std::vector<uint8_t>& d, F&& audio) {
    const size_t n = d.size();
    size_t i = 0;
    while (i + 4 <= n) {
        if (d[i] != 0 || d[i + 1] != 0 || d[i + 2] != 1) { ++i; continue; }
        const uint8_t sid = d[i + 3];
        if (sid == 0xBA) {
            if (i + 14 > n) return false;
            i += (d[i + 4] & 0xC0) == 0x40 ? 14 + (d[i + 13] & 7) : 12;
            continue;
        }
        if (sid == 0xB9) break;
        if (i + 9 > n) return false;
        const size_t plen = size_t(d[i + 4]) << 8 | d[i + 5];
        if (sid == 0xBD) {
            const size_t p0 = i + 9 + d[i + 8], p1 = i + 6 + plen;
            if (p1 > n || p0 + 4 > p1 || d[p0] != 0xFF || d[p0 + 1] != 0xA0) return false;
            audio(p0 + 4, p1 - (p0 + 4));
        }
        i += 6 + plen;
    }
    return true;
}

bool readWhole(const fs::path& p, std::vector<uint8_t>* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out->resize(size_t(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out->data()), std::streamsize(out->size()));
    return bool(f);
}

bool writeWhole(const fs::path& dst, const std::vector<uint8_t>& d, std::string* err) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    fs::path tmp = dst;
    tmp += ".partial";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        if (!out) { *err = "write error (disk full?) at " + dst.string(); return false; }
    }
    fs::rename(tmp, dst, ec);
    if (ec) { *err = "cannot write " + dst.string() + ": " + ec.message(); return false; }
    return true;
}
}  // namespace

bool extractEnglishDub(const fs::path& isoPath, DataSetupProgress& prog, std::string* error) {
    IsoImage iso;
    prog.phase = 0;
    if (!iso.open(isoPath, error)) return false;
    if (!iso.find("SLES_552.16")) {
        *error = isoPath.filename().string() + " is not Baroque (Europe, SLES-55216).";
        return false;
    }
    const auto sfsE = iso.find("SOUND_EN/BRQSOUND.SFS");
    const auto movieDir = iso.find("MOVIE_P");
    if (!sfsE || !movieDir) { *error = "the Europe disc image is missing SOUND_EN / MOVIE_P"; return false; }
    const fs::path disc = discRoot();
    std::error_code ec;
    if (!fs::exists(disc / "MOVIE_EN" / "OPENING.PSS", ec)) { *error = "set up the USA disc first"; return false; }
    std::vector<IsoImage::Entry> movies;
    for (const auto& e : iso.list(*movieDir))
        if (!e.dir) movies.push_back(e);
    uint64_t need = sfsE->size;
    for (const auto& e : movies) need += e.size;
    prog.totalBytes = need;
    prog.totalFiles = uint32_t(movies.size() + 1);
    const auto space = fs::space(disc, ec);
    if (!ec && space.available < uint64_t(sfsE->size) + (64ull << 20)) {
        *error = "not enough free disk space for the English voices (" + std::to_string((sfsE->size >> 20) + 64) + " MB)";
        return false;
    }
    prog.phase = 2;
    int mixed = 0;
    for (const auto& e : movies) {
        if (prog.cancel.load()) { *error = "cancelled"; return false; }
        std::vector<uint8_t> us, eu(e.size);
        const fs::path usPath = disc / "MOVIE_EN" / e.name;
        if (!readWhole(usPath, &us) || !iso.read(iso.offsetOf(e), eu.data(), eu.size())) {
            log("dub setup: " + e.name + " skipped (no USA movie / read error)");
            prog.doneBytes += e.size;
            ++prog.doneFiles;
            continue;
        }
        std::vector<uint8_t> euAudio;
        bool ok = walkPssAudio(eu, [&](size_t o, size_t n) { euAudio.insert(euAudio.end(), eu.begin() + o, eu.begin() + o + n); });
        size_t usAudio = 0;
        bool same = true;
        ok = ok && walkPssAudio(us, [&](size_t o, size_t n) {
            if (usAudio + n > euAudio.size() || std::memcmp(&us[o], &euAudio[usAudio], n) != 0) same = false;
            usAudio += n;
        });
        if (!ok || usAudio != euAudio.size()) {
            log("dub setup: " + e.name + " kept (audio tracks do not line up)");
        } else if (!same) {
            size_t pos = 0;
            walkPssAudio(us, [&](size_t o, size_t n) { std::memcpy(&us[o], &euAudio[pos], n); pos += n; });
            if (!writeWhole(disc / "MOVIE_EN_DUB" / e.name, us, error)) return false;
            ++mixed;
        }
        prog.doneBytes += e.size;
        ++prog.doneFiles;
    }
    std::vector<char> buf(4u << 20);
    const fs::path dst = disc / "SOUND_EN_DUB" / "BRQSOUND.SFS";
    fs::path tmp = dst;
    tmp += ".partial";
    if (!copyRange(iso, iso.offsetOf(*sfsE), sfsE->size, tmp, prog, buf, error)) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, dst, ec);   // last: its presence marks the dub as set up
    if (ec) { *error = "cannot write " + dst.string() + ": " + ec.message(); return false; }
    ++prog.doneFiles;
    log("dub setup: English voices in " + dst.string() + ", " + std::to_string(mixed) + " movie(s) with English audio in " +
        (disc / "MOVIE_EN_DUB").string());
    prog.phase = 3;
    return true;
}
}
