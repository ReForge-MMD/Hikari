#include "hikari/core/backend.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "hikari/core/log.hpp"
#include "hikari/core/mmd_host.hpp"
#include "hikari/core/ui.hpp"

namespace hikari::core {

std::mutex g_d3d11_mutex;
std::unique_ptr<D3D11Context> g_d3d11;
std::atomic_bool g_dx11_frame_rendered{false};
std::atomic<std::uint64_t> g_draw_indexed_total{0};
std::atomic<std::uint64_t> g_draw_indexed_active{0};
std::array<std::atomic<std::size_t>, 8> g_bound_textures{};

std::mutex g_hwnd_mutex;
std::optional<std::uintptr_t> g_global_hwnd;
std::mutex g_active_effect_mutex;
std::optional<std::uintptr_t> g_active_effect;

namespace {

std::mutex g_transform_mutex;
std::array<float, 16> g_world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
std::array<float, 16> g_view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
std::array<float, 16> g_projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
std::optional<D3D11_VIEWPORT> g_viewport;

std::array<float, 16> multiply(const std::array<float, 16>& a,
                               const std::array<float, 16>& b) {
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[row * 4 + k] * b[k * 4 + col];
            }
            result[row * 4 + col] = sum;
        }
    }
    return result;
}

bool write_matrix(std::vector<std::uint8_t>& data, std::optional<std::size_t> offset,
                  const std::array<float, 16>& matrix) {
    if (!offset || *offset + 64 > data.size()) {
        return false;
    }
    std::memcpy(data.data() + *offset, matrix.data(), 64);
    return true;
}

bool write_vec4(std::vector<std::uint8_t>& data, std::optional<std::size_t> offset,
                const std::array<float, 4>& value) {
    if (!offset || *offset + 16 > data.size()) {
        return false;
    }
    std::memcpy(data.data() + *offset, value.data(), 16);
    return true;
}

} // namespace

void on_device_created(HWND hwnd) {
    log_msg("[backend] Window registered: hwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(hwnd)));
    {
        std::lock_guard lock(g_hwnd_mutex);
        g_global_hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    }

    std::lock_guard lock(g_d3d11_mutex);
    if (!g_d3d11) {
        try {
            g_d3d11 = std::make_unique<D3D11Context>(hwnd, 0, 0);
            log_msg("[backend] D3D11Context created successfully");
            init_ui(hwnd);
        } catch (const std::exception& error) {
            log_msg(std::string("[backend] Failed to create D3D11Context: ") + error.what());
        }
    } else {
        init_ui(hwnd);
    }
}

void on_device_reset_pre() {
    log_msg("[backend] device reset: clearing bound state");
    for (auto& texture : g_bound_textures) {
        texture.store(0);
    }
}

void on_device_reset_post(std::uint32_t width, std::uint32_t height) {
    std::lock_guard lock(g_d3d11_mutex);
    if (g_d3d11) {
        try {
            g_d3d11->resize(width, height);
        } catch (const std::exception& error) {
            log_msg(std::string("[backend] resize failed: ") + error.what());
        }
    }
}

void update_effect_standard_matrices(HikariEffect& effect) {
    std::array<float, 16> world;
    std::array<float, 16> view;
    std::array<float, 16> projection;
    {
        std::lock_guard lock(g_transform_mutex);
        world = g_world;
        view = g_view;
        projection = g_projection;
    }
    const auto world_view = multiply(world, view);
    const auto world_view_projection = multiply(world_view, projection);

    const auto offsets = effect.std_offsets;
    auto& data = effect.constant_data;
    bool dirty = false;
    dirty |= write_matrix(data, offsets.world, world);
    dirty |= write_matrix(data, offsets.view, view);
    dirty |= write_matrix(data, offsets.proj, projection);
    dirty |= write_matrix(data, offsets.world_view, world_view);
    dirty |= write_matrix(data, offsets.world_view_proj, world_view_projection);

    const float camera_x = -(view[12] * view[0] + view[13] * view[4] + view[14] * view[8]);
    const float camera_y = -(view[12] * view[1] + view[13] * view[5] + view[14] * view[9]);
    const float camera_z = -(view[12] * view[2] + view[13] * view[6] + view[14] * view[10]);
    dirty |= write_vec4(data, offsets.camera_pos, {camera_x, camera_y, camera_z, 1.0f});
    dirty |= write_vec4(data, offsets.light_dir, {0.5f, 1.0f, -0.5f, 0.0f});
    dirty |= write_vec4(data, offsets.mat_diffuse, {1.0f, 1.0f, 1.0f, 1.0f});
    dirty |= write_vec4(data, offsets.mat_ambient, {0.5f, 0.5f, 0.5f, 1.0f});
    dirty |= write_vec4(data, offsets.mat_specular, {0.0f, 0.0f, 0.0f, 1.0f});

    if (dirty) {
        effect.constant_dirty = true;
    }
}

void on_begin_scene() {}

void on_end_scene() {
    {
        std::lock_guard lock(g_d3d11_mutex);
        if (g_d3d11) {
            ++g_d3d11->frame_counter;
        }
    }
    ensure_ui_injected();

    static std::atomic<std::uint64_t> last_fingerprint{0};
    const std::uint64_t fingerprint = get_host_objects_fingerprint();
    if (fingerprint != last_fingerprint.exchange(fingerprint)) {
        {
            std::lock_guard lock(g_assignment_mutex);
            g_assignment_registry.sync_host_objects();
        }
        notify_host_objects_changed();
    }
    drain_deferred_effects();
}

void on_clear(std::uint32_t, std::uint32_t, float, std::uint32_t) {}

void on_set_viewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(w);
    viewport.Height = static_cast<float>(h);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    {
        std::lock_guard lock(g_transform_mutex);
        g_viewport = viewport;
    }
    std::lock_guard lock(g_d3d11_mutex);
    if (g_d3d11) {
        g_d3d11->context->RSSetViewports(1, &viewport);
    }
}

void on_set_texture(std::uint32_t stage, std::size_t texture) {
    if (stage < 8) {
        g_bound_textures[stage].store(texture);
    }
}

void on_set_stream_source(std::uint32_t, std::size_t, std::uint32_t, std::uint32_t) {}

void on_set_indices(std::size_t) {}

bool should_render_dx11() {
    if (!g_effects_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    if (g_has_assigned_effects.load(std::memory_order_relaxed)) {
        return true;
    }
    {
        std::lock_guard lock(g_active_pass_mutex);
        if (g_active_pass) {
            return true;
        }
    }
    {
        std::lock_guard lock(g_active_effect_mutex);
        if (g_active_effect) {
            return true;
        }
    }
    std::lock_guard lock(g_effect_registry_mutex);
    return std::any_of(g_effect_registry.begin(), g_effect_registry.end(),
                       [](const EffectItemInfo& item) { return item.enabled; });
}

} // namespace hikari::core
