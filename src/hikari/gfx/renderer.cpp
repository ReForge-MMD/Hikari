#include "hikari/gfx/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "hikari/gfx/ibl.hpp"
#include "hikari/gfx/sky.hpp"
#include "hikari/gfx/sky_assets.hpp"

namespace hikari::gfx {
namespace {

const char* kShaderSource = "gfx_core.hlsl";

// Nine real SH coefficients with three floats each.
constexpr std::uint32_t kSkyShCoefficientCount = 27;
// Must match SH_GROUPS and SH_THREADS in SkyShProjectCS/SkyShReduceCS.
constexpr std::uint32_t kSkyShGroupCount = 32;
constexpr std::uint32_t kSkyShPartialCount = kSkyShGroupCount * kSkyShCoefficientCount;

// Creates a structured float buffer with both a shader resource view and an
// unordered access view, the shape every compute-written buffer in the sky path
// needs.
void create_float_structured_buffer(ID3D11Device* device, std::uint32_t element_count,
                                    const char* name,
                                    Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
                                    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav,
                                    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(float) * element_count;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(float);
    if (FAILED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()))) {
        throw std::runtime_error(std::string("CreateBuffer failed: ") + name);
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = element_count;
    if (FAILED(device->CreateUnorderedAccessView(buffer.Get(), &uav_desc, uav.GetAddressOf()))) {
        throw std::runtime_error(std::string("CreateUnorderedAccessView failed: ") + name);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.NumElements = element_count;
    if (FAILED(device->CreateShaderResourceView(buffer.Get(), &srv_desc, srv.GetAddressOf()))) {
        throw std::runtime_error(std::string("CreateShaderResourceView failed: ") + name);
    }
}

std::array<float, 4> to_array4(const Vec3& value, float w) {
    return {value.x, value.y, value.z, w};
}

std::array<std::array<float, 4>, 4> to_array(const Mat4& matrix) {
    return matrix.m;
}

// Face bases in the Direct3D cubemap order: +X, -X, +Y, -Y, +Z, -Z. The rows of
// the reconstructed InverseView are right, up, and forward, so the mapping from
// a face texel back to a direction matches TextureCube sampling exactly. The
// shared table lives in sky.cpp because the CPU cubemap conversion needs it too.
std::array<std::array<float, 4>, 4> face_inverse_view(std::uint32_t face) {
    const CubeFaceBasis& basis = cube_face_basis(face);
    std::array<std::array<float, 4>, 4> inverse_view{};
    inverse_view[0] = {basis.right.x, basis.right.y, basis.right.z, 0.0f};
    inverse_view[1] = {basis.up.x, basis.up.y, basis.up.z, 0.0f};
    inverse_view[2] = {basis.forward.x, basis.forward.y, basis.forward.z, 0.0f};
    inverse_view[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return inverse_view;
}

// Half float conversion for the 16 bit float capture target, matching the
// rounding of a hardware store: round to nearest even on the truncated mantissa.
std::uint16_t float_to_half(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent == 0xffu) {
        return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa != 0 ? 0x200u : 0u));
    }
    const int unbiased = static_cast<int>(exponent) - 127 + 15;
    if (unbiased >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (unbiased <= 0) {
        if (unbiased < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000u;
        const int shift = 14 - unbiased;
        const std::uint32_t rounded = (mantissa + (1u << (shift - 1))) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    mantissa += 0x1000u;
    if ((mantissa & 0x800000u) != 0) {
        mantissa = 0;
        if (unbiased + 1 >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7c00u);
        }
        return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(unbiased + 1) << 10));
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(unbiased) << 10) |
                                      (mantissa >> 13));
}

// The capture only needs to be redone when a sky input moves by more than the
// eye can see in a reflection. Relative tolerance keeps the test scale free.
bool sky_value_changed(float a, float b) {
    const float scale = std::max(1.0f, std::max(std::abs(a), std::abs(b)));
    return std::abs(a - b) > 1.0e-3f * scale;
}

bool sky_values_changed(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (sky_value_changed(a[i], b[i])) {
            return true;
        }
    }
    return false;
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

uint32_t shadow_resolution_from_quality(int quality) {
    switch (quality) {
    case 1: return 1024;
    case 2: return 2048;
    case 3: return 4096;
    case 4: return 8192;
    default: return 2048;
    }
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

    // ShadowParams buffer (b2)
    D3D11_BUFFER_DESC shadow_desc{};
    shadow_desc.ByteWidth = sizeof(ShadowConstants);
    shadow_desc.Usage = D3D11_USAGE_DYNAMIC;
    shadow_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    shadow_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&shadow_desc, nullptr,
                                             shadow_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer shadow constants failed");
    }

    // ShadowDrawParams buffer (b3)
    D3D11_BUFFER_DESC sdraw_desc{};
    sdraw_desc.ByteWidth = sizeof(ShadowDrawConstants);
    sdraw_desc.Usage = D3D11_USAGE_DYNAMIC;
    sdraw_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    sdraw_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&sdraw_desc, nullptr,
                                             shadow_draw_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer shadow draw constants failed");
    }

    // SkyParams buffer (b4)
    D3D11_BUFFER_DESC sky_desc{};
    sky_desc.ByteWidth = sizeof(SkyConstants);
    sky_desc.Usage = D3D11_USAGE_DYNAMIC;
    sky_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    sky_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&sky_desc, nullptr, sky_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer sky constants failed");
    }

    // IblParams buffer (b5)
    D3D11_BUFFER_DESC ibl_desc{};
    ibl_desc.ByteWidth = sizeof(IblConstants);
    ibl_desc.Usage = D3D11_USAGE_DYNAMIC;
    ibl_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ibl_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(context_.device->CreateBuffer(&ibl_desc, nullptr, ibl_buffer_.GetAddressOf()))) {
        throw std::runtime_error("CreateBuffer IBL constants failed");
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
    sky_capture_size_ = 0;
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
    // Shadow map: D32 array, 4 cascades (default 2048 or current_shadow_res_)
    const uint32_t init_shadow_res = current_shadow_res_ > 0 ? current_shadow_res_ : 2048;
    targets_.create_array_depth(context_.device.Get(), "ShadowMap", init_shadow_res, init_shadow_res, 4);
    current_shadow_res_ = init_shadow_res;
    // Sky visibility: R32G32F 1024x1024 + D32 depth
    targets_.create_target(context_.device.Get(), "SkyVisibility", 1024, 1024,
                            DXGI_FORMAT_R32G32_FLOAT);
    targets_.create_depth(context_.device.Get(), "SkyVisibilityDepth", 1024, 1024);
    // Shadow mask + bilateral blur ping-pong at scene resolution
    targets_.create_target(context_.device.Get(), "ShadowMask", width, height,
                            DXGI_FORMAT_R8G8B8A8_UNORM);
    targets_.create_target(context_.device.Get(), "ShadowMaskBlur", width, height,
                            DXGI_FORMAT_R8G8B8A8_UNORM);
    // Sun transmittance by altitude: one texel per atmosphere sample step.
    targets_.create_target(context_.device.Get(), "SunTransmittance", 64, 1,
                            DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void Renderer::ensure_shadow_map(int quality) {
    if (quality <= 0) {
        return;
    }
    const uint32_t target_res = shadow_resolution_from_quality(quality);
    const DepthTarget* existing = targets_.get_depth("ShadowMap");
    if (existing == nullptr || current_shadow_res_ != target_res) {
        targets_.create_array_depth(context_.device.Get(), "ShadowMap", target_res, target_res, 4);
        current_shadow_res_ = target_res;
    }
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
    ensure_shadow_map(settings.shadows.quality);
    ensure_sky_capture(settings);
    ensure_ibl_environment(settings);
    update_frame_constants(settings, view, !view.show_tonemap);
    update_shadow_constants(settings, view);
    update_sky_constants(settings, view);
    update_ibl_constants(settings);
    draw_shadow_cascades(settings, view);
    draw_sky_visibility(settings, view);
    draw_sun_transmittance(settings, view);
    // The capture rewrites the frame constants for every cube face. Capture,
    // prefilter, and SH are one unit, so the throttle skips all three together
    // and the lighting pass keeps the resources of the last refresh.
    if (should_refresh_sky_environment(settings, view)) {
        if (ibl_custom_active_) {
            // A file-backed environment replaces the capture: upload its six
            // faces into the same target, then reuse prefilter and SH as is.
            upload_ibl_environment();
        } else {
            draw_sky_capture(settings, view);
        }
        draw_sky_prefilter(settings, view);
        draw_sky_sh(settings);
    }
    update_frame_constants(settings, view, !view.show_tonemap);
    draw_geometry(view);
    draw_shadow_mask(settings, view);
    draw_shadow_blur(settings, view);
    draw_sky_background(settings, view);
    draw_lighting(settings, view);
    draw_transparent(view);
    draw_tonemap(settings, view, final_rtv, !view.show_tonemap);
    draw_grid(view, final_rtv);

    ID3D11ShaderResourceView* null_views[8] = {};
    context_.context->PSSetShaderResources(0, 8, null_views);
    // Slots 8..11 hold the material texture and the shadow/sky helper targets;
    // they must be released before those targets are rebound as render targets.
    context_.context->PSSetShaderResources(8, 4, null_views);
    context_.context->PSSetShaderResources(12, 6, null_views);
    context_.context->VSSetShaderResources(0, 1, null_views);
    ID3D11ShaderResourceView* null_diffuse = nullptr;
    context_.context->VSSetShaderResources(8, 1, &null_diffuse);
}

void Renderer::update_frame_constants(const GraphicsSettings& settings, const FrameView& view,
                                      bool linear_tonemap) {
    upload_frame_constants(build_frame_constants(settings, view, linear_tonemap));
}

FrameConstants Renderer::build_frame_constants(const GraphicsSettings& settings,
                                               const FrameView& view,
                                               bool linear_tonemap) const {
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
    constants.params0 = {view.near_z, view.far_z, settings.post.exposure, view.time_seconds};
    constants.params1 = {view.orthographic ? 1.0f : 0.0f, static_cast<float>(width_),
                         static_cast<float>(height_), 0.0f};
    const float ortho_width =
        view.orthographic ? 2.0f / std::max(view.projection.m[0][0], 1.0e-6f) : 0.0f;
    const float ortho_height =
        view.orthographic ? 2.0f / std::max(view.projection.m[1][1], 1.0e-6f) : 0.0f;
    constants.params2 = {view.projection.m[0][0], view.projection.m[1][1], ortho_width,
                         ortho_height};
    constants.background_color = to_array4(view.background_color, 1.0f);
    return constants;
}

void Renderer::upload_frame_constants(const FrameConstants& constants) {
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
    defines.push_back({"SKY_ENABLE", settings.sky.enabled ? "1" : "0"});
    defines.push_back({"SUN_SKY_COUPLING", settings.sun.sky_coupling ? "1" : "0"});
    defines.push_back({"SUN_CORONA_ENABLE", settings.sun.corona ? "1" : "0"});
    defines.push_back({"SUN_ANGULAR_RADIUS", std::to_string(settings.sun.angular_radius)});
    defines.push_back({"DIFFUSE_BRDF_TYPE",
                       std::to_string(static_cast<int>(settings.brdf.diffuse_type))});
    defines.push_back({"BRDF_FRESNEL_TYPE",
                       std::to_string(static_cast<int>(settings.brdf.fresnel_type))});
    defines.push_back({"TONEMAP_OPERATOR",
                       std::to_string(linear_tonemap
                                          ? 0
                                          : static_cast<int>(settings.post.tonemap))});
    defines.push_back({"DEBUG_VIEW", std::to_string(static_cast<int>(settings.debug.view))});
    defines.push_back({"IBL_QUALITY", std::to_string(std::clamp(settings.ibl.quality, 0, 2))});
    // Selects the custom environment background, which is a different pixel
    // shader path rather than a parameter, so it belongs in the permutation.
    defines.push_back({"SKY_SOURCE_CUSTOM", ibl_custom_active_ ? "1" : "0"});
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

void Renderer::ensure_sky_capture(const GraphicsSettings& settings) {
    const std::uint32_t size = sky_capture_resolution(settings.ibl.quality);
    if (size == 0) {
        return;
    }
    if (targets_.get("SkyCapture") != nullptr && sky_capture_size_ == size) {
        return;
    }
    targets_.create_cubemap(context_.device.Get(), "SkyCapture", size,
                            DXGI_FORMAT_R16G16B16A16_FLOAT);
    targets_.create_cubemap(context_.device.Get(), "SkyPrefilter", size,
                            DXGI_FORMAT_R16G16B16A16_FLOAT, sky_prefilter_mip_count(size));
    sky_capture_size_ = size;
    // A fresh chain has no content yet, so the next frame must not be throttled.
    sky_capture_valid_ = false;
}

// Loads ibl.custom_path when it changes, or when the capture size changes so an
// already decoded source can be converted to the new face size. Failures keep
// the previous state usable and are reported through ibl_error().
void Renderer::ensure_ibl_environment(const GraphicsSettings& settings) {
    const std::uint32_t size = sky_capture_resolution(settings.ibl.quality);
    if (settings.ibl.custom_path == ibl_source_path_ && size == ibl_target_size_) {
        return;
    }
    ibl_source_path_ = settings.ibl.custom_path;
    ibl_target_size_ = size;
    ibl_error_.clear();
    ibl_custom_active_ = false;
    ibl_cubemap_ = {};
    // The capture no longer describes the requested source, whatever it was.
    sky_capture_valid_ = false;
    if (ibl_source_path_.empty()) {
        return;
    }
    if (size == 0) {
        ibl_error_ = "IBL quality 0 keeps image based lighting off";
        return;
    }
    IblLoadResult loaded = load_ibl_image(ibl_source_path_);
    if (!loaded.ok()) {
        ibl_error_ = std::move(loaded.error);
        return;
    }
    IblLoadResult converted = ibl_image_to_cubemap(loaded.image, size);
    if (!converted.ok()) {
        ibl_error_ = std::move(converted.error);
        return;
    }
    ibl_cubemap_ = std::move(converted.image);
    ibl_custom_active_ = true;
}

// Copies the decoded cubemap into the R16G16B16A16_FLOAT capture target. Only
// mip 0 is written: the prefilter chain derives every other level from it.
void Renderer::upload_ibl_environment() {
    const Target* cube = targets_.get("SkyCapture");
    if (cube == nullptr || !ibl_cubemap_.cubemap) {
        return;
    }
    const std::uint32_t mip_count = static_cast<std::uint32_t>(cube->mip_face_rtvs.size() / 6);
    const std::uint32_t size = ibl_cubemap_.width;
    if (mip_count == 0 || size == 0 || cube->width != size) {
        return;
    }
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t face = 0; face < 6; ++face) {
        const float* source = ibl_cubemap_.face_data(face);
        if (source == nullptr) {
            return;
        }
        for (std::size_t texel = 0; texel < static_cast<std::size_t>(size) * size; ++texel) {
            for (std::uint32_t channel = 0; channel < 4; ++channel) {
                pixels[texel * 4 + channel] = float_to_half(source[texel * 4 + channel]);
            }
        }
        const UINT subresource = D3D11CalcSubresource(0, face, mip_count);
        context_.context->UpdateSubresource(cube->texture.Get(), subresource, nullptr,
                                            pixels.data(), size * 8, 0);
    }
}

// Decides whether the capture, prefilter, and SH refresh run on this frame. A
// changed sky input requests a refresh, the update rate caps how often it may
// happen, and a fresh or resized chain bypasses the cap.
bool Renderer::should_refresh_sky_environment(const GraphicsSettings& settings,
                                              const FrameView& view) {
    if (sky_capture_resolution(settings.ibl.quality) == 0) {
        return false;
    }
    // A file-backed environment does not depend on the analytic sky, so it stays
    // available when the procedural sky is switched off.
    if (ibl_custom_active_) {
        if (!sky_capture_valid_) {
            sky_capture_valid_ = true;
            ++sky_update_count_;
            return true;
        }
        return false;
    }
    if (!settings.sky.enabled) {
        return false;
    }
    const SkyConstants constants = build_sky_constants(settings, view);
    bool changed = !sky_capture_valid_;
    if (!changed && (sky_values_changed(constants.sky_params0, sky_capture_state_.sky_params0) ||
                     sky_values_changed(constants.sky_params1, sky_capture_state_.sky_params1) ||
                     sky_values_changed(constants.sky_params2, sky_capture_state_.sky_params2) ||
                     sky_values_changed(constants.sky_params3, sky_capture_state_.sky_params3))) {
        changed = true;
    }
    // Roughly a tenth of a degree of sun movement is the smallest change that
    // survives the 128 or 256 pixel capture, so anything below it only costs a
    // refresh without changing the result.
    constexpr float kMinimumSunDot = 0.9999985f;
    if (!changed && view.sun_direction.dot(sky_capture_sun_) < kMinimumSunDot) {
        changed = true;
    }
    if (!changed) {
        return false;
    }

    const float rate = static_cast<float>(std::clamp(settings.sky.ibl_update_hz, 1, 60));
    const auto interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(1.0f / rate));
    const auto now = std::chrono::steady_clock::now();
    if (sky_capture_valid_ && now - sky_capture_time_ < interval) {
        return false;
    }

    sky_capture_state_ = constants;
    sky_capture_sun_ = view.sun_direction;
    sky_capture_valid_ = true;
    sky_capture_time_ = now;
    ++sky_update_count_;
    return true;
}

void Renderer::draw_sky_capture(const GraphicsSettings& settings, const FrameView& view) {
    if (!settings.sky.enabled) {
        return;
    }
    const Target* cube = targets_.get("SkyCapture");
    const Target* sun_lut = targets_.get("SunTransmittance");
    if (cube == nullptr || cube->mip_face_rtvs.size() != 6 || sun_lut == nullptr) {
        return;
    }
    ensure_sky_textures();

    ID3D11ShaderResourceView* lut_srv = sun_lut->srv.Get();
    context_.context->PSSetShaderResources(11, 1, &lut_srv);
    ID3D11ShaderResourceView* sky_textures[3] = {cloud_texture_.srv.Get(),
                                                 milky_way_texture_.srv.Get(),
                                                 moon_texture_.srv.Get()};
    context_.context->PSSetShaderResources(12, 3, sky_textures);

    const auto defines = lighting_defines(settings, false);
    const ShaderProgram& program = shaders_.get("sky_capture", kShaderSource, "FullscreenVS",
                                                "SkyCapturePS", defines, nullptr);

    FrameConstants constants = build_frame_constants(settings, view, false);
    const Mat4 face_projection =
        perspective_lh(3.14159265358979323846f * 0.5f, 1.0f, 0.1f, 10.0f);
    constants.projection = to_array(face_projection);
    constants.params1 = {0.0f, static_cast<float>(cube->width),
                         static_cast<float>(cube->height), 0.0f};
    constants.params2 = {face_projection.m[0][0], face_projection.m[1][1], 0.0f, 0.0f};

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(cube->width);
    vp.Height = static_cast<float>(cube->height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_.context->RSSetViewports(1, &vp);
    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_sky_samplers();
    bind_frame_constants();
    bind_sky_constants();

    for (std::size_t face = 0; face < 6; ++face) {
        constants.inverse_view = face_inverse_view(static_cast<std::uint32_t>(face));
        upload_frame_constants(constants);

        ID3D11RenderTargetView* rtv = cube->mip_face_rtvs[face].Get();
        context_.context->OMSetRenderTargets(1, &rtv, nullptr);
        draw_fullscreen(program);
    }

    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));
}

// GGX prefilter of the captured sky into the mip chain of SkyPrefilter, one
// roughness step per mip: 0 at mip 0 (a copy), 1 at the last mip. The split sum
// then picks a level by roughness instead of convolving per pixel.
void Renderer::draw_sky_prefilter(const GraphicsSettings& settings, const FrameView& view) {
    if (sky_capture_resolution(settings.ibl.quality) == 0 ||
        (!settings.sky.enabled && !ibl_custom_active_)) {
        return;
    }
    const Target* source = targets_.get("SkyCapture");
    const Target* prefilter = targets_.get("SkyPrefilter");
    if (source == nullptr || prefilter == nullptr || prefilter->mip_face_rtvs.empty()) {
        return;
    }
    const std::uint32_t mip_count =
        static_cast<std::uint32_t>(prefilter->mip_face_rtvs.size() / 6);
    if (mip_count == 0) {
        return;
    }

    const auto defines = lighting_defines(settings, false);
    const ShaderProgram& program = shaders_.get("sky_prefilter", kShaderSource, "FullscreenVS",
                                                "SkyPrefilterPS", defines, nullptr);

    FrameConstants frame = build_frame_constants(settings, view, false);
    const Mat4 face_projection =
        perspective_lh(3.14159265358979323846f * 0.5f, 1.0f, 0.1f, 10.0f);
    frame.projection = to_array(face_projection);
    frame.params1 = {0.0f, static_cast<float>(source->width),
                     static_cast<float>(source->height), 0.0f};
    frame.params2 = {face_projection.m[0][0], face_projection.m[1][1], 0.0f, 0.0f};

    SkyConstants sky = build_sky_constants(settings, view);

    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_sky_samplers();
    bind_frame_constants();
    bind_sky_constants();
    ID3D11ShaderResourceView* cube_srv = source->srv.Get();
    context_.context->PSSetShaderResources(15, 1, &cube_srv);

    for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
        const float roughness =
            mip_count > 1 ? static_cast<float>(mip) / static_cast<float>(mip_count - 1) : 0.0f;
        // A mirror lobe needs one tap; wider lobes need more samples to hide the
        // blotches that a sparse high dynamic range sky (stars) leaves behind.
        const float samples = mip == 0 ? 1.0f : 16.0f * static_cast<float>(mip);
        sky.sky_params4 = {roughness, samples, static_cast<float>(mip_count), 0.0f};
        upload_sky_constants(sky);

        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(std::max(prefilter->width >> mip, 1u));
        vp.Height = vp.Width;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_.context->RSSetViewports(1, &vp);

        for (std::uint32_t face = 0; face < 6; ++face) {
            frame.inverse_view = face_inverse_view(face);
            upload_frame_constants(frame);

            ID3D11RenderTargetView* rtv =
                prefilter->mip_face_rtvs[static_cast<std::size_t>(mip) * 6 + face].Get();
            context_.context->OMSetRenderTargets(1, &rtv, nullptr);
            draw_fullscreen(program);
        }
    }

    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
    ID3D11ShaderResourceView* null_srv = nullptr;
    context_.context->PSSetShaderResources(15, 1, &null_srv);
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));
}

void Renderer::ensure_sky_sh() {
    if (sky_sh_buffer_ != nullptr) {
        return;
    }
    create_float_structured_buffer(context_.device.Get(), kSkyShCoefficientCount, "sky SH",
                                   sky_sh_buffer_, sky_sh_uav_, sky_sh_srv_);
    create_float_structured_buffer(context_.device.Get(), kSkyShPartialCount, "sky SH partial",
                                   sky_sh_partial_buffer_, sky_sh_partial_uav_,
                                   sky_sh_partial_srv_);
}

void Renderer::draw_sky_sh(const GraphicsSettings& settings) {
    if (sky_capture_resolution(settings.ibl.quality) == 0 ||
        (!settings.sky.enabled && !ibl_custom_active_)) {
        return;
    }
    const Target* cube = targets_.get("SkyCapture");
    if (cube == nullptr) {
        return;
    }
    ensure_sky_sh();

    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->CSSetSamplers(3, 1, &sampler);
    ID3D11ShaderResourceView* cube_srv = cube->srv.Get();
    context_.context->CSSetShaderResources(15, 1, &cube_srv);

    // First pass: one partial sum per group over a slice of the Fibonacci sphere.
    const ComputeProgram& project =
        shaders_.get_compute("sky_sh_project", kShaderSource, "SkyShProjectCS");
    ID3D11UnorderedAccessView* partial = sky_sh_partial_uav_.Get();
    context_.cs_set_uavs(0, {partial});
    context_.context->CSSetShader(project.compute.Get(), nullptr, 0);
    context_.context->Dispatch(kSkyShGroupCount, 1, 1);
    context_.context->CSSetShader(nullptr, nullptr, 0);

    // The partial buffer becomes an input, so it must leave the UAV slot first.
    context_.cs_set_uavs(0, {nullptr});
    ID3D11ShaderResourceView* partial_srv = sky_sh_partial_srv_.Get();
    context_.context->CSSetShaderResources(0, 1, &partial_srv);
    ID3D11UnorderedAccessView* output = sky_sh_uav_.Get();
    context_.cs_set_uavs(1, {output});

    // Second pass: sum the partials in one group and write the final coefficients.
    const ComputeProgram& reduce =
        shaders_.get_compute("sky_sh_reduce", kShaderSource, "SkyShReduceCS");
    context_.context->CSSetShader(reduce.compute.Get(), nullptr, 0);
    context_.context->Dispatch(1, 1, 1);
    context_.context->CSSetShader(nullptr, nullptr, 0);

    context_.cs_set_uavs(1, {nullptr});
    ID3D11ShaderResourceView* null_srv = nullptr;
    context_.context->CSSetShaderResources(0, 1, &null_srv);
    context_.context->CSSetShaderResources(15, 1, &null_srv);
}

void Renderer::draw_sky_background(const GraphicsSettings& settings, const FrameView& view) {
    const Target* hdr = targets_.get("Hdr");
    if (hdr == nullptr) {
        throw std::runtime_error("native renderer HDR target is missing");
    }
    ID3D11RenderTargetView* views[] = {hdr->rtv.Get()};
    context_.context->OMSetRenderTargets(1, views, nullptr);
    context_.context->ClearRenderTargetView(hdr->rtv.Get(),
                                            view.background_color.to_array().data());
    if (!settings.sky.enabled) {
        return;
    }

    const auto defines = lighting_defines(settings, false);
    const ShaderProgram& program =
        shaders_.get("sky", kShaderSource, "FullscreenVS", "SkyPS", defines, nullptr);
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));

    const Target* sun_lut = targets_.get("SunTransmittance");
    if (sun_lut != nullptr) {
        ID3D11ShaderResourceView* lut_srv = sun_lut->srv.Get();
        context_.context->PSSetShaderResources(11, 1, &lut_srv);
    }
    ensure_sky_textures();
    const Target* cube = targets_.get("SkyCapture");
    ID3D11ShaderResourceView* sky_textures[4] = {
        cloud_texture_.srv.Get(), milky_way_texture_.srv.Get(), moon_texture_.srv.Get(),
        cube != nullptr ? cube->srv.Get() : nullptr};
    context_.context->PSSetShaderResources(12, 4, sky_textures);
    const Target* prefilter = targets_.get("SkyPrefilter");
    ID3D11ShaderResourceView* prefilter_srv =
        prefilter != nullptr ? prefilter->srv.Get() : nullptr;
    context_.context->PSSetShaderResources(17, 1, &prefilter_srv);

    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_sky_samplers();
    bind_frame_constants();
    bind_sky_constants();
    draw_fullscreen(program);
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
    if (!view.show_lighting) {
        return;
    }

    auto defines = lighting_defines(settings, false);
    // Merge shadow defines into lighting permutation key
    for (auto& d : shadow_defines(settings)) {
        defines.push_back(d);
    }
    const ShaderProgram& program =
        shaders_.get("lighting", kShaderSource, "FullscreenVS", "LightingPS", defines, nullptr);
    ID3D11ShaderResourceView* resources[6] = {
        targets_.get("GB0")->srv.Get(), targets_.get("GB1")->srv.Get(),
        targets_.get("GB2")->srv.Get(), targets_.get("GB3")->srv.Get(),
        targets_.get("GB4")->srv.Get(), depth->srv.Get()};
    context_.context->PSSetShaderResources(0, 6, resources);

    // Bind shadow mask at t10 and sky visibility at t9
    if (settings.shadows.quality > 0) {
        const Target* shadow_mask = targets_.get("ShadowMask");
        if (shadow_mask != nullptr) {
            ID3D11ShaderResourceView* mask_srv = shadow_mask->srv.Get();
            context_.context->PSSetShaderResources(10, 1, &mask_srv);
        }
        const DepthTarget* shadow_map = targets_.get_depth("ShadowMap");
        if (shadow_map != nullptr) {
            ID3D11ShaderResourceView* shadow_srv = shadow_map->srv.Get();
            context_.context->PSSetShaderResources(7, 1, &shadow_srv);
        }
    }
    if (settings.ao.sky_visibility > 0) {
        const Target* sky_vis = targets_.get("SkyVisibility");
        if (sky_vis != nullptr) {
            ID3D11ShaderResourceView* sky_srv = sky_vis->srv.Get();
            context_.context->PSSetShaderResources(9, 1, &sky_srv);
        }
    }
    if (settings.sky.enabled) {
        const Target* sun_lut = targets_.get("SunTransmittance");
        if (sun_lut != nullptr) {
            ID3D11ShaderResourceView* lut_srv = sun_lut->srv.Get();
            context_.context->PSSetShaderResources(11, 1, &lut_srv);
        }
        ensure_sky_textures();
        ID3D11ShaderResourceView* sky_textures[3] = {cloud_texture_.srv.Get(),
                                                     milky_way_texture_.srv.Get(),
                                                     moon_texture_.srv.Get()};
        context_.context->PSSetShaderResources(12, 3, sky_textures);
        if (sky_sh_srv_ != nullptr && sky_capture_resolution(settings.ibl.quality) > 0) {
            ID3D11ShaderResourceView* sh_srv = sky_sh_srv_.Get();
            context_.context->PSSetShaderResources(16, 1, &sh_srv);
        }
        const Target* prefiltered = targets_.get("SkyPrefilter");
        if (prefiltered != nullptr && settings.ibl.quality > 0) {
            ID3D11ShaderResourceView* prefilter_srv = prefiltered->srv.Get();
            context_.context->PSSetShaderResources(17, 1, &prefilter_srv);
        }
    }
    bind_shadow_constants();
    bind_sky_constants();
    bind_ibl_constants();

    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
    context_.context->PSSetSamplers(0, 1, &sampler);
    auto* point_sampler = context_.state_manager->get_sampler(core::SamplerPreset::PointClamp);
    context_.context->PSSetSamplers(1, 1, &point_sampler);
    auto* wrap_sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(3, 1, &wrap_sampler);
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

namespace {

// Build a row-major 4x4 orthographic projection matrix matching HLSL row_major convention.
// left-handed, depth [0, 1] (DirectX NDC).
std::array<std::array<float, 4>, 4> ortho_lh_01(float left, float right, float bottom, float top,
                                                  float near_z, float far_z) {
    const float rw = 1.0f / (right - left);
    const float rh = 1.0f / (top - bottom);
    const float rd = 1.0f / (far_z - near_z);
    std::array<std::array<float, 4>, 4> m{};
    m[0][0] = 2.0f * rw;
    m[1][1] = 2.0f * rh;
    m[2][2] = rd;
    m[3][0] = -(right + left) * rw;
    m[3][1] = -(top + bottom) * rh;
    m[3][2] = -near_z * rd;
    m[3][3] = 1.0f;
    return m;
}

// Build a view matrix looking from eye along direction, with given up vector.
// Row-vector convention: view = [right | up | fwd | 0; -eye.right | -eye.up | -eye.fwd | 1]
std::array<std::array<float, 4>, 4> look_along(const hikari::Vec3& eye,
                                                 const hikari::Vec3& fwd,
                                                 const hikari::Vec3& world_up) {
    const hikari::Vec3 f = fwd.normalize();
    hikari::Vec3 r = world_up.cross(f);
    hikari::Vec3 u;
    if (r.length_squared() > 1.0e-6f) {
        r = r.normalize();
        u = f.cross(r);
    } else {
        r = hikari::Vec3(1.0f, 0.0f, 0.0f);
        u = hikari::Vec3(0.0f, 0.0f, f.y >= 0.0f ? -1.0f : 1.0f);
    }
    std::array<std::array<float, 4>, 4> m{};
    m[0] = {r.x, u.x, f.x, 0.0f};
    m[1] = {r.y, u.y, f.y, 0.0f};
    m[2] = {r.z, u.z, f.z, 0.0f};
    m[3] = {-eye.dot(r), -eye.dot(u), -eye.dot(f), 1.0f};
    return m;
}

std::array<std::array<float, 4>, 4> mat_mul(const std::array<std::array<float, 4>, 4>& a,
                                              const std::array<std::array<float, 4>, 4>& b) {
    std::array<std::array<float, 4>, 4> c{};
    for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 4; ++col) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += a[r][k] * b[k][col];
            }
            c[r][col] = v;
        }
    }
    return c;
}

} // anonymous namespace

std::vector<ShaderDefine> Renderer::shadow_defines(const GraphicsSettings& settings) const {
    const auto quality_to_contact = [](int q) -> int {
        switch (q) {
        case 1: return 8;
        case 2: return 12;
        case 3: return 16;
        default: return 0;
        }
    };
    const auto quality_to_ssgs = [](int q) -> int {
        switch (q) {
        case 1: return 16;
        case 2: return 24;
        case 3: return 32;
        default: return 0;
        }
    };
    const auto quality_to_pcss_blocker = [](int q) -> int {
        switch (q) {
        case 1: return 8;
        case 2: return 16;
        case 3: return 24;
        case 4: return 32;
        default: return 16;
        }
    };
    const auto quality_to_pcss_filter = [](int q) -> int {
        switch (q) {
        case 1: return 12;
        case 2: return 16;
        case 3: return 24;
        case 4: return 32;
        default: return 16;
        }
    };
    std::vector<ShaderDefine> defines;
    defines.push_back({"SUN_SHADOW_PCSS", settings.shadows.pcss ? "1" : "0"});
    defines.push_back({"PCSS_BLOCKER_SAMPLES",
                        std::to_string(quality_to_pcss_blocker(settings.shadows.quality))});
    defines.push_back({"PCSS_FILTER_SAMPLES",
                        std::to_string(quality_to_pcss_filter(settings.shadows.quality))});
    defines.push_back({"CONTACT_SHADOW_STEPS",
                        std::to_string(quality_to_contact(settings.shadows.contact_quality))});
    defines.push_back({"SSGS_STEPS",
                        std::to_string(quality_to_ssgs(settings.shadows.ssgs_quality))});
    defines.push_back({"SHADOW_MASK_ENABLE", settings.shadows.quality > 0 ? "1" : "0"});
    defines.push_back({"SKY_VISIBILITY_ENABLE", std::to_string(settings.ao.sky_visibility)});
    defines.push_back({"SHADOW_BLUR_RADIUS", "2"});
    return defines;
}

void Renderer::update_shadow_constants(const GraphicsSettings& settings, const FrameView& view) {
    if (shadow_buffer_ == nullptr) {
        return;
    }
    ShadowConstants constants{};

    // Sun direction (from scene toward sun) — normalize it.
    const hikari::Vec3 sun_dir = view.sun_direction.normalize();
    // Choose a stable "up" vector that is not collinear with the sun direction.
    const hikari::Vec3 world_up = std::abs(sun_dir.y) < 0.99f
                                       ? hikari::Vec3(0.0f, 1.0f, 0.0f)
                                       : hikari::Vec3(0.0f, 0.0f, 1.0f);

    constexpr uint32_t kCascadeCount = 4;
    const uint32_t shadow_res = current_shadow_res_ > 0
                                    ? current_shadow_res_
                                    : shadow_resolution_from_quality(settings.shadows.quality);
    const float near_z = std::max(view.near_z, 0.5f);
    const float far_z = std::min(view.far_z, 300.0f);
    constexpr float lambda = 0.85f;
    float cascade_far[kCascadeCount]{};
    float cascade_near[kCascadeCount]{};
    for (uint32_t ci = 0; ci < kCascadeCount; ++ci) {
        const float p = static_cast<float>(ci + 1) / static_cast<float>(kCascadeCount);
        const float c_log = near_z * std::pow(far_z / near_z, p);
        const float c_lin = near_z + (far_z - near_z) * p;
        cascade_far[ci] = c_log * lambda + c_lin * (1.0f - lambda);
        cascade_near[ci] = (ci == 0) ? near_z : cascade_far[ci - 1];
    }

    for (uint32_t ci = 0; ci < kCascadeCount; ++ci) {
        // Build 8 frustum corners in view space spanning from camera near_z to this cascade's far plane.
        // Starting from near_z guarantees that close shadow casters (characters, props) are always
        // rasterized into all subsequent cascades, completely eliminating shadow clipping across cascade boundaries.
        const float n = near_z;
        const float f = cascade_far[ci];
        // Use camera projection to unproject NDC corners.
        // For perspective: corner_view = (NDC_x / proj[0][0], NDC_y / proj[1][1], 1) * depth.
        const float fx = view.projection.m[0][0] > 1.0e-6f ? 1.0f / view.projection.m[0][0] : 1.0f;
        const float fy = view.projection.m[1][1] > 1.0e-6f ? 1.0f / view.projection.m[1][1] : 1.0f;
        const hikari::Vec3 corners_vs[8] = {
            {-fx * n, -fy * n, n}, {fx * n, -fy * n, n},
            {-fx * n,  fy * n, n}, {fx * n,  fy * n, n},
            {-fx * f, -fy * f, f}, {fx * f, -fy * f, f},
            {-fx * f,  fy * f, f}, {fx * f,  fy * f, f},
        };

        // Transform corners from view space to world space using inverse_view.
        const Mat4& inv_v = view.inverse_view;
        hikari::Vec3 corners_ws[8];
        for (int k = 0; k < 8; ++k) {
            const float cx = corners_vs[k].x;
            const float cy = corners_vs[k].y;
            const float cz = corners_vs[k].z;
            corners_ws[k] = hikari::Vec3(
                cx * inv_v.m[0][0] + cy * inv_v.m[1][0] + cz * inv_v.m[2][0] + inv_v.m[3][0],
                cx * inv_v.m[0][1] + cy * inv_v.m[1][1] + cz * inv_v.m[2][1] + inv_v.m[3][1],
                cx * inv_v.m[0][2] + cy * inv_v.m[1][2] + cz * inv_v.m[2][2] + inv_v.m[3][2]);
        }

        // Frustum center in world space (used as light-view eye origin).
        hikari::Vec3 center(0.0f, 0.0f, 0.0f);
        for (int k = 0; k < 8; ++k) center += corners_ws[k];
        center = center / 8.0f;

        // Build light view matrix looking along sun direction.
        const auto view_mat = look_along(center, sun_dir, world_up);

        // Transform corners to light space and compute AABB.
        float min_x = 1e30f, max_x = -1e30f;
        float min_y = 1e30f, max_y = -1e30f;
        float min_z = 1e30f, max_z = -1e30f;
        for (int k = 0; k < 8; ++k) {
            const hikari::Vec3& ws = corners_ws[k];
            const float lx = ws.x * view_mat[0][0] + ws.y * view_mat[1][0] +
                             ws.z * view_mat[2][0] + view_mat[3][0];
            const float ly = ws.x * view_mat[0][1] + ws.y * view_mat[1][1] +
                             ws.z * view_mat[2][1] + view_mat[3][1];
            const float lz = ws.x * view_mat[0][2] + ws.y * view_mat[1][2] +
                             ws.z * view_mat[2][2] + view_mat[3][2];
            min_x = std::min(min_x, lx); max_x = std::max(max_x, lx);
            min_y = std::min(min_y, ly); max_y = std::max(max_y, ly);
            min_z = std::min(min_z, lz); max_z = std::max(max_z, lz);
        }

        // Add 8% safety padding on X and Y to eliminate cascade boundary cuts and penumbra clipping
        const float span_x = max_x - min_x;
        const float span_y = max_y - min_y;
        const float pad_x = span_x * 0.08f;
        const float pad_y = span_y * 0.08f;
        min_x -= pad_x;
        max_x += pad_x;
        min_y -= pad_y;
        max_y += pad_y;

        // Extend Z to catch shadow casters behind and above the frustum.
        min_z -= 100.0f;
        max_z += 50.0f;

        // Snap to texel boundaries to suppress shimmering.
        const float width = max_x - min_x;
        const float height = max_y - min_y;
        const float texel_size = std::max(width, height) / static_cast<float>(shadow_res);
        min_x = std::floor(min_x / texel_size) * texel_size;
        min_y = std::floor(min_y / texel_size) * texel_size;
        max_x = min_x + std::ceil(width  / texel_size) * texel_size;
        max_y = min_y + std::ceil(height / texel_size) * texel_size;

        const auto proj_mat = ortho_lh_01(min_x, max_x, min_y, max_y, min_z, max_z);
        constants.light_view_proj[ci] = mat_mul(view_mat, proj_mat);
        constants.cascade_splits[ci] = cascade_far[ci];
        constants.cascade_texel_world[ci] = texel_size;
        constants.cascade_light_range[ci] = max_z - min_z;
    }

    // Sky visibility orthographic top-down: 160x160 centered forward of camera
    constexpr float kSkyExtent = 160.0f;
    constexpr float kSkyHalfExtent = kSkyExtent * 0.5f;
    const hikari::Vec3 cam_pos = view.camera_position;
    const float fx = view.inverse_view.m[2][0];
    const float fz = view.inverse_view.m[2][2];
    const float f_len = std::sqrt(fx * fx + fz * fz);
    const float off_x = f_len > 1e-3f ? (fx / f_len) * 20.0f : 0.0f;
    const float off_z = f_len > 1e-3f ? (fz / f_len) * 20.0f : 0.0f;
    const hikari::Vec3 sky_eye(cam_pos.x + off_x, 300.0f, cam_pos.z + off_z);
    const auto sky_view = look_along(sky_eye, hikari::Vec3(0.0f, -1.0f, 0.0f),
                                     hikari::Vec3(0.0f, 0.0f, 1.0f));
    const auto sky_proj = ortho_lh_01(-kSkyHalfExtent, kSkyHalfExtent,
                                      -kSkyHalfExtent, kSkyHalfExtent, 0.0f, 600.0f);
    constants.sky_visibility_matrix = mat_mul(sky_view, sky_proj);

    const auto quality_to_contact = [](int q) -> float {
        switch (q) { case 1: return 8.0f; case 2: return 12.0f; case 3: return 16.0f; default: return 0.0f; }
    };
    const auto quality_to_ssgs = [](int q) -> float {
        switch (q) { case 1: return 16.0f; case 2: return 24.0f; case 3: return 32.0f; default: return 0.0f; }
    };
    const auto quality_to_ssgs_distance = [](int q) -> float {
        switch (q) { case 1: return 3.0f; case 2: return 5.5f; case 3: return 8.0f; default: return 0.0f; }
    };
    const float contact_steps = quality_to_contact(settings.shadows.contact_quality);
    const float ssgs_steps    = quality_to_ssgs(settings.shadows.ssgs_quality);
    const float ssgs_dist     = quality_to_ssgs_distance(settings.shadows.ssgs_quality);
    const float shadow_res_f = static_cast<float>(shadow_res);

    constants.shadow_params0 = {0.0005f, 1.5f, settings.shadows.pcss ? 1.0f : 0.0f, 2.0f};
    constants.shadow_params1 = {settings.shadows.light_radius, 0.5f, 0.3f, ssgs_dist};
    constants.shadow_params2 = {0.3f, 0.5f, static_cast<float>(kCascadeCount), kSkyExtent};
    constants.shadow_params3 = {static_cast<float>(settings.ao.sky_visibility),
                                 1.0f / shadow_res_f, contact_steps, ssgs_steps};

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_.context->Map(shadow_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                        &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context_.context->Unmap(shadow_buffer_.Get(), 0);
    }
}

void Renderer::bind_shadow_constants() {
    if (shadow_buffer_ == nullptr) {
        return;
    }
    ID3D11Buffer* buffers[] = {shadow_buffer_.Get()};
    context_.context->VSSetConstantBuffers(2, 1, buffers);
    context_.context->PSSetConstantBuffers(2, 1, buffers);
}

SkyConstants Renderer::build_sky_constants(const GraphicsSettings& settings,
                                           const FrameView& view) const {
    constexpr float kObserverAltitudeM = 1000.0f;
    constexpr float kMinimumSunAngle = 5.0e-4f;
    constexpr float kMaximumSunAngle = 0.35f;
    SkyConstants constants;
    const float angular_radius =
        settings.sun.angular_radius * std::max(settings.sky.sun_radius, 0.0f);
    constants.sky_params0 = {settings.sky.turbidity, settings.sky.ozone,
                             settings.sky.sun_radiance,
                             std::clamp(angular_radius, kMinimumSunAngle, kMaximumSunAngle)};
    constants.sky_params1 = {
        static_cast<float>(atmosphere_step_count(settings.sky.quality)),
        settings.sky.mie_anisotropy,
        kObserverAltitudeM + std::max(view.camera_position.y, 0.0f),
        std::max(view.time_seconds, 0.0f)};
    constants.sky_params2 = {settings.sky.cloud_coverage, settings.sky.cloud_density,
                             settings.sky.cloud_speed, settings.sky.cloud_turbulence};
    constants.sky_params3 = {settings.sky.moon_phase, settings.sky.moon_intensity,
                             settings.sky.star_brightness,
                             settings.sky.milky_way_intensity};
    return constants;
}

void Renderer::upload_sky_constants(const SkyConstants& constants) {
    if (sky_buffer_ == nullptr) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_.context->Map(sky_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                     &mapped))) {
        throw std::runtime_error("Map sky constants failed");
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context_.context->Unmap(sky_buffer_.Get(), 0);
}

void Renderer::update_sky_constants(const GraphicsSettings& settings, const FrameView& view) {
    upload_sky_constants(build_sky_constants(settings, view));
}

void Renderer::bind_sky_constants() {
    if (sky_buffer_ == nullptr) {
        return;
    }
    ID3D11Buffer* buffers[] = {sky_buffer_.Get()};
    context_.context->PSSetConstantBuffers(4, 1, buffers);
}

void Renderer::update_ibl_constants(const GraphicsSettings& settings) {
    if (ibl_buffer_ == nullptr) {
        return;
    }
    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const std::uint32_t size = sky_capture_resolution(settings.ibl.quality);
    const float last_mip =
        size > 0 ? static_cast<float>(sky_prefilter_mip_count(size)) - 1.0f : 0.0f;
    IblConstants constants;
    constants.ibl_params0 = {settings.ibl.intensity, last_mip,
                             settings.ibl.rotation_degrees * kDegreesToRadians,
                             settings.sky.ambient_intensity};

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_.context->Map(ibl_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                     &mapped))) {
        throw std::runtime_error("Map IBL constants failed");
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context_.context->Unmap(ibl_buffer_.Get(), 0);
}

void Renderer::bind_ibl_constants() {
    if (ibl_buffer_ == nullptr) {
        return;
    }
    ID3D11Buffer* buffers[] = {ibl_buffer_.Get()};
    context_.context->PSSetConstantBuffers(5, 1, buffers);
}

// Every sky entry point samples the transmittance LUT through PointSampler and
// the procedural layers through SkySampler, so both must be bound explicitly
// even when a previous pass happened to leave the same states behind.
void Renderer::bind_sky_samplers() {
    auto* linear_clamp = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
    context_.context->PSSetSamplers(0, 1, &linear_clamp);
    auto* point_clamp = context_.state_manager->get_sampler(core::SamplerPreset::PointClamp);
    context_.context->PSSetSamplers(1, 1, &point_clamp);
    auto* linear_wrap = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(3, 1, &linear_wrap);
}

void Renderer::ensure_sky_textures() {
    if (cloud_texture_.srv != nullptr) {
        return;
    }
    const SkyImage& clouds = cloud_noise_image();
    cloud_texture_ = render::GpuTexture::from_rgba_bytes_mipped(
        context_, clouds.width, clouds.height, clouds.rgba.data());
    const SkyImage& milky_way = milky_way_image();
    milky_way_texture_ = render::GpuTexture::from_rgba_bytes_mipped(
        context_, milky_way.width, milky_way.height, milky_way.rgba.data());
    const SkyImage& moon = moon_albedo_image();
    moon_texture_ = render::GpuTexture::from_rgba_bytes_mipped(
        context_, moon.width, moon.height, moon.rgba.data());
}

void Renderer::draw_shadow_cascades(const GraphicsSettings& settings, const FrameView& view) {
    if (settings.shadows.quality == 0 || shadow_draw_buffer_ == nullptr) {
        return;
    }
    const DepthTarget* shadow_map = targets_.get_depth("ShadowMap");
    if (shadow_map == nullptr || shadow_map->slice_dsvs.empty()) {
        return;
    }

    // We need the shadow constants to get light_view_proj. Read them back from our last update.
    // We store shadow constants as a local copy via a re-compute (simpler than re-mapping).
    // Instead, build the matrices inline from view data the same way update_shadow_constants does.
    // We set up the shadow draw cbuffer per cascade and draw geometry.

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout_vec(std::begin(layout), std::end(layout));
    const ShaderProgram& program =
        shaders_.get("shadow_caster", kShaderSource, "ShadowVS", "ShadowPS", {}, &layout_vec);

    // Re-build cascade matrices (same logic as update_shadow_constants).
    const hikari::Vec3 sun_dir = view.sun_direction.normalize();
    const hikari::Vec3 world_up = std::abs(sun_dir.y) < 0.99f
                                       ? hikari::Vec3(0.0f, 1.0f, 0.0f)
                                       : hikari::Vec3(0.0f, 0.0f, 1.0f);
    constexpr uint32_t kCascadeCount = 4;
    const uint32_t shadow_res = current_shadow_res_ > 0
                                    ? current_shadow_res_
                                    : shadow_resolution_from_quality(settings.shadows.quality);
    const float near_z = std::max(view.near_z, 0.5f);
    const float far_z = std::min(view.far_z, 300.0f);
    constexpr float lambda = 0.85f;
    float cascade_far[kCascadeCount]{};
    float cascade_near[kCascadeCount]{};
    for (uint32_t ci = 0; ci < kCascadeCount; ++ci) {
        const float p = static_cast<float>(ci + 1) / static_cast<float>(kCascadeCount);
        const float c_log = near_z * std::pow(far_z / near_z, p);
        const float c_lin = near_z + (far_z - near_z) * p;
        cascade_far[ci] = c_log * lambda + c_lin * (1.0f - lambda);
        cascade_near[ci] = (ci == 0) ? near_z : cascade_far[ci - 1];
    }
    std::array<std::array<std::array<float, 4>, 4>, 4> light_vp{};
    for (uint32_t ci = 0; ci < kCascadeCount; ++ci) {
        const float n = near_z;
        const float f = cascade_far[ci];
        const float fx = view.projection.m[0][0] > 1.0e-6f ? 1.0f / view.projection.m[0][0] : 1.0f;
        const float fy = view.projection.m[1][1] > 1.0e-6f ? 1.0f / view.projection.m[1][1] : 1.0f;
        const hikari::Vec3 corners_vs[8] = {
            {-fx*n,-fy*n,n},{fx*n,-fy*n,n},{-fx*n,fy*n,n},{fx*n,fy*n,n},
            {-fx*f,-fy*f,f},{fx*f,-fy*f,f},{-fx*f,fy*f,f},{fx*f,fy*f,f},
        };
        const Mat4& inv_v = view.inverse_view;
        hikari::Vec3 corners_ws[8];
        for (int k = 0; k < 8; ++k) {
            const float cx = corners_vs[k].x, cy = corners_vs[k].y, cz = corners_vs[k].z;
            corners_ws[k] = hikari::Vec3(
                cx*inv_v.m[0][0]+cy*inv_v.m[1][0]+cz*inv_v.m[2][0]+inv_v.m[3][0],
                cx*inv_v.m[0][1]+cy*inv_v.m[1][1]+cz*inv_v.m[2][1]+inv_v.m[3][1],
                cx*inv_v.m[0][2]+cy*inv_v.m[1][2]+cz*inv_v.m[2][2]+inv_v.m[3][2]);
        }
        hikari::Vec3 center(0.0f,0.0f,0.0f);
        for (int k=0;k<8;++k) center+=corners_ws[k];
        center=center/8.0f;
        const auto view_mat = look_along(center, sun_dir, world_up);
        float min_x=1e30f,max_x=-1e30f,min_y=1e30f,max_y=-1e30f,min_z=1e30f,max_z=-1e30f;
        for (int k=0;k<8;++k) {
            const auto& ws=corners_ws[k];
            const float lx=ws.x*view_mat[0][0]+ws.y*view_mat[1][0]+ws.z*view_mat[2][0]+view_mat[3][0];
            const float ly=ws.x*view_mat[0][1]+ws.y*view_mat[1][1]+ws.z*view_mat[2][1]+view_mat[3][1];
            const float lz=ws.x*view_mat[0][2]+ws.y*view_mat[1][2]+ws.z*view_mat[2][2]+view_mat[3][2];
            min_x=std::min(min_x,lx);max_x=std::max(max_x,lx);
            min_y=std::min(min_y,ly);max_y=std::max(max_y,ly);
            min_z=std::min(min_z,lz);max_z=std::max(max_z,lz);
        }
        const float span_x = max_x - min_x;
        const float span_y = max_y - min_y;
        const float pad_x = span_x * 0.08f;
        const float pad_y = span_y * 0.08f;
        min_x -= pad_x;
        max_x += pad_x;
        min_y -= pad_y;
        max_y += pad_y;
        min_z -= 100.0f;
        max_z += 50.0f;
        const float width=max_x-min_x,height=max_y-min_y;
        const float texel_size=std::max(width,height)/static_cast<float>(shadow_res);
        min_x=std::floor(min_x/texel_size)*texel_size;
        min_y=std::floor(min_y/texel_size)*texel_size;
        max_x=min_x+std::ceil(width/texel_size)*texel_size;
        max_y=min_y+std::ceil(height/texel_size)*texel_size;
        const auto proj_mat = ortho_lh_01(min_x,max_x,min_y,max_y,min_z,max_z);
        light_vp[ci] = mat_mul(view_mat, proj_mat);
    }

    context_.context->IASetInputLayout(program.input_layout.Get());
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    context_.context->OMSetBlendState(
        context_.state_manager->get_blend(core::BlendPreset::Opaque), nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);
    bind_frame_constants();
    bind_shadow_constants();
    bind_material_constants();
    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(0, 1, &sampler);

    MaterialConstants default_constants{};
    default_constants.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D11ShaderResourceView* white_srv = white_texture_.srv.Get();

    for (uint32_t ci = 0; ci < kCascadeCount && ci < shadow_map->slice_dsvs.size(); ++ci) {
        // Set viewport for shadow map resolution
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(shadow_res);
        vp.Height = static_cast<float>(shadow_res);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_.context->RSSetViewports(1, &vp);

        ID3D11RenderTargetView* null_rtv = nullptr;
        context_.context->OMSetRenderTargets(0, &null_rtv, shadow_map->slice_dsvs[ci].Get());
        context_.context->ClearDepthStencilView(shadow_map->slice_dsvs[ci].Get(),
                                                 D3D11_CLEAR_DEPTH, 1.0f, 0);

        // Upload per-cascade draw constants (b3).
        ShadowDrawConstants draw_consts{};
        draw_consts.matrix = light_vp[ci];
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_.context->Map(shadow_draw_buffer_.Get(), 0,
                                             D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &draw_consts, sizeof(draw_consts));
            context_.context->Unmap(shadow_draw_buffer_.Get(), 0);
        }
        ID3D11Buffer* draw_buf[] = {shadow_draw_buffer_.Get()};
        context_.context->VSSetConstantBuffers(3, 1, draw_buf);
        context_.context->PSSetConstantBuffers(3, 1, draw_buf);

        // Draw shadow casters.
        const auto& casters = view.shadow_casters.empty() ? view.opaque : view.shadow_casters;
        for (const DrawItem& item : casters) {
            if (item.mesh == nullptr || item.skinning == nullptr ||
                item.subset >= item.mesh->subsets.size()) continue;

            item.mesh->bind_to(context_.context.Get());
            item.skinning->bind_vs_to(context_.context.Get(), 8);

            const auto& subset = item.mesh->subsets[item.subset];
            MaterialConstants constants = default_constants;
            ID3D11ShaderResourceView* srv = white_srv;

            if (item.materials != nullptr && subset.material_index < item.materials->size()) {
                const auto& mat = (*item.materials)[subset.material_index];
                constants = material_from_gpu(mat.constants);
                if (item.textures != nullptr && mat.texture_index >= 0 &&
                    static_cast<std::size_t>(mat.texture_index) < item.textures->size() &&
                    (*item.textures)[static_cast<std::size_t>(mat.texture_index)]) {
                    srv = (*item.textures)[static_cast<std::size_t>(mat.texture_index)]->srv.Get();
                }
            }
            update_material_constants(constants);
            context_.context->PSSetShaderResources(8, 1, &srv);
            context_.context->DrawIndexed(subset.index_count, subset.index_start, 0);
        }
    }
}

void Renderer::draw_sky_visibility(const GraphicsSettings& settings, const FrameView& view) {
    if (settings.ao.sky_visibility == 0 || shadow_draw_buffer_ == nullptr) {
        return;
    }
    const Target* sky_vis = targets_.get("SkyVisibility");
    const DepthTarget* sky_depth = targets_.get_depth("SkyVisibilityDepth");
    if (sky_vis == nullptr || sky_depth == nullptr) {
        return;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout_vec(std::begin(layout), std::end(layout));
    const ShaderProgram& program =
        shaders_.get("sky_visibility", kShaderSource, "ShadowVS", "SkyHeightPS", {}, &layout_vec);

    D3D11_VIEWPORT vp{};
    vp.Width = 1024.0f;
    vp.Height = 1024.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_.context->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtv = sky_vis->rtv.Get();
    context_.context->OMSetRenderTargets(1, &rtv, sky_depth->dsv.Get());
    // Clear to a very low value so geometry overwrites it.
    const float clear_val[4] = {-9999.0f, 0.0f, 0.0f, 0.0f};
    context_.context->ClearRenderTargetView(rtv, clear_val);
    context_.context->ClearDepthStencilView(sky_depth->dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Build top-down orthographic matrix matching update_shadow_constants.
    constexpr float kSkyExtent = 160.0f;
    constexpr float kSkyHalfExtent = kSkyExtent * 0.5f;
    const hikari::Vec3 cam_pos = view.camera_position;
    const float fx = view.inverse_view.m[2][0];
    const float fz = view.inverse_view.m[2][2];
    const float f_len = std::sqrt(fx * fx + fz * fz);
    const float off_x = f_len > 1e-3f ? (fx / f_len) * 20.0f : 0.0f;
    const float off_z = f_len > 1e-3f ? (fz / f_len) * 20.0f : 0.0f;
    const hikari::Vec3 sky_eye(cam_pos.x + off_x, 300.0f, cam_pos.z + off_z);
    const auto sky_view = look_along(sky_eye, hikari::Vec3(0.0f, -1.0f, 0.0f),
                                     hikari::Vec3(0.0f, 0.0f, 1.0f));
    const auto sky_proj = ortho_lh_01(-kSkyHalfExtent, kSkyHalfExtent,
                                      -kSkyHalfExtent, kSkyHalfExtent, 0.0f, 600.0f);
    ShadowDrawConstants draw_consts{};
    draw_consts.matrix = mat_mul(sky_view, sky_proj);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_.context->Map(shadow_draw_buffer_.Get(), 0,
                                         D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &draw_consts, sizeof(draw_consts));
        context_.context->Unmap(shadow_draw_buffer_.Get(), 0);
    }
    ID3D11Buffer* draw_buf[] = {shadow_draw_buffer_.Get()};
    context_.context->VSSetConstantBuffers(3, 1, draw_buf);
    context_.context->PSSetConstantBuffers(3, 1, draw_buf);

    context_.context->IASetInputLayout(program.input_layout.Get());
    context_.context->VSSetShader(program.vertex.Get(), nullptr, 0);
    context_.context->PSSetShader(program.pixel.Get(), nullptr, 0);
    context_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    context_.context->OMSetBlendState(
        context_.state_manager->get_blend(core::BlendPreset::Opaque), nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::ReadWriteLessEqual), 1);
    bind_frame_constants();
    bind_shadow_constants();
    bind_material_constants();

    auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearWrap);
    context_.context->PSSetSamplers(0, 1, &sampler);

    MaterialConstants default_constants{};
    default_constants.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D11ShaderResourceView* white_srv = white_texture_.srv.Get();

    const auto& casters = view.shadow_casters.empty() ? view.opaque : view.shadow_casters;
    for (const DrawItem& item : casters) {
        if (item.mesh == nullptr || item.skinning == nullptr ||
            item.subset >= item.mesh->subsets.size()) continue;
        item.mesh->bind_to(context_.context.Get());
        item.skinning->bind_vs_to(context_.context.Get(), 8);

        const auto& subset = item.mesh->subsets[item.subset];
        MaterialConstants constants = default_constants;
        ID3D11ShaderResourceView* srv = white_srv;

        if (item.materials != nullptr && subset.material_index < item.materials->size()) {
            const auto& mat = (*item.materials)[subset.material_index];
            constants = material_from_gpu(mat.constants);
            if (item.textures != nullptr && mat.texture_index >= 0 &&
                static_cast<std::size_t>(mat.texture_index) < item.textures->size() &&
                (*item.textures)[static_cast<std::size_t>(mat.texture_index)]) {
                srv = (*item.textures)[static_cast<std::size_t>(mat.texture_index)]->srv.Get();
            }
        }
        update_material_constants(constants);
        context_.context->PSSetShaderResources(8, 1, &srv);
        context_.context->DrawIndexed(subset.index_count, subset.index_start, 0);
    }
    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
    (void)view;
}

void Renderer::draw_sun_transmittance(const GraphicsSettings& settings, const FrameView& view) {
    (void)view;
    if (!settings.sky.enabled) {
        return;
    }
    const Target* target = targets_.get("SunTransmittance");
    if (target == nullptr) {
        return;
    }

    // The LUT is sampled by the lighting pass, so render() releases the SRV
    // slots at the end of every frame.
    ID3D11RenderTargetView* views[] = {target->rtv.Get()};
    context_.context->OMSetRenderTargets(1, views, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(target->width);
    vp.Height = static_cast<float>(target->height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_.context->RSSetViewports(1, &vp);

    const ShaderProgram& program = shaders_.get(
        "sun_transmittance", kShaderSource, "FullscreenVS", "SunTransmittancePS", {}, nullptr);
    context_.context->OMSetBlendState(context_.state_manager->get_blend(core::BlendPreset::Opaque),
                                      nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_frame_constants();
    bind_sky_constants();
    draw_fullscreen(program);

    // The LUT is sampled by the sky capture and the lighting pass later in the
    // frame; release the render target binding so those reads are well defined.
    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
    context_.set_viewport(static_cast<float>(width_), static_cast<float>(height_));
}

void Renderer::draw_shadow_mask(const GraphicsSettings& settings, const FrameView& view) {
    (void)view;
    if (settings.shadows.quality == 0) {
        return;
    }
    const Target* shadow_mask = targets_.get("ShadowMask");
    const DepthTarget* depth = targets_.get_depth("Depth");
    const DepthTarget* shadow_map = targets_.get_depth("ShadowMap");
    if (shadow_mask == nullptr || depth == nullptr || shadow_map == nullptr) {
        return;
    }

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_.context->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtv = shadow_mask->rtv.Get();
    context_.context->OMSetRenderTargets(1, &rtv, nullptr);

    const auto defines = shadow_defines(settings);
    const ShaderProgram& program =
        shaders_.get("shadow_mask", kShaderSource, "FullscreenVS", "ShadowMaskPS", defines, nullptr);

    // t0..t4: GBuffer, t5: depth, t7: shadow map array, t9: sky visibility, t10: (unused here)
    ID3D11ShaderResourceView* gbuf[6] = {
        targets_.get("GB0")->srv.Get(), targets_.get("GB1")->srv.Get(),
        targets_.get("GB2")->srv.Get(), targets_.get("GB3")->srv.Get(),
        targets_.get("GB4")->srv.Get(), depth->srv.Get()};
    context_.context->PSSetShaderResources(0, 6, gbuf);
    ID3D11ShaderResourceView* shadow_srv = shadow_map->srv.Get();
    context_.context->PSSetShaderResources(7, 1, &shadow_srv);

    if (settings.ao.sky_visibility > 0) {
        const Target* sky_vis = targets_.get("SkyVisibility");
        if (sky_vis != nullptr) {
            ID3D11ShaderResourceView* sky_srv = sky_vis->srv.Get();
            context_.context->PSSetShaderResources(9, 1, &sky_srv);
        }
    }

    auto* linear_sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
    auto* point_sampler  = context_.state_manager->get_sampler(core::SamplerPreset::PointClamp);
    auto* cmp_sampler    = context_.state_manager->get_sampler(core::SamplerPreset::ComparisonLinearClamp);
    context_.context->PSSetSamplers(0, 1, &linear_sampler);
    context_.context->PSSetSamplers(1, 1, &point_sampler);
    context_.context->PSSetSamplers(2, 1, &cmp_sampler);
    context_.context->OMSetBlendState(
        context_.state_manager->get_blend(core::BlendPreset::Opaque), nullptr, 0xFFFFFFFF);
    context_.context->OMSetDepthStencilState(
        context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
    context_.context->RSSetState(
        context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
    bind_frame_constants();
    bind_shadow_constants();
    draw_fullscreen(program);

    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
    ID3D11ShaderResourceView* null_srvs[11] = {};
    context_.context->PSSetShaderResources(0, 11, null_srvs);
}

void Renderer::draw_shadow_blur(const GraphicsSettings& settings, const FrameView& view) {
    (void)view;
    if (!settings.shadows.blur || settings.shadows.quality == 0) {
        return;
    }
    const Target* shadow_mask = targets_.get("ShadowMask");
    const Target* shadow_blur = targets_.get("ShadowMaskBlur");
    const DepthTarget* depth  = targets_.get_depth("Depth");
    if (shadow_mask == nullptr || shadow_blur == nullptr || depth == nullptr) {
        return;
    }

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_.context->RSSetViewports(1, &vp);

    auto run_pass = [&](const Target* input, const Target* output, int dir) {
        // Ensure output is not bound as SRV before binding as RTV
        ID3D11ShaderResourceView* null_srv = nullptr;
        context_.context->PSSetShaderResources(10, 1, &null_srv);

        ID3D11RenderTargetView* rtv = output->rtv.Get();
        context_.context->OMSetRenderTargets(1, &rtv, nullptr);

        std::vector<ShaderDefine> blur_defines = shadow_defines(settings);
        blur_defines.push_back({"SHADOW_BLUR_DIR", std::to_string(dir)});
        const ShaderProgram& program =
            shaders_.get(dir == 0 ? "shadow_blur_h" : "shadow_blur_v",
                          kShaderSource, "FullscreenVS", "ShadowBlurPS", blur_defines, nullptr);

        ID3D11ShaderResourceView* input_srv = input->srv.Get();
        context_.context->PSSetShaderResources(10, 1, &input_srv);
        ID3D11ShaderResourceView* depth_srv = depth->srv.Get();
        context_.context->PSSetShaderResources(5, 1, &depth_srv);
        const Target* gb1 = targets_.get("GB1");
        if (gb1 != nullptr) {
            ID3D11ShaderResourceView* gb1_srv = gb1->srv.Get();
            context_.context->PSSetShaderResources(1, 1, &gb1_srv);
        }
        auto* sampler = context_.state_manager->get_sampler(core::SamplerPreset::LinearClamp);
        context_.context->PSSetSamplers(0, 1, &sampler);
        context_.context->OMSetBlendState(
            context_.state_manager->get_blend(core::BlendPreset::Opaque), nullptr, 0xFFFFFFFF);
        context_.context->OMSetDepthStencilState(
            context_.state_manager->get_depth(core::DepthPreset::Disabled), 0);
        context_.context->RSSetState(
            context_.state_manager->get_rasterizer(core::RasterizerPreset::CullNoneSolid));
        bind_frame_constants();
        bind_shadow_constants();
        draw_fullscreen(program);

        // Unbind input SRVs
        context_.context->PSSetShaderResources(10, 1, &null_srv);
        ID3D11ShaderResourceView* null_gb1 = nullptr;
        context_.context->PSSetShaderResources(1, 1, &null_gb1);
    };

    // H pass: ShadowMask -> ShadowMaskBlur
    run_pass(shadow_mask, shadow_blur, 0);
    // V pass: ShadowMaskBlur -> ShadowMask
    run_pass(shadow_blur, shadow_mask, 1);

    // Unbind output RTV so shadow_mask can be bound as SRV in draw_lighting
    ID3D11RenderTargetView* null_rtv = nullptr;
    context_.context->OMSetRenderTargets(1, &null_rtv, nullptr);
}

} // namespace hikari::gfx
