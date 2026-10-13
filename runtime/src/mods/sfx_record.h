// Test aid: WWHD_SFX_RECORD=<hex id>[,<hex id>...] records the game's own sound effects one at a time (sfx_record.cpp).
#pragma once

struct Cpu;

namespace mods::sfx_record {
void step(Cpu* c);  // once per logic step, from builtin::step
}
