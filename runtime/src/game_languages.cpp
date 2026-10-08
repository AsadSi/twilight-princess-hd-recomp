// The console languages TP HD shows its text in (game_languages.h).
#include "game_languages.h"

#include <algorithm>
#include <atomic>

namespace game_lang {
namespace {

const char* const kNames[kLanguages] = {"Japanese", "English", "French", "German", "Italian", "Spanish",
                                        "Chinese", "Korean", "Dutch", "Portuguese", "Russian", "Chinese (Taiwan)"};
std::atomic<int> g_started{-1};

}  // namespace

const char* name(int language) { return language >= 0 && language < kLanguages ? kNames[language] : "?"; }

const std::vector<int>& available() {
    static const std::vector<int> languages = {1, 2, 5};
    return languages;
}

bool is_available(int language) {
    const auto& a = available();
    return std::find(a.begin(), a.end(), language) != a.end();
}

int usable(int language) { return is_available(language) ? language : 1; }

int started() { return g_started.load(std::memory_order_acquire); }
void set_started(int language) { g_started.store(language, std::memory_order_release); }

}  // namespace game_lang
