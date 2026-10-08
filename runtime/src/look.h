// How the game looks, beyond the renderer's options (Graphics menu and the settings overlay's Graphics tab;
// WWHD_BLOOM=reduced|off, WWHD_HUD=0): bloom strength and the HUD. See look.cpp.
#pragma once

namespace look {
enum Bloom : int { kBloomOriginal = 0, kBloomReduced, kBloomOff, kBloomModes };
int bloom();
void set_bloom(int b);
const char* bloom_name(int b);
bool hud();  // the HUD (hearts, rupees, buttons, ...) is drawn
void set_hud(bool on);
}  // namespace look
