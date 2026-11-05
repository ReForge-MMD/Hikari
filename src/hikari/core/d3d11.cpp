#include "hikari/core/d3d11.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <d3dcompiler.h>

#include "hikari/core/log.hpp"
#include "hikari/core/shader.hpp"

namespace hikari::core {
namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08lX)", what,
                      static_cast<unsigned long>(hr));
        throw std::runtime_error(buffer);
    }
}

std::uint64_t hash_bytes(const std::vector<std::uint8_t>& bytes, std::uint32_t stride) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    hash ^= stride;
    hash *= 1099511628211ull;
    return hash;
}

Microsoft::WRL::ComPtr<ID3D11RasterizerState> make_raster(ID3D11Device* device, D3D11_CULL_MODE cull,
                                                           D3D11_FILL_MODE fill) {
    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = fill;
    desc.CullMode = cull;
    desc.DepthClipEnable = TRUE;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;
    check(device->CreateRasterizerState(&desc, state.GetAddressOf()), "CreateRasterizerState");
    return state;
}

Microsoft::WRL::ComPtr<ID3D11DepthStencilState> make_depth(ID3D11Device* device, bool enable,
                                                            bool write) {
    D3D11_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = enable ? TRUE : FALSE;
    desc.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
    check(device->CreateDepthStencilState(&desc, state.GetAddressOf()), "CreateDepthStencilState");
    return state;
}

Microsoft::WRL::ComPtr<ID3D11BlendState> make_blend(ID3D11Device* device, bool alpha) {
    D3D11_BLEND_DESC desc{};
    auto& rt = desc.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (alpha) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }
    Microsoft::WRL::ComPtr<ID3D11BlendState> state;
    check(device->CreateBlendState(&desc, state.GetAddressOf()), "CreateBlendState");
    return state;
}

Microsoft::WRL::ComPtr<ID3D11SamplerState> make_sampler(ID3D11Device* device, D3D11_FILTER filter,
                                                         D3D11_TEXTURE_ADDRESS_MODE address) {
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = address;
    desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    desc.MaxLOD = FLT_MAX;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> state;
    check(device->CreateSamplerState(&desc, state.GetAddressOf()), "CreateSamplerState");
    return state;
}

const char* kBlitHLSL = R"(
struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VOut BlitVS(uint id : SV_VertexID) {
    VOut o;
    float x = (id == 1) ? 3.0f : -1.0f;
    float y = (id == 2) ? 3.0f : -1.0f;
    o.pos = float4(x, y, 0.5f, 1.0f);
    o.uv = float2((x + 1.0f) * 0.5f, 1.0f - (y + 1.0f) * 0.5f);
    return o;
}
Texture2D SrcTexture : register(t0);
SamplerState SrcSampler : register(s0);
float4 BlitPS(VOut input) : SV_Target {
    return SrcTexture.Sample(SrcSampler, input.uv);
}
)";

} // namespace

DeferredContextPool::DeferredContextPool(ID3D11Device* device) : device_(device) {}

Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeferredContextPool::acquire() {
    std::lock_guard lock(mutex_);
    if (!pool_.empty()) {
        auto context = pool_.back();
        pool_.pop_back();
        return context;
    }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    check(device_->CreateDeferredContext(0, context.GetAddressOf()), "CreateDeferredContext");
    return context;
}

void DeferredContextPool::recycle(Microsoft::WRL::ComPtr<ID3D11DeviceContext> context) {
    if (!context) {
        return;
    }
    std::lock_guard lock(mutex_);
    pool_.push_back(std::move(context));
}

void DeferredContextPool::clear() {
    std::lock_guard lock(mutex_);
    pool_.clear();
}

D3D11Context::D3D11Context(HWND hwnd_, std::uint32_t width_, std::uint32_t height_)
    : width(std::max(width_, 1u)), height(std::max(height_, 1u)),
      render_width(std::max(width_, 1u)), render_height(std::max(height_, 1u)), hwnd(hwnd_) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const D3D_FEATURE_LEVEL fallback[] = {D3D_FEATURE_LEVEL_11_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                   D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
                                   context.GetAddressOf());
    if (FAILED(hr)) {
        device.Reset();
        context.Reset();
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fallback, 1,
                               D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
                               context.GetAddressOf());
    }
    if (FAILED(hr)) {
        device.Reset();
        context.Reset();
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, fallback, 1,
                               D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
                               context.GetAddressOf());
    }
    check(hr, "D3D11CreateDevice");
    if (!device || !context) {
        throw std::runtime_error("D3D11CreateDevice returned no device or context");
    }

    deferred_pool = std::make_unique<DeferredContextPool>(device.Get());
    raster_solid_cull_none = make_raster(device.Get(), D3D11_CULL_NONE, D3D11_FILL_SOLID);
    raster_solid_cull_back = make_raster(device.Get(), D3D11_CULL_BACK, D3D11_FILL_SOLID);
    raster_wireframe = make_raster(device.Get(), D3D11_CULL_NONE, D3D11_FILL_WIREFRAME);
    depth_enabled = make_depth(device.Get(), true, true);
    depth_disabled = make_depth(device.Get(), false, false);
    blend_opaque = make_blend(device.Get(), false);
    blend_alpha = make_blend(device.Get(), true);
    sampler_linear_wrap =
        make_sampler(device.Get(), D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    sampler_linear_clamp =
        make_sampler(device.Get(), D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    sampler_point_clamp =
        make_sampler(device.Get(), D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    state_manager = std::make_unique<StateManager>(device.Get());

    if (hwnd != nullptr && width > 0 && height > 0) {
        init_swap_chain(hwnd, width, height);
    }
}

std::string D3D11Context::vs_profile() const {
    switch (feature_level) {
    case D3D_FEATURE_LEVEL_10_1:
        return "vs_4_1";
    case D3D_FEATURE_LEVEL_10_0:
        return "vs_4_0";
    default:
        return "vs_5_0";
    }
}

std::string D3D11Context::ps_profile() const {
    switch (feature_level) {
    case D3D_FEATURE_LEVEL_10_1:
        return "ps_4_1";
    case D3D_FEATURE_LEVEL_10_0:
        return "ps_4_0";
    default:
        return "ps_5_0";
    }
}

void D3D11Context::dispatch_compute(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
    if (x == 0 || y == 0 || z == 0) {
        return;
    }
    context->Dispatch(x, y, z);
}

void D3D11Context::dispatch_compute_indirect(ID3D11Buffer* buffer, std::uint32_t offset) const {
    context->DispatchIndirect(buffer, offset);
}

void D3D11Context::cs_set_uavs(std::uint32_t start_slot,
                               const std::vector<ID3D11UnorderedAccessView*>& uavs) const {
    context->CSSetUnorderedAccessViews(start_slot, static_cast<UINT>(uavs.size()),
                                       uavs.empty() ? nullptr : uavs.data(), nullptr);
}

D3D11ThreadingCaps D3D11Context::check_threading_support() const {
    D3D11_FEATURE_DATA_THREADING data{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &data, sizeof(data)))) {
        return {data.DriverConcurrentCreates != FALSE, data.DriverCommandLists != FALSE};
    }
    return {};
}

Microsoft::WRL::ComPtr<ID3D11DeviceContext> D3D11Context::create_deferred_context() const {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred;
    check(device->CreateDeferredContext(0, deferred.GetAddressOf()), "CreateDeferredContext");
    return deferred;
}

Microsoft::WRL::ComPtr<ID3D11DeviceContext> D3D11Context::acquire_deferred_context() {
    return deferred_pool->acquire();
}

void D3D11Context::recycle_deferred_context(Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx) {
    deferred_pool->recycle(std::move(ctx));
}

Microsoft::WRL::ComPtr<ID3D11CommandList> D3D11Context::finish_command_list(
    ID3D11DeviceContext* deferred, bool restore_state) {
    Microsoft::WRL::ComPtr<ID3D11CommandList> list;
    check(deferred->FinishCommandList(restore_state ? TRUE : FALSE, list.GetAddressOf()),
          "FinishCommandList");
    return list;
}

void D3D11Context::execute_command_list(ID3D11CommandList* command_list, bool restore_state) const {
    context->ExecuteCommandList(command_list, restore_state ? TRUE : FALSE);
}

void D3D11Context::execute_command_lists(const std::vector<ID3D11CommandList*>& lists,
                                         bool restore_state) const {
    for (auto* list : lists) {
        context->ExecuteCommandList(list, restore_state ? TRUE : FALSE);
    }
}

void D3D11Context::init_swap_chain(HWND window, std::uint32_t new_width, std::uint32_t new_height) {
    hwnd = window;
    width = std::max(new_width, 1u);
    height = std::max(new_height, 1u);

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    check(device.As(&dxgi_device), "QueryInterface IDXGIDevice");
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    check(dxgi_device->GetAdapter(adapter.GetAddressOf()), "GetAdapter");
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    check(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), "GetParent IDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    // BitBlt exchange is required for layered-window colour key composition.
    desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
    fullscreen.Windowed = TRUE;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain1;
    check(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &desc, &fullscreen, nullptr,
                                          swap_chain1.GetAddressOf()),
          "CreateSwapChainForHwnd");
    check(swap_chain1.As(&swap_chain), "QueryInterface IDXGISwapChain");
    recreate_views();
}

void D3D11Context::recreate_views() {
    render_target_view.Reset();
    depth_stencil_view.Reset();
    depth_stencil_texture.Reset();
    backbuffer_rtv_.Reset();
    backbuffer_dsv_.Reset();
    context->OMSetRenderTargets(0, nullptr, nullptr);
    if (!swap_chain) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    check(swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())), "GetBuffer");
    check(device->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                         backbuffer_rtv_.GetAddressOf()),
          "CreateRenderTargetView");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check(device->CreateTexture2D(&desc, nullptr, depth_stencil_texture.GetAddressOf()),
          "CreateTexture2D depth");

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device->CreateDepthStencilView(depth_stencil_texture.Get(), &dsv_desc,
                                         backbuffer_dsv_.GetAddressOf()),
          "CreateDepthStencilView");
    if (!scene_active_) {
        render_target_view = backbuffer_rtv_;
        depth_stencil_view = backbuffer_dsv_;
        render_width = width;
        render_height = height;
    }
    bind_default_targets();
}

void D3D11Context::bind_render_targets_to(ID3D11DeviceContext* target) const {
    if (render_target_view) {
        ID3D11RenderTargetView* views[] = {render_target_view.Get()};
        target->OMSetRenderTargets(1, views, depth_stencil_view.Get());
    }
    if (state_manager) {
        target->RSSetState(state_manager->get_rasterizer(RasterizerPreset::CullNoneSolid));
        target->RSSetScissorRects(0, nullptr);
    }
}

void D3D11Context::bind_render_targets() const {
    bind_render_targets_to(context.Get());
}

void D3D11Context::bind_default_targets_to(ID3D11DeviceContext* target) const {
    bind_render_targets_to(target);
    set_viewport_on(target, static_cast<float>(render_width), static_cast<float>(render_height));
}

void D3D11Context::bind_default_targets() const {
    bind_default_targets_to(context.Get());
}

void D3D11Context::set_viewport_rect_on(ID3D11DeviceContext* target, float x, float y, float w,
                                        float h) const {
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = x;
    viewport.TopLeftY = y;
    viewport.Width = w;
    viewport.Height = h;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    target->RSSetViewports(1, &viewport);
}

void D3D11Context::set_viewport_rect(float x, float y, float w, float h) const {
    set_viewport_rect_on(context.Get(), x, y, w, h);
}

void D3D11Context::set_viewport_on(ID3D11DeviceContext* target, float w, float h) const {
    set_viewport_rect_on(target, 0.0f, 0.0f, w, h);
}

void D3D11Context::set_viewport(float w, float h) const {
    set_viewport_on(context.Get(), w, h);
}

void D3D11Context::clear(const float color[4], float depth, std::uint8_t stencil) const {
    if (render_target_view) {
        context->ClearRenderTargetView(render_target_view.Get(), color);
    }
    if (depth_stencil_view) {
        context->ClearDepthStencilView(depth_stencil_view.Get(),
                                       D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, stencil);
    }
}

void D3D11Context::present(std::uint32_t sync_interval) const {
    if (swap_chain) {
        check(swap_chain->Present(sync_interval, 0), "Present");
    }
}

void D3D11Context::ensure_blit_shaders() {
    if (blit_vs && blit_ps) {
        return;
    }
    const auto vs_bc = compile_hlsl(kBlitHLSL, "BlitVS", vs_profile());
    const auto ps_bc = compile_hlsl(kBlitHLSL, "BlitPS", ps_profile());
    check(device->CreateVertexShader(vs_bc.data(), vs_bc.size(), nullptr,
                                     blit_vs.GetAddressOf()),
          "CreateVertexShader blit");
    check(device->CreatePixelShader(ps_bc.data(), ps_bc.size(), nullptr,
                                    blit_ps.GetAddressOf()),
          "CreatePixelShader blit");
}

void D3D11Context::create_scene_targets(std::uint32_t scene_width, std::uint32_t scene_height) {
    scene_tex_.Reset();
    scene_rtv_.Reset();
    scene_srv_.Reset();
    scene_depth_tex_.Reset();
    scene_dsv_.Reset();
    scene_staging_.Reset();
    scene_size_ = {scene_width, scene_height};
    scene_staging_size_ = {0, 0};

    D3D11_TEXTURE2D_DESC color_desc{};
    color_desc.Width = scene_width;
    color_desc.Height = scene_height;
    color_desc.MipLevels = 1;
    color_desc.ArraySize = 1;
    color_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    color_desc.SampleDesc.Count = 1;
    color_desc.Usage = D3D11_USAGE_DEFAULT;
    color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    check(device->CreateTexture2D(&color_desc, nullptr, scene_tex_.GetAddressOf()),
          "CreateTexture2D scene");
    check(device->CreateRenderTargetView(scene_tex_.Get(), nullptr,
                                         scene_rtv_.GetAddressOf()),
          "CreateRenderTargetView scene");
    check(device->CreateShaderResourceView(scene_tex_.Get(), nullptr,
                                           scene_srv_.GetAddressOf()),
          "CreateShaderResourceView scene");

    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width = scene_width;
    depth_desc.Height = scene_height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check(device->CreateTexture2D(&depth_desc, nullptr, scene_depth_tex_.GetAddressOf()),
          "CreateTexture2D scene depth");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device->CreateDepthStencilView(scene_depth_tex_.Get(), &dsv_desc,
                                         scene_dsv_.GetAddressOf()),
          "CreateDepthStencilView scene");
    ensure_blit_shaders();
}

void D3D11Context::set_scene_resolution(std::uint32_t scene_width, std::uint32_t scene_height) {
    if (scene_width == 0 || scene_height == 0) {
        scene_active_ = false;
        scene_tex_.Reset();
        scene_rtv_.Reset();
        scene_srv_.Reset();
        scene_depth_tex_.Reset();
        scene_dsv_.Reset();
        scene_staging_.Reset();
        scene_size_ = {0, 0};
        scene_staging_size_ = {0, 0};
        render_target_view = backbuffer_rtv_;
        depth_stencil_view = backbuffer_dsv_;
        render_width = width;
        render_height = height;
        return;
    }
    scene_width = std::max(scene_width, 16u);
    scene_height = std::max(scene_height, 16u);
    if (scene_rtv_ && scene_size_ == std::make_pair(scene_width, scene_height)) {
        return;
    }
    create_scene_targets(scene_width, scene_height);
    if (scene_active_) {
        render_target_view = scene_rtv_;
        depth_stencil_view = scene_dsv_;
        render_width = scene_width;
        render_height = scene_height;
    }
}

void D3D11Context::begin_scene() {
    if (!scene_rtv_) {
        render_width = width;
        render_height = height;
        scene_active_ = false;
        render_target_view = backbuffer_rtv_;
        depth_stencil_view = backbuffer_dsv_;
        return;
    }
    scene_active_ = true;
    render_target_view = scene_rtv_;
    depth_stencil_view = scene_dsv_;
    render_width = scene_size_.first;
    render_height = scene_size_.second;
    bind_default_targets();
}

void D3D11Context::end_scene() {
    scene_active_ = false;
    render_target_view = backbuffer_rtv_;
    depth_stencil_view = backbuffer_dsv_;
    render_width = width;
    render_height = height;
}

void D3D11Context::clear_backbuffer(const float color[4]) const {
    if (backbuffer_rtv_) {
        context->ClearRenderTargetView(backbuffer_rtv_.Get(), color);
    }
}

void D3D11Context::blit_scene_to_backbuffer(float x, float y, float w, float h) {
    if (!scene_srv_ || !backbuffer_rtv_ || w <= 0.0f || h <= 0.0f) {
        return;
    }
    ensure_blit_shaders();
    ID3D11RenderTargetView* views[] = {backbuffer_rtv_.Get()};
    context->OMSetRenderTargets(1, views, nullptr);
    set_viewport_rect(x, y, w, h);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(state_manager->get_blend(BlendPreset::Opaque), blend_factor,
                             0xFFFFFFFF);
    context->OMSetDepthStencilState(depth_disabled.Get(), 0xFFFFFFFF);
    context->RSSetState(state_manager->get_rasterizer(RasterizerPreset::CullNoneSolid));
    auto* sampler = state_manager->get_sampler(SamplerPreset::LinearClamp);
    ID3D11ShaderResourceView* source = scene_srv_.Get();
    context->PSSetShaderResources(0, 1, &source);
    context->PSSetSamplers(0, 1, &sampler);
    context->VSSetShader(blit_vs.Get(), nullptr, 0);
    context->PSSetShader(blit_ps.Get(), nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* null_source[1] = {nullptr};
    context->PSSetShaderResources(0, 1, null_source);
    context->PSSetShader(nullptr, nullptr, 0);
    context->VSSetShader(nullptr, nullptr, 0);
}

void D3D11Context::recreate_composite(std::uint32_t new_width, std::uint32_t new_height) {
    if (new_width == 0 || new_height == 0) {
        return;
    }
    if (composite_size == std::make_pair(new_width, new_height) && composite_rtv) {
        return;
    }
    composite_size = {new_width, new_height};
    width = new_width;
    height = new_height;
    render_target_view.Reset();
    depth_stencil_view.Reset();
    context->OMSetRenderTargets(0, nullptr, nullptr);

    if (!blit_vs || !blit_ps) {
        ensure_blit_shaders();
    }

    D3D11_TEXTURE2D_DESC target_desc{};
    target_desc.Width = new_width;
    target_desc.Height = new_height;
    target_desc.MipLevels = 1;
    target_desc.ArraySize = 1;
    target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Usage = D3D11_USAGE_DEFAULT;
    target_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    check(device->CreateTexture2D(&target_desc, nullptr, model_tex.ReleaseAndGetAddressOf()),
          "CreateTexture2D model");
    check(device->CreateRenderTargetView(model_tex.Get(), nullptr,
                                         model_rtv.ReleaseAndGetAddressOf()),
          "CreateRenderTargetView model");
    check(device->CreateShaderResourceView(model_tex.Get(), nullptr,
                                           model_srv.ReleaseAndGetAddressOf()),
          "CreateShaderResourceView model");

    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width = new_width;
    depth_desc.Height = new_height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_tex;
    check(device->CreateTexture2D(&depth_desc, nullptr, depth_tex.GetAddressOf()),
          "CreateTexture2D composite depth");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device->CreateDepthStencilView(depth_tex.Get(), &dsv_desc,
                                         model_depth.ReleaseAndGetAddressOf()),
          "CreateDepthStencilView composite");

    check(device->CreateTexture2D(&target_desc, nullptr, composite_tex.ReleaseAndGetAddressOf()),
          "CreateTexture2D composite");
    check(device->CreateRenderTargetView(composite_tex.Get(), nullptr,
                                         composite_rtv.ReleaseAndGetAddressOf()),
          "CreateRenderTargetView composite");

    D3D11_TEXTURE2D_DESC staging_desc = target_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&staging_desc, nullptr,
                                  composite_staging.ReleaseAndGetAddressOf()),
          "CreateTexture2D composite staging");

    render_target_view = model_rtv;
    depth_stencil_view = model_depth;
    bind_default_targets();
    log_msg("Composite targets recreated: " + std::to_string(new_width) + "x" +
            std::to_string(new_height));
}

void D3D11Context::render_composite(ID3D11ShaderResourceView* scene_srv) {
    if (!model_srv || !composite_rtv || !blit_vs || !blit_ps) {
        return;
    }
    auto* sampler = state_manager->get_sampler(SamplerPreset::LinearClamp);
    auto* alpha_blend = state_manager->get_blend(BlendPreset::AlphaBlend);
    auto* opaque_blend = state_manager->get_blend(BlendPreset::Opaque);

    ID3D11RenderTargetView* views[] = {composite_rtv.Get()};
    context->OMSetRenderTargets(1, views, nullptr);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(opaque_blend, blend_factor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(depth_disabled.Get(), 0xFFFFFFFF);
    set_viewport(static_cast<float>(width), static_cast<float>(height));

    context->VSSetShader(blit_vs.Get(), nullptr, 0);
    context->PSSetShader(blit_ps.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, &sampler);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (scene_srv != nullptr) {
        ID3D11ShaderResourceView* scene = scene_srv;
        context->PSSetShaderResources(0, 1, &scene);
        context->Draw(3, 0);
    }

    context->OMSetBlendState(alpha_blend, blend_factor, 0xFFFFFFFF);
    ID3D11ShaderResourceView* model = model_srv.Get();
    context->PSSetShaderResources(0, 1, &model);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* null_srvs[1] = {nullptr};
    context->PSSetShaderResources(0, 1, null_srvs);
    context->PSSetShader(nullptr, nullptr, 0);
    context->VSSetShader(nullptr, nullptr, 0);
}

std::vector<std::uint8_t> D3D11Context::readback_model_layer() {
    if (!model_tex || !composite_staging) {
        throw std::runtime_error("composite targets are not initialized");
    }
    context->CopyResource(composite_staging.Get(), model_tex.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(composite_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map readback");
    const auto w = static_cast<std::size_t>(composite_size.first);
    const auto h = static_cast<std::size_t>(composite_size.second);
    std::vector<std::uint8_t> out(w * h * 4);
    for (std::size_t y = 0; y < h; ++y) {
        std::memcpy(out.data() + y * w * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, w * 4);
    }
    context->Unmap(composite_staging.Get(), 0);
    return out;
}

void D3D11Context::resize(std::uint32_t new_width, std::uint32_t new_height) {
    if (new_width == 0 || new_height == 0 || (new_width == width && new_height == height)) {
        return;
    }
    width = new_width;
    height = new_height;
    if (!scene_active_) {
        render_width = new_width;
        render_height = new_height;
    }
    context->OMSetRenderTargets(0, nullptr, nullptr);
    render_target_view.Reset();
    depth_stencil_view.Reset();
    depth_stencil_texture.Reset();
    backbuffer_rtv_.Reset();
    backbuffer_dsv_.Reset();
    if (swap_chain) {
        check(swap_chain->ResizeBuffers(0, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0),
              "ResizeBuffers");
    }
    recreate_views();
}

Microsoft::WRL::ComPtr<ID3D11Buffer> D3D11Context::create_buffer(const D3D11_BUFFER_DESC& desc,
                                                                 const void* initial_data,
                                                                 std::size_t initial_size) const {
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = initial_data;
    data.SysMemPitch = static_cast<UINT>(initial_size);
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&desc, initial_data != nullptr ? &data : nullptr,
                               buffer.GetAddressOf()),
          "CreateBuffer");
    return buffer;
}

Microsoft::WRL::ComPtr<ID3D11Buffer> D3D11Context::create_constant_buffer(
    std::size_t size_bytes) const {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(((size_bytes + 15) / 16) * 16);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return create_buffer(desc);
}

D3D11Context::StructuredBuffer D3D11Context::create_structured_buffer(
    std::uint32_t element_size, std::uint32_t element_count, bool uav) const {
    StructuredBuffer result;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = element_size * element_count;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = element_size;
    result.buffer = create_buffer(desc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    srv_desc.BufferEx.NumElements = element_count;
    check(device->CreateShaderResourceView(result.buffer.Get(), &srv_desc,
                                           result.srv.GetAddressOf()),
          "CreateShaderResourceView structured");

    if (uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.NumElements = element_count;
        check(device->CreateUnorderedAccessView(result.buffer.Get(), &uav_desc,
                                                result.uav.GetAddressOf()),
              "CreateUnorderedAccessView structured");
    }
    return result;
}

D3D11Context::Texture2DResult D3D11Context::create_texture2d(std::uint32_t tex_width,
                                                             std::uint32_t tex_height,
                                                             DXGI_FORMAT format, bool uav) const {
    Texture2DResult result;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = tex_width;
    desc.Height = tex_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    check(device->CreateTexture2D(&desc, nullptr, result.texture.GetAddressOf()),
          "CreateTexture2D");
    check(device->CreateShaderResourceView(result.texture.Get(), nullptr,
                                           result.srv.GetAddressOf()),
          "CreateShaderResourceView texture");
    if (uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = format;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Texture2D.MipSlice = 0;
        check(device->CreateUnorderedAccessView(result.texture.Get(), &uav_desc,
                                                result.uav.GetAddressOf()),
              "CreateUnorderedAccessView texture");
    }
    return result;
}

Microsoft::WRL::ComPtr<ID3D11Buffer> D3D11Context::upload_dynamic_vertex_buffer(const void* data,
                                                                                std::size_t size) {
    if (!dynamic_vb || dynamic_vb_size < size) {
        std::size_t new_size = 65536;
        while (new_size < size) {
            new_size *= 2;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(new_size);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dynamic_vb = create_buffer(desc);
        dynamic_vb_size = new_size;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(dynamic_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map vertex buffer");
    std::memcpy(mapped.pData, data, size);
    context->Unmap(dynamic_vb.Get(), 0);
    return dynamic_vb;
}

Microsoft::WRL::ComPtr<ID3D11Buffer> D3D11Context::upload_dynamic_index_buffer(const void* data,
                                                                               std::size_t size) {
    if (!dynamic_ib || dynamic_ib_size < size) {
        std::size_t new_size = 32768;
        while (new_size < size) {
            new_size *= 2;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(new_size);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dynamic_ib = create_buffer(desc);
        dynamic_ib_size = new_size;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(dynamic_ib.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map index buffer");
    std::memcpy(mapped.pData, data, size);
    context->Unmap(dynamic_ib.Get(), 0);
    return dynamic_ib;
}

Microsoft::WRL::ComPtr<ID3D11InputLayout> D3D11Context::get_or_create_input_layout(
    const std::vector<std::uint8_t>& vs_bytecode, std::uint32_t stride) {
    const std::uint64_t key = hash_bytes(vs_bytecode, stride);
    const auto existing = input_layout_cache.find(key);
    if (existing != input_layout_cache.end()) {
        return existing->second;
    }

    std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
    if (stride >= 52) {
        descs = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
    } else if (stride >= 38) {
        descs = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R16_UINT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
    } else if (stride >= 32) {
        descs = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
    } else {
        descs = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
    }

    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
    check(device->CreateInputLayout(descs.data(), static_cast<UINT>(descs.size()),
                                    vs_bytecode.data(), vs_bytecode.size(),
                                    layout.GetAddressOf()),
          "CreateInputLayout");
    input_layout_cache[key] = layout;
    return layout;
}

D3D11Context::CaptureResult D3D11Context::capture_backbuffer() {
    if (!swap_chain) {
        throw std::runtime_error("no swap chain");
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    check(swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())), "GetBuffer");

    if (!capture_staging_ || capture_size_ != std::make_pair(width, height)) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc, nullptr, capture_staging_.ReleaseAndGetAddressOf()),
              "CreateTexture2D capture");
        capture_size_ = {width, height};
    }

    context->CopyResource(capture_staging_.Get(), back_buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(capture_staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map capture");
    CaptureResult result;
    result.width = width;
    result.height = height;
    result.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* src = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        auto* dst = result.rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    context->Unmap(capture_staging_.Get(), 0);
    return result;
}

D3D11Context::CaptureResult D3D11Context::capture_scene() {
    if (!scene_tex_) {
        return capture_backbuffer();
    }
    if (!scene_staging_ || scene_staging_size_ != scene_size_) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = scene_size_.first;
        desc.Height = scene_size_.second;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc, nullptr,
                                      scene_staging_.ReleaseAndGetAddressOf()),
              "CreateTexture2D scene capture");
        scene_staging_size_ = scene_size_;
    }
    context->CopyResource(scene_staging_.Get(), scene_tex_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(scene_staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped),
          "Map scene capture");
    CaptureResult result;
    result.width = scene_size_.first;
    result.height = scene_size_.second;
    result.rgba.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    for (std::uint32_t y = 0; y < result.height; ++y) {
        const auto* src = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        auto* dst = result.rgba.data() + static_cast<std::size_t>(y) * result.width * 4;
        for (std::uint32_t x = 0; x < result.width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    context->Unmap(scene_staging_.Get(), 0);
    return result;
}

void save_bmp(const std::wstring& path, std::uint32_t width, std::uint32_t height,
              const std::vector<std::uint8_t>& rgba) {
    BITMAPFILEHEADER file_header{};
    BITMAPINFOHEADER info_header{};
    const std::uint32_t row_size = width * 4;
    const std::uint32_t image_size = row_size * height;
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + image_size;
    info_header.biSize = sizeof(BITMAPINFOHEADER);
    info_header.biWidth = static_cast<LONG>(width);
    info_header.biHeight = -static_cast<LONG>(height);
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = image_size;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open BMP for writing");
    }
    file.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    file.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    std::vector<std::uint8_t> bgra(rgba.size());
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
    }
    file.write(reinterpret_cast<const char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
}

} // namespace hikari::core
