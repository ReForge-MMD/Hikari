#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"

namespace hikari::animation {

struct BoneMorphOffset {
    Vec3 translation;
    Quat rotation = Quat::identity();
};

struct MaterialMorphOffset {
    std::array<float, 4> diffuse{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> specular{};
    float shininess = 0.0f;
    std::array<float, 3> ambient{};
    std::array<float, 4> edge_color{0.0f, 0.0f, 0.0f, 1.0f};
    float edge_size = 0.0f;
};

struct ImpulseMorphOffset {
    std::size_t body_index = 0;
    bool local = false;
    Vec3 velocity;
    Vec3 torque;
};

struct MorphResult {
    std::vector<Vec3> position_offsets;
    std::vector<std::array<float, 2>> uv_offsets;
    std::vector<BoneMorphOffset> bone_offsets;
    std::vector<MaterialMorphOffset> material_offsets;
    std::vector<ImpulseMorphOffset> impulses;
};

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

    // Evaluates every PMX morph type: group, vertex, bone, UV/flip, material and impulse.
    // Extra UV channels (ExtraUv1..4) are evaluated by the format but have no shader
    // inputs in the viewport, so they do not contribute visible offsets.
    MorphResult evaluate() const;

    // Returns (position offsets, UV offsets) for the vertex pipeline.
    std::pair<std::vector<Vec3>, std::vector<std::array<float, 2>>> compute_offsets() const;
    std::vector<Vec3> compute_vertex_offsets(const formats::PmxModel& pmx) const;

private:
    std::size_t bone_count_ = 0;
    std::size_t material_count_ = 0;
    std::vector<std::array<float, 2>> base_uvs_;
    std::vector<formats::PmxMaterial> base_materials_;
    std::unordered_map<std::string, std::size_t> name_index_;
    std::unordered_map<std::size_t, float> index_weights_;
};

} // namespace hikari::animation
