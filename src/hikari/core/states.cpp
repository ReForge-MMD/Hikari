#include "hikari/core/states.hpp"

#include <cfloat>
#include <stdexcept>

namespace hikari::core {
namespace {

template <typename T>
struct EnumHash {
    std::size_t operator()(T value) const noexcept {
        return static_cast<std::size_t>(value);
    }
};

} // namespace

StateManager::StateManager(ID3D11Device* device) {
    for (auto preset : {BlendPreset::Opaque, BlendPreset::AlphaBlend, BlendPreset::Additive,
                        BlendPreset::Subtractive, BlendPreset::Multiply}) {
        blends_.emplace(preset, create_blend(device, preset));
    }
    for (auto preset : {DepthPreset::ReadWriteLessEqual, DepthPreset::ReadOnlyLessEqual,
                        DepthPreset::Disabled, DepthPreset::ShadowComparison}) {
        depths_.emplace(preset, create_depth(device, preset));
    }
    for (auto preset : {RasterizerPreset::CullNoneSolid, RasterizerPreset::CullBackSolid,
                        RasterizerPreset::CullFrontSolid, RasterizerPreset::Wireframe}) {
        rasters_.emplace(preset, create_raster(device, preset));
    }
    for (auto preset : {SamplerPreset::LinearWrap, SamplerPreset::LinearClamp,
                        SamplerPreset::PointClamp, SamplerPreset::PointWrap,
                        SamplerPreset::Anisotropic16Wrap,
                        SamplerPreset::ComparisonLinearClamp}) {
        samplers_.emplace(preset, create_sampler(device, preset));
    }
}

Microsoft::WRL::ComPtr<ID3D11BlendState> StateManager::create_blend(ID3D11Device* device,
                                                                     BlendPreset preset) const {
    D3D11_BLEND_DESC desc{};
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    switch (preset) {
    case BlendPreset::Opaque:
        desc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendPreset::AlphaBlend:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
    case BlendPreset::Additive:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
    case BlendPreset::Subtractive:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
    case BlendPreset::Multiply:
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
    }
    Microsoft::WRL::ComPtr<ID3D11BlendState> state;
    if (FAILED(device->CreateBlendState(&desc, state.GetAddressOf()))) {
        throw std::runtime_error("CreateBlendState failed");
    }
    return state;
}

Microsoft::WRL::ComPtr<ID3D11DepthStencilState> StateManager::create_depth(
    ID3D11Device* device, DepthPreset preset) const {
    D3D11_DEPTH_STENCIL_DESC desc{};
    switch (preset) {
    case DepthPreset::ReadWriteLessEqual:
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        break;
    case DepthPreset::ReadOnlyLessEqual:
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        break;
    case DepthPreset::Disabled:
        desc.DepthEnable = FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        break;
    case DepthPreset::ShadowComparison:
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_LESS;
        break;
    }
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
    if (FAILED(device->CreateDepthStencilState(&desc, state.GetAddressOf()))) {
        throw std::runtime_error("CreateDepthStencilState failed");
    }
    return state;
}

Microsoft::WRL::ComPtr<ID3D11RasterizerState> StateManager::create_raster(
    ID3D11Device* device, RasterizerPreset preset) const {
    D3D11_RASTERIZER_DESC desc{};
    desc.DepthClipEnable = TRUE;
    switch (preset) {
    case RasterizerPreset::CullNoneSolid:
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        break;
    case RasterizerPreset::CullBackSolid:
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_BACK;
        break;
    case RasterizerPreset::CullFrontSolid:
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_FRONT;
        break;
    case RasterizerPreset::Wireframe:
        desc.FillMode = D3D11_FILL_WIREFRAME;
        desc.CullMode = D3D11_CULL_NONE;
        break;
    }
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;
    if (FAILED(device->CreateRasterizerState(&desc, state.GetAddressOf()))) {
        throw std::runtime_error("CreateRasterizerState failed");
    }
    return state;
}

Microsoft::WRL::ComPtr<ID3D11SamplerState> StateManager::create_sampler(
    ID3D11Device* device, SamplerPreset preset) const {
    D3D11_SAMPLER_DESC desc{};
    desc.MaxLOD = FLT_MAX;
    switch (preset) {
    case SamplerPreset::LinearWrap:
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        break;
    case SamplerPreset::LinearClamp:
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        break;
    case SamplerPreset::PointClamp:
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        break;
    case SamplerPreset::PointWrap:
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        break;
    case SamplerPreset::Anisotropic16Wrap:
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
        desc.MaxAnisotropy = 16;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        break;
    case SamplerPreset::ComparisonLinearClamp:
        desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        break;
    }
    Microsoft::WRL::ComPtr<ID3D11SamplerState> state;
    if (FAILED(device->CreateSamplerState(&desc, state.GetAddressOf()))) {
        throw std::runtime_error("CreateSamplerState failed");
    }
    return state;
}

ID3D11BlendState* StateManager::get_blend(BlendPreset preset) const {
    const auto it = blends_.find(preset);
    return it == blends_.end() ? nullptr : it->second.Get();
}
ID3D11DepthStencilState* StateManager::get_depth(DepthPreset preset) const {
    const auto it = depths_.find(preset);
    return it == depths_.end() ? nullptr : it->second.Get();
}
ID3D11RasterizerState* StateManager::get_rasterizer(RasterizerPreset preset) const {
    const auto it = rasters_.find(preset);
    return it == rasters_.end() ? nullptr : it->second.Get();
}
ID3D11SamplerState* StateManager::get_sampler(SamplerPreset preset) const {
    const auto it = samplers_.find(preset);
    return it == samplers_.end() ? nullptr : it->second.Get();
}

} // namespace hikari::core
