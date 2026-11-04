#include "hikari/animation/bezier.hpp"

#include <algorithm>
#include <cmath>

namespace hikari::animation {

float eval_mmd_bezier(std::uint8_t x1_byte, std::uint8_t y1_byte, std::uint8_t x2_byte,
                      std::uint8_t y2_byte, float t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }

    const float x1 = static_cast<float>(x1_byte) / 127.0f;
    const float y1 = static_cast<float>(y1_byte) / 127.0f;
    const float x2 = static_cast<float>(x2_byte) / 127.0f;
    const float y2 = static_cast<float>(y2_byte) / 127.0f;

    if (std::abs(x1 - y1) < 1.0e-4f && std::abs(x2 - y2) < 1.0e-4f) {
        return t;
    }

    float s = t;
    for (int i = 0; i < 16; ++i) {
        const float one_minus_s = 1.0f - s;
        const float x = 3.0f * one_minus_s * one_minus_s * s * x1 +
                        3.0f * one_minus_s * s * s * x2 +
                        s * s * s;
        const float dx = x - t;
        if (std::abs(dx) < 1.0e-5f) {
            break;
        }
        const float d = 3.0f * one_minus_s * one_minus_s * x1 +
                        6.0f * one_minus_s * s * (x2 - x1) +
                        3.0f * s * s * (1.0f - x2);
        if (std::abs(d) < 1.0e-6f) {
            break;
        }
        s -= dx / d;
        s = std::clamp(s, 0.0f, 1.0f);
    }

    const float one_minus_s = 1.0f - s;
    return 3.0f * one_minus_s * one_minus_s * s * y1 +
           3.0f * one_minus_s * s * s * y2 +
           s * s * s;
}

std::array<std::uint8_t, 4> unpack_bone_bezier(const std::array<std::uint8_t, 64>& buffer,
                                               std::size_t channel) {
    return {buffer[channel], buffer[4 + channel], buffer[8 + channel], buffer[12 + channel]};
}

} // namespace hikari::animation
