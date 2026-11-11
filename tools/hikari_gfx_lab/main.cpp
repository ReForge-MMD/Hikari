#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "capture.hpp"
#include "hikari/core/d3d11.hpp"
#include "hikari/gfx/renderer.hpp"
#include "hikari/gfx/settings.hpp"
#include "hikari/gfx/sky.hpp"
#include "hikari/gfx/sky_assets.hpp"
#include "hikari/render/camera.hpp"
#include "scene.hpp"

namespace {

struct Options {
    int phase = 2;
    std::string scene = "synthetic";
    std::string pmx;
    std::string capture;
    std::string probe;
    std::string compare;
    std::string target = "Final";
    float exposure = 0.35f;
    float sun = 1.5f;
    float ambient = 0.10f;
    int debug = 0;
    float sun_elevation = -1.0f;
    float sun_azimuth = 180.0f;
    int sky_coupling = -1;
    float time_of_day = -1.0f;
    float sky_time = 0.0f;
    float sky_drift = 0.0f;
    int sky_hz = -1;
    int ibl = -1;
    float cloud_coverage = -1.0f;
    float moon_intensity = -1.0f;
    std::string ibl_path;
    float ibl_rotation = 0.0f;
    float ibl_intensity = -1.0f;
    int frames = 1;
    bool animate_sun = false;
    std::string camera = "default";
    float camera_azimuth = 0.0f;
    bool linear_tonemap = false;
    bool validate = false;
    bool list_targets = false;
    bool dump_materials = false;
    bool help = false;
};

bool g_running = true;
bool g_capture_requested = false;
bool g_reload_requested = false;
bool g_show_geometry = true;
bool g_show_lighting = true;
bool g_show_transparent = true;
bool g_show_tonemap = true;
bool g_show_grid = true;

void update_window_title(HWND hwnd) {
    const std::wstring title =
        std::wstring(L"Hikari Gfx Lab | 1 Geometry ") +
        (g_show_geometry ? L"on" : L"off") + L" | 2 Lighting " +
        (g_show_lighting ? L"on" : L"off") + L" | 3 Transparent " +
        (g_show_transparent ? L"on" : L"off") + L" | 4 Tonemap " +
        (g_show_tonemap ? L"on" : L"off") + L" | 5 Grid " +
        (g_show_grid ? L"on" : L"off") + L" | R Reload | F5 Capture | Esc Quit";
    SetWindowTextW(hwnd, title.c_str());
}

void print_help() {
    std::cout
        << "hikari_gfx_lab - native Hikari graphics phase lab\n\n"
        << "Usage:\n"
        << "  hikari_gfx_lab --phase 2 --scene synthetic --capture out.bmp --probe out.json\n"
        << "  hikari_gfx_lab --phase 2 --scene room --pmx <stage.pmx>\n"
        << "  hikari_gfx_lab --phase 2 --scene character --pmx <model.pmx>\n\n"
        << "Options:\n"
        << "  --phase <n>        Phase scenario. This build implements phase 1 to 4.\n"
        << "  --scene <name>     synthetic, room, or character.\n"
        << "  --pmx <path>       Override the scene PMX path.\n"
        << "  --capture <path>   Headless BMP capture.\n"
        << "  --probe <path>     Headless JSON probe.\n"
        << "  --compare <path>   Compare the capture with a golden BMP.\n"
        << "  --exposure <n>     Tonemap exposure (default 0.6).\n"
        << "  --sun <n>          Sun intensity multiplier (default 1.2).\n"
        << "  --ambient <n>      Ambient intensity (default 0.10).\n"
        << "  --debug <n>        Debug view (0=Off, 8=Cascades, 9=PCSS, 10=Contact, 11=SSGS, 12=SkyVis, 13=SkyCube, 14=SkyIrradiance, 15=SkyPrefilter, 16=SkyPrefilterRough, 17=IblSpecular).\n"
        << "  --sun-elevation <n> Sun elevation angle in degrees.\n"
        << "  --sun-azimuth <n>   Sun azimuth angle in degrees.\n"
        << "  --sky-coupling <n>  Fade the direct sun into the sky at low elevation (0/1).\n"
        << "  --time-of-day <h>  Sky time of day in hours, 0 to 24 (phase 4).\n"
        << "  --time <s>         Sky animation time in seconds for clouds and meteors.\n"
        << "  --sky-drift <s>    Advance sky time by s seconds per rendered frame.\n"
        << "  --sky-hz <n>       Cap captured environment refreshes per second (default 10).\n"
        << "  --cloud-coverage <n> Cloud layer coverage, 0 clear to 1 overcast (phase 4).\n"
        << "  --moon-intensity <n> Moon disk and moonlight intensity (phase 4).\n"
        << "  --ibl <n>          IBL quality (0=off, 1=128, 2=256 sky capture).\n"
        << "  --ibl-path <file>  Load a custom .hdr or .dds environment instead of the sky.\n"
        << "  --ibl-rotation <d> Rotate the environment around the vertical axis, degrees.\n"
        << "  --ibl-intensity <n> Environment specular intensity multiplier.\n"
        << "  --frames <n>       Render the headless capture n times and report the mean time.\n"
        << "  --animate-sun      Continuously rotate the sun in preview mode.\n"
        << "  --camera <name>    Camera preset (default, close, inside, sky, band).\n"
        << "  --camera-azimuth <d> Orbit the camera preset around the scene center, degrees.\n"
        << "  --linear-tonemap   Use the linear operator for diagnosis.\n"
        << "  --validate-shaders Compile and run the default shader set.\n"
        << "  --list-targets     List render target names.\n"
        << "  --dump-materials   Print PMX material names, colors and texture slots.\n"
        << "  --help             Show this text.\n\n"
        << "Environment:\n"
        << "  HIKARI_GFX_ROOM_PMX       Default room stage PMX.\n"
        << "  HIKARI_GFX_CHARACTER_PMX  Default character PMX.\n"
        << "  HIKARI_GFX_SHADER_DIR     Load gfx_core.hlsl from disk for hot reload.\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                std::exit(2);
            }
            return argv[++i];
        };
        if (argument == "--phase") {
            options.phase = std::stoi(next("--phase"));
        } else if (argument == "--scene") {
            options.scene = next("--scene");
        } else if (argument == "--pmx") {
            options.pmx = next("--pmx");
        } else if (argument == "--capture") {
            options.capture = next("--capture");
        } else if (argument == "--probe") {
            options.probe = next("--probe");
        } else if (argument == "--compare") {
            options.compare = next("--compare");
        } else if (argument == "--target") {
            options.target = next("--target");
        } else if (argument == "--exposure") {
            options.exposure = std::stof(next("--exposure"));
        } else if (argument == "--sun") {
            options.sun = std::stof(next("--sun"));
        } else if (argument == "--ambient") {
            options.ambient = std::stof(next("--ambient"));
        } else if (argument == "--debug") {
            options.debug = std::stoi(next("--debug"));
        } else if (argument == "--sun-elevation") {
            options.sun_elevation = std::stof(next("--sun-elevation"));
        } else if (argument == "--sun-azimuth") {
            options.sun_azimuth = std::stof(next("--sun-azimuth"));
        } else if (argument == "--sky-coupling") {
            options.sky_coupling = std::stoi(next("--sky-coupling"));
        } else if (argument == "--time-of-day") {
            options.time_of_day = std::stof(next("--time-of-day"));
        } else if (argument == "--time") {
            options.sky_time = std::stof(next("--time"));
        } else if (argument == "--sky-drift") {
            options.sky_drift = std::stof(next("--sky-drift"));
        } else if (argument == "--sky-hz") {
            options.sky_hz = std::stoi(next("--sky-hz"));
        } else if (argument == "--cloud-coverage") {
            options.cloud_coverage = std::stof(next("--cloud-coverage"));
        } else if (argument == "--moon-intensity") {
            options.moon_intensity = std::stof(next("--moon-intensity"));
        } else if (argument == "--ibl") {
            options.ibl = std::stoi(next("--ibl"));
        } else if (argument == "--ibl-path") {
            options.ibl_path = next("--ibl-path");
        } else if (argument == "--ibl-rotation") {
            options.ibl_rotation = std::stof(next("--ibl-rotation"));
        } else if (argument == "--ibl-intensity") {
            options.ibl_intensity = std::stof(next("--ibl-intensity"));
        } else if (argument == "--frames") {
            options.frames = std::max(std::stoi(next("--frames")), 1);
        } else if (argument == "--animate-sun") {
            options.animate_sun = true;
        } else if (argument == "--camera") {
            options.camera = next("--camera");
        } else if (argument == "--camera-azimuth") {
            options.camera_azimuth = std::stof(next("--camera-azimuth"));
        } else if (argument == "--linear-tonemap") {
            options.linear_tonemap = true;
        } else if (argument == "--validate-shaders") {
            options.validate = true;
        } else if (argument == "--list-targets") {
            options.list_targets = true;
        } else if (argument == "--dump-materials") {
            options.dump_materials = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            std::exit(2);
        }
    }
    return options;
}

std::string scene_path(const Options& options) {
    if (!options.pmx.empty()) {
        return options.pmx;
    }
    if (options.scene == "room") {
        const char* value = std::getenv("HIKARI_GFX_ROOM_PMX");
        return value != nullptr ? value : "";
    }
    if (options.scene == "character") {
        const char* value = std::getenv("HIKARI_GFX_CHARACTER_PMX");
        return value != nullptr ? value : "";
    }
    return {};
}

hikari::gfx::GraphicsSettings lab_settings(const Options& options) {
    hikari::gfx::GraphicsSettings settings =
        hikari::gfx::GraphicsSettings::preset_values(hikari::gfx::QualityPreset::Medium);
    settings.renderer = hikari::gfx::RendererMode::Native;
    settings.sun.intensity = options.sun;
    settings.sun.specular_disk = true;
    settings.sun.limb_darkening = true;
    settings.sun.auto_cct = true;
    settings.sun.color_temperature = 6500.0f;
    if (options.sky_coupling >= 0) {
        settings.sun.sky_coupling = options.sky_coupling != 0;
    }
    settings.sky.ambient_intensity = options.ambient;
    settings.post.exposure = options.exposure;
    settings.post.tonemap = options.linear_tonemap
                                ? hikari::gfx::TonemapOperator::Linear
                                : hikari::gfx::TonemapOperator::AgXBase;
    if (options.phase == 3) {
        settings.shadows.quality = 2;
        settings.shadows.pcss = true;
        settings.shadows.blur = true;
        settings.shadows.contact_quality = 3;
        settings.shadows.ssgs_quality = 3;
        settings.ao.sky_visibility = 1;
    }
    if (options.time_of_day >= 0.0f) {
        settings.sky.time_of_day = options.time_of_day;
    }
    if (options.phase == 4) {
        settings.sky.enabled = true;
        settings.sky.quality = 3;
    }
    if (options.ibl >= 0) {
        settings.ibl.quality = options.ibl;
    }
    settings.ibl.custom_path = options.ibl_path;
    settings.ibl.rotation_degrees = options.ibl_rotation;
    if (options.ibl_intensity >= 0.0f) {
        settings.ibl.intensity = options.ibl_intensity;
    }
    if (options.sky_hz >= 0) {
        settings.sky.ibl_update_hz = options.sky_hz;
    }
    if (options.cloud_coverage >= 0.0f) {
        settings.sky.cloud_coverage = options.cloud_coverage;
    }
    if (options.moon_intensity >= 0.0f) {
        settings.sky.moon_intensity = options.moon_intensity;
    }
    if (options.debug >= 0 && options.debug <= 17) {
        settings.debug.view = static_cast<hikari::gfx::DebugView>(options.debug);
    }
    settings.validate();
    return settings;
}

hikari::render::Camera frame_camera(const hikari::gfx_lab::LabScene& scene,
                                    const hikari::gfx::GraphicsSettings& settings,
                                    const Options& options,
                                    std::uint32_t width, std::uint32_t height) {
    hikari::render::Camera camera;
    const hikari::Vec3 center = scene.center();
    const float radius = std::max(scene.radius(), 0.5f);
    const float fov = 0.5235987756f;
    const float distance = radius / std::tan(fov * 0.5f) * 1.12f;
    float sky_fov = fov;
    if (options.camera == "close") {
        camera.position = center + hikari::Vec3(0.0f, radius * 0.1f, -radius * 0.45f);
        camera.target = center + hikari::Vec3(0.0f, radius * 0.1f, 0.0f);
    } else if (options.camera == "inside") {
        camera.position = center + hikari::Vec3(0.0f, radius * 0.05f, -radius * 0.2f);
        camera.target = center + hikari::Vec3(0.0f, radius * 0.05f, radius * 0.5f);
    } else if (options.camera == "sky") {
        // Phase 4 sky capture: aim at the anti-solar point so the moon, stars,
        // and the Milky Way band sit inside the frame. Other phases keep a
        // pitched-up view over the scene.
        if (options.phase == 4) {
            const hikari::Vec3 moon_direction =
                -hikari::gfx::sun_direction_from_time_of_day(settings.sky.time_of_day);
            camera.position = center + hikari::Vec3(0.0f, radius * 0.06f, 0.0f);
            camera.target = camera.position + moon_direction * (radius * 4.0f);
        } else {
            camera.position =
                center + hikari::Vec3(0.0f, radius * 0.06f, -distance * 0.35f);
            camera.target =
                camera.position + hikari::Vec3(0.0f, radius * 1.25f, radius * 0.10f);
        }
        sky_fov = 1.0471976f;
    } else if (options.camera == "band") {
        // Frames the brightest part of the generated Milky Way band, which the
        // anti-solar sky preset cannot show.
        const auto core = hikari::gfx::milky_way_core_direction();
        camera.position = center + hikari::Vec3(0.0f, radius * 0.06f, 0.0f);
        camera.target = camera.position + hikari::Vec3(core[0], core[1], core[2]) * (radius * 4.0f);
        sky_fov = 1.0471976f;
    } else {
        camera.position = center + hikari::Vec3(radius * 0.04f, radius * 0.06f, -distance);
        camera.target = center + hikari::Vec3(0.0f, radius * 0.04f, 0.0f);
    }
    camera.up = hikari::Vec3(0.0f, 1.0f, 0.0f);
    if (options.camera_azimuth != 0.0f) {
        // Orbiting both the eye and the target around the scene center turns any
        // preset into a turntable frame without changing its framing.
        constexpr float kDegToRad = 3.1415926535f / 180.0f;
        const float angle = options.camera_azimuth * kDegToRad;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const auto orbit = [&](const hikari::Vec3& point) {
            const hikari::Vec3 local = point - center;
            return center + hikari::Vec3(local.x * c + local.z * s, local.y,
                                         -local.x * s + local.z * c);
        };
        camera.position = orbit(camera.position);
        camera.target = orbit(camera.target);
    }
    camera.fov_y = sky_fov;
    camera.aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1u));
    camera.near_z = std::max(radius * 0.01f, 0.01f);
    camera.far_z = std::max(radius * 20.0f, 100.0f);
    camera.orthographic = false;
    return camera;
}

hikari::Vec3 subset_center(const hikari::gfx_lab::LabScene& scene,
                           const hikari::render::MeshSubset& subset) {
    hikari::Vec3 center(0.0f, 0.0f, 0.0f);
    std::size_t count = 0;
    const std::size_t end = std::min<std::size_t>(
        static_cast<std::size_t>(subset.index_start) + subset.index_count,
        scene.pmx.indices.size());
    for (std::size_t i = subset.index_start; i < end; ++i) {
        const std::uint32_t index = scene.pmx.indices[i];
        if (index < scene.mesh.current_vertices.size()) {
            center += hikari::Vec3::from_array(scene.mesh.current_vertices[index].position);
            ++count;
        }
    }
    return count > 0 ? center / static_cast<float>(count) : center;
}

hikari::gfx::FrameView make_view(const hikari::gfx_lab::LabScene& scene,
                                 const hikari::render::Camera& camera,
                                 const hikari::gfx::GraphicsSettings& settings,
                                 const Options& options,
                                 std::uint32_t width, std::uint32_t height) {
    hikari::gfx::FrameView view;
    view.view = camera.view_matrix();
    view.projection = camera.projection_matrix();
    view.view_projection = camera.view_projection_matrix();
    view.inverse_view = view.view.inverse_transform();
    view.camera_position = camera.position;
    if (options.sun_elevation >= 0.0f) {
        constexpr float kDegToRad = 3.1415926535f / 180.0f;
        const float el = options.sun_elevation * kDegToRad;
        const float az = options.sun_azimuth * kDegToRad;
        view.sun_direction = -hikari::Vec3(std::cos(el) * std::sin(az),
                                           std::sin(el),
                                           std::cos(el) * std::cos(az)).normalize();
    } else if (options.phase == 4) {
        view.sun_direction = -hikari::gfx::sun_direction_from_time_of_day(
            settings.sky.time_of_day);
    } else {
        view.sun_direction = hikari::Vec3(-0.45f, -1.0f, -0.32f).normalize();
    }
    view.sun_color = hikari::gfx::sun_color_from_settings(settings, hikari::Vec3(1.0f, 0.97f, 0.92f));
    view.ambient_color = hikari::gfx::ambient_color_from_settings(settings);
    view.background_color = hikari::Vec3(0.055f, 0.065f, 0.085f);
    view.near_z = camera.near_z;
    view.far_z = camera.far_z;
    view.time_seconds = options.sky_time;
    view.orthographic = camera.orthographic;
    view.draw_grid = true;
    view.show_grid = true;
    view.wireframe = false;
    view.width = width;
    view.height = height;

    const hikari::Vec3 forward = (camera.target - camera.position).normalize();
    for (std::size_t i = 0; i < scene.mesh.subsets.size(); ++i) {
        const hikari::render::MeshSubset& subset = scene.mesh.subsets[i];
        hikari::gfx::DrawItem item;
        item.mesh = &scene.mesh;
        item.materials = &scene.materials;
        item.textures = &scene.textures;
        item.skinning = &scene.skinning;
        item.subset = i;
        item.depth = (subset_center(scene, subset) - camera.position).dot(forward);
        const bool transparent =
            subset.material_index < scene.materials.size() &&
            scene.materials[subset.material_index].constants.diffuse[3] < 0.999f;
        if (transparent) {
            view.transparent.push_back(item);
        } else {
            view.opaque.push_back(item);
        }
    }
    std::stable_sort(view.transparent.begin(), view.transparent.end(),
                     [](const hikari::gfx::DrawItem& a, const hikari::gfx::DrawItem& b) {
                         return a.depth > b.depth;
                     });
    return view;
}

int run_headless(const Options& options) {
    if (options.phase < 1 || options.phase > 4) {
        std::cerr << "phase " << options.phase << " is not implemented in this build\n";
        return 2;
    }
    const std::uint32_t width = 1280;
    const std::uint32_t height = 720;
    try {
        hikari::core::D3D11Context context(nullptr, width, height);
        hikari::gfx_lab::LabScene scene;
        if (options.scene == "synthetic") {
            scene = hikari::gfx_lab::LabScene::synthetic(context);
        } else {
            const std::string path = scene_path(options);
            if (path.empty()) {
                std::cerr << "no PMX path for scene '" << options.scene
                          << "'; pass --pmx or set the environment variable\n";
                return 2;
            }
            std::string error;
            if (!scene.load(context, path, &error)) {
                std::cerr << "scene load failed: " << error << '\n';
                return 1;
            }
        }

        hikari::gfx::GraphicsSettings settings = lab_settings(options);
        if (options.phase == 1) {
            settings.sun.light_mode = 1;
            settings.post.tonemap = hikari::gfx::TonemapOperator::Linear;
        }
        std::cout << "scene bounds: center (" << scene.center().x << ", " << scene.center().y
                  << ", " << scene.center().z << ") radius " << scene.radius() << ", textures "
                  << scene.loaded_textures << '/' << scene.textures.size() << '\n';
        if (options.dump_materials) {
            for (std::size_t i = 0; i < scene.pmx.materials.size(); ++i) {
                const auto& material = scene.pmx.materials[i];
                std::cout << "material " << i << " '" << material.name_local << "' diffuse ("
                          << material.diffuse[0] << ',' << material.diffuse[1] << ','
                          << material.diffuse[2] << ',' << material.diffuse[3] << ") tex "
                          << material.texture_index << " shininess " << material.shininess
                          << '\n';
            }
        }
        hikari::gfx::Renderer renderer(context);
        hikari::gfx::TargetPool final_pool;
        final_pool.create_target(context.device.Get(), "Final", width, height,
                                 DXGI_FORMAT_R8G8B8A8_UNORM);
        const hikari::render::Camera camera =
            frame_camera(scene, settings, options, width, height);
        hikari::gfx::FrameView view =
            make_view(scene, camera, settings, options, width, height);

        const auto start = std::chrono::steady_clock::now();
        std::vector<double> frame_times;
        frame_times.reserve(static_cast<std::size_t>(options.frames));
        for (int frame = 0; frame < options.frames; ++frame) {
            view.time_seconds =
                options.sky_time + options.sky_drift * static_cast<float>(frame);
            const auto frame_start = std::chrono::steady_clock::now();
            renderer.render(settings, view, final_pool.get("Final")->rtv.Get());
            frame_times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                          frame_start)
                    .count());
        }
        const auto end = std::chrono::steady_clock::now();
        const double frame_ms = std::chrono::duration<double, std::milli>(end - start).count() /
                                static_cast<double>(options.frames);
        // First-use shader compilation dominates the mean, so also report the
        // mean of the last quarter of the run as the steady state cost.
        const std::size_t warm_start = frame_times.size() * 3 / 4;
        double warm_ms = 0.0;
        for (std::size_t i = warm_start; i < frame_times.size(); ++i) {
            warm_ms += frame_times[i];
        }
        warm_ms /= static_cast<double>(frame_times.size() - warm_start);
        const double max_ms = *std::max_element(frame_times.begin(), frame_times.end());

        ID3D11Texture2D* capture_texture = final_pool.get("Final")->texture.Get();
        if (options.target != "Final") {
            const hikari::gfx::Target* target = renderer.targets().get(options.target);
            if (target == nullptr || target->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                std::cerr << "target '" << options.target
                          << "' is not an RGBA8 debug target\n";
                return 2;
            }
            capture_texture = target->texture.Get();
        }
        const hikari::gfx_lab::CaptureData capture =
            hikari::gfx_lab::read_target(context, capture_texture, width, height);
        std::string error;
        if (!options.capture.empty() &&
            !hikari::gfx_lab::write_capture(options.capture, capture, &error)) {
            std::cerr << "capture failed: " << error << '\n';
            return 1;
        }
        if (!options.probe.empty() &&
            !hikari::gfx_lab::write_probe(options.probe, capture, settings, scene.name,
                                          std::to_string(options.phase), frame_ms, &error)) {
            std::cerr << "probe failed: " << error << '\n';
            return 1;
        }
        std::cout << "phase " << options.phase << " scene " << scene.name << " rendered "
                  << width << 'x' << height << " in " << frame_ms << " ms (warm " << warm_ms
                  << " ms, max " << max_ms << " ms), sky updates "
                  << renderer.sky_update_count() << '/' << options.frames << '\n';
        if (!settings.ibl.custom_path.empty()) {
            std::cout << "ibl source: " << (renderer.ibl_custom_active() ? "loaded " : "rejected ")
                      << settings.ibl.custom_path;
            if (!renderer.ibl_custom_active()) {
                std::cout << " (" << renderer.ibl_error() << ')';
            }
            std::cout << '\n';
        }
        if (!options.compare.empty()) {
            const auto golden = hikari::gfx_lab::read_capture(options.compare, &error);
            if (!golden) {
                std::cerr << "golden read failed: " << error << '\n';
                return 1;
            }
            const float difference =
                hikari::gfx_lab::mean_channel_difference(capture, *golden);
            std::cout << "golden mean channel difference: " << difference << '\n';
            constexpr float kTolerance = 0.02f;
            if (difference > kTolerance) {
                std::cerr << "golden comparison failed (tolerance " << kTolerance << ")\n";
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "lab failed: " << exception.what() << '\n';
        return 1;
    }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_ESCAPE:
            g_running = false;
            break;
        case VK_F5:
            g_capture_requested = true;
            break;
        case 'R':
            g_reload_requested = true;
            break;
        case '1':
            g_show_geometry = !g_show_geometry;
            update_window_title(hwnd);
            break;
        case '2':
            g_show_lighting = !g_show_lighting;
            update_window_title(hwnd);
            break;
        case '3':
            g_show_transparent = !g_show_transparent;
            update_window_title(hwnd);
            break;
        case '4':
            g_show_tonemap = !g_show_tonemap;
            update_window_title(hwnd);
            break;
        case '5':
            g_show_grid = !g_show_grid;
            update_window_title(hwnd);
            break;
        default:
            break;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

int run_window(const Options& options) {
    if (options.phase < 1 || options.phase > 4) {
        std::cerr << "phase " << options.phase << " is not implemented in this build\n";
        return 2;
    }
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"HikariGfxLabWindow";
    if (RegisterClassExW(&window_class) == 0) {
        std::cerr << "RegisterClassExW failed\n";
        return 1;
    }
    HWND hwnd = CreateWindowExW(0, window_class.lpszClassName, L"Hikari Gfx Lab",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                1280, 720, nullptr, nullptr, window_class.hInstance, nullptr);
    if (hwnd == nullptr) {
        std::cerr << "CreateWindowExW failed\n";
        return 1;
    }

    try {
        hikari::core::D3D11Context context(hwnd, 1280, 720);
        hikari::gfx_lab::LabScene scene;
        if (options.scene == "synthetic") {
            scene = hikari::gfx_lab::LabScene::synthetic(context);
        } else {
            const std::string path = scene_path(options);
            if (path.empty()) {
                std::cerr << "no PMX path for scene '" << options.scene
                          << "'; pass --pmx or set the environment variable\n";
                return 2;
            }
            std::string error;
            if (!scene.load(context, path, &error)) {
                std::cerr << "scene load failed: " << error << '\n';
                return 1;
            }
        }
        hikari::gfx::GraphicsSettings settings = lab_settings(options);
        if (options.phase == 1) {
            settings.sun.light_mode = 1;
            settings.post.tonemap = hikari::gfx::TonemapOperator::Linear;
        }
        hikari::gfx::Renderer renderer(context);
        update_window_title(hwnd);

        while (g_running) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!g_running) {
                break;
            }
            if (g_reload_requested) {
                renderer.reload_shaders();
                g_reload_requested = false;
            }
            if (g_capture_requested) {
                const auto captured = context.capture_scene();
                hikari::gfx_lab::CaptureData capture;
                capture.width = captured.width;
                capture.height = captured.height;
                capture.rgba = captured.rgba;
                std::string error;
                if (!hikari::gfx_lab::write_capture("gfx_phase2_capture.bmp", capture, &error)) {
                    std::cerr << "capture failed: " << error << '\n';
                }
                g_capture_requested = false;
            }

            RECT client{};
            GetClientRect(hwnd, &client);
            const std::uint32_t width =
                std::max<std::uint32_t>(static_cast<std::uint32_t>(client.right), 16u);
            const std::uint32_t height =
                std::max<std::uint32_t>(static_cast<std::uint32_t>(client.bottom), 16u);
            if (width != context.width || height != context.height) {
                context.resize(width, height);
                renderer.resize(width, height);
            }
            const hikari::render::Camera camera =
                frame_camera(scene, settings, options, width, height);
            hikari::gfx::FrameView view = make_view(scene, camera, settings, options, width, height);
            if (options.animate_sun) {
                static float angle = 0.0f;
                angle += 0.01f;
                view.sun_direction = hikari::Vec3(std::cos(angle), -0.8f, std::sin(angle)).normalize();
            }
            static const auto sky_clock = std::chrono::steady_clock::now();
            view.time_seconds =
                options.sky_time +
                std::chrono::duration<float>(std::chrono::steady_clock::now() - sky_clock)
                    .count();
            view.show_geometry = g_show_geometry;
            view.show_lighting = g_show_lighting;
            view.show_transparent = g_show_transparent;
            view.show_tonemap = g_show_tonemap;
            view.show_grid = g_show_grid;
            renderer.render(settings, view, context.render_target_view.Get());
            context.present(1);
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "lab failed: " << exception.what() << '\n';
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    if (options.help) {
        print_help();
        return 0;
    }
    if (options.list_targets) {
        std::cout << "GB0\nGB1\nGB2\nGB3\nGB4\nDepth\nHdr\nFinalLdr\n";
        return 0;
    }
    if (options.validate) {
        Options headless = options;
        headless.scene = "synthetic";
        headless.capture.clear();
        headless.probe.clear();
        headless.compare.clear();
        headless.phase = 2;
        return run_headless(headless);
    }
    if (!options.capture.empty() || !options.probe.empty() || !options.compare.empty()) {
        return run_headless(options);
    }
    return run_window(options);
}
