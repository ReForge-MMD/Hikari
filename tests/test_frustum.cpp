#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/math.hpp"
#include "hikari/render/camera.hpp"
#include "hikari/render/frustum.hpp"

using hikari::Mat4;
using hikari::Vec3;
using hikari::render::BoundingSphere;
using hikari::render::Camera;
using hikari::render::Frustum;

TEST_CASE("Frustum extracts planes from the identity matrix", "[frustum]") {
    const Frustum frustum = Frustum::from_view_projection(Mat4::identity());
    REQUIRE(frustum.contains(Vec3(0.0f, 0.0f, 0.5f)));
    REQUIRE(frustum.contains(Vec3(0.9f, -0.9f, 0.1f)));
    REQUIRE_FALSE(frustum.contains(Vec3(2.0f, 0.0f, 0.5f)));
    REQUIRE_FALSE(frustum.contains(Vec3(0.0f, 0.0f, -0.5f)));
}

TEST_CASE("Frustum culls spheres behind and outside the camera", "[frustum]") {
    Camera camera;
    camera.position = Vec3(0.0f, 0.0f, 0.0f);
    camera.target = Vec3(0.0f, 0.0f, 1.0f);
    camera.up = Vec3(0.0f, 1.0f, 0.0f);
    camera.fov_y = 1.0471976f;
    camera.aspect = 1.0f;
    camera.near_z = 0.1f;
    camera.far_z = 100.0f;
    const Frustum frustum = Frustum::from_view_projection(camera.view_projection_matrix());

    REQUIRE(frustum.intersects(BoundingSphere{Vec3(0.0f, 0.0f, 5.0f), 1.0f}));
    REQUIRE_FALSE(frustum.intersects(BoundingSphere{Vec3(0.0f, 0.0f, -5.0f), 1.0f}));
    REQUIRE_FALSE(frustum.intersects(BoundingSphere{Vec3(100.0f, 0.0f, 5.0f), 1.0f}));
    REQUIRE_FALSE(frustum.intersects(BoundingSphere{Vec3(0.0f, 0.0f, 500.0f), 1.0f}));
}

TEST_CASE("A sphere straddling a plane is not culled", "[frustum]") {
    Camera camera;
    camera.position = Vec3(0.0f, 0.0f, 0.0f);
    camera.target = Vec3(0.0f, 0.0f, 1.0f);
    camera.up = Vec3(0.0f, 1.0f, 0.0f);
    camera.fov_y = 1.0471976f;
    camera.aspect = 1.0f;
    camera.near_z = 0.1f;
    camera.far_z = 100.0f;
    const Frustum frustum = Frustum::from_view_projection(camera.view_projection_matrix());

    REQUIRE(frustum.intersects(BoundingSphere{Vec3(0.0f, 0.0f, 0.1f), 1.0f}));
    REQUIRE(frustum.intersects(BoundingSphere{Vec3(0.0f, -0.6f, 5.0f), 1.5f}));
}
