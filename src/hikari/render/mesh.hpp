#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"

namespace hikari::render {

struct ModelVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
    std::array<std::uint32_t, 4> bone_indices{};
    std::array<float, 4> bone_weights{1.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshSubset {
    std::size_t material_index = 0;
    std::uint32_t index_start = 0;
    std::uint32_t index_count = 0;
};

class GpuMesh {
public:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::vector<MeshSubset> subsets;
    std::vector<ModelVertex> base_vertices;
    std::vector<ModelVertex> current_vertices;

    static GpuMesh from_pmx(core::D3D11Context& context, const formats::PmxModel& pmx);
    void apply_morph_offsets(core::D3D11Context& context, const std::vector<Vec3>& position_offsets,
                             const std::vector<std::array<float, 2>>& uv_offsets);
    void bind_to(ID3D11DeviceContext* context) const;
    void bind(core::D3D11Context& context) const;

private:
    void upload_vertices(core::D3D11Context& context);
    bool morph_applied_ = false;
};

} // namespace hikari::render
