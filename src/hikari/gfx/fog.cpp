#include "hikari/gfx/fog.hpp"

#include <algorithm>

namespace hikari::gfx {

int fog_step_count(int enable, int quality) {
    if (enable < 2) {
        return 0;
    }
    static const int kSteps[] = {12, 20, 32, 48};
    return kSteps[std::clamp(quality, 0, 3)];
}

float fog_raymarch_max_distance() {
    return 250.0f;
}

} // namespace hikari::gfx