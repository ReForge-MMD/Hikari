#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/math.hpp"

namespace hikari::render {

constexpr std::size_t kMaxBones = 512;

class GpuSkinningBuffer {
public:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;

    explicit GpuSkinningBuffer(core::D3D11Context& context);
    void update_on(ID3D11DeviceContext* context, const std::vector<Mat4>& palette) const;
    void update(core::D3D11Context& context, const std::vector<Mat4>& palette) const;
    void bind_vs_to(ID3D11DeviceContext* context, std::uint32_t slot) const;
    void bind_vs(core::D3D11Context& context, std::uint32_t slot) const;
};

} // namespace hikari::render
