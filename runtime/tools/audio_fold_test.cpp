// SPDX-License-Identifier: MPL-2.0
// audio::fold_to_stereo: 5.1 into two speakers
#include "audio_channels.h"
#include <cassert>
#include <cstdio>
using namespace audio;

static void fold(int32_t l, int32_t r, int32_t sl, int32_t sr, int32_t c, int32_t lfe, int32_t &out_l, int32_t &out_r) {
    int32_t ax[kAxChannels];
    ax[kAxLeft] = l, ax[kAxRight] = r, ax[kAxSurroundLeft] = sl, ax[kAxSurroundRight] = sr, ax[kAxCentre] = c, ax[kAxLfe] = lfe;
    fold_to_stereo(ax, out_l, out_r);
}

int main() {
    int32_t l, r;
    fold(1000, -2000, 0, 0, 0, 0, l, r);  // the fronts pass unchanged
    assert(l == 1000 && r == -2000);
    fold(0, 0, 0, 0, 10000, 0, l, r);  // the centre: both sides, -3 dB
    assert(l == 7071 && r == 7071);
    fold(0, 0, 10000, -10000, 0, 0, l, r);  // each surround: its own side, -3 dB
    assert(l == 7071 && r == -7072);
    fold(0, 0, 0, 0, 0, 30000, l, r);  // the LFE is left out
    assert(l == 0 && r == 0);
    fold(INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX, 0, l, r);  // held at the limits, no wrap
    assert(l == INT32_MAX && r == INT32_MIN);
    puts("audio fold: ok");
}
