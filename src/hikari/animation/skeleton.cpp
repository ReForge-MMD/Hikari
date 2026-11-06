#include "hikari/animation/skeleton.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <queue>

namespace hikari::animation {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::vector<std::string> split_words(const std::string& name) {
    std::vector<std::string> words;
    std::string current;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

bool contains_any(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

Skeleton Skeleton::from_pmx(const formats::PmxModel& pmx) {
    const std::size_t count = pmx.bones.size();
    Skeleton skeleton;
    skeleton.bones.reserve(count);
    skeleton.inv_bind_matrices.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& source = pmx.bones[i];
        BoneNode bone;
        bone.name = source.name_local;
        bone.name_en = source.name_universal;
        if (source.parent_index >= 0 && static_cast<std::size_t>(source.parent_index) < count) {
            bone.parent_index = static_cast<std::size_t>(source.parent_index);
        }
        bone.initial_position = Vec3::from_array(source.position);
        bone.offset = bone.parent_index
                          ? bone.initial_position - Vec3::from_array(pmx.bones[*bone.parent_index].position)
                          : bone.initial_position;
        bone.deform_layer = source.deform_layer;
        bone.flags = source.flags;
        bone.after_physics = (source.flags & 0x1000) != 0;
        if (source.has_append_parent && source.append_parent >= 0 &&
            static_cast<std::size_t>(source.append_parent) < count) {
            bone.has_append_parent = true;
            bone.append_parent = static_cast<std::size_t>(source.append_parent);
            bone.append_ratio = source.append_ratio;
        }
        if (source.has_fixed_axis) {
            bone.has_fixed_axis = true;
            bone.fixed_axis = Vec3::from_array(source.fixed_axis);
        }
        if (source.has_tail_bone && source.tail_bone >= 0 &&
            static_cast<std::size_t>(source.tail_bone) < count) {
            bone.has_tail_bone = true;
            bone.tail_bone = static_cast<std::size_t>(source.tail_bone);
        } else {
            bone.tail_offset = Vec3::from_array(source.tail_offset);
        }
        if (source.has_ik) {
            bone.has_ik = true;
            bone.ik_target_bone =
                source.ik.target_bone >= 0 ? static_cast<std::size_t>(source.ik.target_bone) : 0;
            bone.ik_loop_count = source.ik.loop_count;
            bone.ik_angle_limit = source.ik.angle_limit;
            for (const auto& link : source.ik.links) {
                if (link.bone_index >= 0 && static_cast<std::size_t>(link.bone_index) < count) {
                    bone.ik_links.push_back(static_cast<std::size_t>(link.bone_index));
                    bone.ik_link_has_limits.push_back(link.has_limits);
                    bone.ik_link_min.push_back(Vec3::from_array(link.limit_min));
                    bone.ik_link_max.push_back(Vec3::from_array(link.limit_max));
                }
            }
        }
        skeleton.bones.push_back(std::move(bone));
        skeleton.inv_bind_matrices.push_back(
            Mat4::from_translation(Vec3::zero() - Vec3::from_array(source.position)));
    }

    skeleton.local_translations.assign(count, Vec3::zero());
    skeleton.local_rotations.assign(count, Quat::identity());
    skeleton.ik_enabled.assign(count, true);
    skeleton.world_positions.assign(count, Vec3::zero());
    skeleton.world_rotations.assign(count, Quat::identity());
    skeleton.world_matrices.assign(count, Mat4::identity());
    skeleton.skinning_palette.assign(count, Mat4::identity());

    skeleton.children.assign(count, {});
    for (std::size_t i = 0; i < count; ++i) {
        if (skeleton.bones[i].parent_index) {
            skeleton.children[*skeleton.bones[i].parent_index].push_back(i);
        }
    }

    // Respect after-physics, deform layer, and parent hierarchy.
    std::vector<std::size_t> sorted(count);
    for (std::size_t i = 0; i < count; ++i) {
        sorted[i] = i;
    }
    std::stable_sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
        const auto& ba = skeleton.bones[a];
        const auto& bb = skeleton.bones[b];
        if (ba.after_physics != bb.after_physics) return !ba.after_physics;
        if (ba.deform_layer != bb.deform_layer) return ba.deform_layer < bb.deform_layer;
        return a < b;
    });

    std::vector<bool> placed(count, false);
    skeleton.sorted_eval_order.reserve(count);
    while (skeleton.sorted_eval_order.size() < count) {
        const std::size_t before = skeleton.sorted_eval_order.size();
        for (std::size_t index : sorted) {
            if (!placed[index] &&
                (!skeleton.bones[index].parent_index || placed[*skeleton.bones[index].parent_index])) {
                placed[index] = true;
                skeleton.sorted_eval_order.push_back(index);
            }
        }
        if (skeleton.sorted_eval_order.size() == before) {
            for (std::size_t index : sorted) {
                if (!placed[index]) {
                    placed[index] = true;
                    skeleton.sorted_eval_order.push_back(index);
                    break;
                }
            }
        }
    }

    skeleton.build_name_index();
    skeleton.update_transforms();
    return skeleton;
}

std::string normalize_bone_name(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    const auto append_ascii = [&result](char ch) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '_' || ch == '-') {
            return;
        }
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        result.push_back(ch);
    };
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == 0xE3 && i + 2 < name.size()) {
            // U+3000 IDEOGRAPHIC SPACE (E3 80 80).
            if (static_cast<unsigned char>(name[i + 1]) == 0x80 &&
                static_cast<unsigned char>(name[i + 2]) == 0x80) {
                i += 3;
                continue;
            }
        }
        if (c == 0xEF && i + 2 < name.size()) {
            // Full-width ASCII U+FF01..U+FF5E (EF BC 81 .. EF BD 9E).
            const unsigned char b1 = static_cast<unsigned char>(name[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(name[i + 2]);
            if (b1 == 0xBC && b2 >= 0x81) {
                append_ascii(static_cast<char>(b2 - 0x60));
                i += 3;
                continue;
            }
            if (b1 == 0xBD && b2 <= 0x9E) {
                append_ascii(static_cast<char>(b2 - 0x20));
                i += 3;
                continue;
            }
        }
        if (c < 0x80) {
            append_ascii(static_cast<char>(c));
            ++i;
            continue;
        }
        result.push_back(name[i]);
        ++i;
    }

    const auto begin = result.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = result.find_last_not_of(" \t\r\n");
    return result.substr(begin, end - begin + 1);
}

namespace {

const std::vector<std::vector<std::string>>& bone_alias_groups() {
    static const std::vector<std::vector<std::string>> groups = {
        {"全ての親", "Root", "Mother", "Parent"},
        {"センター", "Center"},
        {"グルーブ", "Groove"},
        {"下半身", "LowerBody", "Waist", "Pelvis"},
        {"上半身", "UpperBody", "Spine"},
        {"上半身2", "UpperBody2", "Chest"},
        {"首", "Neck"},
        {"頭", "Head"},
        {"左目", "Eye_L", "Left eye"},
        {"右目", "Eye_R", "Right eye"},
        {"両目", "Eyes", "Both eyes"},
        {"左肩", "Shoulder_L", "Left shoulder"},
        {"右肩", "Shoulder_R", "Right shoulder"},
        {"左腕", "Arm_L", "Left arm"},
        {"右腕", "Arm_R", "Right arm"},
        {"左ひじ", "Elbow_L", "Left elbow"},
        {"右ひじ", "Elbow_R", "Right elbow"},
        {"左手首", "Wrist_L", "Left wrist"},
        {"右手首", "Wrist_R", "Right wrist"},
        {"左足", "Leg_L", "Left leg"},
        {"右足", "Leg_R", "Right leg"},
        {"左ひざ", "Knee_L", "Left knee"},
        {"右ひざ", "Knee_R", "Right knee"},
        {"左足首", "Ankle_L", "Left ankle"},
        {"右足首", "Ankle_R", "Right ankle"},
        {"左足IK", "Leg_IK_L", "Left leg IK"},
        {"右足IK", "Leg_IK_R", "Right leg IK"},
        {"左つま先IK", "Toe_IK_L", "Left toe IK"},
        {"右つま先IK", "Toe_IK_R", "Right toe IK"},
    };
    return groups;
}

} // namespace

std::vector<std::string> get_bone_aliases(const std::string& query) {
    for (const auto& group : bone_alias_groups()) {
        for (const auto& name : group) {
            if (normalize_bone_name(name) == query) {
                return group;
            }
        }
    }
    return {};
}

void Skeleton::build_name_index() {
    name_index_.clear();
    name_index_.reserve(bones.size() * 4);
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (!bone.name.empty()) {
            name_index_.try_emplace(bone.name, i);
        }
        if (!bone.name_en.empty()) {
            name_index_.try_emplace(bone.name_en, i);
        }
        const std::string normalized = normalize_bone_name(bone.name);
        if (!normalized.empty()) {
            name_index_.try_emplace(normalized, i);
        }
        const std::string normalized_en = normalize_bone_name(bone.name_en);
        if (!normalized_en.empty()) {
            name_index_.try_emplace(normalized_en, i);
        }
    }
    for (const auto& group : bone_alias_groups()) {
        std::optional<std::size_t> index;
        for (const auto& alias : group) {
            const std::string target = normalize_bone_name(alias);
            if (target.empty()) {
                continue;
            }
            for (std::size_t i = 0; i < bones.size() && !index; ++i) {
                if (normalize_bone_name(bones[i].name) == target ||
                    normalize_bone_name(bones[i].name_en) == target) {
                    index = i;
                }
            }
            if (index) {
                break;
            }
        }
        if (!index) {
            continue;
        }
        for (const auto& alias : group) {
            const std::string target = normalize_bone_name(alias);
            if (!target.empty()) {
                name_index_.try_emplace(target, *index);
            }
        }
    }
}

std::optional<std::size_t> Skeleton::find_bone_index(const std::string& name) const {
    if (const auto it = name_index_.find(name); it != name_index_.end()) {
        return it->second;
    }
    if (const auto it = name_index_.find(normalize_bone_name(name)); it != name_index_.end()) {
        return it->second;
    }
    return std::nullopt;
}

BoneVisualType Skeleton::get_bone_visual_type(std::size_t index) const {
    if (index >= bones.size()) {
        return BoneVisualType::StandardRotate;
    }
    const auto& bone = bones[index];
    if (bone.has_ik || (bone.flags & 0x0020) != 0) {
        return BoneVisualType::IkController;
    }
    for (const auto& other : bones) {
        if (other.has_ik) {
            if (std::find(other.ik_links.begin(), other.ik_links.end(), index) != other.ik_links.end()) {
                return BoneVisualType::UnderIk;
            }
        }
    }
    if ((bone.flags & 0x0100) != 0 || (bone.flags & 0x0200) != 0 || bone.has_append_parent) {
        return BoneVisualType::GrantAppend;
    }
    if ((bone.flags & 0x0004) != 0 || !bone.parent_index) {
        return BoneVisualType::MoveAndRotate;
    }
    return BoneVisualType::StandardRotate;
}

std::vector<BoneVisualType> Skeleton::bone_visual_types() const {
    std::vector<bool> is_ik_link(bones.size(), false);
    for (const auto& bone : bones) {
        if (bone.has_ik) {
            for (std::size_t link : bone.ik_links) {
                if (link < is_ik_link.size()) {
                    is_ik_link[link] = true;
                }
            }
        }
    }
    std::vector<BoneVisualType> types;
    types.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (bone.has_ik || (bone.flags & 0x0020) != 0) {
            types.push_back(BoneVisualType::IkController);
        } else if (is_ik_link[i]) {
            types.push_back(BoneVisualType::UnderIk);
        } else if ((bone.flags & 0x0100) != 0 || (bone.flags & 0x0200) != 0 ||
                   bone.has_append_parent) {
            types.push_back(BoneVisualType::GrantAppend);
        } else if ((bone.flags & 0x0004) != 0 || !bone.parent_index) {
            types.push_back(BoneVisualType::MoveAndRotate);
        } else {
            types.push_back(BoneVisualType::StandardRotate);
        }
    }
    return types;
}

void Skeleton::set_bone_transform(std::size_t bone_index, const Vec3& translation, const Quat& rotation) {
    if (bone_index < bones.size()) {
        local_translations[bone_index] = translation;
        local_rotations[bone_index] = rotation;
    }
}

void Skeleton::reset_pose() {
    std::fill(local_translations.begin(), local_translations.end(), Vec3::zero());
    std::fill(local_rotations.begin(), local_rotations.end(), Quat::identity());
    update_transforms();
}

void Skeleton::update_transforms() {
    update_transforms_stage(false);
    update_transforms_stage(true);
    update_skinning_palette();
}

void Skeleton::update_transforms_stage(bool after_physics) {
    for (std::size_t idx : sorted_eval_order) {
        if (bones[idx].after_physics != after_physics) {
            continue;
        }
        Vec3 grant_translation = Vec3::zero();
        Quat grant_rotation = Quat::identity();
        if (bones[idx].has_append_parent) {
            const auto parent = bones[idx].append_parent;
            if ((bones[idx].flags & 0x0100) != 0) {
                grant_rotation = Quat::identity().slerp(local_rotations[parent], bones[idx].append_ratio);
            }
            if ((bones[idx].flags & 0x0200) != 0) {
                grant_translation = local_translations[parent] * bones[idx].append_ratio;
            }
        }
        const Vec3 effective_translation = local_translations[idx] + grant_translation;
        const Quat effective_rotation = (grant_rotation * local_rotations[idx]).normalize();
        compute_world_matrix(idx, effective_translation, effective_rotation);
    }
    solve_ik(after_physics);
}

void Skeleton::update_skinning_palette() {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        skinning_palette[i] = inv_bind_matrices[i] * world_matrices[i];
    }
}

void Skeleton::compute_world_matrix(std::size_t index, const Vec3& effective_translation,
                                    const Quat& effective_rotation) {
    const auto& bone = bones[index];
    const Vec3 local_t = bone.offset + effective_translation;
    if (bone.parent_index) {
        const Vec3 parent_position = world_positions[*bone.parent_index];
        const Quat parent_rotation = world_rotations[*bone.parent_index];
        world_positions[index] = parent_position + parent_rotation.rotate_vec3(local_t);
        world_rotations[index] = (parent_rotation * effective_rotation).normalize();
    } else {
        world_positions[index] = local_t;
        world_rotations[index] = effective_rotation.normalize();
    }
    world_matrices[index] =
        Mat4::from_translation_rotation(world_positions[index], world_rotations[index]);
}

void Skeleton::update_subtree(std::size_t index) {
    Vec3 grant_translation = Vec3::zero();
    Quat grant_rotation = Quat::identity();
    if (bones[index].has_append_parent) {
        const auto parent = bones[index].append_parent;
        if ((bones[index].flags & 0x0100) != 0) {
            grant_rotation = Quat::identity().slerp(local_rotations[parent], bones[index].append_ratio);
        }
        if ((bones[index].flags & 0x0200) != 0) {
            grant_translation = local_translations[parent] * bones[index].append_ratio;
        }
    }
    const Vec3 effective_translation = local_translations[index] + grant_translation;
    const Quat effective_rotation = (grant_rotation * local_rotations[index]).normalize();
    compute_world_matrix(index, effective_translation, effective_rotation);
    for (std::size_t child : children[index]) {
        update_subtree(child);
    }
}

void Skeleton::solve_ik(bool after_physics) {
    for (std::size_t ik_bone = 0; ik_bone < bones.size(); ++ik_bone) {
        if (bones[ik_bone].after_physics != after_physics || !bones[ik_bone].has_ik) {
            continue;
        }
        if (ik_bone < ik_enabled.size() && !ik_enabled[ik_bone]) {
            continue;
        }
        const auto& bone = bones[ik_bone];
        const std::size_t target_bone = bone.ik_target_bone;
        if (target_bone >= bones.size()) {
            continue;
        }
        const int max_loops = std::clamp(bone.ik_loop_count, 1, 100);
        const float angle_limit = std::max(bone.ik_angle_limit, 0.001f);

        for (int loop = 0; loop < max_loops; ++loop) {
            for (std::size_t l = 0; l < bone.ik_links.size(); ++l) {
                const std::size_t link_index = bone.ik_links[l];
                if (link_index >= bones.size()) {
                    continue;
                }

                const Vec3 target_pos = world_matrices[target_bone].translation();
                const Vec3 goal_pos = world_matrices[ik_bone].translation();
                if ((target_pos - goal_pos).length_squared() < 1.0e-6f) {
                    break;
                }

                const Vec3 link_pos = world_matrices[link_index].translation();
                const Vec3 target_world = target_pos - link_pos;
                const Vec3 goal_world = goal_pos - link_pos;
                if (target_world.length_squared() < 1.0e-8f || goal_world.length_squared() < 1.0e-8f) {
                    continue;
                }

                const Quat inv_rot = world_rotations[link_index].inverse();
                const Vec3 target_dir = inv_rot.rotate_vec3(target_world).normalize();
                const Vec3 goal_dir = inv_rot.rotate_vec3(goal_world).normalize();
                const float dot = std::clamp(target_dir.dot(goal_dir), -1.0f, 1.0f);
                if (dot > 0.999999f) {
                    continue;
                }

                const float angle = std::min(std::acos(dot), angle_limit);
                Vec3 axis = target_dir.cross(goal_dir);
                const float axis_len_sq = axis.length_squared();
                if (axis_len_sq < 1.0e-8f) {
                    continue;
                }
                axis = axis * (1.0f / std::sqrt(axis_len_sq));

                if (bone.ik_link_has_limits[l]) {
                    const Vec3 limit_min = bone.ik_link_min[l];
                    const Vec3 limit_max = bone.ik_link_max[l];
                    const bool is_x_hinge = std::abs(limit_max.y - limit_min.y) < 1.0e-4f &&
                                            std::abs(limit_max.z - limit_min.z) < 1.0e-4f;
                    if (is_x_hinge) {
                        const Vec3 hinge_axis(axis.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                        const Quat delta = Quat::from_axis_angle(hinge_axis, angle);
                        const Quat trial = (local_rotations[link_index] * delta).normalize();
                        Vec3 euler = trial.to_euler_xyz();
                        euler.x = std::clamp(euler.x, limit_min.x, limit_max.x);
                        euler.y = 0.0f;
                        euler.z = 0.0f;
                        local_rotations[link_index] = Quat::from_euler_xyz(euler);
                    } else {
                        const Quat delta = Quat::from_axis_angle(axis, angle);
                        const Quat trial = (local_rotations[link_index] * delta).normalize();
                        Vec3 euler = trial.to_euler_xyz();
                        euler.x = std::clamp(euler.x, limit_min.x, limit_max.x);
                        euler.y = std::clamp(euler.y, limit_min.y, limit_max.y);
                        euler.z = std::clamp(euler.z, limit_min.z, limit_max.z);
                        local_rotations[link_index] = Quat::from_euler_xyz(euler);
                    }
                } else {
                    const Quat delta = Quat::from_axis_angle(axis, angle);
                    local_rotations[link_index] = (local_rotations[link_index] * delta).normalize();
                }

                update_subtree(link_index);
            }
        }
    }
}

void Skeleton::fill_bone_world_transforms(std::vector<physics::BodyTransform>& out) const {
    out.clear();
    out.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const Vec3 pos = world_positions[i];
        const Quat rot = world_rotations[i];
        out.push_back(physics::BodyTransform{
            Vec3(pos.x, pos.y, -pos.z),
            Quat(-rot.x, -rot.y, rot.z, rot.w).normalize()});
    }
}

std::vector<physics::BodyTransform> Skeleton::get_bone_world_transforms() const {
    std::vector<physics::BodyTransform> transforms;
    fill_bone_world_transforms(transforms);
    return transforms;
}

void Skeleton::reset_physics_bones(const physics::PhysicsRig& rig) {
    bool changed = false;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (!rig.is_bone_driven(i)) {
            continue;
        }
        local_translations[i] = Vec3::zero();
        local_rotations[i] = Quat::identity();
        changed = true;
    }
    if (changed) {
        update_transforms();
    }
}

void Skeleton::apply_physics(physics::PhysicsRig& rig, float dt) {
    fill_bone_world_transforms(bone_transform_scratch_);
    const auto& overrides = rig.step(bone_transform_scratch_, dt);

    for (std::size_t idx : sorted_eval_order) {
        if (bones[idx].after_physics) {
            continue;
        }
        Vec3 parent_position = Vec3::zero();
        Quat parent_rotation = Quat::identity();
        if (bones[idx].parent_index) {
            parent_position = world_positions[*bones[idx].parent_index];
            parent_rotation = world_rotations[*bones[idx].parent_index];
        }
        if (idx >= overrides.size() || !overrides[idx]) {
            continue;
        }
        const auto& override_value = *overrides[idx];
        if (override_value.kind == physics::BoneOverride::Kind::Full) {
            const Vec3& jolt_pos = override_value.position;
            const Quat& jolt_rot = override_value.rotation;
            const Vec3 mmd_pos(jolt_pos.x, jolt_pos.y, -jolt_pos.z);
            const Quat mmd_rot = Quat(-jolt_rot.x, -jolt_rot.y, jolt_rot.z, jolt_rot.w).normalize();
            const Quat inv_parent = parent_rotation.inverse();
            local_rotations[idx] = (inv_parent * mmd_rot).normalize();
            local_translations[idx] =
                inv_parent.rotate_vec3(mmd_pos - parent_position) - bones[idx].offset;
            world_positions[idx] = mmd_pos;
            world_rotations[idx] = mmd_rot;
            world_matrices[idx] = Mat4::from_translation_rotation(mmd_pos, mmd_rot);
        } else {
            const Quat& jolt_rot = override_value.rotation;
            const Quat mmd_rot = Quat(-jolt_rot.x, -jolt_rot.y, jolt_rot.z, jolt_rot.w).normalize();
            const Quat inv_parent = parent_rotation.inverse();
            local_rotations[idx] = (inv_parent * mmd_rot).normalize();
            const Vec3 world_pos =
                parent_position +
                parent_rotation.rotate_vec3(bones[idx].offset + local_translations[idx]);
            world_positions[idx] = world_pos;
            world_rotations[idx] = mmd_rot;
            world_matrices[idx] = Mat4::from_translation_rotation(world_pos, mmd_rot);
        }
    }

    update_transforms_stage(true);
    update_skinning_palette();
}

void Skeleton::apply_jiggle(Jiggle& jiggle, float dt, const JiggleSettings& settings) {
    const auto* offsets = jiggle.step(world_positions, world_rotations, dt, settings);
    if (offsets == nullptr) {
        return;
    }
    for (std::size_t idx : sorted_eval_order) {
        if (idx >= offsets->size() || !(*offsets)[idx]) {
            continue;
        }
        world_positions[idx] = world_positions[idx] + *(*offsets)[idx];
        world_matrices[idx] = Mat4::from_translation_rotation(world_positions[idx], world_rotations[idx]);
        for (std::size_t child : children[idx]) {
            update_subtree(child);
        }
    }
    update_skinning_palette();
}

} // namespace hikari::animation
