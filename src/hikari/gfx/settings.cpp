#include "hikari/gfx/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace hikari::gfx {
namespace {

void put_string(std::ostringstream& out, const char* key, const std::string& value, bool comma) {
    out << '"' << key << "\":\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\';
        }
        out << c;
    }
    out << '"';
    if (comma) {
        out << ',';
    }
}

void put_float(std::ostringstream& out, const char* key, float value, bool comma) {
    out << '"' << key << "\":" << value;
    if (comma) {
        out << ',';
    }
}

void put_int(std::ostringstream& out, const char* key, int value, bool comma) {
    out << '"' << key << "\":" << value;
    if (comma) {
        out << ',';
    }
}

void put_bool(std::ostringstream& out, const char* key, bool value, bool comma) {
    out << '"' << key << "\":" << (value ? "true" : "false");
    if (comma) {
        out << ',';
    }
}

void put_vec3(std::ostringstream& out, const char* key, const Vec3& value, bool comma) {
    out << '"' << key << "\":[" << value.x << ',' << value.y << ',' << value.z << ']';
    if (comma) {
        out << ',';
    }
}

using Values = std::unordered_map<std::string, std::string>;

bool parse_flat_json(std::string_view text, Values& values) {
    std::size_t i = 0;
    const auto skip = [&] {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
                                   text[i] == '\r')) {
            ++i;
        }
    };
    skip();
    if (i >= text.size() || text[i] != '{') {
        return false;
    }
    ++i;
    while (true) {
        skip();
        if (i >= text.size()) {
            return false;
        }
        if (text[i] == '}') {
            ++i;
            break;
        }
        if (text[i] != '"') {
            return false;
        }
        ++i;
        std::string key;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
            }
            key.push_back(text[i]);
            ++i;
        }
        if (i >= text.size()) {
            return false;
        }
        ++i;
        skip();
        if (i >= text.size() || text[i] != ':') {
            return false;
        }
        ++i;
        skip();
        std::string value;
        if (i < text.size() && text[i] == '"') {
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    ++i;
                }
                value.push_back(text[i]);
                ++i;
            }
            if (i >= text.size()) {
                return false;
            }
            ++i;
        } else if (i < text.size() && text[i] == '[') {
            const std::size_t start = i;
            while (i < text.size() && text[i] != ']') {
                ++i;
            }
            if (i >= text.size()) {
                return false;
            }
            ++i;
            value.assign(text.substr(start, i - start));
        } else {
            const std::size_t start = i;
            while (i < text.size() && text[i] != ',' && text[i] != '}') {
                ++i;
            }
            value.assign(text.substr(start, i - start));
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                                      value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
        }
        values[std::move(key)] = std::move(value);
        skip();
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        return false;
    }
    return true;
}

int read_int(const Values& values, const char* key, int fallback) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    char* end = nullptr;
    const long value = std::strtol(it->second.c_str(), &end, 10);
    return end == it->second.c_str() ? fallback : static_cast<int>(value);
}

float read_float(const Values& values, const char* key, float fallback) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    char* end = nullptr;
    const float value = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : value;
}

bool read_bool(const Values& values, const char* key, bool fallback) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    return it->second == "true" || it->second == "1";
}

std::string read_string(const Values& values, const char* key, const std::string& fallback) {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

Vec3 read_vec3(const Values& values, const char* key, const Vec3& fallback) {
    const auto it = values.find(key);
    if (it == values.end() || it->second.size() < 5 || it->second.front() != '[') {
        return fallback;
    }
    float components[3] = {fallback.x, fallback.y, fallback.z};
    const char* cursor = it->second.c_str() + 1;
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        components[i] = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        cursor = end;
        while (*cursor == ',' || *cursor == ' ') {
            ++cursor;
        }
    }
    return Vec3(components[0], components[1], components[2]);
}

template <typename T>
void mix(std::uint64_t& hash, T value) {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

} // namespace

int GraphicsSettings::validate() {
    int changes = 0;
    const auto clamp_int = [&changes](int& value, int low, int high) {
        const int next = std::clamp(value, low, high);
        if (next != value) {
            value = next;
            ++changes;
        }
    };
    const auto clamp_float = [&changes](float& value, float low, float high) {
        if (!std::isfinite(value)) {
            value = low;
            ++changes;
            return;
        }
        const float next = std::clamp(value, low, high);
        if (next != value) {
            value = next;
            ++changes;
        }
    };

    clamp_int(sun.light_mode, 0, 2);
    clamp_float(sun.intensity, 0.0f, 100.0f);
    clamp_float(sun.color_temperature, 1000.0f, 20000.0f);
    clamp_float(sun.angular_radius, 0.0f, 0.1f);

    clamp_int(shadows.quality, 0, 4);
    clamp_float(shadows.light_radius, 0.001f, 0.1f);
    clamp_int(shadows.contact_quality, 0, 3);
    clamp_int(shadows.ssgs_quality, 0, 3);

    clamp_float(sky.time_of_day, 0.0f, 24.0f);
    if (sky.time_of_day >= 24.0f) {
        sky.time_of_day = 0.0f;
    }
    clamp_float(sky.turbidity, 0.0f, 10.0f);
    clamp_float(sky.ozone, 0.0f, 5.0f);
    clamp_float(sky.sun_radius, 0.0f, 20.0f);
    clamp_float(sky.cloud_coverage, 0.0f, 1.0f);
    clamp_float(sky.cloud_speed, 0.0f, 10.0f);
    clamp_float(sky.star_brightness, 0.0f, 10.0f);
    clamp_float(sky.moon_intensity, 0.0f, 10.0f);
    clamp_float(sky.ambient_intensity, 0.0f, 10.0f);

    clamp_int(ibl.quality, 0, 2);
    clamp_float(ibl.intensity, 0.0f, 10.0f);
    clamp_float(ibl.rotation_degrees, -180.0f, 180.0f);

    clamp_int(fog.enable, 0, 3);
    clamp_int(fog.quality, 0, 3);
    clamp_int(fog.debug, 0, 4);
    clamp_float(fog.density, 0.0f, 1.0f);
    clamp_float(fog.height, -1000.0f, 1000.0f);
    clamp_float(fog.falloff, 0.0f, 10.0f);
    clamp_float(fog.ground_density, 0.0f, 1.0f);
    clamp_float(fog.ground_height, -1000.0f, 1000.0f);
    clamp_float(fog.ground_falloff, 0.0f, 10.0f);
    clamp_float(fog.start_distance, 0.0f, 10000.0f);
    clamp_float(fog.cutoff_distance, 0.0f, 100000.0f);
    clamp_float(fog.max_opacity, 0.0f, 1.0f);
    clamp_float(fog.sun_intensity, 0.0f, 100.0f);
    clamp_float(fog.mie_phase, -1.0f, 1.0f);

    clamp_float(brdf.specular_sin_alpha, 0.0f, 1.0f);

    clamp_float(skin.backlit_strength, 0.0f, 10.0f);
    clamp_float(skin.backlit_g, -0.99f, 0.99f);
    clamp_float(skin.backlit_ior, 1.0f, 3.0f);
    clamp_float(skin.spec_lobe0_scale, 0.0f, 10.0f);
    clamp_float(skin.spec_lobe0_add, 0.0f, 1.0f);
    clamp_float(skin.spec_lobe1_scale, 0.0f, 10.0f);
    clamp_float(skin.spec_lobe1_add, 0.0f, 1.0f);
    clamp_float(skin.spec_lobe0_mix, 0.0f, 1.0f);
    clamp_float(skin.spec_lobe1_mix, 0.0f, 1.0f);
    clamp_float(skin.sss_mfp_scale, 0.0f, 10.0f);
    clamp_int(skin.ssss_quality, 0, 1);

    clamp_int(toon.enable, 0, 2);
    clamp_int(toon.diffusion_quality, 256, 8192);
    clamp_float(toon.shadow_hue, -180.0f, 180.0f);
    clamp_float(toon.shadow_warmth, 0.0f, 1.0f);
    clamp_float(toon.autotint_threshold, 0.0f, 1.0f);
    clamp_float(toon.autotint_strength, 0.0f, 1.0f);
    clamp_float(toon.self_power_shadow, 0.0f, 4.0f);
    clamp_float(toon.rim_strength, 0.0f, 10.0f);
    clamp_float(toon.rim_power, 0.0f, 20.0f);

    clamp_int(ao.type, 0, 2);
    clamp_int(ao.quality, 0, 6);
    clamp_int(ao.sky_visibility, 0, 2);
    clamp_float(ao.radius, 0.0f, 10.0f);
    clamp_float(ao.intensity, 0.0f, 10.0f);

    clamp_int(ssr.quality, 0, 4);
    clamp_int(ssr.glass_refraction, 0, 1);
    clamp_float(ssr.water_surface_y, -10000.0f, 10000.0f);
    clamp_float(ssr.caustics_intensity, 0.0f, 100.0f);

    clamp_int(gi.enable, 0, 3);
    clamp_int(gi.quality, 1, 4);
    clamp_int(gi.filter_radius, 1, 64);
    clamp_int(gi.prefilter_radius, 1, 4);
    clamp_float(gi.sample_radius, 0.0f, 10000.0f);
    clamp_float(gi.z_thickness, 0.0f, 100.0f);
    clamp_float(gi.il_amount, 0.0f, 100.0f);
    clamp_float(gi.ao_amount, 0.0f, 10.0f);
    clamp_float(gi.bounce_weight, 0.0f, 1.0f);
    clamp_float(gi.filter_sensitivity, 0.0f, 10.0f);

    clamp_float(camera.fstop, 0.1f, 128.0f);
    clamp_float(camera.focal_length, 1.0f, 2000.0f);
    clamp_float(camera.focal_distance, 0.0f, 100000.0f);
    clamp_float(camera.focal_region, 0.0f, 1000.0f);
    clamp_int(camera.blade_count, 3, 16);
    clamp_float(camera.measure_mode, 0.0f, 1.0f);
    clamp_float(camera.vignette, 0.0f, 10.0f);
    clamp_float(camera.dispersion, 0.0f, 10.0f);
    clamp_float(camera.dispersion_radius, 0.0f, 10.0f);

    clamp_int(bloom.bokeh_mode, 0, 2);
    clamp_int(bloom.eye_adaptation, 0, 2);
    clamp_int(bloom.bloom_mode, 0, 4);
    clamp_int(bloom.bloom_quality, 256, 8192);
    clamp_int(bloom.bloom_spectral_mode, 0, 3);
    clamp_int(bloom.halation_mode, 0, 3);
    clamp_int(bloom.flare_mode, 0, 3);
    clamp_int(bloom.star_mode, 0, 4);
    clamp_int(bloom.dispersion_mode, 0, 3);
    clamp_float(bloom.threshold, 0.0f, 100.0f);
    clamp_float(bloom.radius, 0.0f, 100.0f);
    clamp_float(bloom.intensity, 0.0f, 100.0f);

    clamp_float(post.sharpen_intensity, 0.0f, 1.0f);
    clamp_float(post.grain_intensity, 0.0f, 1.0f);
    clamp_float(post.exposure, 0.0f, 100.0f);
    clamp_float(post.contrast, -1.0f, 1.0f);
    clamp_float(post.saturation, -1.0f, 1.0f);
    clamp_float(post.gamma, 0.1f, 10.0f);
    clamp_float(post.vibrance, -1.0f, 1.0f);
    clamp_float(post.hue, -180.0f, 180.0f);
    clamp_float(post.white_balance_temperature, -100.0f, 100.0f);
    clamp_float(post.white_balance_tint, -100.0f, 100.0f);
    return changes;
}

std::uint64_t GraphicsSettings::permutation_key() const {
    std::uint64_t hash = 1469598103934665603ull;
    mix(hash, static_cast<int>(renderer));
    mix(hash, sun.light_mode);
    mix(hash, sun.specular_disk);
    mix(hash, sun.limb_darkening);
    mix(hash, sun.sky_coupling);
    mix(hash, sun.auto_cct);
    mix(hash, shadows.quality);
    mix(hash, shadows.pcss);
    mix(hash, shadows.blur);
    mix(hash, shadows.contact_quality);
    mix(hash, shadows.ssgs_quality);
    mix(hash, ibl.quality);
    mix(hash, fog.enable);
    mix(hash, fog.quality);
    mix(hash, fog.debug);
    mix(hash, fog.cloud_shadow);
    mix(hash, static_cast<int>(brdf.diffuse_type));
    mix(hash, static_cast<int>(brdf.fresnel_type));
    mix(hash, brdf.cloth_charlie);
    mix(hash, skin.ssss_quality);
    mix(hash, skin.ssss_texspace);
    mix(hash, toon.enable);
    mix(hash, ao.type);
    mix(hash, ao.sky_visibility);
    mix(hash, ssr.quality);
    mix(hash, ssr.debug);
    mix(hash, ssr.glass_refraction);
    mix(hash, gi.enable);
    mix(hash, gi.quality);
    mix(hash, gi.bounces);
    mix(hash, gi.specular_gi);
    mix(hash, bloom.bokeh_mode);
    mix(hash, bloom.bloom_mode);
    mix(hash, bloom.bloom_spectral_mode);
    mix(hash, bloom.halation_mode);
    mix(hash, bloom.flare_mode);
    mix(hash, bloom.star_mode);
    mix(hash, bloom.dispersion_mode);
    mix(hash, static_cast<int>(post.tonemap));
    mix(hash, post.aa_quality);
    mix(hash, post.motion_blur);
    mix(hash, post.sharpen);
    mix(hash, post.grain);
    mix(hash, static_cast<int>(debug.view));
    return hash;
}

std::string GraphicsSettings::to_json() const {
    std::ostringstream out;
    out << '{';
    put_int(out, "renderer", static_cast<int>(renderer), true);
    put_int(out, "preset", static_cast<int>(preset), true);
    put_bool(out, "wireframe", wireframe, true);
    put_int(out, "sun.light_mode", sun.light_mode, true);
    put_bool(out, "sun.specular_disk", sun.specular_disk, true);
    put_bool(out, "sun.backlight_translucency", sun.backlight_translucency, true);
    put_bool(out, "sun.limb_darkening", sun.limb_darkening, true);
    put_bool(out, "sun.hair_transmission", sun.hair_transmission, true);
    put_bool(out, "sun.sky_coupling", sun.sky_coupling, true);
    put_bool(out, "sun.auto_cct", sun.auto_cct, true);
    put_bool(out, "sun.corona", sun.corona, true);
    put_float(out, "sun.intensity", sun.intensity, true);
    put_float(out, "sun.color_temperature", sun.color_temperature, true);
    put_float(out, "sun.angular_radius", sun.angular_radius, true);
    put_int(out, "shadows.quality", shadows.quality, true);
    put_bool(out, "shadows.pcss", shadows.pcss, true);
    put_float(out, "shadows.light_radius", shadows.light_radius, true);
    put_bool(out, "shadows.blur", shadows.blur, true);
    put_int(out, "shadows.contact_quality", shadows.contact_quality, true);
    put_int(out, "shadows.ssgs_quality", shadows.ssgs_quality, true);
    put_float(out, "sky.time_of_day", sky.time_of_day, true);
    put_float(out, "sky.turbidity", sky.turbidity, true);
    put_float(out, "sky.ozone", sky.ozone, true);
    put_float(out, "sky.sun_radius", sky.sun_radius, true);
    put_float(out, "sky.cloud_coverage", sky.cloud_coverage, true);
    put_float(out, "sky.cloud_speed", sky.cloud_speed, true);
    put_float(out, "sky.star_brightness", sky.star_brightness, true);
    put_float(out, "sky.moon_intensity", sky.moon_intensity, true);
    put_float(out, "sky.ambient_intensity", sky.ambient_intensity, true);
    put_vec3(out, "sky.ambient_color", sky.ambient_color, true);
    put_int(out, "ibl.quality", ibl.quality, true);
    put_float(out, "ibl.intensity", ibl.intensity, true);
    put_float(out, "ibl.rotation_degrees", ibl.rotation_degrees, true);
    put_string(out, "ibl.custom_path", ibl.custom_path, true);
    put_int(out, "fog.enable", fog.enable, true);
    put_int(out, "fog.quality", fog.quality, true);
    put_int(out, "fog.debug", fog.debug, true);
    put_bool(out, "fog.cloud_shadow", fog.cloud_shadow, true);
    put_float(out, "fog.density", fog.density, true);
    put_float(out, "fog.height", fog.height, true);
    put_float(out, "fog.falloff", fog.falloff, true);
    put_float(out, "fog.ground_density", fog.ground_density, true);
    put_float(out, "fog.ground_height", fog.ground_height, true);
    put_float(out, "fog.ground_falloff", fog.ground_falloff, true);
    put_float(out, "fog.start_distance", fog.start_distance, true);
    put_float(out, "fog.cutoff_distance", fog.cutoff_distance, true);
    put_float(out, "fog.max_opacity", fog.max_opacity, true);
    put_vec3(out, "fog.color", fog.color, true);
    put_vec3(out, "fog.sun_color", fog.sun_color, true);
    put_float(out, "fog.sun_intensity", fog.sun_intensity, true);
    put_float(out, "fog.mie_phase", fog.mie_phase, true);
    put_int(out, "brdf.diffuse_type", static_cast<int>(brdf.diffuse_type), true);
    put_float(out, "brdf.specular_sin_alpha", brdf.specular_sin_alpha, true);
    put_int(out, "brdf.fresnel_type", static_cast<int>(brdf.fresnel_type), true);
    put_bool(out, "brdf.cloth_charlie", brdf.cloth_charlie, true);
    put_float(out, "skin.backlit_strength", skin.backlit_strength, true);
    put_float(out, "skin.backlit_g", skin.backlit_g, true);
    put_float(out, "skin.backlit_ior", skin.backlit_ior, true);
    put_float(out, "skin.spec_lobe0_scale", skin.spec_lobe0_scale, true);
    put_float(out, "skin.spec_lobe0_add", skin.spec_lobe0_add, true);
    put_float(out, "skin.spec_lobe1_scale", skin.spec_lobe1_scale, true);
    put_float(out, "skin.spec_lobe1_add", skin.spec_lobe1_add, true);
    put_float(out, "skin.spec_lobe0_mix", skin.spec_lobe0_mix, true);
    put_float(out, "skin.spec_lobe1_mix", skin.spec_lobe1_mix, true);
    put_float(out, "skin.sss_mfp_scale", skin.sss_mfp_scale, true);
    put_int(out, "skin.ssss_quality", skin.ssss_quality, true);
    put_bool(out, "skin.ssss_texspace", skin.ssss_texspace, true);
    put_int(out, "toon.enable", toon.enable, true);
    put_int(out, "toon.diffusion_quality", toon.diffusion_quality, true);
    put_vec3(out, "toon.shadow_mul", toon.shadow_mul, true);
    put_vec3(out, "toon.shadow_add", toon.shadow_add, true);
    put_float(out, "toon.shadow_hue", toon.shadow_hue, true);
    put_float(out, "toon.shadow_warmth", toon.shadow_warmth, true);
    put_vec3(out, "toon.autotint_color", toon.autotint_color, true);
    put_float(out, "toon.autotint_threshold", toon.autotint_threshold, true);
    put_float(out, "toon.autotint_strength", toon.autotint_strength, true);
    put_float(out, "toon.self_power_shadow", toon.self_power_shadow, true);
    put_float(out, "toon.rim_strength", toon.rim_strength, true);
    put_float(out, "toon.rim_power", toon.rim_power, true);
    put_int(out, "ao.type", ao.type, true);
    put_int(out, "ao.quality", ao.quality, true);
    put_int(out, "ao.sky_visibility", ao.sky_visibility, true);
    put_float(out, "ao.radius", ao.radius, true);
    put_float(out, "ao.intensity", ao.intensity, true);
    put_int(out, "ssr.quality", ssr.quality, true);
    put_bool(out, "ssr.debug", ssr.debug, true);
    put_int(out, "ssr.glass_refraction", ssr.glass_refraction, true);
    put_bool(out, "ssr.water_caustics", ssr.water_caustics, true);
    put_float(out, "ssr.water_surface_y", ssr.water_surface_y, true);
    put_float(out, "ssr.caustics_intensity", ssr.caustics_intensity, true);
    put_int(out, "gi.enable", gi.enable, true);
    put_int(out, "gi.quality", gi.quality, true);
    put_int(out, "gi.filter_radius", gi.filter_radius, true);
    put_int(out, "gi.prefilter_radius", gi.prefilter_radius, true);
    put_float(out, "gi.sample_radius", gi.sample_radius, true);
    put_float(out, "gi.z_thickness", gi.z_thickness, true);
    put_float(out, "gi.il_amount", gi.il_amount, true);
    put_float(out, "gi.ao_amount", gi.ao_amount, true);
    put_bool(out, "gi.bounces", gi.bounces, true);
    put_float(out, "gi.bounce_weight", gi.bounce_weight, true);
    put_float(out, "gi.filter_sensitivity", gi.filter_sensitivity, true);
    put_bool(out, "gi.specular_gi", gi.specular_gi, true);
    put_float(out, "camera.fstop", camera.fstop, true);
    put_float(out, "camera.focal_length", camera.focal_length, true);
    put_float(out, "camera.focal_distance", camera.focal_distance, true);
    put_float(out, "camera.focal_region", camera.focal_region, true);
    put_int(out, "camera.blade_count", camera.blade_count, true);
    put_float(out, "camera.measure_mode", camera.measure_mode, true);
    put_float(out, "camera.vignette", camera.vignette, true);
    put_float(out, "camera.dispersion", camera.dispersion, true);
    put_float(out, "camera.dispersion_radius", camera.dispersion_radius, true);
    put_int(out, "bloom.bokeh_mode", bloom.bokeh_mode, true);
    put_bool(out, "bloom.cat_eye", bloom.cat_eye, true);
    put_int(out, "bloom.eye_adaptation", bloom.eye_adaptation, true);
    put_int(out, "bloom.bloom_mode", bloom.bloom_mode, true);
    put_int(out, "bloom.bloom_quality", bloom.bloom_quality, true);
    put_int(out, "bloom.bloom_spectral_mode", bloom.bloom_spectral_mode, true);
    put_int(out, "bloom.halation_mode", bloom.halation_mode, true);
    put_int(out, "bloom.flare_mode", bloom.flare_mode, true);
    put_int(out, "bloom.star_mode", bloom.star_mode, true);
    put_int(out, "bloom.dispersion_mode", bloom.dispersion_mode, true);
    put_float(out, "bloom.threshold", bloom.threshold, true);
    put_float(out, "bloom.radius", bloom.radius, true);
    put_float(out, "bloom.intensity", bloom.intensity, true);
    put_int(out, "post.tonemap", static_cast<int>(post.tonemap), true);
    put_int(out, "post.aa_quality", post.aa_quality, true);
    put_int(out, "post.motion_blur", post.motion_blur, true);
    put_bool(out, "post.sharpen", post.sharpen, true);
    put_float(out, "post.sharpen_intensity", post.sharpen_intensity, true);
    put_bool(out, "post.grain", post.grain, true);
    put_float(out, "post.grain_intensity", post.grain_intensity, true);
    put_float(out, "post.exposure", post.exposure, true);
    put_float(out, "post.contrast", post.contrast, true);
    put_float(out, "post.saturation", post.saturation, true);
    put_float(out, "post.gamma", post.gamma, true);
    put_float(out, "post.vibrance", post.vibrance, true);
    put_float(out, "post.hue", post.hue, true);
    put_float(out, "post.white_balance_temperature", post.white_balance_temperature, true);
    put_float(out, "post.white_balance_tint", post.white_balance_tint, true);
    put_int(out, "debug.view", static_cast<int>(debug.view), false);
    out << '}';
    return out.str();
}

bool GraphicsSettings::from_json(const std::string& json) {
    Values values;
    if (!parse_flat_json(json, values)) {
        return false;
    }
    renderer = static_cast<RendererMode>(read_int(values, "renderer", static_cast<int>(renderer)));
    preset = static_cast<QualityPreset>(read_int(values, "preset", static_cast<int>(preset)));
    wireframe = read_bool(values, "wireframe", wireframe);
    sun.light_mode = read_int(values, "sun.light_mode", sun.light_mode);
    sun.specular_disk = read_bool(values, "sun.specular_disk", sun.specular_disk);
    sun.backlight_translucency =
        read_bool(values, "sun.backlight_translucency", sun.backlight_translucency);
    sun.limb_darkening = read_bool(values, "sun.limb_darkening", sun.limb_darkening);
    sun.hair_transmission = read_bool(values, "sun.hair_transmission", sun.hair_transmission);
    sun.sky_coupling = read_bool(values, "sun.sky_coupling", sun.sky_coupling);
    sun.auto_cct = read_bool(values, "sun.auto_cct", sun.auto_cct);
    sun.corona = read_bool(values, "sun.corona", sun.corona);
    sun.intensity = read_float(values, "sun.intensity", sun.intensity);
    sun.color_temperature = read_float(values, "sun.color_temperature", sun.color_temperature);
    sun.angular_radius = read_float(values, "sun.angular_radius", sun.angular_radius);
    shadows.quality = read_int(values, "shadows.quality", shadows.quality);
    shadows.pcss = read_bool(values, "shadows.pcss", shadows.pcss);
    shadows.light_radius = read_float(values, "shadows.light_radius", shadows.light_radius);
    shadows.blur = read_bool(values, "shadows.blur", shadows.blur);
    shadows.contact_quality = read_int(values, "shadows.contact_quality", shadows.contact_quality);
    shadows.ssgs_quality = read_int(values, "shadows.ssgs_quality", shadows.ssgs_quality);
    sky.time_of_day = read_float(values, "sky.time_of_day", sky.time_of_day);
    sky.turbidity = read_float(values, "sky.turbidity", sky.turbidity);
    sky.ozone = read_float(values, "sky.ozone", sky.ozone);
    sky.sun_radius = read_float(values, "sky.sun_radius", sky.sun_radius);
    sky.cloud_coverage = read_float(values, "sky.cloud_coverage", sky.cloud_coverage);
    sky.cloud_speed = read_float(values, "sky.cloud_speed", sky.cloud_speed);
    sky.star_brightness = read_float(values, "sky.star_brightness", sky.star_brightness);
    sky.moon_intensity = read_float(values, "sky.moon_intensity", sky.moon_intensity);
    sky.ambient_intensity = read_float(values, "sky.ambient_intensity", sky.ambient_intensity);
    sky.ambient_color = read_vec3(values, "sky.ambient_color", sky.ambient_color);
    ibl.quality = read_int(values, "ibl.quality", ibl.quality);
    ibl.intensity = read_float(values, "ibl.intensity", ibl.intensity);
    ibl.rotation_degrees = read_float(values, "ibl.rotation_degrees", ibl.rotation_degrees);
    ibl.custom_path = read_string(values, "ibl.custom_path", ibl.custom_path);
    fog.enable = read_int(values, "fog.enable", fog.enable);
    fog.quality = read_int(values, "fog.quality", fog.quality);
    fog.debug = read_int(values, "fog.debug", fog.debug);
    fog.cloud_shadow = read_bool(values, "fog.cloud_shadow", fog.cloud_shadow);
    fog.density = read_float(values, "fog.density", fog.density);
    fog.height = read_float(values, "fog.height", fog.height);
    fog.falloff = read_float(values, "fog.falloff", fog.falloff);
    fog.ground_density = read_float(values, "fog.ground_density", fog.ground_density);
    fog.ground_height = read_float(values, "fog.ground_height", fog.ground_height);
    fog.ground_falloff = read_float(values, "fog.ground_falloff", fog.ground_falloff);
    fog.start_distance = read_float(values, "fog.start_distance", fog.start_distance);
    fog.cutoff_distance = read_float(values, "fog.cutoff_distance", fog.cutoff_distance);
    fog.max_opacity = read_float(values, "fog.max_opacity", fog.max_opacity);
    fog.color = read_vec3(values, "fog.color", fog.color);
    fog.sun_color = read_vec3(values, "fog.sun_color", fog.sun_color);
    fog.sun_intensity = read_float(values, "fog.sun_intensity", fog.sun_intensity);
    fog.mie_phase = read_float(values, "fog.mie_phase", fog.mie_phase);
    brdf.diffuse_type = static_cast<DiffuseBrdf>(
        read_int(values, "brdf.diffuse_type", static_cast<int>(brdf.diffuse_type)));
    brdf.specular_sin_alpha =
        read_float(values, "brdf.specular_sin_alpha", brdf.specular_sin_alpha);
    brdf.fresnel_type = static_cast<FresnelKernel>(
        read_int(values, "brdf.fresnel_type", static_cast<int>(brdf.fresnel_type)));
    brdf.cloth_charlie = read_bool(values, "brdf.cloth_charlie", brdf.cloth_charlie);
    skin.backlit_strength = read_float(values, "skin.backlit_strength", skin.backlit_strength);
    skin.backlit_g = read_float(values, "skin.backlit_g", skin.backlit_g);
    skin.backlit_ior = read_float(values, "skin.backlit_ior", skin.backlit_ior);
    skin.spec_lobe0_scale = read_float(values, "skin.spec_lobe0_scale", skin.spec_lobe0_scale);
    skin.spec_lobe0_add = read_float(values, "skin.spec_lobe0_add", skin.spec_lobe0_add);
    skin.spec_lobe1_scale = read_float(values, "skin.spec_lobe1_scale", skin.spec_lobe1_scale);
    skin.spec_lobe1_add = read_float(values, "skin.spec_lobe1_add", skin.spec_lobe1_add);
    skin.spec_lobe0_mix = read_float(values, "skin.spec_lobe0_mix", skin.spec_lobe0_mix);
    skin.spec_lobe1_mix = read_float(values, "skin.spec_lobe1_mix", skin.spec_lobe1_mix);
    skin.sss_mfp_scale = read_float(values, "skin.sss_mfp_scale", skin.sss_mfp_scale);
    skin.ssss_quality = read_int(values, "skin.ssss_quality", skin.ssss_quality);
    skin.ssss_texspace = read_bool(values, "skin.ssss_texspace", skin.ssss_texspace);
    toon.enable = read_int(values, "toon.enable", toon.enable);
    toon.diffusion_quality = read_int(values, "toon.diffusion_quality", toon.diffusion_quality);
    toon.shadow_mul = read_vec3(values, "toon.shadow_mul", toon.shadow_mul);
    toon.shadow_add = read_vec3(values, "toon.shadow_add", toon.shadow_add);
    toon.shadow_hue = read_float(values, "toon.shadow_hue", toon.shadow_hue);
    toon.shadow_warmth = read_float(values, "toon.shadow_warmth", toon.shadow_warmth);
    toon.autotint_color = read_vec3(values, "toon.autotint_color", toon.autotint_color);
    toon.autotint_threshold =
        read_float(values, "toon.autotint_threshold", toon.autotint_threshold);
    toon.autotint_strength = read_float(values, "toon.autotint_strength", toon.autotint_strength);
    toon.self_power_shadow = read_float(values, "toon.self_power_shadow", toon.self_power_shadow);
    toon.rim_strength = read_float(values, "toon.rim_strength", toon.rim_strength);
    toon.rim_power = read_float(values, "toon.rim_power", toon.rim_power);
    ao.type = read_int(values, "ao.type", ao.type);
    ao.quality = read_int(values, "ao.quality", ao.quality);
    ao.sky_visibility = read_int(values, "ao.sky_visibility", ao.sky_visibility);
    ao.radius = read_float(values, "ao.radius", ao.radius);
    ao.intensity = read_float(values, "ao.intensity", ao.intensity);
    ssr.quality = read_int(values, "ssr.quality", ssr.quality);
    ssr.debug = read_bool(values, "ssr.debug", ssr.debug);
    ssr.glass_refraction = read_int(values, "ssr.glass_refraction", ssr.glass_refraction);
    ssr.water_caustics = read_bool(values, "ssr.water_caustics", ssr.water_caustics);
    ssr.water_surface_y = read_float(values, "ssr.water_surface_y", ssr.water_surface_y);
    ssr.caustics_intensity = read_float(values, "ssr.caustics_intensity", ssr.caustics_intensity);
    gi.enable = read_int(values, "gi.enable", gi.enable);
    gi.quality = read_int(values, "gi.quality", gi.quality);
    gi.filter_radius = read_int(values, "gi.filter_radius", gi.filter_radius);
    gi.prefilter_radius = read_int(values, "gi.prefilter_radius", gi.prefilter_radius);
    gi.sample_radius = read_float(values, "gi.sample_radius", gi.sample_radius);
    gi.z_thickness = read_float(values, "gi.z_thickness", gi.z_thickness);
    gi.il_amount = read_float(values, "gi.il_amount", gi.il_amount);
    gi.ao_amount = read_float(values, "gi.ao_amount", gi.ao_amount);
    gi.bounces = read_bool(values, "gi.bounces", gi.bounces);
    gi.bounce_weight = read_float(values, "gi.bounce_weight", gi.bounce_weight);
    gi.filter_sensitivity = read_float(values, "gi.filter_sensitivity", gi.filter_sensitivity);
    gi.specular_gi = read_bool(values, "gi.specular_gi", gi.specular_gi);
    camera.fstop = read_float(values, "camera.fstop", camera.fstop);
    camera.focal_length = read_float(values, "camera.focal_length", camera.focal_length);
    camera.focal_distance = read_float(values, "camera.focal_distance", camera.focal_distance);
    camera.focal_region = read_float(values, "camera.focal_region", camera.focal_region);
    camera.blade_count = read_int(values, "camera.blade_count", camera.blade_count);
    camera.measure_mode = read_float(values, "camera.measure_mode", camera.measure_mode);
    camera.vignette = read_float(values, "camera.vignette", camera.vignette);
    camera.dispersion = read_float(values, "camera.dispersion", camera.dispersion);
    camera.dispersion_radius =
        read_float(values, "camera.dispersion_radius", camera.dispersion_radius);
    bloom.bokeh_mode = read_int(values, "bloom.bokeh_mode", bloom.bokeh_mode);
    bloom.cat_eye = read_bool(values, "bloom.cat_eye", bloom.cat_eye);
    bloom.eye_adaptation = read_int(values, "bloom.eye_adaptation", bloom.eye_adaptation);
    bloom.bloom_mode = read_int(values, "bloom.bloom_mode", bloom.bloom_mode);
    bloom.bloom_quality = read_int(values, "bloom.bloom_quality", bloom.bloom_quality);
    bloom.bloom_spectral_mode =
        read_int(values, "bloom.bloom_spectral_mode", bloom.bloom_spectral_mode);
    bloom.halation_mode = read_int(values, "bloom.halation_mode", bloom.halation_mode);
    bloom.flare_mode = read_int(values, "bloom.flare_mode", bloom.flare_mode);
    bloom.star_mode = read_int(values, "bloom.star_mode", bloom.star_mode);
    bloom.dispersion_mode = read_int(values, "bloom.dispersion_mode", bloom.dispersion_mode);
    bloom.threshold = read_float(values, "bloom.threshold", bloom.threshold);
    bloom.radius = read_float(values, "bloom.radius", bloom.radius);
    bloom.intensity = read_float(values, "bloom.intensity", bloom.intensity);
    post.tonemap = static_cast<TonemapOperator>(
        read_int(values, "post.tonemap", static_cast<int>(post.tonemap)));
    post.aa_quality = read_int(values, "post.aa_quality", post.aa_quality);
    post.motion_blur = read_int(values, "post.motion_blur", post.motion_blur);
    post.sharpen = read_bool(values, "post.sharpen", post.sharpen);
    post.sharpen_intensity = read_float(values, "post.sharpen_intensity", post.sharpen_intensity);
    post.grain = read_bool(values, "post.grain", post.grain);
    post.grain_intensity = read_float(values, "post.grain_intensity", post.grain_intensity);
    post.exposure = read_float(values, "post.exposure", post.exposure);
    post.contrast = read_float(values, "post.contrast", post.contrast);
    post.saturation = read_float(values, "post.saturation", post.saturation);
    post.gamma = read_float(values, "post.gamma", post.gamma);
    post.vibrance = read_float(values, "post.vibrance", post.vibrance);
    post.hue = read_float(values, "post.hue", post.hue);
    post.white_balance_temperature =
        read_float(values, "post.white_balance_temperature", post.white_balance_temperature);
    post.white_balance_tint =
        read_float(values, "post.white_balance_tint", post.white_balance_tint);
    debug.view = static_cast<DebugView>(read_int(values, "debug.view",
                                                 static_cast<int>(debug.view)));
    debug.show_timings = read_bool(values, "debug.show_timings", debug.show_timings);
    validate();
    return true;
}

GraphicsSettings GraphicsSettings::preset_values(QualityPreset preset_value) {
    GraphicsSettings settings;
    settings.renderer = RendererMode::Native;
    settings.preset = preset_value;
    switch (preset_value) {
    case QualityPreset::Low:
        settings.sun.intensity = 1.2f;
        settings.sun.angular_radius = 0.012f;
        settings.shadows.quality = 1;
        settings.shadows.pcss = false;
        settings.shadows.contact_quality = 1;
        settings.shadows.ssgs_quality = 0;
        settings.sky.ambient_intensity = 0.12f;
        settings.ao.quality = 2;
        settings.ao.type = 2;
        settings.ssr.quality = 1;
        settings.gi.enable = 0;
        settings.fog.enable = 1;
        settings.post.aa_quality = 1;
        settings.post.sharpen = true;
        break;
    case QualityPreset::Medium:
        settings.sun.intensity = 1.5f;
        settings.shadows.quality = 2;
        settings.shadows.pcss = true;
        settings.shadows.contact_quality = 3;
        settings.shadows.ssgs_quality = 2;
        settings.sky.ambient_intensity = 0.10f;
        settings.ao.quality = 4;
        settings.ssr.quality = 2;
        settings.gi.enable = 2;
        settings.gi.quality = 1;
        settings.fog.enable = 2;
        settings.fog.quality = 1;
        settings.post.aa_quality = 5;
        break;
    case QualityPreset::High:
        settings.sun.intensity = 1.6f;
        settings.shadows.quality = 3;
        settings.shadows.contact_quality = 3;
        settings.shadows.ssgs_quality = 3;
        settings.sky.ambient_intensity = 0.09f;
        settings.ao.quality = 6;
        settings.ssr.quality = 3;
        settings.gi.enable = 2;
        settings.gi.quality = 2;
        settings.fog.enable = 2;
        settings.fog.quality = 2;
        settings.post.aa_quality = 5;
        break;
    case QualityPreset::Cinematic:
        settings.sun.intensity = 1.7f;
        settings.shadows.quality = 4;
        settings.shadows.contact_quality = 3;
        settings.shadows.ssgs_quality = 3;
        settings.sky.ambient_intensity = 0.08f;
        settings.ao.quality = 6;
        settings.ssr.quality = 4;
        settings.gi.enable = 3;
        settings.gi.quality = 4;
        settings.fog.enable = 3;
        settings.fog.quality = 3;
        settings.post.aa_quality = 5;
        break;
    case QualityPreset::Custom:
        break;
    }
    settings.validate();
    return settings;
}

const char* renderer_mode_name(RendererMode mode) {
    return mode == RendererMode::Native ? "Native" : "Legacy";
}

const char* quality_preset_name(QualityPreset preset) {
    switch (preset) {
    case QualityPreset::Low:
        return "Low";
    case QualityPreset::Medium:
        return "Medium";
    case QualityPreset::High:
        return "High";
    case QualityPreset::Cinematic:
        return "Cinematic";
    case QualityPreset::Custom:
        return "Custom";
    }
    return "Custom";
}

const char* tonemap_operator_name(TonemapOperator op) {
    switch (op) {
    case TonemapOperator::Linear:
        return "Linear";
    case TonemapOperator::Reinhard:
        return "Reinhard";
    case TonemapOperator::Hable:
        return "Hable";
    case TonemapOperator::Uncharted2:
        return "Uncharted2";
    case TonemapOperator::Hejl:
        return "Hejl 2015";
    case TonemapOperator::AcesSrgb:
        return "ACES sRGB";
    case TonemapOperator::NaughtyDog:
        return "Naughty Dog";
    case TonemapOperator::AcesFitted:
        return "ACES Fitted";
    case TonemapOperator::AgXBase:
        return "AgX Base";
    case TonemapOperator::AgXPunchy:
        return "AgX Punchy";
    case TonemapOperator::PbrNeutral:
        return "PBR Neutral";
    case TonemapOperator::Gt7:
        return "GT7";
    case TonemapOperator::AgXGolden:
        return "AgX Golden";
    }
    return "AgX Base";
}

} // namespace hikari::gfx
