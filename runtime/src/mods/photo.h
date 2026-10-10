// Photo mode, the built-in mod "photo-mode": the game held still, no HUD, the camera moved by the sticks (photo.cpp).
#pragma once
#include <cstdint>

namespace mods::photo {
bool active();
void step();             // once per logic step, from builtin::step
void aim(uint32_t cam);  // in camera_draw, with photo mode on: the camera process's eye and center for this draw
}  // namespace mods::photo
