#pragma once

#include <cstdint>
#include <string>

#include "hikari/math.hpp"

namespace hikari::gfx {

enum class RendererMode : std::uint8_t { Legacy = 0, Native = 1 };
enum class QualityPreset : std::uint8_t { Low = 0, Medium = 1, High = 2, Cinematic = 3, Custom = 4 };
enum class TonemapOperator : std::uint8_t {
    Linear = 0,
    Reinhard = 1,
    Hable = 2,
    Uncharted2 = 3,
    Hejl = 4,
    AcesSrgb = 5,
    NaughtyDog = 6,
    AcesFitted = 7,
    AgXBase = 8,
    AgXPunchy = 9,
    PbrNeutral = 10,
    Gt7 = 11,
    AgXGolden = 12,
};
enum class DiffuseBrdf : std::uint8_t { Burley = 0, Eon = 1 };
enum class FresnelKernel : std::uint8_t { Schlick = 0, AdobeF82 = 1 };
enum class DebugView : std::uint8_t {
    Off = 0,
    BaseColor = 1,
    Normal = 2,
    Roughness = 3,
    Metalness = 4,
    Specular = 5,
    Emissive = 6,
    Depth = 7,
    ShadowCascades = 8,
    ShadowPCSS = 9,
    ContactShadow = 10,
    SSGS = 11,
    SkyVisibility = 12,
    SkyCube = 13,
    SkyIrradiance = 14,
    SkyPrefilter = 15,
    SkyPrefilterRoughness = 16,
    IblSpecular = 17,
    Ao = 18,
    AoBentNormal = 19,
    SsrRadiance = 20,
    SsrConfidence = 21,
    GiRadiance = 22,
    VxgiVoxels = 23,
};

struct SunSettings {
    int light_mode = 1;
    bool specular_disk = true;
    bool backlight_translucency = true;
    bool limb_darkening = true;
    bool hair_transmission = true;
    bool sky_coupling = true;
    bool auto_cct = false;
    bool corona = true;
    float intensity = 1.5f;
    float color_temperature = 6500.0f;
    float angular_radius = 0.020f;
};

struct ShadowSettings {
    int quality = 2;
    bool pcss = true;
    float light_radius = 0.015f;
    bool blur = true;
    int contact_quality = 3;
    int ssgs_quality = 3;
};

struct SkySettings {
    bool enabled = false;
    int quality = 2;
    float time_of_day = 12.0f;
    float turbidity = 1.0f;
    float ozone = 1.0f;
    float sun_radius = 1.5f;
    float sun_radiance = 10.0f;
    float mie_anisotropy = 0.76f;
    float cloud_coverage = 0.35f;
    float cloud_density = 1.0f;
    float cloud_speed = 0.15f;
    float cloud_turbulence = 1.0f;
    float star_brightness = 1.0f;
    float milky_way_intensity = 1.0f;
    float moon_intensity = 1.0f;
    float moon_phase = 0.5f;
    float moon_radius = 1.5f;
    float ambient_intensity = 0.10f;
    Vec3 ambient_color{0.55f, 0.66f, 0.82f};
    // Upper bound on captured environment refreshes per second.
    int ibl_update_hz = 10;
};

struct IblSettings {
    int quality = 1;
    float intensity = 1.0f;
    float rotation_degrees = 0.0f;
    std::string custom_path;
};

struct FogSettings {
    int enable = 2;
    int quality = 1;
    int debug = 0;
    bool cloud_shadow = true;
    float density = 0.0003f;
    float height = 0.0f;
    float falloff = 0.02f;
    float ground_density = 0.001f;
    float ground_height = -2.0f;
    float ground_falloff = 0.12f;
    float start_distance = 5.0f;
    float cutoff_distance = 3000.0f;
    float max_opacity = 0.90f;
    Vec3 color{0.65f, 0.75f, 0.90f};
    Vec3 sun_color{1.0f, 1.0f, 1.0f};
    float sun_intensity = 1.5f;
    float mie_phase = 0.75f;
};

struct BrdfSettings {
    DiffuseBrdf diffuse_type = DiffuseBrdf::Eon;
    float specular_sin_alpha = 0.0f;
    FresnelKernel fresnel_type = FresnelKernel::AdobeF82;
    bool cloth_charlie = true;
};

struct SkinSettings {
    float backlit_strength = 0.0f;
    float backlit_g = 0.76f;
    float backlit_ior = 1.4f;
    float spec_lobe0_scale = 1.15f;
    float spec_lobe0_add = 0.05f;
    float spec_lobe1_scale = 0.35f;
    float spec_lobe1_add = 0.02f;
    float spec_lobe0_mix = 0.70f;
    float spec_lobe1_mix = 0.30f;
    float sss_mfp_scale = 1.0f;
    int ssss_quality = 1;
    bool ssss_texspace = true;
};

struct ToonSettings {
    int enable = 0;
    int diffusion_quality = 2048;
    Vec3 shadow_mul{1.0f, 1.0f, 1.0f};
    Vec3 shadow_add{0.0f, 0.0f, 0.0f};
    float shadow_hue = 0.0f;
    float shadow_warmth = 0.0f;
    Vec3 autotint_color{0.95f, 0.80f, 1.05f};
    float autotint_threshold = 0.03f;
    float autotint_strength = 1.0f;
    float self_power_shadow = 0.0f;
    float rim_strength = 0.0f;
    float rim_power = 2.5f;
};

struct AoSettings {
    int type = 2;
    int quality = 6;
    int sky_visibility = 2;
    float radius = 1.0f;
    float intensity = 1.0f;
};

struct SsrSettings {
    int quality = 4;
    bool debug = false;
    // 0 hit mask, 1 confidence, 2 travel distance, 3 hit normal, 4 roughness.
    int debug_mode = 0;
    int glass_refraction = 1;
    bool water_caustics = true;
    float water_surface_y = 0.0f;
    float caustics_intensity = 1.5f;
};

struct GiSettings {
    int enable = 3;
    int quality = 1;
    int filter_radius = 16;
    int prefilter_radius = 2;
    float sample_radius = 16.0f;
    float z_thickness = 1.0f;
    float il_amount = 2.0f;
    float ao_amount = 1.0f;
    bool bounces = true;
    float bounce_weight = 0.35f;
    float filter_sensitivity = 1.0f;
    bool specular_gi = true;
};

struct CameraSettings {
    float fstop = 5.6f;
    float focal_length = 50.0f;
    float focal_distance = 10.0f;
    float focal_region = 0.0f;
    int blade_count = 10;
    float measure_mode = 0.5f;
    float vignette = 0.0f;
    float dispersion = 0.0f;
    float dispersion_radius = 0.5f;
};

struct BloomSettings {
    int bokeh_mode = 0;
    bool cat_eye = false;
    int eye_adaptation = 0;
    int bloom_mode = 4;
    int bloom_quality = 2048;
    int bloom_spectral_mode = 1;
    int halation_mode = 2;
    int flare_mode = 0;
    int star_mode = 1;
    int dispersion_mode = 3;
    float threshold = 0.8f;
    float radius = 4.0f;
    float intensity = 1.0f;
};

struct PostSettings {
    TonemapOperator tonemap = TonemapOperator::AgXBase;
    int aa_quality = 5;
    int motion_blur = 0;
    bool sharpen = true;
    float sharpen_intensity = 0.5f;
    bool grain = false;
    float grain_intensity = 0.05f;
    float exposure = 0.35f;
    float contrast = 0.0f;
    float saturation = 0.0f;
    float gamma = 1.0f;
    float vibrance = 0.0f;
    float hue = 0.0f;
    float white_balance_temperature = 0.0f;
    float white_balance_tint = 0.0f;
};

struct DebugSettings {
    DebugView view = DebugView::Off;
    bool show_timings = false;
};

// Native pipeline settings. The field groups mirror the eleven sections of the
// Ray-MMD ReForge ray.conf so coverage can be audited mechanically.
struct GraphicsSettings {
    RendererMode renderer = RendererMode::Legacy;
    QualityPreset preset = QualityPreset::Medium;
    bool wireframe = false;

    SunSettings sun;
    ShadowSettings shadows;
    SkySettings sky;
    IblSettings ibl;
    FogSettings fog;
    BrdfSettings brdf;
    SkinSettings skin;
    ToonSettings toon;
    AoSettings ao;
    SsrSettings ssr;
    GiSettings gi;
    CameraSettings camera;
    BloomSettings bloom;
    PostSettings post;
    DebugSettings debug;

    // Clamps every field into its valid range and returns the number of changes.
    int validate();

    // Stable hash of the fields that select shader permutations.
    std::uint64_t permutation_key() const;

    // Flat JSON object with dotted keys. The format is intentionally small and
    // Qt-free so the lab and the studio share one serializer.
    std::string to_json() const;
    bool from_json(const std::string& json);

    static GraphicsSettings preset_values(QualityPreset preset);
};

const char* renderer_mode_name(RendererMode mode);
const char* quality_preset_name(QualityPreset preset);
const char* tonemap_operator_name(TonemapOperator op);

} // namespace hikari::gfx
