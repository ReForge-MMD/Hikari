#include "hikari/gfx/material_presets.hpp"

#include <algorithm>

namespace hikari::gfx {

const std::vector<MaterialPreset>& material_presets() {
    static const std::vector<MaterialPreset> presets = {
        // Architecture
        {"Architecture", "architecture", ShadingModel::Default, 0.55f, {0.045f, 0.045f, 0.045f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Architecture", "asphalt", ShadingModel::Default, 0.85f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 8.0f},
        {"Architecture", "brick", ShadingModel::Default, 0.75f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 6.0f},
        {"Architecture", "concrete", ShadingModel::Default, 0.65f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 4.0f},
        {"Architecture", "marble_polished", ShadingModel::ClearCoat, 0.15f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.90f, 0.0f, false, 0.0f},
        {"Architecture", "parquet", ShadingModel::ClearCoat, 0.30f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.70f, 0.0f, false, 4.0f},
        {"Architecture", "tiles_ceramic", ShadingModel::ClearCoat, 0.18f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.85f, 0.0f, false, 8.0f},
        {"Architecture", "wood_rough", ShadingModel::Default, 0.60f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 4.0f},

        // Cloth
        {"Cloth", "cloth", ShadingModel::Cloth, 0.85f, {0.55f, 0.50f, 0.44f}, 0.0f, 0.60f, 0.0f, false, 0.0f},
        {"Cloth", "denim", ShadingModel::Cloth, 0.80f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.65f, 0.0f, false, 6.0f},
        {"Cloth", "cotton", ShadingModel::Cloth, 0.75f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.50f, 0.0f, false, 0.0f},
        {"Cloth", "silk", ShadingModel::Cloth, 0.35f, {0.06f, 0.06f, 0.06f}, 0.0f, 0.85f, 0.0f, false, 0.0f},
        {"Cloth", "velvet", ShadingModel::Cloth, 0.90f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.95f, 0.0f, false, 0.0f},
        {"Cloth", "wool", ShadingModel::Cloth, 0.88f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.70f, 0.0f, false, 0.0f},
        {"Cloth", "leather_black", ShadingModel::Default, 0.40f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Cloth", "stockings", ShadingModel::Cloth, 0.55f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.75f, 0.0f, true, 0.0f},
        {"Cloth", "lace", ShadingModel::Cloth, 0.65f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.60f, 0.0f, true, 0.0f},
        {"Cloth", "latex", ShadingModel::ClearCoat, 0.10f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.95f, 0.0f, false, 0.0f},

        // Skin
        {"Skin", "skin", ShadingModel::Skin, 0.38f, {0.30f, 0.26f, 0.24f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Skin", "human_realistic", ShadingModel::Skin, 0.42f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.45f, 0.0f, false, 0.0f},
        {"Skin", "anime_warm", ShadingModel::Skin, 0.38f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.35f, 0.0f, false, 0.0f},
        {"Skin", "anime_cool", ShadingModel::Skin, 0.38f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.30f, 0.0f, false, 0.0f},
        {"Skin", "tda_face", ShadingModel::Skin, 0.36f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.40f, 0.0f, false, 0.0f},
        {"Skin", "tda_body", ShadingModel::Skin, 0.40f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.38f, 0.0f, false, 0.0f},

        // Hair
        {"Hair", "hair", ShadingModel::Anisotropy, 0.28f, {0.28f, 0.24f, 0.20f}, 0.0f, 0.72f, 0.0f, false, 0.0f},
        {"Hair", "anisotropic_realistic", ShadingModel::Anisotropy, 0.30f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.75f, 0.0f, true, 0.0f},
        {"Hair", "anime_dual_lobe", ShadingModel::Anisotropy, 0.25f, {0.06f, 0.06f, 0.06f}, 0.0f, 0.85f, 0.0f, true, 0.0f},
        {"Hair", "dark_silk", ShadingModel::Anisotropy, 0.22f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.80f, 0.0f, false, 0.0f},
        {"Hair", "golden_shine", ShadingModel::Anisotropy, 0.28f, {0.07f, 0.07f, 0.07f}, 0.0f, 0.90f, 0.0f, false, 0.0f},

        // Metallic
        {"Metallic", "metal", ShadingModel::Default, 0.22f, {0.92f, 0.90f, 0.86f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "gold", ShadingModel::Default, 0.18f, {1.00f, 0.76f, 0.33f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "silver", ShadingModel::Default, 0.14f, {0.95f, 0.93f, 0.88f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "copper", ShadingModel::Default, 0.20f, {0.95f, 0.64f, 0.54f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "brass", ShadingModel::Default, 0.22f, {0.91f, 0.78f, 0.45f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "iron", ShadingModel::Default, 0.35f, {0.56f, 0.57f, 0.58f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "steel", ShadingModel::Default, 0.25f, {0.60f, 0.60f, 0.60f}, 1.0f, 0.0f, 0.0f, false, 0.0f},
        {"Metallic", "worn_metal", ShadingModel::Default, 0.45f, {0.55f, 0.55f, 0.55f}, 0.85f, 0.0f, 0.0f, false, 0.0f},

        // ClearCoat
        {"ClearCoat", "carpaint", ShadingModel::ClearCoat, 0.25f, {0.04f, 0.04f, 0.04f}, 0.75f, 0.95f, 0.0f, false, 0.0f},
        {"ClearCoat", "lacquer_black", ShadingModel::ClearCoat, 0.08f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.90f, 0.0f, false, 0.0f},
        {"ClearCoat", "pearl_iridescent", ShadingModel::ClearCoat, 0.12f, {0.05f, 0.05f, 0.05f}, 0.20f, 0.95f, 0.5f, false, 0.0f},

        // Subsurface & Nature
        {"Nature", "nature", ShadingModel::Subsurface, 0.62f, {0.05f, 0.06f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Nature", "foliage_leaves", ShadingModel::Subsurface, 0.50f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.60f, 0.0f, true, 0.0f},
        {"Nature", "ground_dirt", ShadingModel::Default, 0.85f, {0.03f, 0.03f, 0.03f}, 0.0f, 0.0f, 0.0f, false, 6.0f},
        {"Nature", "ice", ShadingModel::Glass, 0.08f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Nature", "snow", ShadingModel::Subsurface, 0.65f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.80f, 0.0f, false, 0.0f},
        {"Subsurface", "jade", ShadingModel::Subsurface, 0.30f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.70f, 0.0f, false, 0.0f},
        {"Subsurface", "marble", ShadingModel::Subsurface, 0.25f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.55f, 0.0f, false, 0.0f},

        // Eye
        {"Eye", "eye", ShadingModel::Eye, 0.18f, {0.06f, 0.06f, 0.06f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Eye", "anime_iris", ShadingModel::Eye, 0.12f, {0.06f, 0.06f, 0.06f}, 0.0f, 0.05f, 0.0f, false, 0.0f},
        {"Eye", "cornea_glass", ShadingModel::Eye, 0.05f, {0.05f, 0.05f, 0.05f}, 0.0f, 0.08f, 0.0f, false, 0.0f},

        // Toon
        {"Toon", "toon", ShadingModel::Cel, 0.50f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.50f, 0.0f, false, 0.0f},
        {"Toon", "anime_cel", ShadingModel::Cel, 0.50f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.50f, 0.0f, false, 0.0f},
        {"Toon", "cel_warm_shadow", ShadingModel::Cel, 0.45f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.45f, 0.2f, false, 0.0f},
        {"Toon", "cel_cool_shadow", ShadingModel::Cel, 0.45f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.45f, -0.2f, false, 0.0f},

        // Emissive & Glitter
        {"Emissive", "emissive", ShadingModel::Emissive, 0.45f, {0.04f, 0.04f, 0.04f}, 0.0f, 1.0f, 0.0f, false, 0.0f},
        {"Emissive", "neon_cyan", ShadingModel::Emissive, 0.40f, {0.04f, 0.04f, 0.04f}, 0.0f, 2.5f, 0.0f, false, 0.0f},
        {"Emissive", "neon_magenta", ShadingModel::Emissive, 0.40f, {0.04f, 0.04f, 0.04f}, 0.0f, 2.5f, 0.0f, false, 0.0f},
        {"Glitter", "glitter_gold", ShadingModel::Default, 0.35f, {1.0f, 0.8f, 0.4f}, 0.8f, 1.5f, 0.0f, false, 0.0f},

        // Transparent
        {"Transparent", "transparent", ShadingModel::Glass, 0.06f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Transparent", "glass_clear", ShadingModel::Glass, 0.04f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
        {"Transparent", "plastic_clear", ShadingModel::Glass, 0.10f, {0.04f, 0.04f, 0.04f}, 0.0f, 0.0f, 0.0f, false, 0.0f},
    };
    return presets;
}

const MaterialPreset* find_material_preset(std::string_view name) {
    const auto& list = material_presets();
    for (const auto& p : list) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

void apply_material_preset(const MaterialPreset& preset, MaterialConstants& constants) {
    MaterialResponse response = response_from_constants(constants);
    response.specular = preset.specular;
    response.roughness = preset.roughness;
    response.metallic = preset.metallic;
    response.custom_a = preset.custom_a;
    response.custom_b = preset.custom_b;
    response.shading_model = static_cast<int>(preset.model);
    response.hex_tiling_scale = preset.hex_tiling_scale;
    constants = apply_material_response(constants, response);
}

} // namespace hikari::gfx
