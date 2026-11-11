#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "hikari/core/states.hpp"
#include "hikari/core/targets.hpp"

namespace hikari::core {

struct D3D11ThreadingCaps {
    bool driver_concurrent_creates = false;
    bool driver_command_lists = false;
};

class DeferredContextPool {
public:
    explicit DeferredContextPool(ID3D11Device* device);

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> acquire();
    void recycle(Microsoft::WRL::ComPtr<ID3D11DeviceContext> context);
    void clear();

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::mutex mutex_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11DeviceContext>> pool_;
};

class D3D11Context {
public:
    D3D11Context(HWND hwnd, std::uint32_t width, std::uint32_t height);

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_stencil_texture;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t render_width = 1;
    std::uint32_t render_height = 1;
    HWND hwnd = nullptr;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_solid_cull_none;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_solid_cull_back;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_wireframe;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_enabled;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_disabled;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_opaque;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_alpha;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_linear_wrap;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_linear_clamp;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_point_clamp;

    Microsoft::WRL::ComPtr<ID3D11Buffer> dynamic_vb;
    std::size_t dynamic_vb_size = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> dynamic_ib;
    std::size_t dynamic_ib_size = 0;

    std::uint64_t frame_counter = 0;
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3D11InputLayout>> input_layout_cache;

    std::unique_ptr<StateManager> state_manager;
    RenderTargetManager render_targets;
    std::unique_ptr<DeferredContextPool> deferred_pool;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> model_tex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> model_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> model_srv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> model_depth;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> composite_tex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> composite_rtv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> composite_staging;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> blit_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blit_ps;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> composite_sampler;
    Microsoft::WRL::ComPtr<ID3D11BlendState> composite_alpha_blend;
    std::pair<std::uint32_t, std::uint32_t> composite_size{0, 0};

    std::string vs_profile() const;
    std::string ps_profile() const;
    std::string cs_profile() const;

    void dispatch_compute(std::uint32_t x, std::uint32_t y, std::uint32_t z) const;
    void dispatch_compute_indirect(ID3D11Buffer* buffer, std::uint32_t offset) const;
    void cs_set_uavs(std::uint32_t start_slot,
                     const std::vector<ID3D11UnorderedAccessView*>& uavs) const;
    D3D11ThreadingCaps check_threading_support() const;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> create_deferred_context() const;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> acquire_deferred_context();
    void recycle_deferred_context(Microsoft::WRL::ComPtr<ID3D11DeviceContext> context);
    static Microsoft::WRL::ComPtr<ID3D11CommandList> finish_command_list(
        ID3D11DeviceContext* deferred, bool restore_state);
    void execute_command_list(ID3D11CommandList* command_list, bool restore_state) const;
    void execute_command_lists(const std::vector<ID3D11CommandList*>& lists, bool restore_state) const;

    void init_swap_chain(HWND hwnd, std::uint32_t width, std::uint32_t height);
    void recreate_views();
    void bind_render_targets_to(ID3D11DeviceContext* target) const;
    void bind_render_targets() const;
    void bind_default_targets_to(ID3D11DeviceContext* target) const;
    void bind_default_targets() const;
    void set_viewport_rect_on(ID3D11DeviceContext* target, float x, float y, float w, float h) const;
    void set_viewport_rect(float x, float y, float w, float h) const;
    void set_viewport_on(ID3D11DeviceContext* target, float w, float h) const;
    void set_viewport(float w, float h) const;
    void clear(const float color[4], float depth, std::uint8_t stencil) const;
    void present(std::uint32_t sync_interval) const;

    void set_scene_resolution(std::uint32_t scene_width, std::uint32_t scene_height);
    bool scene_target_ready() const { return scene_rtv_ != nullptr; }
    void begin_scene();
    void end_scene();
    void clear_backbuffer(const float color[4]) const;
    void blit_scene_to_backbuffer(float x, float y, float w, float h);

    void recreate_composite(std::uint32_t width, std::uint32_t height);
    void render_composite(ID3D11ShaderResourceView* scene_srv);
    std::vector<std::uint8_t> readback_model_layer();
    void resize(std::uint32_t width, std::uint32_t height);

    Microsoft::WRL::ComPtr<ID3D11Buffer> create_buffer(const D3D11_BUFFER_DESC& desc,
                                                       const void* initial_data = nullptr,
                                                       std::size_t initial_size = 0) const;
    Microsoft::WRL::ComPtr<ID3D11Buffer> create_constant_buffer(std::size_t size_bytes) const;
    struct StructuredBuffer {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    };
    StructuredBuffer create_structured_buffer(std::uint32_t element_size, std::uint32_t element_count,
                                              bool uav) const;
    struct Texture2DResult {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    };
    Texture2DResult create_texture2d(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
                                     bool uav) const;

    Microsoft::WRL::ComPtr<ID3D11Buffer> upload_dynamic_vertex_buffer(const void* data,
                                                                      std::size_t size);
    Microsoft::WRL::ComPtr<ID3D11Buffer> upload_dynamic_index_buffer(const void* data,
                                                                     std::size_t size);
    Microsoft::WRL::ComPtr<ID3D11InputLayout> get_or_create_input_layout(
        const std::vector<std::uint8_t>& vs_bytecode, std::uint32_t stride);

    struct CaptureResult {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba;
    };
    CaptureResult capture_backbuffer();
    CaptureResult capture_scene();

private:
    void ensure_blit_shaders();
    void create_scene_targets(std::uint32_t scene_width, std::uint32_t scene_height);

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backbuffer_rtv_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> backbuffer_dsv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_tex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> scene_rtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scene_srv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_depth_tex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> scene_dsv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_staging_;
    std::pair<std::uint32_t, std::uint32_t> scene_size_{0, 0};
    std::pair<std::uint32_t, std::uint32_t> scene_staging_size_{0, 0};
    bool scene_active_ = false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> capture_staging_;
    std::pair<std::uint32_t, std::uint32_t> capture_size_{0, 0};
};

void save_bmp(const std::wstring& path, std::uint32_t width, std::uint32_t height,
              const std::vector<std::uint8_t>& rgba);

} // namespace hikari::core
