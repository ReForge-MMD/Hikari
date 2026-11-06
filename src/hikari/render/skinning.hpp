#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/math.hpp"

namespace hikari::render {

class GpuSkinningBuffer {
public:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;

    GpuSkinningBuffer() = default;

    void update(core::D3D11Context& context, const std::vector<Mat4>& palette);
    void bind_vs_to(ID3D11DeviceContext* context, std::uint32_t slot) const;
    void bind_vs(core::D3D11Context& context, std::uint32_t slot) const;

private:
    std::size_t capacity_ = 0;
    std::size_t view_count_ = 0;

    void allocate(core::D3D11Context& context, std::size_t count);
    void create_view(core::D3D11Context& context, std::size_t count);
};

} // namespace hikari::render
