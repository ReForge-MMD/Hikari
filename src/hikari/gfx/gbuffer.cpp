#include "hikari/gfx/gbuffer.hpp"

#include <algorithm>
#include <cmath>

namespace hikari::gfx {

std::array<float, 2> encode_normal_octahedral(const Vec3& normal) {
    Vec3 n = normal.normalize();
    if (!n.is_finite() || n.length_squared() < 1.0e-12f) {
        n = Vec3(0.0f, 0.0f, 1.0f);
    }
    n = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
    float x = n.x;
    float y = n.y;
    if (n.z < 0.0f) {
        x = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return {x * 0.5f + 0.5f, y * 0.5f + 0.5f};
}

Vec3 decode_normal_octahedral(float x, float y) {
    const float fx = x * 2.0f - 1.0f;
    const float fy = y * 2.0f - 1.0f;
    Vec3 n(fx, fy, 1.0f - std::abs(fx) - std::abs(fy));
    if (n.z < 0.0f) {
        const float nx = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        const float ny = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
        n.x = nx;
        n.y = ny;
    }
    return n.normalize();
}

float pack_shading_model(int shading_model, int flags) {
    const int model = std::clamp(shading_model, 0, 15);
    const int low = std::clamp(flags, 0, 255);
    return static_cast<float>(model) + static_cast<float>(low) / 256.0f;
}

int unpack_shading_model(float packed) {
    return static_cast<int>(std::floor(std::max(packed, 0.0f)));
}

int unpack_material_flags(float packed) {
    return static_cast<int>(std::floor((packed - std::floor(packed)) * 256.0f + 0.5f));
}

} // namespace hikari::gfx
