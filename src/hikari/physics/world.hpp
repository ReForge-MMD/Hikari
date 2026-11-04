#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "hikari/math.hpp"
#include "hikari/physics/mmd_physics.h"

namespace hikari::physics {

struct BodyTransform {
    Vec3 position;
    Quat rotation;

    static BodyTransform identity() { return {Vec3::zero(), Quat::identity()}; }
};

enum class RestOverlaps : std::uint32_t {
    Collide = MMD_REST_OVERLAPS_COLLIDE,
    IgnoreDynamic = MMD_REST_OVERLAPS_IGNORE_DYNAMIC,
    IgnoreAll = MMD_REST_OVERLAPS_IGNORE_ALL,
};

struct WorldSettings {
    Vec3 gravity{0.0f, -98.0f, 0.0f};
    float spring_damping_ratio = 0.3f;
    std::uint32_t velocity_steps = 8;
    std::uint32_t position_steps = 4;
};

class World {
public:
    World() = default;
    World(const WorldSettings& settings, std::uint32_t max_bodies);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    bool valid() const { return raw_ != nullptr; }

    void set_gravity(const Vec3& gravity);
    std::optional<std::size_t> add_body(const MmdBodyDesc& desc);
    std::optional<std::size_t> add_joint(const MmdJointDesc& desc);
    void finish(RestOverlaps overlaps);
    std::size_t body_count() const;

    void move_kinematic(const std::vector<BodyTransform>& targets, float dt);
    void step(float dt, std::uint32_t collision_steps);
    void read(std::vector<BodyTransform>& out);
    void reset(const std::vector<BodyTransform>& transforms);

private:
    void pack(const std::vector<BodyTransform>& transforms);
    void destroy();

    MmdWorld* raw_ = nullptr;
    std::vector<float> scratch_;
};

} // namespace hikari::physics
