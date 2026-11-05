#include "hikari/animation/morph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace hikari::animation {
namespace {

struct MaterialMorphAccum {
    std::array<float, 4> add_diffuse{};
    std::array<float, 4> mul_diffuse{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> add_specular{};
    std::array<float, 3> mul_specular{1.0f, 1.0f, 1.0f};
    float add_shininess = 0.0f;
    float mul_shininess = 1.0f;
    std::array<float, 3> add_ambient{};
    std::array<float, 3> mul_ambient{1.0f, 1.0f, 1.0f};
    std::array<float, 4> add_edge_color{};
    std::array<float, 4> mul_edge_color{1.0f, 1.0f, 1.0f, 1.0f};
    float add_edge_size = 0.0f;
    float mul_edge_size = 1.0f;
};

void accumulate_channel(float& add, float& mul, float delta, float weight, bool additive) {
    if (additive) {
        add += delta * weight;
    } else {
        mul = (1.0f - (1.0f - delta) * weight) * mul;
    }
}

template <std::size_t Count>
void accumulate_channels(std::array<float, Count>& add, std::array<float, Count>& mul,
                         const std::array<float, Count>& delta, float weight, bool additive) {
    for (std::size_t i = 0; i < Count; ++i) {
        accumulate_channel(add[i], mul[i], delta[i], weight, additive);
    }
}

void accumulate_material(MaterialMorphAccum& accum, const formats::PmxMaterialMorphItem& item,
                         float weight) {
    const bool additive = item.calc_mode != 0;
    accumulate_channels(accum.add_diffuse, accum.mul_diffuse, item.diffuse, weight, additive);
    accumulate_channels(accum.add_specular, accum.mul_specular, item.specular, weight, additive);
    accumulate_channel(accum.add_shininess, accum.mul_shininess, item.shininess, weight, additive);
    accumulate_channels(accum.add_ambient, accum.mul_ambient, item.ambient, weight, additive);
    accumulate_channels(accum.add_edge_color, accum.mul_edge_color, item.edge_color, weight,
                        additive);
    accumulate_channel(accum.add_edge_size, accum.mul_edge_size, item.edge_size, weight, additive);
}

// MMD scales a bone-morph quaternion through axis-angle before accumulating it.
Quat scaled_rotation(const std::array<float, 4>& rotation, float weight) {
    const float x = rotation[0];
    const float y = rotation[1];
    const float z = rotation[2];
    const float w = std::clamp(rotation[3], -1.0f, 1.0f);
    float angle = std::acos(w);
    if (w < 0.0f) {
        angle -= 3.140000104904175f;
    }
    const float length = std::sqrt(x * x + y * y + z * z);
    if (angle == 0.0f || length < 1.1920929e-7f) {
        return Quat::identity();
    }
    const float scaled = angle * weight;
    const float scale = std::sin(scaled) / length;
    return Quat(x * scale, y * scale, z * scale, std::cos(scaled));
}

} // namespace

MorphEvaluator MorphEvaluator::from_pmx(const formats::PmxModel& pmx) {
    MorphEvaluator evaluator;
    evaluator.morphs = pmx.morphs;
    evaluator.vertex_count = pmx.vertices.size();
    evaluator.bone_count_ = pmx.bones.size();
    evaluator.material_count_ = pmx.materials.size();
    evaluator.base_uvs_.reserve(pmx.vertices.size());
    for (const auto& vertex : pmx.vertices) {
        evaluator.base_uvs_.push_back(vertex.uv);
    }
    evaluator.base_materials_ = pmx.materials;
    evaluator.name_index_.reserve(pmx.morphs.size() * 2);
    for (std::size_t i = 0; i < pmx.morphs.size(); ++i) {
        const auto& morph = pmx.morphs[i];
        if (!morph.name_local.empty()) {
            evaluator.name_index_.try_emplace(morph.name_local, i);
        }
        if (!morph.name_universal.empty()) {
            evaluator.name_index_.try_emplace(morph.name_universal, i);
        }
    }
    return evaluator;
}

void MorphEvaluator::set_weight(const std::string& name, float weight) {
    morph_weights[name] = weight;
    const auto index = name_index_.find(name);
    if (index != name_index_.end()) {
        index_weights_[index->second] = weight;
    }
}

float MorphEvaluator::get_weight(const std::string& name) const {
    const auto index = name_index_.find(name);
    if (index != name_index_.end()) {
        const auto weight = index_weights_.find(index->second);
        if (weight != index_weights_.end()) {
            return weight->second;
        }
    }
    const auto it = morph_weights.find(name);
    return it == morph_weights.end() ? 0.0f : it->second;
}

void MorphEvaluator::reset() {
    morph_weights.clear();
    index_weights_.clear();
}

MorphResult MorphEvaluator::evaluate() const {
    MorphResult result;
    if (morphs.empty()) {
        return result;
    }

    result.position_offsets.assign(vertex_count, Vec3::zero());
    result.uv_offsets.assign(vertex_count, {0.0f, 0.0f});
    result.bone_offsets.resize(bone_count_);
    std::vector<MaterialMorphAccum> material_accum(material_count_);
    std::vector<std::uint8_t> visiting(morphs.size(), 0);

    std::function<void(std::size_t, float, int)> accumulate =
        [&](std::size_t index, float weight, int depth) {
            if (depth > 8 || index >= morphs.size() || std::abs(weight) <= 1.0e-6f ||
                visiting[index] != 0) {
                return;
            }
            const auto& morph = morphs[index];
            switch (morph.type) {
            case formats::PmxMorphType::Group:
                visiting[index] = 1;
                for (const auto& item : morph.group_items) {
                    if (item.morph_index >= 0) {
                        accumulate(static_cast<std::size_t>(item.morph_index),
                                   weight * item.weight, depth + 1);
                    }
                }
                visiting[index] = 0;
                break;
            case formats::PmxMorphType::Vertex:
                for (const auto& item : morph.vertex_items) {
                    const auto vertex = static_cast<std::size_t>(item.vertex_index);
                    if (item.vertex_index >= 0 && vertex < vertex_count) {
                        result.position_offsets[vertex] =
                            result.position_offsets[vertex] +
                            Vec3::from_array(item.offset) * weight;
                    }
                }
                break;
            case formats::PmxMorphType::Uv:
                for (const auto& item : morph.uv_items) {
                    const auto vertex = static_cast<std::size_t>(item.vertex_index);
                    if (item.vertex_index >= 0 && vertex < vertex_count) {
                        result.uv_offsets[vertex][0] += item.offset[0] * weight;
                        result.uv_offsets[vertex][1] += item.offset[1] * weight;
                    }
                }
                break;
            case formats::PmxMorphType::ExtraUv1:
            case formats::PmxMorphType::ExtraUv2:
            case formats::PmxMorphType::ExtraUv3:
            case formats::PmxMorphType::ExtraUv4:
                // The viewport has no extra UV shader inputs, so only the base UV channel
                // can affect rendering.
                break;
            case formats::PmxMorphType::Flip:
                for (const auto& item : morph.group_items) {
                    const auto vertex = static_cast<std::size_t>(item.morph_index);
                    if (item.morph_index >= 0 && vertex < vertex_count) {
                        const auto& base = base_uvs_[vertex];
                        const float flip_weight = weight * item.weight;
                        result.uv_offsets[vertex][0] +=
                            (1.0f - 2.0f * base[0]) * flip_weight;
                        result.uv_offsets[vertex][1] +=
                            (1.0f - 2.0f * base[1]) * flip_weight;
                    }
                }
                break;
            case formats::PmxMorphType::Bone:
                for (const auto& item : morph.bone_items) {
                    const auto bone = static_cast<std::size_t>(item.bone_index);
                    if (item.bone_index < 0 || bone >= result.bone_offsets.size()) {
                        continue;
                    }
                    auto& offset = result.bone_offsets[bone];
                    offset.translation += Vec3::from_array(item.translation) * weight;
                    offset.rotation =
                        (offset.rotation * scaled_rotation(item.rotation, weight)).normalize();
                }
                break;
            case formats::PmxMorphType::Material:
                for (const auto& item : morph.material_items) {
                    if (item.material_index < 0) {
                        for (auto& accum : material_accum) {
                            accumulate_material(accum, item, weight);
                        }
                    } else {
                        const auto material = static_cast<std::size_t>(item.material_index);
                        if (material < material_accum.size()) {
                            accumulate_material(material_accum[material], item, weight);
                        }
                    }
                }
                break;
            case formats::PmxMorphType::Impulse:
                for (const auto& item : morph.impulse_items) {
                    if (item.rigid_body_index < 0) {
                        continue;
                    }
                    result.impulses.push_back(
                        {static_cast<std::size_t>(item.rigid_body_index), item.local != 0,
                         Vec3::from_array(item.velocity) * weight,
                         Vec3::from_array(item.torque) * weight});
                }
                break;
            }
        };

    for (std::size_t i = 0; i < morphs.size(); ++i) {
        const auto weight = index_weights_.find(i);
        if (weight != index_weights_.end()) {
            accumulate(i, weight->second, 0);
        }
    }

    result.material_offsets.resize(material_count_);
    for (std::size_t i = 0; i < material_count_ && i < base_materials_.size(); ++i) {
        const auto& base = base_materials_[i];
        const auto& accum = material_accum[i];
        auto& out = result.material_offsets[i];
        for (std::size_t c = 0; c < 4; ++c) {
            out.diffuse[c] = accum.mul_diffuse[c] * base.diffuse[c] + accum.add_diffuse[c];
        }
        for (std::size_t c = 0; c < 3; ++c) {
            out.specular[c] =
                accum.mul_specular[c] * base.specular[c] + accum.add_specular[c];
            out.ambient[c] = accum.mul_ambient[c] * base.ambient[c] + accum.add_ambient[c];
        }
        out.shininess = accum.mul_shininess * base.shininess + accum.add_shininess;
        for (std::size_t c = 0; c < 4; ++c) {
            out.edge_color[c] =
                accum.mul_edge_color[c] * base.edge_color[c] + accum.add_edge_color[c];
        }
        out.edge_size = accum.mul_edge_size * base.edge_size + accum.add_edge_size;
    }

    return result;
}

std::pair<std::vector<Vec3>, std::vector<std::array<float, 2>>> MorphEvaluator::compute_offsets()
    const {
    MorphResult result = evaluate();
    return {std::move(result.position_offsets), std::move(result.uv_offsets)};
}

std::vector<Vec3> MorphEvaluator::compute_vertex_offsets(const formats::PmxModel& pmx) const {
    MorphEvaluator evaluator = from_pmx(pmx);
    for (const auto& [name, weight] : morph_weights) {
        evaluator.set_weight(name, weight);
    }
    return evaluator.evaluate().position_offsets;
}

} // namespace hikari::animation
