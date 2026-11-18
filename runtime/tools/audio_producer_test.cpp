// Synthetic-only verification of all-channel gain and the speaker test.
#include "audio_out.h"
#include "audio_channels.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace hostui {
bool get(const char *, std::string &) {
    return false;
}
void set(const char *, const std::string &) {}
} // namespace hostui
void log_msg(const char *, ...) {}
static void env(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
int main(int argc, char **argv) {
    assert(argc == 3);
    std::string mode = argv[2];
    const bool stereo = mode.starts_with("stereo");
    const bool tone = mode == "tone";
    env("WWHD_NO_AUDIO", "1");
    env("WWHD_AUDIO_SPEAKERS", stereo ? "stereo" : "surround");
    env("WWHD_AUDIO_VOLUME", "0.5");
    env("WWHD_AUDIO_DUMP", argv[1]);
    audio::init();
    const int channels = audio::channels();
    assert(channels == (stereo ? 2 : 6));
    const int frames = 48000;
    if (tone)
        audio::start_speaker_test();
    // Each channel has a distinct signal, pushed in the mixer's 144-frame blocks.
    std::vector<int16_t> source(144 * channels);
    std::vector<int16_t> expected;
    for (int second = 0; second < (tone ? 6 : 1); ++second) {
        if (tone)
            assert(!strcmp(audio::speaker_test_channel(), audio::kSpeakerNames[second]));
        for (int begin = 0; begin < frames; begin += 144) {
            int count = std::min(144, frames - begin);
            for (int i = 0; i < count; ++i)
                for (int ch = 0; ch < channels; ++ch)
                    source[i * channels + ch] = int16_t((ch == 0 ? 30000 : ch == 1 ? -30000 : (ch + 1) * 1000) + i % 4);
            audio::push(source.data(), count);
            if (!tone)
                for (int i = 0; i < count * channels; ++i)
                    expected.push_back(int16_t(source[i] * 0.5f));
        }
    }
    if (tone)
        assert(audio::speaker_test_channel() == nullptr);
    audio::finish_dump();
    std::ifstream file(argv[1], std::ios::binary);
    file.seekg(stereo ? 44 : 68);
    if (!tone) {
        std::vector<int16_t> actual(expected.size());
        file.read(reinterpret_cast<char *>(actual.data()), actual.size() * 2);
        assert(file.good() && actual == expected);
    } else {
        for (int speaker = 0; speaker < 6; ++speaker) {
            bool heard = false;
            for (int frame = 0; frame < 48000; ++frame) {
                int16_t samples[6];
                file.read(reinterpret_cast<char *>(samples), 12);
                assert(file.good());
                for (int ch = 0; ch < 6; ++ch) {
                    if (ch != speaker || frame >= 31200)
                        assert(samples[ch] == 0);
                    else {
                        heard |= samples[ch] != 0;
                        assert(std::abs(int(samples[ch])) <= 2048);
                    }
                }
            }
            assert(heard);
        }
    }
    file.close();
    std::remove(argv[1]);
}
