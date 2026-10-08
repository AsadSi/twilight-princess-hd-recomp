// Frame interpolation (interp.h): 60, 120 or 240 fps from TP HD's 30 logic steps per second.
//
// main01's loop (02AF0EDC) runs one game step per pass: pad read, frame begin, fapGm_Execute (the painter,
// then the process handlers: delete, priority, create, execute, draw), audio and the HD systems, present.
// With interpolation on, each logic pass is followed by N hold passes (1 at 60 fps, 3 at 120, 7 at 240): only
// frame begin, the painter, the draw handler and present run (the hooks below skip the rest), so each step is
// presented N+1 times. The painter draws the lists the previous pass built:
//   logic pass:     the game steps from S to S+1; draws blended at t = 1/(N+1)
//   hold pass k:    no logic; draws at t = (k+1)/(N+1); the last (k = N) draws S+1 exactly and records it as
//                   the next step's "before"
// Blended: the camera (camera_draw 028E13CC: the view's eye, center, up, fovy, bank) and every J3D model
// (J3DModel::viewCalc 02B5CF50: its world and envelope matrices). Particles, 2D and the rest move at 30 Hz.
// The addresses come from the decomp's structure (docs/brief.md, 60 fps groundwork).
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "aspect.h"
#include "interp.h"
#include "mods/packages.h"
#include "runtime.h"
#include "savestate.h"

namespace interp {

static int valid_fps(int f) { return f >= 240 ? 240 : f >= 120 ? 120 : 60; }
static const int g_env_fps = getenv("WWHD_INTERP_FPS") ? valid_fps(atoi(getenv("WWHD_INTERP_FPS"))) : 0;
static std::atomic<bool> g_on{getenv("WWHD_INTERP") ? atoi(getenv("WWHD_INTERP")) == 1 : g_env_fps != 0};
static std::atomic<int> g_fps{g_env_fps ? g_env_fps : 60};
bool g_hold = false;            // this pass only draws (main thread)
static int g_phase = 0, g_n = 0;    // this pass in its step: 0 logic, k = hold pass k of g_n
static uint64_t g_records = 0;      // record passes so far (the last hold pass of each step)

int mode() { return g_on ? 1 : 0; }
void set_mode(int m) {
    if (m == 2) LOG("[interp] true 60 is not available for TP HD");
    if (g_on.exchange(m == 1) != (m == 1)) LOG("[interp] frame interpolation %s", m == 1 ? "on" : "off");
}
int fps() { return g_fps; }
void set_fps(int f) { g_fps = valid_fps(f); }
void toggle_fps(int f) {
    if (g_on && fps() == valid_fps(f)) return set_mode(0);
    set_fps(f);
    set_mode(1);
}
int display_hz();
// the drawn rate: the chosen one, capped to the display (frames past its refresh rate never show)
int output_fps() {
    if (!g_on) return 30;
    const int hz = display_hz();
    return hz > 0 && hz < fps() ? valid_fps(hz) : fps();
}
int frames_per_step() { return output_fps() / 30; }
const char* mode_name() {
    static const char* const names[] = {"30 fps", "60 fps", "120 fps", "240 fps"};
    const int out = output_fps();
    return names[out == 240 ? 3 : out == 120 ? 2 : out == 60 ? 1 : 0];
}
bool paced_interpolation() { return false; }
void set_paced_interpolation(bool) {}
bool paced_interpolation_at(int) { return false; }
void set_paced_interpolation_at(int, bool) {}
float paced_drawn_share() { return -1; }

// the display: refresh rate of the TV window's screen (0 = unknown; WWHD_DISPLAY_HZ=n overrides)
static const int g_env_hz = getenv("WWHD_DISPLAY_HZ") ? atoi(getenv("WWHD_DISPLAY_HZ")) : -1;
static std::atomic<int> g_display_hz{0};
int display_hz() { return g_env_hz >= 0 ? g_env_hz : g_display_hz.load(std::memory_order_relaxed); }
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    if (g_display_hz.exchange(hz) != hz) LOG("[interp] display refresh rate %d Hz", hz);
}
void set_present_vsync(bool) {}

// gx2_core.cpp's virtual vsync: N+1 presents per step. At 60 fps the game's interval (2 vsyncs) is halved; at
// 120/240 fps the virtual vsync also ticks 2/4 times per vsync.
uint32_t effective_swap_interval(uint32_t game) { return g_on ? (game > 1 ? game / 2 : 1) : game; }
int vsync_rate() { return g_on ? frames_per_step() / 2 : 1; }

// logic steps so far (test scenarios and profiles run on game time): one per logic pass's swap
static std::atomic<uint64_t> g_steps{0};
uint64_t logic_steps() { return g_steps.load(std::memory_order_relaxed); }
void step_done() {  // GX2SwapScanBuffers, game thread
    if (!g_hold) mods::packages::frame(++g_steps);
}
// the blend fraction of this pass: 1 on the record pass and without interpolation
static float pass_t() { return g_on ? float(g_phase + 1) / float(g_n + 1) : 1.0f; }
static bool record_pass() { return g_hold && g_phase == g_n; }

// ---------------------------------------------------------------- camera
// view_class: fovy +0xD0, eye +0xD8, center +0xE4, up +0xF0, bank (s16) +0xFC
namespace {
constexpr uint32_t kFovy = 0xD0, kEye = 0xD8, kCenter = 0xE4, kUp = 0xF0, kBank = 0xFC;
struct Cam {
    float eye[3], center[3], up[3], fovy;
    int16_t bank;
};
Cam read_cam(uint32_t v) {
    Cam s;
    for (int i = 0; i < 3; i++) {
        s.eye[i] = (float)ldf32(v + kEye + 4 * i);
        s.center[i] = (float)ldf32(v + kCenter + 4 * i);
        s.up[i] = (float)ldf32(v + kUp + 4 * i);
    }
    s.fovy = (float)ldf32(v + kFovy);
    s.bank = (int16_t)ld16(v + kBank);
    return s;
}
void write_cam(uint32_t v, const Cam& s) {
    for (int i = 0; i < 3; i++) {
        stf32(v + kEye + 4 * i, s.eye[i]);
        stf32(v + kCenter + 4 * i, s.center[i]);
        stf32(v + kUp + 4 * i, s.up[i]);
    }
    stf32(v + kFovy, s.fovy);
    st16(v + kBank, (uint16_t)s.bank);
}
float dist(const float* a, const float* b) {
    float d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(d);
}
// Halfway between two steps. Cuts (doors, re-centring, cutscene shots) are not blended: a step far larger than
// the camera's recent motion, or larger than an absolute limit, or a turn of more than 45 degrees. Orbits keep
// the eye on the arc: its direction from the center and its distance blend separately.
struct CamPrev { Cam s{}; bool valid = false, cut = false; float last_step = 0; };
float lerp(float a, float b, float t) { return a + (b - a) * t; }
// the step's cut decision (logic pass); its hold passes reuse it
void decide_cut(CamPrev& p, const Cam& a, const Cam& b) {
    const float step = std::max(dist(a.eye, b.eye), dist(a.center, b.center)), prev = p.last_step;
    p.last_step = step;
    p.cut = step > 800 || std::fabs(a.fovy - b.fovy) > 20 || (step > 60 && step > 4 * prev + 20);
}
bool blend_cam(const CamPrev& p, const Cam& a, const Cam& b, float t, Cam& m) {
    if (p.cut) return false;
    float da[3], db[3], la = 0, lb = 0;
    for (int i = 0; i < 3; i++) {
        m.center[i] = lerp(a.center[i], b.center[i], t);
        da[i] = a.eye[i] - a.center[i];
        db[i] = b.eye[i] - b.center[i];
        la += da[i] * da[i];
        lb += db[i] * db[i];
    }
    la = std::sqrt(la);
    lb = std::sqrt(lb);
    if (la < 1e-3f || lb < 1e-3f) {
        for (int i = 0; i < 3; i++) m.eye[i] = lerp(a.eye[i], b.eye[i], t);
    } else {
        float cosang = 0, dir[3], ld = 0;
        for (int i = 0; i < 3; i++) cosang += da[i] / la * db[i] / lb;
        // more than 45 degrees, or the camera's collision pulling it in: a cut
        if (cosang < 0.7071f || std::fabs(la - lb) > 0.1f * std::max(la, lb)) return false;
        for (int i = 0; i < 3; i++) dir[i] = (1 - t) * da[i] / la + t * db[i] / lb, ld += dir[i] * dir[i];
        ld = std::sqrt(ld);
        for (int i = 0; i < 3; i++) m.eye[i] = m.center[i] + dir[i] / ld * lerp(la, lb, t);
    }
    // up: perpendicular to the blended view direction, so the in-between frame gets no extra roll
    float fwd[3], lf = 0, d = 0, lu = 0;
    for (int i = 0; i < 3; i++) m.up[i] = lerp(a.up[i], b.up[i], t), fwd[i] = m.center[i] - m.eye[i], lf += fwd[i] * fwd[i];
    lf = std::sqrt(lf);
    for (int i = 0; i < 3; i++) fwd[i] /= lf, d += m.up[i] * fwd[i];
    for (int i = 0; i < 3; i++) m.up[i] -= d * fwd[i], lu += m.up[i] * m.up[i];
    lu = std::sqrt(lu);
    for (int i = 0; i < 3; i++) m.up[i] = lu > 0.1f ? m.up[i] / lu : b.up[i];
    m.fovy = lerp(a.fovy, b.fovy, t);
    m.bank = (int16_t)(a.bank + (int16_t)std::lround((int16_t)(b.bank - a.bank) * t));  // the short way round
    return true;
}
std::unordered_map<uint32_t, CamPrev> g_cams;

// ---------------------------------------------------------------- models
// J3DModel: model data +0x04 (joint count +0x2C, envelope count +0x2E), matrix buffer +0x84 (world
// matrices +0x0C, envelope matrices +0x10); 3x4 row-major float matrices
struct ModelPrev {
    uint64_t pass = 0;  // the record pass that recorded it
    uint32_t anm = 0, wev = 0, n = 0, w = 0;
    std::vector<uint32_t> m;  // the guest words (big-endian floats) of the world, then the envelope matrices
};
std::unordered_map<uint32_t, ModelPrev> g_models;
std::vector<uint32_t> g_cur;

void read_words(uint32_t at, uint32_t count, uint32_t* out) { memcpy(out, mem::ptr(at), count * 4); }
float bf(uint32_t w) {
    w = __builtin_bswap32(w);
    float f;
    memcpy(&f, &w, 4);
    return f;
}
uint32_t fb(float f) {
    uint32_t w;
    memcpy(&w, &f, 4);
    return __builtin_bswap32(w);
}
// matrices blended from a to b at t into guest memory at `at`; false when a matrix moved too far (a teleport)
bool blend_into(uint32_t at, const uint32_t* a, const uint32_t* b, uint32_t mats, float t) {
    for (uint32_t k = 0; k < mats; k++)
        for (int j : {3, 7, 11})
            if (std::fabs(bf(a[k * 12 + j]) - bf(b[k * 12 + j])) > 300) return false;
    uint32_t* g = (uint32_t*)mem::ptr(at);
    for (uint32_t i = 0; i < mats * 12; i++) g[i] = fb(lerp(bf(a[i]), bf(b[i]), t));
    return true;
}
}  // namespace

}  // namespace interp

using interp::g_hold;

extern "C" {
void f_028E13CC_orig(Cpu* c);
void f_02B5CF50_orig(Cpu* c);
}

// camera_draw(camera_process_class*). The view's aspect comes from the game's screen aspect (1012B374, aspect.cpp)
// each draw: the camera copies it only when it is made (028D7578), so a changed aspect reaches the 3D view at once.
extern "C" void hook_028E13CC(Cpu* c) {
    using namespace interp;
    const uint32_t v = c->r[3];
    st32(v + 0xD4, ld32(0x1012B374));
    if (!g_on) return f_028E13CC_orig(c);
    CamPrev& p = g_cams[v];
    const Cam cur = read_cam(v);
    if (record_pass()) {  // exact: the next step's "before"
        p.s = cur;
        p.valid = true;
        return f_028E13CC_orig(c);
    }
    if (!p.valid) return f_028E13CC_orig(c);
    if (!g_hold) decide_cut(p, p.s, cur);
    Cam mid;
    const bool blended = blend_cam(p, p.s, cur, pass_t(), mid);
    if (blended) write_cam(v, mid);
    f_028E13CC_orig(c);
    if (blended) write_cam(v, cur);
}

// J3DModel::viewCalc(J3DModel*)
extern "C" void hook_02B5CF50(Cpu* c) {
    using namespace interp;
    const uint32_t model = c->r[3];
    if (!g_on) return f_02B5CF50_orig(c);
    const uint32_t data = ld32(model + 0x04), buf = ld32(model + 0x84);
    const uint32_t n = data ? ld16(data + 0x2C) : 0, w = data ? ld16(data + 0x2E) : 0;
    const uint32_t anm = buf ? ld32(buf + 0x0C) : 0, wev = buf ? ld32(buf + 0x10) : 0;
    if (!anm || n == 0 || n > 512 || w > 1024 || (w && !wev)) return f_02B5CF50_orig(c);
    ModelPrev& p = g_models[model];
    if (record_pass()) {
        f_02B5CF50_orig(c);
        p.pass = g_records;
        p.anm = anm, p.wev = wev, p.n = n, p.w = w;
        p.m.resize((n + w) * 12);
        read_words(anm, n * 12, p.m.data());
        if (w) read_words(wev, w * 12, p.m.data() + n * 12);
        return;
    }
    // blended: only from the step's "before" (the last record pass), with the same matrices
    if (p.pass != g_records || p.anm != anm || p.wev != wev || p.n != n || p.w != w) return f_02B5CF50_orig(c);
    g_cur.resize((n + w) * 12);
    read_words(anm, n * 12, g_cur.data());
    if (w) read_words(wev, w * 12, g_cur.data() + n * 12);
    const float t = pass_t();
    bool blended = blend_into(anm, p.m.data(), g_cur.data(), n, t);
    if (blended && w && !blend_into(wev, p.m.data() + n * 12, g_cur.data() + n * 12, w, t)) blended = false;
    if (!blended) {  // a teleport: draw the step exactly
        memcpy(mem::ptr(anm), g_cur.data(), n * 48);
        if (w) memcpy(mem::ptr(wev), g_cur.data() + n * 12, w * 48);
        return f_02B5CF50_orig(c);
    }
    f_02B5CF50_orig(c);
    memcpy(mem::ptr(anm), g_cur.data(), n * 48);
    if (w) memcpy(mem::ptr(wev), g_cur.data() + n * 12, w * 48);
}

// ---------------------------------------------------------------- the pass structure
// Hold passes skip the step's logic: in main01's loop, the pad read, audio and HD systems; in fpcM_Management,
// the delete, priority and create handlers (which report success), execute, the post callback (fapGm_After) and
// cCt_Counter; in the play scene's draw (dScnPly_Draw 02AC08B4), collision and BG moves, vibration, daSus,
// particle calc and the scene change request. Its inline frame counter (1028EBC8) is put back.
#define HOLD_SKIP(a)                                                                          \
    extern "C" void f_##a##_orig(Cpu* c);                                                       \
    extern "C" void hook_##a(Cpu* c) {                                                          \
        if (!g_hold) f_##a##_orig(c);                                                           \
    }
#define HOLD_SKIP_OK(a)                                                                       \
    extern "C" void f_##a##_orig(Cpu* c);                                                       \
    extern "C" void hook_##a(Cpu* c) {                                                          \
        if (g_hold) c->r[3] = 1;                                                                \
        else f_##a##_orig(c);                                                                   \
    }
HOLD_SKIP(02AF0A98)     // pad read
HOLD_SKIP(02AD9F9C)     // audio
HOLD_SKIP(02003ED8)
HOLD_SKIP(02AF09B8)
HOLD_SKIP(02AFA7A0)     // HD systems (object at 1012B65C)
HOLD_SKIP(02AD6EB0)     // fpcDt_Handler
HOLD_SKIP_OK(02AD9664)  // fpcPi_Handler
HOLD_SKIP_OK(02AD6C78)  // fpcCt_Handler
HOLD_SKIP(02AD752C)     // fpcEx_Handler
HOLD_SKIP(0290DD38)
HOLD_SKIP(02ACD0A8)     // fapGm_After
HOLD_SKIP(02BEF838)     // cCt_Counter
HOLD_SKIP(02902B4C)     // dComIfG_Ccsp()->Move()
HOLD_SKIP(028C2AB0)     // dComIfG_Bgsp().ClrMoveFlag()
HOLD_SKIP(02ACC800)     // vibration Run
HOLD_SKIP(0290CDAC)     // daSus_c::execute
HOLD_SKIP(028C2C00)     // dComIfG_Bgsp().Move()
HOLD_SKIP(02A9D6E4)     // particle calc 3D
HOLD_SKIP(02A9D41C)     // particle calc 2D
HOLD_SKIP_OK(02AD5B3C)  // fopScnM_ChangeReq

extern "C" void f_02AC08B4_orig(Cpu* c);
extern "C" void hook_02AC08B4(Cpu* c) {  // dScnPly_Draw
    if (!g_hold) return f_02AC08B4_orig(c);
    const uint32_t counter = ld32(0x1028EBC8);
    f_02AC08B4_orig(c);
    st32(0x1028EBC8, counter);
}

// main01's frame loop (02AF0EDC, top of each pass): the main thread's state is all in guest memory and
// its registers, so save states are taken and restored here (savestate.cpp); test warps are requested here.
// With interpolation on, each logic pass is followed by N hold passes.
namespace warp { void service(Cpu* c); }  // warp.cpp
extern "C" void site_02AF0EDC(Cpu* c) {
    using namespace interp;
    if (g_hold && g_phase == g_n) g_records++;  // the record pass just ended
    // the next pass of the step (after the logic pass: hold pass 1; after hold pass N: the next logic pass); a
    // change of rate starts over with a logic pass
    const int n = g_on ? frames_per_step() - 1 : 0;
    g_phase = n > 0 && n == g_n && g_phase < g_n ? g_phase + 1 : 0;
    g_n = n;
    g_hold = g_phase > 0;
    if (g_hold) return;
    if (g_models.size() > 4096) g_models.clear();  // models are freed with their actors: drop old records
    ss::service(c);
    warp::service(c);
    aspect::service(c);
}
