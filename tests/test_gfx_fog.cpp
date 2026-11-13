#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/fog.hpp"
#include "hikari/gfx/settings.hpp"

namespace gfx = hikari::gfx;

TEST_CASE("fog raymarch budget follows enable and quality") {
    // Off and the analytical path integrate the medium in closed form, so they
    // march nothing and must not request a step budget.
    REQUIRE(gfx::fog_step_count(0, 0) == 0);
    REQUIRE(gfx::fog_step_count(0, 3) == 0);
    REQUIRE(gfx::fog_step_count(1, 0) == 0);
    REQUIRE(gfx::fog_step_count(1, 3) == 0);

    REQUIRE(gfx::fog_step_count(2, 0) == 12);
    REQUIRE(gfx::fog_step_count(2, 1) == 20);
    REQUIRE(gfx::fog_step_count(2, 2) == 32);
    REQUIRE(gfx::fog_step_count(2, 3) == 48);
    // Mode 3 is the same volumetric march at a higher quality ceiling, so the
    // budget is a function of the quality index only.
    REQUIRE(gfx::fog_step_count(3, 0) == gfx::fog_step_count(2, 0));
    REQUIRE(gfx::fog_step_count(3, 3) == 48);
}

TEST_CASE("fog raymarch budget clamps out-of-range quality") {
    REQUIRE(gfx::fog_step_count(2, -5) == 12);
    REQUIRE(gfx::fog_step_count(2, 99) == 48);
    REQUIRE(gfx::fog_step_count(4, 2) == 32);
}

TEST_CASE("fog raymarch budget grows monotonically with quality") {
    for (int quality = 0; quality < 3; ++quality) {
        REQUIRE(gfx::fog_step_count(2, quality) < gfx::fog_step_count(2, quality + 1));
    }
}

TEST_CASE("fog raymarch distance covers a room scale cutoff") {
    const float distance = gfx::fog_raymarch_max_distance();
    REQUIRE(distance > 0.0f);
    // The analytical tail continues past the marched span, so the budget must be
    // long enough to reach the far end of an ordinary scene before it takes over.
    REQUIRE(distance >= 100.0f);
}

TEST_CASE("fog settings clamp invalid values") {
    gfx::GraphicsSettings settings;
    settings.fog.enable = 9;
    settings.fog.quality = -3;
    settings.fog.debug = 12;
    settings.fog.density = -1.0f;
    settings.fog.sun_intensity = 5000.0f;
    settings.fog.mie_phase = 4.0f;
    settings.fog.godray_intensity = -2.0f;
    REQUIRE(settings.validate() > 0);
    REQUIRE(settings.fog.enable == 3);
    REQUIRE(settings.fog.quality == 0);
    REQUIRE(settings.fog.debug == 4);
    REQUIRE(settings.fog.density == 0.0f);
    REQUIRE(settings.fog.sun_intensity == 100.0f);
    REQUIRE(settings.fog.mie_phase == 1.0f);
    REQUIRE(settings.fog.godray_intensity == 0.0f);
}

TEST_CASE("fog settings round-trip through flat JSON") {
    gfx::GraphicsSettings settings;
    settings.fog.enable = 3;
    settings.fog.quality = 2;
    settings.fog.debug = 4;
    settings.fog.cloud_shadow = false;
    settings.fog.density = 0.0125f;
    settings.fog.height = 3.5f;
    settings.fog.falloff = 0.33f;
    settings.fog.ground_density = 0.2f;
    settings.fog.ground_height = -1.25f;
    settings.fog.ground_falloff = 0.7f;
    settings.fog.start_distance = 1.5f;
    settings.fog.cutoff_distance = 120.0f;
    settings.fog.max_opacity = 0.8f;
    settings.fog.color = hikari::Vec3(0.2f, 0.3f, 0.4f);
    settings.fog.sun_color = hikari::Vec3(1.0f, 0.9f, 0.8f);
    settings.fog.sun_intensity = 12.0f;
    settings.fog.mie_phase = 0.55f;
    settings.fog.mist_noise = 0.9f;
    settings.fog.wind_speed = 2.5f;
    settings.fog.godray_intensity = 3.5f;
    settings.fog.ray_density = 0.25f;

    const std::string json = settings.to_json();
    gfx::GraphicsSettings restored;
    REQUIRE(restored.from_json(json));
    REQUIRE(restored.fog.enable == settings.fog.enable);
    REQUIRE(restored.fog.quality == settings.fog.quality);
    REQUIRE(restored.fog.debug == settings.fog.debug);
    REQUIRE(restored.fog.cloud_shadow == settings.fog.cloud_shadow);
    REQUIRE(restored.fog.density == settings.fog.density);
    REQUIRE(restored.fog.height == settings.fog.height);
    REQUIRE(restored.fog.falloff == settings.fog.falloff);
    REQUIRE(restored.fog.ground_density == settings.fog.ground_density);
    REQUIRE(restored.fog.ground_height == settings.fog.ground_height);
    REQUIRE(restored.fog.ground_falloff == settings.fog.ground_falloff);
    REQUIRE(restored.fog.start_distance == settings.fog.start_distance);
    REQUIRE(restored.fog.cutoff_distance == settings.fog.cutoff_distance);
    REQUIRE(restored.fog.max_opacity == settings.fog.max_opacity);
    REQUIRE(restored.fog.color == settings.fog.color);
    REQUIRE(restored.fog.sun_color == settings.fog.sun_color);
    REQUIRE(restored.fog.sun_intensity == settings.fog.sun_intensity);
    REQUIRE(restored.fog.mie_phase == settings.fog.mie_phase);
    REQUIRE(restored.fog.mist_noise == settings.fog.mist_noise);
    REQUIRE(restored.fog.wind_speed == settings.fog.wind_speed);
    REQUIRE(restored.fog.godray_intensity == settings.fog.godray_intensity);
    REQUIRE(restored.fog.ray_density == settings.fog.ray_density);
}

TEST_CASE("fog settings change the shader permutation key") {
    gfx::GraphicsSettings off;
    gfx::GraphicsSettings volumetric = off;
    volumetric.fog.enable = 3;
    REQUIRE(off.permutation_key() != volumetric.permutation_key());

    gfx::GraphicsSettings debug = volumetric;
    debug.fog.debug = 4;
    REQUIRE(debug.permutation_key() != volumetric.permutation_key());

    gfx::GraphicsSettings no_cloud_shadow = volumetric;
    no_cloud_shadow.fog.cloud_shadow = !volumetric.fog.cloud_shadow;
    REQUIRE(no_cloud_shadow.permutation_key() != volumetric.permutation_key());
}

TEST_CASE("quality presets scale the fog budget") {
    REQUIRE(gfx::GraphicsSettings::preset_values(gfx::QualityPreset::Medium).fog.enable == 2);
    REQUIRE(gfx::GraphicsSettings::preset_values(gfx::QualityPreset::Medium).fog.quality == 1);
    REQUIRE(gfx::GraphicsSettings::preset_values(gfx::QualityPreset::High).fog.quality == 2);
    REQUIRE(gfx::GraphicsSettings::preset_values(gfx::QualityPreset::Cinematic).fog.enable == 3);
    REQUIRE(gfx::GraphicsSettings::preset_values(gfx::QualityPreset::Cinematic).fog.quality == 3);
}