#pragma once

#include <array>
#include <cstdint>

namespace hikari::animation {

// Evaluates the cubic Bezier used by VMD interpolation. Control points are stored
// as bytes in the range [0, 127] and normalized to [0, 1] internally.
float eval_mmd_bezier(std::uint8_t x1, std::uint8_t y1, std::uint8_t x2, std::uint8_t y2,
                      float t);

// Channel: 0 = X, 1 = Y, 2 = Z, 3 = Rotation.
std::array<std::uint8_t, 4> unpack_bone_bezier(const std::array<std::uint8_t, 64>& buffer,
                                               std::size_t channel);

} // namespace hikari::animation
