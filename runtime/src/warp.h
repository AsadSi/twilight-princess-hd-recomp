// Warps: WWHD_WARP (a test aid) and the Warp menu mod's places (warp.cpp).
#pragma once
#include <string>
#include <vector>

namespace warp {
struct Place {
    std::string stage;
    int point, room, layer;
};
struct Group {
    std::string name;
    std::vector<Place> places;
};
const std::vector<Group>& groups();  // the developer map select's places, by area, in its order
void go(const Place& p);             // at the top of the next frame
}  // namespace warp
