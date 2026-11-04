#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/render/camera.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::render {

struct CameraConstants {
    std::array<std::array<float, 4>, 4> view{};
    std::array<std::array<float, 4>, 4> projection{};
    std::array<std::array<float, 4>, 4> view_projection{};
    std::array<float, 4> camera_pos{};
};

struct GridVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
};

struct OverlayVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
};

class SceneRenderer {
public:
    explicit SceneRenderer(core::D3D11Context& context);

    Microsoft::WRL::ComPtr<ID3D11Buffer> camera_buffer;
    GpuSkinningBuffer skinning_buffer;
    GpuTexture default_white_texture;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> default_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> default_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> grid_vb;
    std::uint32_t grid_vertex_count = 0;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> grid_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> grid_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> grid_layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bg_buffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> bg_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> bg_ps;
    bool wireframe = false;
    Microsoft::WRL::ComPtr<ID3D11Buffer> overlay_vb;
    std::size_t overlay_capacity = 0;

    void render_overlay_lines(core::D3D11Context& context,
                              const std::vector<OverlayVertex>& vertices);

    void update_camera_on(ID3D11DeviceContext* context, const Camera& camera) const;
    void update_camera(core::D3D11Context& context, const Camera& camera) const;
    void render_viewport_background(core::D3D11Context& context, const std::array<float, 4>& color);
    void render_grid_to(core::D3D11Context& context, ID3D11DeviceContext* target);
    void render_grid(core::D3D11Context& context, const Camera& camera);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_grid(core::D3D11Context& context,
                                                          ID3D11DeviceContext* deferred);
    void render_mesh_subsets_to(core::D3D11Context& context, ID3D11DeviceContext* target,
                                const GpuMesh& mesh, const std::vector<GpuMaterial>& materials,
                                const std::vector<std::optional<GpuTexture>>& textures,
                                const GpuSkinningBuffer& skinning, std::size_t first_subset,
                                std::size_t last_subset);
    void render_mesh_to(core::D3D11Context& context, ID3D11DeviceContext* target,
                        const GpuMesh& mesh, const std::vector<GpuMaterial>& materials,
                        const std::vector<std::optional<GpuTexture>>& textures,
                        const GpuSkinningBuffer& skinning);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_mesh_subsets(
        core::D3D11Context& context, ID3D11DeviceContext* deferred, const GpuMesh& mesh,
        const std::vector<GpuMaterial>& materials,
        const std::vector<std::optional<GpuTexture>>& textures, const GpuSkinningBuffer& skinning,
        std::size_t first_subset, std::size_t last_subset);
    std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> record_mesh_subsets_parallel(
        core::D3D11Context& context, const GpuMesh& mesh,
        const std::vector<GpuMaterial>& materials,
        const std::vector<std::optional<GpuTexture>>& textures, const GpuSkinningBuffer& skinning,
        std::size_t num_threads);
    void render_mesh(core::D3D11Context& context, const GpuMesh& mesh,
                     const std::vector<GpuMaterial>& materials,
                     const std::vector<std::optional<GpuTexture>>& textures,
                     const std::vector<Mat4>& skinning_palette, const Camera& camera);
};

} // namespace hikari::render
