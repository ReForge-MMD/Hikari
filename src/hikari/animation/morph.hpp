#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"

namespace hikari::animation {

class MorphEvaluator {
public:
    std::vector<formats::PmxMorph> morphs;
    std::size_t vertex_count = 0;
    std::unordered_map<std::string, float> morph_weights;

    MorphEvaluator() = default;
    static MorphEvaluator from_pmx(const formats::PmxModel& pmx);

    void set_weight(const std::string& name, float weight);
    float get_weight(const std::string& name) const;
    void reset();

    // Returns (position offsets, UV offsets). Both vectors are empty when no morph
    // contributes, so the caller can keep the previous mesh state.
    std::pair<std::vector<Vec3>, std::vector<std::array<float, 2>>> compute_offsets() const;
    std::vector<Vec3> compute_vertex_offsets(const formats::PmxModel& pmx) const;
};

} // namespace hikari::animation
