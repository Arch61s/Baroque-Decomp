// Baroque-VideoCheck: headless regression check for native/video (PSS demux + MPEG-2 decode + MoviePlayer).
//
//   Baroque-VideoCheck [--movies <dir>] [--out <dir>] [--ref <dir>] [--quick] [--play-seconds <s>]
//                      [--match-window <frames>] [--no-probe]
//
// 1. Demuxes every *.PSS in the movie directory and verifies consistent sce audio headers.
// 2. Decodes OPENING.PSS at full speed, captures frames at 1 s / 20 s / 60 s to <out>/movie_opening_*.png,
//    compares them with <ref>/opening_frame_20s.png / opening_frame_60s.png (mean absolute RGB difference, searching
//    +-match-window frames because the references sit on the GOP keyframe before the mark), compares the demuxed PCM
//    with <ref>/OPENING_audio.wav byte for byte and reports decode speed vs real time.
// 3. Probes the audio device callback cadence, then runs MoviePlayer for a few wall-clock seconds in a hidden window
//    (audio device if available) and skips it.
// Exit code 0 when every check passed.
#include "platform/platform.hpp"
#include "video/movie_decoder.hpp"
#include "video/movie_player.hpp"
#include "video/pss_demux.hpp"

#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque::video;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_failures;
}

double seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// ---- 1. demux every movie ----------------------------------------------------------------------------------------

struct DemuxSummary {
    std::string name;
    PssAudioHeader header;
    PssStats stats;
    std::string error;
    double secs = 0.0;
};

bool demuxAll(const fs::path& dir, std::vector<DemuxSummary>& out) {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        if (e.is_regular_file() && ext == ".PSS") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::printf("demux: %zu PSS files in %s\n", files.size(), dir.string().c_str());
    bool allOk = !files.empty();
    for (const fs::path& f : files) {
        DemuxSummary s;
        s.name = f.filename().string();
        PssDemuxer d;
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        if (!d.open(f, &err)) {
            s.error = err;
            allOk = false;
        } else {
            PssDemuxer::VideoPacket v;
            PssDemuxer::AudioBlock a;
            while (d.next(v, a) != PssDemuxer::Event::End) {}
            s.header = d.audioHeader();
            s.stats = d.stats();
            s.error = d.error();
        }
        s.secs = seconds(t0, std::chrono::steady_clock::now());
        const PssStats& st = s.stats;
        const bool pcmFramesMatch = s.header.format == 0x01 && st.audioFrames == uint64_t(s.header.bodySize) / (2ull * s.header.channels);
        const bool ok = s.error.empty() && s.header.valid && st.badAudioPrefix == 0 && st.firstVideoPts != kNoPts &&
                        st.firstAudioPts != kNoPts && st.audioBodyBytes >= s.header.bodySize && pcmFramesMatch;
        std::printf("  %-12s E0=%6u BD=%6u BB=%u BE=%u video=%10llu audio=%9llu SShd{size %u fmt 0x%02x %u Hz %u ch il %u} "
                    "body %u (seen %llu) frames %llu (%.2f s) pts v0=%lld a0=%lld %s%s\n",
                    s.name.c_str(), st.packetCounts[0xE0], st.packetCounts[0xBD], st.packetCounts[0xBB], st.packetCounts[0xBE],
                    (unsigned long long)st.videoBytes, (unsigned long long)st.audioBytes, s.header.headerSize, s.header.format,
                    s.header.rate, s.header.channels, s.header.interleave, s.header.bodySize, (unsigned long long)st.audioBodyBytes,
                    (unsigned long long)st.audioFrames, s.header.rate ? double(st.audioFrames) / s.header.rate : 0.0,
                    (long long)st.firstVideoPts, (long long)st.firstAudioPts, ok ? "" : "<-- PROBLEM ", s.error.c_str());
        if (!ok) allOk = false;
        out.push_back(std::move(s));
    }
    return allOk;
}

// ---- 2. OPENING decode -------------------------------------------------------------------------------------------

bool savePng(const MovieFrame& f, const fs::path& path) {
    Image img{};
    img.data = const_cast<uint8_t*>(f.rgba.data());
    img.width = f.width;
    img.height = f.height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return ExportImage(img, path.string().c_str());
}

// Mean absolute RGB difference (0..255) against a reference PNG; -1 when the reference is missing/unreadable.
double compareWithReference(const MovieFrame& f, const fs::path& ref) {
    if (!fs::exists(ref)) return -1.0;
    Image img = LoadImage(ref.string().c_str());
    if (img.data == nullptr) return -1.0;
    if (img.width != f.width || img.height != f.height) {
        UnloadImage(img);
        return -2.0;
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const uint8_t* a = static_cast<const uint8_t*>(img.data);
    const uint8_t* b = f.rgba.data();
    double sum = 0.0;
    const size_t pixels = size_t(f.width) * size_t(f.height);
    for (size_t i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) sum += std::fabs(double(a[i * 4 + c]) - double(b[i * 4 + c]));
    }
    UnloadImage(img);
    return sum / double(pixels * 3);
}

struct WavData {
    uint32_t rate = 0;
    uint16_t channels = 0;
    std::vector<int16_t> pcm;
    bool ok = false;
};

WavData readWav(const fs::path& path) {
    WavData w;
    std::ifstream in(path, std::ios::binary);
    if (!in) return w;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) return w;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const uint32_t size = uint32_t(data[pos + 4]) | (uint32_t(data[pos + 5]) << 8) | (uint32_t(data[pos + 6]) << 16) |
                              (uint32_t(data[pos + 7]) << 24);
        const uint8_t* body = data.data() + pos + 8;
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && size >= 16) {
            w.channels = uint16_t(body[2] | (body[3] << 8));
            w.rate = uint32_t(body[4]) | (uint32_t(body[5]) << 8) | (uint32_t(body[6]) << 16) | (uint32_t(body[7]) << 24);
        } else if (std::memcmp(data.data() + pos, "data", 4) == 0) {
            const size_t avail = std::min<size_t>(size, data.size() - pos - 8);
            w.pcm.resize(avail / 2);
            std::memcpy(w.pcm.data(), body, avail / 2 * 2);
            w.ok = true;
        }
        pos += 8 + size + (size & 1);
    }
    return w;
}

// Frames searched on each side of a capture for the reference PNG (--match-window). The references were grabbed with
// ffmpeg -ss, which lands on the GOP keyframe before the mark: 6 frames early at 20 s, 12 frames early at 60 s.
int g_matchWindow = 16;

bool decodeOpening(const fs::path& movie, const fs::path& outDir, const fs::path& refDir, bool quick) {
    std::printf("decode: %s\n", movie.string().c_str());
    MovieDecoder dec;
    std::string err;
    if (!dec.open(movie, &err)) {
        std::printf("  open failed: %s\n", err.c_str());
        ++g_failures;
        return false;
    }
    const double captureAt[3] = {1.0, 20.0, 60.0};
    const char* captureName[3] = {"movie_opening_01s.png", "movie_opening_20s.png", "movie_opening_60s.png"};
    const char* refName[3] = {nullptr, "opening_frame_20s.png", "opening_frame_60s.png"};
    bool captured[3] = {false, false, false};
    int captureIndex[3] = {-1, -1, -1};
    bool compared[3] = {false, false, false};
    const int kWindow = g_matchWindow;  // the reference PNGs were grabbed with ffmpeg -ss on another time axis
    std::deque<MovieFrame> recent;      // last 2*kWindow+1 frames
    std::vector<int16_t> pcm;
    MovieFrame f;
    uint64_t frames = 0;
    double lastTime = 0.0;
    int lastIndex = -1;
    bool monotonic = true;
    double maxStep = 0.0;
    const double stopAt = quick ? 25.0 : 1e9;
    const auto t0 = std::chrono::steady_clock::now();
    while (dec.nextFrame(f)) {
        ++frames;
        if (f.index != lastIndex + 1) monotonic = false;
        if (frames > 1) maxStep = std::max(maxStep, std::fabs((f.time - lastTime) - dec.frameDuration()));
        lastIndex = f.index;
        lastTime = f.time;
        for (int i = 0; i < 3; ++i) {
            if (!captured[i] && f.time + 1e-9 >= captureAt[i]) {
                captured[i] = true;
                captureIndex[i] = f.index;
                const fs::path out = outDir / captureName[i];
                const bool saved = savePng(f, out);
                std::printf("  capture %4.1f s -> frame %d (t=%.4f) %s %s\n", captureAt[i], f.index, f.time, out.string().c_str(),
                            saved ? "" : "(write failed)");
                if (!saved) ++g_failures;
            }
        }
        recent.push_back(f);
        if (recent.size() > size_t(2 * kWindow + 1)) recent.pop_front();
        for (int i = 0; i < 3; ++i) {
            if (!captured[i] || compared[i] || !refName[i] || f.index < captureIndex[i] + kWindow) continue;
            compared[i] = true;
            double best = -1.0;
            int bestIndex = -1;
            double exact = -1.0;
            std::string profile;
            for (const MovieFrame& r : recent) {
                const double mad = compareWithReference(r, refDir / refName[i]);
                if (r.index == captureIndex[i]) exact = mad;
                if (mad >= 0.0 && (best < 0.0 || mad < best)) {
                    best = mad;
                    bestIndex = r.index;
                }
                char buf[48];
                std::snprintf(buf, sizeof(buf), " %+d:%.1f", r.index - captureIndex[i], mad);
                profile += buf;
            }
            if (best < 0.0) {
                std::printf("  reference %s unavailable (%s)\n", refName[i], exact == -2.0 ? "size mismatch" : "missing");
                continue;
            }
            std::printf("  vs %s: frame %d mean|dRGB| = %.2f; best match frame %d (%+d) = %.2f; profile%s\n", refName[i],
                        captureIndex[i], exact, bestIndex, bestIndex - captureIndex[i], best, profile.c_str());
            check(best < 4.0, "a frame within the match window of the capture matches the reference (mean |dRGB| < 4)");
        }
        std::vector<int16_t>& q = dec.audioQueue();
        if (!q.empty()) {
            pcm.insert(pcm.end(), q.begin(), q.end());
            q.clear();
        }
        if (f.time > stopAt) break;
    }
    const double elapsed = seconds(t0, std::chrono::steady_clock::now());
    const PssStats& st = dec.demuxer().stats();
    const PssAudioHeader& h = dec.demuxer().audioHeader();
    const double movieSeconds = double(frames) * dec.frameDuration();
    std::printf("  %llu frames %dx%d, frame duration %.6f s (%.3f fps), movie %.2f s, decoded in %.2f s = %.1f fps = %.2fx real time\n",
                (unsigned long long)frames, dec.width(), dec.height(), dec.frameDuration(), 1.0 / dec.frameDuration(), movieSeconds,
                elapsed, double(frames) / elapsed, movieSeconds / elapsed);
    const MovieDecoderStats& ds = dec.stats();
    std::printf("  decode errors %llu, frames without pts %llu, pts resyncs %llu, max pts deviation %.4f s, max cadence error %.6f s\n",
                (unsigned long long)ds.decodeErrors, (unsigned long long)ds.framesWithoutPts, (unsigned long long)ds.ptsResyncs,
                ds.maxPtsDeviation, maxStep);
    const double audioSeconds = h.rate ? double(pcm.size() / 2) / h.rate : 0.0;
    std::printf("  audio: %u Hz %u ch fmt 0x%02x, %zu stereo frames demuxed (%.3f s), clock origin pts %lld (video pts0 %lld, "
                "last video pts %lld = %.3f s), audio tail after last frame %.2f s\n",
                h.rate, h.channels, h.format, pcm.size() / 2, audioSeconds, (long long)dec.clockOriginPts(), (long long)st.firstVideoPts,
                (long long)st.lastVideoPts, double(st.lastVideoPts - st.firstVideoPts) / 90000.0, audioSeconds - movieSeconds);
    check(dec.width() == 512 && dec.height() == 448, "frame size is 512x448");
    check(std::fabs(dec.frameDuration() - 1001.0 / 30000.0) < 1e-9, "frame duration is exactly 1001/30000 s (29.97 fps)");
    check(monotonic && maxStep < 1e-9, "frame times advance by exactly one frame period");
    check(ds.decodeErrors == 0, "no decode errors");
    check(ds.ptsResyncs == 0 && ds.maxPtsDeviation < dec.frameDuration() * 0.5, "stream PTS agree with the 29.97 cadence");
    check(movieSeconds / elapsed > 1.0, "decode speed exceeds real time");
    if (!quick) {
        check(captured[0] && captured[1] && captured[2], "captured 1 s, 20 s, 60 s frames");
        // Audio against the Python reference WAV.
        const WavData wav = readWav(refDir / "OPENING_audio.wav");
        if (wav.ok) {
            const size_t ourFrames = pcm.size() / 2;
            const size_t refFrames = wav.pcm.size() / wav.channels;
            std::printf("  reference wav: %u Hz %u ch %zu frames; ours %zu frames\n", wav.rate, wav.channels, refFrames, ourFrames);
            check(wav.rate == h.rate && wav.channels == 2, "wav rate/channels match the stream header");
            check(ourFrames == refFrames, "audio frame count equals the reference wav");
            const size_t n = std::min(pcm.size(), wav.pcm.size());
            check(n > 0 && std::memcmp(pcm.data(), wav.pcm.data(), n * sizeof(int16_t)) == 0, "PCM samples are bit-identical to the reference wav");
            // OPENING's PCM runs ~5 s past the last picture (ffmpeg counts the same 2873 frames); video must not outlast audio.
            check(movieSeconds > 0.0 && movieSeconds <= audioSeconds + 1.0, "video does not outlast the audio track");
        } else {
            std::printf("  reference wav missing (%s), skipping PCM comparison\n", (refDir / "OPENING_audio.wav").string().c_str());
        }
        check(st.audioFrames == uint64_t(h.bodySize) / 4, "demuxed PCM frames == SSbd body size / 4");
    }
    return true;
}

// ---- 3a. audio device probe: how does raylib/miniaudio pull from a callback stream? ------------------------------

namespace probe {
constexpr int kMax = 512;
double times[kMax];
unsigned frames[kMax];
std::atomic<int> count{0};
double startTime = 0.0;
void callback(void* buffer, unsigned int n) {
    std::memset(buffer, 0, size_t(n) * 4);
    const int i = count.fetch_add(1);
    if (i < kMax) {
        times[i] = GetTime() - startTime;
        frames[i] = n;
    }
}
}  // namespace probe

void audioDeviceProbe() {
    if (!IsAudioDeviceReady()) return;
    AudioStream s = LoadAudioStream(48000, 16, 2);
    if (s.buffer == nullptr) return;
    SetAudioStreamCallback(s, probe::callback);
    probe::startTime = GetTime();
    PlayAudioStream(s);
    while (GetTime() - probe::startTime < 0.6) {}
    StopAudioStream(s);
    const int n = std::min(probe::count.load(), probe::kMax);
    unsigned long long total = 0;
    for (int i = 0; i < n; ++i) total += probe::frames[i];
    std::printf("audio probe: %d callbacks in 0.6 s, %llu frames (%.3f s of audio) pulled; first ones:", n, total, double(total) / 48000.0);
    for (int i = 0; i < std::min(n, 12); ++i) std::printf(" %.1fms/%u", probe::times[i] * 1000.0, probe::frames[i]);
    std::printf("\n");
    UnloadAudioStream(s);
}

// ---- 3. MoviePlayer in a hidden window ---------------------------------------------------------------------------

void playerSmokeTest(const fs::path& movie, double playSeconds) {
    std::printf("player: %s for %.1f s wall clock (audio device %s)\n", movie.filename().string().c_str(), playSeconds,
                IsAudioDeviceReady() ? "ready" : "unavailable -> wall clock");
    // Warm up the GL swap chain first: the process's first EndDrawing() can stall for hundreds of ms (driver init),
    // which would otherwise look like an A/V hiccup at frame 0 of the test.
    const double tWarm = GetTime();
    BeginDrawing();
    ClearBackground(BLACK);
    EndDrawing();
    std::printf("  first EndDrawing() of the process took %.1f ms\n", (GetTime() - tWarm) * 1000.0);
    MoviePlayer player;
    std::string err;
    if (!player.open(movie, &err)) {
        std::printf("  open failed: %s\n", err.c_str());
        ++g_failures;
        return;
    }
    player.setVolumePercent(50);
    const double start = GetTime();
    int updates = 0;
    int lastFrame = -1;
    bool frameOrderOk = true;
    double prevClock = 0.0;
    double prevDrawMs = 0.0;
    uint64_t prevDropped = 0;
    int dropReports = 0;
    while (!player.finished() && GetTime() - start < playSeconds) {
        const double tUpdate = GetTime();
        player.update();
        const double tDone = GetTime();
        const double clockNow = player.clock();
        if (player.stats().framesDropped != prevDropped && dropReports < 8) {
            ++dropReports;
            std::printf("  drop at update %d: clock %.4f -> %.4f (jump %.1f ms), update took %.1f ms, previous draw took %.1f ms, frame %d\n",
                        updates, prevClock, clockNow, (clockNow - prevClock) * 1000.0, (tDone - tUpdate) * 1000.0, prevDrawMs,
                        player.currentFrameIndex());
        }
        prevDropped = player.stats().framesDropped;
        prevClock = clockNow;
        if (player.currentFrameIndex() < lastFrame) frameOrderOk = false;
        lastFrame = player.currentFrameIndex();
        const double tDraw = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        player.draw(GetScreenWidth(), GetScreenHeight());
        EndDrawing();
        prevDrawMs = (GetTime() - tDraw) * 1000.0;
        ++updates;
    }
    const double clock = player.clock();
    const MoviePlayerStats& ps = player.stats();
    std::printf("  %d updates, clock %.3f s, frame %d (t=%.3f), shown %llu dropped %llu, audio pushed %llu played %llu underruns %llu, "
                "audio callbacks %llu (max %u frames = %.1f ms)\n",
                updates, clock, player.currentFrameIndex(), player.currentFrameTime(), (unsigned long long)ps.framesShown,
                (unsigned long long)ps.framesDropped, (unsigned long long)ps.audioFramesPushed, (unsigned long long)ps.audioFramesPlayed,
                (unsigned long long)ps.audioUnderruns, (unsigned long long)ps.audioCallbacks, ps.maxCallbackFrames,
                1000.0 * double(ps.maxCallbackFrames) / 48000.0);
    check(player.hasFrame() && player.texture().width == 512 && player.texture().height == 448, "player texture is 512x448");
    check(clock > playSeconds * 0.5, "movie clock advanced");
    check(frameOrderOk && std::fabs(player.currentFrameTime() - clock) <= player.decoder().frameDuration() * 1.5, "shown frame tracks the clock within 1.5 frames");
    check(ps.audioUnderruns == 0, "no audio underruns");
    check(MoviePlayer::originalVolumeWord(100) == 0x3f0001u && MoviePlayer::originalVolumeWord(0) == 1u, "original volume word formula");
    const Rectangle r = MoviePlayer::destinationRect(1920, 1080);
    check(std::fabs(r.width - 1440.0f) < 0.5f && std::fabs(r.height - 1080.0f) < 0.5f && std::fabs(r.x - 240.0f) < 0.5f, "4:3 letterbox at 1920x1080 is 1440x1080 at x=240");
    player.skip();
    check(player.finished(), "skip() finishes the movie");
}

// --dump-frames FILE EVERY: save every EVERY-th frame of FILE as <out>/dump_<stem>_<frame>.png (reference captures)
int dumpFrames(const fs::path& movie, const fs::path& outDir, int every) {
    MovieDecoder dec;
    std::string err;
    if (!dec.open(movie, &err)) {
        std::printf("open failed: %s\n", err.c_str());
        return 1;
    }
    MovieFrame f;
    int index = 0, saved = 0;
    while (dec.nextFrame(f)) {
        if (index % every == 0) {
            char name[128];
            std::snprintf(name, sizeof name, "dump_%s_%05d.png", movie.stem().string().c_str(), index);
            if (savePng(f, outDir / name)) ++saved;
        }
        ++index;
    }
    std::printf("dumped %d of %d frames\n", saved, index);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path work = BAROQUE_WORK_ROOT;
    fs::path movieDir = work / "disc" / "MOVIE_EN";
    fs::path outDir = work / "native_captures";
    fs::path refDir = work / "movies";
    bool quick = false;
    bool probeDevice = true;
    double playSeconds = 3.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--movies" && i + 1 < argc) movieDir = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--ref" && i + 1 < argc) refDir = argv[++i];
        else if (a == "--quick") quick = true;
        else if (a == "--no-probe") probeDevice = false;
        else if (a == "--play-seconds" && i + 1 < argc) playSeconds = std::atof(argv[++i]);
        else if (a == "--dump-frames" && i + 2 < argc) {
            const fs::path movie = movieDir / argv[i + 1];
            const int every = std::max(1, std::atoi(argv[i + 2]));
            std::error_code dirEc;
            fs::create_directories(outDir, dirEc);
            return dumpFrames(movie, outDir, every);
        }
        else if (a == "--match-window" && i + 1 < argc) g_matchWindow = std::max(0, std::atoi(argv[++i]));
        else {
            std::printf("usage: Baroque-VideoCheck [--movies dir] [--out dir] [--ref dir] [--quick] [--play-seconds s] [--match-window n]\n");
            return 2;
        }
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (!fs::exists(movieDir)) {
        std::printf("movie directory missing: %s\n", movieDir.string().c_str());
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 448, "Baroque video check");
    InitAudioDevice();

    std::vector<DemuxSummary> summaries;
    const auto t0 = std::chrono::steady_clock::now();
    const bool demuxOk = demuxAll(movieDir, summaries);
    std::printf("  demuxed %zu files in %.1f s\n", summaries.size(), seconds(t0, std::chrono::steady_clock::now()));
    check(demuxOk, "every PSS demuxes with a valid, consistent sce audio header");
    bool consistent = !summaries.empty();
    for (const DemuxSummary& s : summaries) {
        const PssAudioHeader& h = s.header;
        if (!(h.valid && h.headerSize == summaries[0].header.headerSize && h.format == summaries[0].header.format &&
              h.rate == summaries[0].header.rate && h.channels == summaries[0].header.channels &&
              h.interleave == summaries[0].header.interleave))
            consistent = false;
    }
    check(consistent, "all movies share one audio header layout (format/rate/channels/interleave)");
    // Movie index table 0x46dec0 (SCENES.md §0.9) minus the duplicates (OPENING/PLAYABLE twice) and the two files that are
    // not on the disc (EV19A/EV19B): 25 distinct PSS files.
    static const char* const kDiscMovies[] = {"E01.PSS", "E03.PSS", "E04.PSS", "E05.PSS", "E06.PSS", "E08.PSS", "E09.PSS",
                                              "E10.PSS", "E11.PSS", "E12.PSS", "E13.PSS", "E14.PSS", "E15.PSS", "E51.PSS",
                                              "E52.PSS", "E53.PSS", "E62.PSS", "E71.PSS", "E72.PSS", "E82.PSS", "E83.PSS",
                                              "EV15B.PSS", "EV18.PSS", "OPENING.PSS", "PLAYABLE.PSS"};
    bool allPresent = true;
    for (const char* name : kDiscMovies) {
        bool found = false;
        for (const DemuxSummary& s : summaries) found = found || s.name == name;
        if (!found) {
            std::printf("  missing movie %s\n", name);
            allPresent = false;
        }
    }
    check(allPresent && summaries.size() == 25, "all 25 movies of the index table that exist on the disc are present");

    decodeOpening(movieDir / "OPENING.PSS", outDir, refDir, quick);
    if (probeDevice) audioDeviceProbe();
    playerSmokeTest(movieDir / "OPENING.PSS", playSeconds);

    CloseAudioDevice();
    CloseWindow();
    std::printf("video check: %s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
