#include "capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include <windows.h>

namespace hikari::gfx_lab {
namespace {

std::wstring to_wide(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return std::wstring(path.begin(), path.end());
    }
    std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, result.data(), size);
    return result;
}

} // namespace

CaptureData read_target(hikari::core::D3D11Context& context, ID3D11Texture2D* texture,
                        std::uint32_t width, std::uint32_t height) {
    CaptureData result;
    result.width = width;
    result.height = height;
    result.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    if (texture == nullptr) {
        return result;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(context.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) {
        return result;
    }
    context.context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return result;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(result.rgba.data() + static_cast<std::size_t>(y) * width * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4);
    }
    context.context->Unmap(staging.Get(), 0);
    return result;
}

bool write_capture(const std::string& path, const CaptureData& capture, std::string* error) {
    try {
        hikari::core::save_bmp(to_wide(path), capture.width, capture.height, capture.rgba);
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

std::optional<CaptureData> read_capture(const std::string& path, std::string* error) {
    std::ifstream file(to_wide(path), std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open capture: " + path;
        }
        return std::nullopt;
    }
    BITMAPFILEHEADER file_header{};
    BITMAPINFOHEADER info_header{};
    file.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    file.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    if (!file || file_header.bfType != 0x4D42 || info_header.biBitCount != 32 ||
        info_header.biCompression != BI_RGB || info_header.biWidth <= 0 ||
        info_header.biHeight == 0) {
        if (error != nullptr) {
            *error = "unsupported BMP capture: " + path;
        }
        return std::nullopt;
    }
    const bool top_down = info_header.biHeight < 0;
    const std::uint32_t width = static_cast<std::uint32_t>(info_header.biWidth);
    const std::uint32_t height = static_cast<std::uint32_t>(
        top_down ? -info_header.biHeight : info_header.biHeight);
    CaptureData result;
    result.width = width;
    result.height = height;
    result.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    file.seekg(static_cast<std::streamoff>(file_header.bfOffBits), std::ios::beg);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::uint32_t target_row = top_down ? row : (height - 1 - row);
        std::uint8_t* destination =
            result.rgba.data() + static_cast<std::size_t>(target_row) * width * 4;
        file.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(width) * 4);
        if (!file) {
            if (error != nullptr) {
                *error = "truncated BMP capture: " + path;
            }
            return std::nullopt;
        }
        for (std::uint32_t x = 0; x < width; ++x) {
            std::swap(destination[x * 4 + 0], destination[x * 4 + 2]);
        }
    }
    return result;
}

bool write_probe(const std::string& path, const CaptureData& capture,
                 const hikari::gfx::GraphicsSettings& settings, const std::string& scene,
                 const std::string& phase, double frame_ms, std::string* error) {
    float minimum[3] = {1.0f, 1.0f, 1.0f};
    float maximum[3] = {0.0f, 0.0f, 0.0f};
    double sum[3] = {0.0, 0.0, 0.0};
    std::size_t non_finite = 0;
    const std::size_t pixels = static_cast<std::size_t>(capture.width) * capture.height;
    for (std::size_t i = 0; i + 3 < capture.rgba.size(); i += 4) {
        for (int channel = 0; channel < 3; ++channel) {
            const float value = static_cast<float>(capture.rgba[i + channel]) / 255.0f;
            if (!std::isfinite(value)) {
                ++non_finite;
                continue;
            }
            minimum[channel] = std::min(minimum[channel], value);
            maximum[channel] = std::max(maximum[channel], value);
            sum[channel] += value;
        }
    }
    std::ofstream file(to_wide(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot write probe: " + path;
        }
        return false;
    }
    const double denominator = pixels > 0 ? static_cast<double>(pixels) : 1.0;
    file << "{\n";
    file << "  \"phase\": \"" << phase << "\",\n";
    file << "  \"scene\": \"" << scene << "\",\n";
    file << "  \"width\": " << capture.width << ",\n";
    file << "  \"height\": " << capture.height << ",\n";
    file << "  \"frame_ms\": " << frame_ms << ",\n";
    file << "  \"settings_hash\": " << settings.permutation_key() << ",\n";
    file << "  \"non_finite\": " << non_finite << ",\n";
    file << "  \"mean\": [" << sum[0] / denominator << ", " << sum[1] / denominator << ", "
         << sum[2] / denominator << "],\n";
    file << "  \"min\": [" << minimum[0] << ", " << minimum[1] << ", " << minimum[2]
         << "],\n";
    file << "  \"max\": [" << maximum[0] << ", " << maximum[1] << ", " << maximum[2]
         << "]\n";
    file << "}\n";
    if (!file) {
        if (error != nullptr) {
            *error = "failed while writing probe: " + path;
        }
        return false;
    }
    return true;
}

float mean_channel_difference(const CaptureData& a, const CaptureData& b) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) {
        return 1.0f;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (int channel = 0; channel < 3; ++channel) {
            sum += std::abs(static_cast<int>(a.rgba[i + channel]) -
                            static_cast<int>(b.rgba[i + channel]));
        }
    }
    const double count = static_cast<double>(a.rgba.size() / 4) * 3.0;
    return count > 0.0 ? static_cast<float>(sum / count / 255.0) : 0.0f;
}

} // namespace hikari::gfx_lab
