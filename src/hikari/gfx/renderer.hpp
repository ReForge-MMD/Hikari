#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/gfx/material.hpp"
#include "hikari/gfx/settings.hpp"
#include "hikari/gfx/shader_library.hpp"
#include "hikari/gfx/target_pool.hpp"
#include "hikari/math.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/skinning.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::gfx {

struct DrawItem {
    const render::GpuMesh* mesh = nullptr;
    const std::vector<render::GpuMaterial>* materials = nullptr;
    const std::vector<std::optional<render::GpuTexture>>* textures = nullptr;
    const render::GpuSkinningBuffer* skinning = nullptr;
    std::size_t subset = 0;
    float depth = 0.0f;
};

struct FrameView {
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Mat4 inverse_view;
    Vec3 camera_position{0.0f, 0.0f, -10.0f};
    Vec3 sun_direction{-0.5f, -1.0f, -0.5f};
    Vec3 sun_color{1.0f, 1.0f, 1.0f};
    Vec3 ambient_color{0.2f, 0.2f, 0.2f};
    Vec3 background_color{0.13f, 0.14f, 0.17f};
    float near_z = 0.1f;
    float far_z = 1000.0f;
    bool orthographic = false;
    bool draw_grid = true;
    bool wireframe = false;
    std::uint32_t width = 1;
    std::uint32_t height = 1;

    bool show_geometry = true;
    bool show_lighting = true;
    bool show_transparent = true;
    bool show_tonemap = true;
    bool show_grid = true;

    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    std::vector<DrawItem> shadow_casters;
};

// Must match FrameParams in shaders/gfx_core.hlsl.
struct FrameConstants {
    std::array<std::array<float, 4>, 4> view{};
    std::array<std::array<float, 4>, 4> projection{};
    std::array<std::array<float, 4>, 4> view_projection{};
    std::array<std::array<float, 4>, 4> inverse_view{};
    std::array<float, 4> camera_pos{};
    std::array<float, 4> sun_direction{};
    std::array<float, 4> sun_color{};
    std::array<float, 4> ambient_color{};
    std::array<float, 4> params0{};
    std::array<float, 4> params1{};
    std::array<float, 4> params2{};
    std::array<float, 4> background_color{};
};

Vec3 color_temperature_rgb(float kelvin);
Vec3 sun_color_from_settings(const GraphicsSettings& settings, const Vec3& fallback);
Vec3 ambient_color_from_settings(const GraphicsSettings& settings);

// Must match ShadowParams in shaders/gfx_core.hlsl.
struct ShadowConstants {
    std::array<std::array<std::array<float, 4>, 4>, 4> light_view_proj{};
    std::array<std::array<float, 4>, 4> sky_visibility_matrix{};
    std::array<float, 4> cascade_splits{};
    std::array<float, 4> cascade_texel_world{};
    std::array<float, 4> cascade_light_range{};
    std::array<float, 4> shadow_params0{};  // depth_bias, normal_bias_texels, pcss_enable, blur_radius
    std::array<float, 4> shadow_params1{};  // light_radius, contact_length, contact_thickness, ssgs_length
    std::array<float, 4> shadow_params2{};  // ssgs_thickness, sky_height_bias, cascade_count, sky_texel_uv
    std::array<float, 4> shadow_params3{};  // sky_visibility_mode, texel_uv_x, contact_steps, ssgs_steps
};

struct ShadowDrawConstants {
    std::array<std::array<float, 4>, 4> matrix{};
    std::array<float, 4> params{};
};

class Renderer {
public:
    explicit Renderer(core::D3D11Context& context);

    void render(const GraphicsSettings& settings, const FrameView& view,
                ID3D11RenderTargetView* final_rtv);
    void resize(std::uint32_t width, std::uint32_t height);
    void reload_shaders();

    const TargetPool& targets() const { return targets_; }

private:
    struct GridVertex {
        std::array<float, 3> position{};
        std::array<float, 4> color{};
    };

    void ensure_targets(std::uint32_t width, std::uint32_t height);
    void create_buffers();
    void build_grid();
    void update_frame_constants(const GraphicsSettings& settings, const FrameView& view,
                                bool linear_tonemap);
    void update_material_constants(const MaterialConstants& constants);
    void bind_frame_constants();
    void bind_material_constants();
    void draw_geometry(const FrameView& view);
    void draw_lighting(const GraphicsSettings& settings, const FrameView& view);
    void draw_transparent(const FrameView& view);
    void draw_tonemap(const GraphicsSettings& settings, const FrameView& view,
                      ID3D11RenderTargetView* final_rtv, bool linear);
    void draw_grid(const FrameView& view, ID3D11RenderTargetView* final_rtv);
    void draw_fullscreen(const ShaderProgram& program);
    void draw_item(const DrawItem& item, bool transparent);
    std::vector<ShaderDefine> lighting_defines(const GraphicsSettings& settings,
                                               bool linear_tonemap) const;

    // Shadow pass methods
    void draw_shadow_cascades(const GraphicsSettings& settings, const FrameView& view);
    void draw_sky_visibility(const GraphicsSettings& settings, const FrameView& view);
    void draw_shadow_mask(const GraphicsSettings& settings, const FrameView& view);
    void draw_shadow_blur(const GraphicsSettings& settings, const FrameView& view);
    void update_shadow_constants(const GraphicsSettings& settings, const FrameView& view);
    void bind_shadow_constants();
    std::vector<ShaderDefine> shadow_defines(const GraphicsSettings& settings) const;

    core::D3D11Context& context_;
    ShaderLibrary shaders_;
    TargetPool targets_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> grid_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_draw_buffer_;
    std::uint32_t grid_vertex_count_ = 0;
    render::GpuTexture white_texture_;
};

} // namespace hikari::gfx
