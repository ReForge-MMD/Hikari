#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "hikari/animation/jiggle.hpp"
#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/formats/pmx.hpp"
#include "hikari/gfx/material.hpp"
#include "hikari/math.hpp"
#include "hikari/physics/rig.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::studio {

// Axis-aligned bounds of one material subset in authored model space. The
// radius is the half diagonal, so a rotated model needs only its center
// transformed and its radius scaled for frustum tests.
struct SubsetBounds {
    Vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

// One loaded PMX model with its GPU resources, animation state and physics rig.
struct ModelInstance {
    QString name;
    QString path;
    formats::PmxModel pmx;
    render::GpuMesh mesh;
    render::GpuSkinningBuffer skinning;
    std::vector<render::GpuMaterial> materials;
    std::vector<std::optional<render::GpuTexture>> textures;
    animation::Skeleton skeleton;
    std::optional<physics::PhysicsRig> physics_rig;
    std::optional<animation::Jiggle> jiggle;
    animation::JiggleSettings jiggle_settings;
    animation::MorphEvaluator morphs;

    Vec3 offset{0.0f, 0.0f, 0.0f};
    // Model-space rotation around the authored origin. It is applied after
    // skinning together with the uniform scale, so poses, morphs and physics keep
    // their authored model-space units.
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    // Uniform model scale applied after skinning.
    float scale = 1.0f;
    Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    Vec3 bounds_max{0.0f, 0.0f, 0.0f};
    Vec3 shadow_center{0.0f, 0.0f, 0.0f};
    float shadow_radius = 10.0f;
    bool visible = true;
    std::vector<SubsetBounds> subset_bounds;
    // Per-subset material response overrides, indexed like mesh.subsets. An
    // empty entry keeps the authored PMX material for that subset. The response
    // only replaces the shading model and the PBR response, so the authored
    // colour, edge ink and texture slots keep coming from the PMX material and
    // material morphs still reach the surface.
    std::vector<std::optional<gfx::MaterialResponse>> material_overrides;

    // Row-vector model matrix: scale, then rotation, then translation.
    Mat4 model_matrix() const {
        return Mat4::from_scale(Vec3(scale, scale, scale)) * Mat4::from_quat(rotation) *
               Mat4::from_translation(offset);
    }

    Vec3 model_point(const Vec3& point) const { return model_matrix().transform_point(point); }

    Vec3 model_direction(const Vec3& direction) const {
        return rotation.rotate_vec3(direction) * scale;
    }

    Vec3 bounds_center() const { return (bounds_min + bounds_max) * 0.5f; }

    float bounds_radius() const {
        return (bounds_max - bounds_min).length() * 0.5f;
    }
};

} // namespace hikari::studio
