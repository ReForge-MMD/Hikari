#include "hikari/animation/morph.hpp"

#include <functional>
#include <unordered_map>

namespace hikari::animation {

MorphEvaluator MorphEvaluator::from_pmx(const formats::PmxModel& pmx) {
    MorphEvaluator evaluator;
    evaluator.morphs = pmx.morphs;
    evaluator.vertex_count = pmx.vertices.size();
    return evaluator;
}

void MorphEvaluator::set_weight(const std::string& name, float weight) {
    morph_weights[name] = weight;
}

float MorphEvaluator::get_weight(const std::string& name) const {
    const auto it = morph_weights.find(name);
    return it == morph_weights.end() ? 0.0f : it->second;
}

void MorphEvaluator::reset() {
    morph_weights.clear();
}

std::pair<std::vector<Vec3>, std::vector<std::array<float, 2>>> MorphEvaluator::compute_offsets() const {
    if (vertex_count == 0 || morphs.empty()) {
        return {};
    }

    std::unordered_map<std::size_t, float> vertex_weights;
    std::unordered_map<std::size_t, float> uv_weights;

    std::function<void(std::size_t, float, int)> accumulate =
        [&](std::size_t index, float current_weight, int depth) {
            if (depth > 4 || index >= morphs.size() || current_weight <= 1.0e-5f) {
                return;
            }
            const auto& morph = morphs[index];
            switch (morph.type) {
            case formats::PmxMorphType::Vertex:
                vertex_weights[index] += current_weight;
                break;
            case formats::PmxMorphType::Uv:
                uv_weights[index] += current_weight;
                break;
            case formats::PmxMorphType::Group:
                for (const auto& item : morph.group_items) {
                    if (item.morph_index >= 0) {
                        accumulate(static_cast<std::size_t>(item.morph_index),
                                   current_weight * item.weight, depth + 1);
                    }
                }
                break;
            default:
                break;
            }
        };

    for (std::size_t i = 0; i < morphs.size(); ++i) {
        const float weight = get_weight(morphs[i].name_local);
        if (weight > 1.0e-5f) {
            accumulate(i, weight, 0);
        }
    }

    if (vertex_weights.empty() && uv_weights.empty()) {
        return {};
    }

    std::vector<Vec3> position_offsets(vertex_count, Vec3::zero());
    std::vector<std::array<float, 2>> uv_offsets(vertex_count, {0.0f, 0.0f});

    for (const auto& [morph_index, total_weight] : vertex_weights) {
        const auto& morph = morphs[morph_index];
        if (morph.type != formats::PmxMorphType::Vertex) {
            continue;
        }
        for (const auto& item : morph.vertex_items) {
            const auto index = static_cast<std::size_t>(item.vertex_index);
            if (index < vertex_count) {
                position_offsets[index] =
                    position_offsets[index] + Vec3::from_array(item.offset) * total_weight;
            }
        }
    }

    for (const auto& [morph_index, total_weight] : uv_weights) {
        const auto& morph = morphs[morph_index];
        if (morph.type != formats::PmxMorphType::Uv) {
            continue;
        }
        for (const auto& item : morph.uv_items) {
            const auto index = static_cast<std::size_t>(item.vertex_index);
            if (index < vertex_count) {
                uv_offsets[index][0] += item.offset[0] * total_weight;
                uv_offsets[index][1] += item.offset[1] * total_weight;
            }
        }
    }

    return {std::move(position_offsets), std::move(uv_offsets)};
}

std::vector<Vec3> MorphEvaluator::compute_vertex_offsets(const formats::PmxModel& pmx) const {
    MorphEvaluator evaluator = from_pmx(pmx);
    evaluator.morph_weights = morph_weights;
    return evaluator.compute_offsets().first;
}

} // namespace hikari::animation
