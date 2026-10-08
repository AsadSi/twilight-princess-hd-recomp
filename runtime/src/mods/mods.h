// Gameplay mod switches (Gameplay menu, mod manager, mod packages). Their implementations were Wind
// Waker hooks (camera.cpp, mouse.mm, turbo.cpp, cheats.cpp, climb.cpp in git history before the TP HD
// fork), so for TP HD the switches are kept but do nothing yet.
// Test/start-up switches: WWHD_MOD_<NAME>=1 (see mods.cpp).
#pragma once
#include <cstdint>

namespace mods {

// ---- switches (menu + env) ----
bool direct_camera();
void set_direct_camera(bool on);
float camera_speed();  // direct camera: multiplier of the game's own top turning speed
void set_camera_speed(float s);
bool mouse_camera();
void set_mouse_camera(bool on);
float mouse_sensitivity();  // degrees per mouse point
void set_mouse_sensitivity(float s);
bool first_person_wheel();
void set_first_person_wheel(bool on);
bool quick_doors();
void set_quick_doors(bool on);
bool fast_scenes();
void set_fast_scenes(bool on);

// shared helpers (mods.cpp)
uint64_t step();    // full logic steps so far (interp::logic_steps)
double game_time(); // seconds of game time (steps / 30)
bool trace_on();    // WWHD_MODS_TRACE: log mod decisions
void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace mods
