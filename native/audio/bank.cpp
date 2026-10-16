#include "audio/bank.hpp"
#include <algorithm>
#include <cstring>
#include <map>

namespace baroque::audio {

namespace {
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

struct Chunk { size_t offset; size_t size; };

bool table(const std::vector<uint8_t>& hd, const Chunk& c, std::vector<uint32_t>& offs) {
    // u32 max_index at chunk+12, then (max+1) u32 offsets relative to the chunk.
    if (c.offset + 16 > hd.size()) return false;
    const uint32_t n = le32(hd.data() + c.offset + 12) + 1;
    if (c.offset + 16 + size_t(n) * 4 > hd.size()) return false;
    offs.resize(n);
    for (uint32_t i = 0; i < n; ++i) offs[i] = le32(hd.data() + c.offset + 16 + size_t(i) * 4);
    return true;
}
} // namespace

bool HdBank::parse(const std::vector<uint8_t>& hd, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    std::map<std::string, Chunk> chunks;
    size_t o = 0;
    while (o + 12 <= hd.size() && std::memcmp(hd.data() + o, "IECS", 4) == 0) {
        std::string typ(reinterpret_cast<const char*>(hd.data() + o + 4), 4);
        std::reverse(typ.begin(), typ.end());
        const uint32_t size = le32(hd.data() + o + 8);
        if (size < 12) return fail("bad chunk size");
        chunks[typ] = {o, size};
        o += size;
    }
    for (const char* need : {"Vers", "Head", "Vagi", "Smpl", "Sset", "Prog"})
        if (!chunks.count(need)) return fail("missing IECS chunk");

    const Chunk head = chunks["Head"];
    if (head.offset + 64 > hd.size()) return fail("short Head");
    uint32_t h[16];
    for (int i = 0; i < 16; ++i) h[i] = le32(hd.data() + head.offset + size_t(i) * 4);
    fileSize_ = h[3];
    bodySize_ = h[4];
    if (h[5] != chunks["Prog"].offset || h[6] != chunks["Sset"].offset || h[7] != chunks["Smpl"].offset ||
        h[8] != chunks["Vagi"].offset)
        return fail("Head chunk offsets disagree with chunk walk");

    std::vector<uint32_t> offs;
    // Vagi (8 bytes): u32 bdOffset, u16 rate, u8 attr, u8 0xff
    {
        const Chunk c = chunks["Vagi"];
        if (!table(hd, c, offs)) return fail("Vagi table");
        vagi_.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            const uint8_t* p = hd.data() + c.offset + offs[i];
            if (p + 8 > hd.data() + hd.size()) return fail("Vagi record out of range");
            vagi_[i].bdOffset = le32(p);
            vagi_[i].rate = le16(p + 4);
            vagi_[i].attr = p[6];
            const uint32_t next = i + 1 < offs.size() ? le32(hd.data() + c.offset + offs[i + 1]) : bodySize_;
            vagi_[i].size = next - vagi_[i].bdOffset;
        }
    }
    // Smpl (42 bytes)
    {
        const Chunk c = chunks["Smpl"];
        if (!table(hd, c, offs)) return fail("Smpl table");
        samples_.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            // offset 0xffffffff = deleted slot (the retail English bank, SLES-55216 / USA dub, has sample 247 deleted);
            // vagIndex 0xffff makes resolve() treat it as no sample, as for a deleted program
            if (offs[i] == 0xffffffffu) { samples_[i] = HdSample{}; samples_[i].vagIndex = 0xffff; continue; }
            const uint8_t* b = hd.data() + c.offset + offs[i];
            if (b + 42 > hd.data() + hd.size()) return fail("Smpl record out of range");
            HdSample& s = samples_[i];
            s.vagIndex = le16(b);
            s.velRangeLow = b[2]; s.velCrossFade = b[3]; s.velRangeHigh = b[4];
            s.velFollowPitch = b[5]; s.velFollowPitchCenter = b[6]; s.velFollowPitchCurve = b[7];
            s.velFollowAmp = b[8]; s.velFollowAmpCenter = b[9]; s.velFollowAmpCurve = b[10];
            s.baseNote = b[11]; s.detune = b[12]; s.panpot = b[13]; s.group = b[14]; s.priority = b[15];
            s.volume = b[16];
            s.adsr1 = le16(b + 18); s.adsr2 = le16(b + 20);
            s.keyFollowAr = b[22]; s.kfArCenter = b[23]; s.keyFollowDr = b[24]; s.kfDrCenter = b[25];
            s.keyFollowSr = b[26]; s.kfSrCenter = b[27]; s.keyFollowRr = b[28]; s.kfRrCenter = b[29];
            s.keyFollowSl = b[30]; s.kfSlCenter = b[31];
            s.pitchLfoDelay = le16(b + 32); s.pitchLfoFade = le16(b + 34);
            s.ampLfoDelay = le16(b + 36); s.ampLfoFade = le16(b + 38);
            s.lfoAttr = b[40]; s.spuAttr = b[41];
        }
    }
    // Sset: u8 velCurve, velLimitLow, velLimitHigh, nSample; u16 sampleIndex[n]
    {
        const Chunk c = chunks["Sset"];
        if (!table(hd, c, offs)) return fail("Sset table");
        sets_.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xffffffffu) { sets_[i] = HdSampleSet{}; continue; }   // deleted slot: no samples
            const uint8_t* b = hd.data() + c.offset + offs[i];
            if (b + 4 > hd.data() + hd.size()) return fail("Sset record out of range");
            HdSampleSet& ss = sets_[i];
            ss.velCurve = b[0]; ss.velLimitLow = b[1]; ss.velLimitHigh = b[2];
            const int n = b[3];
            if (b + 4 + n * 2 > hd.data() + hd.size()) return fail("Sset indices out of range");
            ss.sampleIndices.resize(size_t(n));
            for (int k = 0; k < n; ++k) ss.sampleIndices[size_t(k)] = le16(b + 4 + k * 2);
        }
    }
    // Prog: 0x24-byte header + nSplit * splitBlockSize
    {
        const Chunk c = chunks["Prog"];
        if (!table(hd, c, offs)) return fail("Prog table");
        programs_.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xffffffffu) { programs_[i].reset(); continue; }
            const uint8_t* b = hd.data() + c.offset + offs[i];
            if (b + 0x24 > hd.data() + hd.size()) return fail("Prog header out of range");
            HdProgram p;
            p.splitBlockAddr = le32(b);
            p.nSplits = b[4]; p.splitBlockSize = b[5]; p.volume = b[6]; p.panpot = b[7];
            p.transpose = b[8]; p.detune = b[9]; p.keyFollowPan = b[10]; p.kfPanCenter = b[11]; p.attr = b[12];
            p.splits.resize(p.nSplits);
            for (int s = 0; s < p.nSplits; ++s) {
                const uint8_t* sb = b + 0x24 + s * p.splitBlockSize;
                if (sb + 20 > hd.data() + hd.size()) return fail("split block out of range");
                HdSplit& sp = p.splits[size_t(s)];
                sp.sampleSet = le16(sb);
                sp.rangeLow = sb[2]; sp.crossfade = sb[3]; sp.rangeHigh = sb[4]; sp.splitNumber = sb[5];
                sp.bendRangeLow = sb[6]; sp.bendRangeHigh = sb[7];
                sp.keyFollowPitch = le16(sb + 8);
                sp.keyFollowAmp = sb[10]; sp.kfAmpCenter = sb[11]; sp.keyFollowPan = sb[12]; sp.kfPanCenter = sb[13];
                sp.keyFollowLfo = sb[14]; sp.kfLfoCenter = sb[15];
                sp.volume = sb[16]; sp.panpot = sb[17]; sp.transpose = sb[18]; sp.detune = sb[19];
            }
            programs_[i] = std::move(p);
        }
    }
    return true;
}

std::optional<ResolvedNote> HdBank::resolve(int program, int note) const {
    if (program < 0 || size_t(program) >= programs_.size() || !programs_[size_t(program)]) return std::nullopt;
    const HdProgram& p = *programs_[size_t(program)];
    for (const HdSplit& sp : p.splits) {
        if (sp.rangeLow <= note && note <= sp.rangeHigh) {
            if (sp.sampleSet >= sets_.size()) return std::nullopt;
            const HdSampleSet& ss = sets_[sp.sampleSet];
            if (ss.sampleIndices.empty() || ss.sampleIndices[0] >= samples_.size()) return std::nullopt;
            const HdSample& smp = samples_[ss.sampleIndices[0]];
            if (smp.vagIndex >= vagi_.size()) return std::nullopt;
            const HdVagInfo& vg = vagi_[smp.vagIndex];
            ResolvedNote r;
            r.program = program; r.note = note;
            r.sampleSet = sp.sampleSet; r.sample = ss.sampleIndices[0]; r.vag = smp.vagIndex;
            r.bdOffset = vg.bdOffset; r.bdSize = vg.size; r.rate = vg.rate;
            r.baseNote = smp.baseNote; r.adsr1 = smp.adsr1; r.adsr2 = smp.adsr2;
            r.volume = smp.volume; r.panpot = smp.panpot;
            r.splitVolume = sp.volume; r.splitPanpot = sp.panpot;
            r.progVolume = p.volume; r.progPanpot = p.panpot;
            r.priority = smp.priority;
            r.sampleDetune = smp.detune; r.splitTranspose = sp.transpose; r.splitDetune = sp.detune;
            r.progTranspose = p.transpose; r.progDetune = p.detune;
            return r;
        }
    }
    return std::nullopt;
}

const uint8_t* HdBank::vagData(int vag, uint32_t* size) const {
    if (vag < 0 || size_t(vag) >= vagi_.size()) return nullptr;
    const HdVagInfo& v = vagi_[size_t(vag)];
    if (size_t(v.bdOffset) + v.size > bd_.size()) return nullptr;
    if (size) *size = v.size;
    return bd_.data() + v.bdOffset;
}

} // namespace baroque::audio
