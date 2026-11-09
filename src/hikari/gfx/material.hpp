#pragma once

#include <array>
#include <cstdint>

#include "hikari/formats/pmx.hpp"
#include "hikari/render/material.hpp"

namespace hikari::gfx {

// Must match the MaterialParams cbuffer in shaders/gfx_core.hlsl.
struct MaterialConstants {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> specular_roughness{0.04f, 0.04f, 0.04f, 0.5f};
    std::array<float, 4> emissive_metallic{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> custom_data{0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 4> uv_transform{1.0f, 1.0f, 0.0f, 0.0f};
};

float roughness_from_shininess(float shininess);
std::uint32_t material_flags_from_pmx(const formats::PmxMaterial& material);
MaterialConstants material_from_pmx(const formats::PmxMaterial& material);
MaterialConstants material_from_gpu(const render::MaterialConstants& material);

} // namespace hikari::gfx
