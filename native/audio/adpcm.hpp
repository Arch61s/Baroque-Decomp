#pragma once
// Sony SPU ADPCM ("VAG") decoding, bit-exact with tools/sound_formats.py (decode_block /
// decode_mono / decode_ster / decode_vag_entry), plus the STER stream reader model of IOPSND.IRX
// (func 0x5170: header parse, random start, loop seek).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace baroque::audio {

// ---------------------------------------------------------------- block decoder
struct AdpcmState {
    int32_t s1 = 0; // previous sample
    int32_t s2 = 0; // sample before that
};

// Block flag byte (byte 1 of each 16-byte block), SPU semantics.
enum : uint8_t {
    kAdpcmFlagEnd = 1,        // last block: key off / jump to loop address
    kAdpcmFlagRepeat = 2,     // ordinary block ("loop region")
    kAdpcmFlagLoopEnd = 3,    // end of loop region: jump to LSAX
    kAdpcmFlagLoopStart = 6,  // set LSAX here
    kAdpcmFlagEndMute = 7,    // terminating silent block
};

// Decodes one 16-byte block into 28 samples. Returns the block flag byte.
// Predictor table (x64): (0,0) (60,0) (115,-52) (98,-55) (122,-60); predictor >= 5 -> (0,0).
// s = (nibble<<12 signed) >> shift + ((s1*f0 + s2*f1 + 32) >> 6), clamped to int16.
uint8_t decodeAdpcmBlock(const uint8_t block[16], AdpcmState& st, int16_t out[28]);

// decode_mono: decodes blocks until a flag-7 block (not output) or, when stopAtEnd, after a block
// whose flag has bit 0 set (flag 1 or 3; that block is output).
std::vector<int16_t> decodeAdpcmMono(const uint8_t* body, size_t len, bool stopAtEnd = true);

// ---------------------------------------------------------------- STER (BGM stream)
struct SterHeader {
    uint32_t perChannelBytes = 0;   // +0x04 LE (also +0x0c BE)
    int32_t loopStartFileOffset = -1; // +0x08 LE, relative to the entry start, header included; -1 = none
    uint32_t sampleRate = 0;        // +0x10 BE
    std::string name;               // +0x20 char[16]
    uint32_t samplesPerChannel() const { return perChannelBytes / 16 * 28; }
    // (loop - 0x30) / 32 * 28, per-channel sample index of the loop point.
    int32_t loopStartSample() const { return loopStartFileOffset < 0 ? -1 : (loopStartFileOffset - 0x30) / 32 * 28; }
};
constexpr size_t kSterHeaderSize = 0x30;
// Parses the 48-byte header; asserts magic and the LE/BE size pair like the Python tool.
bool parseSterHeader(const uint8_t* head, size_t len, SterHeader& out);

// decode_ster: whole-entry decode. L/R interleaved per 16-byte block starting at +0x30; stops when
// an L block carries flag 7 (that pair is dropped). Used by the check against work/sound/ster_001.wav.
bool decodeSterEntry(const uint8_t* data, size_t len, SterHeader& hdr,
                     std::vector<int16_t>& left, std::vector<int16_t>& right);

// ---------------------------------------------------------------- VAGp (SE / voice clips, BD samples)
struct VagHeader {
    uint32_t version = 0;   // +4 BE
    uint32_t dataSize = 0;  // +12 BE
    uint32_t sampleRate = 0; // +16 BE
    std::string name;       // +32 char[16]
};
constexpr size_t kVagHeaderSize = 48;
bool parseVagHeader(const uint8_t* data, size_t len, VagHeader& out);
// decode_vag_entry: decodeAdpcmMono(data + 48, dataSize).
bool decodeVagEntry(const uint8_t* data, size_t len, VagHeader& hdr, std::vector<int16_t>& pcm);

// ---------------------------------------------------------------- stream reader model (IOPSND 0x5170)
// Byte-level model of the IOP stream reader for a STER entry. The IOP streams 16-byte blocks to the
// SPU; this model hands out decoded L/R block pairs in the order the SPU would play them.
//
// Random start (L_05294..L_052d0): when the stream was started with volume exactly 1 (which the EE
// sends for a fade-in start), the reader reads the 48-byte header, then skips
//   skip = (entrySizeBytes / 16) * ((readCounter & 7) + 1)
// bytes from the entry start before streaming (readCounter = global count of reader invocations so
// far, incremented before the computation). The data pointer then sits at entry+skip, i.e. at data
// offset skip-0x30 which is 16 mod 32: the first block handed to the de-interleaver is an R block.
// UNVERIFIED: whether func_56e4 (header read) advances the file position; if it does, the data
// starts at entry+skip+0x30 instead (set kLiteralRandomStartOffset = false to get that variant).
//
// Loop (L_053a0..L_053f0, L_054a8..L_05508): if loop != -1, when the remaining byte count reaches 0
// the reader seeks to the loop file offset and sets the "looped" status bit. If loop == -1 the
// stream ends; the voice is keyed off once the SPU passes the end.
class SterStreamReader {
public:
    static constexpr bool kLiteralRandomStartOffset = true;

    // entry = the whole STER entry bytes are NOT required; the caller supplies a reader callback.
    // `entrySize` = entry size in bytes (sector multiple), `header` = parsed header.
    struct Source {
        virtual ~Source() = default;
        // Reads `size` bytes at `offset` (relative to the entry start) into dst; returns bytes read.
        virtual size_t readEntry(uint64_t offset, void* dst, size_t size) = 0;
    };

    // randomStartK = (readCounter & 7) + 1 (1..8), or 0 for a normal start.
    void start(Source* src, const SterHeader& header, uint32_t entrySize, int randomStartK);
    bool active() const { return src_ != nullptr; }
    bool ended() const { return ended_; }
    bool looped() const { return looped_; }
    const SterHeader& header() const { return hdr_; }

    // Decodes the next L/R block pair (28 frames). Returns false when the stream has ended
    // (no loop and data exhausted). Flags of the two blocks are returned for diagnostics.
    bool nextPair(int16_t left[28], int16_t right[28], uint8_t* flagL = nullptr, uint8_t* flagR = nullptr);

    // Position bookkeeping (bytes relative to the entry start).
    uint64_t filePos() const { return filePos_; }
    int64_t remaining() const { return remaining_; }

private:
    bool fill();
    Source* src_ = nullptr;
    SterHeader hdr_;
    uint32_t entrySize_ = 0;
    uint64_t filePos_ = 0;      // next byte to read, relative to entry start (IOP slot +0xc)
    int64_t remaining_ = 0;     // bytes left before loop/end (IOP slot +0x10)
    int64_t loopFileOffset_ = -1; // absolute loop target (IOP slot +0x2c), -1 = no loop
    bool ended_ = false;
    bool looped_ = false;
    AdpcmState stL_, stR_;
    std::vector<uint8_t> buf_;  // read chunk
    size_t bufPos_ = 0, bufLen_ = 0;
    static constexpr size_t kChunk = 0x2000; // IOP RAM chunk; only affects I/O granularity here
};

} // namespace baroque::audio
