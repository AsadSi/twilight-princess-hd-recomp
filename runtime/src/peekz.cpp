// TP HD's depth peeks (dPeekZ_c, the GameCube's GXPeekZ; the sun's lens flare fades with them when
// something is in front of it). The game queues screen points, and once a frame peekData (029196A0)
// reads each point's depth. HD reads it from a CPU-linear view (tiling aperture) of the depth buffer
// converted to a texture (02BD5C50), which this renderer doesn't write back to guest memory. The hook
// answers each point from the latest GPU readback of that pixel instead (the far plane until there is
// one) and has the renderer read the points again (OP_PEEK_Z). An answer is kept by the result address
// that asked (the sun asks with the same ones every frame, from points that move a little), so it is a
// frame old, as HD's own aperture read is. It is written here, when that address asks again, never from
// the GPU's completion: an object that asked may be gone by then.
//
// dPeekZ_c: u8 count at +0, then cells of { s16 x, s16 y, u32* result } from +4, in the GameCube's
// 640x456 screen; 02BD5C50 maps them to the 1920x1080 depth buffer, mirrored in x when the byte at
// 1012C8E3 is set.
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gx2/gx2_cmd.h"
#include "runtime.h"

namespace peekz {
static std::mutex g_mu;
static std::unordered_map<uint32_t, uint32_t> g_depth;  // result address -> 24-bit depth (0xFFFFFF = far)

// GPU completion (metal_main.mm)
void publish(uint32_t dst, uint32_t depth) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_depth[dst] = depth;
}
}  // namespace peekz

extern "C" void hook_029196A0(Cpu* c) {
    const uint32_t obj = c->r[3], n = std::min<uint32_t>(ld8(obj), 64);
    const bool mirror = ld8(0x1012C8E3) != 0;
    std::vector<uint32_t> req{gx2::converted_depth()};
    {
        std::lock_guard<std::mutex> lk(peekz::g_mu);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t cell = obj + 4 + i * 8, dst = ld32(cell + 4);
            int x = std::clamp((int)((int16_t)ld16(cell) * 1920.0f / 640.0f + 0.5f), 0, 1919);
            int y = std::clamp((int)((int16_t)ld16(cell + 2) * 1080.0f / 456.0f + 0.5f), 0, 1079);
            if (mirror) x = 1919 - x;
            if (!dst) continue;
            auto it = peekz::g_depth.find(dst);
            st32(dst, it != peekz::g_depth.end() ? it->second : 0xFFFFFF);
            req.insert(req.end(), {(uint32_t)x, (uint32_t)y, dst});
        }
    }
    st8(obj, 0);  // as the game's function: the cells are answered
    if (req[0] && req.size() > 1) gx2::emit(gx2::OP_PEEK_Z, req.data(), (uint32_t)req.size());
}
