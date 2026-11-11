#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hikari/math.hpp"

namespace hikari::gfx {

// Sources below this size cannot contribute a usable specular lobe, and sizes
// above it only cost memory without surviving the 256 pixel capture.
inline constexpr std::uint32_t kIblMinDimension = 16;
inline constexpr std::uint32_t kIblMaxDimension = 4096;

// Ceiling for the decoded float pixels. A 2048 pixel cubemap needs 25 million
// texels; anything larger is a mistake rather than a sky.
inline constexpr std::size_t kIblMaxTexels = 32u * 1024u * 1024u;

// Decoded environment source: one equirectangular image or six cubemap faces
// stored back to back in the Direct3D order (+X, -X, +Y, -Y, +Z, -Z). Pixels
// are linear float RGBA, so an HDR source keeps its range.
struct IblImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool cubemap = false;
    std::vector<float> rgba;

    std::size_t texel_count() const;
    const float* face_data(std::uint32_t face) const;
};

// Loading and conversion report failure through `error` instead of throwing,
// because a broken user file must not take the renderer down mid frame.
struct IblLoadResult {
    IblImage image;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

// Reads `.hdr` (Radiance) and `.dds`. The container is detected from the file
// contents, not the extension, so a renamed panorama still loads.
IblLoadResult load_ibl_image(const std::string& path);
IblLoadResult load_ibl_image_from_memory(const void* data, std::size_t size);

// Direction of a texel on a cube face, matching how the shader reconstructs the
// face direction from the capture matrices. `u` and `v` are in [0, 1] with v = 0
// at the top of the face (the -X face of an equirectangular source therefore
// keeps its screen orientation).
Vec3 ibl_cube_texel_direction(std::uint32_t face, float u, float v);

// Equirectangular to cubemap conversion, with the longitude convention of the
// reference implementation: u = atan2(x, z) / 2pi + 0.5, v = acos(y) / pi. A
// cubemap source is resampled face by face instead, so both container layouts
// end up in the same capture shape.
IblLoadResult ibl_image_to_cubemap(const IblImage& source, std::uint32_t face_size);

} // namespace hikari::gfx