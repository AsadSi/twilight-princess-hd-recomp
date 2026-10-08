// Tests the text variants (game_languages.cpp): the region a language gets, region codes, a language the game
// lacks, and WWHD_LANGUAGE / WWHD_LANGUAGE_REGION.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game_languages.h"
namespace hostui { bool get(const char*, std::string&) { return false; } }
void log_msg(const char*, ...) {}
int main() {
    using namespace game_lang;
    assert(variants().size() == 9);
    assert(choose(3, "").region == kEurope && std::string(choose(3, "").name) == "German");
    assert(choose(1, "").region == kUsa && choose(1, "eu").region == kEurope && choose(2, "Europe").region == kEurope);
    assert(choose(0, "us").region == kJapan);  // Japanese text only with the Japanese region
    assert(choose(7, "").language == 1 && choose(7, "").region == kUsa);  // Korean: English
    assert(region_from_code("EUR") == kEurope && region_from_code("x") == 0 && std::string(region_code(kJapan)) == "jp");
    setenv("WWHD_LANGUAGE", "5", 1);
    setenv("WWHD_LANGUAGE_REGION", "eu", 1);
    assert(current().language == 5 && current().region == kEurope);
    puts("game_languages_test: variants, regions, fallback, environment passed");
}
