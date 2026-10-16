#pragma once
// baroque::video::PssDemuxer - streaming demuxer for the MOVIE_EN/*.PSS cutscenes (MPEG-2 program stream).
//
// Mirrors tools/pss_movie.py exactly, but reads the file in chunks instead of loading it whole:
//   - 00 00 01 BA pack headers are skipped (MPEG-2 pack: 14 + (byte13 & 7) bytes, MPEG-1 pack: 12 bytes);
//   - 00 00 01 B9 (program end) stops the scan;
//   - every other 00 00 01 xx is a PES packet: u16 BE length at +4, payload after the MPEG-2 PES header
//     (header data length at +8, payload starts at +9+hdrlen). Only 0xE0 (video) and 0xBD (private 1) are used;
//     0xBB (system header) and 0xBE (padding) are counted and skipped like the Python tool does.
//   - 0xBD payloads start with FF A0 00 00; the remaining bytes concatenate into one sce stream:
//     'SShd' u32 size | u32 format (0x01 = 16-bit LE PCM, 0x10 = Sony ADPCM) | u32 rate | u32 channels |
//     u32 interleave | ... (size bytes in total) | 'SSbd' u32 bodySize | body interleaved per channel block of
//     `interleave` bytes. Blocks are de-interleaved into stereo 16-bit frames; a trailing partial channel group is
//     dropped and bytes beyond bodySize are ignored (same as decode_audio() in the Python tool).
//
// The video elementary stream is handed out per PES packet together with the packet's 90 kHz PTS/DTS when present
// (the Python tool ignores timestamps; they drive A/V sync in MoviePlayer).
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace baroque::video {

constexpr int64_t kNoPts = -1;

struct PssAudioHeader {
    uint32_t headerSize = 0;  // 'SShd' size field (24 on every disc movie)
    uint32_t format = 0;      // 0x01 = PCM16LE, 0x10 = Sony ADPCM
    uint32_t rate = 0;        // Hz (48000 on disc)
    uint32_t channels = 0;    // 1 or 2 (2 on disc)
    uint32_t interleave = 0;  // bytes per channel block (0x200 on disc)
    uint32_t bodySize = 0;    // 'SSbd' size field
    bool valid = false;       // both headers parsed and accepted
};

struct PssStats {
    std::array<uint32_t, 256> packetCounts{};  // per stream id, like the Python `counts`
    uint64_t videoBytes = 0;                   // concatenated 0xE0 payload bytes (== OPENING.m2v size)
    uint64_t audioBytes = 0;                   // 0xBD payload bytes after the 4-byte FF A0 00 00 prefix
    uint64_t audioBodyBytes = 0;               // body bytes seen (may exceed bodySize; the excess is ignored)
    uint64_t audioFrames = 0;                  // stereo frames emitted
    uint32_t badAudioPrefix = 0;               // 0xBD packets whose payload did not start with FF A0
    int64_t firstVideoPts = kNoPts;
    int64_t firstAudioPts = kNoPts;
    int64_t lastVideoPts = kNoPts;
};

class PssDemuxer {
public:
    enum class Event { Video, Audio, End };

    struct VideoPacket {
        std::vector<uint8_t> data;  // MPEG-2 video ES bytes of one PES packet
        int64_t pts = kNoPts;       // 90 kHz, kNoPts when the PES header carries none
        int64_t dts = kNoPts;
    };
    struct AudioBlock {
        std::vector<int16_t> stereo;  // interleaved L,R; frames = stereo.size() / 2
    };

    PssDemuxer() = default;
    PssDemuxer(const PssDemuxer&) = delete;
    PssDemuxer& operator=(const PssDemuxer&) = delete;

    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    bool isOpen() const { return file_.is_open(); }
    void close();

    // Returns the next video packet or audio block; End once the whole file (or a program-end code) is consumed.
    Event next(VideoPacket& video, AudioBlock& audio);

    const PssAudioHeader& audioHeader() const { return audioHeader_; }
    const PssStats& stats() const { return stats_; }
    const std::string& error() const { return error_; }  // non-fatal format complaints (first one)
    uint64_t fileSize() const { return fileSize_; }
    uint64_t bytesRead() const { return bufferFileOffset_ + pos_; }

    // Shared by the ADPCM path with tools/sound_archive.py decode_vag_block(): decodes one 16-byte block, float
    // history, 28 samples. Exposed for tests.
    static void decodeVagBlock(const uint8_t block[16], double history[2], int16_t out[28]);

private:
    bool ensure(size_t count);  // make >= count bytes available at pos_ (false near EOF)
    size_t available() const { return buffer_.size() - pos_; }
    const uint8_t* cur() const { return buffer_.data() + pos_; }
    bool pushAudioPayload(const uint8_t* data, size_t size, AudioBlock& out);
    bool parseAudioHeader();
    void emitAudioGroups(AudioBlock& out);
    void setError(const std::string& message);

    std::ifstream file_;
    std::vector<uint8_t> buffer_;
    size_t pos_ = 0;
    uint64_t bufferFileOffset_ = 0;
    uint64_t fileSize_ = 0;
    bool eof_ = false;
    bool ended_ = false;

    PssAudioHeader audioHeader_;
    std::vector<uint8_t> audioHeaderBytes_;  // bytes collected until both sce headers are parsed
    std::vector<uint8_t> audioPending_;      // body bytes waiting for a complete channel group
    uint64_t audioBodyConsumed_ = 0;         // body bytes accepted (capped at bodySize)
    bool audioBroken_ = false;
    std::vector<std::array<double, 2>> vagHistory_;

    PssStats stats_;
    std::string error_;
};

}  // namespace baroque::video
