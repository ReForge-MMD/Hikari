#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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

hikari::formats::PmxModel ik_model() {
    hikari::formats::PmxModel model;
    hikari::formats::PmxBone root;
    root.name_local = "root";
    root.name_universal = "root";
    root.position = {0.0f, 0.0f, 0.0f};
    root.parent_index = -1;
    root.flags = 0x0008;

    hikari::formats::PmxBone link;
    link.name_local = "link";
    link.name_universal = "link";
    link.position = {0.0f, 2.0f, 0.0f};
    link.parent_index = 0;
    link.flags = 0x0008;

    hikari::formats::PmxBone target;
    target.name_local = "target";
    target.name_universal = "target";
    target.position = {0.0f, 4.0f, 0.0f};
    target.parent_index = 1;
    target.flags = 0x0008;

    hikari::formats::PmxBone ik;
    ik.name_local = "ik";
    ik.name_universal = "ik";
    ik.position = {0.0f, 4.0f, 0.0f};
    ik.parent_index = 0;
    ik.flags = 0x0008;
    ik.has_ik = true;
    ik.ik.target_bone = 2;
    ik.ik.loop_count = 40;
    ik.ik.angle_limit = 3.1415927f;
    hikari::formats::PmxIkLink ik_link;
    ik_link.bone_index = 1;
    ik.ik.links.push_back(ik_link);

    model.bones = {root, link, target, ik};
    model.vertices.resize(4);
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

TEST_CASE("Skeleton keeps PMX display tails", "[animation]") {
    auto model = chain_model();
    model.bones[0].has_tail_bone = true;
    model.bones[0].tail_bone = 1;
    model.bones[1].tail_offset = {0.0f, 1.5f, 0.0f};

    const auto skeleton = hikari::animation::Skeleton::from_pmx(model);
    REQUIRE(skeleton.bones[0].has_tail_bone);
    REQUIRE(skeleton.bones[0].tail_bone == 1);
    REQUIRE(skeleton.bones[1].tail_offset.y == Catch::Approx(1.5f));
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

TEST_CASE("Skeleton skips IK solving when the IK bone is disabled", "[animation]") {
    auto skeleton = hikari::animation::Skeleton::from_pmx(ik_model());
    REQUIRE(skeleton.ik_enabled.size() == 4);

    skeleton.set_bone_transform(3, hikari::Vec3(2.0f, 0.0f, 0.0f), hikari::Quat::identity());
    skeleton.update_transforms();
    REQUIRE(std::abs((skeleton.ik_rotations[1] * skeleton.local_rotations[1]).to_euler_xyz().z) >
            0.05f);

    skeleton.reset_pose();
    skeleton.ik_enabled[3] = false;
    skeleton.set_bone_transform(3, hikari::Vec3(2.0f, 0.0f, 0.0f), hikari::Quat::identity());
    skeleton.update_transforms();
    REQUIRE((skeleton.ik_rotations[1] * skeleton.local_rotations[1]).to_euler_xyz().z ==
            Catch::Approx(0.0f).margin(1.0e-4f));
}

TEST_CASE("Motion player applies VMD IK switches as stepped frames", "[animation]") {
    hikari::formats::VmdMotion motion;
    hikari::formats::VmdIkFrame off;
    off.frame = 0;
    off.ik_switches = {{"ik", false}};
    hikari::formats::VmdIkFrame on;
    on.frame = 10;
    on.ik_switches = {{"ik", true}};
    motion.ik_frames = {on, off};

    const hikari::animation::MotionPlayer player(motion);
    auto skeleton = hikari::animation::Skeleton::from_pmx(ik_model());
    std::fill(skeleton.ik_enabled.begin(), skeleton.ik_enabled.end(), true);

    player.apply_ik(5.0f, skeleton);
    REQUIRE_FALSE(skeleton.ik_enabled[3]);
    player.apply_ik(10.0f, skeleton);
    REQUIRE(skeleton.ik_enabled[3]);
    player.apply_ik(0.0f, skeleton);
    REQUIRE_FALSE(skeleton.ik_enabled[3]);
    REQUIRE(player.max_frame == 10);
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
