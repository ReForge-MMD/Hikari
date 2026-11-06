#include "hikari/render/skinning.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace hikari::render {

void GpuSkinningBuffer::allocate(core::D3D11Context& context, std::size_t count) {
    if (count > std::numeric_limits<UINT>::max() / sizeof(Mat4)) {
        throw std::runtime_error("skinning palette is too large for a D3D11 buffer");
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(count * sizeof(Mat4));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = static_cast<UINT>(sizeof(Mat4));
    if (FAILED(context.device->CreateBuffer(&desc, nullptr, buffer.ReleaseAndGetAddressOf()))) {
        throw std::runtime_error("CreateBuffer skinning failed");
    }
    capacity_ = count;
    create_view(context, count);
}

void GpuSkinningBuffer::create_view(core::D3D11Context& context, std::size_t count) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = static_cast<UINT>(count);
    if (FAILED(context.device->CreateShaderResourceView(
            buffer.Get(), &desc, srv.ReleaseAndGetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView skinning failed");
    }
    view_count_ = count;
}

void GpuSkinningBuffer::update(core::D3D11Context& context,
                               const std::vector<Mat4>& palette) {
    if (palette.empty()) {
        buffer.Reset();
        srv.Reset();
        capacity_ = 0;
        view_count_ = 0;
        return;
    }
    if (palette.size() > capacity_) {
        allocate(context, palette.size());
    } else if (palette.size() != view_count_) {
        create_view(context, palette.size());
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map skinning buffer failed");
    }
    std::memcpy(mapped.pData, palette.data(), palette.size() * sizeof(Mat4));
    context.context->Unmap(buffer.Get(), 0);
}

void GpuSkinningBuffer::bind_vs_to(ID3D11DeviceContext* context, std::uint32_t slot) const {
    ID3D11ShaderResourceView* views[] = {srv.Get()};
    context->VSSetShaderResources(slot, 1, views);
}

void GpuSkinningBuffer::bind_vs(core::D3D11Context& context, std::uint32_t slot) const {
    bind_vs_to(context.context.Get(), slot);
}

} // namespace hikari::render
