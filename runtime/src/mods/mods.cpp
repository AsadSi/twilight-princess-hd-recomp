// Game time and the game's aiming state for the mouse as a gyro. See mods.h.
//
// The game aims when Link (dComIfGp_getPlayer(0), 1014B5B0) is in one of the decomp's first-person
// procedures: daAlink_c's mProcID (u16 at +0x305C in HD, +0x2FE8 in the decomp), numbered as the decomp's
// daAlink_PROC. Checked: looking around (R3, 0x13E) and the slingshot (0x5E, also with Options > Aim set to
// 3rd Person); the game turns both with the GamePad's motion.
#include "mods.h"

#include <cstdint>

#include "../motion/motion.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace mods {
double game_time() { return (double)interp::logic_steps() / 30.0; }

static bool first_person(uint16_t proc) {
    switch (proc) {
    case 0x05E: case 0x060: case 0x063:  // bow and slingshot, boomerang, dominion rod
    case 0x0BC: case 0x0C4: case 0x0D8:  // hawk, clawshot, ball and chain
    case 0x07D:                          // clawshot, swimming
    case 0x040: case 0x045: case 0x048: case 0x04A:  // on Epona: look, bow, boomerang, clawshot
    case 0x098: case 0x099: case 0x09C: case 0x09E:  // in the canoe: look, bow, boomerang, clawshot
    case 0x0AB: case 0x117:              // on a board, wolf on a rope: look
    case 0x13E: case 0x13F: case 0x140:  // look around: standing, swimming, peeping
        return true;
    default:
        return false;
    }
}

void update_aiming() {
    const uint32_t link = ld32(0x1014B5B0);
    motion::set_aiming(link >= mem::kMem2Start && link < mem::kMem2End && first_person(ld16(link + 0x305C)));
}
}  // namespace mods
