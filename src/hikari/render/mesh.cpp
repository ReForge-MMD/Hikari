#include "hikari/render/mesh.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace hikari::render {
namespace {

std::pair<std::array<std::uint32_t, 4>, std::array<float, 4>> vertex_weights(
    const formats::PmxWeight& weight) {
    switch (weight.type) {
    case formats::PmxWeightType::Bdef1:
        return {{static_cast<std::uint32_t>(std::max(weight.bdef1.bone, 0)), 0, 0, 0},
                {1.0f, 0.0f, 0.0f, 0.0f}};
    case formats::PmxWeightType::Bdef2:
        return {{static_cast<std::uint32_t>(std::max(weight.bdef2.bone1, 0)),
                 static_cast<std::uint32_t>(std::max(weight.bdef2.bone2, 0)), 0, 0},
                {weight.bdef2.weight1, 1.0f - weight.bdef2.weight1, 0.0f, 0.0f}};
    case formats::PmxWeightType::Bdef4:
        return {{static_cast<std::uint32_t>(std::max(weight.bdef4.bones[0], 0)),
                 static_cast<std::uint32_t>(std::max(weight.bdef4.bones[1], 0)),
                 static_cast<std::uint32_t>(std::max(weight.bdef4.bones[2], 0)),
                 static_cast<std::uint32_t>(std::max(weight.bdef4.bones[3], 0))},
                weight.bdef4.weights};
    case formats::PmxWeightType::Sdef:
        return {{static_cast<std::uint32_t>(std::max(weight.sdef.bone1, 0)),
                 static_cast<std::uint32_t>(std::max(weight.sdef.bone2, 0)), 0, 0},
                {weight.sdef.weight1, 1.0f - weight.sdef.weight1, 0.0f, 0.0f}};
    case formats::PmxWeightType::Qdef:
        return {{static_cast<std::uint32_t>(std::max(weight.qdef.bones[0], 0)),
                 static_cast<std::uint32_t>(std::max(weight.qdef.bones[1], 0)),
                 static_cast<std::uint32_t>(std::max(weight.qdef.bones[2], 0)),
                 static_cast<std::uint32_t>(std::max(weight.qdef.bones[3], 0))},
                weight.qdef.weights};
    }
    return {};
}

} // namespace

GpuMesh GpuMesh::from_pmx(core::D3D11Context& context, const formats::PmxModel& pmx) {
    if (pmx.vertices.empty() || pmx.indices.empty()) {
        throw std::invalid_argument("PMX contains no renderable geometry");
    }

    GpuMesh mesh;
    mesh.base_vertices.reserve(pmx.vertices.size());
    for (const auto& source : pmx.vertices) {
        ModelVertex vertex;
        vertex.position = source.position;
        vertex.normal = source.normal;
        vertex.uv = source.uv;
        std::tie(vertex.bone_indices, vertex.bone_weights) = vertex_weights(source.weight);
        mesh.base_vertices.push_back(vertex);
    }
    mesh.current_vertices = mesh.base_vertices;
    mesh.vertex_count = static_cast<std::uint32_t>(mesh.base_vertices.size());
    mesh.index_count = static_cast<std::uint32_t>(pmx.indices.size());

    D3D11_BUFFER_DESC vb_desc{};
    vb_desc.ByteWidth = static_cast<UINT>(mesh.base_vertices.size() * sizeof(ModelVertex));
    vb_desc.Usage = D3D11_USAGE_DYNAMIC;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA vb_data{};
    vb_data.pSysMem = mesh.base_vertices.data();
    if (FAILED(context.device->CreateBuffer(&vb_desc, &vb_data,
                                            mesh.vertex_buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer vertex failed");
    }

    D3D11_BUFFER_DESC ib_desc{};
    ib_desc.ByteWidth = static_cast<UINT>(pmx.indices.size() * sizeof(std::uint32_t));
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data{};
    ib_data.pSysMem = pmx.indices.data();
    if (FAILED(context.device->CreateBuffer(&ib_desc, &ib_data,
                                            mesh.index_buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer index failed");
    }

    std::uint32_t current_start = 0;
    for (std::size_t i = 0; i < pmx.materials.size(); ++i) {
        const auto remaining = static_cast<std::int64_t>(pmx.indices.size()) -
                               static_cast<std::int64_t>(current_start);
        const auto count = static_cast<std::uint32_t>(
            std::min<std::int64_t>(std::max<std::int64_t>(pmx.materials[i].surface_count, 0),
                                   std::max<std::int64_t>(remaining, 0)));
        mesh.subsets.push_back({i, current_start, count});
        current_start += count;
    }
    return mesh;
}

void GpuMesh::upload_vertices(core::D3D11Context& context) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map vertex buffer failed");
    }
    std::memcpy(mapped.pData, current_vertices.data(),
                current_vertices.size() * sizeof(ModelVertex));
    context.context->Unmap(vertex_buffer.Get(), 0);
}

void GpuMesh::apply_morph_offsets(core::D3D11Context& context,
                                  const std::vector<Vec3>& position_offsets,
                                  const std::vector<std::array<float, 2>>& uv_offsets) {
    if (position_offsets.empty() && uv_offsets.empty()) {
        if (morph_applied_) {
            for (std::size_t i = 0; i < current_vertices.size(); ++i) {
                current_vertices[i].position = base_vertices[i].position;
                current_vertices[i].uv = base_vertices[i].uv;
            }
            upload_vertices(context);
            morph_applied_ = false;
        }
        return;
    }

    bool changed = false;
    const std::size_t count = base_vertices.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& base = base_vertices[i];
        const Vec3 position_offset =
            i < position_offsets.size() ? position_offsets[i] : Vec3::zero();
        const std::array<float, 2> uv_offset =
            i < uv_offsets.size() ? uv_offsets[i] : std::array<float, 2>{0.0f, 0.0f};
        const bool has_position = position_offset.x != 0.0f || position_offset.y != 0.0f ||
                                  position_offset.z != 0.0f;
        const bool has_uv = uv_offset[0] != 0.0f || uv_offset[1] != 0.0f;
        if (has_position || has_uv) {
            changed = true;
            current_vertices[i].position = {base.position[0] + position_offset.x,
                                            base.position[1] + position_offset.y,
                                            base.position[2] + position_offset.z};
            current_vertices[i].uv = {base.uv[0] + uv_offset[0], base.uv[1] + uv_offset[1]};
        } else if (current_vertices[i].position != base.position ||
                   current_vertices[i].uv != base.uv) {
            changed = true;
            current_vertices[i].position = base.position;
            current_vertices[i].uv = base.uv;
        }
    }
    if (changed) {
        upload_vertices(context);
        morph_applied_ = true;
    }
}

void GpuMesh::bind_to(ID3D11DeviceContext* context) const {
    const UINT stride = sizeof(ModelVertex);
    const UINT offset = 0;
    ID3D11Buffer* buffers[] = {vertex_buffer.Get()};
    context->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void GpuMesh::bind(core::D3D11Context& context) const {
    bind_to(context.context.Get());
}

} // namespace hikari::render
