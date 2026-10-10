// Photo mode, the built-in mod "photo-mode" (photo.h). The game is held still as a hit-stop holds it (dScnPly_c's
// next pause timer, 10129B6B, kept up each step), the HUD and the overlay's bars are left out, and each camera draw
// (camera_draw's hook, interp.cpp) looks from where the sticks put it: around the point the camera looked at when photo
// mode came on (camera process: eye +0xD8, center +0xE4), the right stick turning it, the left stick's up and down
// bringing it closer or further. The game's own camera is put back after each draw. It is switched off at each launch.
#include "photo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "../input.h"
#include "builtin.h"
#include "runtime.h"

namespace mods::photo {
namespace {
bool g_held = false;  // the view below is set up
float g_center[3], g_yaw, g_pitch, g_dist;
std::chrono::steady_clock::time_point g_last;
}  // namespace

bool active() { return builtin::on("photo-mode"); }

void step() {
    static bool started = false;  // photo mode is for the moment: off at each launch (on, it holds the title still too)
    if (std::string error; !started && (started = true) && active()) builtin::set_on("photo-mode", false, error);
    if (!active()) return void(g_held = false);
    st8(0x10129B6B, (uint8_t)std::max<int>((int8_t)ld8(0x10129B6B), 2));
}

void aim(uint32_t cam) {
    const auto now = std::chrono::steady_clock::now();
    if (!g_held) {
        float off[3];
        for (int i = 0; i < 3; i++) g_center[i] = (float)ldf32(cam + 0xE4 + 4 * i), off[i] = (float)ldf32(cam + 0xD8 + 4 * i) - g_center[i];
        g_dist = std::max(1.0f, std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]));
        g_yaw = std::atan2(off[0], off[2]), g_pitch = std::asin(std::clamp(off[1] / g_dist, -1.0f, 1.0f));
        g_last = now, g_held = true;
    }
    const float dt = std::clamp(std::chrono::duration<float>(now - g_last).count(), 0.0f, 0.1f);
    g_last = now;
    const input::PadState pad = input::read();  // per second: 2 radians across, 1.5 up and down, 1.5x the distance
    g_yaw -= pad.rx * 2.0f * dt;
    g_pitch = std::clamp(g_pitch - pad.ry * 1.5f * dt, -1.45f, 1.45f);
    g_dist = std::clamp(g_dist * (1 - pad.ly * 1.5f * dt), 30.0f, 8000.0f);
    const float eye[3] = {g_center[0] + g_dist * std::cos(g_pitch) * std::sin(g_yaw), g_center[1] + g_dist * std::sin(g_pitch),
                          g_center[2] + g_dist * std::cos(g_pitch) * std::cos(g_yaw)};
    for (int i = 0; i < 3; i++) stf32(cam + 0xD8 + 4 * i, eye[i]), stf32(cam + 0xE4 + 4 * i, g_center[i]);
}
}  // namespace mods::photo
