#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace hikari::core {

struct OffscreenRenderTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct DepthStencilTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct StructuredGpuBuffer {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    std::uint32_t element_size = 0;
    std::uint32_t element_count = 0;
};

class RenderTargetManager {
public:
    void create_target(ID3D11Device* device, const std::string& name, std::uint32_t width,
                       std::uint32_t height, DXGI_FORMAT format, bool enable_uav);
    void create_depth(ID3D11Device* device, const std::string& name, std::uint32_t width,
                      std::uint32_t height);
    void create_structured_buffer(ID3D11Device* device, const std::string& name,
                                  std::uint32_t element_size, std::uint32_t element_count,
                                  bool enable_uav);

    const OffscreenRenderTarget* get_target(const std::string& name) const;
    ID3D11RenderTargetView* get_rtv(const std::string& name) const;
    const DepthStencilTarget* get_depth(const std::string& name) const;
    ID3D11DepthStencilView* get_dsv(const std::string& name) const;
    const StructuredGpuBuffer* get_buffer(const std::string& name) const;

    void bind_mrt(ID3D11DeviceContext* context,
                  const std::vector<ID3D11RenderTargetView*>& rtvs,
                  ID3D11DepthStencilView* dsv);
    void clear_render_target(ID3D11DeviceContext* context, std::size_t slot,
                             const float color[4]) const;
    void clear_depth_stencil(ID3D11DeviceContext* context, bool clear_depth, float depth,
                             bool clear_stencil, std::uint8_t stencil) const;

private:
    std::unordered_map<std::string, OffscreenRenderTarget> targets_;
    std::unordered_map<std::string, DepthStencilTarget> depths_;
    std::unordered_map<std::string, StructuredGpuBuffer> buffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 8> active_rtvs_{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> active_dsv_;
};

} // namespace hikari::core
