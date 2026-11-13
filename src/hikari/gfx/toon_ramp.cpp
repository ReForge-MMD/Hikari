#include "hikari/gfx/toon_ramp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace hikari::gfx {
namespace {

struct RampSpec {
    std::array<float, 3> shadow;
    std::array<float, 3> lit;
    // Terminator position along the row and the width of the transition. A PMX
    // toon ramp is a stepped gradient, not a cosine, so the band edge is a
    // smoothstep of that width around that position.
    float threshold;
    float edge;
};

// The ten built-in PMX ramps: a shadow tint, the lit colour, and where the band
// edge sits. Skin and fabric ramps keep a warm shadow and a low threshold, the
// neutral ramps only vary in contrast and edge hardness.
constexpr std::array<RampSpec, kBuiltinToonRampCount> kBuiltinRamps = {{
    {{{0.55f, 0.36f, 0.30f}}, {{1.00f, 0.93f, 0.89f}}, 0.30f, 0.10f},
    {{{0.35f, 0.40f, 0.55f}}, {{0.95f, 0.97f, 1.00f}}, 0.20f, 0.06f},
    {{{0.30f, 0.42f, 0.30f}}, {{0.95f, 1.00f, 0.92f}}, 0.25f, 0.08f},
    {{{0.50f, 0.30f, 0.28f}}, {{1.00f, 0.95f, 0.90f}}, 0.35f, 0.12f},
    {{{0.35f, 0.30f, 0.48f}}, {{0.97f, 0.94f, 1.00f}}, 0.25f, 0.05f},
    {{{0.45f, 0.36f, 0.25f}}, {{1.00f, 0.96f, 0.85f}}, 0.40f, 0.15f},
    {{{0.28f, 0.40f, 0.42f}}, {{0.90f, 1.00f, 1.00f}}, 0.30f, 0.07f},
    {{{0.30f, 0.30f, 0.32f}}, {{1.00f, 1.00f, 1.00f}}, 0.50f, 0.02f},
    {{{0.62f, 0.60f, 0.58f}}, {{1.00f, 1.00f, 1.00f}}, 0.55f, 0.25f},
    {{{0.16f, 0.15f, 0.17f}}, {{0.95f, 0.93f, 0.90f}}, 0.45f, 0.02f},
}};

std::uint8_t to_byte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void write_row(std::uint8_t* row, const RampSpec& spec) {
    for (std::uint32_t x = 0; x < kToonRampWidth; ++x) {
        const float t = static_cast<float>(x) / static_cast<float>(kToonRampWidth - 1);
        // smoothstep, written out so the row is the same in every compiler.
        const float lower = std::clamp((t - (spec.threshold - spec.edge)) /
                                           std::max(2.0f * spec.edge, 1.0e-4f),
                                       0.0f, 1.0f);
        const float shaped = lower * lower * (3.0f - 2.0f * lower);
        std::uint8_t* texel = row + static_cast<std::size_t>(x) * 4;
        for (int channel = 0; channel < 3; ++channel) {
            const float value =
                spec.shadow[static_cast<std::size_t>(channel)] * (1.0f - shaped) +
                spec.lit[static_cast<std::size_t>(channel)] * shaped;
            texel[channel] = to_byte(value);
        }
        texel[3] = 255;
    }
}

} // namespace

ToonRampAtlas::ToonRampAtlas(core::D3D11Context& context) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kToonRampSlices) *
                                     kToonRampWidth * 4);
    const std::size_t row_bytes = static_cast<std::size_t>(kToonRampWidth) * 4;
    // Slice 0 is the neutral row: a material without a ramp multiplies by white.
    std::fill_n(pixels.begin(), static_cast<std::ptrdiff_t>(row_bytes), std::uint8_t{255});
    for (int i = 0; i < kBuiltinToonRampCount; ++i) {
        write_row(pixels.data() + static_cast<std::size_t>(i + 1) * row_bytes,
                  kBuiltinRamps[static_cast<std::size_t>(i)]);
    }
    // The remaining slices stay neutral, so an index the table does not cover
    // degrades to "no ramp" instead of sampling uninitialised memory.
    for (int slice = kBuiltinToonRampCount + 1; slice < kToonRampSlices; ++slice) {
        std::fill_n(pixels.begin() + static_cast<std::ptrdiff_t>(slice) * row_bytes,
                    static_cast<std::ptrdiff_t>(row_bytes), std::uint8_t{255});
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kToonRampWidth;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = kToonRampSlices;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(context.device->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()))) {
        throw std::runtime_error("CreateTexture2D toon ramps failed");
    }
    if (FAILED(context.device->CreateShaderResourceView(texture_.Get(), nullptr,
                                                        srv_.GetAddressOf()))) {
        throw std::runtime_error("CreateShaderResourceView toon ramps failed");
    }
    // An array with one initial subresource is rejected, so each row is uploaded
    // into its own slice. The slice is selected by the destination subresource,
    // not by the box depth.
    const D3D11_BOX box{0, 0, 0, kToonRampWidth, 1, 1};
    for (int slice = 0; slice < kToonRampSlices; ++slice) {
        context.context->UpdateSubresource(
            texture_.Get(), static_cast<UINT>(slice), &box,
            pixels.data() + static_cast<std::size_t>(slice) * row_bytes,
            static_cast<UINT>(row_bytes), 0);
    }
}

} // namespace hikari::gfx
