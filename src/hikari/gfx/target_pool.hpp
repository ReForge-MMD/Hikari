#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

namespace hikari::gfx {

struct Target {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    // Per-mip, per-face render target views for cubemap targets, indexed
    // mip * 6 + face, in the Direct3D face order +X, -X, +Y, -Y, +Z, -Z.
    // Empty for 2D targets.
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> mip_face_rtvs;
    // Per-mip render target views and single-level shader resource views for mipmapped 2D targets.
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> mip_rtvs;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> mip_srvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct DepthTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    std::vector<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> slice_dsvs; // per-slice DSVs for array textures
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct VolumeTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture3D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> mip_uavs;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> mip_srvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t mip_levels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

class TargetPool {
public:
    void create_target(ID3D11Device* device, const std::string& name, std::uint32_t width,
                       std::uint32_t height, DXGI_FORMAT format, bool uav = false);
    // 2D target with a full or custom mip chain, per-mip RTVs, per-mip single-level SRVs,
    // and a master SRV covering all mip levels.
    void create_target_mipmapped(ID3D11Device* device, const std::string& name, std::uint32_t width,
                                 std::uint32_t height, DXGI_FORMAT format, std::uint32_t mip_levels = 0);
    // Square cubemap with one render target view per mip and face and a
    // TextureCube shader resource view covering every mip.
    void create_cubemap(ID3D11Device* device, const std::string& name, std::uint32_t size,
                        DXGI_FORMAT format, std::uint32_t mip_levels = 1);
    void create_depth(ID3D11Device* device, const std::string& name, std::uint32_t width,
                      std::uint32_t height);
    void create_array_depth(ID3D11Device* device, const std::string& name, std::uint32_t width,
                            std::uint32_t height, std::uint32_t array_size);
    void create_volume(ID3D11Device* device, const std::string& name, std::uint32_t width,
                       std::uint32_t height, std::uint32_t depth, DXGI_FORMAT format,
                       std::uint32_t mip_levels = 1);

    const Target* get(const std::string& name) const;
    const DepthTarget* get_depth(const std::string& name) const;
    const VolumeTarget* get_volume(const std::string& name) const;
    bool has(const std::string& name) const;
    void clear();

private:
    std::unordered_map<std::string, Target> targets_;
    std::unordered_map<std::string, DepthTarget> depths_;
    std::unordered_map<std::string, VolumeTarget> volumes_;
};

} // namespace hikari::gfx
