// AX TV order verified against Cemu snd_core/ax_out.cpp, AXOut_SubmitTVFrame.
// Host and WAV order: FL FR FC LFE BL BR. CoreAudio uses explicit labels in this order.
#pragma once
#include <algorithm>
#include <climits>
#include <cstdint>
namespace audio {
// the TV mix's channels as AX orders them
enum AxChannel { kAxLeft, kAxRight, kAxSurroundLeft, kAxSurroundRight, kAxCentre, kAxLfe, kAxChannels };
inline constexpr int kHostToAx[6] = {kAxLeft, kAxRight, kAxCentre, kAxLfe, kAxSurroundLeft, kAxSurroundRight};

// 5.1 folded to stereo for two speakers (ITU-R BS.775): the centre into both sides and each surround into its own,
// at -3 dB; the LFE is left out. ax: one sample per AX channel.
inline void fold_to_stereo(const int32_t *ax, int32_t &left, int32_t &right) {
    constexpr int64_t kMinus3dB = 46341;  // 1/sqrt(2) in 16.16
    auto at_minus_3dB = [](int32_t s) { return int64_t(s) * kMinus3dB >> 16; };
    auto fit = [](int64_t s) { return int32_t(std::clamp<int64_t>(s, INT32_MIN, INT32_MAX)); };
    const int64_t centre = at_minus_3dB(ax[kAxCentre]);
    left = fit(ax[kAxLeft] + centre + at_minus_3dB(ax[kAxSurroundLeft]));
    right = fit(ax[kAxRight] + centre + at_minus_3dB(ax[kAxSurroundRight]));
}
inline constexpr const char *kSpeakerNames[6] = {"Front left", "Front right",   "Centre",
                                                 "Subwoofer",  "Surround left", "Surround right"};
inline int16_t tv_sample(const int32_t *ax, int host_channel, bool play_tv, int32_t drc, bool play_drc) {
    const int64_t sample =
        (play_tv ? int64_t(ax[kHostToAx[host_channel]]) : 0) + (play_drc && host_channel < 2 ? int64_t(drc) : 0);
    return int16_t(std::clamp<int64_t>(sample, -32768, 32767));
}
} // namespace audio
