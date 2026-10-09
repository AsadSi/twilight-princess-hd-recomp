#include "mouse_sdl.h"
#include "../motion/motion.h"
#include "../overlay/overlay.h"
#include "../runtime.h"
#include <cstdlib>

namespace mods {
namespace {
SDL_Window* tv = nullptr;
}
void mouse_init(SDL_Window* window) { tv = window; }
// the mouse as a gyro (motion.h): while the game aims the pointer is captured, so that Steam Input's
// gyro-to-mouse (or the mouse) never stops at the screen's edge; it is released when the aim ends
static bool gyro_capture = false;
static void update_gyro_capture() {
    const bool want = tv && motion::mouse_drives_gyro() && !overlay::captures() && SDL_GetKeyboardFocus() == tv &&
                      !getenv("WWHD_NO_HOST_INPUT");
    if (want && !gyro_capture) {
        gyro_capture = true;
        if (SDL_SetWindowRelativeMouseMode(tv, true)) LOG("[gyro] mouse captured while the game aims");
    } else if (!want && gyro_capture) {
        gyro_capture = false;
        if (tv) SDL_SetWindowRelativeMouseMode(tv, false);
    }
}
void update_mouse() { update_gyro_capture(); }
bool handle_mouse_event(const SDL_Event& event) {
    update_mouse();
    if (!tv || getenv("WWHD_NO_HOST_INPUT")) return false;
    // the mouse as a gyro: every movement in the game window (motion.h keeps it only while the game aims)
    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == SDL_GetWindowID(tv)) {
        motion::mouse_motion(event.motion.xrel, event.motion.yrel);
        if (motion::mouse_drives_gyro()) return true;
    }
    return false;
}
}
