// nn_nfp (amiibo) with virtual figures (amiibo.h): no reader, no figure dumps, no keys. A game only sees a
// figure through this API, already decrypted, so a figure here is its character ID plus, once a game has made
// one, its application area, kept as plain bytes in <settings>/amiibo/<figure>-<area id>.bin.
// Structures and the event protocol follow Cemu's nn_nfp: the activate event fires when a figure is found,
// the deactivate event when it is lifted off. A figure stays on the reader until the game has read it.
// TP HD: its amiibo thread (02002D24, one read per request: 020025D0) polls the activate event, then calls
// GetTagInfo, Mount, GetNfpCommonInfo (game+character ID at +6: 0100 Link, 0101 Zelda, 0102 Ganondorf, 0103 Wolf
// Link, by the table at 10000140; the variant is not looked at), GetNfpRegisterInfo and OpenApplicationArea
// (1019C800). Wolf Link's 0x34 bytes there are read (quick start, Cave of Shadows), created and written.
#include "../amiibo.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "../gfx/renderer.h"
#include "../platform/host.h"
#include "../runtime.h"

void os_init_event(uint32_t e, bool signaled, bool auto_reset);  // threads.cpp
void os_signal_event(uint32_t e);

namespace {
// nn::Result: bit 31 set = failure
constexpr uint32_t kResultOk = 0;
constexpr uint32_t kResultFail = 0xA0000000;
constexpr uint32_t kResultNoArea = 0xA1B10400;  // module 27 (nfp) "no application area": the game may create one
constexpr uint32_t kAreaSize = 0xD8;

struct Info {
    const char* name;
    const char* key;  // file names, WWHD_AMIIBO
    uint8_t character[3];  // game+character ID, variant
};
const Info kInfo[amiibo::kFigures] = {
    {"Wolf Link", "wolf_link", {0x01, 0x03, 0x00}},
    {"Link", "link", {0x01, 0x00, 0x00}},
    {"Zelda", "zelda", {0x01, 0x01, 0x00}},
    {"Ganondorf", "ganondorf", {0x01, 0x02, 0x00}},
};

std::mutex g_mu;
uint32_t g_activate = 0, g_deactivate = 0;  // the game's OSEvents
int g_figure = -1;                          // on the reader
bool g_mounted = false;
uint32_t g_area_id = 0;  // the open application area (0: none)
std::vector<uint8_t> g_area;

std::string area_path(uint32_t id) {
    char name[64];
    snprintf(name, sizeof name, "/amiibo/%s-%08x.bin", kInfo[g_figure].key, id);
    return host::config_dir() + name;
}
bool save_area(uint32_t id, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(host::config_dir() + "/amiibo", ec);
    const std::string p = area_path(id);
    FILE* f = fopen(p.c_str(), "wb");
    bool ok = f && fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) ok = fclose(f) == 0 && ok;
    LOG("[nfp] %s: application area %08X (%zu bytes) %s %s", kInfo[g_figure].name, id, bytes.size(),
        ok ? "saved to" : "could not be saved to", p.c_str());
    return ok;
}
// fires one of the game's events, auto-reset as SetActivateEvent made them (a save state taken before nn_nfp was
// implemented holds them as the game's polls left them)
void fire(uint32_t ev) {
    if (!ev) return;
    os_init_event(ev, false, true);
    os_signal_event(ev);
}
}  // namespace

namespace amiibo {
const char* name(int f) { return f >= 0 && f < kFigures ? kInfo[f].name : "?"; }
void tap(int f) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_figure = f;
    LOG("[nfp] %s on the reader", kInfo[f].name);
    fire(g_activate);  // found now, or as soon as the game looks for a figure
}
int on_reader() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_figure;
}
void service() {
    static const char* e = getenv("WWHD_AMIIBO");  // the next tap; null when none is left
    if (!e || !*e) return;
    char key[16];
    unsigned long long at;
    if (sscanf(e, "%15[^@]@%llu", key, &at) != 2) {
        LOG("[nfp] WWHD_AMIIBO: expected <figure>@<frame>[,...]");
        e = nullptr;
        return;
    }
    if (render::frame_count() < at) return;
    const char* next = strchr(e, ',');
    e = next ? next + 1 : nullptr;
    for (int f = 0; f < kFigures; f++)
        if (!strcmp(key, kInfo[f].key)) return tap(f);
    LOG("[nfp] WWHD_AMIIBO: no figure %s (wolf_link, link, zelda, ganondorf)", key);
}
}  // namespace amiibo

HLE(nn_nfp, Initialize__Q2_2nn3nfpFv) { ret(c, kResultOk); }
HLE(nn_nfp, Finalize__Q2_2nn3nfpFv) { ret(c, kResultOk); }
// the library makes the game's events auto-reset
HLE(nn_nfp, SetActivateEvent__Q2_2nn3nfpFP7OSEvent) {
    std::lock_guard<std::mutex> lk(g_mu);
    os_init_event(g_activate = arg(c, 0), false, true);
    ret(c, kResultOk);
}
HLE(nn_nfp, SetDeactivateEvent__Q2_2nn3nfpFP7OSEvent) {
    std::lock_guard<std::mutex> lk(g_mu);
    os_init_event(g_deactivate = arg(c, 0), false, true);
    ret(c, kResultOk);
}
HLE(nn_nfp, StartDetection__Q2_2nn3nfpFv) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_figure >= 0) fire(g_activate);
    ret(c, kResultOk);
}
HLE(nn_nfp, StopDetection__Q2_2nn3nfpFv) { ret(c, kResultOk); }
HLE(nn_nfp, GetTagInfo__Q2_2nn3nfpFPQ3_2nn3nfp7TagInfo) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_figure < 0) return ret(c, kResultFail);
    const uint32_t info = arg(c, 0);
    static const uint8_t uid[7] = {0x04, 'T', 'P', 'H', 'D', 0x00, 0x00};  // 0x04: NXP, as the figures' NTAG215
    memset(mem::ptr(info), 0, 0x54);
    st8(info, sizeof uid);
    memcpy(mem::ptr(info + 1), uid, sizeof uid);
    st8(info + 7, g_figure);  // one UID per figure
    ret(c, kResultOk);
}
HLE(nn_nfp, Mount__Q2_2nn3nfpFv) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_mounted = g_figure >= 0;
    ret(c, g_mounted ? kResultOk : kResultFail);
}
// the game is done with the figure: it comes off the reader
HLE(nn_nfp, Unmount__Q2_2nn3nfpFv) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_mounted) {
        LOG("[nfp] %s lifted off the reader", kInfo[g_figure].name);
        g_figure = -1;
        fire(g_deactivate);
    }
    g_mounted = false;
    g_area_id = 0;
    ret(c, kResultOk);
}
HLE(nn_nfp, GetNfpCommonInfo__Q2_2nn3nfpFPQ3_2nn3nfp10CommonInfo) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_mounted) return ret(c, kResultFail);
    const uint32_t info = arg(c, 0);
    memset(mem::ptr(info), 0, 0x40);
    memcpy(mem::ptr(info + 6), kInfo[g_figure].character, 3);  // then series, number, type (0: figure), version
    st16(info + 0xE, kAreaSize);
    LOG("[nfp] the game reads %s (character %02X%02X)", kInfo[g_figure].name, kInfo[g_figure].character[0],
        kInfo[g_figure].character[1]);
    ret(c, kResultOk);
}
// registered: an owner and a nickname are set (both left empty)
HLE(nn_nfp, GetNfpRegisterInfo__Q2_2nn3nfpFPQ3_2nn3nfp12RegisterInfo) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_mounted) return ret(c, kResultFail);
    memset(mem::ptr(arg(c, 0)), 0, 0xA8);
    ret(c, kResultOk);
}
HLE(nn_nfp, OpenApplicationArea__Q2_2nn3nfpFUi) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_mounted) return ret(c, kResultFail);
    const uint32_t id = arg(c, 0);
    FILE* f = fopen(area_path(id).c_str(), "rb");
    if (!f) return ret(c, kResultNoArea);
    g_area.assign(kAreaSize, 0);
    g_area.resize(fread(g_area.data(), 1, kAreaSize, f));
    fclose(f);
    g_area_id = id;
    ret(c, kResultOk);
}
HLE(nn_nfp, ReadApplicationArea__Q2_2nn3nfpFPvUi) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_area_id) return ret(c, kResultFail);
    const uint32_t buf = arg(c, 0), len = std::min(arg(c, 1), kAreaSize);
    memset(mem::ptr(buf), 0, len);
    memcpy(mem::ptr(buf), g_area.data(), std::min<size_t>(len, g_area.size()));
    LOG("[nfp] %s: the game reads application area %08X (%u bytes)", kInfo[g_figure].name, g_area_id, len);
    ret(c, kResultOk);
}
// kept until Flush, as on the figure
HLE(nn_nfp, WriteApplicationArea__Q2_2nn3nfpFPCvUiRCQ3_2nn3nfp5TagId) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_area_id) return ret(c, kResultFail);
    const uint8_t* p = mem::ptr(arg(c, 0));
    g_area.assign(p, p + std::min(arg(c, 1), kAreaSize));
    LOG("[nfp] %s: the game writes application area %08X (%zu bytes)", kInfo[g_figure].name, g_area_id, g_area.size());
    ret(c, kResultOk);
}
HLE(nn_nfp, Flush__Q2_2nn3nfpFv) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_mounted) return ret(c, kResultFail);
    ret(c, !g_area_id || save_area(g_area_id, g_area) ? kResultOk : kResultFail);
}
// (id, initial data, its size); written to the figure at once. The game opens it afterwards
HLE(nn_nfp, CreateApplicationArea__Q2_2nn3nfpFRCQ3_2nn3nfp25ApplicationAreaCreateInfo) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_mounted) return ret(c, kResultFail);
    const uint32_t info = arg(c, 0);
    const uint8_t* p = mem::ptr(ld32(info + 4));
    ret(c, save_area(ld32(info), std::vector<uint8_t>(p, p + std::min(ld32(info + 8), kAreaSize))) ? kResultOk : kResultFail);
}
HLE(nn_nfp, InitializeCreateInfo__Q2_2nn3nfpFPQ3_2nn3nfp25ApplicationAreaCreateInfo) { ret(c, kResultOk); }
HLE(nn_nfp, GetErrorCode__Q2_2nn3nfpFRCQ2_2nn6Result) { ret(c, 0); }
// amiibo Settings (the system applet that registers an owner) is not there
HLE(nn_nfp, InitializeAmiiboSettingsArgsIn__Q2_2nn3nfpFPQ3_2nn3nfp20AmiiboSettingsArgsIn) { ret(c, kResultOk); }
HLE(nn_nfp, SwitchToAmiiboSettings__Q2_2nn3nfpFRCQ3_2nn3nfp20AmiiboSettingsArgsInPCcUi) { ret(c, kResultFail); }
HLE(nn_nfp, GetAmiiboSettingsResult__Q2_2nn3nfpFPQ3_2nn3nfp20AmiiboSettingsResultRC15SysArgDataBlock) { ret(c, kResultFail); }
