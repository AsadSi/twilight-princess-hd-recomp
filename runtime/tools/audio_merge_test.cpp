// Host audio from the TV and GamePad mixes (runtime/src/audio_device_merge.h): the usual TV play is
// unchanged, Off-TV Play (everything on the GamePad) is heard, a sound on both devices is heard once,
// different sounds on each are both heard, and a switch between the devices neither drops nor doubles.
#include "audio_device_merge.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

constexpr int kN = 144;  // one 3 ms frame at 48 kHz
constexpr double kPi = 3.14159265358979323846;

using Signal = std::function<double(long)>;  // sample index -> value (s16 range)
static Signal sine(double hz, double amp) { return [=](long i) { return amp * std::sin(2 * kPi * hz * i / 48000.0); }; }
static Signal noise(double amp, uint32_t seed) {
    auto st = std::make_shared<std::vector<double>>();
    return [=](long i) {
        while ((long)st->size() <= i) {
            uint32_t x = seed + (uint32_t)st->size() * 2654435761u;
            x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12; x *= 0x297a2d39u; x ^= x >> 15;
            st->push_back(amp * ((double)x / 4294967295.0 * 2 - 1));
        }
        return (*st)[i];
    };
}
static Signal silence() { return [](long) { return 0.0; }; }
static Signal sum(Signal a, Signal b) { return [=](long i) { return a(i) + b(i); }; }
static Signal gain(Signal a, std::function<double(long)> g) { return [=](long i) { return a(i) * g(i); }; }
static Signal gain(Signal a, double g) { return [=](long i) { return a(i) * g; }; }

// runs `frames` frames (same signal on L and R, R slightly delayed for a stereo difference); the output
// samples of all frames
struct Run {
    std::vector<double> out_l, out_r;
    std::vector<float> k;
};
static Run run(audio::DeviceMerge& m, Signal tv, Signal drc, int frames) {
    Run r;
    for (int f = 0; f < frames; f++) {
        int32_t tl[kN], tr[kN], dl[kN], dr[kN], ol[kN], orr[kN];
        for (int i = 0; i < kN; i++) {
            long s = (long)f * kN + i;
            tl[i] = (int32_t)std::lrint(tv(s));
            tr[i] = (int32_t)std::lrint(tv(s + 3));
            dl[i] = (int32_t)std::lrint(drc(s));
            dr[i] = (int32_t)std::lrint(drc(s + 3));
        }
        m.merge(tl, tr, dl, dr, kN, ol, orr);
        for (int i = 0; i < kN; i++) { r.out_l.push_back(ol[i]); r.out_r.push_back(orr[i]); }
        r.k.push_back(m.shared_fraction());
    }
    return r;
}
// rms of (out - want) and of want over frames [from, to)
static double rms_err(const Run& r, Signal want, int from, int to) {
    double e = 0, n = 0;
    for (long s = (long)from * kN; s < (long)to * kN; s++) {
        double d = r.out_l[s] - want(s);
        e += d * d;
        n++;
    }
    return std::sqrt(e / n);
}
static double rms(Signal x, int from, int to) {
    double e = 0, n = 0;
    for (long s = (long)from * kN; s < (long)to * kN; s++) { e += x(s) * x(s); n++; }
    return std::sqrt(e / n);
}
static double rms_out(const Run& r, int from, int to) {
    double e = 0, n = 0;
    for (long s = (long)from * kN; s < (long)to * kN; s++) { e += r.out_l[s] * r.out_l[s]; n++; }
    return std::sqrt(e / n);
}

int main() {
    const int F = 400;  // 1.2 s
    Signal music = sine(220, 6000), chime = sine(1567, 4000), sfx = noise(3000, 7);
    {
        // TV play, nothing on the GamePad: exactly the TV mix
        audio::DeviceMerge m;
        Signal tv = sum(music, sfx);
        Run r = run(m, tv, silence(), F);
        for (long s = 0; s < (long)F * kN; s++) assert(r.out_l[s] == std::lrint(tv(s)));
        for (float k : r.k) assert(k == 0);
        printf("tv only: exact\n");
    }
    {
        // Off-TV Play: TV silent, everything on the GamePad -> the GamePad mix (issue #49)
        audio::DeviceMerge m;
        Signal drc = sum(music, sfx);
        Run r = run(m, silence(), drc, F);
        for (long s = 0; s < (long)F * kN; s++) assert(r.out_l[s] == std::lrint(drc(s)));
        printf("gamepad only: exact\n");
    }
    {
        // the same sounds on both at the same level ("both" output mode): once, not twice
        audio::DeviceMerge m;
        Signal x = sum(music, sfx);
        Run r = run(m, x, x, F);
        double e = rms_err(r, x, 100, F) / rms(x, 100, F);
        printf("same on both: error %.4f of the signal, k %.3f\n", e, r.k.back());
        assert(e < 0.01);
        // the first frames ramp in from the sum (k starts at 0) and settle within ~50 ms
        assert(rms_out(r, 0, 1) <= 2.01 * rms(x, 0, 1));
        assert(rms_err(r, x, 17, 30) / rms(x, 17, 30) < 0.05);
    }
    {
        // the same sound louder on the GamePad (0.5 TV, 1.0 GamePad) -> the louder one
        audio::DeviceMerge m;
        Signal x = sum(music, sfx);
        Run r = run(m, gain(x, 0.5), x, F);
        double e = rms_err(r, x, 100, F) / rms(x, 100, F);
        printf("same, louder on the GamePad: error %.4f\n", e);
        assert(e < 0.01);
        audio::DeviceMerge m2;
        Run r2 = run(m2, x, gain(x, 0.5), F);
        double e2 = rms_err(r2, x, 100, F) / rms(x, 100, F);
        printf("same, louder on the TV: error %.4f\n", e2);
        assert(e2 < 0.01);
    }
    {
        // different sounds on each (TV: music; GamePad: a chime and noise) -> both, at full level
        audio::DeviceMerge m;
        Signal drc = sum(chime, sfx);
        Run r = run(m, music, drc, F);
        Signal want = sum(music, drc);
        double e = rms_err(r, want, 0, F) / rms(want, 0, F);
        float kmax = 0;
        for (float k : r.k) kmax = std::max(kmax, k);
        printf("different sounds: error %.4f, k max %.3f\n", e, kmax);
        assert(e < 0.02 && kmax < 0.05);
    }
    {
        // TV play with a GamePad sound on top that also plays on the TV (PLAYER_DRC_GAME: TV at 0.6,
        // GamePad at 1) and music on the TV only: music once, the sound at its louder level
        audio::DeviceMerge m;
        Signal tv = sum(music, gain(chime, 0.6)), drc = chime;
        Run r = run(m, tv, drc, F);
        Signal ideal = sum(music, chime);
        Signal doubled = sum(music, gain(chime, 1.6));
        double e = rms_err(r, ideal, 100, F) / rms(ideal, 100, F);
        double ed = rms_err(r, doubled, 100, F) / rms(ideal, 100, F);
        printf("TV music + shared GamePad sound: error %.4f (to the plain sum %.4f)\n", e, ed);
        // the music dominates the correlation (rho 0.37): the signals count as different and are
        // added (for voices ax.cpp takes the shared part out exactly, mix_shared); never more than
        // the plain sum, never less than the ideal
        assert(rms_out(r, 100, F) <= rms(doubled, 100, F) * 1.001);
        assert(rms_out(r, 100, F) >= rms(ideal, 100, F) * 0.999);
        // the limit of estimating from two signals: the same music on both, louder on the GamePad,
        // plus a quiet different sound in the quieter (TV) signal. The quieter signal is taken as
        // carried by the louder one, so the extra sound is lost; the music is still not doubled.
        // (Voices never get here: ax.cpp dedups them from their gains. The final-mix additions this
        // is for are streams playing on both at two volumes, the exact cases above.)
        audio::DeviceMerge m2;
        Signal tv2 = sum(gain(music, 0.7), gain(chime, 0.3)), drc2 = music;
        Run r2 = run(m2, tv2, drc2, F);
        Signal ideal2 = sum(music, gain(chime, 0.3));
        double e2 = rms_err(r2, ideal2, 100, F) / rms(ideal2, 100, F);
        printf("shared music + quiet TV-only sound: error %.4f (the quiet sound)\n", e2);
        assert(rms_out(r2, 100, F) <= rms(ideal2, 100, F) * 1.01);
        assert(rms_err(r2, music, 100, F) / rms(music, 100, F) < 0.01);
    }
    {
        // the game switching to Off-TV Play: TV fades out and the GamePad in over 5 game frames
        // (83 ms); the same sounds throughout -> no gap, no doubling
        audio::DeviceMerge m;
        Signal x = sum(music, sfx);
        const double t0 = 150 * kN, len = 0.083 * 48000;
        auto up = [=](long s) { return std::clamp((s - t0) / len, 0.0, 1.0); };
        Run r = run(m, gain(x, [=](long s) { return 1 - up(s); }), gain(x, up), F);
        double worst_hi = 0, worst_lo = 10;
        for (int f = 100; f < F; f++) {
            double g = rms_out(r, f, f + 1) / rms(x, f, f + 1);
            worst_hi = std::max(worst_hi, g);
            worst_lo = std::min(worst_lo, g);
        }
        printf("switch to Off-TV Play: output level %.3f .. %.3f of the sound\n", worst_lo, worst_hi);
        assert(worst_hi < 1.12 && worst_lo > 0.6);
        assert(rms_err(r, x, 250, F) / rms(x, 250, F) < 0.01);
    }
    {
        // a quiet TV-only sound against loud, different GamePad sounds (Off-TV Play: controller
        // sounds stay on the TV): it is not cancelled by chance correlations
        audio::DeviceMerge m;
        Signal tv = gain(chime, 0.1), drc = sum(music, sfx);
        Run r = run(m, tv, drc, F);
        Signal want = sum(tv, drc);
        double e = rms_err(r, want, 0, F) / rms(tv, 0, F);
        printf("quiet TV sound under the GamePad mix: error %.4f of that sound\n", e);
        assert(e < 0.1);
    }
    {
        // voices: mixed into both devices at their gains (with ramps), the shared part taken out
        // once -> each voice at the larger gain, per sample
        struct G { uint16_t vol; int16_t delta; };
        const G cases[][2] = {{{0x8000, 0}, {0x8000, 0}}, {{0x4000, 0}, {0x8000, 0}}, {{0x8000, 0}, {0, 0}},
                              {{0, 0}, {0x6000, 0}}, {{0x8000, -100}, {0x2000, 120}}, {{0x3000, 40}, {0x3000, -40}}};
        float in[96];
        for (int i = 0; i < 96; i++) in[i] = (float)(sine(700, 9000)(i) * 256);
        for (auto& c : cases) {
            float tv[96] = {}, drc[96] = {}, shared[96] = {};
            float a = c[0].vol / 32768.0f, b = c[1].vol / 32768.0f;
            for (int i = 0; i < 96; i++) {
                a += c[0].delta / 32768.0f;
                b += c[1].delta / 32768.0f;
                tv[i] = in[i] * a;
                drc[i] = in[i] * b;
            }
            audio::mix_shared(in, shared, 96, c[0].vol, c[0].delta, c[1].vol, c[1].delta);
            a = c[0].vol / 32768.0f;
            b = c[1].vol / 32768.0f;
            for (int i = 0; i < 96; i++) {
                a += c[0].delta / 32768.0f;
                b += c[1].delta / 32768.0f;
                float want = in[i] * std::max(a, b);
                assert(std::fabs(tv[i] + drc[i] - shared[i] - want) <= 1e-3f * 256 * 9000);
            }
        }
        printf("voices: each at its larger gain\n");
    }
    {
        // debug overrides: one device alone
        audio::DeviceMerge m;
        m.source = audio::DeviceMerge::kTvOnly;
        Run r = run(m, music, chime, 10);
        for (long s = 0; s < 10L * kN; s++) assert(r.out_l[s] == std::lrint(music(s)));
        m.source = audio::DeviceMerge::kDrcOnly;
        r = run(m, music, chime, 10);
        for (long s = 0; s < 10L * kN; s++) assert(r.out_l[s] == std::lrint(chime(s)));
        printf("overrides: exact\n");
    }
    printf("audio merge: all passed\n");
    return 0;
}
