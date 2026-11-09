#pragma once

#include <array>
#include <cstdint>

#include "hikari/math.hpp"

namespace hikari::gfx {

// Octahedral normal encoding used by GB1.xy, in the 0..1 range.
std::array<float, 2> encode_normal_octahedral(const Vec3& normal);
Vec3 decode_normal_octahedral(float x, float y);

// GB4.a stores floor(alpha) = shading model and frac(alpha) * 256 = flags.
float pack_shading_model(int shading_model, int flags);
int unpack_shading_model(float packed);
int unpack_material_flags(float packed);

enum class MaterialFlag : std::uint32_t {
    AlphaTest = 1u << 0,
    TwoSided = 1u << 1,
    ReceiveShadow = 1u << 2,
    ReceiveAo = 1u << 3,
    ReceiveSsr = 1u << 4,
    ReceiveGi = 1u << 5,
    Outline = 1u << 6,
};

} // namespace hikari::gfx
