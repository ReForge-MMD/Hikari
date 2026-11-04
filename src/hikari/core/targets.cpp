#include "hikari/core/targets.hpp"

#include <stdexcept>

namespace hikari::core {

void RenderTargetManager::create_target(ID3D11Device* device, const std::string& name,
                                        std::uint32_t width, std::uint32_t height,
                                        DXGI_FORMAT format, bool enable_uav) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    bool needs_creation = true;
    const auto existing = targets_.find(name);
    if (existing != targets_.end()) {
        const auto& target = existing->second;
        needs_creation = target.width != width || target.height != height ||
                         target.format != format || enable_uav != (target.uav != nullptr);
    }
    if (!needs_creation) {
        return;
    }

    UINT bind_flags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (enable_uav) {
        bind_flags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;

    OffscreenRenderTarget target;
    target.width = width;
    target.height = height;
    target.format = format;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D failed for render target " + name);
    }
    if (FAILED(device->CreateRenderTargetView(target.texture.Get(), nullptr,
                                              target.rtv.GetAddressOf()))) {
        throw std::runtime_error("CreateRenderTargetView failed for " + name);
    }
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), nullptr,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed for " + name);
    }
    if (enable_uav && FAILED(device->CreateUnorderedAccessView(target.texture.Get(), nullptr,
                                                               target.uav.GetAddressOf()))) {
        throw std::runtime_error("CreateUnorderedAccessView failed for " + name);
    }
    targets_[name] = std::move(target);
}

void RenderTargetManager::create_depth(ID3D11Device* device, const std::string& name,
                                       std::uint32_t width, std::uint32_t height) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    const auto existing = depths_.find(name);
    if (existing != depths_.end() && existing->second.width == width &&
        existing->second.height == height) {
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    DepthStencilTarget target;
    target.width = width;
    target.height = height;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D failed for depth target " + name);
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateDepthStencilView(target.texture.Get(), &dsv_desc,
                                              target.dsv.GetAddressOf()))) {
        throw std::runtime_error("CreateDepthStencilView failed for " + name);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(target.texture.Get(), &srv_desc,
                                                target.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed for depth " + name);
    }
    depths_[name] = std::move(target);
}

void RenderTargetManager::create_structured_buffer(ID3D11Device* device, const std::string& name,
                                                   std::uint32_t element_size,
                                                   std::uint32_t element_count, bool enable_uav) {
    const UINT total_bytes = std::max(element_size * element_count, 16u);
    UINT bind_flags = D3D11_BIND_SHADER_RESOURCE;
    if (enable_uav) {
        bind_flags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = total_bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = element_size;

    StructuredGpuBuffer buffer;
    buffer.element_size = element_size;
    buffer.element_count = element_count;
    if (FAILED(device->CreateBuffer(&desc, nullptr, buffer.buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer failed for structured buffer " + name);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    srv_desc.BufferEx.FirstElement = 0;
    srv_desc.BufferEx.NumElements = element_count;
    if (FAILED(device->CreateShaderResourceView(buffer.buffer.Get(), &srv_desc,
                                                buffer.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed for " + name);
    }

    if (enable_uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = element_count;
        if (FAILED(device->CreateUnorderedAccessView(buffer.buffer.Get(), &uav_desc,
                                                     buffer.uav.GetAddressOf()))) {
            throw std::runtime_error("CreateUnorderedAccessView failed for " + name);
        }
    }
    buffers_[name] = std::move(buffer);
}

const OffscreenRenderTarget* RenderTargetManager::get_target(const std::string& name) const {
    const auto it = targets_.find(name);
    return it == targets_.end() ? nullptr : &it->second;
}

ID3D11RenderTargetView* RenderTargetManager::get_rtv(const std::string& name) const {
    const auto it = targets_.find(name);
    return it == targets_.end() ? nullptr : it->second.rtv.Get();
}

const DepthStencilTarget* RenderTargetManager::get_depth(const std::string& name) const {
    const auto it = depths_.find(name);
    return it == depths_.end() ? nullptr : &it->second;
}

ID3D11DepthStencilView* RenderTargetManager::get_dsv(const std::string& name) const {
    const auto it = depths_.find(name);
    return it == depths_.end() ? nullptr : it->second.dsv.Get();
}

const StructuredGpuBuffer* RenderTargetManager::get_buffer(const std::string& name) const {
    const auto it = buffers_.find(name);
    return it == buffers_.end() ? nullptr : &it->second;
}

void RenderTargetManager::bind_mrt(ID3D11DeviceContext* context,
                                   const std::vector<ID3D11RenderTargetView*>& rtvs,
                                   ID3D11DepthStencilView* dsv) {
    const std::size_t count = std::min<std::size_t>(rtvs.size(), 8);
    for (std::size_t i = 0; i < 8; ++i) {
        active_rtvs_[i] = i < count ? rtvs[i] : nullptr;
    }
    active_dsv_ = dsv;
    context->OMSetRenderTargets(static_cast<UINT>(count), count > 0 ? rtvs.data() : nullptr, dsv);
}

void RenderTargetManager::clear_render_target(ID3D11DeviceContext* context, std::size_t slot,
                                              const float color[4]) const {
    if (slot < active_rtvs_.size() && active_rtvs_[slot]) {
        context->ClearRenderTargetView(active_rtvs_[slot].Get(), color);
    }
}

void RenderTargetManager::clear_depth_stencil(ID3D11DeviceContext* context, bool clear_depth,
                                              float depth, bool clear_stencil,
                                              std::uint8_t stencil) const {
    if (!active_dsv_) {
        return;
    }
    UINT flags = 0;
    if (clear_depth) {
        flags |= D3D11_CLEAR_DEPTH;
    }
    if (clear_stencil) {
        flags |= D3D11_CLEAR_STENCIL;
    }
    if (flags != 0) {
        context->ClearDepthStencilView(active_dsv_.Get(), flags, depth, stencil);
    }
}

} // namespace hikari::core
