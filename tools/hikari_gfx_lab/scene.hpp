#pragma once

#include <optional>
#include <string>
#include <vector>

#include "hikari/core/d3d11.hpp"
#include "hikari/formats/pmx.hpp"
#include "hikari/gfx/material.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::gfx_lab {

struct LabScene {
    std::string name;
    std::string source_path;
    hikari::formats::PmxModel pmx;
    hikari::render::GpuMesh mesh;
    std::vector<hikari::render::GpuMaterial> materials;
    std::vector<std::optional<hikari::render::GpuTexture>> textures;
    hikari::render::GpuSkinningBuffer skinning;
    // Material overrides indexed by subset, for scenes that need metallic or
    // emissive values the PMX format cannot express.
    std::vector<hikari::gfx::MaterialConstants> material_overrides;
    hikari::Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    hikari::Vec3 bounds_max{0.0f, 0.0f, 0.0f};
    std::size_t loaded_textures = 0;

    bool load(hikari::core::D3D11Context& context, const std::string& path,
              std::string* error = nullptr);
    static LabScene synthetic(hikari::core::D3D11Context& context);
    // Closed room with a metal floor, coloured walls, and a bright emitter. It
    // exercises AO corners, SSR parallax, and GI colour bleed.
    static LabScene phase5_room(hikari::core::D3D11Context& context);
    // Stone hall with three window openings, a pillar, and a low mist base. The
    // sun enters only through the openings, so the volumetric fog shows the
    // shafts and the pillar shadow.
    static LabScene phase6_room(hikari::core::D3D11Context& context);
    // Row of eleven spheres against a dark backdrop, one per shading model, so
    // the shared surface library can be compared side by side. `pmx_maps` adds
    // a matcap sphere map and PMX toon ramps to the entries that show them.
    // shading_model filters the chart to one sphere, which is what a per-model
    // golden captures; -1 keeps the whole row.
    static LabScene phase7_chart(hikari::core::D3D11Context& context, bool pmx_maps = false,
                                 int shading_model = -1);

    hikari::Vec3 center() const { return (bounds_min + bounds_max) * 0.5f; }
    float radius() const { return (bounds_max - bounds_min).length() * 0.5f; }
};

} // namespace hikari::gfx_lab
