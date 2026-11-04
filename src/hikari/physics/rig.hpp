#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"
#include "hikari/physics/world.hpp"

namespace hikari::physics {

struct PhysicsSettings {
    float gravity = 98.0f;
    float max_substep = 1.0f / 60.0f;
    std::uint32_t max_substeps = 8;
    float max_frame = 0.25f;
    std::uint32_t collision_steps = 1;
    std::uint32_t velocity_steps = 8;
    std::uint32_t position_steps = 4;
    float spring_damping_ratio = 0.30f;
    float spring_scale = 1.0f;
    float extra_damping = 0.02f;
    RestOverlaps rest_overlaps = RestOverlaps::IgnoreDynamic;
};

struct BoneOverride {
    enum class Kind { Full, Rotation };
    Kind kind = Kind::Full;
    Vec3 position;
    Quat rotation;
};

struct RigidBodyDebugInfo {
    std::string name;
    std::optional<std::size_t> bone_index;
    std::uint8_t shape = 0;
    std::array<float, 3> size{1.0f, 1.0f, 1.0f};
    std::uint8_t mode = 0;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct JointDebugInfo {
    std::string name;
    std::size_t body_a = 0;
    std::size_t body_b = 0;
    std::array<float, 3> position{};
};

BodyTransform compose(const BodyTransform& a, const BodyTransform& b);
BodyTransform inverse(const BodyTransform& a);
float damping_rate(float fraction);

class PhysicsRig {
public:
    static std::optional<PhysicsRig> create(const formats::PmxModel& model,
                                            const std::vector<std::array<float, 3>>& bone_rest_positions,
                                            const PhysicsSettings& settings);

    std::size_t body_count() const { return bodies_.size(); }
    bool is_bone_driven(std::size_t bone_index) const;
    std::size_t dynamic_count() const;
    const std::vector<std::string>& names() const { return names_; }

    void request_reset() { needs_reset_ = true; }
    void set_gravity(float gravity);
    void set_gravity_vec(const std::array<float, 3>& gravity);

    const std::vector<std::optional<BoneOverride>>& step(const std::vector<BodyTransform>& bone_world,
                                                         float dt);

    const std::vector<BodyTransform>& body_transforms() const { return current_; }
    std::vector<RigidBodyDebugInfo> get_debug_rigid_bodies() const;
    const std::vector<JointDebugInfo>& get_debug_joints() const { return debug_joints_; }

private:
    struct RigBody {
        std::optional<std::size_t> bone;
        std::uint8_t mode = 0;
        BodyTransform offset;
    };

    struct ChainDistanceConstraint {
        std::size_t parent = 0;
        std::size_t child = 0;
        float max_distance = 0.0f;
    };

    void update_targets(const std::vector<BodyTransform>& bone_world);

    World world_;
    std::vector<RigBody> bodies_;
    std::vector<ChainDistanceConstraint> distance_constraints_;
    std::vector<std::optional<std::size_t>> drivers_;
    std::vector<BodyTransform> previous_targets_;
    std::vector<BodyTransform> targets_;
    std::vector<BodyTransform> interpolated_;
    std::vector<BodyTransform> current_;
    std::vector<std::optional<BoneOverride>> overrides_;
    std::vector<std::string> names_;
    std::vector<std::uint8_t> debug_shapes_;
    std::vector<std::array<float, 3>> debug_sizes_;
    std::vector<JointDebugInfo> debug_joints_;
    PhysicsSettings settings_;
    bool needs_reset_ = true;
    float accumulator_ = 0.0f;
};

} // namespace hikari::physics
