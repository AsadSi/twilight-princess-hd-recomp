// Aspect ratio of the TV picture (aspect.h). TP HD draws its original 16:9 picture: the wider modes
// needed the game's projection and layout hooks, which were Wind Waker's (git history before the
// TP HD fork), so the mode stays kOriginal and render targets keep their guest size.
#include "aspect.h"

#include <atomic>

#include "runtime.h"

namespace aspect {
namespace {
constexpr float kBase = 16.0f / 9.0f;
std::atomic<uint64_t> g_swaps{0};
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
int mode() { return kOriginal; }
void set_mode(int m) {
    if (m != kOriginal) LOG("[aspect] only 16:9 is available for TP HD yet");
}
void set_window_aspect(float) {}
float requested() { return kBase; }
float on_swap() {
    g_swaps.fetch_add(1, std::memory_order_relaxed);
    return kBase;
}
float game() { return kBase; }
uint64_t game_frame() { return g_swaps.load(std::memory_order_relaxed); }
bool tagged_projection() { return false; }
void layout_root_target(uint32_t, bool) {}
void ss_reset() {}

}  // namespace aspect
