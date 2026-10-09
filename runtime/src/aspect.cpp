// Aspect ratio of the TV picture (aspect.h). TP HD keeps the Wii version's wide screen: setTvSize (02AEC704)
// takes the screen size from l_tvSize (100A4CE4, u16 pairs: {608, 448} and the wide {808, 448}, 808/448 being
// its 16:9) and derives the 3D aspect (1012B374, read by the camera) and the 2D bounds the HUD anchors to (minX
// = -(width - 608) / 2, ...). For another aspect the wide entry is made wider (or, for narrower screens, taller)
// and setTvSize recomputes them (then dMeter2Info_onWide2D, 02A387B4, as onWide does); the screen-shaped render
// targets get as much wider or taller (gfx/metal_surfaces.mm), so the game draws the new shape and lays out its
// HUD to its edges.
#include "aspect.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "runtime.h"

namespace aspect {
constexpr int kWideW = 808;  // l_tvSize's 16:9 width
namespace {
constexpr float kBase = 16.0f / 9.0f;
constexpr uint32_t kTvSize = 0x100A4CE4, kSetTvSize = 0x02AEC704, kOnWide2D = 0x02A387B4;
constexpr int kWideH = 448;
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
    // The TV minimap's top-left (dMeterMap's draw, 02A52C00: a function static at 10169504, set from 1009B16C on its
    // first draw, guard 10169510) is a 16:9 layout position: it moves out with the left edge. The game drops the
    // map once it ends left of 16:9's edge (x -100), so it moves at most 21:9's distance (134.5).
    stf32(0x10169504, ldf32(0x1009B16C) - std::min((w - kWideW) / 2.0, 134.5));
    stf32(0x10169508, ldf32(0x1009B170));
    st32(0x10169510, 1);
    guest_call(c, kOnWide2D, {});  // dMeter2Info_onWide2D, as onWide() calls after setTvSize: the HUD lays out again
    g_applied = want;
    g_game = kBase * (float(w) / kWideW) * (float(kWideH) / h);
    LOG("[aspect] %.4f: the game's wide screen is %dx%d", g_game.load(), w, h);
}

}  // namespace aspect

// J2DPane::draw (02B34334). TP HD stretches each 2D screen, laid out in the Wii's 608 units, to the screen's width
// (a root pane's x by ortho width / 608 about x = 304) and undoes it for panes whose user tag is 'n_43'. What it
// draws comes out right only at 16:9 (808 units wide), so on a wider screen, while these draw:
// - stretched text, and the D-pad's labels, are drawn narrow so that 16:9's stretch gives them their shape: their
//   own x is scaled back to that stretch (808 / width) around their position, and a label keeps its 16:9 offset
//   from the D-pad;
// - the HUD's groups (the 'n_43' panes in the meter's n_base: hearts, rupees, the X/Y/A/B cluster, ...) hold their
//   parts as unstretched offsets from a stretched point, so the parts drift inward.
// A part keeps its 16:9 distance from the edge it is nearest to (left or right third), or from the center.
namespace {
// the x range of a pane's shown pictures and text at 16:9, from its own x there (offsets only; the HUD's parts are
// unscaled; hidden ones are often parked off screen)
void content_x(uint32_t p, double x, double& lo, double& hi, int depth = 0) {
    if (!ld8(p + 0xA8)) return;                                                     // mVisible
    if (ld32(p + 4) >> 8 != 0x50414E) lo = std::min(lo, x), hi = std::max(hi, x);  // not a plain 'PAN' pane
    if (depth < 4)
        for (uint32_t l = ld32(p + 0xD4); l; l = ld32(l + 0xC)) content_x(ld32(l), x + ldf32(ld32(l) + 0x54), lo, hi, depth + 1);
}
uint32_t parent_of(uint32_t pane) {
    const uint32_t list = pane ? ld32(pane + 0xE4) : 0;
    return list ? ld32(list + 0xC) : 0;
}
constexpr uint32_t kN43 = 0x6E5F3433;  // 'n_43': HD undoes the stretch for this pane
bool unstretched(uint32_t p) {
    for (; p; p = parent_of(p))
        if (ld32(p + 20) == kN43) return true;
    return false;
}
bool dpad_group(uint32_t p) { return p && ld64(p + 8) == 0x6A756A695F6EULL; }  // juji_n: the D-pad and its labels
}  // namespace

extern "C" void f_02B34334_orig(Cpu* c);
extern "C" void hook_02B34334(Cpu* c) {
    const uint32_t pane = c->r[3], ctx = c->r[4], parent = parent_of(pane);
    if (!parent || ld32(ctx + 0xB8) != 0x100AA998) return f_02B34334_orig(c);  // a root pane, or not a J2DOrthoGraph
    const double x0 = ldf32(ctx + 0xBC), w = ldf32(ctx + 0xC4) - x0, k = aspect::kWideW / w;
    const bool label = dpad_group(parent) && ld32(pane + 20) != kN43;  // the D-pad's labels ("Minimap", ...)
    const bool text = ld32(pane + 4) >> 8 == 0x544258 && !dpad_group(parent_of(parent)) && !unstretched(pane);  // 'TBX'
    const bool group = ld32(pane + 20) == kN43 && ld64(parent + 8) == 0x6E5F62617365ULL;  // in the meter's n_base
    if (k > 0.999 || (!text && !label && !group)) return f_02B34334_orig(c);
    const double s = ldf32(parent + 0x78), g = ldf32(parent + 0x84) + s * ldf32(pane + 0x54);  // its x on screen
    const double g16 = 304 + ((g - x0) * 608 / w - 304) * aspect::kWideW / 608, half = (w - aspect::kWideW) / 2;
    auto anchor = [&](double x16) { return x16 < 169.33 ? -half : x16 > 438.67 ? half : 0; };  // 16:9's thirds
    std::vector<std::pair<uint32_t, uint32_t>> saved;  // (address, word) to put back after the draw
    auto set = [&](uint32_t a, double v) { saved.push_back({a, ld32(a)}); stf32(a, v); };
    if (group) {
        for (uint32_t l = ld32(pane + 0xD4); l; l = ld32(l + 0xC)) {
            const uint32_t part = ld32(l);
            double lo = 1e9, hi = -1e9;
            content_x(part, g16 + ldf32(part + 0x54), lo, hi);  // where the part shows at 16:9 (the group is unscaled)
            set(part + 0x54, ldf32(part + 0x54) + g16 - g + anchor(lo <= hi ? (lo + hi) / 2 : g16 + ldf32(part + 0x54)));
        }
    } else {
        for (int i = 0; i < 3; i++) set(pane + 0x48 + 4 * i, ldf32(pane + 0x48 + 4 * i) * k);
        if (label)  // its 16:9 offset from the D-pad (juj_i_n, unstretched, in the same group)
            for (uint32_t l = ld32(parent + 0xD4); l; l = ld32(l + 0xC))
                if (ld64(ld32(l) + 8) == 0x6A756A5F695F6EULL) {
                    const double d = ldf32(ld32(l) + 0x54);
                    set(pane + 0x54, d + (ldf32(pane + 0x54) - d) * k);
                }
    }
    f_02B34334_orig(c);
    for (auto& [a, v] : saved) st32(a, v);
}
