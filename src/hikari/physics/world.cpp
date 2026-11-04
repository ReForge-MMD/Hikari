#include "hikari/physics/world.hpp"

#include <stdexcept>
#include <utility>

namespace hikari::physics {

World::World(const WorldSettings& settings, std::uint32_t max_bodies) {
    MmdWorldSettings raw_settings{};
    raw_settings.gravity[0] = settings.gravity.x;
    raw_settings.gravity[1] = settings.gravity.y;
    raw_settings.gravity[2] = settings.gravity.z;
    raw_settings.spring_damping_ratio = settings.spring_damping_ratio;
    raw_settings.velocity_steps = settings.velocity_steps;
    raw_settings.position_steps = settings.position_steps;
    raw_ = mmd_world_create(&raw_settings, max_bodies);
    if (raw_ == nullptr) {
        throw std::runtime_error("Jolt MmdWorld creation failed");
    }
}

World::~World() {
    destroy();
}

World::World(World&& other) noexcept : raw_(std::exchange(other.raw_, nullptr)),
                                       scratch_(std::move(other.scratch_)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        destroy();
        raw_ = std::exchange(other.raw_, nullptr);
        scratch_ = std::move(other.scratch_);
    }
    return *this;
}

void World::destroy() {
    if (raw_ != nullptr) {
        mmd_world_destroy(raw_);
        raw_ = nullptr;
    }
}

void World::set_gravity(const Vec3& gravity) {
    if (raw_ != nullptr) {
        mmd_world_set_gravity(raw_, gravity.x, gravity.y, gravity.z);
    }
}

std::optional<std::size_t> World::add_body(const MmdBodyDesc& desc) {
    const std::int32_t index = mmd_world_add_body(raw_, &desc);
    if (index < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(index);
}

std::optional<std::size_t> World::add_joint(const MmdJointDesc& desc) {
    const std::int32_t index = mmd_world_add_joint(raw_, &desc);
    if (index < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(index);
}

void World::finish(RestOverlaps overlaps) {
    mmd_world_finish(raw_, static_cast<std::uint32_t>(overlaps));
}

std::size_t World::body_count() const {
    return raw_ != nullptr ? mmd_world_body_count(raw_) : 0;
}

void World::pack(const std::vector<BodyTransform>& transforms) {
    scratch_.resize(transforms.size() * 7);
    for (std::size_t i = 0; i < transforms.size(); ++i) {
        const auto& transform = transforms[i];
        float* out = scratch_.data() + i * 7;
        out[0] = transform.position.x;
        out[1] = transform.position.y;
        out[2] = transform.position.z;
        out[3] = transform.rotation.x;
        out[4] = transform.rotation.y;
        out[5] = transform.rotation.z;
        out[6] = transform.rotation.w;
    }
}

void World::move_kinematic(const std::vector<BodyTransform>& targets, float dt) {
    pack(targets);
    mmd_world_move_kinematic(raw_, scratch_.data(), static_cast<std::uint32_t>(targets.size()), dt);
}

void World::step(float dt, std::uint32_t collision_steps) {
    mmd_world_step(raw_, dt, collision_steps);
}

void World::read(std::vector<BodyTransform>& out) {
    const std::size_t count = body_count();
    scratch_.resize(count * 7);
    mmd_world_read(raw_, scratch_.data(), static_cast<std::uint32_t>(count));
    out.clear();
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float* in = scratch_.data() + i * 7;
        out.push_back({Vec3(in[0], in[1], in[2]),
                       Quat(in[3], in[4], in[5], in[6]).normalize()});
    }
}

void World::reset(const std::vector<BodyTransform>& transforms) {
    pack(transforms);
    mmd_world_reset(raw_, scratch_.data(), static_cast<std::uint32_t>(transforms.size()));
}

} // namespace hikari::physics
