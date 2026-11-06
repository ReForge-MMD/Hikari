#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "hikari/animation/jiggle.hpp"
#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"
#include "hikari/physics/rig.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::studio {

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
    Vec3 shadow_center{0.0f, 0.0f, 0.0f};
    float shadow_radius = 10.0f;
    bool visible = true;
};

} // namespace hikari::studio
