#include "hikari/gfx/material_presets.hpp"

namespace hikari::gfx {

// Roughness values follow the range a real surface of that kind shows: polished
// stone sits near 0.2, woven cloth near 0.9. The specular column is the
// dielectric F0 the PMX specular field is too coarse to express.
const std::vector<MaterialPreset>& material_presets() {
    static const std::vector<MaterialPreset> presets = {
        {"architecture", ShadingModel::Default, 0.55f, {0.045f, 0.045f, 0.045f}, 0.0f, 0.0f,
         0.0f},
        {"cloth", ShadingModel::Cloth, 0.85f, {0.55f, 0.50f, 0.44f}, 0.0f, 0.60f, 0.0f},
        {"skin", ShadingModel::Skin, 0.38f, {0.30f, 0.26f, 0.24f}, 0.0f, 0.0f, 0.0f},
        {"hair", ShadingModel::Anisotropy, 0.28f, {0.28f, 0.24f, 0.20f}, 0.0f, 0.72f, 0.0f},
        {"metal", ShadingModel::Default, 0.22f, {0.92f, 0.90f, 0.86f}, 1.0f, 0.0f, 0.0f},
        {"nature", ShadingModel::Subsurface, 0.62f, {0.05f, 0.06f, 0.04f}, 0.0f, 0.0f, 0.0f},
        {"transparent", ShadingModel::Glass, 0.06f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f},
        {"emissive", ShadingModel::Emissive, 0.45f, {0.04f, 0.04f, 0.04f}, 0.0f, 1.0f, 0.0f},
        {"toon", ShadingModel::Cel, 0.50f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.50f, 0.0f},
        {"eye", ShadingModel::Eye, 0.18f, {0.06f, 0.06f, 0.06f}, 0.0f, 0.0f, 0.0f},
    };
    return presets;
}

void apply_material_preset(const MaterialPreset& preset, MaterialConstants& constants) {
    MaterialResponse response = response_from_constants(constants);
    response.specular = preset.specular;
    response.roughness = preset.roughness;
    response.metallic = preset.metallic;
    response.custom_a = preset.custom_a;
    response.custom_b = preset.custom_b;
    response.shading_model = static_cast<int>(preset.model);
    constants = apply_material_response(constants, response);
}

} // namespace hikari::gfx
