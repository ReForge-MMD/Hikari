#include "hikari/gfx/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace hikari::gfx {
namespace {

const char* kShaderSource = "gfx_core.hlsl";

std::array<float, 4> to_array4(const Vec3& value, float w) {
    return {value.x, value.y, value.z, w};
}

std::array<std::array<float, 4>, 4> to_array(const Mat4& matrix) {
    return matrix.m;
}

} // namespace

Vec3 color_temperature_rgb(float kelvin) {
    const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    if (temperature <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * std::log(temperature) - 161.1195681661f;
        blue = temperature <= 19.0f
                   ? 0.0f
                   : 138.5177312231f * std::log(temperature - 10.0f) - 305.0447927307f;
    } else {
        red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
        green = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
        blue = 255.0f;
    }
    red = std::clamp(red / 255.0f, 0.0f, 1.0f);
    green = std::clamp(green / 255.0f, 0.0f, 1.0f);
    blue = std::clamp(blue / 255.0f, 0.0f, 1.0f);
    return Vec3(std::pow(red, 2.2f), std::pow(green, 2.2f), std::pow(blue, 2.2f));
}

Vec3 sun_color_from_settings(const GraphicsSettings& settings, const Vec3& fallback) {
    if (settings.sun.light_mode == 0) {
        return Vec3::zero();
    }
    const Vec3 base = settings.sun.auto_cct
                          ? color_temperature_rgb(settings.sun.color_temperature)
                          : fallback;
    return base * settings.sun.intensity;
}

Vec3 ambient_color_from_settings(const GraphicsSettings& settings) {
    return settings.sky.ambient_color * settings.sky.ambient_intensity;
}

Renderer::Renderer(core::D3D11Context& context)
    : context_(context),
      shaders_(context),
      white_texture_([&context] {
          const std::uint8_t white[4] = {255, 255, 255, 255};
          return render::GpuTexture::create_solid_color(context, white);
      }()) {
    create_buffers();
    build_grid();
}

void Renderer::create_buffers() {
    D3D11_BUFFER_DESC frame_desc{};
    frame_desc.ByteWidth = sizeof(FrameConstants);
    frame_desc.Usage = D3D11_USAGE_DYNAMIC;
    frame_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    frame_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&frame_desc, nullptr,
                                             frame_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer frame constants failed");
    }

    D3D11_BUFFER_DESC material_desc{};
    material_desc.ByteWidth = sizeof(MaterialConstants);
    material_desc.Usage = D3D11_USAGE_DYNAMIC;
    material_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    material_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&material_desc, nullptr,
                                             material_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer material constants failed");
    }
}

void Renderer::build_grid() {
    std::vector<GridVertex> vertices;
    const float extent = 50.0f;
    const float step = 2.0f;
    for (float z = -extent; z <= extent + 1.0e-4f; z += step) {
        std::array<float, 4> color{0.26f, 0.30f, 0.37f, 0.7f};
        if (std::abs(z) < 1.0e-4f) {
            color = {0.75f, 0.38f, 0.42f, 1.0f};
        } else if (std::abs(std::fmod(z, 10.0f)) < 1.0e-3f) {
            color = {0.38f, 0.44f, 0.52f, 1.0f};
        }
        vertices.push_back({{-extent, 0.0f, z}, color});
        vertices.push_back({{extent, 0.0f, z}, color});
    }
    for (float x = -extent; x <= extent + 1.0e-4f; x += step) {
        std::array<float, 4> color{0.26f, 0.30f, 0.37f, 0.7f};
        if (std::abs(x) < 1.0e-4f) {
            color = {0.51f, 0.63f, 0.76f, 1.0f};
        } else if (std::abs(std::fmod(x, 10.0f)) < 1.0e-3f) {
            color = {0.38f, 0.44f, 0.52f, 1.0f};
        }
        vertices.push_back({{x, 0.0f, -extent}, color});
        vertices.push_back({{x, 0.0f, extent}, color});
    }
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.64f, 0.75f, 0.55f, 1.0f}});
    vertices.push_back({{0.0f, 15.0f, 0.0f}, {0.64f, 0.75f, 0.55f, 1.0f}});
    grid_vertex_count_ = static_cast<std::uint32_t>(vertices.size());

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GridVertex));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    if (FAILED(context_.device->CreateBuffer(&desc, &data, grid_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer grid failed");
    }
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    width = std::max(width, 16u);
    height = std::max(height, 16u);
    if (width == width_ && height == height_) {
        return;
    }
    width_ = width;
    height_ = height;
    targets_.clear();
    ensure_targets(width, height);
}

void Renderer::ensure_targets(std::uint32_t width, std::uint32_t height) {
    if (targets_.get("Hdr") != nullptr && targets_.get("GB0") != nullptr) {
        return;
    }
    targets_.clear();
    targets_.create_target(context_.device.Get(), "GB0", width, height,
                            DXGI_FORMAT_R8G8B8A8_UNORM);
    for (const char* name : {"GB1", "GB2", "GB3", "GB4", "Hdr"}) {
        targets_.create_target(context_.device.Get(), name, width, height,
                                DXGI_FORMAT_R16G16B16A16_FLOAT);
    }
    targets_.create_depth(context_.device.Get(), "Depth", width, height);
}

void Renderer::reload_shaders() {
    shaders_.reload();
}

void Renderer::render(const GraphicsSettings& settings, const FrameView& view,
                      ID3D11RenderTargetView* final_rtv) {
    if (final_rtv == nullptr) {
        throw std::runtime_error("native renderer requires a final render target");
    }
    resize(view.width, view.height);
    ensure_targets(width_, height_);
    update_frame_constants(settings, view, !view.show_tonemap);
    draw_geometry(view);
    draw_lighting(settings, view);
    draw_transparent(view);
    draw_tonemap(settings, view, final_rtv, !view.show_tonemap);
    draw_grid(view, final_rtv);

    ID3D11ShaderResourceView* null_views[8] = {};
    context_.context->PSSetShaderResources(0, 8, null_views);
    context_.context->VSSetShaderResources(0, 1, null_views);
    ID3D11ShaderResourceView* null_diffuse = nullptr;
    context_.context->PSSetShaderResources(8, 1, &null_diffuse);
    context_.context->VSSetShaderResources(8, 1, &null_diffuse);
}

void Renderer::update_frame_constants(const GraphicsSettings& settings, const FrameView& view,
                                      bool linear_tonemap) {
    (void)settings;
    (void)linear_tonemap;
    FrameConstants constants;
    constants.view = to_array(view.view);
    constants.projection = to_array(view.projection);
    constants.view_projection = to_array(view.view_projection);
    constants.inverse_view = to_array(view.inverse_view);
    constants.camera_pos = to_array4(view.camera_position, 1.0f);
    constants.sun_direction = to_array4(view.sun_direction.normalize(), 0.0f);
    constants.sun_color = to_array4(view.sun_color, 0.0f);
    constants.ambient_color = to_array4(view.ambient_color, 0.0f);
    constants.params0 = {view.near_z, view.far_z, settings.post.exposure, 0.0f};
    constants.params1 = {view.orthographic ? 1.0f : 0.0f, static_cast<float>(width_),
                         static_cast<float>(height_), 0.0f};
    const float ortho_width =
        view.orthographic ? 2.0f / std::max(view.projection.m[0][0], 1.0e-6f) : 0.0f;
    const float ortho_height =
        view.orthographic ? 2.0f / std::max(view.projection.m[1][1], 1.0e-6f) : 0.0f;
    constants.params2 = {view.projection.m[0][0], view.projection.m[1][1], ortho_width,
                         ortho_height};
    constants.background_color = to_array4(view.background_color, 1.0f);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_.context->Map(frame_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                     &mapped))) {
        throw std::runtime_error("Map frame constants failed");
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context_.context->Unmap(frame_buffer_.Get(), 0);
}

void Renderer::update_material_constants(const MaterialConstants& constants) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_.context->Map(material_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                     &mapped))) {
        throw std::runtime_error("Map material constants failed");
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context_.context->Unmap(material_buffer_.Get(), 0);
}

void Renderer::bind_frame_constants() {
    ID3D11Buffer* buffers[] = {frame_buffer_.Get()};
    context_.context->VSSetConstantBuffers(0, 1, buffers);
    context_.context->PSSetConstantBuffers(0, 1, buffers);
}

void Renderer::bind_material_constants() {
    ID3D11Buffer* buffers[] = {material_buffer_.Get()};
    context_.context->VSSetConstantBuffers(1, 1, buffers);
    context_.context->PSSetConstantBuffers(1, 1, buffers);
}

std::vector<ShaderDefine> Renderer::lighting_defines(const GraphicsSettings& settings,
                                                     bool linear_tonemap) const {
    std::vector<ShaderDefine> defines;
    defines.push_back({"SUN_LIGHT_ENABLE", std::to_string(settings.sun.light_mode)});
    defines.push_back({"SUN_SPECULAR_DISK_ENABLE",
                       settings.sun.specular_disk ? "1" : "0"});
    defines.push_back({"SUN_LIMB_DARKENING", settings.sun.limb_darkening ? "1" : "0"});
    defines.push_back({"SUN_ANGULAR_RADIUS", std::to_string(settings.sun.angular_radius)});
    defines.push_back({"DIFFUSE_BRDF_TYPE",
                       std::to_string(static_cast<int>(settings.brdf.diffuse_type))});
    defines.push_back({"BRDF_FRESNEL_TYPE",
                       std::to_string(static_cast<int>(settings.brdf.fresnel_type))});
    defines.push_back({"TONEMAP_OPERATOR",
                       std::to_string(linear_tonemap
                                          ? 0
                                          : static_cast<int>(settings.post.tonemap))});
    return defines;
}

void Renderer::draw_geometry(const FrameView& view) {
    const Target* gb0 = targets_.get("GB0");
    const Target* gb1 = targets_.get("GB1");
    const Target* gb2 = targets_.get("GB2");
    const Target* gb3 = targets_.get("GB3");
    const Target* gb4 = targets_.get("GB4");
    const DepthTarget* depth = targets_.get_depth("Depth");
    if (gb0 == nullptr || gb1 == nullptr || gb2 == nullptr || gb3 == nullptr ||
        gb4 == nullptr || depth == nullptr) {
        throw std::runtime_error("native renderer targets are incomplete");
    }

    ID3D11RenderTargetView* views[] = {gb0->rtv.Get(), gb1->rtv.Get(), gb2->rtv.Get(),
                                       gb3->rtv.Get(), gb4->rtv.Get()};
    context_.context->OMSetRenderTargets(5, views, depth->dsv.Get());
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (ID3D11RenderTargetView* view_target : views) {
        context_.context->ClearRenderTargetView(view_target, clear);
    }
    context_.context->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout_vector(std::begin(layout), std::end(layout));
    const ShaderProgram& program =
        shaders_.get("gbuffer", kShaderSource, "GBufferVS", "GBufferPS", {}, &layout_vector);
    context_.context->IASetInputLayout(program.input_layout.Get());
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    bind_frame_constants();
    bind_material_constants();
    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(0, 1, &sampler);
    context_.context->RSSetState(context_.state_manager->get_rasterizer(
        view.wireframe ? core::RasterizerPreset::Wireframe
                       : core::RasterizerPreset::CullNoneSolid));
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      blend_factor, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);

    if (view.show_geometry) {
        for (const DrawItem& item : view.opaque) {
            draw_item(item, false);
        }
    }
}

void Renderer::draw_item(const DrawItem& item, bool transparent) {
    (void)transparent;
    if (item.mesh == nullptr || item.materials == nullptr || item.textures == nullptr ||
        item.skinning == nullptr || item.subset >= item.mesh->subsets.size()) {
        return;
    }
    item.mesh->bind_to(context_.context.Get());
    item.skinning->bind_vs_to(context_.context.Get(), 8);

    const render::MeshSubset& subset = item.mesh->subsets[item.subset];
    MaterialConstants constants;
    ID3D11ShaderResourceView* srv = white_texture_.srv.Get();
    if (subset.material_index < item.materials->size()) {
        const render::GpuMaterial& material = (*item.materials)[subset.material_index];
        constants = material_from_gpu(material.constants);
        if (material.texture_index >= 0 &&
            static_cast<std::size_t>(material.texture_index) < item.textures->size() &&
            (*item.textures)[static_cast<std::size_t>(material.texture_index)]) {
            srv = (*item.textures)[static_cast<std::size_t>(material.texture_index)]->srv.Get();
        }
    }
    update_material_constants(constants);
    bind_material_constants();
    context_.context->PSSetShaderResources(8, 1, &srv);
    context_.context->DrawIndexed(subset.index_count, subset.index_start, 0);
}

void Renderer::draw_lighting(const GraphicsSettings& settings, const FrameView& view) {
    (void)view;
    const Target* hdr = targets_.get("Hdr");
    const DepthTarget* depth = targets_.get_depth("Depth");
    if (hdr == nullptr || depth == nullptr) {
        throw std::runtime_error("native renderer HDR target is missing");
    }
    ID3D11RenderTargetView* views[] = {hdr->rtv.Get()};
    context_.context->OMSetRenderTargets(1, views, nullptr);
    context_.context->ClearRenderTargetView(hdr->rtv.Get(), view.background_color.to_array().data());
    if (!view.show_lighting) {
        return;
    }

    const auto defines = lighting_defines(settings, false);
    const ShaderProgram& program =
        shaders_.get("lighting", kShaderSource, "FullscreenVS", "LightingPS", defines, nullptr);
    ID3D11ShaderResourceView* resources[6] = {
        targets_.get("GB0")->srv.Get(), targets_.get("GB1")->srv.Get(),
        targets_.get("GB2")->srv.Get(), targets_.get("GB3")->srv.Get(),
        targets_.get("GB4")->srv.Get(), depth->srv.Get()};
    context_.context->PSSetShaderResources(0, 6, resources);
    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
    context_.context->PSSetSamplers(0, 1, &sampler);
    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_frame_constants();
    draw_fullscreen(program);
}

void Renderer::draw_transparent(const FrameView& view) {
    if (!view.show_transparent || view.transparent.empty()) {
        return;
    }
    const Target* hdr = targets_.get("Hdr");
    const DepthTarget* depth = targets_.get_depth("Depth");
    if (hdr == nullptr || depth == nullptr) {
        return;
    }
    ID3D11RenderTargetView* views[] = {hdr->rtv.Get()};
    context_.context->OMSetRenderTargets(1, views, depth->dsv.Get());
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout_vector(std::begin(layout), std::end(layout));
    const ShaderProgram& program =
        shaders_.get("forward", kShaderSource, "GBufferVS", "ForwardPS", {}, &layout_vector);
    context_.context->IASetInputLayout(program.input_layout.Get());
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    bind_frame_constants();
    bind_material_constants();
    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(0, 1, &sampler);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context->OMSetBlendState(
        context_.state_manager->get_blend(core::BlendPreset::AlphaBlend), blend_factor,
        0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::ReadOnlyLessEqual), 0);
    for (const DrawItem& item : view.transparent) {
        draw_item(item, true);
    }
}

void Renderer::draw_tonemap(const GraphicsSettings& settings, const FrameView& view,
                            ID3D11RenderTargetView* final_rtv, bool linear) {
    const Target* hdr = targets_.get("Hdr");
    if (hdr == nullptr) {
        return;
    }
    ID3D11RenderTargetView* views[] = {final_rtv};
    context_.context->OMSetRenderTargets(1, views, nullptr);
    context_.context->ClearRenderTargetView(final_rtv, view.background_color.to_array().data());
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));

    // The tonemap permutation only needs the operator. Reuse the lighting
    // defines so the cache key stays consistent with the rest of the frame.
    std::vector<ShaderDefine> defines;
    defines.push_back(
        {"TONEMAP_OPERATOR",
         std::to_string(linear ? 0 : static_cast<int>(settings.post.tonemap))});
    const ShaderProgram& program =
        shaders_.get("tonemap", kShaderSource, "FullscreenVS", "TonemapPS", defines, nullptr);
    ID3D11ShaderResourceView* resources[] = {hdr->srv.Get()};
    context_.context->PSSetShaderResources(6, 1, resources);
    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
    context_.context->PSSetSamplers(0, 1, &sampler);
    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_frame_constants();
    draw_fullscreen(program);
}

void Renderer::draw_grid(const FrameView& view, ID3D11RenderTargetView* final_rtv) {
    if (!view.draw_grid || !view.show_grid || grid_vertex_count_ == 0) {
        return;
    }
    const DepthTarget* depth = targets_.get_depth("Depth");
    if (depth == nullptr) {
        return;
    }
    ID3D11RenderTargetView* views[] = {final_rtv};
    context_.context->OMSetRenderTargets(1, views, depth->dsv.Get());
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout_vector(std::begin(layout), std::end(layout));
    const ShaderProgram& program =
        shaders_.get("grid", kShaderSource, "GridVS", "GridPS", {}, &layout_vector);
    context_.context->IASetInputLayout(program.input_layout.Get());
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    const UINT stride = sizeof(GridVertex);
    const UINT offset = 0;
    ID3D11Buffer* vertex_buffers[] = {grid_buffer_.Get()};
    context_.context->IASetVertexBuffers(0, 1, vertex_buffers, &stride, &offset);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    bind_frame_constants();
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context->OMSetBlendState(
        context_.state_manager->get_blend(core::BlendPreset::AlphaBlend), blend_factor,
        0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 0);
    context_.context->Draw(grid_vertex_count_, 0);
}

void Renderer::draw_fullscreen(const ShaderProgram& program) {
    context_.context->IASetInputLayout(nullptr);
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context->Draw(3, 0);
}

} // namespace hikari::gfx
