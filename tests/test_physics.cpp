#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/animation/skeleton.hpp"
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

TEST_CASE("Physics floor stops simulated bodies and can be disabled", "[physics]") {
    hikari::physics::WorldSettings settings;
    settings.gravity = {0.0f, -9.81f, 0.0f};
    hikari::physics::World world(settings, 16);
    world.set_ground_enabled(true);

    MmdBodyDesc desc{};
    desc.shape = MMD_SHAPE_SPHERE;
    desc.size[0] = 0.5f;
    desc.position[1] = 5.0f;
    desc.rotation[3] = 1.0f;
    desc.mass = 1.0f;
    desc.friction = 0.5f;
    desc.collide_mask = 0xFFFF;
    const auto index = world.add_body(desc);
    REQUIRE(index.has_value());
    world.finish(hikari::physics::RestOverlaps::IgnoreDynamic);

    for (int i = 0; i < 400; ++i) {
        world.step(1.0f / 60.0f, 1);
    }
    std::vector<hikari::physics::BodyTransform> transforms;
    world.read(transforms);
    REQUIRE(transforms.size() == 1);
    REQUIRE(transforms[0].position.y > 0.0f);

    world.set_ground_enabled(false);
    for (int i = 0; i < 600; ++i) {
        world.step(1.0f / 60.0f, 1);
    }
    world.read(transforms);
    REQUIRE(transforms[0].position.y < 0.0f);
}

TEST_CASE("Physics time scale speeds up motion without raising gravity", "[physics]") {
    hikari::formats::PmxModel model;
    hikari::formats::PmxRigidBody body;
    body.name_local = "falling";
    body.bone_index = -1;
    body.shape = 0;
    body.size = {0.5f, 0.5f, 0.5f};
    body.position = {0.0f, 10.0f, 0.0f};
    body.mass = 1.0f;
    body.mode = 1;
    body.collision_mask = 0xFFFF;
    model.rigid_bodies = {body};

    hikari::physics::PhysicsSettings slow;
    slow.gravity = 9.8f;
    slow.time_scale = 1.0f;
    hikari::physics::PhysicsSettings fast = slow;
    fast.time_scale = 3.0f;

    auto slow_rig = hikari::physics::PhysicsRig::create(model, {}, slow);
    auto fast_rig = hikari::physics::PhysicsRig::create(model, {}, fast);
    REQUIRE(slow_rig.has_value());
    REQUIRE(fast_rig.has_value());

    const std::vector<hikari::physics::BodyTransform> bones;
    for (int i = 0; i < 12; ++i) {
        slow_rig->step(bones, 1.0f / 60.0f);
        fast_rig->step(bones, 1.0f / 60.0f);
    }
    const float slow_y = slow_rig->body_transforms()[0].position.y;
    const float fast_y = fast_rig->body_transforms()[0].position.y;
    REQUIRE(fast_y < slow_y);
}

TEST_CASE("Disabling physics restores driven bones to the rest pose", "[physics]") {
    hikari::formats::PmxModel model;
    hikari::formats::PmxBone bone;
    bone.name_local = "hair";
    bone.position = {0.0f, 10.0f, 0.0f};
    model.bones = {bone};

    hikari::formats::PmxRigidBody body;
    body.name_local = "hair_body";
    body.bone_index = 0;
    body.shape = 0;
    body.size = {0.5f, 0.5f, 0.5f};
    body.position = {0.0f, 10.0f, 0.0f};
    body.mass = 1.0f;
    body.mode = 1;
    body.collision_mask = 0xFFFF;
    model.rigid_bodies = {body};

    auto rig = hikari::physics::PhysicsRig::create(model, {{0.0f, 10.0f, 0.0f}},
                                                   hikari::physics::PhysicsSettings{});
    REQUIRE(rig.has_value());
    REQUIRE(rig->is_bone_driven(0));

    auto skeleton = hikari::animation::Skeleton::from_pmx(model);
    skeleton.local_translations[0] = {1.0f, 2.0f, 3.0f};
    skeleton.local_rotations[0] =
        hikari::Quat::from_axis_angle(hikari::Vec3::axis_z(), 0.5f);
    skeleton.update_transforms();

    skeleton.reset_physics_bones(*rig);
    REQUIRE(skeleton.local_translations[0].x == Catch::Approx(0.0f));
    REQUIRE(skeleton.local_translations[0].y == Catch::Approx(0.0f));
    REQUIRE(skeleton.local_translations[0].z == Catch::Approx(0.0f));
    REQUIRE(skeleton.local_rotations[0].w == Catch::Approx(1.0f));
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
