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
#include <cstring>
#include <mutex>

#include "../gx2/gx2.h"
#include "../input.h"
#include "../platform/keycodes.h"
#include "boss_rush.h"
#include "horror.h"
#include "mod_json.h"
#include "packages.h"
#include "photo.h"
#include "posture.h"
#include "runtime.h"
#include "../savestate.h"

namespace interp {
uint64_t logic_steps();
uint32_t fader();
}  // namespace interp

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
    {"tunic", "Tunic colours", "Link's green clothes (the tunic and cap) in another colour. Takes effect the next time "
     "the game starts.", {{"colour", "Colour", {"Red", "Blue", "Purple", "Yellow", "Black", "White"}}}},
    {"big-head", "Big head", "Link's head drawn bigger, as in Ocarina of Time's Big Head Mode (not the wolf's).",
     {{"size", "Size", {"Twice the size", "Three times"}}}},
    {"autosave", "Autosave", "The Quest Log in play is saved a few seconds into each new area, as the Save menu would "
     "save it.", {}},
    {"timer", "Timer", "A timer at the top of the screen, counting the game's time since the mod was switched on "
     "(switch it off and on to reset it).", {}},
    {"turbo", "Turbo key", "Hold T on the keyboard to run the game as fast as this Mac can (text, travel, cutscenes).", {}},
    {"quick-doors", "Quick doors and scenes", "Doors, and the fades between areas, play as fast as with the turbo key.", {}},
    {"skip-cutscenes", "Fast-forward cutscenes", "Hold + in a cutscene (or any scene the game plays out) to run it as "
     "fast as with the turbo key. One the game lets you skip still skips with a press of +.", {}},
    {"direct-camera", "Direct camera", "The right stick turns the camera in proportion to how far it is tilted (the game's "
     "turns at a third of full speed at half tilt), and either axis can be inverted.",
     {{"speed", "Speed", {"Normal", "Fast", "Slow"}}, {"x", "Left and right", {"Normal", "Inverted"}},
      {"y", "Up and down", {"Normal", "Inverted"}}}},
    {"quick-transform", "Quick transform", "Press G on the keyboard to change between Link and the wolf (it taps the "
     "GamePad screen's transform button, so it works where that button does).", {}},
    {"mouse-camera", "Mouse camera", "Hold the right mouse button on the game window and move the mouse to turn the "
     "camera.", {}},
    {"moon-jump", "Moon jump", "Link rises into the air while the left stick is held in (L3).", {}},
    {"fast-moves", "Faster rolls and climbing", "Link rolls, and climbs ladders and vine walls, half again as fast.", {}},
    {"great-spin", "Great spin", "Link's spin attack is always the great spin, at any health and before it is learned.", {}},
    {"boss-rush", "Boss rush",
     "Every dungeon boss in story order: Start warps to the first, and leaving a boss's room after the fight goes on to "
     "the next (a game over, to the same one). Play it on a file that has the items for them.",
     {}},
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
     "guard; a blow that lands just as Link shield bashes (ZR while locked on, C on the keyboard) is a parry, which "
     "fills the attacker's "
     "posture instead. Blows, parries and shield bashes fill an enemy's posture; once it breaks, the next blow is a "
     "deathblow. Posture drains when the fighting pauses.",
     {{"window", "Parry window", {"Normal (0.3 s)", "Generous (0.5 s)", "Strict (0.15 s)"}}}},
    {"horror", "Horror events",
     "Now and then, during normal play: a night in Hyrule Field turns into a Blood Night, with hordes until dawn; "
     "a giant Shadow Beast follows Link from area to area for a while; at dusk, twilight creeps over the field, bringing Shadow "
     "Keese and Shadow Beasts; stand still too long at night or in a dungeon and a shadow rises behind Link. During an "
     "event the screen tints, the music is turned down and a heartbeat sounds when something is near. Never in a boss "
     "room, and the shadow never twice in a row.",
     {{"often", "How often", {"Now and then", "Rare", "Often", "Always (to try them out)"}},
      {"blood", "Blood Night", kOnOff}, {"stalker", "Stalker", {"Giant Shadow Beast", "Shadow Beast", "Off"}},
      {"creep", "Twilight creep", kOnOff},
      {"shadow", "Shadow behind Link", kOnOff}}},
};

constexpr uint32_t kSave = 0x10145348, kPlayer = 0x1014B5B0, kEvent = 0x1014A6B0;  // kEvent: dEvt_control_c
constexpr float kFaster = 1.5f;  // faster rolls and climbing (the hooks at the end)
std::atomic<float> g_mouse_dx{0}, g_mouse_dy{0};  // mouse camera: pixels moved with the right button held, not yet used

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
// whether the actor with process id `id` is a door: a knob door, a shutter, a boss door or the spiral door (HD's
// process names are the decomp's + 1); found as fpcM_SearchByID does (fpcLyIt_AllJudge with fpcSch_JudgeByID)
bool door(Cpu* c, uint32_t id) {
    static const uint32_t slot = mem::host_alloc(4);
    if (id == 0xFFFFFFFF) return false;
    st32(slot, id);
    const uint32_t actor = guest_call(c, 0x02ACE114, {0x02AD990C, slot});
    const int name = actor ? ld16(actor + 8) : 0;
    return name == 0xE9 || (name >= 0x227 && name <= 0x22C) || name == 0x22E;
}
// the Quest Log in play saved as the Save menu saves it (dMenu_save_c::dataWrite 02A08494, dataSave 02A06360): the
// stage's memory put back (dSv_info_c::putSave 02AA856C, with the stage info's save table: the stage data's (+0x52C0)
// virtual at +0x174, then (+9 >> 1) & 0x1F), dSv_info_c::memory_to_card (02AA8ECC) into a buffer, its checksum (02AF3048)
// and the save storage manager's (10187E80) write request (02AF25B8) for the slot, dSv_info_c::mDataNum (10146700)
void autosave(Cpu* c) {
    static const uint32_t buf = mem::host_alloc(0xE00, 32);
    const uint32_t data = kSave + 0x52C0, info = guest_call(c, ld32(ld32(data + 4) + 0x174), {data});
    if (!info) return;
    guest_call(c, 0x02AA856C, {kSave, (uint32_t)(ld8(info + 9) >> 1) & 0x1F});
    guest_call(c, 0x02AA8ECC, {kSave, buf});
    guest_call(c, 0x02AF3048, {buf});
    guest_call(c, 0x02AF25B8, {0x10187E80, ld8(0x10146700), buf, 0xE00});
    ss::notify("Saved to Quest Log " + std::to_string(ld8(0x10146700) + 1));
}
}  // namespace

const std::vector<Mod>& catalogue() { return kCatalogue; }
void mouse_look(float dx, float dy) {
    if (!on("mouse-camera")) return;
    g_mouse_dx.fetch_add(dx);
    g_mouse_dy.fetch_add(dy);
}
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

void step(Cpu* c) {
    if (const uint64_t r = packages::revision(); r != g_revision) {  // a profile switched or saved
        g_revision = r;
        json::Value s = packages::builtin_state();
        std::lock_guard guard(g_mutex);
        g_state = std::move(s);
    }
    posture::step();
    photo::step();
    boss_rush::step();
    horror::step(c);
    static uint64_t timer_start = 0;  // logic step + 1 when the timer was switched on
    if (!on("timer")) timer_start = 0;
    else if (!timer_start) timer_start = interp::logic_steps() + 1;
    g_timer = timer_start ? (double)(interp::logic_steps() + 1 - timer_start) / 30 : -1;
    // turbo, gx2's pacing off: while T is held; while + is held in an event; for doors and scene changes, while an event
    // has a door among its two actors, Link walks through a door (daAlink_c::mProcID +0x305C, PROC_DOOR_OPEN 0xB0) or
    // the screen is dark or fading: the display's JUTFader (scene changes; status +0, 1 clear), mDoGph_gInf_c::mFade
    // (1012B3BE) or dDlst_list_c::mWipe (1011DAFC). The event: dEvt_control_c's mEventStatus (+0xE5), its actors'
    // process ids mPt1 (+0xC4) and mPt2 (+0xC8)
    bool keys[256];
    input::held_keys(keys);
    const bool event = ld8(kEvent + 0xE5) != 0;
    const uint32_t now = ld32(kPlayer), fader = interp::fader();
    bool fast = on("turbo") && keys[kVK_ANSI_T];
    fast |= on("skip-cutscenes") && event && (input::read().buttons & input::kPlus);
    fast |= on("quick-doors") && ((now && ld16(now + 0x305C) == 0xB0) || (fader && ld32(fader) != 1) || ld8(0x1012B3BE) ||
                                  ld8(0x1011DAFC) || (event && (door(c, ld32(kEvent + 0xC4)) || door(c, ld32(kEvent + 0xC8)))));
    gx2::set_turbo(fast);
    // autosave: once a new area (the stage name at 1014A5E8; not the title, F_SP102) has been in play for 3 s with no
    // event and the screen clear
    static char stage[8] = {};
    static int quiet = -1;  // steps the area has been quiet, -1 once saved
    if (memcmp(stage, mem::ptr(0x1014A5E8), 8)) memcpy(stage, mem::ptr(0x1014A5E8), 8), quiet = 0;
    if (on("autosave") && quiet >= 0 && now && !event && fader && ld32(fader) == 1 && strncmp(stage, "F_SP102", 8) &&
        ++quiet >= 90)
        quiet = -1, autosave(c);
    // quick transform: G taps the GamePad screen's transform button (top right, Link and the wolf) for a few steps
    static int tap = 0;  // steps left of the tap
    static bool g_key = false;
    if (on("quick-transform") && keys[kVK_ANSI_G] && !g_key) tap = 4, input::set_touch(true, 0.875f, 0.063f);
    else if (tap && !--tap) input::set_touch(false, 0.875f, 0.063f);
    g_key = keys[kVK_ANSI_G];
    // what the save held after the last step, while the same Link was in play
    static uint32_t link = 0;
    static uint16_t life = 0, rupees = 0, oil = 0;
    static uint8_t ammo[5] = {};  // arrows, the three bomb bags, slingshot seeds
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

// Direct camera: dCamera_c::updatePad (028D9360, r3 the camera) reads the right stick into mPadInfo.mCStick
// (mLastPosX +0x1D8, mLastPosY +0x1DC; full tilt is 0.9 in HD). The chase camera's turn for x, measured in HD (degrees a
// step, the yaw +0x2E): 0.1 at 0.15, 0.55 at 0.3, 1.65 at 0.52 (a third of full tilt's at half tilt), 3.1 at 0.72, 5.1 at
// 0.9, 7.3 at 1.2, less beyond. x becomes the value whose turn is in proportion to the tilt. The mouse camera turns it
// by the mouse's movement since the last step (0.25 degrees a pixel, up to the camera's fastest) the same way.
namespace {
double tilt_for(double degrees) {  // the x (>= 0) that turns the chase camera that many degrees a step
    static const double kX[] = {0.05, 0.15, 0.297, 0.517, 0.715, 0.9, 1.2}, kTurn[] = {0, 0.11, 0.55, 1.65, 3.08, 5.14, 7.26};
    degrees = std::clamp(degrees, 0.0, kTurn[6]);
    int i = 0;
    while (i < 5 && kTurn[i + 1] < degrees) i++;
    return kX[i] + (degrees - kTurn[i]) / (kTurn[i + 1] - kTurn[i]) * (kX[i + 1] - kX[i]);
}
}  // namespace
extern "C" void f_028D9360_orig(Cpu* c);
extern "C" void hook_028D9360(Cpu* c) {
    const uint32_t cam = c->r[3];
    f_028D9360_orig(c);
    using namespace mods::builtin;
    if (on("direct-camera")) {
        const double x = ldf32(cam + 0x1D8), ax = std::fabs(x), flip = choice("direct-camera", "x") ? -1 : 1;
        static const double kMax[] = {5.14, 7.2, 3.0};  // Normal (full tilt as in the game), Fast, Slow
        const double out = ax > 0.05 ? tilt_for(kMax[choice("direct-camera", "speed")] * std::min(1.0, (ax - 0.05) / 0.85)) : ax;
        stf32(cam + 0x1D8, std::copysign(out, x) * flip);
        if (choice("direct-camera", "y")) stf32(cam + 0x1DC, -ldf32(cam + 0x1DC));
    }
    const float dx = g_mouse_dx.exchange(0), dy = g_mouse_dy.exchange(0);
    if (on("mouse-camera") && (dx || dy)) {  // right is the stick's right (x below 0), up its up
        if (dx) stf32(cam + 0x1D8, -std::copysign(tilt_for(std::fabs(dx) * 0.25), dx));
        if (dy) stf32(cam + 0x1DC, std::clamp(-dy * 0.05f, -1.0f, 1.0f));
    }
}

// Big head: daAlink_c::setItemMatrix (0205E1D8, r3 Link) runs after his body model's calc and places the face and hat
// models (+0x658, +0x65C) on the head joint's matrix (4); that matrix (mpLinkModel +0x654: buffer +0x80, world
// matrices +0x0C) is scaled first, so the face, hair and hat grow from the neck. Kept from scaling twice when it is
// called again with no calc between.
extern "C" void f_0205E1D8_orig(Cpu* c);
extern "C" void hook_0205E1D8(Cpu* c) {
    using namespace mods::builtin;
    static float scaled[12];
    const uint32_t model = ld32(c->r[3] + 0x654), buf = model ? ld32(model + 0x80) : 0, anm = buf ? ld32(buf + 0x0C) : 0;
    if (on("big-head") && anm) {
        float m[12];
        for (int i = 0; i < 12; i++) m[i] = (float)ldf32(anm + 0xC0 + 4 * i);
        if (memcmp(m, scaled, sizeof m) != 0) {
            const float k = choice("big-head", "size") ? 3 : 2;
            for (int r = 0; r < 3; r++)
                for (int col = 0; col < 3; col++) m[r * 4 + col] *= k;
            for (int i = 0; i < 12; i++) stf32(anm + 0xC0 + 4 * i, m[i]);
            memcpy(scaled, m, sizeof m);
        }
    }
    f_0205E1D8_orig(c);
}

// Faster rolls and climbing: the animations Link starts (daAlink_c::setSingleAnime 0201F128: r3 Link, f1 the rate) in a
// roll (mProcID +0x305C, 0x0E), on a ladder (0x67-0x6B) or a vine wall (0x80-0x85), and the rate ladders and vine walls
// set each step for the stick (setWaterInAnmRate 0202BA48), run 1.5x; the roll's speed too (mNormalSpeed +0x3458, set by
// procFrontRollInit 0204D094 and kept until the roll can be cancelled), so it covers the same ground.
static bool fast_proc(Cpu* c) {
    const int proc = ld16(c->r[3] + 0x305C);
    return mods::builtin::on("fast-moves") && (proc == 0x0E || (proc >= 0x67 && proc <= 0x6B) || (proc >= 0x80 && proc <= 0x85));
}
extern "C" void f_0201F128_orig(Cpu* c);
extern "C" void hook_0201F128(Cpu* c) {
    if (fast_proc(c)) c->f[1].ps0 *= mods::builtin::kFaster;
    f_0201F128_orig(c);
}
extern "C" void f_0202BA48_orig(Cpu* c);
extern "C" void hook_0202BA48(Cpu* c) {
    if (fast_proc(c)) c->f[1].ps0 *= mods::builtin::kFaster;
    f_0202BA48_orig(c);
}
extern "C" void f_0204D094_orig(Cpu* c);
extern "C" void hook_0204D094(Cpu* c) {
    const uint32_t link = c->r[3];
    f_0204D094_orig(c);
    if (mods::builtin::on("fast-moves") && c->r[3]) stf32(link + 0x3458, ldf32(link + 0x3458) * mods::builtin::kFaster);
}

// Great spin: daAlink_c::checkCutLargeTurnState (0203ABD0, asked by the spin's start and the camera) holds the great
// spin back until it is learned (an event flag) and while Link's life is short of full; with the mod it says yes.
extern "C" void f_0203ABD0_orig(Cpu* c);
extern "C" void hook_0203ABD0(Cpu* c) {
    if (mods::builtin::on("great-spin")) c->r[3] = 1;
    else f_0203ABD0_orig(c);
}
