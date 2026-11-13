#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "hikari/gfx/material.hpp"

namespace hikari::gfx {

// One native material preset. A preset only carries the PBR response and the
// shading model, never the base colour: the authored PMX diffuse stays the
// artist's own texture tint, and a preset must not overwrite it.
struct MaterialPreset {
    std::string_view category;
    std::string_view name;
    ShadingModel model;
    // GGX roughness of the base lobe.
    float roughness;
    std::array<float, 3> specular;
    float metallic;
    // CustomData.x/y, the two free floats a PMX material cannot express.
    float custom_a;
    float custom_b;
    bool hashed_alpha = false;
    float hex_tiling_scale = 0.0f;
};

// The native preset library: architecture, cloth, skin, hair, metal, nature,
// transparent, emissive, toon, and eye.
const std::vector<MaterialPreset>& material_presets();

// Looks up a preset by name or category/name; nullptr if not found.
const MaterialPreset* find_material_preset(std::string_view name);

// Applies a preset to a material. The shading model and the flags share one
// packed word, so the flags are preserved.
void apply_material_preset(const MaterialPreset& preset, MaterialConstants& constants);

} // namespace hikari::gfx
