#include "audio/adpcm.hpp"
#include <algorithm>
#include <cstring>

namespace baroque::audio {

namespace {
// VAG_F in tools/sound_formats.py: exact /64 predictor table.
constexpr int32_t kF0[5] = {0, 60, 115, 98, 122};
constexpr int32_t kF1[5] = {0, 0, -52, -55, -60};

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
std::string cstr16(const uint8_t* p) {
    size_t n = 0;
    while (n < 16 && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}
} // namespace

uint8_t decodeAdpcmBlock(const uint8_t block[16], AdpcmState& st, int16_t out[28]) {
    const int shift = block[0] & 0x0f;
    const int pred = block[0] >> 4;
    const uint8_t flags = block[1];
    const int32_t f0 = pred < 5 ? kF0[pred] : 0;
    const int32_t f1 = pred < 5 ? kF1[pred] : 0;
    int32_t s1 = st.s1, s2 = st.s2;
    int n = 0;
    for (int i = 0; i < 14; ++i) {
        const uint8_t b = block[2 + i];
        for (int k = 0; k < 2; ++k) {
            const int nib = k == 0 ? (b & 0x0f) : (b >> 4);
            int32_t v = nib << 12;
            if (v & 0x8000) v -= 0x10000;
            // Python: s = (v >> shift) + ((s1*f0 + s2*f1 + 32) >> 6); both shifts arithmetic (C++20).
            int32_t s = (v >> shift) + ((s1 * f0 + s2 * f1 + 32) >> 6);
            s = std::clamp(s, -32768, 32767);
            s2 = s1;
            s1 = s;
            out[n++] = static_cast<int16_t>(s);
        }
    }
    st.s1 = s1;
    st.s2 = s2;
    return flags;
}

std::vector<int16_t> decodeAdpcmMono(const uint8_t* body, size_t len, bool stopAtEnd) {
    std::vector<int16_t> pcm;
    AdpcmState st;
    int16_t tmp[28];
    // Python: for o in range(0, len(body) - 15, 16)
    for (size_t o = 0; o + 15 < len; o += 16) {
        const uint8_t flags = decodeAdpcmBlock(body + o, st, tmp);
        if (flags == kAdpcmFlagEndMute) break;
        pcm.insert(pcm.end(), tmp, tmp + 28);
        if (stopAtEnd && (flags & 1) && flags != 2) break;
    }
    return pcm;
}

bool parseSterHeader(const uint8_t* head, size_t len, SterHeader& out) {
    if (len < kSterHeaderSize) return false;
    if (std::memcmp(head, "STER", 4) != 0) return false;
    out.perChannelBytes = le32(head + 4);
    out.loopStartFileOffset = static_cast<int32_t>(le32(head + 8));
    const uint32_t perChBe = be32(head + 0xc);
    if (perChBe != out.perChannelBytes) return false; // assert like the Python tool
    out.sampleRate = be32(head + 0x10);
    out.name = cstr16(head + 0x20);
    return true;
}

bool decodeSterEntry(const uint8_t* data, size_t len, SterHeader& hdr,
                     std::vector<int16_t>& left, std::vector<int16_t>& right) {
    if (!parseSterHeader(data, len, hdr)) return false;
    const size_t n = hdr.perChannelBytes;
    size_t bodyLen = 2 * n;
    if (len < kSterHeaderSize) return false;
    bodyLen = std::min(bodyLen, len - kSterHeaderSize);
    const uint8_t* body = data + kSterHeaderSize;
    AdpcmState hl, hr;
    int16_t tmp[28];
    left.clear();
    right.clear();
    left.reserve(bodyLen / 32 * 28);
    right.reserve(bodyLen / 32 * 28);
    // Python: for o in range(0, len(body) - 31, 32)
    for (size_t o = 0; o + 31 < bodyLen; o += 32) {
        const uint8_t fl = decodeAdpcmBlock(body + o, hl, tmp);
        if (fl == kAdpcmFlagEndMute) break;
        left.insert(left.end(), tmp, tmp + 28);
        decodeAdpcmBlock(body + o + 16, hr, tmp);
        right.insert(right.end(), tmp, tmp + 28);
    }
    return true;
}

bool parseVagHeader(const uint8_t* data, size_t len, VagHeader& out) {
    if (len < kVagHeaderSize) return false;
    if (std::memcmp(data, "VAGp", 4) != 0) return false;
    out.version = be32(data + 4);
    out.dataSize = be32(data + 12);
    out.sampleRate = be32(data + 16);
    out.name = cstr16(data + 32);
    return true;
}

bool decodeVagEntry(const uint8_t* data, size_t len, VagHeader& hdr, std::vector<int16_t>& pcm) {
    if (!parseVagHeader(data, len, hdr)) return false;
    size_t size = hdr.dataSize;
    if (len < kVagHeaderSize) return false;
    size = std::min(size, len - kVagHeaderSize);
    pcm = decodeAdpcmMono(data + kVagHeaderSize, size, true);
    return true;
}

// ---------------------------------------------------------------- SterStreamReader
void SterStreamReader::start(Source* src, const SterHeader& header, uint32_t entrySize, int randomStartK) {
    src_ = src;
    hdr_ = header;
    entrySize_ = entrySize;
    ended_ = false;
    looped_ = false;
    stL_ = AdpcmState{};
    stR_ = AdpcmState{};
    bufPos_ = bufLen_ = 0;
    const int64_t total = int64_t(hdr_.perChannelBytes) * 2; // stereo: +0x28 = N*2 (L_053f4)
    if (randomStartK > 0) {
        // L_05294: s3 = (entrySize >> 4) * k; +0xc += s3; data read starts at entry+s3.
        const int64_t skip = int64_t(entrySize_ >> 4) * randomStartK;
        filePos_ = kLiteralRandomStartOffset ? uint64_t(skip) : uint64_t(skip + 0x30);
        // L_05424..: remaining = total - (chunk - 0x30 + s3) after the first read of chunk-0x30
        // data bytes => bytes still to stream from here = total - s3 + 0x30 ... but data starts at
        // file offset s3, so the stream runs until file offset 0x30 + total (the end of the data).
        remaining_ = total - skip + 0x30;
        if (!kLiteralRandomStartOffset) remaining_ = total - skip;
    } else {
        filePos_ = kSterHeaderSize;
        remaining_ = total;
    }
    if (remaining_ <= 0) { remaining_ = 0; ended_ = true; }
    // L_053c4..L_053f0: loop target = loop + (+0xc) - s3 = entry start + loop.
    loopFileOffset_ = hdr_.loopStartFileOffset < 0 ? -1 : int64_t(hdr_.loopStartFileOffset);
}

bool SterStreamReader::fill() {
    if (!src_ || remaining_ <= 0) return false;
    size_t want = kChunk;
    if (int64_t(want) > remaining_) want = size_t(remaining_);
    want &= ~size_t(31); // whole L/R pairs
    if (want == 0) return false;
    buf_.resize(kChunk);
    const size_t got = src_->readEntry(filePos_, buf_.data(), want);
    if (got < 32) return false;
    bufLen_ = got & ~size_t(31);
    bufPos_ = 0;
    filePos_ += bufLen_;
    remaining_ -= int64_t(bufLen_);
    return true;
}

bool SterStreamReader::nextPair(int16_t left[28], int16_t right[28], uint8_t* flagL, uint8_t* flagR) {
    if (!src_ || ended_) return false;
    if (bufPos_ + 32 > bufLen_) {
        if (!fill()) {
            // Data exhausted (L_054a8..): remaining == 0 -> loop or end.
            if (remaining_ <= 0 && loopFileOffset_ >= 0) {
                looped_ = true;
                filePos_ = uint64_t(loopFileOffset_);
                remaining_ = int64_t(hdr_.perChannelBytes) * 2 - (loopFileOffset_ - int64_t(kSterHeaderSize));
                // ADPCM history continues across the seam, as the SPU's does.
                if (!fill()) { ended_ = true; return false; }
            } else {
                ended_ = true;
                return false;
            }
        }
    }
    const uint8_t* p = buf_.data() + bufPos_;
    const uint8_t fl = decodeAdpcmBlock(p, stL_, left);
    const uint8_t fr = decodeAdpcmBlock(p + 16, stR_, right);
    bufPos_ += 32;
    if (flagL) *flagL = fl;
    if (flagR) *flagR = fr;
    return true;
}

} // namespace baroque::audio
