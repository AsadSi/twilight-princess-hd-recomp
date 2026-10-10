#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace mods::content {
using Files=std::map<std::string,std::string>;
// Validate a replacement tree. Keys follow Wii U case-insensitive path semantics.
// The installed game folder: loose imported files that name one of its content files are placed at that path.
void set_game_root(const std::filesystem::path& game);
const std::filesystem::path& installed_game();  // set_game_root's folder
void import_legacy(const std::filesystem::path& stage,const std::string& source_name);
Files index(const std::filesystem::path& directory);
// Called once before guest execution. Never swap resources during a session. The game's size lists, patched for the
// replacements, are written into `cache`.
void activate(Files files,const std::filesystem::path& cache={});
std::string replacement(const std::string& guest, const std::string& mode="rb");
}
