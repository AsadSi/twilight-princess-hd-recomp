// Frame rate modes (interp.h). TP HD runs at its own 30 fps: frame interpolation and true 60 were
// Wind Waker hooks (git history before the TP HD fork), so the mode is always 0 and the setters only
// log. One logic step is one GX2SwapScanBuffers until TP HD has a main loop hook.
#include <atomic>
#include <cstdlib>

#include "interp.h"
#include "mods/packages.h"
#include "runtime.h"

namespace interp {

int mode() { return 0; }
void set_mode(int m) {
    if (m) LOG("[interp] frame rate modes are not available for TP HD yet");
}
int fps() { return 60; }
void set_fps(int) {}
void toggle_fps(int) { set_mode(1); }
const char* mode_name() { return "30 fps"; }
int output_fps() { return 30; }
int frames_per_step() { return 1; }
bool paced_interpolation() { return false; }
void set_paced_interpolation(bool) {}
bool paced_interpolation_at(int) { return false; }
void set_paced_interpolation_at(int, bool) {}
float paced_drawn_share() { return -1; }

// the display: refresh rate of the TV window's screen (0 = unknown; WWHD_DISPLAY_HZ=n overrides)
static const int g_env_hz = getenv("WWHD_DISPLAY_HZ") ? atoi(getenv("WWHD_DISPLAY_HZ")) : -1;
static std::atomic<int> g_display_hz{0};
int display_hz() { return g_env_hz >= 0 ? g_env_hz : g_display_hz.load(std::memory_order_relaxed); }
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    if (g_display_hz.exchange(hz) != hz) LOG("[interp] display refresh rate %d Hz", hz);
}
void set_present_vsync(bool) {}

// gx2_core.cpp's virtual vsync: the game's own swap interval at the normal rate
uint32_t effective_swap_interval(uint32_t game) { return game; }
int vsync_rate() { return 1; }

// logic steps so far (test scenarios and profiles run on game time): one per swap
static std::atomic<uint64_t> g_steps{0};
uint64_t logic_steps() { return g_steps.load(std::memory_order_relaxed); }
void step_done() { mods::packages::frame(++g_steps); }  // GX2SwapScanBuffers, game thread

}  // namespace interp
