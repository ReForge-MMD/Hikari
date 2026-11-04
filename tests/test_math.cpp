#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/math.hpp"

using hikari::Mat4;
using hikari::Quat;
using hikari::Vec3;

TEST_CASE("Vector arithmetic and normalization", "[math]") {
    const Vec3 a(1.0f, 2.0f, 3.0f);
    const Vec3 b(4.0f, 5.0f, 6.0f);
    REQUIRE(a.dot(b) == Catch::Approx(32.0f));
    const Vec3 cross = a.cross(b);
    REQUIRE(cross.x == Catch::Approx(-3.0f));
    REQUIRE(cross.y == Catch::Approx(6.0f));
    REQUIRE(cross.z == Catch::Approx(-3.0f));
    REQUIRE(a.normalize().length() == Catch::Approx(1.0f));
}

TEST_CASE("Quaternion round trip through Euler angles", "[math]") {
    const Vec3 euler(0.3f, -0.4f, 0.7f);
    const Quat quaternion = Quat::from_euler_xyz(euler);
    const Vec3 result = quaternion.to_euler_xyz();
    REQUIRE(result.x == Catch::Approx(euler.x).margin(1.0e-4f));
    REQUIRE(result.y == Catch::Approx(euler.y).margin(1.0e-4f));
    REQUIRE(result.z == Catch::Approx(euler.z).margin(1.0e-4f));
}

TEST_CASE("Matrix affine inversion", "[math]") {
    const Mat4 transform = Mat4::from_translation_rotation(
        Vec3(2.0f, 3.0f, 4.0f), Quat::from_euler_xyz(Vec3(0.1f, 0.2f, 0.3f)));
    const Mat4 inverse = transform.inverse_transform();
    const Vec3 point(5.0f, -1.0f, 2.0f);
    const Vec3 restored = inverse.transform_point(transform.transform_point(point));
    REQUIRE(restored.x == Catch::Approx(point.x).margin(1.0e-4f));
    REQUIRE(restored.y == Catch::Approx(point.y).margin(1.0e-4f));
    REQUIRE(restored.z == Catch::Approx(point.z).margin(1.0e-4f));
}

TEST_CASE("YXZ joint rotation follows the PMX authoring convention", "[math]") {
    const Quat around_x = Quat::from_euler_yxz(0.0f, 1.5707963f, 0.0f);
    const Vec3 rotated_y = around_x.rotate_vec3(Vec3::axis_y());
    REQUIRE(rotated_y.z == Catch::Approx(1.0f).margin(1.0e-4f));

    const Quat around_y = Quat::from_euler_yxz(1.5707963f, 0.0f, 0.0f);
    const Vec3 rotated_z = around_y.rotate_vec3(Vec3::axis_z());
    REQUIRE(rotated_z.x == Catch::Approx(1.0f).margin(1.0e-4f));
}

TEST_CASE("Left-handed projection maps the depth range to zero and one", "[math]") {
    const Mat4 projection = hikari::perspective_lh(0.5f, 1.5f, 0.1f, 1000.0f);
    const auto depth = [&](float z) {
        const float clip = z * projection.m[2][2] + projection.m[3][2];
        const float w = z * projection.m[2][3] + projection.m[3][3];
        return clip / w;
    };
    REQUIRE(depth(0.1f) == Catch::Approx(0.0f).margin(1.0e-4f));
    REQUIRE(depth(1000.0f) == Catch::Approx(1.0f).margin(1.0e-4f));
}
