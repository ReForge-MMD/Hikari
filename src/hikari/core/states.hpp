#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include <d3d11.h>
#include <wrl/client.h>

namespace hikari::core {

enum class BlendPreset : std::uint8_t { Opaque, AlphaBlend, Additive, Subtractive, Multiply };
enum class DepthPreset : std::uint8_t {
    ReadWriteLessEqual,
    ReadOnlyLessEqual,
    Disabled,
    ShadowComparison,
};
enum class RasterizerPreset : std::uint8_t {
    CullNoneSolid,
    CullBackSolid,
    CullFrontSolid,
    Wireframe,
};
enum class SamplerPreset : std::uint8_t {
    LinearWrap,
    LinearClamp,
    PointClamp,
    PointWrap,
    Anisotropic16Wrap,
    ComparisonLinearClamp,
};

class StateManager {
public:
    explicit StateManager(ID3D11Device* device);

    ID3D11BlendState* get_blend(BlendPreset preset) const;
    ID3D11DepthStencilState* get_depth(DepthPreset preset) const;
    ID3D11RasterizerState* get_rasterizer(RasterizerPreset preset) const;
    ID3D11SamplerState* get_sampler(SamplerPreset preset) const;

private:
    Microsoft::WRL::ComPtr<ID3D11BlendState> create_blend(ID3D11Device* device, BlendPreset preset) const;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> create_depth(ID3D11Device* device, DepthPreset preset) const;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> create_raster(ID3D11Device* device, RasterizerPreset preset) const;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> create_sampler(ID3D11Device* device, SamplerPreset preset) const;

    std::unordered_map<BlendPreset, Microsoft::WRL::ComPtr<ID3D11BlendState>> blends_;
    std::unordered_map<DepthPreset, Microsoft::WRL::ComPtr<ID3D11DepthStencilState>> depths_;
    std::unordered_map<RasterizerPreset, Microsoft::WRL::ComPtr<ID3D11RasterizerState>> rasters_;
    std::unordered_map<SamplerPreset, Microsoft::WRL::ComPtr<ID3D11SamplerState>> samplers_;
};

} // namespace hikari::core
