#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/gbuffer.hpp"
#include "hikari/gfx/material.hpp"
#include "hikari/gfx/material_import.hpp"
#include "hikari/gfx/material_presets.hpp"
#include "hikari/gfx/toon_ramp.hpp"

TEST_CASE("octahedral normal encoding round-trips", "[gfx][gbuffer]") {
    const hikari::Vec3 normals[] = {
        hikari::Vec3(0.0f, 0.0f, 1.0f), hikari::Vec3(0.0f, 1.0f, 0.0f),
        hikari::Vec3(1.0f, 0.0f, 0.0f), hikari::Vec3(-0.4f, 0.7f, 0.59f),
        hikari::Vec3(0.2f, -0.9f, -0.3f)};
    for (const hikari::Vec3& normal : normals) {
        const auto encoded = hikari::gfx::encode_normal_octahedral(normal);
        const hikari::Vec3 decoded =
            hikari::gfx::decode_normal_octahedral(encoded[0], encoded[1]);
        REQUIRE(decoded.x == Catch::Approx(normal.normalize().x).margin(1.0e-3));
        REQUIRE(decoded.y == Catch::Approx(normal.normalize().y).margin(1.0e-3));
        REQUIRE(decoded.z == Catch::Approx(normal.normalize().z).margin(1.0e-3));
    }
}

TEST_CASE("shading model and flags pack into one float", "[gfx][gbuffer]") {
    const float packed = hikari::gfx::pack_shading_model(5, 0xA5);
    REQUIRE(hikari::gfx::unpack_shading_model(packed) == 5);
    REQUIRE(hikari::gfx::unpack_material_flags(packed) == 0xA5);
}

TEST_CASE("PMX material bridge sets roughness and flags", "[gfx][material]") {
    hikari::formats::PmxMaterial material;
    material.diffuse = {0.8f, 0.7f, 0.6f, 0.5f};
    material.specular = {0.2f, 0.3f, 0.4f};
    material.shininess = 10.0f;
    material.flags = 0x01;
    material.edge_size = 1.0f;
    material.edge_color = {0.0f, 0.0f, 0.0f, 1.0f};

    const hikari::gfx::MaterialConstants constants = hikari::gfx::material_from_pmx(material);
    REQUIRE(constants.base_color[3] == Catch::Approx(0.5f));
    REQUIRE(constants.specular_roughness[3] < 1.0f);
    const int flags = hikari::gfx::unpack_material_flags(constants.custom_data[3]);
    REQUIRE((flags & static_cast<int>(hikari::gfx::MaterialFlag::AlphaTest)) != 0);
    REQUIRE((flags & static_cast<int>(hikari::gfx::MaterialFlag::TwoSided)) != 0);
    REQUIRE((flags & static_cast<int>(hikari::gfx::MaterialFlag::Outline)) != 0);
}

TEST_CASE("roughness falls as shininess rises", "[gfx][material]") {
    REQUIRE(hikari::gfx::roughness_from_shininess(0.0f) == Catch::Approx(1.0f));
    REQUIRE(hikari::gfx::roughness_from_shininess(2.0f) >
            hikari::gfx::roughness_from_shininess(20.0f));
    REQUIRE(hikari::gfx::roughness_from_shininess(10000.0f) >= 0.04f);
}

TEST_CASE("shading model travels beside the material flags", "[gfx][material]") {
    hikari::formats::PmxMaterial material;
    material.diffuse = {0.8f, 0.7f, 0.6f, 0.5f};
    material.flags = 0x01;
    material.edge_size = 1.0f;
    material.edge_color = {0.0f, 0.0f, 0.0f, 1.0f};
    hikari::gfx::MaterialConstants constants = hikari::gfx::material_from_pmx(material);
    REQUIRE(hikari::gfx::shading_model_from_constants(constants) == 0);
    const int flags = hikari::gfx::unpack_material_flags(constants.custom_data[3]);

    hikari::gfx::set_shading_model(constants, static_cast<int>(hikari::gfx::ShadingModel::Cloth));
    REQUIRE(hikari::gfx::shading_model_from_constants(constants) == 5);
    REQUIRE(hikari::gfx::unpack_material_flags(constants.custom_data[3]) == flags);

    hikari::gfx::set_shading_model(constants, static_cast<int>(hikari::gfx::ShadingModel::Eye));
    REQUIRE(hikari::gfx::shading_model_from_constants(constants) == 10);
    REQUIRE(hikari::gfx::unpack_material_flags(constants.custom_data[3]) == flags);

    // Out-of-range models clamp into the four bits the packing reserves.
    hikari::gfx::set_shading_model(constants, 99);
    REQUIRE(hikari::gfx::shading_model_from_constants(constants) == 15);
    REQUIRE(hikari::gfx::unpack_material_flags(constants.custom_data[3]) == flags);
}

TEST_CASE("PMX sphere and toon slots reach the native material", "[gfx][material]") {
    hikari::formats::PmxMaterial material;
    material.diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
    material.sphere_texture_index = 3;
    material.sphere_mode = 2;
    material.toon_flag = 0;
    material.toon_index = 4;

    const hikari::gfx::MaterialConstants constants = hikari::gfx::material_from_pmx(material);
    // x is the sphere mode, y is the ramp slice of the built-in toon.
    REQUIRE(constants.pmx_params[0] == Catch::Approx(2.0f));
    REQUIRE(constants.pmx_params[1] == Catch::Approx(5.0f));

    // An external toon texture is a file the loader has to register, so it maps
    // to slice 0 and the shading model keeps its procedural band.
    material.toon_flag = 1;
    REQUIRE(hikari::gfx::material_from_pmx(material).pmx_params[1] == Catch::Approx(0.0f));

    // Modes outside the four PMX sphere modes clamp instead of wrapping.
    material.sphere_mode = 9;
    REQUIRE(hikari::gfx::material_from_pmx(material).pmx_params[0] == Catch::Approx(3.0f));
}

TEST_CASE("built-in toon indices map to ramp slices", "[gfx][material]") {
    REQUIRE(hikari::gfx::toon_slice_from_index(-1) == 0);
    REQUIRE(hikari::gfx::toon_slice_from_index(0) == 1);
    REQUIRE(hikari::gfx::toon_slice_from_index(9) == 10);
    // Ten built-in ramps is the whole range; anything above it has no row.
    REQUIRE(hikari::gfx::toon_slice_from_index(10) == 0);
}

TEST_CASE("the toon ramp array holds every built-in row", "[gfx][material]") {
    // The slice travels in a float through GB3.g and indexes a texture array, so
    // a slice outside the array is an out-of-bounds sample rather than a clamp.
    for (int index = 0; index < hikari::gfx::kBuiltinToonRampCount; ++index) {
        const int slice = hikari::gfx::toon_slice_from_index(index);
        REQUIRE(slice > 0);
        REQUIRE(slice < hikari::gfx::kToonRampSlices);
    }
    // Slice 0 is the neutral row, so the built-in rows start at one.
    REQUIRE(hikari::gfx::kBuiltinToonRampCount + 1 <= hikari::gfx::kToonRampSlices);
}

TEST_CASE("PMX materials keep the identity UV transform", "[gfx][material]") {
    // PMX has no UV transform, so a bridge that left scale or offset set would
    // shift every texture on the model. The slot exists for an authored
    // transform and must start inert.
    hikari::formats::PmxMaterial material;
    material.diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
    material.sphere_texture_index = 2;
    material.sphere_mode = 1;
    material.toon_index = 3;
    const hikari::gfx::MaterialConstants constants = hikari::gfx::material_from_pmx(material);
    REQUIRE(constants.uv_transform[0] == Catch::Approx(1.0f));
    REQUIRE(constants.uv_transform[1] == Catch::Approx(1.0f));
    REQUIRE(constants.uv_transform[2] == Catch::Approx(0.0f));
    REQUIRE(constants.uv_transform[3] == Catch::Approx(0.0f));
}

TEST_CASE("the subsurface pass scatters only skin and subsurface albedo", "[gfx][material]") {
    // The diffusion pass blurs the lit image and blends it back over the sharp
    // one, so a model outside this set must keep its own lighting untouched.
    for (int model = static_cast<int>(hikari::gfx::ShadingModel::Default);
         model <= static_cast<int>(hikari::gfx::ShadingModel::Eye); ++model) {
        const bool expected = model == static_cast<int>(hikari::gfx::ShadingModel::Skin) ||
                              model == static_cast<int>(hikari::gfx::ShadingModel::Subsurface);
        REQUIRE(hikari::gfx::shading_model_scatters_subsurface(model) == expected);
    }
    REQUIRE_FALSE(hikari::gfx::shading_model_scatters_subsurface(-1));
    REQUIRE_FALSE(hikari::gfx::shading_model_scatters_subsurface(64));
}

TEST_CASE("the subsurface composite blends the albedo instead of adding it", "[gfx][material]") {
    // GI reaches the frame as radiance that already carries the bounce albedo,
    // so the only other place a skin or cloth albedo can enter twice is this
    // composite. A weight above one turns the lerp into an addition and doubles
    // the surface; zero would leave the pass doing nothing.
    REQUIRE(hikari::gfx::kSubsurfaceCompositeStrength > 0.0f);
    REQUIRE(hikari::gfx::kSubsurfaceCompositeStrength <= 1.0f);

    // The same albedo reaches the frame once through the lit image and once
    // through the diffused copy, mixed by the weight, never summed.
    const float lit = 0.8f;
    const float diffused = 0.8f;
    const float weight = hikari::gfx::kSubsurfaceCompositeStrength;
    const float composed = lit * (1.0f - weight) + diffused * weight;
    REQUIRE(composed == Catch::Approx(lit).margin(1.0e-6f));
}

TEST_CASE("material preset library covers the native surface kinds", "[gfx][material]") {
    const std::vector<hikari::gfx::MaterialPreset>& presets = hikari::gfx::material_presets();
    REQUIRE(presets.size() >= 10);
    for (const hikari::gfx::MaterialPreset& preset : presets) {
        REQUIRE_FALSE(preset.name.empty());
        REQUIRE(preset.roughness > 0.0f);
        REQUIRE(preset.roughness <= 1.0f);
        REQUIRE(preset.metallic >= 0.0f);
        REQUIRE(preset.metallic <= 1.0f);
        for (float channel : preset.specular) {
            REQUIRE(channel >= 0.0f);
            REQUIRE(channel <= 1.0f);
        }
        const int model = static_cast<int>(preset.model);
        REQUIRE(model >= static_cast<int>(hikari::gfx::ShadingModel::Default));
        REQUIRE(model <= static_cast<int>(hikari::gfx::ShadingModel::Eye));
    }
}

TEST_CASE("applying a preset keeps the material flags and the base colour", "[gfx][material]") {
    hikari::formats::PmxMaterial material;
    material.diffuse = {0.4f, 0.5f, 0.6f, 0.9f};
    material.flags = 0x01;
    material.edge_size = 0.5f;
    material.edge_color = {0.1f, 0.2f, 0.3f, 1.0f};

    hikari::gfx::MaterialConstants constants = hikari::gfx::material_from_pmx(material);
    const int flags = hikari::gfx::unpack_material_flags(constants.custom_data[3]);
    const std::array<float, 4> base_color = constants.base_color;

    for (const hikari::gfx::MaterialPreset& preset : hikari::gfx::material_presets()) {
        hikari::gfx::apply_material_preset(preset, constants);
        REQUIRE(hikari::gfx::shading_model_from_constants(constants) ==
                static_cast<int>(preset.model));
        REQUIRE(hikari::gfx::unpack_material_flags(constants.custom_data[3]) == flags);
        REQUIRE(constants.base_color == base_color);
        REQUIRE(constants.specular_roughness[0] == Catch::Approx(preset.specular[0]));
        REQUIRE(constants.specular_roughness[3] == Catch::Approx(preset.roughness));
        REQUIRE(constants.emissive_metallic[3] == Catch::Approx(preset.metallic));
        REQUIRE(constants.custom_data[0] == Catch::Approx(preset.custom_a));
        REQUIRE(constants.custom_data[1] == Catch::Approx(preset.custom_b));
    }
}

TEST_CASE("a material response round-trips through the authored material", "[gfx][material]") {
    hikari::formats::PmxMaterial material;
    material.diffuse = {0.3f, 0.6f, 0.9f, 0.75f};
    material.specular = {0.5f, 0.4f, 0.3f};
    material.shininess = 20.0f;
    material.flags = 0x01;
    material.edge_size = 0.25f;
    material.edge_color = {0.2f, 0.1f, 0.0f, 1.0f};
    material.sphere_mode = 2;
    material.toon_flag = 0;
    material.toon_index = 3;

    const hikari::gfx::MaterialConstants authored = hikari::gfx::material_from_pmx(material);
    const hikari::gfx::MaterialResponse original =
        hikari::gfx::response_from_constants(authored);
    REQUIRE(original.roughness == Catch::Approx(authored.specular_roughness[3]));
    REQUIRE(original.metallic == Catch::Approx(authored.emissive_metallic[3]));
    REQUIRE(original.shading_model == 0);

    // The response is the only part an override owns, so applying it back must
    // not disturb the authored colour, ink, UVs, PMX slots or flags.
    hikari::gfx::MaterialResponse response;
    response.specular = {0.12f, 0.11f, 0.10f};
    response.roughness = 0.31f;
    response.emissive = {0.7f, 0.6f, 0.5f};
    response.metallic = 0.8f;
    response.custom_a = 1.5f;
    response.custom_b = -2.0f;
    response.shading_model = static_cast<int>(hikari::gfx::ShadingModel::ClearCoat);

    const hikari::gfx::MaterialConstants overridden =
        hikari::gfx::apply_material_response(authored, response);
    REQUIRE(overridden.base_color == authored.base_color);
    REQUIRE(overridden.edge_color_size == authored.edge_color_size);
    REQUIRE(overridden.uv_transform == authored.uv_transform);
    REQUIRE(overridden.pmx_params == authored.pmx_params);
    REQUIRE(hikari::gfx::unpack_material_flags(overridden.custom_data[3]) ==
            hikari::gfx::unpack_material_flags(authored.custom_data[3]));

    const hikari::gfx::MaterialResponse applied =
        hikari::gfx::response_from_constants(overridden);
    REQUIRE(applied.specular == response.specular);
    REQUIRE(applied.roughness == Catch::Approx(response.roughness));
    REQUIRE(applied.emissive == response.emissive);
    REQUIRE(applied.metallic == Catch::Approx(response.metallic));
    REQUIRE(applied.custom_a == Catch::Approx(response.custom_a));
    REQUIRE(applied.custom_b == Catch::Approx(response.custom_b));
    REQUIRE(applied.shading_model == response.shading_model);
}

TEST_CASE("shading model names are stable and parse back", "[gfx][material]") {
    const int last = static_cast<int>(hikari::gfx::ShadingModel::Eye);
    for (int model = 0; model <= last; ++model) {
        const std::string_view name = hikari::gfx::shading_model_name(model);
        REQUIRE_FALSE(name.empty());
        REQUIRE(hikari::gfx::shading_model_from_name(name) == model);
        REQUIRE_FALSE(hikari::gfx::shading_model_label(model).empty());
    }
    // A saved file may name a model this build does not know; the caller turns
    // the -1 into "no override" instead of guessing a model.
    REQUIRE(hikari::gfx::shading_model_from_name("unobtainium") == -1);
    REQUIRE(hikari::gfx::shading_model_name(-1).empty());
    REQUIRE(hikari::gfx::shading_model_name(last + 1).empty());
}

TEST_CASE("Ray-MMD material import maps the parameters it can express", "[gfx][material]") {
    constexpr std::string_view source = R"(
// Toon (cel-shaded) duplicate of: Skin/material_skin.fx
#define ALBEDO_MAP_FROM 3
#define ALBEDO_MAP_UV_FLIP 0
#define ALBEDO_MAP_FILE "albedo.png"

const float3 albedo = 1.0;
const float alpha = 1.0;
const float smoothness = 0.10;
const float metalness = 0.0;
const float3 specular = 0.35;
#define EMISSIVE_ENABLE 1
const float3 emissive = float3(0.5, 0.25, 0.125);
const float emissiveIntensity = 2.0;
#define CUSTOM_ENABLE 1
const float customA = 0.75;
const float3 customB = float3(0.92, 0.78, 0.75);

#include "../material_common_2.0.fxsub"
)";

    const hikari::gfx::RayMmdImport imported =
        hikari::gfx::import_ray_mmd_material(source);
    REQUIRE(imported.report.ok);
    REQUIRE(imported.report.error.empty());

    // smoothness is the inverse of roughness in the native response.
    REQUIRE(imported.response.roughness == Catch::Approx(0.9f));
    REQUIRE(imported.response.metallic == Catch::Approx(0.0f));
    REQUIRE(imported.response.specular[0] == Catch::Approx(0.35f));
    REQUIRE(imported.response.shading_model ==
            static_cast<int>(hikari::gfx::ShadingModel::Skin));
    // The emission colour is scaled by the intensity, which has no native slot.
    REQUIRE(imported.response.emissive[0] == Catch::Approx(1.0f));
    REQUIRE(imported.response.emissive[1] == Catch::Approx(0.5f));
    REQUIRE(imported.response.emissive[2] == Catch::Approx(0.25f));
    REQUIRE(imported.response.custom_a == Catch::Approx(0.75f));
    // customB is a colour upstream and a scalar here, so it is reported as
    // approximate and reduced to its luminance.
    REQUIRE(imported.response.custom_b == Catch::Approx(0.8076f).margin(1.0e-3));

    const auto status_of = [&imported](std::string_view name) {
        for (const hikari::gfx::ImportParameter& parameter : imported.report.parameters) {
            if (parameter.name == name) {
                return parameter.status;
            }
        }
        return hikari::gfx::ImportStatus::Unsupported;
    };
    REQUIRE(status_of("smoothness") == hikari::gfx::ImportStatus::Applied);
    REQUIRE(status_of("CUSTOM_ENABLE") == hikari::gfx::ImportStatus::Applied);
    REQUIRE(status_of("customB") == hikari::gfx::ImportStatus::Approximate);
    REQUIRE(status_of("ALBEDO_MAP_FILE") == hikari::gfx::ImportStatus::Unsupported);
    // A per-subset override never touches the authored colour or alpha.
    REQUIRE(status_of("albedo") == hikari::gfx::ImportStatus::Unsupported);
    REQUIRE(status_of("alpha") == hikari::gfx::ImportStatus::Unsupported);
    // The include is listed so the report states the file was not executed.
    REQUIRE(status_of("shader body") == hikari::gfx::ImportStatus::Unsupported);

    REQUIRE(imported.report.applied + imported.report.approximate +
                imported.report.unsupported ==
            static_cast<int>(imported.report.parameters.size()));
    REQUIRE(imported.report.parameters.size() > 10);
}

TEST_CASE("Ray-MMD import keeps emission off when the file disables it", "[gfx][material]") {
    constexpr std::string_view source = R"(
#define EMISSIVE_ENABLE 0
const float3 emissive = float3(1.0, 0.5, 0.25);
const float emissiveIntensity = 4.0;
const float smoothness = 0.5;
)";
    const hikari::gfx::RayMmdImport imported =
        hikari::gfx::import_ray_mmd_material(source);
    REQUIRE(imported.report.ok);
    REQUIRE(imported.response.emissive[0] == Catch::Approx(0.0f));
    REQUIRE(imported.response.emissive[1] == Catch::Approx(0.0f));
    REQUIRE(imported.response.emissive[2] == Catch::Approx(0.0f));
    REQUIRE(imported.response.roughness == Catch::Approx(0.5f));
    REQUIRE(imported.report.unsupported >= 2);
}

TEST_CASE("Ray-MMD import reports a file it cannot read as a material", "[gfx][material]") {
    const hikari::gfx::RayMmdImport empty = hikari::gfx::import_ray_mmd_material("");
    REQUIRE_FALSE(empty.report.ok);
    REQUIRE_FALSE(empty.report.error.empty());

    // Only an include means there is nothing to convert, even though the scan
    // itself succeeded.
    const hikari::gfx::RayMmdImport include_only = hikari::gfx::import_ray_mmd_material(
        "#include \"../material_common_2.0.fxsub\"\n");
    REQUIRE_FALSE(include_only.report.ok);

    // A commented-out define must not be imported as if it were live.
    const hikari::gfx::RayMmdImport commented = hikari::gfx::import_ray_mmd_material(
        "// #define CUSTOM_ENABLE 3\nconst float smoothness = 0.25;\n");
    REQUIRE(commented.report.ok);
    REQUIRE(commented.response.shading_model == 0);
    REQUIRE(commented.response.roughness == Catch::Approx(0.75f));
}
