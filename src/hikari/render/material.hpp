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
    // PMX sphere map: the texture table index and the 0..3 blend mode. The
    // native G-buffer samples it with the view-space normal as UV.
    std::int32_t sphere_texture_index = -1;
    std::uint8_t sphere_mode = 0;
    // Built-in PMX toon index, resolved to a ramp slice by material_from_gpu.
    // An external toon texture has no built-in index, so it keeps the model's
    // procedural terminator until the Materials section registers the texture.
    std::uint8_t toon_flag = 0;
    std::int32_t toon_index = -1;

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
