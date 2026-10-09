// Game time and the game's aiming state for the mouse as a gyro (motion.cpp). The gameplay mod switches
// were Wind Waker hooks (camera.cpp, mouse.mm, turbo.cpp, cheats.cpp, climb.cpp in git history before the
// TP HD fork).
#pragma once

namespace mods {
double game_time();  // seconds of game time (logic steps / 30)
// main01's frame loop, once per logic step (interp.cpp): whether the game aims now (motion::set_aiming)
void update_aiming();
}  // namespace mods
