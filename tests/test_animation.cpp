#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/animation/bezier.hpp"
#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
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

TEST_CASE("Bezier unpack reads the rotation channel", "[animation]") {
    std::array<std::uint8_t, 64> buffer{};
    buffer[3] = 10;
    buffer[7] = 20;
    buffer[11] = 30;
    buffer[15] = 40;
    const auto channel = hikari::animation::unpack_bone_bezier(buffer, 3);
    REQUIRE(channel == std::array<std::uint8_t, 4>{10, 20, 30, 40});
}
