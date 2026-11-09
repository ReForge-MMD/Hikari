#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/settings.hpp"

using hikari::gfx::DiffuseBrdf;
using hikari::gfx::GraphicsSettings;
using hikari::gfx::QualityPreset;
using hikari::gfx::TonemapOperator;

TEST_CASE("graphics settings defaults validate cleanly", "[gfx][settings]") {
    GraphicsSettings settings;
    REQUIRE(settings.validate() == 0);
    REQUIRE(settings.renderer == hikari::gfx::RendererMode::Legacy);
    REQUIRE(settings.preset == QualityPreset::Medium);
}

TEST_CASE("graphics settings round-trip through flat JSON", "[gfx][settings]") {
    GraphicsSettings settings = GraphicsSettings::preset_values(QualityPreset::High);
    settings.renderer = hikari::gfx::RendererMode::Native;
    settings.sun.intensity = 4.25f;
    settings.sun.color_temperature = 5200.0f;
    settings.post.tonemap = TonemapOperator::AcesSrgb;
    settings.post.exposure = 1.75f;
    settings.sky.ambient_color = hikari::Vec3(0.1f, 0.2f, 0.3f);
    settings.brdf.diffuse_type = DiffuseBrdf::Burley;
    settings.ibl.custom_path = "C:\\sky\\room.hdr";
    settings.debug.view = hikari::gfx::DebugView::Normal;

    const std::string json = settings.to_json();
    GraphicsSettings restored;
    REQUIRE(restored.from_json(json));
    REQUIRE(restored.renderer == settings.renderer);
    REQUIRE(restored.preset == settings.preset);
    REQUIRE(restored.sun.intensity == settings.sun.intensity);
    REQUIRE(restored.sun.color_temperature == settings.sun.color_temperature);
    REQUIRE(restored.post.tonemap == settings.post.tonemap);
    REQUIRE(restored.post.exposure == settings.post.exposure);
    REQUIRE(restored.sky.ambient_color == settings.sky.ambient_color);
    REQUIRE(restored.brdf.diffuse_type == settings.brdf.diffuse_type);
    REQUIRE(restored.ibl.custom_path == settings.ibl.custom_path);
    REQUIRE(restored.debug.view == settings.debug.view);
}

TEST_CASE("graphics settings clamp invalid values", "[gfx][settings]") {
    GraphicsSettings settings;
    settings.sun.light_mode = 9;
    settings.sun.intensity = -4.0f;
    settings.post.exposure = 999.0f;
    settings.sky.time_of_day = 30.0f;
    settings.camera.blade_count = 100;
    REQUIRE(settings.validate() > 0);
    REQUIRE(settings.sun.light_mode == 2);
    REQUIRE(settings.sun.intensity == 0.0f);
    REQUIRE(settings.post.exposure <= 100.0f);
    REQUIRE(settings.sky.time_of_day == 0.0f);
    REQUIRE(settings.camera.blade_count == 16);
}

TEST_CASE("graphics settings permutation key tracks algorithm changes", "[gfx][settings]") {
    GraphicsSettings a;
    GraphicsSettings b;
    REQUIRE(a.permutation_key() == b.permutation_key());
    b.post.tonemap = TonemapOperator::Reinhard;
    REQUIRE(a.permutation_key() != b.permutation_key());
    a.post.tonemap = TonemapOperator::Reinhard;
    REQUIRE(a.permutation_key() == b.permutation_key());
}

TEST_CASE("preset values are valid and select the native renderer", "[gfx][settings]") {
    for (const QualityPreset preset :
         {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High,
          QualityPreset::Cinematic}) {
        GraphicsSettings settings = GraphicsSettings::preset_values(preset);
        REQUIRE(settings.renderer == hikari::gfx::RendererMode::Native);
        REQUIRE(settings.preset == preset);
        REQUIRE(settings.validate() == 0);
    }
}
