#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "hikari/core/d3d11.hpp"
#include "hikari/core/effect.hpp"

namespace hikari::core {

extern std::mutex g_d3d11_mutex;
extern std::unique_ptr<D3D11Context> g_d3d11;
extern std::atomic_bool g_dx11_frame_rendered;
extern std::atomic<std::uint64_t> g_draw_indexed_total;
extern std::atomic<std::uint64_t> g_draw_indexed_active;
extern std::array<std::atomic<std::size_t>, 8> g_bound_textures;

extern std::mutex g_hwnd_mutex;
extern std::optional<std::uintptr_t> g_global_hwnd;
extern std::mutex g_active_effect_mutex;
extern std::optional<std::uintptr_t> g_active_effect;

void on_device_created(HWND hwnd);
void on_device_reset_pre();
void on_device_reset_post(std::uint32_t width, std::uint32_t height);

void update_effect_standard_matrices(HikariEffect& effect);

void on_begin_scene();
void on_end_scene();
void on_clear(std::uint32_t flags, std::uint32_t color, float z, std::uint32_t stencil);
void on_set_viewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
void on_set_texture(std::uint32_t stage, std::size_t texture);
void on_set_stream_source(std::uint32_t stream, std::size_t texture, std::uint32_t offset,
                          std::uint32_t stride);
void on_set_indices(std::size_t indices);
bool should_render_dx11();

} // namespace hikari::core
