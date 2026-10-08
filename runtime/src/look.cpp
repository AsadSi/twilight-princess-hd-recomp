// How the game looks (look.h).
// Bloom: mDoGph_gInf_c's bloom object (10182D24; the decomp's bloom_c: enable +0x08, bright-pass ratio +0x0C,
// which the environment sets every frame) is drawn by bloom_c::draw (02AEA3E4); Reduced halves the ratio, Off
// draws without it (the mono color pass, +0x07, still runs). The HUD is the meter's draw (dMeter2Draw_c::draw,
// 02A31164: hearts, rupees, the buttons, the map icon); hidden, it is not drawn, and menus and messages are.
#include "look.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "runtime.h"

namespace look {
namespace {
// WWHD_BLOOM=reduced|off and WWHD_HUD=0 for this start (tests)
int env_bloom() {
    const char* e = getenv("WWHD_BLOOM");
    return !e ? kBloomOriginal : !strcmp(e, "reduced") ? kBloomReduced : !strcmp(e, "off") ? kBloomOff : kBloomOriginal;
}
std::atomic<int> g_bloom{env_bloom()};
std::atomic<bool> g_hud{!getenv("WWHD_HUD") || atoi(getenv("WWHD_HUD")) != 0};
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
bool hud() { return g_hud; }
void set_hud(bool on) {
    if (g_hud.exchange(on) != on) LOG("[look] HUD %s", on ? "shown" : "hidden");
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
