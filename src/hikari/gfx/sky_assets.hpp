#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace hikari::gfx {

// A generated RGBA8 image handed to the renderer as an immutable texture.
struct SkyImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

// Deterministic generators. The sizes are explicit so tests can build small
// images without paying for the full resolution.
SkyImage build_cloud_noise_image(std::uint32_t size);
SkyImage build_milky_way_image(std::uint32_t width, std::uint32_t height);
SkyImage build_moon_albedo_image(std::uint32_t size);

// Tiling cloud noise. R holds the shaped base fbm, G the high frequency detail,
// B a low frequency mask that varies coverage across the sky.
const SkyImage& cloud_noise_image();

// Equirectangular Milky Way band in the red channel: a bright ridge with dust
// lanes and clumps. Sampled in the shader and tinted there.
const SkyImage& milky_way_image();

// World space direction of the brightest part of the generated band. The lab
// uses it to frame the band for verification captures.
std::array<float, 3> milky_way_core_direction();

// Equirectangular lunar albedo: bright highlands and darker maria.
const SkyImage& moon_albedo_image();

} // namespace hikari::gfx