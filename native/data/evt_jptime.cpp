// PORT (voice language): see evt_jptime.hpp. Checked during development against an independent Python implementation
// (byte-identical output for all 54 events); Baroque-DataCheck "EVT Japanese timing" runs it over every event and
// writes the merged files to %BAROQUE_EVT_JPTIME_DUMP% when that is set.
#include "data/evt_jptime.hpp"
#include "data/evt.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace baroque::data {
namespace {

struct Cmd {
    int32_t op = 0;
    std::vector<int32_t> args;
};
struct Cue {
    int32_t frame = 0;          // 'current id' (stream opcode 1) before the block
    size_t word = 0;            // word index of the 64-word block in the stream (file offset 0x808 + 4 * word)
    std::vector<Cmd> cmds;
};

bool isVoice(int32_t op) { return op == 132 || op == 133; }
// Cues that change game state or the music: they must stay exactly the English script's set.
bool isState(int32_t op) {
    switch (op) {
    case 190: case 191: case 52: case 53: case 180: case 193: case 192: case 43: case 45: case 46: case 128: case 129:
        return true;
    default:
        return false;
    }
}

// The stream walk of FUN_0016be20 (data/evt.cpp parseEvtOrThrow) keeping each cue block's position, and the cue
// command walk of FUN_00175e30 (decodeCueStream).
bool walk(Bytes d, std::vector<Cue>* out, std::string* why) {
    if (d.size() % 2048 != 0 || d.size() <= 0x808) { *why = "bad EVT size"; return false; }
    const size_t nWords = (d.size() - 0x808) / 4;
    auto word = [&](size_t i) { return s32le(d, 0x808 + 4 * i); };
    size_t pc = 0;
    int32_t cur = 0;
    for (;;) {
        if (pc >= nWords) { *why = "EVT stream has no terminator"; return false; }
        const int32_t op = word(pc);
        if (op == 201) break;
        if (op == 20) {
            if (pc + 65 > nWords) { *why = "short cue block"; return false; }
            Cue c;
            c.frame = cur;
            c.word = pc + 1;
            size_t q = 0;
            while (q < 64) {
                const int32_t o = word(c.word + q);
                if (o == 0) break;
                const EvtCueOpDesc* desc = evtCueOpDesc(o);
                if (desc == nullptr) { *why = "unknown cue opcode " + std::to_string(o); return false; }
                const size_t n = static_cast<size_t>(desc->operands);
                if (q + 1 + n > 64) { *why = "cue operands past the block"; return false; }
                Cmd cmd;
                cmd.op = o;
                for (size_t i = 0; i < n; ++i) cmd.args.push_back(word(c.word + q + 1 + i));
                c.cmds.push_back(std::move(cmd));
                q += 1 + n;
                if (o == 200) break;
            }
            out->push_back(std::move(c));
            pc += 65;
            continue;
        }
        const int n = evtStreamOperands(op);
        if (n < 0) { *why = "bad EVT opcode " + std::to_string(op); return false; }
        if (op == 1) {
            if (pc + 1 >= nWords) { *why = "EVT stream has no terminator"; return false; }
            cur = word(pc + 1);
        }
        pc += 1 + static_cast<size_t>(n);
    }
    return true;
}

bool sjisLead(uint8_t c) { return (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc); }

// Characters of text line `line` as FUN_0016f690 (op 27) selects it: the field after `line` ';' separators of the
// 0x800-byte block, up to the next ';' or NUL. At least 1 (an empty page still takes its turn).
int lineChars(Bytes d, int32_t line) {
    size_t p = 0;
    for (int32_t s = 0; s < line && p < 0x800; ++p)
        if (d[p] == ';') ++s;
    if (line < 0) return 1;
    int n = 0;
    while (p < 0x800 && d[p] != ';' && d[p] != 0) {
        p += sjisLead(d[p]) ? 2 : 1;
        ++n;
    }
    return std::max(1, n);
}

struct Page {
    size_t cue = 0, cmd = 0;        // op 27 position
    const Cmd* show = nullptr;
    bool hasSpeed = false;          // op 110 for the same slot right after the op 27
    int32_t speed = 0;
    int weight = 1;                 // characters of the line
};
using GroupKey = std::pair<int, int32_t>;   // (voice segment, text slot)

std::map<GroupKey, std::vector<Page>> pagesOf(Bytes d, const std::vector<Cue>& cues) {
    std::vector<int32_t> voiceFrames;
    for (const Cue& c : cues)
        for (const Cmd& m : c.cmds)
            if (isVoice(m.op)) voiceFrames.push_back(c.frame);
    std::map<GroupKey, std::vector<Page>> out;
    for (size_t ci = 0; ci < cues.size(); ++ci) {
        const Cue& c = cues[ci];
        // segment = voices fired at or before this frame (a page shown on the frame of its voice belongs to it)
        const int seg = static_cast<int>(std::count_if(voiceFrames.begin(), voiceFrames.end(),
                                                       [&](int32_t f) { return f <= c.frame; }));
        for (size_t k = 0; k < c.cmds.size(); ++k) {
            const Cmd& m = c.cmds[k];
            if (m.op != 27) continue;
            Page p;
            p.cue = ci;
            p.cmd = k;
            p.show = &m;
            if (k + 1 < c.cmds.size() && c.cmds[k + 1].op == 110 && c.cmds[k + 1].args[0] == m.args[1]) {
                p.hasSpeed = true;
                p.speed = c.cmds[k + 1].args[1];
            }
            p.weight = lineChars(d, m.args[0]);
            out[{seg, m.args[1]}].push_back(p);
        }
    }
    return out;
}

// English page j -> Japanese page index. Equal counts: in order. Fewer English pages: the first goes on the first
// Japanese page, each next one on the Japanese page whose start (cumulative characters / total) is nearest to the
// English page's start, keeping the order and leaving room for the pages after it (integer cross-multiplied
// distances; ties take the earlier page).
std::vector<size_t> assign(const std::vector<Page>& E, const std::vector<Page>& J) {
    const size_t n = E.size(), m = J.size();
    std::vector<size_t> res;
    if (n == m) {
        for (size_t i = 0; i < n; ++i) res.push_back(i);
        return res;
    }
    int64_t et = 0, jt = 0;
    for (const Page& p : E) et += p.weight;
    for (const Page& p : J) jt += p.weight;
    std::vector<int64_t> jcum(1, 0);
    for (const Page& p : J) jcum.push_back(jcum.back() + p.weight);
    res.push_back(0);
    size_t prev = 0;
    int64_t ecum = E[0].weight;
    for (size_t j = 1; j < n; ++j) {
        int64_t best = -1;
        size_t bk = prev + 1;
        for (size_t k = prev + 1; k <= m - (n - j); ++k) {
            const int64_t dist = std::llabs(jcum[k] * et - ecum * jt);
            if (best < 0 || dist < best) { best = dist; bk = k; }
        }
        res.push_back(bk);
        prev = bk;
        ecum += E[j].weight;
    }
    return res;
}

using StateKey = std::pair<int32_t, std::vector<int32_t>>;
std::map<StateKey, int> stateSet(const std::vector<std::vector<Cmd>>& cueCmds) {
    std::map<StateKey, int> s;
    for (const auto& cmds : cueCmds)
        for (const Cmd& m : cmds)
            if (isState(m.op)) ++s[{m.op, m.args}];
    return s;
}
std::vector<std::vector<Cmd>> cmdsOf(const std::vector<Cue>& cues) {
    std::vector<std::vector<Cmd>> out;
    for (const Cue& c : cues) out.push_back(c.cmds);
    return out;
}
int endCue(const std::vector<Cue>& cues) {
    for (size_t i = 0; i < cues.size(); ++i)
        for (const Cmd& m : cues[i].cmds)
            if (m.op == 200) return static_cast<int>(i);
    return -1;
}

}  // namespace

std::optional<std::vector<uint8_t>> buildJapaneseTimedEvt(Bytes en, Bytes jp, std::string* why,
                                                          EvtJpTimingStats* stats) {
    std::string dummy;
    if (why == nullptr) why = &dummy;
    EvtJpTimingStats st;
    std::vector<Cue> ce, cj;
    try {
        if (!walk(en, &ce, why) || !walk(jp, &cj, why)) return std::nullopt;
    } catch (const FormatError& e) {
        *why = e.what();
        return std::nullopt;
    }
    std::vector<int32_t> ve, vj;
    for (const Cue& c : ce) for (const Cmd& m : c.cmds) if (isVoice(m.op)) ve.push_back(m.args[0]);
    for (const Cue& c : cj) for (const Cmd& m : c.cmds) if (isVoice(m.op)) vj.push_back(m.args[0]);
    if (ve != vj) { *why = "voice cues differ"; return std::nullopt; }

    const auto pe = pagesOf(en, ce), pj = pagesOf(jp, cj);
    // (cue, cmd) of a Japanese op 27 -> its replacement; Japanese op 110s that went with a replaced op 27
    std::map<std::pair<size_t, size_t>, std::vector<Cmd>> edits;
    std::map<std::pair<size_t, size_t>, bool> drop110;
    std::vector<GroupKey> keys;
    for (const auto& kv : pe) keys.push_back(kv.first);
    for (const auto& kv : pj) if (!pe.count(kv.first)) keys.push_back(kv.first);
    for (const GroupKey& key : keys) {
        static const std::vector<Page> kNone;
        const auto ie = pe.find(key), ij = pj.find(key);
        const std::vector<Page>& E = ie == pe.end() ? kNone : ie->second;
        const std::vector<Page>& J = ij == pj.end() ? kNone : ij->second;
        if (E.size() > J.size()) {
            *why = "more English pages than Japanese in voice segment " + std::to_string(key.first);
            return std::nullopt;
        }
        if (E.empty() && !J.empty()) {
            *why = "Japanese-only text in voice segment " + std::to_string(key.first);
            return std::nullopt;
        }
        if (E.size() != J.size()) ++st.unevenGroups;
        const std::vector<size_t> a = assign(E, J);
        for (size_t ji = 0; ji < J.size(); ++ji) {
            const Page& p = J[ji];
            std::vector<Cmd> rep;
            const auto it = std::find(a.begin(), a.end(), ji);
            if (it != a.end()) {
                const Page& e = E[static_cast<size_t>(it - a.begin())];
                rep.push_back(*e.show);
                if (e.hasSpeed) rep.push_back(Cmd{110, {e.show->args[1], e.speed}});
                ++st.pages;
            } else {
                ++st.pagesDropped;
            }
            edits[{p.cue, p.cmd}] = std::move(rep);
            if (p.hasSpeed) drop110[{p.cue, p.cmd + 1}] = true;
        }
    }
    std::vector<std::vector<Cmd>> outCues;
    for (size_t ci = 0; ci < cj.size(); ++ci) {
        std::vector<Cmd> cmds;
        for (size_t k = 0; k < cj[ci].cmds.size(); ++k) {
            if (drop110.count({ci, k})) continue;
            const auto it = edits.find({ci, k});
            if (it != edits.end()) cmds.insert(cmds.end(), it->second.begin(), it->second.end());
            else cmds.push_back(cj[ci].cmds[k]);
        }
        outCues.push_back(std::move(cmds));
    }

    const auto se = stateSet(cmdsOf(ce)), sj = stateSet(cmdsOf(cj));
    std::map<StateKey, int> missing;
    for (const auto& [k, n] : sj) {
        const auto it = se.find(k);
        if (it == se.end() || it->second < n) { *why = "Japanese-only state cue " + std::to_string(k.first); return std::nullopt; }
    }
    for (const auto& [k, n] : se) {
        const auto it = sj.find(k);
        const int have = it == sj.end() ? 0 : it->second;
        if (n > have) missing[k] = n - have;
    }
    if (!missing.empty()) {
        const int ee = endCue(ce), je = endCue(cj);
        if (ee < 0 || je < 0) { *why = "no end cue"; return std::nullopt; }
        std::vector<Cmd> ins;
        std::map<StateKey, int> left = missing;
        for (const Cmd& m : ce[static_cast<size_t>(ee)].cmds) {
            if (!isState(m.op)) continue;
            const auto it = left.find({m.op, m.args});
            if (it == left.end() || it->second == 0) continue;
            --it->second;
            ins.push_back(m);
            ++st.stateMoved;
        }
        for (const auto& [k, n] : left)
            if (n != 0) { *why = "English-only state cue " + std::to_string(k.first) + " outside the end cue"; return std::nullopt; }
        auto& target = outCues[static_cast<size_t>(je)];
        target.insert(target.begin(), ins.begin(), ins.end());
    }
    if (stateSet(outCues) != se) { *why = "state cues still differ"; return std::nullopt; }

    std::vector<uint8_t> out(jp.begin(), jp.end());
    std::copy(en.begin(), en.begin() + 0x800, out.begin());
    for (size_t ci = 0; ci < cj.size(); ++ci) {
        std::vector<int32_t> words;
        for (const Cmd& m : outCues[ci]) {
            words.push_back(m.op);
            words.insert(words.end(), m.args.begin(), m.args.end());
        }
        if (words.size() > 64) { *why = "cue overflow at frame " + std::to_string(cj[ci].frame); return std::nullopt; }
        words.resize(64, 0);
        std::memcpy(out.data() + 0x808 + 4 * cj[ci].word, words.data(), 64 * 4);
    }
    if (stats) *stats = st;
    return out;
}

}  // namespace baroque::data
