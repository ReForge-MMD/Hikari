#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "hikari/gfx/material_import.hpp"
#include "hikari/gfx/matgen/material_io.hpp"
#include "hikari/gfx/material_presets.hpp"

using namespace hikari::gfx;
using namespace hikari::gfx::matgen;

TEST_CASE("native hkmat serialization roundtrips response", "[matgen][io]") {
    MaterialResponse resp;
    resp.shading_model = static_cast<int>(ShadingModel::Cloth);
    resp.roughness = 0.72f;
    resp.metallic = 0.15f;
    resp.specular = {0.05f, 0.05f, 0.05f};
    resp.emissive = {0.1f, 0.2f, 0.3f};
    resp.custom_a = 0.65f;
    resp.custom_b = 0.40f;
    resp.hashed_alpha = true;
    resp.hex_tiling_scale = 8.0f;

    std::string json = MaterialIO::serialize_response("denim_test", resp);
    REQUIRE(!json.empty());
    REQUIRE(json.find("\"format\": \"hikari_material\"") != std::string::npos);
    REQUIRE(json.find("\"shading_model\": \"cloth\"") != std::string::npos);

    auto deserialized = MaterialIO::deserialize_hkmat(json);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->name == "denim_test");
    REQUIRE(deserialized->response.shading_model == static_cast<int>(ShadingModel::Cloth));
    REQUIRE(deserialized->response.roughness == Catch::Approx(0.72f));
    REQUIRE(deserialized->response.metallic == Catch::Approx(0.15f));
    REQUIRE(deserialized->response.specular[0] == Catch::Approx(0.05f));
    REQUIRE(deserialized->response.emissive[1] == Catch::Approx(0.2f));
    REQUIRE(deserialized->response.custom_a == Catch::Approx(0.65f));
    REQUIRE(deserialized->response.custom_b == Catch::Approx(0.40f));
    REQUIRE(deserialized->response.hashed_alpha == true);
    REQUIRE(deserialized->response.hex_tiling_scale == Catch::Approx(8.0f));
}

TEST_CASE("expanded presets library covers all categories", "[matgen][presets]") {
    const auto& presets = material_presets();
    REQUIRE(presets.size() >= 30);

    const MaterialPreset* denim = find_material_preset("denim");
    REQUIRE(denim != nullptr);
    REQUIRE(denim->category == "Cloth");
    REQUIRE(denim->model == ShadingModel::Cloth);
    REQUIRE(denim->hex_tiling_scale == Catch::Approx(6.0f));

    const MaterialPreset* skin = find_material_preset("anime_warm");
    REQUIRE(skin != nullptr);
    REQUIRE(skin->category == "Skin");
    REQUIRE(skin->model == ShadingModel::Skin);

    const MaterialPreset* hair = find_material_preset("anisotropic_realistic");
    REQUIRE(hair != nullptr);
    REQUIRE(hair->category == "Hair");
    REQUIRE(hair->model == ShadingModel::Anisotropy);
    REQUIRE(hair->hashed_alpha == true);

    const MaterialPreset* gold = find_material_preset("gold");
    REQUIRE(gold != nullptr);
    REQUIRE(gold->category == "Metallic");
    REQUIRE(gold->metallic == Catch::Approx(1.0f));

    const MaterialPreset* toon = find_material_preset("anime_cel");
    REQUIRE(toon != nullptr);
    REQUIRE(toon->category == "Toon");
    REQUIRE(toon->model == ShadingModel::Cel);
}

TEST_CASE("native hkmat serialization roundtrips advanced pbr features", "[matgen][io]") {
    MaterialResponse resp;
    resp.shading_model = static_cast<int>(ShadingModel::Skin);
    resp.albedo = std::array<float, 3>{1.0f, 0.782f, 0.344f};
    resp.custom_b_color = {0.86f, 0.35f, 0.31f};
    resp.procedural_hair = true;
    resp.procedural_hair_scale = 0.45f;
    resp.procedural_hair_intensity = 0.50f;
    resp.glitter = true;
    resp.glitter_density = 160.0f;
    resp.glitter_roughness = 0.50f;
    resp.glitter_intensity = 2.5f;
    resp.eye_parallax = true;
    resp.eye_iris_depth = 0.05f;
    resp.eye_cornea_ior = 1.376f;

    std::string json = MaterialIO::serialize_response("advanced_pbr_test", resp);
    auto deserialized = MaterialIO::deserialize_hkmat(json);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->response.albedo.has_value());
    REQUIRE((*deserialized->response.albedo)[0] == Catch::Approx(1.0f));
    REQUIRE((*deserialized->response.albedo)[1] == Catch::Approx(0.782f));
    REQUIRE((*deserialized->response.albedo)[2] == Catch::Approx(0.344f));
    REQUIRE(deserialized->response.custom_b_color[0] == Catch::Approx(0.86f));
    REQUIRE(deserialized->response.procedural_hair == true);
    REQUIRE(deserialized->response.procedural_hair_scale == Catch::Approx(0.45f));
    REQUIRE(deserialized->response.glitter == true);
    REQUIRE(deserialized->response.glitter_density == Catch::Approx(160.0f));
    REQUIRE(deserialized->response.eye_parallax == true);
    REQUIRE(deserialized->response.eye_cornea_ior == Catch::Approx(1.376f));
}

TEST_CASE("regenerate all 718 ray-mmd presets", "[.regenerate_presets]") {
    namespace fs = std::filesystem;
    const fs::path src_root = "ref-code/ray-mmd-reforge/Materials";
    const fs::path dst_root = "assets/materials/presets";
    if (!fs::exists(src_root)) {
        return;
    }
    fs::create_directories(dst_root);

    std::vector<std::string> qrc_entries;
    int converted = 0;

    for (const auto& entry : fs::recursive_directory_iterator(src_root)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".fx") continue;

        fs::path rel = fs::relative(p, src_root);
        fs::path out_file = dst_root / rel;
        out_file.replace_extension(".hkmat");
        fs::create_directories(out_file.parent_path());

        std::ifstream ifs(p, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string source = ss.str();

        auto res = import_ray_mmd_material(source);
        if (res.report.ok) {
            MaterialFileData data;
            data.name = p.stem().string();
            data.version = 1;
            data.response = res.response;
            if (MaterialIO::save_file(out_file.string(), data)) {
                converted++;
                std::string stem_posix = rel.parent_path().empty()
                    ? (p.stem().string() + ".hkmat")
                    : (rel.parent_path().generic_string() + "/" + p.stem().string() + ".hkmat");
                qrc_entries.push_back(stem_posix);
            }
        }
    }
    REQUIRE(converted >= 700);

    // Update materials.qrc
    std::sort(qrc_entries.begin(), qrc_entries.end());
    std::ofstream qrc("assets/materials/materials.qrc", std::ios::out | std::ios::trunc);
    qrc << "<!DOCTYPE RCC>\n<RCC version=\"1.0\">\n  <qresource prefix=\"/materials\">\n";
    for (const auto& f : qrc_entries) {
        qrc << "    <file alias=\"presets/" << f << "\">presets/" << f << "</file>\n";
    }
    qrc << "  </qresource>\n</RCC>\n";
}
