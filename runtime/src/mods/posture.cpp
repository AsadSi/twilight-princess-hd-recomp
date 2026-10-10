// Posture combat, the built-in mod "posture". See posture.h.
//
// Link: every blow on his raised shield fills his posture, and only a full bar breaks his guard. The game reacts to a
// blow on the shield with procGuardSlipInit (0206B164; r4 the blow's size, r5 Link's collider), or for a
// guard-breaking blow (a Darknut's swing) with procGuardBreakInit (0206AA24, from checkDamageAction at 0206CFFC with
// the size in r25 and the collider in r29); with the mod both slide Link back unless the bar is full. HD's own break on
// every fourth block (the count at +0x3045) is off. A blow on the shield within the parry window of a shield bash's
// start (Link's mProcID +0x305C becoming PROC_GUARD_ATTACK, 0x1E) is a parry: Link barely loses posture and steps back
// less (mNormalSpeed
// +0x3458), a metal clang rings (mZ2Link +0x2CDC, startCollisionSE 02C146F8, the Z2SE_HIT_METAL_WEAPON setGuardSe
// 0206A948 plays for a bomb), the scene stops for a moment (dScnPly_c's next pause timer, 10129B6B), and the
// attacker (the hitting collider's actor, 028FF5B8 on the collider's tg part at +0x9C) takes posture.
// Enemies: cc_at_check (02903E80), which most enemies call on a hit to take the attack's power (at info +0x1C) off
// their health (+0x566) and set how hard the hit lands (+0x1F: 1 strong, 2 lethal), gives Link's blows posture (more
// for a shield bash, hit type +0x20 = 16), and so does def_se_set (02903934), the clang of an enemy's guard taking a
// blow. A broken enemy reels as from a strong hit, and the next blow that hurts it is a deathblow: lethal, or triple
// damage for a boss. Posture drains after a pause in the fighting, an enemy's slower the more it is hurt.
#include "posture.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "builtin.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace mods::posture {
namespace {
constexpr uint32_t kPlayer = 0x1014B5B0, kNextPause = 0x10129B6B;
constexpr uint32_t kFlags = 0x57C, kGuard = 0x08000000, kBlocks = 0x3045, kSpeed = 0x3458, kHealth = 0x566, kName = 8;
constexpr float kMax = 100;
constexpr int kBroken = 150;  // steps a broken enemy stays open to a deathblow

struct Foe {
    float posture = 0;
    uint64_t last = 0, broken_until = 0;  // logic steps
    int health = 0;                        // when the fight began
};
std::unordered_map<uint32_t, Foe> g_foes;  // by actor, for the Link in play
uint32_t g_link = 0, g_foe = 0;            // g_foe: the enemy fought last
float g_posture = 0;
uint64_t g_last = 0, g_bash_since = 0, g_deflect = 0;
bool g_guarding = false, g_bashing = false;
uint32_t g_boss = 0, g_boss_link = 0;  // the Boss health bar mod: the boss hurt last, while that Link is in play
int g_boss_max = 1;                    // its health when first hurt
std::mutex g_hud_mutex;
Hud g_hud;

uint64_t now() { return interp::logic_steps(); }
bool active() { return builtin::on("posture") && ld32(kPlayer) == g_link && g_link; }
void hitstop(int steps) { st8(kNextPause, (uint8_t)std::max<int>((int8_t)ld8(kNextPause), steps)); }

bool boss(uint32_t actor) {  // the decomp's B_ actors; HD's process names are the decomp's + 1
    const int n = (int16_t)ld16(actor + kName) - 1;
    return n == 0x07D || n == 0x0D2 || n == 0x0F3 || (n >= 0x0F6 && n <= 0x0FA) || (n >= 0x20B && n <= 0x216) || n == 0x2F1;
}

// posture for an enemy; true when it breaks
bool hurt(uint32_t actor, float amount) {
    if (!actor) return false;
    Foe& f = g_foes[actor];
    if (!f.last) f.health = std::max<int>(1, (int16_t)ld16(actor + kHealth));
    f.last = now();
    g_foe = actor;
    if (f.broken_until) return false;
    f.posture = std::min(kMax, f.posture + amount);
    if (f.posture < kMax) return false;
    f.broken_until = now() + kBroken;
    hitstop(6);
    LOG("[posture] enemy %04X broken", ld16(actor + kName));
    return true;
}
}  // namespace

void step() {
    const uint32_t link = ld32(kPlayer);
    if (link != g_link || !builtin::on("posture")) {  // a stage entered or a file loaded: start over
        g_link = builtin::on("posture") ? link : 0;
        g_foes.clear();
        g_foe = 0, g_posture = 0, g_guarding = false, g_bashing = false, g_deflect = 0;
    }
    Hud h;
    if (g_link) {
        const bool up = ld32(link + kFlags) & kGuard, bash = ld16(link + 0x305C) == 0x1E;
        if (bash && !g_bashing) g_bash_since = now();  // a shield bash started
        g_guarding = up, g_bashing = bash;
        if (now() - g_last > 45) g_posture = std::max(0.0f, g_posture - (up ? 0.5f : 0.25f));  // 15/s guarding, 7.5/s
        for (auto it = g_foes.begin(); it != g_foes.end();) {
            Foe& f = it->second;
            if (now() - f.last > 900) {  // out of the fight for half a minute
                it = g_foes.erase(it);
                continue;
            }
            if (f.broken_until && now() >= f.broken_until) {  // the deathblow missed: it recovers some
                f.broken_until = 0, f.posture = kMax * 0.6f, f.last = now();
            } else if (!f.broken_until && now() - f.last > 90) {  // 7.5/s at full health
                const float healthy = std::clamp((float)(int16_t)ld16(it->first + kHealth) / f.health, 0.25f, 1.0f);
                f.posture = std::max(0.0f, f.posture - 0.25f * healthy);
            }
            ++it;
        }
        // a bar shows while it has posture, and fades out over half a second once empty (Link's) or once the
        // enemy has been left alone for 4 s
        const auto fade = [](float since, float after) { return std::clamp(1.0f - (since - after) / 15.0f, 0.0f, 1.0f); };
        h.link = g_posture / kMax;
        h.link_alpha = g_posture > 0 ? 1.0f : fade((float)(now() - g_last), 30);
        h.deflect = g_deflect ? std::clamp(1.0f - (now() - g_deflect) / 9.0f, 0.0f, 1.0f) : 0;
        if (auto it = g_foes.find(g_foe); it != g_foes.end()) {
            const Foe& f = it->second;
            h.foe = f.broken_until ? 1.0f : f.posture / kMax;
            h.foe_broken = f.broken_until != 0;
            h.foe_alpha = h.foe_broken ? 1.0f : fade((float)(now() - f.last), 120);
        }
    }
    if (!builtin::on("boss-health") || link != g_boss_link) g_boss = 0;
    if (const int health = g_boss ? (int16_t)ld16(g_boss + kHealth) : 0; health > 0)
        h.boss = std::min(1.0f, (float)health / g_boss_max), h.boss_alpha = 1;
    std::lock_guard guard(g_hud_mutex);
    g_hud = h;
}

Hud hud() {
    std::lock_guard guard(g_hud_mutex);
    return g_hud;
}
}  // namespace mods::posture

using namespace mods::posture;

extern "C" void f_0206B164_orig(Cpu* c);
extern "C" void f_0206AA24_orig(Cpu* c);
namespace {
// A blow on Link's raised shield, in place of the game's reaction (r3 Link): procGuardSlipInit(size, collider), or
// procGuardBreakInit for a guard-breaking blow (`breaker`), which here only a full bar gets
void block(Cpu* c, uint32_t spl, uint32_t collider, bool breaker) {
    const uint32_t link = c->r[3];
    const auto slip = [&] {
        c->r[3] = link, c->r[4] = spl, c->r[5] = collider;
        f_0206B164_orig(c);
    };
    st8(link + kBlocks, 0);
    static const uint64_t kWindow[] = {9, 15, 5};  // steps: 0.3 s, 0.5 s, 0.15 s
    const bool deflect = g_bash_since && now() - g_bash_since <= kWindow[mods::builtin::choice("posture", "window")];
    const bool huge = spl == 2 || spl == 7 || spl == 11 || spl == 14, large = spl == 1 || spl == 6 || spl == 10 || spl == 13;
    g_last = now();
    if (deflect) {
        const uint32_t attacker = collider ? guest_call(c, 0x028FF5B8, {collider + 0x9C}) : 0;
        g_posture = std::min(kMax - 1, g_posture + 4);
        g_deflect = now();
        LOG("[posture] parry");
        hurt(attacker, breaker || huge ? 40 : 30);
        hitstop(3);
        guest_call(c, 0x02C146F8, {link + 0x2CDC, 0x4000B, 0x28});  // mZ2Link.startCollisionSE: a metal clang
        slip();
        stf32(link + kSpeed, ldf32(link + kSpeed) * 0.35);
        return;
    }
    g_posture += breaker ? 50 : huge ? 45 : large ? 30 : 18;
    if (g_posture < kMax) return slip();
    g_posture = 0;
    c->r[3] = link;
    f_0206AA24_orig(c);
}
}  // namespace

extern "C" void hook_0206B164(Cpu* c) {  // daAlink_c::procGuardSlipInit(at_spl, collider): a blow on the shield
    if (!active() || c->r[3] != g_link || ld16(c->r[3] + 0x305C) == 0x1D) return f_0206B164_orig(c);  // or sliding
    block(c, c->r[4], c->r[5], false);
}

// daAlink_c::procGuardBreakInit. checkDamageAction calls it at 0206CFFC for a guard-breaking blow (size 9 to 11 in
// r25, Link's collider in r29), which with the mod is a block like any other while the shield is up.
extern "C" void hook_0206AA24(Cpu* c) {
    if (!active() || c->r[3] != g_link || c->lr != 0x0206D000 || !g_guarding || c->r[25] - 9 > 2 || !c->r[29])
        return f_0206AA24_orig(c);
    block(c, c->r[25], c->r[29], true);
}

// def_se_set(sound, collider, map info, actor) 02903934: an enemy's guard took a blow (the Darknut's shield, the
// Lizalfos's, a Stalfos's, ...). Most pass no actor; their sound's position (+0, HD keeps Z2Creature's vtable last) is
// the actor's current.pos (+0x4D0). Enemies are group 2 (+0x496).
extern "C" void f_02903934_orig(Cpu* c);
extern "C" void hook_02903934(Cpu* c) {
    uint32_t actor = c->r[6];
    if (!actor && c->r[3]) actor = ld32(c->r[3]) - 0x4D0;
    if (active() && actor >= mem::kMem2Start && actor < mem::kMem2End && ld8(actor + 0x496) == 2) hurt(actor, 12);
    f_02903934_orig(c);
}

extern "C" void f_02903E80_orig(Cpu* c);
extern "C" void hook_02903E80(Cpu* c) {  // cc_at_check(enemy, at info): an enemy hit
    const uint32_t enemy = c->r[3], info = c->r[4];
    const int before = (int16_t)ld16(enemy + kHealth);
    f_02903E80_orig(c);
    const int power = ld16(info + 0x1C), health = (int16_t)ld16(enemy + kHealth);
    if (health < before && mods::builtin::on("boss-health") && boss(enemy)) {  // a boss hurt: its bar, from this health
        if (enemy != g_boss) g_boss = enemy, g_boss_max = 1, g_boss_link = ld32(kPlayer);
        g_boss_max = std::max(g_boss_max, before);
    }
    if (!active() || c->r[3] != g_link) return;  // only Link's own blows (not arrows, bombs, the boomerang)
    auto it = g_foes.find(enemy);
    if (it != g_foes.end() && it->second.broken_until) {
        if (!power || health <= 0) return;
        const bool big = boss(enemy);
        st16(enemy + kHealth, (uint16_t)(big ? std::max(0, health - 2 * power) : 0));
        if (!big || health <= 2 * power) st8(info + 0x1F, 2);
        hitstop(10);
        LOG("[posture] deathblow on %04X", ld16(enemy + kName));
        g_foes.erase(it);
        return;
    }
    if (health <= 0) return void(g_foes.erase(enemy));
    const float gain = ld8(info + 0x20) == 16 ? 25 : power ? 6 + std::min(power, 40) * 0.5f : 0;
    if (gain > 0 && hurt(enemy, gain)) st8(info + 0x1F, std::max<int>((int8_t)ld8(info + 0x1F), 1));
}
