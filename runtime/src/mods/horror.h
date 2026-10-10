// Horror events, the built-in mod "horror": now and then during normal play a night turns to a Blood Night, something
// stalks Link from area to area, twilight creeps over the field at dusk, or a shadow rises behind him when he stands
// still (horror.cpp).
#pragma once
#include <cstdint>

struct Cpu;

namespace mods::horror {
void step(Cpu* c);  // once per logic step, from builtin::step
struct Tint {
    uint8_t r, g, b, a;  // the colour over the frame (bloom_c's mono colour), alpha 0 for none
};
Tint tint();  // for look.cpp's hook on bloom_c::draw
}  // namespace mods::horror
