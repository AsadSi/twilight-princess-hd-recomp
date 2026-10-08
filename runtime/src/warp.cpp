// Test aid: WWHD_WARP=<frame>:<stage>,<point>,<room>,<layer> sends the game to that place at TV frame <frame>,
// and more warps can follow, separated by ';' (a later frame each; tools/warplist.py lists the developer map select's places from the disc). It calls the game's own
// dComIfGp_setNextStage (with its default speed, mode and wipe), as a door or a cutscene does; the play
// scene then changes stage. Story flags stay as they are, so a place may come up in another state than
// the map select's description says.
#include <cstdlib>
#include <cstring>

#include "gfx/renderer.h"
#include "runtime.h"

namespace warp {

// dComIfGp_setNextStage(stage, point, room, layer) with the remaining arguments defaulted (029049F4 is
// the full one); called this way by the reset to the title (02904CA0)
constexpr uint32_t kSetNextStage = 0x02904C60;

// main01's frame loop (interp.cpp), on the main thread; the volatile registers are free there
void service(Cpu* c) {
    static const char* e = getenv("WWHD_WARP");  // the next warp; null when none is left
    if (!e || !*e) return;
    char* p;
    uint64_t at = strtoull(e, &p, 10);
    if (render::frame_count() < at) return;
    const char* next = strchr(p, ';');
    e = next ? next + 1 : nullptr;
    char stage[8] = {};
    int point = 0, room = 0, layer = -1;
    if (*p != ':' || sscanf(p + 1, "%7[^,],%d,%d,%d", stage, &point, &room, &layer) < 1) {
        LOG("[warp] WWHD_WARP: expected <frame>:<stage>,<point>,<room>,<layer>");
        return;
    }
    static const uint32_t name = mem::runtime_alloc(8, 8);
    memcpy(mem::ptr(name), stage, 8);
    LOG("[warp] to %s point %d room %d layer %d", stage, point, room, layer);
    guest_call(c, kSetNextStage, {name, (uint32_t)point, (uint32_t)room, (uint32_t)layer});
}

}  // namespace warp
