// How the game looks (look.h).
// Bloom: mDoGph_gInf_c's bloom object (10182D24; the decomp's bloom_c: enable +0x08, bright-pass ratio +0x0C,
// which the environment sets every frame) is drawn by bloom_c::draw (02AEA3E4); Reduced halves the ratio, Off
// draws without it (the mono color pass, +0x07, still runs). The HUD is the meter's draw (dMeter2Draw_c::draw,
// 02A31164: hearts, rupees, the buttons, the map icon); hidden, it is not drawn, and menus and messages are.
// Distant fog: HD fogs in its vertex shaders, fog = opacity * clamp(GX fog z / reach, 0, 1) toward the fog color.
// GXSetFog (02BDAD3C) passes the GX parameters to 02BDAB7C, which reads opacity (1012C898, 0.925) and reach
// (1012C89C, 0.9), cuts each to twentieths and packs them into the fog constants as opacity + 21 * reach. Reduced
// halves the opacity (0.45), Off makes it 0; only that function reads the float, and it is put back after the call.
// Cemu's TP HD "Distant Fog" pack writes the same float (21.0 there is no fog: the excess spills into the reach).
// Texture sharpness is the Metal renderer's (metal_draw.mm: add_lod_bias); Cemu's TP HD pack biases the textures'
// LOD by -0.5 or -1 for play.
#include "look.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "runtime.h"

namespace look {
namespace {
// WWHD_BLOOM / WWHD_FOG=reduced|off, WWHD_HUD=0 and WWHD_SHARPNESS=sharper|sharpest for this start (tests)
int env_strength(const char* var) {
    const char* e = getenv(var);
    return !e ? kBloomOriginal : !strcmp(e, "reduced") ? kBloomReduced : !strcmp(e, "off") ? kBloomOff : kBloomOriginal;
}
int env_sharpness() {
    const char* e = getenv("WWHD_SHARPNESS");
    return !e ? kSharpOriginal : !strcmp(e, "sharper") ? kSharper : !strcmp(e, "sharpest") ? kSharpest : kSharpOriginal;
}
std::atomic<int> g_bloom{env_strength("WWHD_BLOOM")};
std::atomic<int> g_fog{env_strength("WWHD_FOG")};
std::atomic<bool> g_hud{!getenv("WWHD_HUD") || atoi(getenv("WWHD_HUD")) != 0};
std::atomic<int> g_sharpness{env_sharpness()};
}  // namespace

int bloom() { return g_bloom; }
void set_bloom(int b) {
    if (b < 0 || b >= kBloomModes) b = kBloomOriginal;
    if (g_bloom.exchange(b) != b) LOG("[look] bloom: %s", bloom_name(b));
}
const char* bloom_name(int b) {
    static const char* n[kBloomModes] = {"Original", "Reduced", "Off"};
    return b >= 0 && b < kBloomModes ? n[b] : "?";
}
int fog() { return g_fog; }
void set_fog(int f) {
    if (f < 0 || f >= kBloomModes) f = kBloomOriginal;
    if (g_fog.exchange(f) != f) LOG("[look] distant fog: %s", bloom_name(f));
}
bool hud() { return g_hud; }
void set_hud(bool on) {
    if (g_hud.exchange(on) != on) LOG("[look] HUD %s", on ? "shown" : "hidden");
}
int sharpness() { return g_sharpness; }
void set_sharpness(int s) {
    if (s < 0 || s >= kSharpnessModes) s = kSharpOriginal;
    if (g_sharpness.exchange(s) != s) LOG("[look] texture sharpness: %s", sharpness_name(s));
}
const char* sharpness_name(int s) {
    static const char* n[kSharpnessModes] = {"Original", "Sharper", "Sharpest"};
    return s >= 0 && s < kSharpnessModes ? n[s] : "?";
}
float lod_bias() {
    static const float bias[kSharpnessModes] = {0.0f, -0.5f, -1.0f};
    return bias[g_sharpness];
}
}  // namespace look

extern "C" void f_02AEA3E4_orig(Cpu* c);
extern "C" void hook_02AEA3E4(Cpu* c) {  // bloom_c::draw(bloom_c*)
    const int b = look::bloom();
    if (b == look::kBloomOriginal) return f_02AEA3E4_orig(c);
    const uint32_t bloom = c->r[3];
    const uint8_t enable = ld8(bloom + 0x08), ratio = ld8(bloom + 0x0C);
    if (b == look::kBloomOff) st8(bloom + 0x08, 0);
    else st8(bloom + 0x0C, ratio / 2);
    f_02AEA3E4_orig(c);
    st8(bloom + 0x08, enable);
    st8(bloom + 0x0C, ratio);
}

extern "C" void f_02A31164_orig(Cpu* c);
extern "C" void hook_02A31164(Cpu* c) {  // dMeter2Draw_c::draw
    if (look::hud()) f_02A31164_orig(c);
}

extern "C" void f_02BDAB7C_orig(Cpu* c);
extern "C" void hook_02BDAB7C(Cpu* c) {  // GXSetFog's state write (type, color, A, B, C)
    const int f = look::fog();
    if (f == look::kBloomOriginal) return f_02BDAB7C_orig(c);
    const uint32_t opacity = ld32(0x1012C898);
    stf32(0x1012C898, f == look::kBloomOff ? 0.0 : u32_as_f32(opacity) / 2);
    f_02BDAB7C_orig(c);
    st32(0x1012C898, opacity);
}
