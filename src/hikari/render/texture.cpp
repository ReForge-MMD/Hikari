#include "hikari/render/texture.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <stb_image.h>

namespace hikari::render {
namespace {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class DdsCompression { Dxt1, Dxt3, Dxt5 };

void decode_bc(std::uint32_t width, std::uint32_t height, const std::uint8_t* blocks,
               DdsCompression compression, Image& image) {
    const bool block_alpha = compression == DdsCompression::Dxt3 ||
                             compression == DdsCompression::Dxt5;
    const std::uint32_t block_size = block_alpha ? 16 : 8;
    const std::uint32_t blocks_per_row = (width + 3) / 4;
    const std::uint32_t block_rows = (height + 3) / 4;
    image.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::uint32_t by = 0; by < block_rows; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_per_row; ++bx) {
            const std::uint8_t* block =
                blocks + (static_cast<std::size_t>(by) * blocks_per_row + bx) * block_size;
            const std::uint8_t* color_block =
                compression == DdsCompression::Dxt1 ? block : block + 8;
            const std::uint16_t c0 = static_cast<std::uint16_t>(color_block[0] | (color_block[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(color_block[2] | (color_block[3] << 8));
            std::uint32_t palette[4] = {};
            const auto expand = [](std::uint16_t c) {
                const std::uint32_t r = (c >> 11) & 0x1F;
                const std::uint32_t g = (c >> 5) & 0x3F;
                const std::uint32_t b = c & 0x1F;
                return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) |
                       (((b << 3) | (b >> 2)) << 16);
            };
            palette[0] = expand(c0);
            palette[1] = expand(c1);
            if (c0 > c1 || compression != DdsCompression::Dxt1) {
                palette[2] = ((palette[0] & 0xFEFEFE) >> 1) + ((palette[1] & 0xFEFEFE) >> 1);
                palette[3] = 0;
            } else {
                palette[2] = ((palette[0] + palette[1]) & 0xFEFEFE) >> 1;
                palette[3] = 0;
            }
            const std::uint32_t indices = static_cast<std::uint32_t>(color_block[4]) |
                                          (static_cast<std::uint32_t>(color_block[5]) << 8) |
                                          (static_cast<std::uint32_t>(color_block[6]) << 16) |
                                          (static_cast<std::uint32_t>(color_block[7]) << 24);

            std::uint32_t alphas[4] = {255, 255, 255, 255};
            std::uint64_t alpha_indices = 0;
            if (compression == DdsCompression::Dxt5) {
                const std::uint32_t a0 = block[0];
                const std::uint32_t a1 = block[1];
                alphas[0] = a0;
                alphas[1] = a1;
                if (a0 > a1) {
                    alphas[2] = (6 * a0 + 1 * a1) / 7;
                    alphas[3] = (1 * a0 + 6 * a1) / 7;
                } else {
                    alphas[2] = (4 * a0 + 1 * a1) / 5;
                    alphas[3] = (1 * a0 + 4 * a1) / 5;
                }
                for (int i = 0; i < 6; ++i) {
                    alpha_indices |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
                }
            } else if (compression == DdsCompression::Dxt3) {
                alpha_indices = static_cast<std::uint64_t>(block[0]) |
                                (static_cast<std::uint64_t>(block[1]) << 8) |
                                (static_cast<std::uint64_t>(block[2]) << 16) |
                                (static_cast<std::uint64_t>(block[3]) << 24) |
                                (static_cast<std::uint64_t>(block[4]) << 32) |
                                (static_cast<std::uint64_t>(block[5]) << 40) |
                                (static_cast<std::uint64_t>(block[6]) << 48) |
                                (static_cast<std::uint64_t>(block[7]) << 56);
            }

            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    const std::uint32_t x = bx * 4 + static_cast<std::uint32_t>(px);
                    const std::uint32_t y = by * 4 + static_cast<std::uint32_t>(py);
                    if (x >= width || y >= height) {
                        continue;
                    }
                    const std::uint32_t pixel = py * 4 + px;
                    const std::uint32_t index = (indices >> (2 * pixel)) & 3;
                    const std::uint32_t color = palette[index];
                    std::uint8_t alpha = 255;
                    if (compression == DdsCompression::Dxt5) {
                        alpha = static_cast<std::uint8_t>(
                            std::min<std::uint32_t>(alphas[(alpha_indices >> (3 * pixel)) & 7], 255));
                    } else if (compression == DdsCompression::Dxt3) {
                        alpha = static_cast<std::uint8_t>(
                            ((alpha_indices >> (4 * pixel)) & 0xF) * 17);
                    } else if (c0 <= c1 && index == 3) {
                        alpha = 0;
                    }
                    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
                    image.rgba[offset + 0] = static_cast<std::uint8_t>(color & 0xFF);
                    image.rgba[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 0xFF);
                    image.rgba[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 0xFF);
                    image.rgba[offset + 3] = alpha;
                }
            }
        }
    }
}

bool load_dds(const std::vector<std::uint8_t>& bytes, Image& image) {
    if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) != 0) {
        return false;
    }
    const auto* data = bytes.data();
    const std::uint32_t height = *reinterpret_cast<const std::uint32_t*>(data + 12);
    const std::uint32_t width = *reinterpret_cast<const std::uint32_t*>(data + 16);
    const std::uint32_t header_size = *reinterpret_cast<const std::uint32_t*>(data + 76);
    if (header_size != 32 || width == 0 || height == 0) {
        return false;
    }
    const std::uint32_t four_cc = *reinterpret_cast<const std::uint32_t*>(data + 84);
    const std::uint32_t rgb_bits = *reinterpret_cast<const std::uint32_t*>(data + 88);
    const std::uint32_t masks[4] = {
        *reinterpret_cast<const std::uint32_t*>(data + 92),
        *reinterpret_cast<const std::uint32_t*>(data + 96),
        *reinterpret_cast<const std::uint32_t*>(data + 100),
        *reinterpret_cast<const std::uint32_t*>(data + 104)};
    const std::uint8_t* payload = data + 128;

    image.width = width;
    image.height = height;

    if (four_cc == 0x31545844) { // DXT1
        decode_bc(width, height, payload, DdsCompression::Dxt1, image);
        return true;
    }
    if (four_cc == 0x33545844) { // DXT3
        decode_bc(width, height, payload, DdsCompression::Dxt3, image);
        return true;
    }
    if (four_cc == 0x35545844) { // DXT5
        decode_bc(width, height, payload, DdsCompression::Dxt5, image);
        return true;
    }
    if (four_cc != 0 || rgb_bits != 32) {
        return false;
    }

    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const auto channel = [&](std::uint32_t pixel, std::uint32_t mask) -> std::uint8_t {
        if (mask == 0) {
            return 255;
        }
        const std::uint32_t shift = [&] {
            std::uint32_t value = 0;
            std::uint32_t bit = mask;
            while ((bit & 1u) == 0 && bit != 0) {
                bit >>= 1;
                ++value;
            }
            return value;
        }();
        const std::uint32_t max_value = mask >> shift;
        return static_cast<std::uint8_t>(((pixel & mask) >> shift) * 255 / max_value);
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t pixel = *reinterpret_cast<const std::uint32_t*>(
                payload + (static_cast<std::size_t>(y) * width + x) * 4);
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            image.rgba[offset + 0] = channel(pixel, masks[0]);
            image.rgba[offset + 1] = channel(pixel, masks[1]);
            image.rgba[offset + 2] = channel(pixel, masks[2]);
            image.rgba[offset + 3] = channel(pixel, masks[3]);
        }
    }
    return true;
}

} // namespace

GpuTexture GpuTexture::from_rgba_bytes(core::D3D11Context& context, std::uint32_t width,
                                       std::uint32_t height, const std::uint8_t* pixels) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels;
    data.SysMemPitch = width * 4;

    GpuTexture result;
    result.width = width;
    result.height = height;
    if (FAILED(context.device->CreateTexture2D(&desc, &data, result.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D failed");
    }
    if (FAILED(context.device->CreateShaderResourceView(result.texture.Get(), nullptr,
                                                        result.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed");
    }
    return result;
}

GpuTexture GpuTexture::from_rgba_bytes_mipped(core::D3D11Context& context,
                                              std::uint32_t width, std::uint32_t height,
                                              const std::uint8_t* pixels) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    GpuTexture result;
    result.width = width;
    result.height = height;
    if (FAILED(context.device->CreateTexture2D(&desc, nullptr, result.texture.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D failed");
    }
    if (FAILED(context.device->CreateShaderResourceView(result.texture.Get(), nullptr,
                                                        result.srv.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView failed");
    }
    context.context->UpdateSubresource(result.texture.Get(), 0, nullptr, pixels, width * 4, 0);
    context.context->GenerateMips(result.srv.Get());
    return result;
}

GpuTexture GpuTexture::from_memory(core::D3D11Context& context, const std::uint8_t* data,
                                   std::size_t size) {
    Image image;
    if (size >= 4 && std::memcmp(data, "DDS ", 4) == 0) {
        std::vector<std::uint8_t> bytes(data, data + size);
        if (!load_dds(bytes, image)) {
            throw std::runtime_error("unsupported DDS format");
        }
    } else {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height,
                                                &channels, 4);
        if (pixels == nullptr) {
            throw std::runtime_error("image decode failed");
        }
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);
    }
    return from_rgba_bytes(context, image.width, image.height, image.rgba.data());
}

GpuTexture GpuTexture::from_file(core::D3D11Context& context, const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open texture file");
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return from_memory(context, bytes.data(), bytes.size());
}

GpuTexture GpuTexture::create_solid_color(core::D3D11Context& context,
                                          const std::uint8_t color[4]) {
    return from_rgba_bytes(context, 1, 1, color);
}

} // namespace hikari::render
