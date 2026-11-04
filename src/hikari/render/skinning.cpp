#include "hikari/render/skinning.hpp"

#include <cstring>
#include <stdexcept>

namespace hikari::render {

GpuSkinningBuffer::GpuSkinningBuffer(core::D3D11Context& context) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(Mat4) * kMaxBones);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context.device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer skinning failed");
    }
}

void GpuSkinningBuffer::update_on(ID3D11DeviceContext* context,
                                  const std::vector<Mat4>& palette) const {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map skinning buffer failed");
    }
    auto* destination = static_cast<Mat4*>(mapped.pData);
    const std::size_t count = std::min(palette.size(), kMaxBones);
    if (count > 0) {
        std::memcpy(destination, palette.data(), count * sizeof(Mat4));
    }
    const Mat4 identity = Mat4::identity();
    for (std::size_t i = count; i < kMaxBones; ++i) {
        destination[i] = identity;
    }
    context->Unmap(buffer.Get(), 0);
}

void GpuSkinningBuffer::update(core::D3D11Context& context,
                               const std::vector<Mat4>& palette) const {
    update_on(context.context.Get(), palette);
}

void GpuSkinningBuffer::bind_vs_to(ID3D11DeviceContext* context, std::uint32_t slot) const {
    ID3D11Buffer* buffers[] = {buffer.Get()};
    context->VSSetConstantBuffers(slot, 1, buffers);
}

void GpuSkinningBuffer::bind_vs(core::D3D11Context& context, std::uint32_t slot) const {
    bind_vs_to(context.context.Get(), slot);
}

} // namespace hikari::render
