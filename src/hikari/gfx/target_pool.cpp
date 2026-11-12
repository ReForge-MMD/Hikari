#include "hikari/gfx/target_pool.hpp"

#include <algorithm>
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

void TargetPool::create_cubemap(ID3D11Device* device, const std::string& name,
                                std::uint32_t size, DXGI_FORMAT format,
                                std::uint32_t mip_levels) {
    if (device == nullptr || size == 0) {
        throw std::runtime_error("invalid cubemap description: " + name);
    }
    std::uint32_t available = 1;
    for (std::uint32_t mip_size = size; mip_size > 1; mip_size >>= 1) {
        ++available;
    }
    mip_levels = std::clamp(mip_levels, 1u, available);
    Target target;
    target.width = size;
    target.height = size;
    target.format = format;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = mip_levels;
    desc.ArraySize = 6;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D cubemap failed: " + name);
    }

    target.mip_face_rtvs.resize(static_cast<std::size_t>(mip_levels) * 6);
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        for (std::uint32_t face = 0; face < 6; ++face) {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
            rtv_desc.Format = format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv_desc.Texture2DArray.MipSlice = mip;
            rtv_desc.Texture2DArray.FirstArraySlice = face;
            rtv_desc.Texture2DArray.ArraySize = 1;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv =
                target.mip_face_rtvs[static_cast<std::size_t>(mip) * 6 + face];
            if (FAILED(device->CreateRenderTargetView(target.texture.Get(), &rtv_desc,
                                                      rtv.GetAddressOf()))) {
                throw std::runtime_error("CreateRenderTargetView cubemap face failed: " + name);
            }
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = 0;
    srv_desc.TextureCube.MipLevels = mip_levels;
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &srv_desc,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView cubemap failed: " + name);
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

void TargetPool::create_volume(ID3D11Device* device, const std::string& name,
                               std::uint32_t width, std::uint32_t height,
                               std::uint32_t depth, DXGI_FORMAT format,
                               std::uint32_t mip_levels) {
    if (device == nullptr || width == 0 || height == 0 || depth == 0) {
        throw std::runtime_error("invalid volume target description: " + name);
    }
    std::uint32_t max_dim = std::max({width, height, depth});
    std::uint32_t available = 1;
    for (std::uint32_t s = max_dim; s > 1; s >>= 1) {
        ++available;
    }
    mip_levels = std::clamp(mip_levels, 1u, available);

    VolumeTarget target;
    target.width = width;
    target.height = height;
    target.depth = depth;
    target.mip_levels = mip_levels;
    target.format = format;

    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Depth = depth;
    desc.MipLevels = mip_levels;
    desc.Format = format;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device->CreateTexture3D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture3D failed: " + name);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srv_desc.Texture3D.MostDetailedMip = 0;
    srv_desc.Texture3D.MipLevels = mip_levels;
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &srv_desc,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView volume failed: " + name);
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.Format = format;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    uav_desc.Texture3D.MipSlice = 0;
    uav_desc.Texture3D.FirstWSlice = 0;
    uav_desc.Texture3D.WSize = depth;
    if (FAILED(device->CreateUnorderedAccessView(target.texture.Get(), &uav_desc,
                                                target.uav.GetAddressOf()))) {
        throw std::runtime_error("CreateUnorderedAccessView volume failed: " + name);
    }

    target.mip_uavs.resize(mip_levels);
    target.mip_srvs.resize(mip_levels);
    target.mip_uavs[0] = target.uav;

    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        D3D11_SHADER_RESOURCE_VIEW_DESC msrv{};
        msrv.Format = format;
        msrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        msrv.Texture3D.MostDetailedMip = mip;
        msrv.Texture3D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &msrv,
                                                    target.mip_srvs[mip].GetAddressOf()))) {
            throw std::runtime_error("CreateShaderResourceView volume mip failed: " + name);
        }

        if (mip > 0) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC muav{};
            muav.Format = format;
            muav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            muav.Texture3D.MipSlice = mip;
            muav.Texture3D.FirstWSlice = 0;
            muav.Texture3D.WSize = std::max(1u, depth >> mip);
            if (FAILED(device->CreateUnorderedAccessView(target.texture.Get(), &muav,
                                                        target.mip_uavs[mip].GetAddressOf()))) {
                throw std::runtime_error("CreateUnorderedAccessView volume mip failed: " + name);
            }
        }
    }

    volumes_[name] = std::move(target);
}

const Target* TargetPool::get(const std::string& name) const {
    const auto it = targets_.find(name);
    return it == targets_.end() ? nullptr : &it->second;
}

const DepthTarget* TargetPool::get_depth(const std::string& name) const {
    const auto it = depths_.find(name);
    return it == depths_.end() ? nullptr : &it->second;
}

const VolumeTarget* TargetPool::get_volume(const std::string& name) const {
    const auto it = volumes_.find(name);
    return it == volumes_.end() ? nullptr : &it->second;
}

bool TargetPool::has(const std::string& name) const {
    return targets_.find(name) != targets_.end() || depths_.find(name) != depths_.end() ||
           volumes_.find(name) != volumes_.end();
}

void TargetPool::clear() {
    targets_.clear();
    depths_.clear();
    volumes_.clear();
}

} // namespace hikari::gfx
