#pragma once
#include <SDL3/SDL.h>
namespace input {
// Main-thread calls from the SDL window/event loop.
void handle_event(const SDL_Event& event);
void update();
void set_prompt_window(SDL_Window* window);
// Android: a touch device that belongs to a connected controller (GameSir and others register a
// virtual touch screen for their buttons); its touches are not the player's fingers
bool touch_from_controller(SDL_TouchID id);
}
