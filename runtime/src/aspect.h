// Aspect ratio of the TV picture (Graphics menu, WWHD_ASPECT=window|16:10|21:9|32:9): see aspect.cpp.
#pragma once
#include <cstdint>

struct Cpu;

namespace aspect {
enum Mode : int { kOriginal = 0, kWindow, k16x10, k21x9, k32x9, kCustom, kModes };
int mode();
void set_mode(int m);
const char* mode_name(int m);
float mode_value(int m);           // fixed aspect of a mode (0 for kWindow)
void set_window_aspect(float a);   // shape of the TV window's drawable (render thread, every present)
float requested();                 // what the setting asks for now
// main thread, GX2SwapScanBuffers: latch the aspect for the next game frame, update the game's
// projections; returns the aspect the render thread should switch to at this swap
float on_swap();
float game();                      // aspect the game draws with this frame
uint64_t game_frame();             // swaps so far
void ss_reset();  // save state loaded: the game's screen size is set again
void service(Cpu* c);  // main01's frame loop: applies a changed aspect to the game (setTvSize)
}  // namespace aspect
