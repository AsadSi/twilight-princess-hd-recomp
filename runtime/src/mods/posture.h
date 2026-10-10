// Posture combat, the built-in mod "posture": Sekiro's posture on top of TP's own sword fighting (posture.cpp), and the
// "boss-health" mod's bar, which shares its hook.
#pragma once

namespace mods::posture {
void step();  // once per logic step, from builtin::step

// what the bars show (read by the overlay on the render thread): fill 0..1, how visible 0..1
struct Hud {
    float link = 0, link_alpha = 0, deflect = 0;  // deflect: a flash on Link's bar, 1 at a deflect fading to 0
    float foe = 0, foe_alpha = 0;
    bool foe_broken = false;  // the deathblow mark
    float boss = 0, boss_alpha = 0;  // the Boss health bar mod: the boss hurt last
};
Hud hud();
}  // namespace mods::posture
