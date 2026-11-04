#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hikari/animation/jiggle.hpp"
#include "hikari/math.hpp"
#include "hikari/physics/rig.hpp"

namespace hikari::animation {

// Exact MMD cubic Bezier evaluator for VMD keyframe interpolation.
float eval_mmd_bezier(std::uint8_t x1, std::uint8_t y1, std::uint8_t x2, std::uint8_t y2, float t);

// Channel: 0 = X, 1 = Y, 2 = Z, 3 = Rotation.
std::array<std::uint8_t, 4> unpack_bone_bezier(const std::array<std::uint8_t, 64>& buffer,
                                               std::size_t channel);

struct BoneNode {
    std::string name;
    std::string name_en;
    std::optional<std::size_t> parent_index;
    Vec3 initial_position;
    Vec3 offset;
    std::int32_t deform_layer = 0;
    std::uint16_t flags = 0;
    bool after_physics = false;
    bool has_append_parent = false;
    std::size_t append_parent = 0;
    float append_ratio = 0.0f;
    bool has_fixed_axis = false;
    Vec3 fixed_axis;
    bool has_ik = false;
    std::size_t ik_target_bone = 0;
    std::int32_t ik_loop_count = 1;
    float ik_angle_limit = 0.0f;
    std::vector<std::size_t> ik_links;
    std::vector<bool> ik_link_has_limits;
    std::vector<Vec3> ik_link_min;
    std::vector<Vec3> ik_link_max;
};

enum class BoneVisualType { IkController, UnderIk, MoveAndRotate, GrantAppend, StandardRotate };

class Skeleton {
public:
    std::vector<BoneNode> bones;
    std::vector<std::vector<std::size_t>> children;
    std::vector<Mat4> inv_bind_matrices;
    std::vector<Vec3> local_translations;
    std::vector<Quat> local_rotations;
    std::vector<Vec3> world_positions;
    std::vector<Quat> world_rotations;
    std::vector<Mat4> world_matrices;
    std::vector<Mat4> skinning_palette;
    std::vector<std::size_t> sorted_eval_order;

    static Skeleton from_pmx(const formats::PmxModel& pmx);

    std::optional<std::size_t> find_bone_index(const std::string& name) const;
    BoneVisualType get_bone_visual_type(std::size_t index) const;
    std::vector<BoneVisualType> bone_visual_types() const;

    void set_bone_transform(std::size_t bone_index, const Vec3& translation, const Quat& rotation);
    void reset_pose();
    void update_transforms();
    void update_transforms_stage(bool after_physics);
    void update_skinning_palette();
    void update_subtree(std::size_t index);

    std::vector<physics::BodyTransform> get_bone_world_transforms() const;
    void apply_physics(physics::PhysicsRig& rig, float dt);
    void apply_jiggle(Jiggle& jiggle, float dt, const JiggleSettings& settings);

private:
    void compute_world_matrix(std::size_t index, const Vec3& effective_translation,
                              const Quat& effective_rotation);
    void solve_ik(bool after_physics);
    void fill_bone_world_transforms(std::vector<physics::BodyTransform>& out) const;
    std::vector<physics::BodyTransform> bone_transform_scratch_;
};

std::string normalize_bone_name(const std::string& name);
std::vector<std::string> get_bone_aliases(const std::string& query);

} // namespace hikari::animation
