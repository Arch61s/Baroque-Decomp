#include "data/common.hpp"
#include "data/text_tables.hpp"
#include <fstream>

namespace baroque::data {

std::string hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string id4ToString(uint32_t id) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((id >> (8 * i)) & 0xff);
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

Bytes cstringAt(Bytes b, size_t o) {
    if (o > b.size()) throw FormatError("string offset past end at " + hex(o));
    size_t e = o;
    while (e < b.size() && b[e] != 0) ++e;
    if (e == b.size()) throw FormatError("unterminated string at " + hex(o));
    return b.subspan(o, e - o);
}

std::string asciiString(Bytes b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

std::string fixedString(Bytes b, size_t o, size_t n) {
    needBytes(b, o, n);
    size_t e = 0;
    while (e < n && b[o + e] != 0) ++e;
    return std::string(reinterpret_cast<const char*>(b.data() + o), e);
}

SjisString makeSjis(Bytes raw) {
    SjisString s;
    s.bytes.assign(raw.begin(), raw.end());
    s.utf8 = sjisToUtf8(raw);
    return s;
}

std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return std::nullopt;
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(data.data()), n)) return std::nullopt;
    return data;
}

}  // namespace baroque::data
