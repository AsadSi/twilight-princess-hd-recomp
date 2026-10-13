// Test aid: WWHD_SFX_RECORD=<hex id>[,<hex id>...] plays each sound effect id in turn through Link's sound object
// (Z2CreatureLink::startCollisionSE 02C146F8 on mZ2Link, Link +0x2CDC, as the horror mod's heartbeat), one every 3 s
// once a file is in play (WWHD_SFX_START steps after, default 150), with the music faded out; every effect is stopped
// after 2.8 s (Z2SeMgr::seStopAll 02C1EFC4: some loop, and Link's handles keep only some). With WWHD_NO_AUDIO=1
// WWHD_AUDIO_DUMP=<wav> it logs "[sfxrec] <id> at <frame>", each effect's first frame in the dump, so they can be cut into WAV files of their own (the Wind Waker HD recomp's
// tools/tp_sounds.py does, for its Twilight Princess monsters). An enemy's waves are only loaded in the stages it
// lives in: WWHD_SFX_WAVES=<hex wave>[,...] loads more SE wave archives first (Z2SceneMgr::loadSeWave 02C19BE0; the
// enemy test stage's are 8A and 8B).
#include "sfx_record.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "../audio_out.h"
#include "runtime.h"

namespace mods::sfx_record {
namespace {
constexpr uint32_t kPlayer = 0x1014B5B0, kSeqMgr = 0x1012AFF4, kSceneMgr = 0x1012AFF8, kSeMgr = 0x1012AFF0;
constexpr int kEvery = 90, kCut = 84;  // steps apart; cut at

std::vector<uint32_t> hex_list(const char* var) {
    std::vector<uint32_t> out;
    if (const char* e = getenv(var))
        for (std::string s = e; !s.empty();) {
            const size_t comma = s.find(',');
            out.push_back((uint32_t)strtoul(s.substr(0, comma).c_str(), nullptr, 16));
            s = comma == std::string::npos ? "" : s.substr(comma + 1);
        }
    return out;
}

}  // namespace

void step(Cpu* c) {
    static const std::vector<uint32_t> list = hex_list("WWHD_SFX_RECORD");
    static int steps = 0;
    const uint32_t link = ld32(kPlayer);
    if (list.empty() || !link) return;
    if (const uint32_t mgr = ld32(kSeqMgr); mgr && steps == 0) {  // the music down to silence (the horror mod's fader)
        c->f[1].ps0 = 0;
        guest_call(c, 0x02C15818, {mgr + 0x74, 15});
        for (uint32_t w : hex_list("WWHD_SFX_WAVES"))
            LOG("[sfxrec] wave %X load %u", w, guest_call(c, 0x02C19BE0, {ld32(kSceneMgr), w}));
    }
    static const int start = getenv("WWHD_SFX_START") ? atoi(getenv("WWHD_SFX_START")) : 150;  // steps after a file is in play
    const int k = steps - start;
    steps++;
    if (k < 0 || k >= (int)list.size() * kEvery) return;
    const size_t i = k / kEvery;
    if (k % kEvery == 0) {
        guest_call(c, 0x02C146F8, {link + 0x2CDC, list[i], 0});
        LOG("[sfxrec] %X at %u", list[i], audio::dump_frames());
    } else if (k % kEvery == kCut) guest_call(c, 0x02C1EFC4, {ld32(kSeMgr), 3});
    if (k == (int)list.size() * kEvery - 1) LOG("[sfxrec] done at %u", audio::dump_frames());
}

}  // namespace mods::sfx_record
