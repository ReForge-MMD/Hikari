#pragma once

#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/gfx/material.hpp"

namespace hikari::gfx {

// One ramp row is indexed by the terminator: x is NoL, so the first texel is the
// shadow band and the last one is fully lit.
inline constexpr std::uint32_t kToonRampWidth = 256;
// Slice 0 is the neutral row and slices 1..10 are the ten built-in PMX ramps.
// Every remaining slice is neutral too, so a PMX material that points at an
// external toon texture (which maps to slice 0) or an index the table does not
// cover degrades to "no ramp" instead of sampling an uninitialised row.
inline constexpr int kToonRampSlices = 64;

// Texture array of toon ramps sampled by the lighting pass. The built-in ramps
// are authored here instead of shipped as PMX assets: the reference ramps are
// simple shadow-to-lit gradients, and a native table keeps the pipeline free of
// copied texture data. A PMX material that points at an external toon texture
// keeps its model's procedural terminator, because that image is the model
// author's own data and never a native asset.
class ToonRampAtlas {
public:
    explicit ToonRampAtlas(core::D3D11Context& context);

    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
};

} // namespace hikari::gfx
