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
#include <unordered_map>
#include <vector>

#include "runtime.h"

namespace aspect {
constexpr int kWideW = 808;                 // l_tvSize's 16:9 width
constexpr uint32_t kTvSize = 0x100A4CE4;  // l_tvSize
namespace {
constexpr float kBase = 16.0f / 9.0f;
constexpr uint32_t kSetTvSize = 0x02AEC704, kOnWide2D = 0x02A387B4;
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
std::atomic<bool> g_menu_drawn{false}, g_menu_shown{false};  // a full-screen menu at 16:9 in the middle (this frame, the last)
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
    g_menu_shown = g_menu_drawn.exchange(false);
    return g_game;
}
float game() { return g_game; }
bool full_menu() { return g_menu_shown; }
uint64_t game_frame() { return g_swaps.load(std::memory_order_relaxed); }
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
// draws comes out right only at 16:9 (808 units wide). On a wider screen:
// - the HUD (the meter's draw, dMeter2Draw_c::draw 02A31164..02A32000) keeps to the screen's edges: stretched text,
//   and the D-pad's labels, are drawn narrow so that 16:9's stretch gives them their shape (their own x scaled back
//   to that stretch, 808 / width, around their position; a label keeps its 16:9 offset from the D-pad), and the
//   HUD's groups (the 'n_43' panes in the meter's n_base: hearts, rupees, the X/Y/A/B cluster, ...), which hold
//   their parts as unstretched offsets from a stretched point, have each part keep its 16:9 distance from the edge
//   it is nearest to (left or right third), or from the center;
// - every other screen on the TV (menus, messages, the title) is drawn at 16:9 in the middle, as a whole: the
//   root's stretch is narrowed to 16:9's around the center, and a 'n_43' pane's undoing of it with it (a full-screen
//   menu, whose art lines up with its text, shows black at the sides). Pictures beside the layout (off screen at
//   16:9) keep the full stretch and stay off screen, and so do a screen's top layers that reach well past both
//   edges (fills made to cover any screen). A full-screen menu (a top layer from an edge past the middle) has the presenter
//   paint the TV picture black beside it (full_menu). Where the game asks for a menu pane's place on screen
//   (CPaneMgr::getGlobalVtx, 02A95A78: the field map's clip rectangle and icons) it gets the centered place.
// The GamePad's screens are left alone.
namespace {
constexpr uint32_t kN43 = 0x6E5F3433;  // 'n_43': HD undoes the stretch for this pane
enum Screen { kMenu, kHud, kGamePad };
int g_screen = kMenu;  // the screen being drawn, from its root's caller
int g_full = 0;        // > 0: inside a pane drawn at the full stretch (a menu's backdrop)
int g_map = 0;         // > 0: the TV's field map is drawing
std::unordered_map<uint32_t, int> g_tv_screens;  // root pane -> how it was last drawn on the TV

int screen_of(uint32_t sp) {  // the callers' return addresses, up the stack's back chain
    for (int i = 0; i < 8 && (sp = ld32(sp)); i++) {
        const uint32_t lr = ld32(sp + 4);
        if (lr == 0x02AEC510) return kGamePad;  // the painter's call into HD's GamePad drawing
        if (lr >= 0x02A31164 && lr < 0x02A32000) return kHud;
    }
    return kMenu;
}
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
    if (ld32(ctx + 0xB8) != 0x100AA998) return f_02B34334_orig(c);  // not a J2DOrthoGraph
    if (!parent) {                                                   // a screen's root
        const int outer = g_screen;
        g_screen = screen_of(c->r[1]);
        if (g_screen != kGamePad) g_tv_screens[pane] = g_screen;
        f_02B34334_orig(c);
        g_screen = outer;
        return;
    }
    const double x0 = ldf32(ctx + 0xBC), w = ldf32(ctx + 0xC4) - x0, k = aspect::kWideW / w, mid = x0 + w / 2;
    if (k > 0.999 || g_screen == kGamePad) return f_02B34334_orig(c);
    const uint32_t kind = ld32(pane + 4) >> 8;
    const bool text = kind == 0x544258;  // 'TBX'
    std::vector<std::pair<uint32_t, uint32_t>> saved;  // (address, word) to put back after the draw
    auto set = [&](uint32_t a, double v) { saved.push_back({a, ld32(a)}); stf32(a, v); };
    auto squeeze = [&](uint32_t mtx, double f) {  // a matrix's x about the screen's middle
        for (int i = 0; i < 3; i++) set(mtx + 4 * i, ldf32(mtx + 4 * i) * f);
        set(mtx + 12, mid + (ldf32(mtx + 12) - mid) * f);
    };
    int full = 0;
    if (g_screen == kMenu) {
        if (!parent_of(parent)) squeeze(parent + 0x78, k);  // the root's stretch, for its whole tree
        else if (g_full) {
            if (text && !unstretched(pane)) for (int i = 0; i < 3; i++) set(pane + 0x48 + 4 * i, ldf32(pane + 0x48 + 4 * i) * k);
        } else if (ld8(pane + 0xA8) && kind != 0x50414E && !text) {  // a picture or window: is it a backdrop?
            const uint32_t m = parent + 0x78, l = pane + 0x48;
            double g[4];  // its global x row = parent's x row x its own matrix
            for (int j = 0; j < 4; j++)
                g[j] = ldf32(m) * ldf32(l + 4 * j) + ldf32(m + 4) * ldf32(l + 16 + 4 * j) + ldf32(m + 8) * ldf32(l + 32 + 4 * j) + (j == 3 ? ldf32(m + 12) : 0);
            double lo = 1e9, hi = -1e9;
            for (float bx : {ldf32(pane + 0x18), ldf32(pane + 0x20)})
                for (float by : {ldf32(pane + 0x1C), ldf32(pane + 0x24)}) {
                    const double u = (mid + (g[0] * bx + g[1] * by + g[3] - mid) / k - x0) * 608 / w;  // in the layout's units
                    lo = std::min(lo, u), hi = std::max(hi, u);
                }
            int depth = 0;  // a screen's layers are the children of its top group (root -> ROOT -> group -> layer)
            for (uint32_t p = parent; p; p = parent_of(p)) depth++;
            if (hi < 1 || lo > 607 || (depth <= 3 && lo < -50 && hi > 658))  // beside, or a top layer well past both edges
                squeeze(parent + 0x78, 1 / k), full = 1;
            else if (depth <= 3 && ((lo < 1 && hi > 303) || (hi > 607 && lo < 305)))  // a background, or half of one
                aspect::g_menu_drawn = true;
        } else if (ld32(pane + 20) == kN43 && !unstretched(parent))
            for (int i = 0; i < 3; i++) set(pane + 0x48 + 4 * i, ldf32(pane + 0x48 + 4 * i) / k);  // its undoing was for the full stretch
        g_full += full;
        f_02B34334_orig(c);
        g_full -= full;
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) st32(it->first, it->second);
        return;
    }
    const bool label = dpad_group(parent) && ld32(pane + 20) != kN43;  // the D-pad's labels ("Minimap", ...)
    const bool hud_text = text && !dpad_group(parent_of(parent)) && !unstretched(pane);
    const bool group = ld32(pane + 20) == kN43 && ld64(parent + 8) == 0x6E5F62617365ULL;  // in the meter's n_base
    if (!hud_text && !label && !group) return f_02B34334_orig(c);
    const double s = ldf32(parent + 0x78), gx = ldf32(parent + 0x84) + s * ldf32(pane + 0x54);  // its x on screen
    const double g16 = 304 + ((gx - x0) * 608 / w - 304) * aspect::kWideW / 608, half = (w - aspect::kWideW) / 2;
    auto anchor = [&](double x16) { return x16 < 169.33 ? -half : x16 > 438.67 ? half : 0; };  // 16:9's thirds
    if (group) {
        for (uint32_t l = ld32(pane + 0xD4); l; l = ld32(l + 0xC)) {
            const uint32_t part = ld32(l);
            double lo = 1e9, hi = -1e9;
            content_x(part, g16 + ldf32(part + 0x54), lo, hi);  // where the part shows at 16:9 (the group is unscaled)
            set(part + 0x54, ldf32(part + 0x54) + g16 - gx + anchor(lo <= hi ? (lo + hi) / 2 : g16 + ldf32(part + 0x54)));
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

extern "C" void f_02A95A78_orig(Cpu* c);
extern "C" void hook_02A95A78(Cpu* c) {  // CPaneMgr::getGlobalVtx(Vec* out, CPaneMgr*, J2DPane*, Mtx*, u8 corner, ...)
    const uint32_t out = c->r[3], sp = c->r[1];
    uint32_t root = c->r[5];
    for (uint32_t p = root; p; p = parent_of(p)) root = p;
    f_02A95A78_orig(c);
    const double k = aspect::kWideW / double(ld16(aspect::kTvSize + 4));
    const auto it = g_tv_screens.find(root);
    if (k < 0.999 && it != g_tv_screens.end() && it->second == kMenu && screen_of(sp) != kGamePad)
        stf32(out, 304 + (ldf32(out) - 304) * k);
}

// The field map (dMenu_Fmap2DBack_c::draw, 029DF284) draws its region art with J2DPicture::draw(x, y, w, h)
// (02B38B94) at the full stretch: on the TV it is narrowed to 16:9's around the middle, as the menu is (the full
// screen's backgrounds stay as they are).
extern "C" void f_029DF284_orig(Cpu* c);
extern "C" void hook_029DF284(Cpu* c) {
    const int tv = screen_of(c->r[1]) != kGamePad;
    g_map += tv;
    f_029DF284_orig(c);
    g_map -= tv;
}

extern "C" void f_02B38B94_orig(Cpu* c);
extern "C" void hook_02B38B94(Cpu* c) {
    const double w = ld16(aspect::kTvSize + 4), k = aspect::kWideW / w;
    if (g_map && k < 0.999 && c->f[3].ps0 < w - 1) {
        c->f[1].ps0 = c->f[1].ps1 = 304 + (c->f[1].ps0 - 304) * k;
        c->f[3].ps0 = c->f[3].ps1 = c->f[3].ps0 * k;
    }
    f_02B38B94_orig(c);
}
