// Built-in mods. See builtin.h.
//
// The cheats read and write the live save data, dSv_player_c at 10145348 (the layout of the disc's QA saves; HD's
// status A is the decomp's with a u16 in front): max life +2, life +4, rupees +6, lantern oil +0x0A (u16, in units the
// game burns per step), wallet size +0x1B (300, 1,000 or 2,000 rupees); the item record at +0xEC: arrows, the three
// bomb bags, slingshot seeds at +0xF4. They act on what changed in a logic step, so the game's own rules (maxima,
// wallet, death) still apply, and start over whenever Link's actor changes (a file loaded, a stage entered).
#include "builtin.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include "../gx2/gx2.h"
#include "../input.h"
#include "../platform/keycodes.h"
#include "mod_json.h"
#include "packages.h"
#include "photo.h"
#include "posture.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

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
    {"fast-text", "Fast text", "Each page of a message shows at once instead of letter by letter.", {}},
    {"time", "Time of day", "Stops the clock, or keeps it at an hour of your choice (where time passes at all).",
     {{"time", "Time", {"Stopped", "Morning (7:00)", "Noon", "Evening (18:30)", "Night (23:00)"}}}},
    {"size", "Link's size", "Link (and the wolf) drawn smaller or bigger; the world still sees him at his usual size.",
     {{"size", "Size", {"Tiny", "Small", "Big", "Giant"}}}},
    {"timer", "Timer", "A timer at the top of the screen, counting the game's time since the mod was switched on "
     "(switch it off and on to reset it).", {}},
    {"turbo", "Turbo key", "Hold T on the keyboard to run the game as fast as this Mac can (text, travel, cutscenes).", {}},
    {"moon-jump", "Moon jump", "Link rises into the air while the left stick is held in (L3).", {}},
    {"warp", "Warp menu",
     "Lists the game's places here (the developers' map select) to warp to. Warping skips the story's order and "
     "can leave a file stuck: save first.",
     {}},
    {"hero-mode", "Hero Mode on this file", "The file in play follows Hero Mode's rules (double damage; the world mirrored "
     "from the next area unless Unmirrored Hero Mode is on). Saving while it's on keeps the file in Hero Mode.", {}},
    {"epona", "Endless spurs", "Epona's spurs (the carrots) never run out.", {}},
    {"unmirror", "Unmirrored Hero Mode", "Hero Mode keeps the world as in Normal Mode instead of mirroring it.", {}},
    {"photo-mode", "Photo mode", "While it's on the game holds still with no HUD, and the camera moves around what it "
     "looked at: the right stick (arrow keys) turns it, the left stick up and down (W, S) brings it closer or further.",
     {}},
    {"no-bars", "No cutscene bars", "Cutscenes fill the screen, without the black bars above and below.", {}},
    {"boss-health", "Boss health bar", "A boss's (or mini-boss's) health shows at the top once Link has hurt it.", {}},
    {"posture", "Posture combat",
     "Sekiro's posture on TP's sword fighting. Blocking with the shield fills Link's posture and a full bar breaks his "
     "guard; a block just after the shield goes up (lock on right before the hit) is a deflect, which fills the "
     "attacker's posture instead. Blows, deflects and shield bashes fill an enemy's posture; once it breaks, the next "
     "blow is a deathblow. Posture drains when the fighting pauses.",
     {{"window", "Deflect window", {"Normal (0.2 s)", "Generous (0.33 s)", "Strict (0.1 s)"}}}},
};

constexpr uint32_t kSave = 0x10145348, kPlayer = 0x1014B5B0;

std::mutex g_mutex;
std::atomic<double> g_timer{-1};
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
double timer() { return g_timer.load(std::memory_order_relaxed); }

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
    posture::step();
    photo::step();
    static uint64_t timer_start = 0;  // logic step + 1 when the timer was switched on
    if (!on("timer")) timer_start = 0;
    else if (!timer_start) timer_start = interp::logic_steps() + 1;
    g_timer = timer_start ? (double)(interp::logic_steps() + 1 - timer_start) / 30 : -1;
    bool keys[256];  // turbo: gx2's pacing off while T is held
    input::held_keys(keys);
    gx2::set_turbo(on("turbo") && keys[kVK_ANSI_T]);
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

    // moon jump: Link's current.pos.y (+0x4D4) lifted 60 at first, past where the ground pulls him back so that the
    // game lets him fall, then 25 a step with speed.y (+0x4FC) kept from falling
    static bool lifting = false;
    const bool lift = on("moon-jump") && (input::read().buttons & input::kStickL);
    if (lift) {
        stf32(now + 0x4D4, ldf32(now + 0x4D4) + (lifting ? 25 : 60));
        stf32(now + 0x4FC, std::max(0.0, ldf32(now + 0x4FC)));
    }
    lifting = lift;

    // Link's size: his model's (daAlink_c::mpLinkModel, +0x654) base scale (J3DModel +0x14), put back to 1 when off
    static bool sized = false;
    const uint32_t model = ld32(now + 0x654);
    if (model && (on("size") || sized)) {
        static const double kSize[] = {0.35, 0.65, 1.4, 2.0};
        sized = on("size");
        for (int i = 0; i < 3; i++) stf32(model + 0x14 + 4 * i, sized ? kSize[choice("size", "size")] : 1);
    }

    // Hero Mode on this file: the save's Hero Mode flag (+0x1D7), which the damage (02905130) reads as it goes and a
    // scene's start reads for mirroring; set while the mod is on, the file's own value put back when it is turned off
    static int hero = -1;  // the file's own flag, while the mod holds it
    if (on("hero-mode")) {
        if (hero < 0) hero = ld8(kSave + 0x1D7);
        st8(kSave + 0x1D7, 1);
    } else if (hero >= 0) {
        st8(kSave + 0x1D7, (uint8_t)hero);
        hero = -1;
    }

    // endless spurs: Epona (dComIfGp_getHorseActor, the player pointer after Link's; daHorse_c, process 0xEF in HD)
    // has her spurs refilled to 6 (m_lashCnt, +0x1704 in HD), as the game does when they recover
    if (const uint32_t horse = ld32(kPlayer + 4); on("epona") && horse && ld16(horse + 8) == 0xEF) st16(horse + 0x1704, 6);

    // the time of day, status B's float at +0x34 in degrees (15 an hour from midnight), which the environment reads,
    // advances and writes back each step (dScnKy_env_light_c::setDaytime)
    static double held = -1;  // where the stopped clock stands
    if (!on("time") || fresh) held = -1;
    if (const int c = choice("time", "time"); on("time") && c > 0) {
        static const double kHour[] = {0, 7, 12, 18.5, 23};
        stf32(kSave + 0x34, kHour[c] * 15);
        held = -1;
    } else if (on("time")) {
        if (held < 0) held = ldf32(kSave + 0x34);
        stf32(kSave + 0x34, held);
    }
}
}  // namespace mods::builtin

// Unmirrored Hero Mode, as Cemu's Disable Mirroring pack does it: 1012667D is 1 for the world drawn as is, and the two
// places that set it store 0 for Hero Mode (02941064 from r31, 029481AC from whether 1014551F is clear); the byte is
// set back to 1 right after each.
extern "C" void site_02941068(Cpu*) {
    if (mods::builtin::on("unmirror")) st8(0x1012667D, 1);
}
extern "C" void site_029481B0(Cpu*) {
    if (mods::builtin::on("unmirror")) st8(0x1012667D, 1);
}

// Fast text: a message starts as if with the instant tag (MSGTAG_INSTANT sets field_0xb2, at +0xB2 in HD too), so each
// page shows at once, as after pressing A. jmessage_tSequenceProcessor::do_begin (02A58584, r3 the processor).
extern "C" void f_02A58584_orig(Cpu* c);
extern "C" void hook_02A58584(Cpu* c) {
    const uint32_t processor = c->r[3];
    f_02A58584_orig(c);
    if (mods::builtin::on("fast-text")) st8(processor + 0xB2, 1);
}
