#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/physics/rig.hpp"
#include "hikari/physics/world.hpp"

TEST_CASE("Jolt world steps a falling body", "[physics]") {
    hikari::physics::WorldSettings settings;
    settings.gravity = {0.0f, -9.81f, 0.0f};
    hikari::physics::World world(settings, 16);

    MmdBodyDesc desc{};
    desc.shape = MMD_SHAPE_SPHERE;
    desc.size[0] = 0.5f;
    desc.position[1] = 10.0f;
    desc.rotation[3] = 1.0f;
    desc.mass = 1.0f;
    desc.friction = 0.5f;
    desc.collide_mask = 0xFFFF;
    const auto index = world.add_body(desc);
    REQUIRE(index.has_value());
    world.finish(hikari::physics::RestOverlaps::IgnoreDynamic);
    for (int i = 0; i < 30; ++i) {
        world.step(1.0f / 60.0f, 1);
    }
    std::vector<hikari::physics::BodyTransform> transforms;
    world.read(transforms);
    REQUIRE(transforms.size() == 1);
    REQUIRE(transforms[0].position.y < 10.0f);
}

TEST_CASE("Physics rig builds bodies and joints from a PMX model", "[physics]") {
    hikari::formats::PmxModel model;
    hikari::formats::PmxBone bone;
    bone.name_local = "root";
    bone.position = {0.0f, 10.0f, 0.0f};
    model.bones = {bone};

    hikari::formats::PmxRigidBody first;
    first.name_local = "body_a";
    first.bone_index = 0;
    first.shape = 0;
    first.size = {0.5f, 0.5f, 0.5f};
    first.position = {0.0f, 10.0f, 0.0f};
    first.mass = 1.0f;
    first.mode = 0;
    first.collision_mask = 0xFFFF;

    hikari::formats::PmxRigidBody second = first;
    second.name_local = "body_b";
    second.position = {0.0f, 9.0f, 0.0f};
    second.mode = 1;
    second.bone_index = -1;
    model.rigid_bodies = {first, second};

    hikari::formats::PmxJoint joint;
    joint.name_local = "joint";
    joint.body_a = 0;
    joint.body_b = 1;
    joint.position = {0.0f, 9.5f, 0.0f};
    joint.pos_min = {-1.0f, -1.0f, -1.0f};
    joint.pos_max = {1.0f, 1.0f, 1.0f};
    joint.rot_min = {-0.5f, -0.5f, -0.5f};
    joint.rot_max = {0.5f, 0.5f, 0.5f};
    model.joints = {joint};

    hikari::physics::PhysicsSettings settings;
    auto rig = hikari::physics::PhysicsRig::create(model, {{0.0f, 10.0f, 0.0f}}, settings);
    REQUIRE(rig.has_value());
    REQUIRE(rig->body_count() == 2);
    REQUIRE(rig->dynamic_count() == 1);
    REQUIRE(rig->get_debug_joints().size() == 1);

    std::vector<hikari::physics::BodyTransform> bones;
    bones.push_back(hikari::physics::BodyTransform::identity());
    bones[0].position = {0.0f, 10.0f, 0.0f};
    for (int i = 0; i < 10; ++i) {
        rig->step(bones, 1.0f / 60.0f);
    }
    REQUIRE(rig->body_transforms().size() == 2);
}
