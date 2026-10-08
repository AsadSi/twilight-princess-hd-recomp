// The characters the game can draw in the player's name, read at run time from the user's own game
// files (nothing of the game is stored in the port): the font rodan_b_24_22.bfn in res/Fontus/fontres.arc
// (Yaz0-compressed RARC), which draws the name on the file select and in the dialogs. It decides which
// keys the text prompt (text_entry.h) offers and which typed characters it takes. On the USA console the
// runtime reports the game uses the US font for English, French and Spanish.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace game_font {

using Glyphs = std::unordered_set<uint32_t>;  // Unicode code points

// The name font's characters: null when the font can't be read (the prompt then offers its full set; the
// reason is logged once). Any thread; read once and kept. (The language is the swkbd's; every language
// the game shows uses the same font.)
std::shared_ptr<const Glyphs> name_glyphs(int language);

// The code points a JUTResFont (.bfn, single-byte Windows-1252 codes) maps, from its MAP1 blocks; false
// if the data is no font. Exposed for tests.
bool parse_bfn(const uint8_t* data, size_t size, Glyphs& out);

}  // namespace game_font
