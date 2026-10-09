#pragma once
#include <SDL3/SDL.h>
namespace mods {
void mouse_init(SDL_Window* tv);
bool handle_mouse_event(const SDL_Event& event);
void update_mouse();
}
