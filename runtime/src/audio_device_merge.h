// Host audio from the Wii U's two outputs: the TV mix and the GamePad (DRC) mix.
//
// The host has one stereo output; the Wii U had two sets of speakers. The game decides per sound
// where it plays (Wind Waker HD, hd_snd 0202F41C / 02030880): in TV play the music and effects go
// to the TV and the GamePad's own sounds (menus, items, controller) to both, the GamePad at full
// level; in Off-TV Play (Minus) the TV master faders go to 0 and everything plays on the GamePad;
// in the "both" mode every sound plays on both at once.
//
// Decision: the host plays what a player in front of the Wii U hears, every sound once, at the
// louder of its two levels. ax.cpp does this in two parts:
//  - voices (almost everything): exactly, from the gains. Both devices' mixes are added and the
//    part a voice has on both (per channel, the smaller of its two main-bus gains) is taken out
//    once, so a voice at TV gain a and GamePad gain b is heard at max(a, b).
//  - what the final-mix callbacks add to each device's output (NW4F's stream mixer, 0281A540:
//    streamed music with a TV and a GamePad volume): only the output is known, so DeviceMerge
//    below estimates the shared part from the two signals.
// Stereo: the GamePad speakers are stereo (DRC channels L, R, surround L, R; the device mode is
// stereo, AXGetDeviceMode), so its L/R go to the host's L/R like the TV's front L/R.
// WWHD_AUDIO_OUTPUT=tv|gamepad (debug) plays one device alone.
//
// DeviceMerge: two signals that may carry the same sounds at different levels -> each sound once.
// The louder signal S is kept; of the quieter one W only what S does not already carry is added:
//   out = S + (1 - k) * W,  k = min(1, <S,W> / <W,W>) * g(rho)
// <S,W> / <W,W> is how much of W the louder signal carries (its regression on W); it counts only
// as far as the two are measurably the same sound (correlation rho from 0.5 to 0.9; below, they
// are different sounds and both are kept whole). The sums are smoothed over ~50 ms and the weights
// move linearly within a frame, so the levels change slowly and the waveform never.
//   one signal silent                       out = the other, exactly
//   the same sound at levels a and b        out = max(a, b) * sound, not a + b
//   different sounds                        out = their sum
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace audio {

// The part of one voice that both devices play, added to out: per sample in * min(TV gain, GamePad
// gain). Gains as AX device mixes have them (vol / 0x8000, ramping by delta / 0x8000 per sample
// from the first sample on, as ax.cpp mix_into). Mixing a voice into both devices and taking this
// out once leaves it at max(TV gain, GamePad gain).
inline void mix_shared(const float* in, float* out, int n, uint16_t tv_vol, int16_t tv_delta, uint16_t drc_vol,
                       int16_t drc_delta) {
    float a = tv_vol / 32768.0f, da = tv_delta / 32768.0f, b = drc_vol / 32768.0f, db = drc_delta / 32768.0f;
    for (int i = 0; i < n; i++) {
        a += da;
        b += db;
        out[i] += in[i] * std::min(a, b);
    }
}

class DeviceMerge {
public:
    enum Source { kMerged, kTvOnly, kDrcOnly };
    Source source = kMerged;

    // one frame of n samples per channel (s16 range, may exceed it) -> out (not clamped)
    void merge(const int32_t* tv_l, const int32_t* tv_r, const int32_t* drc_l, const int32_t* drc_r, int n,
               int32_t* out_l, int32_t* out_r) {
        if (source != kMerged) {
            const int32_t* l = source == kTvOnly ? tv_l : drc_l;
            const int32_t* r = source == kTvOnly ? tv_r : drc_r;
            for (int i = 0; i < n; i++) { out_l[i] = l[i]; out_r[i] = r[i]; }
            return;
        }
        double tt = 0, dd = 0, td = 0;
        for (int i = 0; i < n; i++) {
            double a = tv_l[i], b = tv_r[i], x = drc_l[i], y = drc_r[i];
            tt += a * a + b * b;
            dd += x * x + y * y;
            td += a * x + b * y;
        }
        tt_ = tt_ * kDecay + tt;
        dd_ = dd_ * kDecay + dd;
        td_ = td_ * kDecay + td;
        // weights of TV and GamePad: the louder one 1, the quieter one 1 - k
        float wt = 1, wd = 1;
        float k = shared();
        if (tt_ >= dd_) wd = 1 - k;
        else wt = 1 - k;
        for (int i = 0; i < n; i++) {
            float f = (float)(i + 1) / (float)n;
            float a = wt_ + (wt - wt_) * f, b = wd_ + (wd - wd_) * f;
            out_l[i] = (int32_t)std::lrint(a * (float)tv_l[i] + b * (float)drc_l[i]);
            out_r[i] = (int32_t)std::lrint(a * (float)tv_r[i] + b * (float)drc_r[i]);
        }
        wt_ = wt;
        wd_ = wd;
        k_ = k;
    }

    float shared_fraction() const { return k_; }  // k of the last frame (tests, stats)
    void reset() { tt_ = dd_ = td_ = 0; wt_ = wd_ = 1; k_ = 0; }

private:
    // per 3 ms frame: exp(-3 / 50)
    static constexpr double kDecay = 0.94176453358424872;
    // below this energy (smoothed sum of squares, ~1 LSB rms) a signal counts as silent
    static constexpr double kSilent = 2.0 * 144 / (1.0 - kDecay);
    double tt_ = 0, dd_ = 0, td_ = 0;
    float wt_ = 1, wd_ = 1, k_ = 0;

    float shared() const {
        if (tt_ < kSilent || dd_ < kSilent || td_ <= 0) return 0;
        double weak = std::min(tt_, dd_);
        double carried = std::min(1.0, td_ / weak);
        double rho = td_ / std::sqrt(tt_ * dd_);
        double g = std::clamp((rho - 0.5) / 0.4, 0.0, 1.0);
        return (float)(carried * g);
    }
};

}  // namespace audio
