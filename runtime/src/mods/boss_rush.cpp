// The boss rush, the built-in mod "boss-rush" (boss_rush.h). Start warps to the first dungeon boss's room (warp.h);
// once Link has been there, leaving it (by the portal after the fight) warps to the next boss. A game over there sends
// him back to the same boss. The rush ends with Ganondorf's throne room, after which the game leads on to the last
// fights itself. The stage is the name at 1014A5E8 (dComIfG_play_c's, which the game logs at a scene's start; it is the
// new one as soon as a stage change is asked for); Link's life is the save's (10145348 +4).
#include "boss_rush.h"

#include <atomic>
#include <cstring>
#include <iterator>

#include "../warp.h"
#include "builtin.h"
#include "runtime.h"

namespace mods::boss_rush {
namespace {
struct Boss {
    const char* name;
    warp::Place place;
};
const Boss kBosses[] = {
    {"Diababa", {"D_MN05A", 0, 50, 0}},   {"Fyrus", {"D_MN04A", 0, 50, 0}},     {"Morpheel", {"D_MN01A", 0, 50, 0}},
    {"Stallord", {"D_MN10A", 0, 50, 0}},  {"Blizzeta", {"D_MN11A", 0, 50, 0}},  {"Armogohma", {"D_MN06A", 0, 50, 0}},
    {"Argorok", {"D_MN07A", 0, 50, 0}},   {"Zant", {"D_MN08D", 0, 50, -1}},     {"Ganondorf", {"D_MN09A", 0, 50, 0}},
};
constexpr int kCount = (int)std::size(kBosses);
std::atomic<int> g_boss{-1};     // the rush's boss, -1 when none
std::atomic<bool> g_start{false};  // the overlay asked for a start
bool g_arrived = false, g_down = false;  // Link has been in its stage; his life ran out there
}  // namespace

void start() { g_start = true; }

void step() {
    if (!builtin::on("boss-rush")) return void(g_boss = -1);
    if (g_start.exchange(false)) {
        g_boss = 0, g_arrived = g_down = false;
        warp::go(kBosses[0].place);
        return;
    }
    const int b = g_boss;
    if (b < 0) return;
    const bool there = strncmp((const char*)mem::ptr(0x1014A5E8), kBosses[b].place.stage.c_str(), 8) == 0;
    if (there) {
        g_arrived = true;
        if (ld16(0x10145348 + 4) == 0) g_down = true;
        return;
    }
    if (!g_arrived) return;  // on the way there
    g_arrived = false;
    if (g_down) {  // a game over: the same boss again
        g_down = false;
        return warp::go(kBosses[b].place);
    }
    g_boss = b + 1 < kCount ? b + 1 : -1;
    if (b + 1 < kCount) warp::go(kBosses[b + 1].place);
}

std::string status() {
    const int b = g_boss;
    return b < 0 ? std::string() : "Boss " + std::to_string(b + 1) + " of " + std::to_string(kCount) + ": " + kBosses[b].name;
}
}  // namespace mods::boss_rush
