// Tunic colours, the built-in mod "tunic": Link's Hero's clothes recoloured (tunic.cpp).
#pragma once
#include <filesystem>
#include <string>

namespace mods::tunic {
// the game's (`game`) res/Object/Kmdl.pack.gz with the clothes in colour `look` (the mod's choice), written into `dir`
// once; empty if it could not be made
std::string pack(const std::filesystem::path& game, const std::filesystem::path& dir, int look);
}  // namespace mods::tunic
