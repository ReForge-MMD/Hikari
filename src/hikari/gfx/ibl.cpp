#include "hikari/gfx/ibl.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <stb_image.h>

#include "hikari/gfx/sky.hpp"

namespace hikari::gfx {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint64_t kMaxFileBytes = 512ull * 1024ull * 1024ull;

constexpr std::uint32_t kDdsPixelFormatFourCc = 0x4;
constexpr std::uint32_t kDdsPixelFormatRgb = 0x40;
constexpr std::uint32_t kDdsCaps2Cubemap = 0x200;
constexpr std::uint32_t kDdsCaps2AllFaces = 0xfc00;
constexpr std::uint32_t kDx10ResourceDimensionTexture2d = 3;
constexpr std::uint32_t kDx10MiscTextureCube = 0x4;

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint32_t four_cc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

float half_to_float(std::uint16_t value) {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
    std::uint32_t exponent = (value >> 10) & 0x1fu;
    std::uint32_t mantissa = value & 0x3ffu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int shift = -1;
            do {
                ++shift;
                mantissa <<= 1;
            } while ((mantissa & 0x400u) == 0);
            mantissa &= 0x3ffu;
            bits = sign | ((127u - 15u - static_cast<std::uint32_t>(shift)) << 23) |
                   (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

enum class SourceFormat { Rgba8, Bgra8, Rgba16f, Rgba32f, Rgb32f };

std::uint32_t bytes_per_texel(SourceFormat format) {
    switch (format) {
    case SourceFormat::Rgba8:
    case SourceFormat::Bgra8:
        return 4;
    case SourceFormat::Rgba16f:
        return 8;
    case SourceFormat::Rgba32f:
        return 16;
    case SourceFormat::Rgb32f:
        return 12;
    }
    return 0;
}

std::string dimension_error(std::uint32_t width, std::uint32_t height, std::uint32_t faces) {
    if (width < kIblMinDimension || height < kIblMinDimension) {
        return "environment image is smaller than 16 pixels";
    }
    if (width > kIblMaxDimension || height > kIblMaxDimension) {
        return "environment image exceeds 4096 pixels";
    }
    const std::uint64_t texels =
        static_cast<std::uint64_t>(width) * height * static_cast<std::uint64_t>(faces);
    if (texels > kIblMaxTexels) {
        return "environment image exceeds the 32 million texel budget";
    }
    return {};
}

std::string equirect_error(std::uint32_t width, std::uint32_t height) {
    if (static_cast<std::uint64_t>(width) == 2ull * height) {
        return {};
    }
    return "equirectangular sky must be 2:1, got " + std::to_string(width) + "x" +
           std::to_string(height);
}

void read_texel(const std::uint8_t* texel, SourceFormat format, float* out) {
    switch (format) {
    case SourceFormat::Rgba8:
        out[0] = static_cast<float>(texel[0]) / 255.0f;
        out[1] = static_cast<float>(texel[1]) / 255.0f;
        out[2] = static_cast<float>(texel[2]) / 255.0f;
        out[3] = static_cast<float>(texel[3]) / 255.0f;
        break;
    case SourceFormat::Bgra8:
        out[0] = static_cast<float>(texel[2]) / 255.0f;
        out[1] = static_cast<float>(texel[1]) / 255.0f;
        out[2] = static_cast<float>(texel[0]) / 255.0f;
        out[3] = static_cast<float>(texel[3]) / 255.0f;
        break;
    case SourceFormat::Rgba16f:
        for (std::uint32_t channel = 0; channel < 4; ++channel) {
            std::uint16_t half = 0;
            std::memcpy(&half, texel + channel * 2, sizeof(half));
            out[channel] = half_to_float(half);
        }
        break;
    case SourceFormat::Rgba32f:
    case SourceFormat::Rgb32f: {
        const std::uint32_t channels = format == SourceFormat::Rgba32f ? 4u : 3u;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            std::memcpy(&out[channel], texel + channel * 4, sizeof(float));
        }
        if (channels == 3) {
            out[3] = 1.0f;
        }
        break;
    }
    }
}

bool parse_dds(const std::uint8_t* bytes, std::size_t size, IblImage& image,
               std::string& error) {
    if (size < 128) {
        error = "DDS header is truncated";
        return false;
    }
    const std::uint32_t height = read_u32(bytes + 12);
    const std::uint32_t width = read_u32(bytes + 16);
    const std::uint32_t mip_count = std::max(read_u32(bytes + 28), 1u);
    const std::uint32_t pf_flags = read_u32(bytes + 80);
    const std::uint32_t four_cc_code = read_u32(bytes + 84);
    const std::uint32_t bit_count = read_u32(bytes + 88);
    const std::uint32_t caps2 = read_u32(bytes + 112);

    SourceFormat format = SourceFormat::Rgba8;
    std::size_t data_offset = 128;
    bool cubemap = false;

    if (four_cc_code == four_cc('D', 'X', '1', '0')) {
        if (size < 148) {
            error = "DDS DX10 header is truncated";
            return false;
        }
        const std::uint32_t dxgi_format = read_u32(bytes + 128);
        const std::uint32_t dimension = read_u32(bytes + 132);
        const std::uint32_t misc = read_u32(bytes + 136);
        const std::uint32_t array_size = std::max(read_u32(bytes + 140), 1u);
        data_offset = 148;
        if (dimension != kDx10ResourceDimensionTexture2d) {
            error = "DDS source must be a 2D texture or a cubemap";
            return false;
        }
        cubemap = (misc & kDx10MiscTextureCube) != 0;
        if (cubemap && array_size < 6) {
            error = "DDS cubemap is missing faces";
            return false;
        }
        switch (dxgi_format) {
        case 2:
            format = SourceFormat::Rgba32f;
            break;
        case 6:
            format = SourceFormat::Rgb32f;
            break;
        case 10:
            format = SourceFormat::Rgba16f;
            break;
        case 28:
            format = SourceFormat::Rgba8;
            break;
        case 87:
        case 91:
            format = SourceFormat::Bgra8;
            break;
        default:
            error = "DDS DXGI format " + std::to_string(dxgi_format) +
                    " is not supported (expected 8-bit RGBA, 16-bit float, or 32-bit float)";
            return false;
        }
    } else if ((pf_flags & kDdsPixelFormatFourCc) != 0) {
        error = "block compressed DDS is not supported; export an uncompressed DDS or an HDR";
        return false;
    } else if ((pf_flags & kDdsPixelFormatRgb) != 0 && bit_count == 32) {
        const std::uint32_t r_mask = read_u32(bytes + 92);
        const std::uint32_t g_mask = read_u32(bytes + 96);
        const std::uint32_t b_mask = read_u32(bytes + 100);
        const std::uint32_t a_mask = read_u32(bytes + 104);
        if (r_mask == 0x000000ffu && g_mask == 0x0000ff00u && b_mask == 0x00ff0000u &&
            (a_mask == 0xff000000u || a_mask == 0u)) {
            format = SourceFormat::Rgba8;
        } else if (b_mask == 0x000000ffu && g_mask == 0x0000ff00u &&
                   r_mask == 0x00ff0000u && (a_mask == 0xff000000u || a_mask == 0u)) {
            format = SourceFormat::Bgra8;
        } else {
            error = "DDS channel masks are not supported (expected 8-bit RGBA or BGRA)";
            return false;
        }
    } else {
        error = "DDS pixel format is not supported (expected 8-bit RGBA or a float format)";
        return false;
    }

    const std::uint32_t faces = cubemap ? 6u : 1u;
    const std::string size_problem = dimension_error(width, height, faces);
    if (!size_problem.empty()) {
        error = size_problem;
        return false;
    }
    if (!cubemap) {
        const std::string ratio_problem = equirect_error(width, height);
        if (!ratio_problem.empty()) {
            error = ratio_problem;
            return false;
        }
    }
    if (cubemap && ((caps2 & kDdsCaps2Cubemap) == 0 ||
                    (caps2 & kDdsCaps2AllFaces) != kDdsCaps2AllFaces)) {
        error = "DDS cubemap is missing faces";
        return false;
    }

    // Faces are stored one after another, each with its whole mip chain, so the
    // first level of face n starts after n complete chains.
    const std::uint32_t texel_bytes = bytes_per_texel(format);
    std::uint64_t level_bytes = 0;
    std::uint64_t face_bytes = 0;
    for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
        const std::uint64_t mip_width = std::max(width >> mip, 1u);
        const std::uint64_t mip_height = std::max(height >> mip, 1u);
        level_bytes = mip_width * mip_height * texel_bytes;
        face_bytes += level_bytes;
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(data_offset) + face_bytes * faces;
    if (required > size) {
        error = "DDS pixel data is truncated";
        return false;
    }

    image.width = width;
    image.height = height;
    image.cubemap = cubemap;
    image.rgba.assign(static_cast<std::size_t>(width) * height * faces * 4, 0.0f);
    const std::size_t texel_count = static_cast<std::size_t>(width) * height;
    for (std::uint32_t face = 0; face < faces; ++face) {
        const std::uint8_t* source = bytes + data_offset + face_bytes * face;
        float* destination = image.rgba.data() + texel_count * face * 4;
        for (std::size_t texel = 0; texel < texel_count; ++texel) {
            read_texel(source + texel * texel_bytes, format, destination + texel * 4);
        }
    }
    return true;
}

IblLoadResult load_hdr(const void* data, int size) {
    const auto* bytes = static_cast<const stbi_uc*>(data);
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(bytes, size, &width, &height, &channels) == 0) {
        return {{}, "unsupported image container (expected Radiance HDR or uncompressed DDS)"};
    }
    const std::string size_problem =
        dimension_error(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1);
    if (!size_problem.empty()) {
        return {{}, size_problem};
    }
    const std::string ratio_problem =
        equirect_error(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    if (!ratio_problem.empty()) {
        return {{}, ratio_problem};
    }

    float* pixels = stbi_loadf_from_memory(bytes, size, &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return {{}, "image decode failed"};
    }
    IblLoadResult result;
    result.image.width = static_cast<std::uint32_t>(width);
    result.image.height = static_cast<std::uint32_t>(height);
    result.image.cubemap = false;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    result.image.rgba.assign(pixels, pixels + texels * 4);
    stbi_image_free(pixels);
    return result;
}

std::size_t wrap_index(std::int64_t index, std::uint32_t size) {
    const std::int64_t wrapped =
        ((index % static_cast<std::int64_t>(size)) + static_cast<std::int64_t>(size)) %
        static_cast<std::int64_t>(size);
    return static_cast<std::size_t>(wrapped);
}

void bilinear_equirect(const IblImage& source, float u, float v, float* out) {
    const float x = u * static_cast<float>(source.width) - 0.5f;
    const float y = v * static_cast<float>(source.height) - 0.5f;
    const std::int64_t x0 = static_cast<std::int64_t>(std::floor(x));
    const std::int64_t y0 = static_cast<std::int64_t>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto clamp_y = [&](std::int64_t row) {
        return static_cast<std::size_t>(
            std::clamp<std::int64_t>(row, 0, static_cast<std::int64_t>(source.height) - 1));
    };
    const std::size_t columns[2] = {wrap_index(x0, source.width),
                                    wrap_index(x0 + 1, source.width)};
    const std::size_t rows[2] = {clamp_y(y0), clamp_y(y0 + 1)};
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        const auto texel = [&](std::uint32_t column, std::uint32_t row) {
            return source.rgba[(rows[row] * source.width + columns[column]) * 4 + channel];
        };
        const float top = texel(0, 0) + (texel(1, 0) - texel(0, 0)) * tx;
        const float bottom = texel(0, 1) + (texel(1, 1) - texel(0, 1)) * tx;
        out[channel] = top + (bottom - top) * ty;
    }
}

void bilinear_face(const float* face, std::uint32_t width, std::uint32_t height, float u, float v,
                   float* out) {
    const float x = u * static_cast<float>(width) - 0.5f;
    const float y = v * static_cast<float>(height) - 0.5f;
    const std::int64_t x0 = static_cast<std::int64_t>(std::floor(x));
    const std::int64_t y0 = static_cast<std::int64_t>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto clamp_x = [&](std::int64_t column) {
        return static_cast<std::size_t>(
            std::clamp<std::int64_t>(column, 0, static_cast<std::int64_t>(width) - 1));
    };
    const auto clamp_y = [&](std::int64_t row) {
        return static_cast<std::size_t>(
            std::clamp<std::int64_t>(row, 0, static_cast<std::int64_t>(height) - 1));
    };
    const std::size_t columns[2] = {clamp_x(x0), clamp_x(x0 + 1)};
    const std::size_t rows[2] = {clamp_y(y0), clamp_y(y0 + 1)};
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        const auto texel = [&](std::uint32_t column, std::uint32_t row) {
            return face[(rows[row] * width + columns[column]) * 4 + channel];
        };
        const float top = texel(0, 0) + (texel(1, 0) - texel(0, 0)) * tx;
        const float bottom = texel(0, 1) + (texel(1, 1) - texel(0, 1)) * tx;
        out[channel] = top + (bottom - top) * ty;
    }
}

} // namespace

std::size_t IblImage::texel_count() const {
    return static_cast<std::size_t>(width) * height;
}

const float* IblImage::face_data(std::uint32_t face) const {
    if (!cubemap || face >= 6 || rgba.size() < texel_count() * 6 * 4) {
        return nullptr;
    }
    return rgba.data() + texel_count() * face * 4;
}

IblLoadResult load_ibl_image(const std::string& path) {
    const std::u8string utf8(path.begin(), path.end());
    std::ifstream file(std::filesystem::path(utf8), std::ios::binary | std::ios::ate);
    if (!file) {
        return {{}, "cannot open " + path};
    }
    const std::streamoff length = file.tellg();
    if (length <= 0) {
        return {{}, "environment file is empty: " + path};
    }
    if (static_cast<std::uint64_t>(length) > kMaxFileBytes) {
        return {{}, "environment file is larger than 512 MB: " + path};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        return {{}, "short read on " + path};
    }
    IblLoadResult result = load_ibl_image_from_memory(bytes.data(), bytes.size());
    if (!result.ok()) {
        result.error = path + ": " + result.error;
    }
    return result;
}

IblLoadResult load_ibl_image_from_memory(const void* data, std::size_t size) {
    if (data == nullptr || size < 16) {
        return {{}, "image data is too small to identify"};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (size >= 4 && std::memcmp(bytes, "DDS ", 4) == 0) {
        IblLoadResult result;
        if (!parse_dds(bytes, size, result.image, result.error)) {
            return {{}, result.error};
        }
        return result;
    }
    if (size > static_cast<std::size_t>(0x7fffffff)) {
        return {{}, "image data exceeds the decoder limit"};
    }
    return load_hdr(data, static_cast<int>(size));
}

Vec3 ibl_cube_texel_direction(std::uint32_t face, float u, float v) {
    const CubeFaceBasis& basis = cube_face_basis(face);
    const float x = u * 2.0f - 1.0f;
    const float y = 1.0f - v * 2.0f;
    const Vec3 direction(basis.right.x * x + basis.up.x * y + basis.forward.x,
                         basis.right.y * x + basis.up.y * y + basis.forward.y,
                         basis.right.z * x + basis.up.z * y + basis.forward.z);
    return direction.normalize();
}

IblLoadResult ibl_image_to_cubemap(const IblImage& source, std::uint32_t face_size) {
    if (face_size < kIblMinDimension || face_size > kIblMaxDimension) {
        return {{}, "cubemap face size " + std::to_string(face_size) +
                        " is outside 16..4096"};
    }
    if (source.width == 0 || source.height == 0) {
        return {{}, "environment image has no pixels"};
    }
    const std::uint32_t faces = source.cubemap ? 6u : 1u;
    const std::size_t expected = source.texel_count() * faces * 4;
    if (source.rgba.size() != expected) {
        return {{}, "environment image buffer does not match its dimensions"};
    }

    IblLoadResult result;
    result.image.width = face_size;
    result.image.height = face_size;
    result.image.cubemap = true;
    const std::size_t output_texels = static_cast<std::size_t>(face_size) * face_size;
    result.image.rgba.assign(output_texels * 6 * 4, 0.0f);

    for (std::uint32_t face = 0; face < 6; ++face) {
        const float* source_face = source.cubemap ? source.face_data(face) : nullptr;
        float* destination = result.image.rgba.data() + output_texels * face * 4;
        for (std::uint32_t y = 0; y < face_size; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_size);
            for (std::uint32_t x = 0; x < face_size; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_size);
                float* texel = destination + (static_cast<std::size_t>(y) * face_size + x) * 4;
                if (source_face != nullptr) {
                    bilinear_face(source_face, source.width, source.height, u, v, texel);
                } else {
                    const Vec3 direction = ibl_cube_texel_direction(face, u, v);
                    const float longitude =
                        std::atan2(direction.x, direction.z) / (2.0f * kPi) + 0.5f;
                    const float latitude =
                        std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / kPi;
                    bilinear_equirect(source, longitude - std::floor(longitude), latitude, texel);
                }
            }
        }
    }
    return result;
}

} // namespace hikari::gfx