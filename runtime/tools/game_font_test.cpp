// Tests the name font reader (overlay/game_font.cpp) on a synthetic JUTResFont: MAP1 blocks with the
// linear, table and map methods, Windows-1252 codes, and a missing game folder (null: the text prompt
// offers everything).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "overlay/game_font.h"
namespace config { std::string game_dir = "/nonexistent-tphd-game", save_dir; }
void log_msg(const char*, ...) {}
static void be16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = x >> 8; v[at + 1] = x & 255; }
static void be32(std::vector<uint8_t>& v, size_t at, uint32_t x) { be16(v, at, x >> 16); be16(v, at + 2, x & 0xFFFF); }
int main() {
    // header (0x20) + three MAP1 blocks of 0x20 bytes
    std::vector<uint8_t> f(0x80, 0);
    memcpy(&f[0], "FONTbfn1", 8); be32(f, 12, 3);
    auto map1 = [&](size_t at, uint16_t method, uint16_t first, uint16_t last, uint16_t count) {
        memcpy(&f[at], "MAP1", 4); be32(f, at + 4, 0x20); be16(f, at + 8, method); be16(f, at + 10, first);
        be16(f, at + 12, last); be16(f, at + 14, count);
    };
    map1(0x20, 0, 'A', 'C', 0);                                                    // linear: A B C
    map1(0x40, 2, 'a', 'c', 3); be16(f, 0x50, 0); be16(f, 0x52, 0xFFFF); be16(f, 0x54, 1);  // table: a, (no b), c
    map1(0x60, 3, 0x8C, 0xE9, 2); be16(f, 0x70, 0x8C); be16(f, 0x72, 5); be16(f, 0x74, 0xE9); be16(f, 0x76, 6);  // Œ é
    game_font::Glyphs g;
    assert(game_font::parse_bfn(f.data(), f.size(), g));
    assert(g.size() == 7 && g.count('A') && g.count('C') && g.count('a') && !g.count('b') && g.count('c') && g.count(0x0152) && g.count(0xE9));
    f[0] = 'X';
    game_font::Glyphs none;
    assert(!game_font::parse_bfn(f.data(), f.size(), none));
    assert(!game_font::name_glyphs(1));  // no game files: the full set
    puts("game_font_test: MAP1 linear / table / map, Windows-1252, bad data, missing game passed");
}
