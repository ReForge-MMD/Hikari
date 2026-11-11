#include "hikari/gfx/sky_assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace hikari::gfx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }

float smoothstep_edge(float edge0, float edge1, float x) {
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

std::uint32_t mix_bits(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

float lattice_value(int x, int y, int period, int seed) {
    const int wrapped_x = ((x % period) + period) % period;
    const int wrapped_y = ((y % period) + period) % period;
    const std::uint32_t hash =
        mix_bits(static_cast<std::uint32_t>(wrapped_x) * 0x9e3779b1U ^
                 (static_cast<std::uint32_t>(wrapped_y) * 0x85ebca77U) ^
                 (static_cast<std::uint32_t>(seed) * 0xc2b2ae3dU));
    return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
}

// Value noise on a periodic integer lattice, so the field tiles in both axes.
float tile_noise(float u, float v, int period, int seed) {
    const float scaled_x = u * static_cast<float>(period);
    const float scaled_y = v * static_cast<float>(period);
    const int x0 = static_cast<int>(std::floor(scaled_x));
    const int y0 = static_cast<int>(std::floor(scaled_y));
    const float fx = scaled_x - static_cast<float>(x0);
    const float fy = scaled_y - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = lattice_value(x0, y0, period, seed);
    const float b = lattice_value(x0 + 1, y0, period, seed);
    const float c = lattice_value(x0, y0 + 1, period, seed);
    const float d = lattice_value(x0 + 1, y0 + 1, period, seed);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

float tile_fbm(float u, float v, int base_period, int octaves, int seed) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    int period = base_period;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * tile_noise(u, v, period, seed + octave * 977);
        total += amplitude;
        amplitude *= 0.5f;
        period *= 2;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

std::array<float, 3> direction_from_equirect(float u, float v) {
    const float lon = (u - 0.5f) * 2.0f * kPi;
    const float lat = (v - 0.5f) * kPi;
    const float cos_lat = std::cos(lat);
    return {cos_lat * std::cos(lon), std::sin(lat), cos_lat * std::sin(lon)};
}

std::array<float, 3> normalize3(const std::array<float, 3>& value) {
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] +
                                   value[2] * value[2]);
    const float inv = length > 1.0e-6f ? 1.0f / length : 0.0f;
    return {value[0] * inv, value[1] * inv, value[2] * inv};
}

std::array<float, 3> cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

float dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void store_gray(SkyImage& image, std::size_t index, float value) {
    const auto level = static_cast<std::uint8_t>(saturate(value) * 255.0f + 0.5f);
    image.rgba[index + 0] = level;
    image.rgba[index + 1] = level;
    image.rgba[index + 2] = level;
    image.rgba[index + 3] = 255;
}

// Galactic pole picked so the band crosses the default camera view as a
// diagonal strip instead of filling the frame edge on.
std::array<float, 3> band_pole() { return normalize3({0.35f, 0.90f, -0.26f}); }

// Brightest point of the band: projected onto the galactic plane so the bulge
// lands just above the default camera target.
std::array<float, 3> band_core() {
    const std::array<float, 3> pole = band_pole();
    const std::array<float, 3> wanted = normalize3({0.10f, 0.24f, 0.97f});
    const float along = dot3(wanted, pole);
    return normalize3({wanted[0] - pole[0] * along, wanted[1] - pole[1] * along,
                       wanted[2] - pole[2] * along});
}

} // namespace

SkyImage build_cloud_noise_image(std::uint32_t size) {
    SkyImage image{size, size, std::vector<std::uint8_t>(
                                   static_cast<std::size_t>(size) * size * 4)};
    const float inv = 1.0f / static_cast<float>(size);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;
            const float base = tile_fbm(u, v, 4, 6, 1337);
            const std::size_t index =
                (static_cast<std::size_t>(y) * size + x) * 4;
            image.rgba[index + 0] = static_cast<std::uint8_t>(
                saturate((base - 0.5f) * 2.6f + 0.5f) * 255.0f + 0.5f);
            image.rgba[index + 1] = static_cast<std::uint8_t>(
                tile_fbm(u, v, 48, 3, 7717) * 255.0f + 0.5f);
            image.rgba[index + 2] = static_cast<std::uint8_t>(
                tile_fbm(u, v, 2, 2, 4242) * 255.0f + 0.5f);
            image.rgba[index + 3] = 255;
        }
    }
    return image;
}

SkyImage build_milky_way_image(std::uint32_t width, std::uint32_t height) {
    SkyImage image{width, height, std::vector<std::uint8_t>(
                                      static_cast<std::size_t>(width) * height * 4)};
    // Galactic plane chosen for an attractive tilt over the MMD default camera.
    const std::array<float, 3> pole = band_pole();
    const std::array<float, 3> core = band_core();
    const float inv_width = 1.0f / static_cast<float>(width);
    const float inv_height = 1.0f / static_cast<float>(height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv_width;
            const float v = (static_cast<float>(y) + 0.5f) * inv_height;
            const std::array<float, 3> dir = direction_from_equirect(u, v);
            const float plane = dot3(dir, pole);
            const float plane_sq = plane * plane;
            // A bright narrow ridge inside a soft glow, so the band reads as a
            // strip of light rather than a broad grey wash.
            const float ridge = std::exp(-plane_sq / (2.0f * 0.055f * 0.055f));
            const float glow = std::exp(-plane_sq / (2.0f * 0.130f * 0.130f));
            const float band = 0.85f * ridge + 0.40f * glow;
            const std::array<float, 3> in_plane =
                normalize3({dir[0] - pole[0] * plane, dir[1] - pole[1] * plane,
                            dir[2] - pole[2] * plane});
            const float core_angle =
                std::acos(std::clamp(dot3(in_plane, core), -1.0f, 1.0f));
            const float bulge =
                std::exp(-(core_angle * core_angle) / (2.0f * 0.90f * 0.90f));
            const float structure = tile_fbm(u, v, 40, 4, 9001);
            const float dust = tile_fbm(u, v, 110, 4, 6121);
            const float haze = tile_fbm(u, v, 200, 2, 331);
            float brightness = band * (0.78f + 0.44f * structure) *
                                   (0.55f + 0.85f * bulge);
            brightness -= band * smoothstep_edge(0.55f, 0.85f, dust) * 0.45f;
            brightness += band * (haze - 0.5f) * 0.16f;
            const float shaped = std::pow(saturate(brightness), 1.25f);
            store_gray(image, (static_cast<std::size_t>(y) * width + x) * 4, shaped);
        }
    }
    return image;
}

SkyImage build_moon_albedo_image(std::uint32_t size) {
    SkyImage image{size, size, std::vector<std::uint8_t>(
                                   static_cast<std::size_t>(size) * size * 4)};
    const float inv = 1.0f / static_cast<float>(size);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;
            const float maria = smoothstep_edge(0.42f, 0.62f, tile_fbm(u, v, 3, 3, 21));
            const float crater_field = tile_fbm(u, v, 40, 4, 77);
            const float albedo = 0.80f - 0.30f * maria + (crater_field - 0.5f) * 0.18f;
            store_gray(image, (static_cast<std::size_t>(y) * size + x) * 4, albedo);
        }
    }
    return image;
}

const SkyImage& cloud_noise_image() {
    static const SkyImage image = build_cloud_noise_image(512);
    return image;
}

const SkyImage& milky_way_image() {
    static const SkyImage image = build_milky_way_image(1024, 512);
    return image;
}

const SkyImage& moon_albedo_image() {
    static const SkyImage image = build_moon_albedo_image(256);
    return image;
}

std::array<float, 3> milky_way_core_direction() { return band_core(); }

} // namespace hikari::gfx