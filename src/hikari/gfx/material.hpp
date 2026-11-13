#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hikari/formats/pmx.hpp"
#include "hikari/render/material.hpp"

namespace hikari::gfx {

// Shading models of the shared surface library in shaders/gfx_core.hlsl. The
// integer travels in the whole part of MaterialConstants::custom_data[3], so
// the deferred lighting pass selects a surface function per pixel.
enum class ShadingModel : int {
    Default = 0,
    Skin = 1,
    Emissive = 2,
    Anisotropy = 3,
    Glass = 4,
    Cloth = 5,
    ClearCoat = 6,
    Subsurface = 7,
    Cel = 8,
    ToneBased = 9,
    Eye = 10,
};

// Must match the MaterialParams cbuffer in shaders/gfx_core.hlsl.
struct MaterialConstants {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> specular_roughness{0.04f, 0.04f, 0.04f, 0.5f};
    std::array<float, 4> emissive_metallic{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> custom_data{0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 4> uv_transform{1.0f, 1.0f, 0.0f, 0.0f};
    // Outline ink: rgb is the edge colour and a is the PMX edge size in model
    // units. The outline pass scales it by the global width setting.
    std::array<float, 4> edge_color_size{0.0f, 0.0f, 0.0f, 0.0f};
    // PMX texture slots that are not the diffuse map: x is the sphere map mode
    // (0 off, 1 multiply, 2 add, 3 sub-texture), y is the toon ramp slice in the
    // renderer's ramp array (0 means no ramp). z is hex-tiling scale, w is reserved.
    std::array<float, 4> pmx_params{0.0f, 0.0f, 0.0f, 0.0f};
    // custom_b vector (RGB) and feature flags in w:
    // bit 0 (1): procedural hair tangent perturbation
    // bit 1 (2): micro-glitter stochastic sparkle
    // bit 2 (4): eye cornea-iris parallax refraction
    // bit 3 (8): auto-normal from diffuse height
    std::array<float, 4> custom_b{0.0f, 0.0f, 0.0f, 0.0f};
    // Feature parameters:
    // params0: x = hair_scale, y = hair_intensity, z = eye_iris_depth, w = eye_cornea_ior
    std::array<float, 4> feature_params0{1.0f, 0.5f, 0.05f, 1.376f};
    // params1: x = glitter_density, y = glitter_roughness, z = glitter_intensity, w = reserved
    std::array<float, 4> feature_params1{150.0f, 0.55f, 2.0f, 0.0f};
};

float roughness_from_shininess(float shininess);
std::uint32_t material_flags_from_pmx(const formats::PmxMaterial& material);
MaterialConstants material_from_pmx(const formats::PmxMaterial& material);
MaterialConstants material_from_gpu(const render::GpuMaterial& material);

// The response half of a material: everything a preset or the Materials panel
// can change. The authored colour, edge ink, UV transform, PMX slots and flags
// stay with the material itself, so a per-subset override does not freeze them
// and material morphs keep reaching the surface.
struct MaterialResponse {
    std::optional<std::array<float, 3>> albedo;
    std::array<float, 3> specular{0.04f, 0.04f, 0.04f};
    float roughness = 0.5f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float metallic = 0.0f;
    // CustomData.x/y, the two free floats a PMX material cannot express.
    float custom_a = 0.0f;
    float custom_b = 0.0f;
    std::array<float, 3> custom_b_color{0.0f, 0.0f, 0.0f};
    int shading_model = 0;
    bool hashed_alpha = false;
    float hex_tiling_scale = 0.0f;
    std::string albedo_map{};
    std::string normal_map{};
    std::string roughness_map{};
    std::string metallic_map{};
    std::string emissive_map{};
    std::string occlusion_map{};

    // Advanced procedural & PBR parameters
    bool procedural_hair = false;
    float procedural_hair_scale = 1.0f;
    float procedural_hair_intensity = 0.5f;

    bool glitter = false;
    float glitter_density = 150.0f;
    float glitter_roughness = 0.55f;
    float glitter_intensity = 2.0f;

    bool eye_parallax = false;
    float eye_iris_depth = 0.05f;
    float eye_cornea_ior = 1.376f;
};

MaterialResponse response_from_constants(const MaterialConstants& constants);
// Overlays the response on an authored material and keeps its colour, edge,
// UV transform, PMX slots and material flags.
MaterialConstants apply_material_response(const MaterialConstants& authored,
                                          const MaterialResponse& response);

// Reads the shading model back out of the packed custom_data[3] word.
int shading_model_from_constants(const MaterialConstants& constants);
// Replaces the shading model and keeps the material flags already packed next
// to it. Values outside 0..15 are clamped by the packing.
void set_shading_model(MaterialConstants& constants, int shading_model);

// Built-in PMX toon ramps are addressed by their index in the model file. The
// renderer stores them one slice after zero, so index 0 becomes slice 1 and the
// zero slice stays free for "no ramp".
inline constexpr int kBuiltinToonRampCount = 10;
int toon_slice_from_index(int toon_index);

// Shading models whose lit image is diffused by the subsurface pass. The pass
// blends a blurred copy of the lit image over the sharp one, so only these
// models can carry a second copy of their own albedo into the frame.
bool shading_model_scatters_subsurface(int shading_model);
// Blend weight of that composite. The pass lerps, so the weight has to stay at
// or below one; above it the diffused copy would be added on top of the lit
// albedo and every skin and cloth surface would read twice as bright.
inline constexpr float kSubsurfaceCompositeStrength = 0.55f;

// Stable lowercase identifier of a shading model, used by the studio panels and
// by the project file so a saved override survives a reordering of the enum.
std::string_view shading_model_name(int shading_model);
// Human readable label of the same model, for the Materials panel.
std::string_view shading_model_label(int shading_model);
// Parses an identifier written by shading_model_name; -1 when unknown.
int shading_model_from_name(std::string_view name);

// High-level native Hikari Material (.hkmat) file I/O
bool save_hkmat(std::string_view filepath, std::string_view name, const MaterialResponse& response);
std::optional<MaterialResponse> load_hkmat(std::string_view filepath);

} // namespace hikari::gfx
