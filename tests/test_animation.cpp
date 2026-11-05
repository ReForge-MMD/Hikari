#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/animation/bezier.hpp"
#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/animation/timeline.hpp"
#include "hikari/formats/pmx.hpp"

namespace {

hikari::formats::PmxModel chain_model() {
    hikari::formats::PmxModel model;
    hikari::formats::PmxBone parent;
    parent.name_local = "root";
    parent.name_universal = "root";
    parent.position = {0.0f, 0.0f, 0.0f};
    parent.parent_index = -1;
    parent.flags = 0x0008;

    hikari::formats::PmxBone child;
    child.name_local = "child";
    child.name_universal = "child";
    child.position = {0.0f, 5.0f, 0.0f};
    child.parent_index = 0;
    child.flags = 0x0008;
    model.bones = {parent, child};
    model.vertices.resize(2);
    return model;
}

} // namespace

TEST_CASE("Skeleton evaluates forward kinematics", "[animation]") {
    const auto model = chain_model();
    auto skeleton = hikari::animation::Skeleton::from_pmx(model);
    REQUIRE(skeleton.bones.size() == 2);
    REQUIRE(skeleton.world_positions[1].y == Catch::Approx(5.0f));

    skeleton.set_bone_transform(1, hikari::Vec3(0.0f, 1.0f, 0.0f), hikari::Quat::identity());
    skeleton.update_transforms();
    REQUIRE(skeleton.world_positions[1].y == Catch::Approx(6.0f));
}

TEST_CASE("Skeleton rotation propagates to children", "[animation]") {
    const auto model = chain_model();
    auto skeleton = hikari::animation::Skeleton::from_pmx(model);
    const auto rotation = hikari::Quat::from_axis_angle(hikari::Vec3::axis_z(), 1.5707963f);
    skeleton.set_bone_transform(0, hikari::Vec3::zero(), rotation);
    skeleton.update_transforms();
    REQUIRE(skeleton.world_positions[1].x == Catch::Approx(-5.0f).margin(1.0e-3f));
    REQUIRE(skeleton.world_positions[1].y == Catch::Approx(0.0f).margin(1.0e-3f));
}

TEST_CASE("Morph evaluator accumulates vertex and group weights", "[animation]") {
    hikari::formats::PmxModel model;
    model.vertices.resize(2);
    hikari::formats::PmxMorph vertex_morph;
    vertex_morph.name_local = "smile";
    vertex_morph.type = hikari::formats::PmxMorphType::Vertex;
    vertex_morph.vertex_items = {{0, {1.0f, 2.0f, 3.0f}}};
    hikari::formats::PmxMorph group_morph;
    group_morph.name_local = "group";
    group_morph.type = hikari::formats::PmxMorphType::Group;
    group_morph.group_items = {{0, 1.0f}};
    model.morphs = {vertex_morph, group_morph};

    auto evaluator = hikari::animation::MorphEvaluator::from_pmx(model);
    evaluator.set_weight("group", 0.5f);
    const auto [positions, uvs] = evaluator.compute_offsets();
    REQUIRE(positions.size() == 2);
    REQUIRE(positions[0].y == Catch::Approx(1.0f));
    REQUIRE(uvs.size() == 2);
}

TEST_CASE("Keyframe easing is linear for equal control pairs", "[animation]") {
    const std::array<std::uint8_t, 4> linear{20, 20, 20, 20};
    REQUIRE(hikari::animation::evaluate_keyframe_ease(linear, 0.25f) ==
            Catch::Approx(0.25f));
    REQUIRE(hikari::animation::evaluate_keyframe_ease(linear, 0.8f) ==
            Catch::Approx(0.8f));
}

TEST_CASE("Keyframe easing applies an S-curve", "[animation]") {
    const std::array<std::uint8_t, 4> scurve{13, 114, 114, 13};
    const float eased = hikari::animation::evaluate_keyframe_ease(scurve, 0.25f);
    REQUIRE(eased > 0.25f);
    REQUIRE(eased < 1.0f);
    REQUIRE(hikari::animation::evaluate_keyframe_ease(scurve, 1.0f) ==
            Catch::Approx(1.0f));
}

TEST_CASE("Motion player samples VMD camera, light and shadow tracks", "[animation]") {
    hikari::formats::VmdMotion motion;
    hikari::formats::VmdCameraFrame first;
    first.frame = 0;
    first.distance = 10.0f;
    first.position = {0.0f, 0.0f, 0.0f};
    first.rotation = {0.0f, 0.0f, 0.0f};
    first.fov = 30;
    hikari::formats::VmdCameraFrame second = first;
    second.frame = 10;
    second.distance = 20.0f;
    second.position = {2.0f, 4.0f, 6.0f};
    second.rotation = {0.2f, 0.4f, 0.6f};
    second.fov = 40;
    motion.camera_frames = {first, second};

    hikari::formats::VmdLightFrame light_a;
    light_a.frame = 0;
    light_a.color = {0.2f, 0.2f, 0.2f};
    light_a.direction = {-0.5f, -1.0f, -0.5f};
    hikari::formats::VmdLightFrame light_b = light_a;
    light_b.frame = 10;
    light_b.color = {0.8f, 0.8f, 0.8f};
    light_b.direction = {-1.0f, -1.0f, 0.0f};
    motion.light_frames = {light_a, light_b};

    hikari::formats::VmdShadowFrame shadow_a;
    shadow_a.frame = 0;
    shadow_a.mode = 1;
    shadow_a.distance = 100.0f;
    hikari::formats::VmdShadowFrame shadow_b = shadow_a;
    shadow_b.frame = 10;
    shadow_b.mode = 2;
    shadow_b.distance = 200.0f;
    motion.shadow_frames = {shadow_a, shadow_b};

    const hikari::animation::MotionPlayer player(motion);
    const auto sample = player.evaluate_scene(5.0f);
    REQUIRE(sample.has_value());
    REQUIRE(sample->camera.has_value());
    REQUIRE(sample->camera->distance == Catch::Approx(15.0f));
    REQUIRE(sample->camera->position[1] == Catch::Approx(2.0f));
    REQUIRE(sample->camera->rotation[1] == Catch::Approx(0.2f));
    REQUIRE(sample->camera->fov == Catch::Approx(35.0f));
    REQUIRE(sample->light.has_value());
    REQUIRE(sample->light->color[0] == Catch::Approx(0.5f));
    REQUIRE(sample->shadow.has_value());
    REQUIRE(sample->shadow->distance == Catch::Approx(150.0f));
    REQUIRE(player.max_frame == 10);
}

TEST_CASE("Motion player reports no scene sample without scene tracks", "[animation]") {
    hikari::formats::VmdMotion motion;
    const hikari::animation::MotionPlayer player(motion);
    REQUIRE_FALSE(player.evaluate_scene(0.0f).has_value());
}

TEST_CASE("Bezier unpack reads the rotation channel", "[animation]") {
    std::array<std::uint8_t, 64> buffer{};
    buffer[3] = 10;
    buffer[7] = 20;
    buffer[11] = 30;
    buffer[15] = 40;
    const auto channel = hikari::animation::unpack_bone_bezier(buffer, 3);
    REQUIRE(channel == std::array<std::uint8_t, 4>{10, 20, 30, 40});
}
