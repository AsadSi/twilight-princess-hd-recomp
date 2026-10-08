// Aspect ratio of the TV picture (aspect.h). TP HD keeps the Wii version's wide screen: setTvSize (02AEC704)
// takes the screen size from l_tvSize (100A4CE4, u16 pairs: {608, 448} and the wide {808, 448}, 808/448 being
// its 16:9) and derives the 3D aspect (1012B374, read by the camera) and the 2D bounds the HUD anchors to (minX
// = -(width - 608) / 2, ...). For another aspect the wide entry is made wider (or, for narrower screens, taller)
// and setTvSize recomputes them (then dMeter2Info_onWide2D, 02A387B4, as onWide does); the screen-shaped render
// targets get as much wider or taller (gfx/metal_surfaces.mm), so the game draws the new shape and lays out its
// HUD to its edges.
#include "aspect.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "runtime.h"

namespace aspect {
namespace {
constexpr float kBase = 16.0f / 9.0f;
constexpr uint32_t kTvSize = 0x100A4CE4, kSetTvSize = 0x02AEC704, kOnWide2D = 0x02A387B4;
constexpr int kWideW = 808, kWideH = 448;
int env_mode() {
    const char* e = getenv("WWHD_ASPECT");
    if (!e) return kOriginal;
    if (!strcmp(e, "window")) return kWindow;
    if (!strcmp(e, "16:10")) return k16x10;
    if (!strcmp(e, "21:9")) return k21x9;
    if (!strcmp(e, "32:9")) return k32x9;
    return kOriginal;
}
std::atomic<int> g_mode{env_mode()};
std::atomic<float> g_window{kBase};
std::atomic<float> g_game{kBase};  // the aspect the game draws with (set on the main thread)
std::atomic<uint64_t> g_swaps{0};
float g_applied = 0;  // main thread: the requested aspect setTvSize last ran for (0: run it again)
}  // namespace

const char* mode_name(int m) {
    static const char* n[kModes] = {"16:9 (original)", "Match window", "16:10", "21:9", "32:9", "custom"};
    return m >= 0 && m < kModes ? n[m] : "?";
}
float mode_value(int m) {
    switch (m) {
    case kOriginal: return kBase;
    case k16x10: return 16.0f / 10.0f;
    case k21x9: return 64.0f / 27.0f;
    case k32x9: return 32.0f / 9.0f;
    default: return 0;
    }
}
int mode() { return g_mode; }
void set_mode(int m) {
    if (m < kOriginal || m > k32x9) m = kOriginal;
    if (g_mode.exchange(m) != m) LOG("[aspect] %s", mode_name(m));
}
void set_window_aspect(float a) {
    if (a > 0.5f && a < 8.0f) g_window = a;
}
float requested() { return mode() == kWindow ? g_window.load() : mode_value(mode()); }
float on_swap() {
    g_swaps.fetch_add(1, std::memory_order_relaxed);
    return g_game;
}
float game() { return g_game; }
uint64_t game_frame() { return g_swaps.load(std::memory_order_relaxed); }
bool tagged_projection() { return false; }
void layout_root_target(uint32_t, bool) {}
void ss_reset() { g_applied = 0; }

// main01's frame loop (interp.cpp, logic passes): a changed aspect gives the game a wider or taller wide screen
void service(Cpu* c) {
    const float want = requested();
    if (want == g_applied) return;
    int w = kWideW, h = kWideH;
    if (want >= kBase) w = (int)std::lround(kWideW * want / kBase);
    else h = (int)std::lround(kWideH * kBase / want);
    st16(kTvSize + 4, (uint16_t)w);
    st16(kTvSize + 6, (uint16_t)h);
    guest_call(c, kSetTvSize, {});
    guest_call(c, kOnWide2D, {});  // dMeter2Info_onWide2D, as onWide() calls after setTvSize: the HUD lays out again
    g_applied = want;
    g_game = kBase * (float(w) / kWideW) * (float(kWideH) / h);
    LOG("[aspect] %.4f: the game's wide screen is %dx%d", g_game.load(), w, h);
}

}  // namespace aspect
