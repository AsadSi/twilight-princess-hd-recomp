// Game time for the mouse as a gyro (motion.cpp). The gameplay mod switches were Wind Waker hooks
// (camera.cpp, mouse.mm, turbo.cpp, cheats.cpp, climb.cpp in git history before the TP HD fork).
#pragma once

namespace mods {
double game_time();  // seconds of game time (logic steps / 30)
}  // namespace mods
