#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <d3d11.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/gfx/settings.hpp"

namespace hikari::gfx_lab {

struct CaptureData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

CaptureData read_target(hikari::core::D3D11Context& context, ID3D11Texture2D* texture,
                        std::uint32_t width, std::uint32_t height);

bool write_capture(const std::string& path, const CaptureData& capture, std::string* error);
std::optional<CaptureData> read_capture(const std::string& path, std::string* error);

bool write_probe(const std::string& path, const CaptureData& capture,
                 const hikari::gfx::GraphicsSettings& settings, const std::string& scene,
                 const std::string& phase, double frame_ms, std::string* error);
// Mean absolute channel difference in the 0..1 range.
float mean_channel_difference(const CaptureData& a, const CaptureData& b);
// Mean of the three channel averages, in the 0..1 range.
float mean_luminance(const CaptureData& capture);

} // namespace hikari::gfx_lab
