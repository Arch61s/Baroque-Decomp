#pragma once
// sce HD (IECS) bank tables + BD body: programs -> splits -> sample sets -> samples -> VAGs.
// Layout per work/reports/SOUND.md §2 and tools/sound_formats.py parse_hd / resolve_note.
// Field names marked [I] in the report follow the sce HD spec; offsets of baseNote(11), panpot(13),
// volume(16), adsr(18/20) were confirmed from the data pattern.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace baroque::audio {

struct HdVagInfo {
    uint32_t bdOffset = 0; // byte offset into the BD body
    uint32_t size = 0;     // next offset - this (last: Head.bodySize - offset)
    uint16_t rate = 0;     // sample rate (6000/11025/12000/16000/24000 in this bank)
    uint8_t attr = 0;
};

struct HdSample {          // Smpl record, 42 bytes
    uint16_t vagIndex = 0;        // +0
    uint8_t velRangeLow = 0;      // +2
    uint8_t velCrossFade = 0;     // +3
    uint8_t velRangeHigh = 0;     // +4
    uint8_t velFollowPitch = 0;   // +5
    uint8_t velFollowPitchCenter = 0; // +6
    uint8_t velFollowPitchCurve = 0;  // +7
    uint8_t velFollowAmp = 0;     // +8
    uint8_t velFollowAmpCenter = 0;   // +9
    uint8_t velFollowAmpCurve = 0;    // +10
    uint8_t baseNote = 0;         // +11
    uint8_t detune = 0;           // +12
    uint8_t panpot = 0;           // +13
    uint8_t group = 0;            // +14
    uint8_t priority = 0;         // +15
    uint8_t volume = 0;           // +16
    uint16_t adsr1 = 0;           // +18 SPU2 ADSR1
    uint16_t adsr2 = 0;           // +20 SPU2 ADSR2
    uint8_t keyFollowAr = 0, kfArCenter = 0, keyFollowDr = 0, kfDrCenter = 0; // +22..25
    uint8_t keyFollowSr = 0, kfSrCenter = 0, keyFollowRr = 0, kfRrCenter = 0; // +26..29
    uint8_t keyFollowSl = 0, kfSlCenter = 0;                                   // +30..31
    uint16_t pitchLfoDelay = 0, pitchLfoFade = 0, ampLfoDelay = 0, ampLfoFade = 0; // +32..39
    uint8_t lfoAttr = 0;          // +40
    uint8_t spuAttr = 0;          // +41
};

struct HdSampleSet {       // Sset record
    uint8_t velCurve = 0;         // +0
    uint8_t velLimitLow = 0;      // +1
    uint8_t velLimitHigh = 0;     // +2
    std::vector<uint16_t> sampleIndices; // +4.. (count at +3)
};

struct HdSplit {           // 20-byte split block
    uint16_t sampleSet = 0;       // +0
    uint8_t rangeLow = 0;         // +2
    uint8_t crossfade = 0;        // +3
    uint8_t rangeHigh = 0;        // +4
    uint8_t splitNumber = 0;      // +5
    uint8_t bendRangeLow = 0;     // +6
    uint8_t bendRangeHigh = 0;    // +7
    uint16_t keyFollowPitch = 0;  // +8
    uint8_t keyFollowAmp = 0, kfAmpCenter = 0, keyFollowPan = 0, kfPanCenter = 0; // +10..13
    uint8_t keyFollowLfo = 0, kfLfoCenter = 0;                                     // +14..15
    uint8_t volume = 0;           // +16
    uint8_t panpot = 0;           // +17
    uint8_t transpose = 0;        // +18
    uint8_t detune = 0;           // +19
};

struct HdProgram {         // 0x24-byte header + splits
    uint32_t splitBlockAddr = 0;  // +0 (0x24)
    uint8_t nSplits = 0;          // +4
    uint8_t splitBlockSize = 0;   // +5 (20)
    uint8_t volume = 0;           // +6
    uint8_t panpot = 0;           // +7
    uint8_t transpose = 0;        // +8
    uint8_t detune = 0;           // +9
    uint8_t keyFollowPan = 0;     // +10
    uint8_t kfPanCenter = 0;      // +11
    uint8_t attr = 0;             // +12
    std::vector<HdSplit> splits;
};

// Resolved (program, note) chain: first split whose range contains the note -> its sample set ->
// first sample -> VAG. Every split in this bank is a single note == sample baseNote.
struct ResolvedNote {
    int program = 0, note = 0;
    int sampleSet = 0, sample = 0, vag = 0;
    uint32_t bdOffset = 0, bdSize = 0;
    uint16_t rate = 0;
    uint8_t baseNote = 0;
    uint16_t adsr1 = 0, adsr2 = 0;
    uint8_t volume = 0, panpot = 0;           // sample
    uint8_t splitVolume = 0, splitPanpot = 0; // split
    uint8_t progVolume = 0, progPanpot = 0;   // program
    uint8_t priority = 0;
    uint8_t sampleDetune = 0, splitTranspose = 0, splitDetune = 0, progTranspose = 0, progDetune = 0;
};

class HdBank {
public:
    // Parses an IECS HD image (entry 1). Asserts chunk magics like the Python tool.
    bool parse(const std::vector<uint8_t>& hd, std::string* error = nullptr);
    // Attaches the BD body (entry 0 bytes; may be longer than bodySize - zero padded).
    void setBody(std::vector<uint8_t> bd) { bd_ = std::move(bd); }

    uint32_t fileSize() const { return fileSize_; }
    uint32_t bodySize() const { return bodySize_; }
    const std::vector<HdVagInfo>& vagi() const { return vagi_; }
    const std::vector<HdSample>& samples() const { return samples_; }
    const std::vector<HdSampleSet>& sampleSets() const { return sets_; }
    const std::vector<std::optional<HdProgram>>& programs() const { return programs_; }
    const std::vector<uint8_t>& body() const { return bd_; }

    std::optional<ResolvedNote> resolve(int program, int note) const;
    // Pointer/size of a BD VAG's ADPCM data (null if no body attached or out of range).
    const uint8_t* vagData(int vag, uint32_t* size) const;

private:
    uint32_t fileSize_ = 0, bodySize_ = 0;
    std::vector<HdVagInfo> vagi_;
    std::vector<HdSample> samples_;
    std::vector<HdSampleSet> sets_;
    std::vector<std::optional<HdProgram>> programs_;
    std::vector<uint8_t> bd_;
};

} // namespace baroque::audio
