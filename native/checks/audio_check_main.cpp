// Baroque-AudioCheck: headless regression check for native/audio (no audio device is opened).
//
//   Baroque-AudioCheck [--out <dir>] [--python <exe>] [--seconds <s>]
//
// 1. Tables: every byte of the embedded EE tables (audio/sound_tables.inc: SE descriptors 0x46bcf0, BGM/voice
//    descriptors 0x46dd80/0x46dda0, effect rows 0x46dc10, bank id list 0x46b990, option volume steps 0x491cc0) is
//    compared with work/disc/SLUS_217.14.
// 2. Archive: BRQSOUND.SFS entry count/kinds and the HD bank shape (SOUND.md §2).
// 3. Raw ADPCM, bit-exact against tools/sound_formats.py: STER entry 2 (BGM01, `ster 2`), VAGp entries 186
//    (voice 0, `vag 186`) and 29 (SE0001, `vag 29`), BD VAG 0 (`bdvag 0`). Missing references are generated with
//      <python> -I tools/sound_formats.py {ster 2|vag 186|vag 29|bdvag 0} <out>/audio_ref_*.wav
//    The IOP stream reader model (SterStreamReader) must hand out the same samples as the whole-entry decode.
// 4. Mixer: BGM 0 (title, BGM01.wav) for --seconds (20) and SE 0/1/2/6 and voice 0 played through the game-facing
//    API (game::sound() with GameSoundManager installed) -> SoundSystem -> IopSound -> SpuMixer, written to
//    <out>/audio_*.wav. The BGM output is compared sample-exactly with the SPU volume model applied to the raw
//    decode (stream pitch 0x1000 = no resampling). Id mappings (BGM id+2, SE id+1, voice id+0xba) are checked on the
//    manager slots.
// Prints PASS/FAIL; exit code 0 when every check passed.
#include "audio/adpcm.hpp"
#include "audio/sound_system.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque;
using namespace baroque::audio;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// ------------------------------------------------------------------------------------------------ WAV I/O
bool writeWav(const fs::path& path, int channels, int rate, const std::vector<int16_t>& pcm) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(uint16_t(channels)); u32(uint32_t(rate));
    u32(uint32_t(rate * channels * 2)); u16(uint16_t(channels * 2)); u16(16);
    f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
    return bool(f);
}

struct Wav { int channels = 0; int rate = 0; std::vector<int16_t> pcm; };

bool readWav(const fs::path& path, Wav& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    auto le32 = [&](size_t o) { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24; };
    size_t o = 12;
    bool haveFmt = false;
    while (o + 8 <= d.size()) {
        const uint32_t size = le32(o + 4);
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && o + 24 <= d.size()) {
            out.channels = d[o + 10] | d[o + 11] << 8;
            out.rate = int(le32(o + 12));
            haveFmt = true;
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            const size_t n = std::min<size_t>(size, d.size() - o - 8) / 2;
            out.pcm.resize(n);
            std::memcpy(out.pcm.data(), d.data() + o + 8, n * 2);
            return haveFmt;
        }
        o += 8 + size + (size & 1);
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ Python refs
fs::path g_projectRoot = BAROQUE_PROJECT_ROOT;
std::string g_python = "python";

bool ensureReference(const fs::path& ref, const std::string& args) {
    if (fs::exists(ref)) return true;
    const fs::path tool = g_projectRoot / "tools" / "sound_formats.py";
    std::string cmd = "\"" + g_python + "\" -I \"" + tool.string() + "\" " + args + " \"" + ref.string() + "\"";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe strips one outer pair of quotes
#endif
    std::printf("  generating reference: %s\n", cmd.c_str());
    std::fflush(stdout);
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !fs::exists(ref)) {
        std::printf("  (reference generation failed, rc=%d; run: python -I tools/sound_formats.py %s <path>)\n", rc, args.c_str());
        return false;
    }
    return true;
}

size_t firstDiff(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return i;
    return a.size() == b.size() ? SIZE_MAX : n;
}

// ------------------------------------------------------------------------------------------------ 1. tables
void checkTables(const fs::path& elfPath) {
    std::printf("[1] EE tables vs %s\n", elfPath.string().c_str());
    std::ifstream f(elfPath, std::ios::binary);
    std::vector<uint8_t> elf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check(!elf.empty(), "ELF readable");
    if (elf.empty()) return;
    auto at = [&](uint32_t addr) -> const uint8_t* {
        const size_t o = size_t(addr) - 0x100000 + 0x80;
        return o < elf.size() ? elf.data() + o : nullptr;
    };
    auto rowBytes = [](const SeDescriptor& r, uint8_t out[20]) {
        auto p32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) out[o + i] = uint8_t(v >> (8 * i)); };
        auto p16 = [&](int o, uint16_t v) { out[o] = uint8_t(v); out[o + 1] = uint8_t(v >> 8); };
        p32(0, r.flags); p32(4, r.x); p16(8, r.field8); p16(10, r.priority); p16(12, r.field12);
        out[14] = r.defaultVolume; out[15] = r.mode; p32(16, r.user);
    };
    int bad = 0;
    for (int i = 0; i < kSeTableRows; ++i) {
        uint8_t b[20];
        rowBytes(kSeTable[i], b);
        if (std::memcmp(b, at(0x46bcf0 + uint32_t(i) * 0x14), 20) != 0) ++bad;
    }
    check(bad == 0, "SE descriptor table 0x46bcf0 (398 x 0x14 bytes) identical (" + std::to_string(bad) + " rows differ)");
    uint8_t b[20];
    rowBytes(kBgmDescriptor, b);
    check(std::memcmp(b, at(0x46dd80), 20) == 0, "BGM descriptor 0x46dd80 identical");
    rowBytes(kVoiceDescriptor, b);
    check(std::memcmp(b, at(0x46dda0), 20) == 0, "voice descriptor 0x46dda0 identical");
    bad = 0;
    for (int r = 0; r < kEffectRows; ++r) {
        const EffectRow& e = kEffectTable[r];
        uint8_t eb[16];
        std::memcpy(eb, &e.mode, 4);
        std::memcpy(eb + 4, &e.depthL, 2);
        std::memcpy(eb + 6, &e.depthR, 2);
        std::memcpy(eb + 8, &e.delay, 4);
        std::memcpy(eb + 12, &e.feedback, 4);
        if (std::memcmp(eb, at(0x46dc10 + uint32_t(r) * 0x20), 16) != 0) ++bad;
    }
    check(bad == 0, "effect table 0x46dc10 (6 rows) identical");
    check(std::memcmp(kStationSeIds, at(0x46b990), sizeof kStationSeIds) == 0, "bank id list 0x46b990 (397 x s16) identical");
    uint32_t cnt = 0;
    std::memcpy(&cnt, at(0x46bcc0 + 0x14 + 0x10), 4);
    check(cnt == uint32_t(kStationSeCount), "bank table row 1 count == 397");
    check(std::memcmp(kVolumeTableBits, at(0x491cc0), sizeof kVolumeTableBits) == 0,
          "option volume table 0x491cc0 (11 steps x 12 bytes) identical");
    std::printf("      volume steps BGM:");
    for (int s = 0; s <= 10; ++s) std::printf(" %.2f", double(volumeStep(s, 0)));
    std::printf("\n");
}

// ------------------------------------------------------------------------------------------------ helpers
struct Capture {
    std::vector<int16_t> pcm;   // stereo interleaved
    int ticks = 0;
    bool ended = false;
};

// Runs `sys.tick()` until `done()` (checked after each tick) or maxTicks; appends the audio.
template <class Done>
Capture run(SoundSystem& sys, int maxTicks, int tailTicks, Done done) {
    Capture c;
    int tail = -1;
    for (int t = 0; t < maxTicks; ++t) {
        sys.tick();
        c.pcm.insert(c.pcm.end(), sys.lastTickAudio().begin(), sys.lastTickAudio().end());
        ++c.ticks;
        if (tail < 0 && done()) { c.ended = true; tail = tailTicks; }
        if (tail >= 0 && tail-- == 0) break;
    }
    return c;
}

double rms(const std::vector<int16_t>& pcm) {
    if (pcm.empty()) return 0.0;
    double s = 0.0;
    for (int16_t v : pcm) s += double(v) * double(v);
    return std::sqrt(s / double(pcm.size()));
}
int peak(const std::vector<int16_t>& pcm) {
    int p = 0;
    for (int16_t v : pcm) p = std::max(p, std::abs(int(v)));
    return p;
}
int slotWithGameId(const SoundSystem& sys, uint32_t gameId) {
    for (int i = 0; i < SoundSystem::kSlots; ++i)
        if (sys.slot(i).handle != 0 && sys.slot(i).gameId == gameId) return i;
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    fs::path out = fs::path(BAROQUE_WORK_ROOT) / "native_captures";
    double seconds = 20.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--python" && i + 1 < argc) g_python = argv[++i];
        else if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
    }
    fs::create_directories(out);
    std::printf("Baroque-AudioCheck: archive %s\n", soundArchivePath().string().c_str());

    checkTables(fs::path(BAROQUE_WORK_ROOT) / "disc" / "SLUS_217.14");

    // ---------------------------------------------------------------------------------------- 2. archive
    std::printf("[2] archive / bank\n");
    SoundSystem sys;
    const bool inited = sys.init(false);
    check(inited, "SoundSystem::init (FUN_00114f30) opened BRQSOUND.SFS" + (inited ? std::string() : ": " + sys.lastError()));
    if (!inited) {
        std::printf("FAIL (%d)\n", g_failures);
        return 1;
    }
    const SfsArchive& sfs = sys.archive();
    check(sfs.entryCount() == 2120, "2120 entries (got " + std::to_string(sfs.entryCount()) + ")");
    int ster = 0, vag = 0;
    for (size_t i = 2; i < sfs.entryCount(); ++i) {
        const std::string k = sfs.kind(i);
        if (k == "STER") ++ster;
        else if (k == "VAGp") ++vag;
    }
    check(sfs.kind(0) == "BD" && sfs.kind(1) == "IECS" && ster == 27 && vag == 2091,
          "kinds: BD, IECS, 27 STER, 2091 VAGp (got " + std::to_string(ster) + " STER, " + std::to_string(vag) + " VAGp)");
    const HdBank& bank = sys.bank();
    check(bank.vagi().size() == 251 && bank.samples().size() == 251 && bank.programs().size() == 3 &&
              bank.bodySize() == 0x1d5ef0,
          "HD: 251 VAGs, 251 samples, 3 programs, body 0x1d5ef0");
    check(sys.seIdMap(1) == 1 && sys.seIdMap(397) == 397, "SE id map 0x53fb70 built by FUN_00211860 (id -> position+1)");

    // ---------------------------------------------------------------------------------------- 3. raw ADPCM
    std::printf("[3] raw ADPCM decode vs tools/sound_formats.py (bit-exact)\n");
    std::vector<int16_t> sterL, sterR;
    {
        const auto data = sfs.read(2);
        SterHeader h;
        const bool decoded = decodeSterEntry(data.data(), data.size(), h, sterL, sterR);
        check(decoded, "decodeSterEntry(entry 2 = " + h.name + ", " + std::to_string(h.sampleRate) + " Hz, loop " +
                           std::to_string(h.loopStartFileOffset) + ")");
        std::vector<int16_t> inter(sterL.size() * 2);
        for (size_t i = 0; i < sterL.size(); ++i) { inter[2 * i] = sterL[i]; inter[2 * i + 1] = sterR[i]; }
        const fs::path ref = out / "audio_ref_ster_002.wav";
        Wav w;
        if (ensureReference(ref, "ster 2") && readWav(ref, w)) {
            const size_t d = firstDiff(inter, w.pcm);
            check(w.channels == 2 && w.rate == int(h.sampleRate) && d == SIZE_MAX,
                  "STER entry 2: " + std::to_string(sterL.size()) + " frames, " + std::to_string(w.pcm.size() / 2) +
                      " in reference" + (d == SIZE_MAX ? ", identical" : ", first difference at sample " + std::to_string(d)));
        } else {
            check(false, "STER reference available");
        }
        // IOP reader model (func 0x5170) vs the whole-entry decode
        struct Src final : SterStreamReader::Source {
            const std::vector<uint8_t>* d = nullptr;
            size_t readEntry(uint64_t off, void* dst, size_t size) override {
                if (off >= d->size()) return 0;
                size = std::min<size_t>(size, d->size() - size_t(off));
                std::memcpy(dst, d->data() + off, size);
                return size;
            }
        } src;
        src.d = &data;
        SterStreamReader rd;
        rd.start(&src, h, uint32_t(data.size()), 0);
        int16_t l[28], r[28];
        size_t n = 0;
        bool same = true;
        while (n + 28 <= sterL.size() && rd.nextPair(l, r)) {
            if (std::memcmp(l, &sterL[n], sizeof l) != 0 || std::memcmp(r, &sterR[n], sizeof r) != 0) { same = false; break; }
            n += 28;
        }
        check(same && n == sterL.size(), "SterStreamReader (IOP func 0x5170 model) yields the same " + std::to_string(n) + " frames");
    }
    auto vagCheck = [&](size_t entry, const char* refName) {
        const auto data = sfs.read(entry);
        VagHeader h;
        std::vector<int16_t> pcm;
        const bool ok = decodeVagEntry(data.data(), data.size(), h, pcm);
        const fs::path ref = out / refName;
        Wav w;
        if (ok && ensureReference(ref, "vag " + std::to_string(entry)) && readWav(ref, w)) {
            const size_t d = firstDiff(pcm, w.pcm);
            check(w.channels == 1 && w.rate == int(h.sampleRate) && d == SIZE_MAX,
                  "VAGp entry " + std::to_string(entry) + " (" + h.name + ", " + std::to_string(h.sampleRate) + " Hz): " +
                      std::to_string(pcm.size()) + " samples" + (d == SIZE_MAX ? ", identical" : ", first difference at " + std::to_string(d)));
        } else {
            check(false, "VAGp entry " + std::to_string(entry) + " decoded and reference available");
        }
    };
    vagCheck(186, "audio_ref_vag_0186.wav");
    vagCheck(29, "audio_ref_vag_0029.wav");
    {
        uint32_t size = 0;
        const uint8_t* p = bank.vagData(0, &size);
        const std::vector<int16_t> pcm = p ? decodeAdpcmMono(p, size, true) : std::vector<int16_t>{};
        const fs::path ref = out / "audio_ref_bdvag_000.wav";
        Wav w;
        if (p && ensureReference(ref, "bdvag 0") && readWav(ref, w)) {
            const size_t d = firstDiff(pcm, w.pcm);
            check(w.rate == bank.vagi()[0].rate && d == SIZE_MAX,
                  "BD VAG 0 (SE row 2 = prog 0 note 13): " + std::to_string(pcm.size()) + " samples" +
                      (d == SIZE_MAX ? ", identical" : ", first difference at " + std::to_string(d)));
        } else {
            check(false, "BD VAG 0 decoded and reference available");
        }
    }

    // ---------------------------------------------------------------------------------------- 4. mixer
    std::printf("[4] mixer captures (SoundSystem -> IopSound -> SpuMixer, 48 kHz stereo)\n");
    {
        GameSoundManager mgr(sys);
        game::setSoundManager(&mgr);
        game::sound().init();                          // FUN_00114fd0
        check(game::sound().initialized(), "game::sound() is the audio implementation and enabled");
        game::sound().playBgm(0, 0, 1);                // title: FUN_00115d70(snd, 0, 0, 1)
        const int ticks = int(std::lround(seconds * 30.0));
        Capture c = run(sys, ticks, 0, [] { return false; });
        const int bgmSlot = slotWithGameId(sys, 2);
        check(bgmSlot == 9 && (sys.slot(9).handle & 0x3ffff) == 0x10002 && sys.bgmHandle() == sys.slot(9).handle,
              "BGM 0 -> SFS entry 2 (id+2), slot 9, handle low bits 0x10002 (stereo)");
        check(game::sound().currentBgm() == 0, "currentBgm() == 0 (+0x47c)");
        writeWav(out / "audio_bgm00_title.wav", 2, kSpuRate, c.pcm);
        // Expected output: stereo stream voices 18/19, VOLL/VOLR = 16204/162 and 162/16204 (vol 100: lin 0x3ff0,
        // curve[100] = 99), envelope at 0x7fff from frame 2 on (ADSR1 0x000f attack), master 0x3ff0.
        const size_t frames = c.pcm.size() / 2;
        size_t mism = 0, firstMism = SIZE_MAX;
        for (size_t k = 2; k < frames && k < sterL.size(); ++k) {
            const int32_t a = (int32_t(sterL[k]) * 0x7fff) >> 15;
            const int32_t b = (int32_t(sterR[k]) * 0x7fff) >> 15;
            const int32_t ml = ((a * 16204) >> 14) + ((b * 162) >> 14);
            const int32_t mr = ((a * 162) >> 14) + ((b * 16204) >> 14);
            const int32_t el = std::clamp((ml * 0x3ff0) >> 14, -32768, 32767);
            const int32_t er = std::clamp((mr * 0x3ff0) >> 14, -32768, 32767);
            if (c.pcm[2 * k] != el || c.pcm[2 * k + 1] != er) {
                if (firstMism == SIZE_MAX) firstMism = k;
                ++mism;
            }
        }
        char line[256];
        std::snprintf(line, sizeof line, "BGM 0 %.1f s: %zu frames, RMS %.0f, peak %d; SPU-model match %zu/%zu frames%s",
                      seconds, frames, rms(c.pcm), peak(c.pcm), frames - 2 - mism, frames - 2,
                      firstMism == SIZE_MAX ? "" : (" (first mismatch at " + std::to_string(firstMism) + ")").c_str());
        check(frames == size_t(ticks) * 1600 && mism == 0 && peak(c.pcm) > 1000, line);
        game::sound().stopBgm(8);
        Capture f = run(sys, 120, 0, [&] { return sys.bgmHandle() == 0; });
        check(f.ended, "stopBgm(8) fades out (IOP 2%/9 ticks) and frees slot 9 after " + std::to_string(f.ticks) + " frames");
        game::setSoundManager(nullptr);
    }

    {
        // Play-once BGM (loop == 0 -> id | 0x20000 -> slot flag 0x10000): the IOP loops at the end of the data and
        // sets status word 3; FUN_00212df0 then fades the stream out with time 0x10 and the slot is freed.
        SoundSystem s4;
        if (s4.init(false)) {
            s4.enable();
            s4.playBgm(2, 0, 0);                       // BGM03.wav, loop start 228656
            const auto head = s4.archive().read(4, 48);
            SterHeader h;
            parseSterHeader(head.data(), head.size(), h);
            const double trackSeconds = double(h.samplesPerChannel()) / double(h.sampleRate);
            int loopedAt = -1;
            Capture c = run(s4, 30 * 120, 0, [&] {
                if (loopedAt < 0 && (s4.status().word[3] & (1u << 24))) loopedAt = 1;
                return s4.bgmHandle() == 0;
            });
            const double freedAt = c.ticks / 30.0;
            char line[256];
            std::snprintf(line, sizeof line, "play-once BGM 2 (%s, %.2f s, loop sample %d): looped, faded (0x10) and freed at %.2f s",
                          h.name.c_str(), trackSeconds, h.loopStartSample(), freedAt);
            // 100% -> 0 in 2% steps every 0x10+1 IOP ticks = 50*17/240 = 3.54 s after the loop point
            check(loopedAt > 0 && c.ended && freedAt > trackSeconds + 3.0 && freedAt < trackSeconds + 4.0, line);
        } else {
            check(false, "init for play-once BGM");
        }
    }

    struct SeCase { int gameId; const char* file; };
    for (const SeCase sc : {SeCase{0, "audio_se000.wav"}, SeCase{1, "audio_se001.wav"}, SeCase{2, "audio_se002.wav"},
                            SeCase{6, "audio_se006.wav"}}) {
        SoundSystem s2;
        if (!s2.init(false)) { check(false, "init for SE"); continue; }
        GameSoundManager mgr(s2);
        game::setSoundManager(&mgr);
        game::sound().init();
        game::sound().playSe(1.0f, sc.gameId);        // FUN_00115390: row = id + 1
        const SeDescriptor& row = kSeTable[sc.gameId + 1];
        const int slotIdx = slotWithGameId(s2, uint32_t(sc.gameId + 1));
        const bool synth = (row.flags & 0x2000000) != 0;
        const bool slotOk = synth ? (slotIdx >= 1 && slotIdx <= 8) : (slotIdx >= 10 && slotIdx <= 13);
        Capture c = run(s2, 30 * 10, 6, [&] { return slotWithGameId(s2, uint32_t(sc.gameId + 1)) < 0; });
        writeWav(out / sc.file, 2, kSpuRate, c.pcm);
        char line[256];
        if (synth) {
            std::snprintf(line, sizeof line, "SE %d -> row %d (synth prog %u note %u, %u/10 s), slot %d; %.2f s rendered, RMS %.0f, peak %d, slot freed: %s",
                          sc.gameId, sc.gameId + 1, row.x & 0xff, row.x >> 8, unsigned(row.field12), slotIdx,
                          double(c.pcm.size() / 2) / kSpuRate, rms(c.pcm), peak(c.pcm), c.ended ? "yes" : "no");
        } else {
            std::snprintf(line, sizeof line, "SE %d -> row %d (streamed VAG, SFS entry %u), slot %d; %.2f s rendered, RMS %.0f, peak %d, slot freed: %s",
                          sc.gameId, sc.gameId + 1, row.x, slotIdx, double(c.pcm.size() / 2) / kSpuRate, rms(c.pcm),
                          peak(c.pcm), c.ended ? "yes" : "no");
        }
        check(slotOk && c.ended && peak(c.pcm) > 500, line);
        game::setSoundManager(nullptr);
    }

    {
        SoundSystem s3;
        if (s3.init(false)) {
            GameSoundManager mgr(s3);
            game::setSoundManager(&mgr);
            game::sound().init();
            game::sound().playVoice(1.0f, 0);         // FUN_00115ce0: entry = id + 0xba
            const int slotIdx = slotWithGameId(s3, 0xba);
            const uint32_t handle = slotIdx >= 0 ? s3.slot(slotIdx).handle : 0;
            Capture c = run(s3, 30 * 20, 6, [&] { return slotWithGameId(s3, 0xba) < 0; });
            writeWav(out / "audio_voice0000.wav", 2, kSpuRate, c.pcm);
            const auto data = s3.archive().read(0xba);
            VagHeader h;
            std::vector<int16_t> pcm;
            decodeVagEntry(data.data(), data.size(), h, pcm);
            // nonzero span of the render vs the clip length resampled to 48 kHz
            size_t firstNz = SIZE_MAX, lastNz = 0;
            for (size_t i = 0; i < c.pcm.size(); ++i)
                if (c.pcm[i] != 0) { firstNz = std::min(firstNz, i / 2); lastNz = i / 2; }
            size_t clipFirst = SIZE_MAX, clipLast = 0;
            for (size_t i = 0; i < pcm.size(); ++i)
                if (pcm[i] != 0) { clipFirst = std::min(clipFirst, i); clipLast = i; }
            const double expect = clipFirst == SIZE_MAX ? 0.0 : double(clipLast - clipFirst + 1) * 48000.0 / double(h.sampleRate);
            const double got = firstNz == SIZE_MAX ? 0.0 : double(lastNz - firstNz + 1);
            char line[256];
            std::snprintf(line, sizeof line, "voice 0 -> SFS entry 0xba (%s, %u Hz), slot %d, handle low 0x%x; %.3f s audible (clip's audible span %.3f s at 48 kHz), RMS %.0f, slot freed: %s",
                          h.name.c_str(), h.sampleRate, slotIdx, handle & 0xffff, got / kSpuRate, expect / kSpuRate,
                          rms(c.pcm), c.ended ? "yes" : "no");
            check(slotIdx >= 10 && slotIdx <= 13 && (handle & 0xffff) == 0xba && c.ended && std::fabs(got - expect) < 48000 * 0.01, line);
            game::setSoundManager(nullptr);
        } else {
            check(false, "init for voice");
        }
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
