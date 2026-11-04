#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>

#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace hikari::studio {

enum class EditorMode { Model, Camera };
enum class BoneTransformMode { Select, Rotate, Move };
enum class MorphCategory { Eyes = 0, Mouth = 1, Brow = 2, Other = 3 };
enum class PhysicsMode { Always, Anytime, Trace, Disabled };
enum class ShadowMode { Off, Mode1, Mode2 };
enum class FpsLimit { Fps30, Fps60, Unlimited };
enum class InterpolationTarget { Rotation, TranslationX, TranslationY, TranslationZ };
enum class EffectTab { Main, MaterialMap, DepthBuffer, NormalMap, Ssao, ReforgePbr };

struct EffectDialogState {
    EffectTab active_tab = EffectTab::ReforgePbr;
    float pbr_exposure = 1.0f;
    float pbr_bloom_intensity = 0.8f;
    float pbr_ssao_radius = 0.5f;
    float pbr_ssr_roughness_cutoff = 0.4f;
    float pbr_volumetric_fog_density = 0.02f;
    int pbr_tonemapper = 1;
};

struct StudioAppState {
    EditorMode mode = EditorMode::Model;
    QString model_name;
    QStringList loaded_models;
    int selected_model_idx = 0;
    bool model_visible = true;
    bool model_shadow_visible = true;
    bool model_delete_requested = false;
    QStringList model_bone_names;

    std::optional<int> selected_bone_idx;
    std::array<float, 3> bone_pos_override{0.0f, 0.0f, 0.0f};
    std::array<float, 3> bone_rot_euler_deg{0.0f, 0.0f, 0.0f};
    bool bone_transform_dirty = false;
    bool is_local_coord = false;
    BoneTransformMode bone_mode = BoneTransformMode::Select;
    bool box_select_active = false;
    bool show_bones = true;
    bool show_physics_bones = false;

    MorphCategory active_morph_category = MorphCategory::Eyes;
    std::array<QStringList, 4> category_morph_names;
    std::array<QString, 4> selected_morph_names;
    std::array<float, 4> morph_slider_values{0.0f, 0.0f, 0.0f, 0.0f};
    QMap<QString, float> morph_weights;
    bool morph_weights_dirty = false;

    float current_frame = 0.0f;
    int max_timeline_frame = 300;
    bool is_playing = false;
    bool loop_playback = true;
    std::pair<int, int> play_range{0, 300};
    float timeline_zoom = 1.0f;
    float timeline_scroll = 0.0f;
    std::map<QString, std::set<unsigned int>> track_keyframes;
    std::set<std::pair<QString, unsigned int>> selected_keyframes;
    QString jump_frame_input = "0";

    InterpolationTarget curve_target = InterpolationTarget::Rotation;
    bool curve_auto = true;
    std::array<float, 2> curve_p1{0.2f, 0.2f};
    std::array<float, 2> curve_p2{0.8f, 0.8f};

    std::array<float, 3> light_color{0.6f, 0.6f, 0.6f};
    std::array<float, 3> light_direction{-0.5f, -1.0f, -0.5f};

    ShadowMode shadow_mode = ShadowMode::Mode1;
    float shadow_range = 8875.0f;
    float shadow_detail = 0.5f;

    QString accessory_name;
    std::array<float, 3> accessory_pos{0.0f, 0.0f, 0.0f};
    std::array<float, 3> accessory_rot{0.0f, 0.0f, 0.0f};
    float accessory_scale = 1.0f;
    float accessory_alpha = 1.0f;
    bool accessory_shadow = true;

    float camera_distance = 35.0f;
    float camera_yaw = 0.0f;
    float camera_pitch = 0.15f;
    std::array<float, 3> camera_pan{0.0f, 10.0f, 0.0f};
    float camera_fov_deg = 30.0f;
    bool camera_orthographic = false;
    QString follow_bone;

    PhysicsMode physics_mode = PhysicsMode::Anytime;
    bool playtime_only = false;
    bool show_rigid_bodies = false;
    bool show_joints = false;
    std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
    float gravity_noise = 0.0f;
    bool reset_physics_requested = false;

    FpsLimit fps_limit = FpsLimit::Fps60;
    bool show_grid = true;
    bool show_axes = true;
    bool viewport_white_bg = true;
    bool ground_shadow = true;
    bool transparent_ground_shadow = true;
    bool antialiasing = true;
    bool wireframe = false;
    float edge_thickness = 1.0f;
    std::array<float, 4> edge_color{0.0f, 0.0f, 0.0f, 1.0f};
    bool save_cpu_power = false;
    bool fullscreen = false;
    bool info_display = true;

    bool multithreaded = true;
    bool jiggle_enabled = true;
    bool vsync = false;

    EffectDialogState effect_state;
    int screen_size_width = 1920;
    int screen_size_height = 1080;
    int video_render_start_frame = 0;
    int video_render_end_frame = 300;
    int video_render_fps = 30;

    QString status_message;
    QElapsedTimer status_timer;
    QString default_directory;

    StudioAppState();

    void set_status(const QString& message);
    std::optional<QString> current_status() const;
    void toggle_play();
    void step_frame(int delta);
    QString selected_track_name() const;
    void register_keyframe(const QString& track, unsigned int frame);
    void delete_keyframe(const QString& track, unsigned int frame);
    bool is_playing_back() const { return is_playing; }
};

} // namespace hikari::studio
