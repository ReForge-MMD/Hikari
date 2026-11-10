#include "hikari/gfx/target_pool.hpp"

#include <stdexcept>

namespace hikari::gfx {

void TargetPool::create_target(ID3D11Device* device, const std::string& name,
                               std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
                               bool uav) {
    if (device == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("invalid render target description: " + name);
    }
    Target target;
    target.width = width;
    target.height = height;
    target.format = format;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
                     (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D failed: " + name);
    }
    if (FAILED(device->CreateRenderTargetView(target.texture.Get(), nullptr,
                                              target.rtv.GetAddressOf()))) {
        throw std::runtime_error("CreateRenderTargetView failed: " + name);
    }
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), nullptr,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed: " + name);
    }
    if (uav && FAILED(device->CreateUnorderedAccessView(target.texture.Get(), nullptr,
                                                        target.uav.GetAddressOf()))) {
        throw std::runtime_error("CreateUnorderedAccessView failed: " + name);
    }
    targets_[name] = std::move(target);
}

void TargetPool::create_depth(ID3D11Device* device, const std::string& name,
                              std::uint32_t width, std::uint32_t height) {
    if (device == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("invalid depth target description: " + name);
    }
    DepthTarget target;
    target.width = width;
    target.height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D depth failed: " + name);
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    if (FAILED(device->CreateDepthStencilView(target.texture.Get(), &dsv_desc,
                                              target.dsv.GetAddressOf()))) {
        throw std::runtime_error("CreateDepthStencilView failed: " + name);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &srv_desc,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView depth failed: " + name);
    }
    depths_[name] = std::move(target);
}

void TargetPool::create_array_depth(ID3D11Device* device, const std::string& name,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t array_size) {
    if (device == nullptr || width == 0 || height == 0 || array_size == 0) {
        throw std::runtime_error("invalid array depth target description: " + name);
    }
    DepthTarget target;
    target.width = width;
    target.height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = array_size;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D array depth failed: " + name);
    }

    // Whole-array SRV (for sampling in ShadowMaskPS)
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Texture2DArray.MostDetailedMip = 0;
    srv_desc.Texture2DArray.MipLevels = 1;
    srv_desc.Texture2DArray.FirstArraySlice = 0;
    srv_desc.Texture2DArray.ArraySize = array_size;
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &srv_desc,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView array depth failed: " + name);
    }

    // Whole-array DSV (for clearing)
    D3D11_DEPTH_STENCIL_VIEW_DESC main_dsv_desc{};
    main_dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    main_dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    main_dsv_desc.Texture2DArray.MipSlice = 0;
    main_dsv_desc.Texture2DArray.FirstArraySlice = 0;
    main_dsv_desc.Texture2DArray.ArraySize = array_size;
    if (FAILED(device->CreateDepthStencilView(target.texture.Get(), &main_dsv_desc,
                                              target.dsv.GetAddressOf()))) {
        throw std::runtime_error("CreateDepthStencilView array depth failed: " + name);
    }

    // Per-slice DSVs (for depth rendering into each cascade)
    target.slice_dsvs.resize(array_size);
    for (std::uint32_t i = 0; i < array_size; ++i) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
        dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_desc.Texture2DArray.MipSlice = 0;
        dsv_desc.Texture2DArray.FirstArraySlice = i;
        dsv_desc.Texture2DArray.ArraySize = 1;
        if (FAILED(device->CreateDepthStencilView(target.texture.Get(), &dsv_desc,
                                                  target.slice_dsvs[i].GetAddressOf()))) {
            throw std::runtime_error("CreateDepthStencilView slice failed: " + name);
        }
    }

    depths_[name] = std::move(target);
}

const Target* TargetPool::get(const std::string& name) const {
    const auto it = targets_.find(name);
    return it == targets_.end() ? nullptr : &it->second;
}

const DepthTarget* TargetPool::get_depth(const std::string& name) const {
    const auto it = depths_.find(name);
    return it == depths_.end() ? nullptr : &it->second;
}

bool TargetPool::has(const std::string& name) const {
    return targets_.find(name) != targets_.end() || depths_.find(name) != depths_.end();
}

void TargetPool::clear() {
    targets_.clear();
    depths_.clear();
}

} // namespace hikari::gfx
