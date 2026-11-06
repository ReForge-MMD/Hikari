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
    std::array<float, 4> light_color{0.6f, 0.6f, 0.6f, 1.0f};
    std::array<float, 4> light_direction{-0.5f, -1.0f, -0.5f, 0.0f};
};

struct GridVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
};

struct OverlayVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
};

// One material subset draw. Depth is the view-space distance used to sort
// transparent subsets back to front; it is ignored for opaque draws.
struct SubsetDraw {
    const GpuMesh* mesh = nullptr;
    const std::vector<GpuMaterial>* materials = nullptr;
    const std::vector<std::optional<GpuTexture>>* textures = nullptr;
    const GpuSkinningBuffer* skinning = nullptr;
    std::size_t subset = 0;
    float depth = 0.0f;
};

struct GroundShadow {
    Vec3 center;
    float radius = 1.0f;
    std::array<float, 4> color{};
};

class SceneRenderer {
public:
    explicit SceneRenderer(core::D3D11Context& context);

    Microsoft::WRL::ComPtr<ID3D11Buffer> camera_buffer;
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
    std::array<float, 4> light_color{0.6f, 0.6f, 0.6f, 1.0f};
    std::array<float, 4> light_direction{-0.5f, -1.0f, -0.5f, 0.0f};

    void set_light(const std::array<float, 3>& color, const std::array<float, 3>& direction);
    void update_camera_on(ID3D11DeviceContext* context, const Camera& camera) const;
    void update_camera(core::D3D11Context& context, const Camera& camera) const;

    Microsoft::WRL::ComPtr<ID3D11CommandList> record_background(
        core::D3D11Context& context, const std::array<float, 4>& color);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_grid(core::D3D11Context& context);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_ground_shadows(
        core::D3D11Context& context, const std::vector<GroundShadow>& shadows);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_overlay_lines(
        core::D3D11Context& context, const std::vector<OverlayVertex>& vertices);
    Microsoft::WRL::ComPtr<ID3D11CommandList> record_mesh_draws(
        core::D3D11Context& context, ID3D11DeviceContext* deferred, const SubsetDraw* draws,
        std::size_t count, bool transparent);
    std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> record_mesh_draws_parallel(
        core::D3D11Context& context, const std::vector<SubsetDraw>& draws, bool transparent,
        std::size_t max_threads = 0);

private:
    void render_mesh_draws_to(core::D3D11Context& context, ID3D11DeviceContext* target,
                              const SubsetDraw* draws, std::size_t count, bool transparent);
    void upload_overlay_vertices(core::D3D11Context& context,
                                 Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
                                 std::size_t& capacity,
                                 const std::vector<OverlayVertex>& vertices);

    Microsoft::WRL::ComPtr<ID3D11Buffer> overlay_vb;
    std::size_t overlay_capacity = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_vb;
    std::size_t shadow_capacity = 0;
};

} // namespace hikari::render
