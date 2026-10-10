// Built-in mods: the Mods tab's first section. Their switches and choices are kept in the active mod profile
// (packages.h builtin_state) and applied once per logic step on the game thread (step).
#pragma once
#include <string>
#include <vector>

namespace mods::builtin {
struct Option {
    std::string id, name;
    std::vector<std::string> choices;  // the first is the default
};
struct Mod {
    std::string id, name, description;
    std::vector<Option> options;
};
const std::vector<Mod>& catalogue();
bool on(const std::string& mod);
int choice(const std::string& mod, const std::string& option);  // index into the option's choices
bool set_on(const std::string& mod, bool on, std::string& error);
bool set_choice(const std::string& mod, const std::string& option, int index, std::string& error);
void step();  // main01's frame loop, once per logic step (interp.cpp)
double timer();  // the Timer mod's seconds, or -1 while it is off (any thread)
}  // namespace mods::builtin
