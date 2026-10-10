// The boss rush, the built-in mod "boss-rush": each dungeon's boss in story order (boss_rush.cpp).
#pragma once
#include <string>

namespace mods::boss_rush {
void start();          // to the first boss (the overlay's button)
void step();           // once per logic step, from builtin::step
std::string status();  // for the overlay: which boss, or empty when no rush is on
}  // namespace mods::boss_rush
