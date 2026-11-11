#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "hikari/gfx/settings.hpp"
#include "hikari/gfx/sky.hpp"
#include "hikari/gfx/sky_assets.hpp"

TEST_CASE("atmosphere step counts grow with quality") {
    int previous = 0;
    for (int quality = 0; quality <= 4; ++quality) {
        const int steps = hikari::gfx::atmosphere_step_count(quality);
        REQUIRE(steps > previous);
        previous = steps;
    }
    REQUIRE(hikari::gfx::atmosphere_step_count(-3) ==
            hikari::gfx::atmosphere_step_count(0));
    REQUIRE(hikari::gfx::atmosphere_step_count(9) ==
            hikari::gfx::atmosphere_step_count(4));
}

TEST_CASE("sky capture resolution follows ibl quality") {
    REQUIRE(hikari::gfx::sky_capture_resolution(0) == 0u);
    REQUIRE(hikari::gfx::sky_capture_resolution(1) == 128u);
    REQUIRE(hikari::gfx::sky_capture_resolution(2) == 256u);
    REQUIRE(hikari::gfx::sky_capture_resolution(-1) == 0u);
    REQUIRE(hikari::gfx::sky_capture_resolution(7) == 256u);
}

TEST_CASE("sun elevation follows a continuous day cycle") {
    REQUIRE(hikari::gfx::sun_elevation_from_time_of_day(12.0f) > 60.0f);
    REQUIRE(hikari::gfx::sun_elevation_from_time_of_day(0.0f) < -60.0f);
    REQUIRE(std::abs(hikari::gfx::sun_elevation_from_time_of_day(6.0f)) < 1.0e-3f);
    REQUIRE(std::abs(hikari::gfx::sun_elevation_from_time_of_day(18.0f)) < 1.0e-3f);
    REQUIRE(hikari::gfx::sun_elevation_from_time_of_day(0.0f) ==
            Catch::Approx(hikari::gfx::sun_elevation_from_time_of_day(24.0f))
                .margin(1.0e-4f));
    REQUIRE(hikari::gfx::sun_elevation_from_time_of_day(-6.0f) ==
            Catch::Approx(hikari::gfx::sun_elevation_from_time_of_day(18.0f))
                .margin(1.0e-4f));
}

TEST_CASE("sun direction is periodic across midnight") {
    const hikari::Vec3 before = hikari::gfx::sun_direction_from_time_of_day(24.0f);
    const hikari::Vec3 after = hikari::gfx::sun_direction_from_time_of_day(0.0f);
    REQUIRE(before.x == Catch::Approx(after.x).margin(1.0e-5f));
    REQUIRE(before.y == Catch::Approx(after.y).margin(1.0e-5f));
    REQUIRE(before.z == Catch::Approx(after.z).margin(1.0e-5f));
    REQUIRE(before.length() == Catch::Approx(1.0f).margin(1.0e-5f));
}

TEST_CASE("sun direction has no discontinuity over the day") {
    constexpr int kSamples = 24 * 60;
    float smallest_dot = 1.0f;
    hikari::Vec3 previous = hikari::gfx::sun_direction_from_time_of_day(0.0f);
    for (int i = 1; i <= kSamples; ++i) {
        const float hours = static_cast<float>(i) * (24.0f / static_cast<float>(kSamples));
        const hikari::Vec3 current = hikari::gfx::sun_direction_from_time_of_day(hours);
        smallest_dot = std::min(smallest_dot, previous.dot(current));
        previous = current;
    }
    REQUIRE(smallest_dot > 0.999f);
}

TEST_CASE("sun rises in the east and sets in the west") {
    const hikari::Vec3 sunrise = hikari::gfx::sun_direction_from_time_of_day(6.0f);
    const hikari::Vec3 noon = hikari::gfx::sun_direction_from_time_of_day(12.0f);
    const hikari::Vec3 sunset = hikari::gfx::sun_direction_from_time_of_day(18.0f);
    REQUIRE(sunrise.x > 0.9f);
    REQUIRE(std::abs(sunrise.y) < 0.05f);
    REQUIRE(sunset.x < -0.9f);
    REQUIRE(noon.z < -0.4f);
    REQUIRE(noon.y > 0.8f);
}

TEST_CASE("sky settings clamp into range") {
    hikari::gfx::GraphicsSettings settings;
    settings.sky.quality = 9;
    settings.sky.sun_radiance = -4.0f;
    settings.sky.mie_anisotropy = 3.0f;
    settings.sky.turbidity = 99.0f;
    settings.sky.time_of_day = 24.0f;
    settings.validate();
    REQUIRE(settings.sky.quality == 4);
    REQUIRE(settings.sky.sun_radiance == 0.0f);
    REQUIRE(settings.sky.mie_anisotropy == Catch::Approx(0.99f));
    REQUIRE(settings.sky.turbidity == 10.0f);
    REQUIRE(settings.sky.time_of_day == 0.0f);
}

TEST_CASE("sky settings round trip through json") {
    hikari::gfx::GraphicsSettings source;
    source.sky.enabled = true;
    source.sky.quality = 3;
    source.sky.sun_radiance = 7.5f;
    source.sky.mie_anisotropy = 0.55f;
    source.sky.time_of_day = 6.25f;

    hikari::gfx::GraphicsSettings restored;
    REQUIRE(restored.from_json(source.to_json()));
    REQUIRE(restored.sky.enabled);
    REQUIRE(restored.sky.quality == 3);
    REQUIRE(restored.sky.sun_radiance == Catch::Approx(7.5f));
    REQUIRE(restored.sky.mie_anisotropy == Catch::Approx(0.55f));
    REQUIRE(restored.sky.time_of_day == Catch::Approx(6.25f));
}
TEST_CASE("generated cloud noise tiles seamlessly") {
    const hikari::gfx::SkyImage image = hikari::gfx::build_cloud_noise_image(64);
    REQUIRE(image.width == 64);
    REQUIRE(image.height == 64);
    REQUIRE(image.rgba.size() == 64u * 64u * 4u);

    double interior = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < image.width; ++x) {
            const std::size_t left = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const std::size_t right = left + 4;
            interior += std::abs(static_cast<int>(image.rgba[right]) -
                                 static_cast<int>(image.rgba[left]));
        }
    }
    interior /= static_cast<double>((image.width - 2) * image.height);

    double seam = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const std::size_t first = (static_cast<std::size_t>(y) * image.width) * 4;
        const std::size_t last =
            (static_cast<std::size_t>(y) * image.width + image.width - 1) * 4;
        seam += std::abs(static_cast<int>(image.rgba[first]) -
                         static_cast<int>(image.rgba[last]));
    }
    seam /= static_cast<double>(image.height);

    REQUIRE(seam <= interior * 3.0 + 2.0);
}

TEST_CASE("generated sky assets are deterministic and non degenerate") {
    const hikari::gfx::SkyImage first = hikari::gfx::build_cloud_noise_image(32);
    const hikari::gfx::SkyImage second = hikari::gfx::build_cloud_noise_image(32);
    REQUIRE(first.rgba == second.rgba);

    int minimum = 255;
    int maximum = 0;
    for (std::size_t i = 0; i < first.rgba.size(); i += 4) {
        minimum = std::min(minimum, static_cast<int>(first.rgba[i]));
        maximum = std::max(maximum, static_cast<int>(first.rgba[i]));
    }
    REQUIRE(maximum - minimum > 120);
}

TEST_CASE("milky way has a bright ridge") {
    const hikari::gfx::SkyImage image = hikari::gfx::build_milky_way_image(128, 64);
    REQUIRE(image.width == 128);
    REQUIRE(image.height == 64);

    double total = 0.0;
    int brightest = 0;
    int count = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        const int level = static_cast<int>(image.rgba[i]);
        total += static_cast<double>(level);
        brightest = std::max(brightest, level);
        ++count;
    }
    const double mean = total / static_cast<double>(count);
    REQUIRE(mean > 2.0);
    REQUIRE(brightest > mean * 4.0);
}

TEST_CASE("milky way core sits above the horizon") {
    const std::array<float, 3> core = hikari::gfx::milky_way_core_direction();
    const float length =
        std::sqrt(core[0] * core[0] + core[1] * core[1] + core[2] * core[2]);
    REQUIRE(length == Catch::Approx(1.0f).margin(1.0e-5f));
    REQUIRE(core[1] > 0.15f);
}

TEST_CASE("moon albedo separates maria from highlands") {
    const hikari::gfx::SkyImage image = hikari::gfx::build_moon_albedo_image(64);
    int minimum = 255;
    int maximum = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        minimum = std::min(minimum, static_cast<int>(image.rgba[i]));
        maximum = std::max(maximum, static_cast<int>(image.rgba[i]));
    }
    REQUIRE(maximum - minimum > 40);
}

TEST_CASE("night and cloud settings clamp into range") {
    hikari::gfx::GraphicsSettings settings;
    settings.sky.cloud_density = -2.0f;
    settings.sky.cloud_turbulence = 9.0f;
    settings.sky.moon_phase = 3.0f;
    settings.sky.milky_way_intensity = -1.0f;
    settings.sky.ibl_update_hz = 0;
    settings.validate();
    REQUIRE(settings.sky.cloud_density == 0.0f);
    REQUIRE(settings.sky.cloud_turbulence == 2.0f);
    REQUIRE(settings.sky.moon_phase == 1.0f);
    REQUIRE(settings.sky.milky_way_intensity == 0.0f);
    REQUIRE(settings.sky.ibl_update_hz == 1);

    settings.sky.ibl_update_hz = 240;
    settings.validate();
    REQUIRE(settings.sky.ibl_update_hz == 60);
}

TEST_CASE("night sky settings round trip through json") {
    hikari::gfx::GraphicsSettings source;
    source.sky.cloud_density = 2.25f;
    source.sky.cloud_turbulence = 0.4f;
    source.sky.moon_phase = 0.75f;
    source.sky.milky_way_intensity = 3.5f;
    source.sky.ibl_update_hz = 24;

    hikari::gfx::GraphicsSettings restored;
    REQUIRE(restored.from_json(source.to_json()));
    REQUIRE(restored.sky.cloud_density == Catch::Approx(2.25f));
    REQUIRE(restored.sky.cloud_turbulence == Catch::Approx(0.4f));
    REQUIRE(restored.sky.moon_phase == Catch::Approx(0.75f));
    REQUIRE(restored.sky.milky_way_intensity == Catch::Approx(3.5f));
    REQUIRE(restored.sky.ibl_update_hz == 24);
}
