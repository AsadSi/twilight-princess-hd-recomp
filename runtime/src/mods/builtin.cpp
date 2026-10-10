// Built-in mods. See builtin.h.
//
// The cheats read and write the live save data, dSv_player_c at 10145348 (the layout of the disc's QA saves; HD's
// status A is the decomp's with a u16 in front): max life +2, life +4, rupees +6, lantern oil +0x0A (u16, in units the
// game burns per step), wallet size +0x1B (300, 1,000 or 2,000 rupees); the item record at +0xEC: arrows, the three
// bomb bags, slingshot seeds at +0xF4. They act on what changed in a logic step, so the game's own rules (maxima,
// wallet, death) still apply, and start over whenever Link's actor changes (a file loaded, a stage entered).
#include "builtin.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "mod_json.h"
#include "packages.h"
#include "runtime.h"

namespace mods::builtin {
namespace {
const std::vector<std::string> kOnOff = {"On", "Off"};

const std::vector<Mod> kCatalogue = {
    {"infinite-ammo", "Infinite ammo and oil",
     "Arrows, bombs, slingshot seeds and lantern oil don't run out (what the game takes is given back).",
     {{"arrows", "Arrows", kOnOff}, {"bombs", "Bombs", kOnOff}, {"seeds", "Slingshot seeds", kOnOff}, {"oil", "Lantern oil", kOnOff}}},
    {"damage", "Damage multiplier", "Link takes more or less damage than the game deals.",
     {{"rate", "Damage taken", {"x2", "x4", "One hit kills", "x0.5", "None"}}}},
    {"rupees", "Rupee multiplier", "Rupees Link picks up or is given count more (up to what the wallet holds).",
     {{"rate", "Rupees", {"x2", "x3", "x5", "x10"}}}},
};

constexpr uint32_t kSave = 0x10145348, kPlayer = 0x1014B5B0;

std::mutex g_mutex;
json::Value g_state;  // the active profile's built-in state (packages.h)
uint64_t g_revision = ~0ull;

const Option* find_option(const std::string& mod, const std::string& option) {
    for (const auto& m : kCatalogue)
        if (m.id == mod)
            for (const auto& o : m.options)
                if (o.id == option) return &o;
    return nullptr;
}
bool save_state(const std::string& mod, const json::Value& v, std::string& error) {
    if (!packages::set_builtin(mod, v, error)) return false;
    std::lock_guard guard(g_mutex);
    g_state[mod] = v;
    return true;
}
}  // namespace

const std::vector<Mod>& catalogue() { return kCatalogue; }

bool on(const std::string& mod) {
    std::lock_guard guard(g_mutex);
    const auto& v = g_state.get(mod).get("on");
    return v.type == json::Value::Bool && v.boolean;
}

int choice(const std::string& mod, const std::string& option) {
    const Option* o = find_option(mod, option);
    std::lock_guard guard(g_mutex);
    const auto& v = g_state.get(mod).get(option);
    const int i = v.type == json::Value::Number ? (int)v.number : 0;
    return o && i >= 0 && i < (int)o->choices.size() ? i : 0;
}

bool set_on(const std::string& mod, bool enabled, std::string& error) {
    json::Value v;
    {
        std::lock_guard guard(g_mutex);
        v = g_state.get(mod);
    }
    v["on"] = enabled;
    return save_state(mod, v, error);
}

bool set_choice(const std::string& mod, const std::string& option, int index, std::string& error) {
    const Option* o = find_option(mod, option);
    if (!o || index < 0 || index >= (int)o->choices.size()) return error = "Invalid choice", false;
    json::Value v;
    {
        std::lock_guard guard(g_mutex);
        v = g_state.get(mod);
    }
    v[option] = index;
    return save_state(mod, v, error);
}

void step() {
    if (const uint64_t r = packages::revision(); r != g_revision) {  // a profile switched or saved
        g_revision = r;
        json::Value s = packages::builtin_state();
        std::lock_guard guard(g_mutex);
        g_state = std::move(s);
    }
    // what the save held after the last step, while the same Link was in play
    static uint32_t link = 0;
    static uint16_t life = 0, rupees = 0, oil = 0;
    static uint8_t ammo[5] = {};  // arrows, the three bomb bags, slingshot seeds
    const uint32_t now = ld32(kPlayer);
    const bool fresh = now != link;
    link = now;
    if (!now) return;
    const auto u16 = [](uint32_t off) { return (uint16_t)ld16(kSave + off); };
    auto keep = [&](uint32_t off, uint8_t& last, bool enabled) {  // never decreases
        const uint8_t v = ld8(kSave + off);
        if (enabled && !fresh && v < last) st8(kSave + off, last);
        else last = v;
    };
    const bool ammo_on = on("infinite-ammo");
    keep(0xEC, ammo[0], ammo_on && choice("infinite-ammo", "arrows") == 0);
    for (int i = 0; i < 3; i++) keep(0xED + i, ammo[1 + i], ammo_on && choice("infinite-ammo", "bombs") == 0);
    keep(0xF4, ammo[4], ammo_on && choice("infinite-ammo", "seeds") == 0);
    if (const uint16_t v = u16(0x0A); ammo_on && choice("infinite-ammo", "oil") == 0 && !fresh && v < oil) st16(kSave + 0x0A, oil);
    else oil = v;

    const uint16_t max_life = u16(0x02), cur = u16(0x04);
    if (on("damage") && !fresh && cur < life) {  // also a hit that killed: None and x0.5 can save Link
        static const double kRate[] = {2, 4, 1e9, 0.5, 0};
        const double hit = life - cur, rate = kRate[choice("damage", "rate")];
        const long v = std::lround(life - hit * rate);
        st16(kSave + 0x04, (uint16_t)std::clamp<long>(rate > 0 && rate < 1 && v == life ? life - 1 : v, 0, max_life));
    }
    life = u16(0x04);

    const uint16_t money = u16(0x06);
    if (on("rupees") && !fresh && money > rupees) {
        static const int kRate[] = {2, 3, 5, 10}, kWallet[] = {300, 1000, 2000};
        const int wallet = kWallet[std::min<int>(ld8(kSave + 0x1B), 2)];
        st16(kSave + 0x06, (uint16_t)std::min<int>(rupees + (money - rupees) * kRate[choice("rupees", "rate")], wallet));
    }
    rupees = u16(0x06);
}
}  // namespace mods::builtin
