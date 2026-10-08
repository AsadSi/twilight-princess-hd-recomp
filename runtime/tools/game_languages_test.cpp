// Tests the console languages TP HD shows its text in (game_languages.cpp) and the language a start
// gets for one it doesn't show.
#include <cassert>
#include <cstdio>
#include <string>
#include "game_languages.h"
int main() {
    assert((game_lang::available() == std::vector<int>{1, 2, 5}));
    assert(game_lang::is_available(1) && game_lang::is_available(5) && !game_lang::is_available(3) && !game_lang::is_available(0));
    assert(game_lang::usable(2) == 2 && game_lang::usable(3) == 1 && game_lang::usable(10) == 1);
    assert(std::string(game_lang::name(11)) == "Chinese (Taiwan)" && std::string(game_lang::name(12)) == "?");
    assert(game_lang::started() == -1);
    game_lang::set_started(5);
    assert(game_lang::started() == 5);
    std::puts("game_languages_test: ok");
}
