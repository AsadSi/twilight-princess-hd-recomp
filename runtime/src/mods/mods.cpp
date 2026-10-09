// Game time for the mouse as a gyro. See mods.h.
#include "mods.h"

#include <cstdint>

namespace interp { uint64_t logic_steps(); }

namespace mods {
double game_time() { return (double)interp::logic_steps() / 30.0; }
}  // namespace mods
