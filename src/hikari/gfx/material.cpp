#include "hikari/gfx/material.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "hikari/gfx/gbuffer.hpp"
#include "hikari/gfx/matgen/material_io.hpp"

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

// A built-in PMX toon resolves to its ramp slice through the shared mapping. An
// external toon texture is an image file the renderer would have to register, so
// it stays on the model's procedural terminator instead of a missing slice.
static int toon_slice_for_gpu(const render::GpuMaterial& material) {
    if (material.toon_flag != 0) {
        return 0;
    }
    return toon_slice_from_index(material.toon_index);
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
    constants.edge_color_size = {material.edge_color[0], material.edge_color[1],
                                 material.edge_color[2], material.edge_size};
    // An external toon texture is a file the caller has to register with the
    // renderer first, so only the built-in indices resolve here.
    const int toon_slice = material.toon_flag == 0 ? toon_slice_from_index(material.toon_index) : 0;
    constants.pmx_params = {static_cast<float>(std::min<std::uint8_t>(material.sphere_mode, 3)),
                            static_cast<float>(toon_slice), 0.0f, 0.0f};
    return constants;
}

MaterialConstants material_from_gpu(const render::GpuMaterial& material) {
    const render::MaterialConstants& source = material.constants;
    MaterialConstants constants;
    constants.base_color = source.diffuse;
    const float roughness = roughness_from_shininess(source.shininess);
    constants.specular_roughness = dielectric_specular(source.specular, roughness);
    constants.emissive_metallic = {0.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t flags = static_cast<std::uint32_t>(MaterialFlag::ReceiveShadow) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveAo) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveSsr) |
                          static_cast<std::uint32_t>(MaterialFlag::ReceiveGi);
    if (source.diffuse[3] < 0.999f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::AlphaTest);
    }
    if (source.edge_size > 0.0f && source.edge_color[3] > 0.0f) {
        flags |= static_cast<std::uint32_t>(MaterialFlag::Outline);
    }
    constants.custom_data = {0.0f, 0.0f, 1.0f,
                             pack_shading_model(0, static_cast<int>(flags))};
    constants.uv_transform = {1.0f, 1.0f, 0.0f, 0.0f};
    constants.edge_color_size = {source.edge_color[0], source.edge_color[1],
                                 source.edge_color[2], source.edge_size};
    constants.pmx_params = {static_cast<float>(std::min<std::uint8_t>(material.sphere_mode, 3)),
                            static_cast<float>(toon_slice_for_gpu(material)), 0.0f, 0.0f};
    return constants;
}

int shading_model_from_constants(const MaterialConstants& constants) {
    return unpack_shading_model(constants.custom_data[3]);
}

void set_shading_model(MaterialConstants& constants, int shading_model) {
    const int flags = unpack_material_flags(constants.custom_data[3]);
    constants.custom_data[3] = pack_shading_model(shading_model, flags);
}

int toon_slice_from_index(int toon_index) {
    return (toon_index >= 0 && toon_index < kBuiltinToonRampCount) ? toon_index + 1 : 0;
}

bool shading_model_scatters_subsurface(int shading_model) {
    return shading_model == static_cast<int>(ShadingModel::Skin) ||
           shading_model == static_cast<int>(ShadingModel::Subsurface);
}

MaterialResponse response_from_constants(const MaterialConstants& constants) {
    MaterialResponse response;
    response.albedo = std::array<float, 3>{constants.base_color[0], constants.base_color[1],
                                           constants.base_color[2]};
    response.specular = {constants.specular_roughness[0], constants.specular_roughness[1],
                         constants.specular_roughness[2]};
    response.roughness = constants.specular_roughness[3];
    response.emissive = {constants.emissive_metallic[0], constants.emissive_metallic[1],
                         constants.emissive_metallic[2]};
    response.metallic = constants.emissive_metallic[3];
    response.custom_a = constants.custom_data[0];
    response.custom_b = constants.custom_data[1];
    response.custom_b_color = {constants.custom_b[0], constants.custom_b[1], constants.custom_b[2]};
    const int feat = static_cast<int>(std::round(constants.custom_b[3]));
    response.procedural_hair = (feat & 1) != 0;
    response.glitter = (feat & 2) != 0;
    response.eye_parallax = (feat & 4) != 0;
    if ((feat & 8) != 0) {
        response.normal_map = "auto";
    }
    response.procedural_hair_scale = constants.feature_params0[0];
    response.procedural_hair_intensity = constants.feature_params0[1];
    response.eye_iris_depth = constants.feature_params0[2];
    response.eye_cornea_ior = constants.feature_params0[3];
    response.glitter_density = constants.feature_params1[0];
    response.glitter_roughness = constants.feature_params1[1];
    response.glitter_intensity = constants.feature_params1[2];

    response.shading_model = shading_model_from_constants(constants);
    const int flags = unpack_material_flags(constants.custom_data[3]);
    response.hashed_alpha = (flags & static_cast<int>(MaterialFlag::HashedAlpha)) != 0;
    response.hex_tiling_scale = constants.pmx_params[2];
    return response;
}

MaterialConstants apply_material_response(const MaterialConstants& authored,
                                          const MaterialResponse& response) {
    MaterialConstants constants = authored;
    if (response.albedo.has_value()) {
        constants.base_color[0] = (*response.albedo)[0];
        constants.base_color[1] = (*response.albedo)[1];
        constants.base_color[2] = (*response.albedo)[2];
    }
    constants.specular_roughness = {response.specular[0], response.specular[1],
                                    response.specular[2], response.roughness};
    constants.emissive_metallic = {response.emissive[0], response.emissive[1],
                                   response.emissive[2], response.metallic};
    constants.custom_data[0] = response.custom_a;
    constants.custom_data[1] = response.custom_b;

    std::array<float, 3> cb = response.custom_b_color;
    if (cb[0] == 0.0f && cb[1] == 0.0f && cb[2] == 0.0f && response.custom_b != 0.0f) {
        cb = {response.custom_b, response.custom_b, response.custom_b};
    }
    float feat = 0.0f;
    if (response.procedural_hair) feat += 1.0f;
    if (response.glitter) feat += 2.0f;
    if (response.eye_parallax) feat += 4.0f;
    if (response.normal_map == "auto") feat += 8.0f;

    constants.custom_b = {cb[0], cb[1], cb[2], feat};
    constants.feature_params0 = {response.procedural_hair_scale, response.procedural_hair_intensity,
                                 response.eye_iris_depth, response.eye_cornea_ior};
    constants.feature_params1 = {response.glitter_density, response.glitter_roughness,
                                 response.glitter_intensity, 0.0f};

    int flags = unpack_material_flags(constants.custom_data[3]);
    if (response.hashed_alpha) {
        flags |= static_cast<int>(MaterialFlag::HashedAlpha);
    } else {
        flags &= ~static_cast<int>(MaterialFlag::HashedAlpha);
    }
    constants.custom_data[3] = pack_shading_model(response.shading_model, flags);
    constants.pmx_params[2] = response.hex_tiling_scale;
    return constants;
}

// Order matches the ShadingModel enum; the array is indexed by the model id.
static constexpr std::string_view kShadingModelNames[] = {
    "default", "skin",  "emissive", "anisotropy", "glass",     "cloth",
    "coat",    "sss",   "cel",      "tone",       "eye",
};

std::string_view shading_model_name(int shading_model) {
    if (shading_model < 0 ||
        shading_model >= static_cast<int>(std::size(kShadingModelNames))) {
        return {};
    }
    return kShadingModelNames[static_cast<std::size_t>(shading_model)];
}

static constexpr std::string_view kShadingModelLabels[] = {
    "Default",    "Skin",       "Emissive", "Anisotropy", "Glass", "Cloth",
    "Clear coat", "Subsurface", "Cel",      "Tone based", "Eye",
};

std::string_view shading_model_label(int shading_model) {
    if (shading_model < 0 ||
        shading_model >= static_cast<int>(std::size(kShadingModelLabels))) {
        return {};
    }
    return kShadingModelLabels[static_cast<std::size_t>(shading_model)];
}

int shading_model_from_name(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kShadingModelNames); ++i) {
        if (kShadingModelNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool save_hkmat(std::string_view filepath, std::string_view name, const MaterialResponse& response) {
    matgen::MaterialFileData data;
    data.name = std::string(name);
    data.version = 1;
    data.response = response;
    return matgen::MaterialIO::save_file(filepath, data);
}

std::optional<MaterialResponse> load_hkmat(std::string_view filepath) {
    auto data = matgen::MaterialIO::load_file(filepath);
    if (!data) return std::nullopt;
    return data->response;
}

} // namespace hikari::gfx
