#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"

namespace hikari::render {

class GpuTexture {
public:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    static GpuTexture from_rgba_bytes(core::D3D11Context& context, std::uint32_t width,
                                      std::uint32_t height, const std::uint8_t* pixels);
    // Same as from_rgba_bytes but builds a full mip chain so minified procedural
    // sampling does not alias.
    static GpuTexture from_rgba_bytes_mipped(core::D3D11Context& context,
                                             std::uint32_t width, std::uint32_t height,
                                             const std::uint8_t* pixels);
    static GpuTexture from_file(core::D3D11Context& context, const std::wstring& path);
    static GpuTexture from_memory(core::D3D11Context& context, const std::uint8_t* data,
                                  std::size_t size);
    static GpuTexture create_solid_color(core::D3D11Context& context,
                                         const std::uint8_t color[4]);
};

} // namespace hikari::render
