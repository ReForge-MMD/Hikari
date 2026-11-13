#include "hikari/render/material.hpp"

#include <cstring>
#include <stdexcept>

namespace hikari::render {

GpuMaterial GpuMaterial::create(core::D3D11Context& context, std::string name,
                                const MaterialConstants& constants, std::int32_t texture_index) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(MaterialConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    GpuMaterial material;
    material.name = std::move(name);
    material.constants = constants;
    material.texture_index = texture_index;
    if (FAILED(context.device->CreateBuffer(&desc, nullptr,
                                            material.constant_buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer material failed");
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context.context->Map(material.constant_buffer.Get(), 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context.context->Unmap(material.constant_buffer.Get(), 0);
    }
    return material;
}

GpuMaterial GpuMaterial::from_pmx_material(core::D3D11Context& context,
                                           const formats::PmxMaterial& material) {
    MaterialConstants constants;
    constants.diffuse = material.diffuse;
    constants.specular = material.specular;
    constants.shininess = material.shininess;
    constants.ambient = material.ambient;
    constants.edge_size = material.edge_size;
    constants.edge_color = material.edge_color;
    GpuMaterial result = create(context, material.name_local, constants, material.texture_index);
    result.sphere_texture_index = material.sphere_texture_index;
    result.sphere_mode = material.sphere_mode;
    result.toon_flag = material.toon_flag;
    result.toon_index = material.toon_index;
    return result;
}

GpuMaterial GpuMaterial::fallback(core::D3D11Context& context, const std::string& name,
                                  std::int32_t texture_index) {
    MaterialConstants constants;
    constants.diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
    constants.specular = {0.0f, 0.0f, 0.0f};
    constants.shininess = 0.0f;
    constants.ambient = {0.5f, 0.5f, 0.5f};
    constants.edge_size = 0.0f;
    constants.edge_color = {0.0f, 0.0f, 0.0f, 1.0f};
    return create(context, name, constants, texture_index);
}

void GpuMaterial::set_constants(core::D3D11Context& context,
                                const MaterialConstants& values) {
    if (std::memcmp(&constants, &values, sizeof(MaterialConstants)) == 0) {
        return;
    }
    constants = values;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context.context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                       &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context.context->Unmap(constant_buffer.Get(), 0);
    }
}

void GpuMaterial::bind_to(ID3D11DeviceContext* context, std::uint32_t slot) const {
    ID3D11Buffer* buffers[] = {constant_buffer.Get()};
    context->PSSetConstantBuffers(slot, 1, buffers);
    context->VSSetConstantBuffers(slot, 1, buffers);
}

void GpuMaterial::bind(core::D3D11Context& context, std::uint32_t slot) const {
    bind_to(context.context.Get(), slot);
}

} // namespace hikari::render
