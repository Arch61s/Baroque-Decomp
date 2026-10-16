#include "video/pss_demux.hpp"

#include "platform/platform.hpp"

#include <cmath>
#include <cstring>

namespace baroque::video {
namespace {

constexpr size_t kChunkSize = 1u << 20;  // 1 MiB reads; a PES packet is at most 6 + 65535 bytes
constexpr uint32_t kFormatPcm16 = 0x01;
constexpr uint32_t kFormatAdpcm = 0x10;

uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// MPEG PES timestamp: 0010/0011 PTS[32..30] 1 PTS[29..15] 1 PTS[14..0] 1 in five bytes.
int64_t readTimestamp(const uint8_t* b) {
    return (int64_t((b[0] >> 1) & 7) << 30) | (int64_t(b[1]) << 22) | (int64_t(b[2] >> 1) << 15) |
           (int64_t(b[3]) << 7) | int64_t(b[4] >> 1);
}

// tools/sound_archive.py VAG_F table (predictor coefficients / 64).
constexpr double kVagF[5][2] = {
    {0.0, 0.0}, {60.0 / 64.0, 0.0}, {115.0 / 64.0, -52.0 / 64.0}, {98.0 / 64.0, -55.0 / 64.0}, {122.0 / 64.0, -60.0 / 64.0}};

}  // namespace

void PssDemuxer::decodeVagBlock(const uint8_t block[16], double history[2], int16_t out[28]) {
    const int shift = block[0] & 0x0f;
    const int pred = (block[0] >> 4) & 0x0f;
    const double f0 = kVagF[pred < 5 ? pred : 0][0];
    const double f1 = kVagF[pred < 5 ? pred : 0][1];
    double s1 = history[0];
    double s2 = history[1];
    int n = 0;
    for (int i = 0; i < 14; ++i) {
        const int byte = block[2 + i];
        for (int k = 0; k < 2; ++k) {
            const int nib = k == 0 ? (byte & 0x0f) : (byte >> 4);
            int v = nib << 12;
            if (v & 0x8000) v -= 0x10000;
            const double sample = double(v >> shift) + s1 * f0 + s2 * f1;  // arithmetic shift like Python ints
            s2 = s1;
            s1 = sample;
            // Python: int(round(v)) with round-half-even, then clamp.
            double r = std::nearbyint(sample);
            if (r > 32767.0) r = 32767.0;
            if (r < -32768.0) r = -32768.0;
            out[n++] = int16_t(r);
        }
    }
    history[0] = s1;
    history[1] = s2;
}

void PssDemuxer::setError(const std::string& message) {
    if (error_.empty()) error_ = message;
    platform::log("pss_demux: " + message);
}

bool PssDemuxer::open(const std::filesystem::path& path, std::string* error) {
    close();
    std::error_code ec;
    fileSize_ = std::filesystem::file_size(path, ec);
    if (ec) {
        if (error) *error = "cannot stat " + path.string();
        return false;
    }
    file_.open(path, std::ios::binary);
    if (!file_) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    buffer_.reserve(kChunkSize * 2);
    vagHistory_.clear();
    return true;
}

void PssDemuxer::close() {
    if (file_.is_open()) file_.close();
    buffer_.clear();
    pos_ = 0;
    bufferFileOffset_ = 0;
    fileSize_ = 0;
    eof_ = false;
    ended_ = false;
    audioHeader_ = PssAudioHeader{};
    audioHeaderBytes_.clear();
    audioPending_.clear();
    audioBodyConsumed_ = 0;
    audioBroken_ = false;
    vagHistory_.clear();
    stats_ = PssStats{};
    error_.clear();
}

bool PssDemuxer::ensure(size_t count) {
    while (available() < count && !eof_) {
        // Drop consumed bytes, then append one more chunk.
        if (pos_ > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(pos_));
            bufferFileOffset_ += pos_;
            pos_ = 0;
        }
        const size_t old = buffer_.size();
        buffer_.resize(old + kChunkSize);
        file_.read(reinterpret_cast<char*>(buffer_.data() + old), static_cast<std::streamsize>(kChunkSize));
        const size_t got = static_cast<size_t>(file_.gcount());
        buffer_.resize(old + got);
        if (got < kChunkSize) eof_ = true;
    }
    return available() >= count;
}

PssDemuxer::Event PssDemuxer::next(VideoPacket& video, AudioBlock& audio) {
    video.data.clear();
    video.pts = video.dts = kNoPts;
    audio.stereo.clear();
    if (ended_ || !file_.is_open()) return Event::End;

    // Python: while i < n - 4 (at least 5 bytes left).
    while (ensure(5)) {
        const uint8_t* p = cur();
        if (p[0] != 0 || p[1] != 0 || p[2] != 1) {
            ++pos_;
            continue;
        }
        const uint8_t sid = p[3];
        if (sid == 0xBA) {
            if ((p[4] & 0xC0) == 0x40) {
                if (!ensure(14)) break;
                pos_ += 14 + (cur()[13] & 7);
            } else {
                pos_ += 12;
            }
            continue;
        }
        if (sid == 0xB9) {
            ended_ = true;
            break;
        }
        if (!ensure(6)) break;
        const size_t plen = (size_t(cur()[4]) << 8) | cur()[5];
        stats_.packetCounts[sid]++;
        const size_t packetEnd = 6 + plen;
        // Python slices clamp at EOF; do the same with whatever is available.
        const size_t have = ensure(packetEnd) ? packetEnd : available();
        p = cur();
        if (sid == 0xE0 || sid == 0xBD) {
            const size_t hdrlen = have > 8 ? p[8] : 0;
            const size_t payloadStart = 9 + hdrlen;
            int64_t pts = kNoPts;
            int64_t dts = kNoPts;
            if (have >= 9) {
                const uint8_t flags = p[7];
                if ((flags & 0x80) && have >= 14) pts = readTimestamp(p + 9);
                if ((flags & 0xC0) == 0xC0 && have >= 19) dts = readTimestamp(p + 14);
            }
            const uint8_t* payload = p + (payloadStart < have ? payloadStart : have);
            const size_t payloadSize = payloadStart < have ? have - payloadStart : 0;
            if (sid == 0xE0) {
                stats_.videoBytes += payloadSize;
                if (pts != kNoPts) {
                    if (stats_.firstVideoPts == kNoPts) stats_.firstVideoPts = pts;
                    stats_.lastVideoPts = pts;
                }
                video.data.assign(payload, payload + payloadSize);
                video.pts = pts;
                video.dts = dts;
                pos_ += have;
                return Event::Video;
            }
            // Private stream 1: FF A0 00 00 prefix then the sce stream bytes.
            if (payloadSize < 2 || payload[0] != 0xFF || payload[1] != 0xA0) {
                stats_.badAudioPrefix++;
                setError("0xBD payload without FF A0 prefix");
            } else {
                if (pts != kNoPts && stats_.firstAudioPts == kNoPts) stats_.firstAudioPts = pts;
                const size_t skip = payloadSize < 4 ? payloadSize : 4;
                stats_.audioBytes += payloadSize - skip;
                const bool produced = pushAudioPayload(payload + skip, payloadSize - skip, audio);
                pos_ += have;
                if (produced) return Event::Audio;
                continue;
            }
        }
        pos_ += have;
    }
    ended_ = true;
    return Event::End;
}

bool PssDemuxer::parseAudioHeader() {
    // Need 'SShd' u32 size, `size` header bytes, then 'SSbd' u32 bodySize.
    if (audioHeaderBytes_.size() < 8) return false;
    if (std::memcmp(audioHeaderBytes_.data(), "SShd", 4) != 0) {
        setError("audio stream does not start with SShd");
        audioBroken_ = true;
        return false;
    }
    const uint32_t hdrSize = readLe32(audioHeaderBytes_.data() + 4);
    const size_t bodyOff = 8 + size_t(hdrSize);
    if (hdrSize < 16 || hdrSize > 0x1000) {
        setError("unreasonable SShd size");
        audioBroken_ = true;
        return false;
    }
    if (audioHeaderBytes_.size() < bodyOff + 8) return false;
    PssAudioHeader h;
    h.headerSize = hdrSize;
    h.format = readLe32(audioHeaderBytes_.data() + 8);
    h.rate = readLe32(audioHeaderBytes_.data() + 12);
    h.channels = readLe32(audioHeaderBytes_.data() + 16);
    h.interleave = readLe32(audioHeaderBytes_.data() + 20);
    if ((h.format != kFormatPcm16 && h.format != kFormatAdpcm) || (h.channels != 1 && h.channels != 2) ||
        h.interleave == 0 || h.interleave > (1u << 20) || h.rate == 0) {
        setError("unsupported SShd format/channels/interleave");
        audioBroken_ = true;
        return false;
    }
    if (std::memcmp(audioHeaderBytes_.data() + bodyOff, "SSbd", 4) != 0) {
        setError("SSbd marker missing after SShd");
        audioBroken_ = true;
        return false;
    }
    h.bodySize = readLe32(audioHeaderBytes_.data() + bodyOff + 4);
    h.valid = true;
    audioHeader_ = h;
    vagHistory_.assign(h.channels, {0.0, 0.0});
    // Whatever follows the SSbd header is body.
    audioPending_.assign(audioHeaderBytes_.begin() + static_cast<std::ptrdiff_t>(bodyOff + 8), audioHeaderBytes_.end());
    audioHeaderBytes_.clear();
    audioHeaderBytes_.shrink_to_fit();
    return true;
}

bool PssDemuxer::pushAudioPayload(const uint8_t* data, size_t size, AudioBlock& out) {
    if (audioBroken_) return false;
    if (!audioHeader_.valid) {
        audioHeaderBytes_.insert(audioHeaderBytes_.end(), data, data + size);
        if (!parseAudioHeader()) return false;
        // The pending bytes were taken from the header buffer; cap them like the body path below.
        stats_.audioBodyBytes += audioPending_.size();
        if (audioPending_.size() > audioHeader_.bodySize) audioPending_.resize(audioHeader_.bodySize);
        audioBodyConsumed_ = audioPending_.size();
    } else {
        stats_.audioBodyBytes += size;
        const uint64_t room = audioHeader_.bodySize - audioBodyConsumed_;
        const size_t take = size < room ? size : static_cast<size_t>(room);
        audioPending_.insert(audioPending_.end(), data, data + take);
        audioBodyConsumed_ += take;
    }
    emitAudioGroups(out);
    return !out.stereo.empty();
}

void PssDemuxer::emitAudioGroups(AudioBlock& out) {
    const size_t il = audioHeader_.interleave;
    const size_t ch = audioHeader_.channels;
    const size_t group = il * ch;
    size_t pos = 0;
    while (pos + group <= audioPending_.size()) {
        const uint8_t* g = audioPending_.data() + pos;
        if (audioHeader_.format == kFormatPcm16) {
            const size_t samples = il / 2;
            out.stereo.reserve(out.stereo.size() + samples * 2);
            for (size_t k = 0; k < samples; ++k) {
                for (size_t c = 0; c < 2; ++c) {
                    const uint8_t* s = g + (c < ch ? c : 0) * il + k * 2;  // mono duplicated to both channels
                    out.stereo.push_back(int16_t(uint16_t(s[0]) | (uint16_t(s[1]) << 8)));
                }
            }
            stats_.audioFrames += samples;
        } else {
            // Python: for o in range(0, len(blk) - 15, 16) -> only whole 16-byte blocks of the channel block.
            const size_t blocks = il / 16;
            const size_t samples = blocks * 28;
            std::vector<int16_t> chan[2];
            for (size_t c = 0; c < ch; ++c) {
                chan[c].resize(samples);
                for (size_t b = 0; b < blocks; ++b)
                    decodeVagBlock(g + c * il + b * 16, vagHistory_[c].data(), chan[c].data() + b * 28);
            }
            out.stereo.reserve(out.stereo.size() + samples * 2);
            for (size_t k = 0; k < samples; ++k) {
                out.stereo.push_back(chan[0][k]);
                out.stereo.push_back(chan[ch > 1 ? 1 : 0][k]);
            }
            stats_.audioFrames += samples;
        }
        pos += group;
    }
    if (pos > 0) audioPending_.erase(audioPending_.begin(), audioPending_.begin() + static_cast<std::ptrdiff_t>(pos));
}

}  // namespace baroque::video
