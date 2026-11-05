#include "hikari/physics/rig.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace hikari::physics {
namespace {

Vec3 to_physics_position(const std::array<float, 3>& v) {
    return Vec3(v[0], v[1], -v[2]);
}

Quat to_physics_euler_yxz(const std::array<float, 3>& r) {
    return Quat::from_euler_yxz(-r[1], -r[0], r[2]);
}

std::pair<std::array<float, 3>, std::array<float, 3>> linear_range(
    const std::array<float, 3>& lower, const std::array<float, 3>& upper) {
    return {{lower[0], lower[1], -upper[2]}, {upper[0], upper[1], -lower[2]}};
}

std::pair<std::array<float, 3>, std::array<float, 3>> angular_range(
    const std::array<float, 3>& lower, const std::array<float, 3>& upper) {
    return {{-upper[0], -upper[1], lower[2]}, {-lower[0], -lower[1], upper[2]}};
}

} // namespace

BodyTransform compose(const BodyTransform& a, const BodyTransform& b) {
    return {a.position + a.rotation.rotate_vec3(b.position),
            (a.rotation * b.rotation).normalize()};
}

BodyTransform inverse(const BodyTransform& a) {
    const Quat rotation = a.rotation.inverse();
    return {-(rotation.rotate_vec3(a.position)), rotation};
}

float damping_rate(float fraction) {
    return -std::log(1.0f - std::clamp(fraction, 0.0f, 0.999f));
}

std::optional<PhysicsRig> PhysicsRig::create(
    const formats::PmxModel& model,
    const std::vector<std::array<float, 3>>& bone_rest_positions,
    const PhysicsSettings& settings) {
    if (model.rigid_bodies.empty()) {
        return std::nullopt;
    }

    WorldSettings world_settings;
    world_settings.gravity = Vec3(0.0f, -settings.gravity, 0.0f);
    world_settings.spring_damping_ratio = settings.spring_damping_ratio;
    world_settings.velocity_steps = settings.velocity_steps;
    world_settings.position_steps = settings.position_steps;

    PhysicsRig rig;
    rig.settings_ = settings;
    rig.world_ = World(world_settings, static_cast<std::uint32_t>(model.rigid_bodies.size() + 16));

    const std::size_t num_bones = bone_rest_positions.size();
    rig.drivers_.assign(num_bones, std::nullopt);

    const std::size_t num_bodies = model.rigid_bodies.size();
    std::vector<std::uint32_t> depths(num_bodies, std::numeric_limits<std::uint32_t>::max());
    std::deque<std::size_t> queue;
    for (std::size_t i = 0; i < num_bodies; ++i) {
        if (model.rigid_bodies[i].mode == 0) {
            depths[i] = 0;
            queue.push_back(i);
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(num_bodies);
    for (const auto& joint : model.joints) {
        const std::int32_t a = joint.body_a;
        const std::int32_t b = joint.body_b;
        if (a >= 0 && b >= 0 && static_cast<std::size_t>(a) < num_bodies &&
            static_cast<std::size_t>(b) < num_bodies && a != b) {
            adjacency[static_cast<std::size_t>(a)].push_back(static_cast<std::size_t>(b));
            adjacency[static_cast<std::size_t>(b)].push_back(static_cast<std::size_t>(a));
        }
    }

    while (!queue.empty()) {
        const std::size_t u = queue.front();
        queue.pop_front();
        const std::uint32_t next = depths[u] == std::numeric_limits<std::uint32_t>::max()
                                       ? depths[u]
                                       : depths[u] + 1;
        for (std::size_t v : adjacency[u]) {
            if (next < depths[v]) {
                depths[v] = next;
                queue.push_back(v);
            }
        }
    }

    std::vector<std::optional<std::size_t>> ids;
    ids.reserve(num_bodies);
    const float extra = damping_rate(settings.extra_damping);

    for (std::size_t body_index = 0; body_index < num_bodies; ++body_index) {
        const auto& rb = model.rigid_bodies[body_index];
        std::optional<std::size_t> bone;
        if (rb.bone_index >= 0 && static_cast<std::size_t>(rb.bone_index) < num_bones) {
            bone = static_cast<std::size_t>(rb.bone_index);
        }

        const BodyTransform world_tf{to_physics_position(rb.position),
                                     to_physics_euler_yxz(rb.rotation)};
        const BodyTransform bone_tf =
            bone ? BodyTransform{to_physics_position(bone_rest_positions[*bone]), Quat::identity()}
                 : BodyTransform::identity();

        const std::uint8_t mode = rb.mode;
        const std::uint32_t depth = depths[body_index];
        float mass = rb.mass > 0.0f ? rb.mass : 1.0f;
        float move_damping = rb.move_damping;
        if (mode != 0 && depth > 0 && depth != std::numeric_limits<std::uint32_t>::max()) {
            const float mass_scale = 1.0f / (1.0f + 0.25f * static_cast<float>(depth - 1));
            mass = std::max(mass * mass_scale, 0.01f);
            if (depth >= 2) {
                move_damping = std::max(rb.move_damping, 0.96f);
            }
        }

        MmdBodyDesc desc{};
        desc.shape = rb.shape == 1 ? MMD_SHAPE_BOX
                                   : (rb.shape == 2 ? MMD_SHAPE_CAPSULE : MMD_SHAPE_SPHERE);
        desc.size[0] = std::abs(rb.size[0]);
        desc.size[1] = std::abs(rb.size[1]);
        desc.size[2] = std::abs(rb.size[2]);
        desc.position[0] = world_tf.position.x;
        desc.position[1] = world_tf.position.y;
        desc.position[2] = world_tf.position.z;
        desc.rotation[0] = world_tf.rotation.x;
        desc.rotation[1] = world_tf.rotation.y;
        desc.rotation[2] = world_tf.rotation.z;
        desc.rotation[3] = world_tf.rotation.w;
        desc.mass = mass;
        desc.linear_damping = damping_rate(move_damping) + extra;
        desc.angular_damping = damping_rate(rb.rot_damping) + extra;
        desc.restitution = std::clamp(rb.restitution, 0.0f, 1.0f);
        desc.friction = std::max(rb.friction, 0.0f);
        desc.group = rb.collision_group & 0x0F;
        desc.collide_mask = static_cast<std::uint32_t>(rb.collision_mask) & 0xFFFFu;
        desc.kinematic = mode == 0 ? 1u : 0u;

        const auto id = rig.world_.add_body(desc);
        ids.push_back(id);
        if (!id) {
            continue;
        }

        const std::size_t index = rig.bodies_.size();
        rig.names_.push_back(rb.name_local);
        rig.debug_shapes_.push_back(rb.shape);
        rig.debug_sizes_.push_back({std::abs(rb.size[0]), std::abs(rb.size[1]), std::abs(rb.size[2])});
        rig.bodies_.push_back({bone, mode, compose(inverse(bone_tf), world_tf)});

        if (bone && mode != 0 && !rig.drivers_[*bone]) {
            rig.drivers_[*bone] = index;
        }
    }

    for (const auto& joint : model.joints) {
        const auto get_body = [&](std::int32_t i) -> std::optional<std::size_t> {
            if (i >= 0 && static_cast<std::size_t>(i) < ids.size()) {
                return ids[static_cast<std::size_t>(i)];
            }
            return std::nullopt;
        };
        const auto body_a = get_body(joint.body_a);
        const auto body_b = get_body(joint.body_b);
        if (!body_a || !body_b) {
            continue;
        }

        const std::uint32_t depth_a = joint.body_a >= 0 &&
                                              static_cast<std::size_t>(joint.body_a) < depths.size()
                                          ? depths[static_cast<std::size_t>(joint.body_a)]
                                          : std::numeric_limits<std::uint32_t>::max();
        const std::uint32_t depth_b = joint.body_b >= 0 &&
                                              static_cast<std::size_t>(joint.body_b) < depths.size()
                                          ? depths[static_cast<std::size_t>(joint.body_b)]
                                          : std::numeric_limits<std::uint32_t>::max();

        if (depth_a != depth_b &&
            depth_a != std::numeric_limits<std::uint32_t>::max() &&
            depth_b != std::numeric_limits<std::uint32_t>::max()) {
            const bool a_is_parent = depth_a < depth_b;
            const auto& parent_rb = a_is_parent ? model.rigid_bodies[static_cast<std::size_t>(joint.body_a)]
                                                : model.rigid_bodies[static_cast<std::size_t>(joint.body_b)];
            const auto& child_rb = a_is_parent ? model.rigid_bodies[static_cast<std::size_t>(joint.body_b)]
                                               : model.rigid_bodies[static_cast<std::size_t>(joint.body_a)];
            const Vec3 parent_pos = to_physics_position(parent_rb.position);
            const Vec3 child_pos = to_physics_position(child_rb.position);
            const float rest_distance = (child_pos - parent_pos).length();
            if (rest_distance > 1.0e-4f) {
                rig.distance_constraints_.push_back({a_is_parent ? *body_a : *body_b,
                                                     a_is_parent ? *body_b : *body_a,
                                                     rest_distance * 1.15f});
            }
        }

        const auto [linear_lower, linear_upper] =
            linear_range(joint.pos_min, joint.pos_max);
        const auto [angular_lower, angular_upper] =
            angular_range(joint.rot_min, joint.rot_max);

        MmdJointDesc desc{};
        desc.body_a = static_cast<std::int32_t>(*body_a);
        desc.body_b = static_cast<std::int32_t>(*body_b);
        const Vec3 position = to_physics_position(joint.position);
        const Quat rotation = to_physics_euler_yxz(joint.rotation);
        desc.position[0] = position.x;
        desc.position[1] = position.y;
        desc.position[2] = position.z;
        desc.rotation[0] = rotation.x;
        desc.rotation[1] = rotation.y;
        desc.rotation[2] = rotation.z;
        desc.rotation[3] = rotation.w;
        for (int i = 0; i < 3; ++i) {
            desc.linear_lower[i] = linear_lower[static_cast<std::size_t>(i)];
            desc.linear_upper[i] = linear_upper[static_cast<std::size_t>(i)];
            desc.angular_lower[i] = angular_lower[static_cast<std::size_t>(i)];
            desc.angular_upper[i] = angular_upper[static_cast<std::size_t>(i)];
            desc.linear_spring[i] = std::abs(joint.spring_pos[static_cast<std::size_t>(i)]) *
                                   settings.spring_scale;
            desc.angular_spring[i] = std::abs(joint.spring_rot[static_cast<std::size_t>(i)]) *
                                    settings.spring_scale;
        }
        rig.world_.add_joint(desc);

        rig.debug_joints_.push_back({joint.name_local, *body_a, *body_b,
                                     {position.x, position.y, position.z}});
    }

    rig.world_.finish(settings.rest_overlaps);

    const std::size_t count = rig.bodies_.size();
    rig.previous_targets_.assign(count, BodyTransform::identity());
    rig.targets_.assign(count, BodyTransform::identity());
    rig.interpolated_.assign(count, BodyTransform::identity());
    rig.current_.reserve(count);
    rig.overrides_.assign(num_bones, std::nullopt);
    rig.needs_reset_ = true;
    rig.accumulator_ = 0.0f;
    return rig;
}

bool PhysicsRig::is_bone_driven(std::size_t bone_index) const {
    return bone_index < drivers_.size() && drivers_[bone_index].has_value();
}

std::size_t PhysicsRig::dynamic_count() const {
    return static_cast<std::size_t>(
        std::count_if(bodies_.begin(), bodies_.end(),
                      [](const RigBody& body) { return body.mode != 0; }));
}

void PhysicsRig::set_gravity(float gravity) {
    settings_.gravity = gravity;
    world_.set_gravity(Vec3(0.0f, -gravity, 0.0f));
}

void PhysicsRig::set_gravity_vec(const std::array<float, 3>& gravity) {
    const Vec3 value = Vec3::from_array(gravity);
    settings_.gravity = value.length();
    world_.set_gravity(value);
}

void PhysicsRig::set_ground_enabled(bool enabled) {
    world_.set_ground_enabled(enabled);
}

void PhysicsRig::set_time_scale(float scale) {
    settings_.time_scale = std::clamp(scale, 0.1f, 4.0f);
}

void PhysicsRig::apply_impulse(std::size_t body_index, bool local, const Vec3& velocity,
                               const Vec3& torque) {
    if (body_index >= bodies_.size()) {
        return;
    }
    world_.add_impulse(body_index, local, velocity, torque);
}

void PhysicsRig::update_targets(const std::vector<BodyTransform>& bone_world) {
    for (std::size_t i = 0; i < targets_.size() && i < bodies_.size(); ++i) {
        const auto& body = bodies_[i];
        BodyTransform bone_tf = BodyTransform::identity();
        if (body.bone && *body.bone < bone_world.size()) {
            bone_tf = bone_world[*body.bone];
        }
        targets_[i] = compose(bone_tf, body.offset);
    }
}

const std::vector<std::optional<BoneOverride>>& PhysicsRig::step(
    const std::vector<BodyTransform>& bone_world, float dt) {
    update_targets(bone_world);

    const bool invalid_dt = !(dt > 0.0f) || dt > settings_.max_frame;
    dt *= settings_.time_scale;
    if (needs_reset_ || invalid_dt) {
        world_.reset(targets_);
        previous_targets_ = targets_;
        needs_reset_ = false;
        accumulator_ = 0.0f;

        world_.move_kinematic(targets_, settings_.max_substep);
        for (int i = 0; i < 2; ++i) {
            world_.step(settings_.max_substep, settings_.collision_steps);
        }
    } else {
        const float h = std::max(settings_.max_substep, 1.0e-5f);
        const std::size_t max_substeps = std::max<std::uint32_t>(settings_.max_substeps, 1);
        const float max_sim_dt = h * static_cast<float>(max_substeps);

        accumulator_ = std::min(accumulator_ + dt, max_sim_dt);
        float consumed = 0.0f;
        std::size_t steps = 0;

        while (accumulator_ >= h && steps < max_substeps) {
            accumulator_ -= h;
            consumed = std::min(consumed + h, dt);
            ++steps;

            const float alpha = dt > 0.0f ? std::min(consumed / dt, 1.0f) : 1.0f;
            for (std::size_t i = 0; i < interpolated_.size(); ++i) {
                interpolated_[i].position =
                    previous_targets_[i].position.lerp(targets_[i].position, alpha);
                interpolated_[i].rotation =
                    previous_targets_[i].rotation.slerp(targets_[i].rotation, alpha);
            }
            world_.move_kinematic(interpolated_, h);
            world_.step(h, settings_.collision_steps);
        }
        previous_targets_ = targets_;
    }

    world_.read(current_);

    for (const auto& constraint : distance_constraints_) {
        if (constraint.parent >= current_.size() || constraint.child >= current_.size()) {
            continue;
        }
        const Vec3 pa = current_[constraint.parent].position;
        const Vec3 pb = current_[constraint.child].position;
        const Vec3 delta = pb - pa;
        const float distance = delta.length();
        if (distance > constraint.max_distance && distance > 1.0e-4f) {
            current_[constraint.child].position =
                pa + delta * (constraint.max_distance / distance);
        }
    }

    std::fill(overrides_.begin(), overrides_.end(), std::nullopt);
    for (std::size_t bone = 0; bone < drivers_.size(); ++bone) {
        if (!drivers_[bone]) {
            continue;
        }
        const std::size_t index = *drivers_[bone];
        if (index >= bodies_.size() || index >= current_.size()) {
            continue;
        }
        const auto& transform = current_[index];
        if (!transform.position.is_finite() || !transform.rotation.is_finite()) {
            needs_reset_ = true;
            continue;
        }
        const BodyTransform bone_tf = compose(transform, inverse(bodies_[index].offset));
        BoneOverride value;
        value.kind = bodies_[index].mode == 2 ? BoneOverride::Kind::Rotation
                                              : BoneOverride::Kind::Full;
        value.position = bone_tf.position;
        value.rotation = bone_tf.rotation;
        overrides_[bone] = value;
    }

    return overrides_;
}

std::vector<RigidBodyDebugInfo> PhysicsRig::get_debug_rigid_bodies() const {
    std::vector<RigidBodyDebugInfo> result;
    result.reserve(bodies_.size());
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        const BodyTransform transform =
            i < current_.size() ? current_[i] : BodyTransform::identity();
        RigidBodyDebugInfo info;
        info.name = i < names_.size() ? names_[i] : std::string();
        info.bone_index = bodies_[i].bone;
        info.mode = bodies_[i].mode;
        info.shape = i < debug_shapes_.size() ? debug_shapes_[i] : 0;
        info.size = i < debug_sizes_.size() ? debug_sizes_[i]
                                            : std::array<float, 3>{1.0f, 1.0f, 1.0f};
        info.position = transform.position.to_array();
        info.rotation = transform.rotation.to_array();
        result.push_back(std::move(info));
    }
    return result;
}

} // namespace hikari::physics
