#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/gbuffer.hpp"
#include "hikari/gfx/material.hpp"

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
