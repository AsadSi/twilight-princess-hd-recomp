// Horror events, the built-in mod "horror" (horror.h). A director gives each event a chance at a natural moment and
// keeps a cooldown between them, one event at a time, none in a boss room (a D_MN stage with a letter after its
// number) and the jump scare never twice in a row:
// - Blood Night: nightfall (19:00) in Hyrule Field (F_SP121-123). The screen turns red, and Stalhounds, Bulblins and
//   Stalfos keep coming out around Link until dawn (6:00) or until he leaves the field.
// - Twilight creep: dusk (17:30) in the field. An amber dark deepens into the night, Shadow Keese and Shadow Beasts
//   drop in from the sky; it lifts at 20:00.
// - Stalker: a new area. A Shadow Beast comes up behind Link, keeps behind him (put back within reach when he gets far
//   away), follows him into the next area, and leaves after a few minutes once it is out of sight.
// - Shadow: standing still for 8 s at night or in a dungeon. A Shadow Beast rises right behind Link.
// During an event the music is turned down and a heartbeat sounds when one of its enemies is near.
// Enemies are made with fopAcM_create (02ACE764: process, parameter, position, room, angle, scale, argument) in
// Link's process layer (base_process_class +0x2C, set at 1012A1B8 for the call: made in the root layer they are drawn
// outside the play scene's draw lists, as the WW recomp found), with the parameter bytes at 0xFF that each actor
// takes as its defaults (no switch), found again by process id as fpcM_SearchByID does (02ACE114 with 02AD990C) and
// taken away with fopAcM_delete by id (02ACE698). HD's process names are the decomp's + 1.
// The music is the sequence manager's (JASGlobalInstance<Z2SeqMgr> at 1012AFF4) mAllBgmMaster fader (+0x74:
// current, step, target, frames left), moved by Z2SoundFader::move (02C15818: f1 the level, r4 the frames), as
// bgmAllMute does for a scene change. The heartbeat is Z2SE_AL_D04_HEARTBEAT (0x20090), a looping sound, started
// on Link's creature (mZ2Link +0x2CDC, Z2CreatureLink::startCollisionSE 02C146F8, which starts any sound id) for each
// beat and cut. The tint is
// bloom_c's mono colour (look.cpp's hook on bloom_c::draw): the frame's red channel times the colour, blended by its
// alpha, which the environment sets every frame.
// Test aid: WWHD_HORROR=<blood|stalker|creep|shadow>@<frame> starts that event at TV frame <frame> whatever the
// chances and the moment, and spawn:<hex process>@<frame> makes one of that enemy in front of Link; several follow
// each other separated by ';'.
#include "horror.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "../gfx/renderer.h"
#include "builtin.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace mods::horror {
namespace {
constexpr uint32_t kSave = 0x10145348, kPlayer = 0x1014B5B0, kEvent = 0x1014A6B0, kStage = 0x1014A5E8, kLayer = 0x1012A1B8,
                   kSeqMgr = 0x1012AFF4;
constexpr uint32_t kPos = 0x4D0, kRoom = 0x4E2, kYaw = 0x4E6;

enum Kind { kNone, kBlood, kStalker, kCreep, kShadow };
const char* const kName[] = {"", "blood", "stalker", "creep", "shadow"};  // the mod's options too

// an enemy: process, parameter, angle.z (a switch or range byte for some; 0xFF is none)
struct Enemy {
    int proc;
    uint32_t param;
    uint16_t angle_z;
};
// E_SH (chase range 0xFF: unlimited), E_RD (type 0 with a club, no scene, no switch; angle.z: attack range unlimited,
// no switch), E_SF (wait type and switch none), E_S1 (no group, search range 20 x 100, no path or switch; the low
// nibble 1 drops it from the sky), E_YK (no switch or path)
constexpr Enemy kStalhound{0x1BA, 0xFFFFFF00, 0}, kBulblin{0x1D5, 0xFFFF0000, 0xFFFF}, kStalfos{0x1B9, 0xFFFFFFFF, 0xFF},
    kShadowBeast{0x1B5, 0xFFFF14F0, 0}, kShadowBeastSky{0x1B5, 0xFFFF14F1, 0}, kShadowKeese{0x1FA, 0xFFFFFFFF, 0};

struct Spawned {
    uint32_t id;
    uint64_t born;  // logic step
};
std::vector<Spawned> g_out;  // the event's enemies
Kind g_kind = kNone, g_last = kNone;
uint64_t g_started = 0, g_until = 0, g_cool_until = 0, g_beat = 0, g_next = 0;  // logic steps
uint32_t g_stalker = 0;  // the stalker's id
uint64_t g_stalker_born = 0;
bool g_stalker_grown = false;  // the giant's health is set
float g_near = 0;        // how near the nearest enemy is, 1 at Link to 0 at 1800 away
std::atomic<uint32_t> g_tint{0};
std::minstd_rand g_random{std::random_device{}()};

uint64_t now() { return interp::logic_steps(); }
bool chance(int percent) { return (int)(g_random() % 100) < percent; }
float frand(float lo, float hi) { return lo + (hi - lo) * (g_random() % 1000) / 1000.0f; }
double daytime() { return ldf32(kSave + 0x34); }  // degrees, 15 an hour
bool night(double t) { return t >= 285 || t < 90; }
const char* stage() { return (const char*)mem::ptr(kStage); }
bool field(const char* s) { return !strncmp(s, "F_SP12", 6); }
bool dungeon(const char* s) { return !strncmp(s, "D_MN", 4) && s[6] == 0; }
bool boss_room(const char* s) { return !strncmp(s, "D_MN", 4) && s[6] != 0; }
int often() { return builtin::choice("horror", "often"); }  // Now and then, Rare, Often, Always

uint32_t actor(Cpu* c, uint32_t id) {  // the actor with that process id, or 0
    static const uint32_t slot = mem::host_alloc(4);
    st32(slot, id);
    return guest_call(c, 0x02ACE114, {0x02AD990C, slot});
}
float distance(uint32_t a, uint32_t link) {
    float d = 0;
    for (int i = 0; i < 3; i++) {
        const float v = (float)(ldf32(a + kPos + 4 * i) - ldf32(link + kPos + 4 * i));
        d += v * v;
    }
    return std::sqrt(d);
}
// an enemy at `dist` from Link in the direction `angle` (radians, the world's yaw; Link faces his +0x4E6), `dy` above
// him, facing him
uint32_t spawn(Cpu* c, uint32_t link, const Enemy& e, float dist, double angle, float dy, float scale = 1) {
    static const uint32_t buf = mem::runtime_alloc(0x30);  // position (cXyz) at +0, angle (csXyz) at +0x10, scale at +0x20
    const float x = (float)ldf32(link + kPos) + (float)std::sin(angle) * dist, y = (float)ldf32(link + kPos + 4) + dy,
                z = (float)ldf32(link + kPos + 8) + (float)std::cos(angle) * dist;
    stf32(buf, x), stf32(buf + 4, y), stf32(buf + 8, z);
    st16(buf + 0x10, 0), st16(buf + 0x12, (uint16_t)(int16_t)((angle + M_PI) * 32768 / M_PI)), st16(buf + 0x14, e.angle_z);
    for (int i = 0; i < 3; i++) stf32(buf + 0x20 + 4 * i, scale);
    const uint32_t layer = ld32(kLayer);
    st32(kLayer, ld32(link + 0x2C));
    const uint32_t id = guest_call(c, 0x02ACE764, {(uint32_t)e.proc, e.param, buf, (uint32_t)(int8_t)ld8(link + kRoom), buf + 0x10, buf + 0x20, 0xFFFFFFFF});
    st32(kLayer, layer);
    LOG("[horror] %03X id %08X at %.0f %.0f %.0f", e.proc, id, x, y, z);
    if (id != 0xFFFFFFFF) g_out.push_back({id, now()});
    return id;
}
double around(uint32_t link, bool behind) {  // a direction: behind Link (within 50 degrees of it) or any
    const double yaw = (int16_t)ld16(link + kYaw) * M_PI / 32768;
    return behind ? yaw + M_PI + frand(-0.45f, 0.45f) : frand(0, (float)(2 * M_PI));
}
// the stalker: a giant Shadow Beast (its model twice the size through the actor's scale, which daE_S1 multiplies into
// its own; its health, 50 when made, tripled once it exists; its hit spheres follow the bigger body's joints but keep
// their size) or an ordinary one, by the Stalker option
bool giant() { return builtin::choice("horror", "stalker") == 0; }
uint32_t spawn_stalker(Cpu* c, uint32_t link) {
    g_stalker_born = now(), g_stalker_grown = false;
    return spawn(c, link, kShadowBeast, 1500, around(link, true), 150, giant() ? 2.0f : 1.0f);
}
// forgets the enemies that are gone (not found 5 s after their creation); the nearest one's distance to Link
float prune(Cpu* c, uint32_t link) {
    float nearest = 1e9f;
    std::erase_if(g_out, [&](const Spawned& s) {
        const uint32_t a = actor(c, s.id);
        if (a) nearest = std::min(nearest, distance(a, link));
        return !a && now() - s.born > 150;
    });
    return nearest;
}
void remove_all(Cpu* c) {
    for (const Spawned& s : g_out) guest_call(c, 0x02ACE698, {s.id});
    g_out.clear();
}
// the music's master fader toward `level` over half a second, unless it is there or moving (the game's own scene
// fades are left alone)
void music(Cpu* c, float level) {
    const uint32_t mgr = ld32(kSeqMgr);
    if (!mgr) return;
    const uint32_t fader = mgr + 0x74;
    if (ld32(fader + 0xC) || std::fabs((float)ldf32(fader) - level) < 0.02f) return;
    LOG("[horror] music to %.2f", level);
    c->f[1].ps0 = level;
    guest_call(c, 0x02C15818, {fader, 15});
}
// the heartbeat sound loops: one beat is started, and cut after two thirds of a second (its handle through
// Z2SoundObjBase::getHandleSoundID 02C28004 on the creature's sound object, mZ2Link +4; JAISound::stop 02B06010)
uint64_t g_beat_cut = 0;
void heartbeat(Cpu* c, uint32_t link, bool start) {
    static const uint32_t id = mem::host_alloc(4);
    if (start) return void(guest_call(c, 0x02C146F8, {link + 0x2CDC, 0x20090, 0}));
    st32(id, 0x20090);
    const uint32_t handle = guest_call(c, 0x02C28004, {link + 0x2CDC + 4, id}), sound = handle ? ld32(handle) : 0;
    if (sound) guest_call(c, 0x02B06010, {sound, 0});
}

void begin(Cpu* c, uint32_t link, Kind kind) {
    g_kind = kind, g_started = now(), g_until = 0, g_next = 0;
    LOG("[horror] %s begins (%s, %.1f h)", kName[kind], stage(), daytime() / 15);
    switch (kind) {
    case kStalker:
        g_stalker = spawn_stalker(c, link);
        g_until = now() + 30 * 180;
        break;
    case kShadow:
        spawn(c, link, kShadowBeast, 250, around(link, true), 0);
        g_until = now() + 30 * 20;
        g_beat = 0;
        break;
    default: break;
    }
}
void end(Cpu* c, bool remove) {
    LOG("[horror] %s ends", kName[g_kind]);
    if (const uint32_t link = ld32(kPlayer)) heartbeat(c, link, false);
    g_beat_cut = 0;
    if (remove) remove_all(c);
    g_out.clear();
    static const int kCooldown[] = {30 * 240, 30 * 600, 30 * 90, 30 * 10};
    g_last = g_kind, g_kind = kNone, g_stalker = 0, g_near = 0;
    g_cool_until = now() + kCooldown[often()];
}
// may `kind` start now: its switch, the cooldown, the jump scare not twice in a row, and its chance at this moment
bool roll(Kind kind, const int percent[4]) {
    const int off = kind == kStalker ? 2 : 1;  // the Stalker option's third choice is Off
    if (g_kind != kNone || now() < g_cool_until || !builtin::on("horror") || builtin::choice("horror", kName[kind]) == off) return false;
    if (kind == kShadow && g_last == kShadow) return false;
    return chance(percent[often()]);
}

// WWHD_HORROR: forced events or spawns, each once its frame comes ("blood@5701;stalker@7201" for several in turn)
struct Forced {
    Kind kind = kNone;
    int proc = 0;
    uint64_t frame = 0;
};
const std::vector<Forced>& forced() {
    static const std::vector<Forced> list = [] {
        std::vector<Forced> out;
        for (const char* e = getenv("WWHD_HORROR"); e && *e; e = strchr(e, ';') ? strchr(e, ';') + 1 : nullptr) {
            Forced f;
            if (const char* at = strchr(e, '@')) f.frame = strtoull(at + 1, nullptr, 10);
            if (!strncmp(e, "spawn:", 6)) f.proc = (int)strtol(e + 6, nullptr, 16);
            else
                for (int k = kBlood; k <= kShadow; k++)
                    if (!strncmp(e, kName[k], strlen(kName[k]))) f.kind = (Kind)k;
            out.push_back(f);
        }
        return out;
    }();
    return list;
}
}  // namespace

Tint tint() {
    const uint32_t t = g_tint.load(std::memory_order_relaxed);
    return {(uint8_t)(t >> 24), (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t};
}

void step(Cpu* c) {
    static uint32_t link = 0;
    static char last_stage[8] = {};
    static double last_time = -1;
    static int quiet = 0, still = 0;  // steps the area has been in play; steps Link has stood still
    static float pos[3];
    const uint32_t now_link = ld32(kPlayer);
    const bool cutscene = ld8(kEvent + 0xE5) != 0, on = builtin::on("horror");
    const double t = daytime();
    const char* s = stage();
    const bool new_area = memcmp(last_stage, s, 8) != 0 || now_link != link;
    if (new_area) {
        memcpy(last_stage, s, 8), quiet = 0, still = 0, last_time = -1;
        g_out.clear();  // the game took the old area's actors with it
        if (g_kind != kNone && g_kind != kStalker) end(c, false);
    }
    link = now_link;
    if (!on && g_kind != kNone) end(c, true);
    if (!link || !on || boss_room(s) || !strncmp(s, "F_SP102", 7)) {  // no file in play, the title, a boss room
        if (g_kind != kNone && link) end(c, true);
        g_tint = 0;
        return;
    }
    quiet++;
    // Link standing still: his position unchanged
    float p[3];
    for (int i = 0; i < 3; i++) p[i] = (float)ldf32(link + kPos + 4 * i);
    still = !new_area && std::fabs(p[0] - pos[0]) < 0.5f && std::fabs(p[1] - pos[1]) < 0.5f && std::fabs(p[2] - pos[2]) < 0.5f ? still + 1 : 0;
    memcpy(pos, p, sizeof pos);

    // the moments, each with its chance by How often (Now and then, Rare, Often, Always)
    static const int kBlood_[] = {35, 15, 60, 100}, kCreep_[] = {30, 12, 55, 100}, kStalker_[] = {15, 6, 30, 100},
                     kShadow_[] = {35, 15, 60, 100};
    static size_t forced_next = 0;
    if (forced_next < forced().size() && render::frame_count() >= forced()[forced_next].frame && !cutscene) {
        const Forced f = forced()[forced_next++];
        if (f.proc) spawn(c, link, {f.proc, 0xFFFFFFFF, 0xFFFF}, 500, around(link, true) + M_PI, 100);
        else if (f.kind != kNone) {
            if (g_kind != kNone) end(c, true);
            begin(c, link, f.kind);
        }
    } else if (!cutscene && quiet > 90) {
        const bool nightfall = last_time >= 0 && last_time < 285 && t >= 285, dusk = last_time >= 0 && last_time < 262.5 && t >= 262.5;
        if (field(s) && nightfall && roll(kBlood, kBlood_)) begin(c, link, kBlood);
        else if (field(s) && dusk && roll(kCreep, kCreep_)) begin(c, link, kCreep);
        else if (quiet == 120 && g_kind == kNone && roll(kStalker, kStalker_)) begin(c, link, kStalker);
        else if (still == 240 && (night(t) || dungeon(s)) && roll(kShadow, kShadow_)) begin(c, link, kShadow);
        if (still >= 240) still = -60;  // another chance in 10 s
    }
    last_time = t;

    // the event in play
    uint32_t tint = 0;
    float music_level = 1;
    const float nearest = g_kind != kNone ? prune(c, link) : 1e9f;
    g_near = std::clamp(1 - nearest / 1800, 0.0f, 1.0f);
    const uint64_t age = now() - g_started;
    switch (g_kind) {
    case kBlood: {
        if (!night(t) || !field(s)) {
            end(c, true);
            break;
        }
        if (!cutscene && now() >= g_next && g_out.size() < 8) {  // a new one every 2 s while fewer than 8 are out
            const int r = (int)(g_random() % 10);
            spawn(c, link, r < 6 ? kStalhound : r < 9 ? kBulblin : kStalfos, frand(900, 1500), around(link, false), 150);
            g_next = now() + 60;
        }
        const float a = std::min(1.0f, age / 90.0f) * 160;
        tint = 0xFF282800 | (uint32_t)a;
        music_level = 0.25f;
        break;
    }
    case kCreep: {
        if (t >= 300 || t < 90 || !field(s)) {
            end(c, true);
            break;
        }
        // deepens from dusk to nightfall, lifts by 20:00
        const float depth = t < 285 ? std::clamp((float)(t - 262.5) / 22.5f, 0.0f, 1.0f) : 1 - (float)(t - 285) / 15;
        if (!cutscene && now() >= g_next && (int)g_out.size() < 2 + (int)(4 * depth)) {  // one every 3 s
            const bool keese = g_random() % 10 < 7;
            spawn(c, link, keese ? kShadowKeese : kShadowBeastSky, frand(600, 1300), around(link, false), keese ? 300 : 150);
            g_next = now() + 90;
        }
        tint = 0xFFAA4600 | (uint32_t)(150 * depth);
        music_level = 0.35f;
        break;
    }
    case kStalker: {
        const uint32_t beast = actor(c, g_stalker);
        static bool near = false;  // the music dims once it is near, until it is well away
        if (!beast && now() - g_stalker_born > 150 && quiet > 120 && !cutscene && now() >= g_next && now() < g_until) {  // gone, or a new area: it comes back
            g_stalker = spawn_stalker(c, link);
            g_next = now() + 300;
        }
        if (beast && !g_stalker_grown && giant()) st16(beast + 0x566, 150), g_stalker_grown = true, LOG("[horror] the giant has %d health", (int16_t)ld16(beast + 0x566));
        if (beast && distance(beast, link) > 3500 && !cutscene) {  // left behind: put back behind Link
            const double a = around(link, true);
            stf32(beast + kPos, ldf32(link + kPos) + std::sin(a) * 1800), stf32(beast + kPos + 4, ldf32(link + kPos + 4) + 100),
                stf32(beast + kPos + 8, ldf32(link + kPos + 8) + std::cos(a) * 1800);
            LOG("[horror] the stalker is put back behind Link");
        }
        if (now() >= g_until && (!beast || distance(beast, link) > 2000 || now() >= g_until + 30 * 60)) {
            end(c, true);
            break;
        }
        near = g_near > (near ? 0.05f : 0.25f);
        tint = 0x8C96BE00 | (uint32_t)(110 * g_near);
        music_level = near ? 0.3f : 1.0f;
        break;
    }
    case kShadow: {
        if (now() >= g_until) {
            end(c, false);  // the shadow stays
            break;
        }
        tint = 0xFF141400 | (uint32_t)(200 * std::max(0.0f, 1 - age / 60.0f));
        music_level = 0.2f;
        break;
    }
    default: break;
    }
    g_tint = tint;
    // a heartbeat while something is near, faster the nearer; the shadow's first seconds pound
    if (g_kind != kNone && !cutscene) {
        const bool pound = g_kind == kShadow && age < 90;
        if ((g_near > 0.3f || pound) && now() >= g_beat) {
            heartbeat(c, link, true);
            g_beat = now() + (pound ? 15 : (uint64_t)(36 - 18 * g_near)), g_beat_cut = now() + (pound ? 10 : 20);
        }
    }
    if (g_beat_cut && now() >= g_beat_cut) heartbeat(c, link, false), g_beat_cut = 0;
    // the music, and put back once the event is over
    static bool dimmed = false;
    if (music_level < 1) music(c, music_level), dimmed = true;
    else if (dimmed) music(c, 1), dimmed = std::fabs((float)ldf32(ld32(kSeqMgr) + 0x74) - 1) > 0.02f;
}
}  // namespace mods::horror
