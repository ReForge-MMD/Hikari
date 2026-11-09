#include "hikari/gfx/material.hpp"

#include <algorithm>
#include <cmath>

#include "hikari/gfx/gbuffer.hpp"

namespace hikari::gfx {

float roughness_from_shininess(float shininess) {
    const float safe = std::max(shininess, 0.0f);
    return std::clamp(std::sqrt(1.0f / (1.0f + safe * 0.15f)), 0.08f, 1.0f);
}

static std::array<float, 4> dielectric_specular(const std::array<float, 3>& specular,
                                                float roughness) {
    return {std::clamp(0.04f + 0.12f * specular[0], 0.04f, 0.20f),
            std::clamp(0.04f + 0.12f * specular[1], 0.04f, 0.20f),
            std::clamp(0.04f + 0.12f * specular[2], 0.04f, 0.20f), roughness};
}

std::uint32_t material_flags_from_pmx(const formats::PmxMaterial& material) {
    std::uint32_t flags = 0;
    if (material.diffuse[3] < 0.999f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::AlphaTest);
    }
    // PMX material flag bit 0 means "draw both sides".
    if ((material.flags & 0x01u) != 0) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::TwoSided);
    }
    flags |= static_cast<std::uint32_t>(MaterialFlag::ReceiveShadow);
    flags |= static_cast<std::uint32_t>(MaterialFlag::ReceiveAo);
    flags |= static_cast<std::uint32_t>(MaterialFlag::ReceiveSsr);
    flags |= static_cast<std::uint32_t>(MaterialFlag::ReceiveGi);
    if (material.edge_size > 0.0f && material.edge_color[3] > 0.0f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::Outline);
    }
    return flags;
}

MaterialConstants material_from_pmx(const formats::PmxMaterial& material) {
    MaterialConstants constants;
    constants.base_color = material.diffuse;
    const float roughness = roughness_from_shininess(material.shininess);
    constants.specular_roughness = dielectric_specular(material.specular, roughness);
    constants.emissive_metallic = {0.0f, 0.0f, 0.0f, 0.0f};
    const std::uint32_t flags = material_flags_from_pmx(material);
    constants.custom_data = {0.0f, 0.0f, 1.0f,
                             pack_shading_model(0, static_cast<int>(flags))};
    constants.uv_transform = {1.0f, 1.0f, 0.0f, 0.0f};
    return constants;
}

MaterialConstants material_from_gpu(const render::MaterialConstants& material) {
    MaterialConstants constants;
    constants.base_color = material.diffuse;
    const float roughness = roughness_from_shininess(material.shininess);
    constants.specular_roughness = dielectric_specular(material.specular, roughness);
    constants.emissive_metallic = {0.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t flags = static_cast<std::uint32_t>(MaterialFlag::ReceiveShadow) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveAo) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveSsr) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveGi);
    if (material.diffuse[3] < 0.999f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::AlphaTest);
    }
    if (material.edge_size > 0.0f && material.edge_color[3] > 0.0f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::Outline);
    }
    constants.custom_data = {0.0f, 0.0f, 1.0f,
                             pack_shading_model(0, static_cast<int>(flags))};
    constants.uv_transform = {1.0f, 1.0f, 0.0f, 0.0f};
    return constants;
}

} // namespace hikari::gfx
