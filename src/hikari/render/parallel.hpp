#pragma once

#include <cstddef>
#include <vector>

#include "hikari/core/d3d11.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/scene_renderer.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::render {

struct ScenePassItem {
    enum class Kind { Grid, Mesh };
    Kind kind = Kind::Grid;
    const GpuMesh* mesh = nullptr;
    const std::vector<GpuMaterial>* materials = nullptr;
    const std::vector<std::optional<GpuTexture>>* textures = nullptr;
    const GpuSkinningBuffer* skinning = nullptr;
    std::size_t first_subset = 0;
    std::size_t last_subset = 0;

    static ScenePassItem grid() { return {}; }
    static ScenePassItem mesh_pass(const GpuMesh& mesh,
                                   const std::vector<GpuMaterial>& materials,
                                   const std::vector<std::optional<GpuTexture>>& textures,
                                   const GpuSkinningBuffer& skinning, std::size_t first,
                                   std::size_t last) {
        ScenePassItem item;
        item.kind = Kind::Mesh;
        item.mesh = &mesh;
        item.materials = &materials;
        item.textures = &textures;
        item.skinning = &skinning;
        item.first_subset = first;
        item.last_subset = last;
        return item;
    }
};

class ParallelFrameRenderer {
public:
    static std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> record_passes_parallel(
        core::D3D11Context& context, SceneRenderer& renderer,
        const std::vector<ScenePassItem>& passes);
    static Microsoft::WRL::ComPtr<ID3D11CommandList> record_single_pass(
        core::D3D11Context& context, SceneRenderer& renderer, ID3D11DeviceContext* deferred,
        const ScenePassItem& pass);
};

} // namespace hikari::render
