#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/formats/pmx.hpp"

namespace hikari::render {

struct MaterialConstants {
    std::array<float, 4> diffuse{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> specular{};
    float shininess = 0.0f;
    std::array<float, 3> ambient{0.5f, 0.5f, 0.5f};
    float edge_size = 0.0f;
    std::array<float, 4> edge_color{0.0f, 0.0f, 0.0f, 1.0f};
};

class GpuMaterial {
public:
    std::string name;
    MaterialConstants constants;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
    std::int32_t texture_index = -1;

    static GpuMaterial from_pmx_material(core::D3D11Context& context,
                                         const formats::PmxMaterial& material);
    static GpuMaterial fallback(core::D3D11Context& context, const std::string& name,
                                std::int32_t texture_index);
    void set_constants(core::D3D11Context& context, const MaterialConstants& values);
    void bind_to(ID3D11DeviceContext* context, std::uint32_t slot) const;
    void bind(core::D3D11Context& context, std::uint32_t slot) const;

private:
    static GpuMaterial create(core::D3D11Context& context, std::string name,
                              const MaterialConstants& constants, std::int32_t texture_index);
};

} // namespace hikari::render
