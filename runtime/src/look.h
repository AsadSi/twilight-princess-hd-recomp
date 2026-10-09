// How the game looks, beyond the renderer's options (Graphics menu and the settings overlay's Graphics tab;
// WWHD_BLOOM=reduced|off, WWHD_FOG=reduced|off, WWHD_HUD=0, WWHD_SHARPNESS=sharper|sharpest): bloom strength,
// distant fog, the HUD and texture sharpness (the Metal renderer's LOD bias). See look.cpp.
#pragma once

namespace look {
enum Bloom : int { kBloomOriginal = 0, kBloomReduced, kBloomOff, kBloomModes };
int bloom();
void set_bloom(int b);
const char* bloom_name(int b);
int fog();  // distant fog: the Bloom choices (Original, Reduced, Off)
void set_fog(int f);
bool hud();  // the HUD (hearts, rupees, buttons, ...) is drawn
void set_hud(bool on);
enum Sharpness : int { kSharpOriginal = 0, kSharper, kSharpest, kSharpnessModes };
int sharpness();
void set_sharpness(int s);
const char* sharpness_name(int s);
float lod_bias();  // added to mipmapped textures' level of detail (negative: sharper)
}  // namespace look
