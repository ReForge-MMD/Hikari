#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/math.hpp"
#include "hikari/render/camera.hpp"

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

TEST_CASE("MMD camera places the signed distance on the local Z axis", "[math][camera]") {
    using hikari::render::Camera;
    constexpr float kHalfPi = 1.5707963267948966f;
    const Vec3 focus(0.0f, 10.0f, 0.0f);
    Camera camera;

    const Quat front = Quat::from_euler_yxz(0.0f, 0.0f, 0.0f).inverse();
    camera.place_mmd(focus, front, -45.0f);
    REQUIRE(camera.position.x == Catch::Approx(0.0f).margin(1.0e-4f));
    REQUIRE(camera.position.y == Catch::Approx(10.0f).margin(1.0e-4f));
    REQUIRE(camera.position.z == Catch::Approx(-45.0f).margin(1.0e-4f));
    REQUIRE(camera.target.z == Catch::Approx(-44.0f).margin(1.0e-4f));

    const Quat yaw = Quat::from_euler_yxz(kHalfPi, 0.0f, 0.0f).inverse();
    camera.place_mmd(focus, yaw, -45.0f);
    REQUIRE(camera.position.x == Catch::Approx(45.0f).margin(1.0e-4f));
    REQUIRE(camera.position.z == Catch::Approx(0.0f).margin(1.0e-4f));
    REQUIRE(camera.target.x == Catch::Approx(44.0f).margin(1.0e-4f));

    // MMD top view is pitch -90: the eye sits above the focus and looks down.
    const Quat top = Quat::from_euler_yxz(0.0f, -kHalfPi, 0.0f).inverse();
    camera.place_mmd(focus, top, -45.0f);
    REQUIRE(camera.position.y == Catch::Approx(55.0f).margin(1.0e-4f));
    REQUIRE(camera.target.y == Catch::Approx(54.0f).margin(1.0e-4f));
    REQUIRE(camera.up.z == Catch::Approx(1.0f).margin(1.0e-4f));

    // A positive distance puts the focus behind the camera.
    camera.place_mmd(focus, front, 45.0f);
    REQUIRE(camera.position.z == Catch::Approx(45.0f).margin(1.0e-4f));
    REQUIRE(camera.target.z == Catch::Approx(46.0f).margin(1.0e-4f));
}

TEST_CASE("MMD camera composition keeps the horizon level", "[math][camera]") {
    using hikari::render::Camera;
    constexpr float kHalfPi = 1.5707963267948966f;
    constexpr float kSixthPi = 0.5235987755982988f;
    const Vec3 focus(0.0f, 10.0f, 0.0f);
    Camera camera;
    camera.place_mmd(focus,
                     hikari::render::mmd_camera_orientation(kHalfPi, kSixthPi, 0.0f), -45.0f);
    REQUIRE(camera.position.x == Catch::Approx(38.9711f).margin(1.0e-3f));
    REQUIRE(camera.position.y == Catch::Approx(-12.5f).margin(1.0e-3f));
    REQUIRE(camera.position.z == Catch::Approx(0.0f).margin(1.0e-3f));
    const Vec3 forward = (camera.target - camera.position).normalize();
    const Vec3 right = camera.up.normalize().cross(forward).normalize();
    REQUIRE(right.y == Catch::Approx(0.0f).margin(1.0e-4f));
}

TEST_CASE("MMD orthographic framing uses the camera distance", "[math][camera]") {
    using hikari::render::Camera;
    constexpr float kAspect = 16.0f / 9.0f;
    const float height = Camera::mmd_ortho_height(-45.0f, kAspect);
    REQUIRE(height == Catch::Approx(45.0f / kAspect).margin(1.0e-4f));

    Camera camera;
    camera.aspect = kAspect;
    camera.orthographic = true;
    camera.ortho_height = height;
    const Mat4 projection = camera.projection_matrix();
    REQUIRE(projection.m[0][0] == Catch::Approx(2.0f / 45.0f).margin(1.0e-5f));
    REQUIRE(projection.m[1][1] == Catch::Approx(2.0f * kAspect / 45.0f).margin(1.0e-5f));
}
