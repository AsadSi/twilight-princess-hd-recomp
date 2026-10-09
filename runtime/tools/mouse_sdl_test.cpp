// Exercise event routing and capture transitions without opening a desktop window.
// The SDL window boundary is simulated; the production mouse event code is linked.
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include "platform/mouse_sdl.h"
static bool relative = false;
extern "C" SDL_WindowID SDL_GetWindowID(SDL_Window*) { return 17; }
extern "C" bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool value) { relative = value; return true; }
extern "C" SDL_Window* SDL_GetKeyboardFocus() { return reinterpret_cast<SDL_Window*>(1); }
void log_msg(const char*, ...) {}
// the mouse as a gyro (motion/motion.h): the game aims while `aiming`
static bool gyro_mouse = false, aiming = false, overlay_open = false;
static float gx = 0, gy = 0;
namespace motion {
void mouse_motion(float x, float y) { if (gyro_mouse && aiming) { gx += x; gy += y; } }
bool mouse_drives_gyro() { return gyro_mouse && aiming; }
}
namespace overlay { bool captures() { return overlay_open; } }
int main() {
    mods::mouse_init(reinterpret_cast<SDL_Window*>(1));
    // mouse gyro: the pointer is captured while the game aims and released after; movement in the game
    // window goes to the gyro then
    gyro_mouse = true; mods::update_mouse(); assert(!relative);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = 17; e.motion.xrel = 5; e.motion.yrel = 2;
    assert(!mods::handle_mouse_event(e)); assert(gx == 0 && gy == 0);
    aiming = true; mods::update_mouse(); assert(relative);
    assert(mods::handle_mouse_event(e)); assert(gx == 5 && gy == 2);
    e.motion.windowID = 18; assert(!mods::handle_mouse_event(e)); assert(gx == 5 && gy == 2);
    overlay_open = true; mods::update_mouse(); assert(!relative);  // the settings overlay takes the pointer
    overlay_open = false; mods::update_mouse(); assert(relative);
    aiming = false; mods::update_mouse(); assert(!relative);
    gyro_mouse = false;
    mods::mouse_init(nullptr);
    puts("mouse_sdl_test: window routing and mouse gyro capture passed");
}
