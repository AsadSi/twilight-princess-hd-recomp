// Host audio output mode (runtime/src/audio_output_mode.h): TV play plays the TV mix bit-exactly, with the
// game's sound on the GamePad (Off-TV Play) the host plays TV + GamePad, the switches in and out follow the
// game's own fade with nothing it plays dropped or doubled (also what each device still had queued at a
// flip), and the mode comes from game state alone (a boot or a loaded save state with the sound on the
// GamePad gets it right).
#include "audio_output_mode.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>

using audio::OutputSelect;

// the game's sound DSP (02B06FE0, 02B077A4): one mix on one device (0 TV, 1 GamePad) at level `fade`; asked
// for the other device it fades out by 1/15 per DSP frame, switches once below 0.001 and fades back in.
// The final mixes play what it queued kQueue frames earlier (about 11 audio frames, measured).
constexpr int kQueue = 11;
struct Dsp {
    int dev = 0, want = 0;
    float fade = 1;
    std::deque<float> tv_q = std::deque<float>(kQueue, 1.0f), drc_q = std::deque<float>(kQueue, 0.0f);
    float tv = 1, drc = 0;  // the levels the final mixes play this frame
    void frame() {
        if (dev != want) {
            fade -= 1 / 15.0f;
            if (fade <= 0.001f) dev = want, fade = 0;
        } else {
            fade = std::min(1.0f, fade + 1 / 15.0f);
        }
        tv_q.push_back(dev == 0 ? fade : 0);
        drc_q.push_back(dev == 1 ? fade : 0);
        tv = tv_q.front(), drc = drc_q.front();
        tv_q.pop_front(), drc_q.pop_front();
    }
};

// `frames` audio frames of a sound the game plays at full level throughout (1 = full): the host level each
// frame must be what the devices carry. Returns the lowest level and whether the host ended on TV + GamePad
struct Levels { double lo = 1e9; bool drc_end = false; };
static Levels run(Dsp& d, OutputSelect& o, int frames) {
    Levels L;
    for (int i = 0; i < frames; i++) {
        d.frame();
        o.update(true, d.dev == 1);
        int32_t tl = (int32_t)(d.tv * 10000), dl = (int32_t)(d.drc * 10000), ol, orr;
        o.mix(&tl, &tl, &dl, &dl, 1, &ol, &orr);
        assert(ol == tl + dl);  // the host hears every device that carries the sound, once
        L.lo = std::min(L.lo, ol / 10000.0);
        L.drc_end = o.play_drc;
    }
    return L;
}

int main() {
    {
        // TV play: the host gets the TV final mix bit-exactly, the GamePad mix not at all
        OutputSelect o;
        Dsp d;
        run(d, o, 50);
        assert(o.play_tv && !o.play_drc);
        int32_t tl[144], tr[144], dl[144], dr[144], ol[144], orr[144];
        srand(5);
        for (int i = 0; i < 144; i++) {
            tl[i] = rand() % 80000 - 40000; tr[i] = rand() % 80000 - 40000;
            dl[i] = rand() % 80000 - 40000; dr[i] = rand() % 80000 - 40000;
        }
        o.mix(tl, tr, dl, dr, 144, ol, orr);
        for (int i = 0; i < 144; i++) assert(ol[i] == tl[i] && orr[i] == tr[i]);
        // before the game's DSP exists (boot): TV
        OutputSelect boot;
        boot.update(false, true);
        assert(boot.play_tv && !boot.play_drc);
        printf("TV play: TV mix bit-exact\n");
    }
    {
        // Off-TV Play: the TV fades out, the DSP switches, the GamePad fades in; the host follows it
        OutputSelect o;
        Dsp d;
        run(d, o, 50);
        d.want = 1;
        Levels L = run(d, o, 60);
        printf("into Off-TV Play: host level down to %.3f (the game's fade), then %s\n", L.lo, L.drc_end ? "TV + GamePad" : "TV");
        assert(L.drc_end && d.dev == 1 && d.drc == 1);
        int32_t tl[4] = {0, 0, 0, 0}, dl[4] = {100, -200, 300, -400}, ol[4], orr[4];
        o.mix(tl, tl, dl, dl, 4, ol, orr);
        for (int i = 0; i < 4; i++) assert(ol[i] == dl[i] && orr[i] == dl[i]);
        printf("Off-TV Play: host = TV + GamePad (= the GamePad mix)\n");

        // a save state taken now and loaded into a fresh session: the mode comes from game memory
        OutputSelect fresh;
        fresh.update(true, d.dev == 1);
        assert(fresh.play_drc);
        printf("save state in Off-TV Play: loads as Off-TV Play\n");

        // back to the TV: TV + GamePad until the GamePad has played out its queue, then the TV alone
        d.want = 0;
        Levels B = run(d, o, 20);  // switched (after 15), the GamePad's queue still playing
        assert(B.drc_end && d.dev == 0);
        B = run(d, o, 30);
        assert(!B.drc_end && d.tv == 1);
        printf("back to TV play: TV + GamePad until the GamePad's queue is played out, then TV only\n");
    }
    {
        // debug overrides
        OutputSelect o;
        assert(OutputSelect::parse(nullptr) == OutputSelect::kAuto && OutputSelect::parse("auto") == OutputSelect::kAuto);
        o.source = OutputSelect::parse("tv");
        o.update(true, true);
        assert(o.play_tv && !o.play_drc);
        o.source = OutputSelect::parse("gamepad");
        o.update(false, false);
        assert(!o.play_tv && o.play_drc);
        printf("overrides: tv / gamepad\n");
    }
    printf("audio output: all passed\n");
    return 0;
}
