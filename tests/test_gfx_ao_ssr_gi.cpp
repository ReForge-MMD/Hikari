#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/ao_ssr_gi.hpp"
#include "hikari/gfx/gbuffer.hpp"
#include "hikari/gfx/material.hpp"
#include "hikari/gfx/settings.hpp"

namespace gfx = hikari::gfx;
namespace formats = hikari::formats;

namespace {

gfx::GraphicsSettings validated(gfx::GraphicsSettings settings) {
    settings.validate();
    return settings;
}

} // namespace

TEST_CASE("ao quality maps to sample budget") {
    // SSDO traces a hemisphere per pixel; the horizon algorithms use slices.
    REQUIRE(gfx::ao_sample_count(0, 0) == 0);
    REQUIRE(gfx::ao_sample_count(0, 1) == 8);
    REQUIRE(gfx::ao_sample_count(0, 6) == 28);
    REQUIRE(gfx::ao_sample_count(1, 6) == 0);
    REQUIRE(gfx::ao_sample_count(2, 6) == 0);

    REQUIRE(gfx::ao_slice_count(0, 6) == 0);
    REQUIRE(gfx::ao_slice_count(1, 1) == 4);
    REQUIRE(gfx::ao_slice_count(1, 6) == 8);
    REQUIRE(gfx::ao_slice_count(2, 1) == 2);
    REQUIRE(gfx::ao_slice_count(2, 6) == 12);

    REQUIRE(gfx::ao_step_count(0, 6) == 0);
    REQUIRE(gfx::ao_step_count(1, 1) == 4);
    REQUIRE(gfx::ao_step_count(2, 6) == 8);

    REQUIRE(gfx::ao_blur_radius(0) == 0);
    REQUIRE(gfx::ao_blur_radius(1) == 4);
    REQUIRE(gfx::ao_blur_radius(6) == 6);
}

TEST_CASE("ao sample budget grows monotonically with quality") {
    for (int quality = 1; quality < 6; ++quality) {
        REQUIRE(gfx::ao_sample_count(0, quality) <= gfx::ao_sample_count(0, quality + 1));
        REQUIRE(gfx::ao_slice_count(2, quality) <= gfx::ao_slice_count(2, quality + 1));
        REQUIRE(gfx::ao_step_count(1, quality) <= gfx::ao_step_count(1, quality + 1));
    }
}

TEST_CASE("ssr tap budget follows the quality table") {
    REQUIRE(gfx::ssr_tap_budget(0) == 0);
    REQUIRE(gfx::ssr_tap_budget(1) == 128);
    REQUIRE(gfx::ssr_tap_budget(2) == 256);
    REQUIRE(gfx::ssr_tap_budget(3) == 384);
    REQUIRE(gfx::ssr_tap_budget(4) == 512);
    // Out-of-range values clamp to the last entry instead of indexing past it.
    REQUIRE(gfx::ssr_tap_budget(7) == 512);
}

TEST_CASE("gi ray and step budgets follow enable and quality") {
    REQUIRE(gfx::gi_ray_count(0, 4) == 0);
    REQUIRE(gfx::gi_step_count(0, 4) == 0);
    REQUIRE(gfx::gi_ray_count(1, 1) == 4);
    REQUIRE(gfx::gi_ray_count(2, 4) == 16);
    REQUIRE(gfx::gi_ray_count(3, 3) == 12);
    REQUIRE(gfx::gi_ray_count(5, 1) == 5);
    REQUIRE(gfx::gi_ray_count(5, 4) == 9);

    REQUIRE(gfx::gi_step_count(1, 1) == 12);
    REQUIRE(gfx::gi_step_count(2, 4) == 16);
    // SSPT spends the same ray budget on longer paths.
    REQUIRE(gfx::gi_step_count(3, 1) == 12);
    REQUIRE(gfx::gi_step_count(3, 4) == 32);
    REQUIRE(gfx::gi_step_count(3, 4) > gfx::gi_step_count(2, 4));
    // VXGI cone marching steps
    REQUIRE(gfx::gi_step_count(5, 1) == 24);
    REQUIRE(gfx::gi_step_count(5, 4) == 64);
}

TEST_CASE("gi filter taps clamp to the unroll budget") {
    REQUIRE(gfx::gi_filter_taps(0) == 1);
    REQUIRE(gfx::gi_filter_taps(1) == 1);
    REQUIRE(gfx::gi_filter_taps(6) == 6);
    REQUIRE(gfx::gi_filter_taps(64) == 8);
}

TEST_CASE("ao and ssr settings validate into range") {
    gfx::GraphicsSettings settings;
    settings.ao.type = 9;
    settings.ao.quality = 12;
    settings.ao.radius = -3.0f;
    settings.ao.intensity = 40.0f;
    settings.ssr.quality = 9;
    settings.ssr.debug_mode = 11;
    settings = validated(settings);

    REQUIRE(settings.ao.type == 2);
    REQUIRE(settings.ao.quality == 6);
    REQUIRE(settings.ao.radius == 0.0f);
    REQUIRE(settings.ao.intensity == 10.0f);
    REQUIRE(settings.ssr.quality == 4);
    REQUIRE(settings.ssr.debug_mode == 4);

    settings.ao.type = -2;
    settings.ssr.quality = -1;
    settings = validated(settings);
    REQUIRE(settings.ao.type == 0);
    REQUIRE(settings.ssr.quality == 0);
}

TEST_CASE("gi settings validate into range") {
    gfx::GraphicsSettings settings;
    settings.gi.enable = 8;
    settings.gi.quality = 0;
    settings.gi.filter_radius = 0;
    settings.gi.prefilter_radius = 9;
    settings.gi.z_thickness = -1.0f;
    settings.gi.bounce_weight = 4.0f;
    settings = validated(settings);

    REQUIRE(settings.gi.enable == 5);
    REQUIRE(settings.gi.quality == 1);
    REQUIRE(settings.gi.filter_radius == 1);
    REQUIRE(settings.gi.prefilter_radius == 4);
    REQUIRE(settings.gi.z_thickness == 0.0f);
    REQUIRE(settings.gi.bounce_weight == 1.0f);

    settings.gi.quality = 99;
    settings = validated(settings);
    REQUIRE(settings.gi.quality == 4);
}

TEST_CASE("phase 5 debug views keep their identifiers") {
    using gfx::DebugView;
    REQUIRE(static_cast<int>(DebugView::Depth) == 7);
    REQUIRE(static_cast<int>(DebugView::Ao) == 18);
    REQUIRE(static_cast<int>(DebugView::AoBentNormal) == 19);
    REQUIRE(static_cast<int>(DebugView::SsrRadiance) == 20);
    REQUIRE(static_cast<int>(DebugView::SsrConfidence) == 21);
    REQUIRE(static_cast<int>(DebugView::GiRadiance) == 22);
    REQUIRE(static_cast<int>(DebugView::VxgiVoxels) == 23);
}

TEST_CASE("pmx materials receive ao, ssr, and gi by default") {
    formats::PmxMaterial material;
    const std::uint32_t flags = gfx::material_flags_from_pmx(material);
    REQUIRE((flags & static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveAo)) != 0);
    REQUIRE((flags & static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveSsr)) != 0);
    REQUIRE((flags & static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveGi)) != 0);
}

TEST_CASE("receive masks survive the gbuffer packing") {
    const std::uint32_t flags = static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveShadow) |
                                static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveAo) |
                                static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveSsr) |
                                static_cast<std::uint32_t>(gfx::MaterialFlag::ReceiveGi);
    const float packed = gfx::pack_shading_model(2, static_cast<int>(flags));
    REQUIRE(gfx::unpack_shading_model(packed) == 2);
    REQUIRE(gfx::unpack_material_flags(packed) == static_cast<int>(flags));

    const float none = gfx::pack_shading_model(0, 0);
    REQUIRE(gfx::unpack_material_flags(none) == 0);
    REQUIRE(gfx::unpack_shading_model(none) == 0);
}

TEST_CASE("material override leaves the receive masks intact") {
    formats::PmxMaterial material;
    gfx::MaterialConstants constants = gfx::material_from_pmx(material);
    const int flags = gfx::unpack_material_flags(constants.custom_data[3]);
    REQUIRE((flags & static_cast<int>(gfx::MaterialFlag::ReceiveAo)) != 0);
    REQUIRE((flags & static_cast<int>(gfx::MaterialFlag::ReceiveSsr)) != 0);
    REQUIRE((flags & static_cast<int>(gfx::MaterialFlag::ReceiveGi)) != 0);
}
