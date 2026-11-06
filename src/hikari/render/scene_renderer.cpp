#include "hikari/render/scene_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "hikari/core/shader.hpp"

namespace hikari::render {
namespace {

const char* kMeshHLSL = R"(
cbuffer CameraParams : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ViewProjection;
    float4 CameraPos;
    float4 LightColor;
    float4 LightDirection;
};
cbuffer MaterialParams : register(b1) {
    float4 Diffuse;
    float3 Specular;
    float Shininess;
    float3 Ambient;
    float EdgeSize;
    float4 EdgeColor;
};
StructuredBuffer<float4> BoneRows : register(t0);
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    uint4 bone_indices : BLENDINDICES;
    float4 bone_weights : BLENDWEIGHT;
};
struct VS_OUTPUT {
    float4 pos_clip : SV_POSITION;
    float3 normal_world : NORMAL;
    float2 uv : TEXCOORD;
    float3 pos_world : TEXCOORD1;
};
float4x4 load_bone(uint index) {
    const uint base = index * 4u;
    return float4x4(BoneRows[base], BoneRows[base + 1u], BoneRows[base + 2u],
                    BoneRows[base + 3u]);
}
VS_OUTPUT VS_Main(VS_INPUT input) {
    VS_OUTPUT output;
    float total_w = input.bone_weights.x + input.bone_weights.y + input.bone_weights.z + input.bone_weights.w;
    float4 weights = (total_w > 0.0001f) ? (input.bone_weights / total_w) : float4(1.0f, 0.0f, 0.0f, 0.0f);
    float4x4 skin_matrix = load_bone(input.bone_indices.x) * weights.x
                        + load_bone(input.bone_indices.y) * weights.y
                        + load_bone(input.bone_indices.z) * weights.z
                        + load_bone(input.bone_indices.w) * weights.w;
    float4 pos_world = mul(float4(input.position, 1.0f), skin_matrix);
    float3 norm_world = normalize(mul(float4(input.normal, 0.0f), skin_matrix).xyz);
    output.pos_world = pos_world.xyz;
    output.normal_world = norm_world;
    output.uv = input.uv;
    output.pos_clip = mul(pos_world, ViewProjection);
    return output;
}
Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);
float4 PS_Main(VS_OUTPUT input) : SV_Target {
    float3 light_dir = normalize(-LightDirection.xyz);
    float n_dot_l = dot(normalize(input.normal_world), light_dir);
    float half_lambert = n_dot_l * 0.5f + 0.5f;
    float toon = smoothstep(0.32f, 0.48f, half_lambert) * 0.4f + 0.6f;
    float4 tex_color = DiffuseMap.Sample(LinearSampler, input.uv);
    float alpha = tex_color.a * Diffuse.a;
    if (alpha < 0.05f) {
        discard;
    }
    float3 view_dir = normalize(CameraPos.xyz - input.pos_world);
    float rim = 1.0f - max(dot(normalize(input.normal_world), view_dir), 0.0f);
    float rim_term = pow(rim, 3.5f) * 0.2f;
    float3 base_color = Diffuse.rgb * tex_color.rgb;
    if (Diffuse.r + Diffuse.g + Diffuse.b < 0.01f) {
        base_color = tex_color.rgb;
    }
    float3 light_tint = saturate(LightColor.rgb * 2.0f);
    float3 lit_color = base_color * toon * light_tint + float3(rim_term, rim_term, rim_term);
    return float4(saturate(lit_color), alpha);
}
)";

const char* kGridHLSL = R"(
cbuffer CameraParams : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ViewProjection;
    float4 CameraPos;
};
struct GridIn { float3 pos : POSITION; float4 col : COLOR; };
struct GridOut { float4 pos_clip : SV_POSITION; float4 col : COLOR; };
GridOut Grid_VS(GridIn input) {
    GridOut output;
    output.pos_clip = mul(float4(input.pos, 1.0f), ViewProjection);
    output.col = input.col;
    return output;
}
float4 Grid_PS(GridOut input) : SV_Target { return input.col; }
)";

const char* kBackgroundHLSL = R"(
cbuffer BgParams : register(b0) { float4 BgColor; };
struct VS_Out { float4 pos : SV_POSITION; };
VS_Out Bg_VS(uint id : SV_VertexID) {
    VS_Out o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * 2.0f - 1.0f, 0.9999f, 1.0f);
    return o;
}
float4 Bg_PS(VS_Out input) : SV_Target { return BgColor; }
)";

template <typename T>
Microsoft::WRL::ComPtr<T> make_shader(ID3D11Device* device,
                                      const std::vector<std::uint8_t>& bytecode,
                                      HRESULT (STDMETHODCALLTYPE ID3D11Device::*create)(
                                          const void*, SIZE_T, ID3D11ClassLinkage*, T**)) {
    Microsoft::WRL::ComPtr<T> shader;
    if (FAILED((device->*create)(bytecode.data(), bytecode.size(), nullptr,
                                 shader.GetAddressOf()))) {
        throw std::runtime_error("failed to create a render shader");
    }
    return shader;
}

} // namespace

SceneRenderer::SceneRenderer(core::D3D11Context& context)
    : default_white_texture([&] {
          const std::uint8_t white[4] = {255, 255, 255, 255};
          return GpuTexture::create_solid_color(context, white);
      }()) {
    D3D11_BUFFER_DESC camera_desc{};
    camera_desc.ByteWidth = sizeof(CameraConstants);
    camera_desc.Usage = D3D11_USAGE_DYNAMIC;
    camera_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    camera_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context.device->CreateBuffer(&camera_desc, nullptr,
                                            camera_buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer camera failed");
    }

    const auto vs_bytecode = core::compile_hlsl(kMeshHLSL, "VS_Main", "vs_5_0");
    const auto ps_bytecode = core::compile_hlsl(kMeshHLSL, "PS_Main", "ps_5_0");
    default_vs = make_shader<ID3D11VertexShader>(context.device.Get(), vs_bytecode,
                                                 &ID3D11Device::CreateVertexShader);
    default_ps = make_shader<ID3D11PixelShader>(context.device.Get(), ps_bytecode,
                                                &ID3D11Device::CreatePixelShader);

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(context.device->CreateInputLayout(layout, 5, vs_bytecode.data(), vs_bytecode.size(),
                                                 input_layout.GetAddressOf()))) {
        throw std::runtime_error("CreateInputLayout mesh failed");
    }

    std::vector<GridVertex> grid_vertices;
    const float extent = 50.0f;
    const float step = 2.0f;
    for (float z = -extent; z <= extent + 1.0e-4f; z += step) {
        std::array<float, 4> color{0.26f, 0.30f, 0.37f, 0.7f};
        if (std::abs(z) < 1.0e-4f) {
            color = {0.75f, 0.38f, 0.42f, 1.0f};
        } else if (std::abs(std::fmod(z, 10.0f)) < 1.0e-3f) {
            color = {0.38f, 0.44f, 0.52f, 1.0f};
        }
        grid_vertices.push_back({{-extent, 0.0f, z}, color});
        grid_vertices.push_back({{extent, 0.0f, z}, color});
    }
    for (float x = -extent; x <= extent + 1.0e-4f; x += step) {
        std::array<float, 4> color{0.26f, 0.30f, 0.37f, 0.7f};
        if (std::abs(x) < 1.0e-4f) {
            color = {0.51f, 0.63f, 0.76f, 1.0f};
        } else if (std::abs(std::fmod(x, 10.0f)) < 1.0e-3f) {
            color = {0.38f, 0.44f, 0.52f, 1.0f};
        }
        grid_vertices.push_back({{x, 0.0f, -extent}, color});
        grid_vertices.push_back({{x, 0.0f, extent}, color});
    }
    grid_vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.64f, 0.75f, 0.55f, 1.0f}});
    grid_vertices.push_back({{0.0f, 15.0f, 0.0f}, {0.64f, 0.75f, 0.55f, 1.0f}});
    grid_vertex_count = static_cast<std::uint32_t>(grid_vertices.size());

    D3D11_BUFFER_DESC grid_desc{};
    grid_desc.ByteWidth = static_cast<UINT>(grid_vertices.size() * sizeof(GridVertex));
    grid_desc.Usage = D3D11_USAGE_IMMUTABLE;
    grid_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA grid_data{};
    grid_data.pSysMem = grid_vertices.data();
    if (FAILED(context.device->CreateBuffer(&grid_desc, &grid_data, grid_vb.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer grid failed");
    }

    const auto grid_vs_bc = core::compile_hlsl(kGridHLSL, "Grid_VS", "vs_5_0");
    const auto grid_ps_bc = core::compile_hlsl(kGridHLSL, "Grid_PS", "ps_5_0");
    grid_vs = make_shader<ID3D11VertexShader>(context.device.Get(), grid_vs_bc,
                                              &ID3D11Device::CreateVertexShader);
    grid_ps = make_shader<ID3D11PixelShader>(context.device.Get(), grid_ps_bc,
                                             &ID3D11Device::CreatePixelShader);
    const D3D11_INPUT_ELEMENT_DESC grid_layout_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(context.device->CreateInputLayout(grid_layout_desc, 2, grid_vs_bc.data(),
                                                 grid_vs_bc.size(), grid_layout.GetAddressOf()))) {
        throw std::runtime_error("CreateInputLayout grid failed");
    }

    const auto bg_vs_bc = core::compile_hlsl(kBackgroundHLSL, "Bg_VS", "vs_5_0");
    const auto bg_ps_bc = core::compile_hlsl(kBackgroundHLSL, "Bg_PS", "ps_5_0");
    bg_vs = make_shader<ID3D11VertexShader>(context.device.Get(), bg_vs_bc,
                                            &ID3D11Device::CreateVertexShader);
    bg_ps = make_shader<ID3D11PixelShader>(context.device.Get(), bg_ps_bc,
                                           &ID3D11Device::CreatePixelShader);

    D3D11_BUFFER_DESC bg_desc{};
    bg_desc.ByteWidth = 16;
    bg_desc.Usage = D3D11_USAGE_DYNAMIC;
    bg_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bg_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context.device->CreateBuffer(&bg_desc, nullptr, bg_buffer.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer background failed");
    }
}

void SceneRenderer::update_camera_on(ID3D11DeviceContext* context, const Camera& camera) const {
    const Mat4 view = camera.view_matrix();
    const Mat4 projection = camera.projection_matrix();
    const Mat4 view_projection = camera.view_projection_matrix();
    CameraConstants constants;
    constants.view = view.m;
    constants.projection = projection.m;
    constants.view_projection = view_projection.m;
    constants.camera_pos = {camera.position.x, camera.position.y, camera.position.z, 1.0f};
    constants.light_color = light_color;
    constants.light_direction = light_direction;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(camera_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map camera buffer failed");
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(camera_buffer.Get(), 0);
}

void SceneRenderer::update_camera(core::D3D11Context& context, const Camera& camera) const {
    update_camera_on(context.context.Get(), camera);
}

void SceneRenderer::set_light(const std::array<float, 3>& color,
                              const std::array<float, 3>& direction) {
    light_color = {color[0], color[1], color[2], 1.0f};
    light_direction = {direction[0], direction[1], direction[2], 0.0f};
}

void SceneRenderer::upload_overlay_vertices(core::D3D11Context& context,
                                            Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
                                            std::size_t& capacity,
                                            const std::vector<OverlayVertex>& vertices) {
    if (!buffer || capacity < vertices.size()) {
        std::size_t new_capacity = 4096;
        while (new_capacity < vertices.size()) {
            new_capacity *= 2;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(new_capacity * sizeof(OverlayVertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(context.device->CreateBuffer(&desc, nullptr,
                                                buffer.ReleaseAndGetAddressOf()))) {
            throw std::runtime_error("CreateBuffer overlay failed");
        }
        capacity = new_capacity;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map overlay buffer failed");
    }
    std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(OverlayVertex));
    context.context->Unmap(buffer.Get(), 0);
}

Microsoft::WRL::ComPtr<ID3D11CommandList> SceneRenderer::record_ground_shadows(
    core::D3D11Context& context, const std::vector<GroundShadow>& shadows) {
    if (shadows.empty()) {
        return {};
    }
    std::vector<OverlayVertex> vertices;
    constexpr int kSegments = 48;
    for (const auto& shadow : shadows) {
        const auto ring = [&](float scale, float alpha) {
            const std::array<float, 4> ring_color{shadow.color[0], shadow.color[1],
                                                  shadow.color[2],
                                                  shadow.color[3] * alpha};
            const Vec3 origin(shadow.center.x, 0.02f, shadow.center.z);
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = static_cast<float>(i) * 2.0f * 3.14159265358979323846f /
                                 static_cast<float>(kSegments);
                const float a1 = static_cast<float>(i + 1) * 2.0f * 3.14159265358979323846f /
                                 static_cast<float>(kSegments);
                vertices.push_back({{origin.x, origin.y, origin.z}, ring_color});
                vertices.push_back(
                    {{origin.x + std::cos(a0) * shadow.radius * scale, origin.y,
                      origin.z + std::sin(a0) * shadow.radius * scale},
                     ring_color});
                vertices.push_back(
                    {{origin.x + std::cos(a1) * shadow.radius * scale, origin.y,
                      origin.z + std::sin(a1) * shadow.radius * scale},
                     ring_color});
            }
        };
        ring(1.0f, 0.35f);
        ring(0.72f, 0.55f);
        ring(0.45f, 0.8f);
    }
    if (vertices.empty()) {
        return {};
    }

    upload_overlay_vertices(context, shadow_vb, shadow_capacity, vertices);

    auto deferred = context.acquire_deferred_context();
    auto* target = deferred.Get();
    context.bind_render_targets_to(target);
    context.set_viewport_on(target, static_cast<float>(context.render_width),
                            static_cast<float>(context.render_height));
    target->IASetInputLayout(grid_layout.Get());
    target->VSSetShader(grid_vs.Get(), nullptr, 0);
    target->PSSetShader(grid_ps.Get(), nullptr, 0);
    ID3D11Buffer* camera_buffers[] = {camera_buffer.Get()};
    target->VSSetConstantBuffers(0, 1, camera_buffers);
    const UINT stride = sizeof(OverlayVertex);
    const UINT offset = 0;
    ID3D11Buffer* buffers[] = {shadow_vb.Get()};
    target->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    target->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    target->OMSetDepthStencilState(
        context.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    target->OMSetBlendState(context.state_manager->get_blend(core::BlendPreset::AlphaBlend),
                            blend_factor, 0xFFFFFFFF);
    target->RSSetState(
        context.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    target->Draw(static_cast<UINT>(vertices.size()), 0);
    auto list = core::D3D11Context::finish_command_list(target, false);
    context.recycle_deferred_context(std::move(deferred));
    return list;
}

Microsoft::WRL::ComPtr<ID3D11CommandList> SceneRenderer::record_background(
    core::D3D11Context& context, const std::array<float, 4>& color) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.context->Map(bg_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        throw std::runtime_error("Map background buffer failed");
    }
    std::memcpy(mapped.pData, color.data(), 16);
    context.context->Unmap(bg_buffer.Get(), 0);

    auto deferred = context.acquire_deferred_context();
    auto* target = deferred.Get();
    context.bind_render_targets_to(target);
    context.set_viewport_on(target, static_cast<float>(context.render_width),
                            static_cast<float>(context.render_height));
    target->IASetInputLayout(nullptr);
    target->VSSetShader(bg_vs.Get(), nullptr, 0);
    target->PSSetShader(bg_ps.Get(), nullptr, 0);
    ID3D11Buffer* buffers[] = {bg_buffer.Get()};
    target->PSSetConstantBuffers(0, 1, buffers);
    target->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    target->OMSetDepthStencilState(
        context.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    target->OMSetBlendState(context.state_manager->get_blend(core::BlendPreset::Opaque),
                            blend_factor, 0xFFFFFFFF);
    target->RSSetState(
        context.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    target->Draw(3, 0);
    auto list = core::D3D11Context::finish_command_list(target, false);
    context.recycle_deferred_context(std::move(deferred));
    return list;
}

Microsoft::WRL::ComPtr<ID3D11CommandList> SceneRenderer::record_grid(
    core::D3D11Context& context) {
    auto deferred = context.acquire_deferred_context();
    auto* target = deferred.Get();
    context.bind_render_targets_to(target);
    context.set_viewport_on(target, static_cast<float>(context.render_width),
                            static_cast<float>(context.render_height));
    target->RSSetState(
        context.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    target->RSSetScissorRects(0, nullptr);
    target->IASetInputLayout(grid_layout.Get());
    target->VSSetShader(grid_vs.Get(), nullptr, 0);
    target->PSSetShader(grid_ps.Get(), nullptr, 0);
    ID3D11Buffer* camera_buffers[] = {camera_buffer.Get()};
    target->VSSetConstantBuffers(0, 1, camera_buffers);
    const UINT stride = sizeof(GridVertex);
    const UINT offset = 0;
    ID3D11Buffer* buffers[] = {grid_vb.Get()};
    target->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    target->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    target->OMSetDepthStencilState(
        context.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    target->OMSetBlendState(context.state_manager->get_blend(core::BlendPreset::Opaque),
                            blend_factor, 0xFFFFFFFF);
    target->Draw(grid_vertex_count, 0);
    auto list = core::D3D11Context::finish_command_list(target, false);
    context.recycle_deferred_context(std::move(deferred));
    return list;
}

void SceneRenderer::render_mesh_draws_to(core::D3D11Context& context,
                                         ID3D11DeviceContext* target, const SubsetDraw* draws,
                                         std::size_t count, bool transparent) {
    if (count == 0) {
        return;
    }
    context.bind_render_targets_to(target);
    context.set_viewport_on(target, static_cast<float>(context.render_width),
                            static_cast<float>(context.render_height));
    target->IASetInputLayout(input_layout.Get());
    target->VSSetShader(default_vs.Get(), nullptr, 0);
    target->PSSetShader(default_ps.Get(), nullptr, 0);
    ID3D11Buffer* camera_buffers[] = {camera_buffer.Get()};
    target->VSSetConstantBuffers(0, 1, camera_buffers);
    target->PSSetConstantBuffers(0, 1, camera_buffers);
    auto* sampler = context.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    target->PSSetSamplers(0, 1, &sampler);
    target->RSSetState(context.state_manager->get_rasterizer(
        wireframe ? core::RasterizerPreset::Wireframe : core::RasterizerPreset::CullNoneSolid));
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    target->OMSetBlendState(
        context.state_manager->get_blend(transparent ? core::BlendPreset::AlphaBlend
                                                     : core::BlendPreset::Opaque),
        blend_factor, 0xFFFFFFFF);
    target->OMSetDepthStencilState(
        context.state_manager->get_depth(transparent ? core::DepthPreset::ReadOnlyLessEqual
                                                     : core::DepthPreset::ReadWriteLessEqual),
        1);

    const GpuMesh* bound_mesh = nullptr;
    const GpuSkinningBuffer* bound_skinning = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const SubsetDraw& draw = draws[i];
        if (draw.mesh == nullptr || draw.materials == nullptr || draw.textures == nullptr ||
            draw.skinning == nullptr || draw.subset >= draw.mesh->subsets.size()) {
            continue;
        }
        if (draw.mesh != bound_mesh) {
            draw.mesh->bind_to(target);
            bound_mesh = draw.mesh;
        }
        if (draw.skinning != bound_skinning) {
            draw.skinning->bind_vs_to(target, 0);
            bound_skinning = draw.skinning;
        }

        const MeshSubset& subset = draw.mesh->subsets[draw.subset];
        ID3D11ShaderResourceView* srv = default_white_texture.srv.Get();
        if (subset.material_index < draw.materials->size()) {
            const GpuMaterial& material = (*draw.materials)[subset.material_index];
            material.bind_to(target, 1);
            if (material.texture_index >= 0 &&
                static_cast<std::size_t>(material.texture_index) < draw.textures->size() &&
                (*draw.textures)[static_cast<std::size_t>(material.texture_index)]) {
                srv = (*draw.textures)[static_cast<std::size_t>(material.texture_index)]->srv.Get();
            }
        }
        target->PSSetShaderResources(0, 1, &srv);
        target->DrawIndexed(subset.index_count, subset.index_start, 0);
    }
}

Microsoft::WRL::ComPtr<ID3D11CommandList> SceneRenderer::record_mesh_draws(
    core::D3D11Context& context, ID3D11DeviceContext* deferred, const SubsetDraw* draws,
    std::size_t count, bool transparent) {
    render_mesh_draws_to(context, deferred, draws, count, transparent);
    return core::D3D11Context::finish_command_list(deferred, false);
}

std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>>
SceneRenderer::record_mesh_draws_parallel(core::D3D11Context& context,
                                          const std::vector<SubsetDraw>& draws,
                                          bool transparent, std::size_t max_threads) {
    if (draws.empty()) {
        return {};
    }
    constexpr std::size_t kMaxRecordingThreads = 4;
    std::size_t requested = max_threads;
    if (requested == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        requested = hardware == 0 ? kMaxRecordingThreads : static_cast<std::size_t>(hardware);
    }
    requested = std::min(requested, kMaxRecordingThreads);
    // Transparent subsets must stay in a single list to preserve the sorted order.
    const std::size_t threads =
        transparent ? 1 : std::clamp<std::size_t>(requested, 1, draws.size());
    if (threads <= 1) {
        auto deferred = context.acquire_deferred_context();
        auto list =
            record_mesh_draws(context, deferred.Get(), draws.data(), draws.size(), transparent);
        context.recycle_deferred_context(std::move(deferred));
        return {std::move(list)};
    }

    const std::size_t chunk = (draws.size() + threads - 1) / threads;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (std::size_t i = 0; i < threads; ++i) {
        const std::size_t start = i * chunk;
        if (start < draws.size()) {
            ranges.push_back({start, std::min(start + chunk, draws.size())});
        }
    }

    std::vector<Microsoft::WRL::ComPtr<ID3D11DeviceContext>> deferred_contexts;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        deferred_contexts.push_back(context.acquire_deferred_context());
    }
    std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> results(ranges.size());
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        workers.emplace_back([&, i] {
            results[i] = record_mesh_draws(context, deferred_contexts[i].Get(),
                                           draws.data() + ranges[i].first,
                                           ranges[i].second - ranges[i].first, transparent);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (auto& deferred : deferred_contexts) {
        context.recycle_deferred_context(std::move(deferred));
    }
    return results;
}

Microsoft::WRL::ComPtr<ID3D11CommandList> SceneRenderer::record_overlay_lines(
    core::D3D11Context& context, const std::vector<OverlayVertex>& vertices) {
    if (vertices.empty()) {
        return {};
    }
    upload_overlay_vertices(context, overlay_vb, overlay_capacity, vertices);

    auto deferred = context.acquire_deferred_context();
    auto* target = deferred.Get();
    context.bind_render_targets_to(target);
    context.set_viewport_on(target, static_cast<float>(context.render_width),
                            static_cast<float>(context.render_height));
    target->IASetInputLayout(grid_layout.Get());
    target->VSSetShader(grid_vs.Get(), nullptr, 0);
    target->PSSetShader(grid_ps.Get(), nullptr, 0);
    ID3D11Buffer* camera_buffers[] = {camera_buffer.Get()};
    target->VSSetConstantBuffers(0, 1, camera_buffers);
    const UINT stride = sizeof(OverlayVertex);
    const UINT offset = 0;
    ID3D11Buffer* buffers[] = {overlay_vb.Get()};
    target->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    target->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    target->OMSetDepthStencilState(
        context.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    target->OMSetBlendState(context.state_manager->get_blend(core::BlendPreset::AlphaBlend),
                            blend_factor, 0xFFFFFFFF);
    target->RSSetState(
        context.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    target->Draw(static_cast<UINT>(vertices.size()), 0);
    auto list = core::D3D11Context::finish_command_list(target, false);
    context.recycle_deferred_context(std::move(deferred));
    return list;
}

} // namespace hikari::render
