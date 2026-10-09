// Which of the Wii U's two sound outputs the host plays: the TV mix, or in Off-TV Play the TV and
// GamePad (DRC) mixes summed. Driven by the game's own output device.
//
// TP HD mixes all of its sound in software, in its DSP thread ("Dsp Thread", object at *1012B6DC), and
// writes the one mix to one device (02B077A4): +0x34 is that device (0 TV, 1 GamePad); the other
// device's buffers are zeroed. +0x35 is the device asked for, set every game frame (02AD9F9C ->
// 02B06D38): the GamePad in Off-TV Play (Minus; the game's "DRC only" flag 1012C8D9, toggled in
// 02AF0A98 and cleared when it leaves GamePad play) or with headphones in the GamePad (VPADStatus
// +0x90, which the runtime never reports). A switch (02B06FE0) fades the mix out (1/15 per DSP frame,
// fade at 1012B6E8), flips +0x34 and fades it in on the new device. The final-mix callbacks play what
// the DSP queued about 30 ms earlier, so each device still sounds that long after a flip.
//
// The device is read once per audio frame (ax.cpp), not from the call: the state is in game memory, so
// a boot, a switch and a loaded save state all give the right output with no extra state of our own.
//
// Decision:
//   DSP on the TV        host = TV final mix, exactly as without a GamePad mix
//   DSP on the GamePad   host = TV + GamePad final mixes: the GamePad's sound, what its speaker plays
//                        (the TV is silent once it has played out what it had queued at the flip)
//   back on the TV       host = TV + GamePad until the GamePad has played out its queue (its first
//                        silent frame), then the TV alone. The game faded both sides of a switch itself.
// The game drives the GamePad's small speaker at DSP volume 4.0 against the TV's 1.64 (7.7 dB louder);
// both play on the same host speakers here, so the GamePad mix is played at the TV's level.
// WWHD_AUDIO_OUTPUT=auto (default) | tv | gamepad overrides it (debug).
#pragma once
#include <cstdint>
#include <cstring>

namespace audio {

class OutputSelect {
public:
    enum Source { kAuto, kTv, kGamePad };
    Source source = kAuto;

    static Source parse(const char* s) {
        if (!s) return kAuto;
        if (!strcmp(s, "tv")) return kTv;
        if (!strcmp(s, "gamepad")) return kGamePad;
        return kAuto;
    }
    // per audio frame: which mixes go to the host. dsp_ok false: the game's sound DSP is not there
    // (yet): TV only
    void update(bool dsp_ok, bool dsp_on_gamepad) {
        play_tv = source != kGamePad;
        play_drc = source == kGamePad || (source == kAuto && ((dsp_ok && dsp_on_gamepad) || (play_drc && drc_sounds)));
    }
    bool play_tv = true, play_drc = false;
    bool drc_sounds = false;  // the last GamePad frame was not silent
    static constexpr float kDrcLevel = 1.64f / 4.0f;  // the GamePad mix at the TV's level

    // one frame of n samples per channel -> out (not clamped)
    void mix(const int32_t* tv_l, const int32_t* tv_r, const int32_t* drc_l, const int32_t* drc_r, int n, int32_t* out_l,
             int32_t* out_r) {
        drc_sounds = false;
        for (int i = 0; i < n; i++) {
            out_l[i] = (play_tv ? tv_l[i] : 0) + (play_drc ? int32_t(drc_l[i] * kDrcLevel) : 0);
            out_r[i] = (play_tv ? tv_r[i] : 0) + (play_drc ? int32_t(drc_r[i] * kDrcLevel) : 0);
            drc_sounds |= (drc_l[i] | drc_r[i]) != 0;
        }
    }
};

}  // namespace audio
