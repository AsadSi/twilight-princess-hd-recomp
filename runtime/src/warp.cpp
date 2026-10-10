// Warps. Test aid: WWHD_WARP=<frame>:<stage>,<point>,<room>,<layer> sends the game to that place at TV frame <frame>,
// and more warps can follow, separated by ';' (a later frame each; tools/warplist.py lists the developer map select's places from the disc). It calls the game's own
// dComIfGp_setNextStage (with its default speed, mode and wipe), as a door or a cutscene does; the play
// scene then changes stage. Story flags stay as they are, so a place may come up in another state than
// the map select's description says.
// The Warp menu mod (go) does the same on the next frame. Its places are the developer map select's, read from the
// disc's res/Menu/Menu1.dat: menu_stage_class groups (label[65], count at +0x41, entries at +0x44), each entry a
// menu_data_class (description[65], stage[8], room s8, point u8, layer s8). The groups named after a stage of the
// game are kept, with English names; the descriptions are in Japanese and left out.
#include "warp.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>

#include "gfx/renderer.h"
#include "runtime.h"

namespace warp {
namespace {
const std::map<std::string, std::string> kNames = {
    {"F_SP00", "Ordon Ranch"}, {"F_SP103", "Ordon Village"}, {"R_SP01", "Ordon Village, indoors"},
    {"F_SP104", "Ordon Woods and Spring"}, {"R_SP107", "Hyrule Castle, inside"}, {"F_SP108", "Faron Woods"},
    {"R_SP108", "Faron Woods, indoors"}, {"F_SP117", "Sacred Grove"}, {"F_SP109", "Kakariko Village"},
    {"R_SP109", "Kakariko Village, indoors"}, {"R_SP209", "Kakariko Village, under the sanctuary"},
    {"F_SP110", "Death Mountain"}, {"R_SP110", "Death Mountain, sumo hall"}, {"F_SP111", "Kakariko Graveyard"},
    {"F_SP128", "Hidden Village"}, {"R_SP128", "Hidden Village, indoors"}, {"F_SP115", "Lake Hylia"},
    {"F_SP112", "Zora's River"}, {"F_SP126", "Upper Zora's River"}, {"F_SP127", "Fishing Pond"},
    {"R_SP127", "Fishing Pond, indoors"}, {"F_SP113", "Zora's Domain"}, {"F_SP116", "Castle Town"},
    {"R_SP116", "Castle Town, indoors"}, {"R_SP160", "Castle Town, shops"}, {"R_SP161", "Castle Town, STAR game"},
    {"F_SP114", "Snowpeak"}, {"F_SP124", "Gerudo Desert"}, {"F_SP121", "Hyrule Field"},
    {"F_SP122", "Hyrule Field (F_SP122)"}, {"F_SP123", "Hyrule Field (F_SP123)"}, {"F_SP102", "Hyrule Field (F_SP102)"},
    {"D_MN05", "Forest Temple"}, {"D_MN04", "Goron Mines"}, {"D_MN04A", "Goron Mines, boss"},
    {"D_MN01", "Lakebed Temple"}, {"D_MN01B", "Lakebed Temple, mini-boss"}, {"D_MN01A", "Lakebed Temple, boss"},
    {"D_MN10", "Arbiter's Grounds"}, {"D_MN10B", "Arbiter's Grounds, mini-boss"}, {"D_MN10A", "Arbiter's Grounds, boss"},
    {"D_MN11", "Snowpeak Ruins"}, {"D_MN11B", "Snowpeak Ruins, mini-boss"}, {"D_MN11A", "Snowpeak Ruins, boss"},
    {"D_MN06", "Temple of Time"}, {"D_MN07", "City in the Sky"}, {"D_MN07B", "City in the Sky, mini-boss"},
    {"D_MN07A", "City in the Sky, boss"}, {"D_MN08", "Palace of Twilight"}, {"D_MN09", "Hyrule Castle"},
    {"D_SB00", "Ice block cave"}, {"D_SB01", "Cave of Ordeals"}, {"D_SB02", "Lantern cave 1"},
    {"D_SB03", "Lantern cave 2"}, {"D_SB04", "Magnet cave"}, {"D_SB05", "Grotto (grass) 1"},
    {"D_SB06", "Grotto (grass) 2"}, {"D_SB07", "Grotto (rock) 1"}, {"D_SB08", "Grotto (rock) 2"},
    {"D_SB09", "Grotto (water)"}, {"D_SB10", "Faron Woods cave"},
};

std::mutex g_mutex;
std::optional<Place> g_next;
}  // namespace

const std::vector<Group>& groups() {
    static const std::vector<Group> all = [] {
        std::vector<Group> out;
        std::ifstream f(config::game_dir + "/content/res/Menu/Menu1.dat", std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        const auto u32 = [&](size_t o) { return o + 4 <= d.size() ? (uint32_t)d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3] : 0u; };
        const auto text = [&](size_t o, size_t n) { return std::string((const char*)&d[o], strnlen((const char*)&d[o], n)); };
        for (size_t g = 0, first = u32(4); !d.empty() && g < d[0]; g++) {
            const size_t o = first + g * 0x48;
            if (o + 0x48 > d.size()) break;
            const std::string label = text(o, 0x41), code = label.substr(0, label.find(':'));
            const auto name = kNames.find(code);
            if (name == kNames.end()) continue;
            Group grp{name->second, {}};
            for (size_t i = 0, e = u32(o + 0x44); i < d[o + 0x41] && e + (i + 1) * 0x4C <= d.size(); i++) {
                const size_t p = e + i * 0x4C;
                grp.places.push_back({text(p + 0x41, 8), d[p + 0x4A], (int8_t)d[p + 0x49], (int8_t)d[p + 0x4B]});
            }
            out.push_back(std::move(grp));
        }
        LOG("[warp] the map select's places: %zu areas", out.size());
        return out;
    }();
    return all;
}

void go(const Place& p) {
    std::lock_guard guard(g_mutex);
    g_next = p;
}

// dComIfGp_setNextStage(stage, point, room, layer) with the remaining arguments defaulted (029049F4 is
// the full one); called this way by the reset to the title (02904CA0)
constexpr uint32_t kSetNextStage = 0x02904C60;

// main01's frame loop (interp.cpp), on the main thread; the volatile registers are free there
void service(Cpu* c) {
    static const uint32_t name = mem::runtime_alloc(8, 8);
    const auto warp = [&](const Place& p) {
        memset(mem::ptr(name), 0, 8);
        memcpy(mem::ptr(name), p.stage.data(), std::min<size_t>(p.stage.size(), 7));
        LOG("[warp] to %s point %d room %d layer %d", p.stage.c_str(), p.point, p.room, p.layer);
        guest_call(c, kSetNextStage, {name, (uint32_t)p.point, (uint32_t)p.room, (uint32_t)p.layer});
    };
    std::optional<Place> asked;
    {
        std::lock_guard guard(g_mutex);
        asked.swap(g_next);
    }
    if (asked) return warp(*asked);
    static const char* e = getenv("WWHD_WARP");  // the next warp; null when none is left
    if (!e || !*e) return;
    char* p;
    uint64_t at = strtoull(e, &p, 10);
    if (render::frame_count() < at) return;
    const char* next = strchr(p, ';');
    e = next ? next + 1 : nullptr;
    char stage[8] = {};
    int point = 0, room = 0, layer = -1;
    if (*p != ':' || sscanf(p + 1, "%7[^,],%d,%d,%d", stage, &point, &room, &layer) < 1) {
        LOG("[warp] WWHD_WARP: expected <frame>:<stage>,<point>,<room>,<layer>");
        return;
    }
    warp({stage, point, room, layer});
}

}  // namespace warp
