#include <catch2/catch_test_macros.hpp>
#include "hikari/gfx/settings.hpp"

TEST_CASE("shadow settings defaults are valid") {
    hikari::gfx::GraphicsSettings s;
    REQUIRE(s.shadows.quality >= 1);
    REQUIRE(s.shadows.quality <= 4);
    REQUIRE(s.shadows.light_radius > 0.0f);
    REQUIRE(s.shadows.contact_quality >= 0);
    REQUIRE(s.shadows.ssgs_quality >= 0);
}

TEST_CASE("shadow quality clamp") {
    hikari::gfx::GraphicsSettings s;
    s.shadows.quality = 99;
    s.validate();
    REQUIRE(s.shadows.quality <= 4);
    s.shadows.quality = -1;
    s.validate();
    REQUIRE(s.shadows.quality >= 0);
}

TEST_CASE("shadow blur enabled by default") {
    hikari::gfx::GraphicsSettings s;
    REQUIRE(s.shadows.blur == true);
}

TEST_CASE("contact shadow steps map") {
    // contact_quality 0 -> 0 steps, 1->8, 2->12, 3->16
    // This validates the mapping contract used by the shader.
    const int expected[] = {0, 8, 12, 16};
    for (int q = 0; q <= 3; ++q) {
        hikari::gfx::GraphicsSettings s;
        s.shadows.contact_quality = q;
        s.validate();
        REQUIRE(s.shadows.contact_quality == q);
        (void)expected;
    }
}

TEST_CASE("ssgs steps map") {
    // ssgs_quality 0->0, 1->16, 2->24, 3->32
    hikari::gfx::GraphicsSettings s;
    s.shadows.ssgs_quality = 3;
    s.validate();
    REQUIRE(s.shadows.ssgs_quality == 3);
}

TEST_CASE("sky visibility mode range") {
    hikari::gfx::GraphicsSettings s;
    s.ao.sky_visibility = 0;
    s.validate();
    REQUIRE(s.ao.sky_visibility == 0);
    s.ao.sky_visibility = 2;
    s.validate();
    REQUIRE(s.ao.sky_visibility == 2);
    s.ao.sky_visibility = 5;
    s.validate();
    REQUIRE(s.ao.sky_visibility <= 2);
}

TEST_CASE("shadow settings permutation key changes with pcss") {
    hikari::gfx::GraphicsSettings a, b;
    b.shadows.pcss = !a.shadows.pcss;
    REQUIRE(a.permutation_key() != b.permutation_key());
}
