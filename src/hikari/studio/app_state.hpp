#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hikari::studio {

enum class EditorMode { Model, Camera };
enum class CameraTarget { Free, Model, SelectedBone };
enum class BoneTransformMode { Select, Rotate, Move };
enum class MorphCategory { Eyes = 0, Mouth = 1, Brow = 2, Other = 3 };
enum class PhysicsMode { Always, Anytime, Trace, Disabled };
enum class ShadowMode { Off, Mode1, Mode2 };
enum class FpsLimit { Fps30, Fps60, Unlimited };

// One MMD outside-parent (OP) registration carried by the Disp/IK/OP track.
// model is empty for "none", kOpGroundTarget for ground, otherwise the target
// model name; bone is the target bone name.
struct OpTarget {
    QString model;
    QString bone;
};

inline QString op_root_subject() { return QStringLiteral("<root>"); }
inline QString op_ground_target() { return QStringLiteral("<ground>"); }

// Snapshot of an animated value captured when a keyframe is registered.
// Per-track interpretation:
//   bone: position + rotation (local transform)
//   camera: position (pan), angles (yaw/pitch/roll radians), distance, fov
//   light: position (color), angles (direction)
//   shadow: weight (mode), distance (range), fov (detail)
//   gravity: position, weight (noise)
//   accessory: position, angles, weight (alpha), scale
//   morph: weight
struct TrackKeyframe {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> angles{0.0f, 0.0f, 0.0f};
    float weight = 0.0f;
    float distance = 0.0f;
    float fov = 0.0f;
    float scale = 1.0f;
    // MMD Bezier control points (X1, Y1, X2, Y2) in the 0..127 byte range.
    // Equal X/Y pairs evaluate as linear.
    std::array<std::uint8_t, 4> interpolation{20, 20, 20, 20};
    // IK on/off state per IK controller bone for the Disp/IK/OP track.
    QMap<QString, bool> ik_enabled;
    // Outside-parent registrations per subject bone for the same track.
    QMap<QString, OpTarget> op_bindings;
    // Per-model display state. MMD keeps a display-key track on every model;
    // this map namespaces the shared Hikari row the same way.
    QMap<QString, QMap<QString, bool>> model_ik;
    QMap<QString, QMap<QString, OpTarget>> model_op;
    // Camera follow-bone target carried by camera keys (MMD CameraKey pair).
    int parent_model = -1;
    int parent_bone = 0;
    // Camera offset from the followed bone, expressed in the bone's local
    // frame. MMD keeps the parent-relative camera position this way.
    std::array<float, 3> follow_offset{0.0f, 0.0f, 0.0f};
};

using TrackKeyframeMap = std::map<QString, std::map<unsigned int, TrackKeyframe>>;

struct KeyframeSnapshot {
    TrackKeyframeMap track_keyframes;
    std::set<std::pair<QString, unsigned int>> selection;
};

struct ClipboardKey {
    QString track;
    unsigned int frame = 0;
    TrackKeyframe key;
};

struct StudioAppState {
    EditorMode mode = EditorMode::Camera;
    bool model_loaded = false;
    QString model_name;
    QStringList loaded_models;
    int selected_model_idx = 0;
    float model_scale = 1.0f;
    bool model_scale_dirty = false;
    bool model_visible = true;
    bool model_delete_requested = false;
    QStringList model_bone_names;

    std::optional<int> selected_bone_idx;
    std::array<float, 3> bone_pos_override{0.0f, 0.0f, 0.0f};
    std::array<float, 3> bone_rot_euler_deg{0.0f, 0.0f, 0.0f};
    bool bone_transform_dirty = false;
    bool is_local_coord = true;
    BoneTransformMode bone_mode = BoneTransformMode::Select;
    bool box_select_active = false;
    bool show_bones = true;
    bool show_physics_bones = false;
    bool show_all_model_bones = false;

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
    TrackKeyframeMap track_keyframes;
    // Frames of the loaded VMD, shown on the timeline as a read-only reference.
    // The motion player still owns the animation; these entries never drive it.
    QMap<QString, QSet<unsigned int>> motion_keyframes;
    std::set<std::pair<QString, unsigned int>> selected_keyframes;
    bool track_keys_dirty = true;
    std::uint64_t keyframe_revision = 0;
    QString jump_frame_input = "0";

    bool curve_auto = true;
    std::array<float, 2> curve_p1{0.2f, 0.2f};
    std::array<float, 2> curve_p2{0.8f, 0.8f};
    std::array<std::uint8_t, 4> default_interpolation{20, 20, 20, 20};

    static constexpr std::size_t kUndoLimit = 64;
    std::vector<KeyframeSnapshot> undo_stack;
    std::vector<KeyframeSnapshot> redo_stack;

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

    // Studio camera distance: positive means the eye sits away from the focus,
    // negative means it crossed the pivot. VMD stores the opposite sign.
    float camera_distance = 45.0f;
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    float camera_roll = 0.0f;
    std::array<float, 3> camera_pan{0.0f, 10.0f, 0.0f};
    float camera_fov_deg = 30.0f;
    bool camera_orthographic = false;
    CameraTarget camera_target = CameraTarget::Free;
    // MMD "Track" (follow bone): camera rides the selected model's bone.
    bool camera_follow = false;
    int camera_parent_model = -1;
    int camera_parent_bone = 0;
    std::array<float, 3> camera_follow_offset{0.0f, 0.0f, 0.0f};
    // Near/far clipping. A larger near plane removes geometry around a POV
    // camera, such as the head the camera sits inside.
    float camera_near_clip = 0.1f;
    float camera_far_clip = 1000.0f;

    PhysicsMode physics_mode = PhysicsMode::Anytime;
    bool playtime_only = false;
    bool show_rigid_bodies = false;
    bool show_joints = false;
    std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
    float gravity_noise = 0.0f;
    float physics_speed = 3.0f;
    bool reset_physics_requested = false;
    bool ground_collision = true;

    FpsLimit fps_limit = FpsLimit::Fps60;
    bool show_grid = true;
    bool viewport_white_bg = true;
    bool ground_shadow = true;
    bool transparent_ground_shadow = true;
    bool wireframe = false;
    float edge_thickness = 1.0f;
    std::array<float, 4> edge_color{0.0f, 0.0f, 0.0f, 1.0f};
    bool save_cpu_power = false;
    bool fullscreen = false;
    bool info_display = true;

    bool jiggle_enabled = true;
    bool vsync = false;

    // Preview audio. The decoded samples live in the player; the state keeps
    // the display metadata and the waveform peaks for the timeline.
    QString audio_path;
    QString audio_name;
    double audio_duration = 0.0;
    double audio_peaks_per_second = 0.0;
    float audio_offset_frames = 0.0f;
    float audio_volume = 1.0f;
    std::vector<float> audio_peaks;

    int screen_size_width = 1280;
    int screen_size_height = 720;
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
    void push_undo_snapshot();
    bool can_undo() const { return !undo_stack.empty(); }
    bool can_redo() const { return !redo_stack.empty(); }
    void undo_keyframes();
    void redo_keyframes();

    void register_keyframe(const QString& track, unsigned int frame, bool record_undo = true);
    void register_ik_keyframe(const QMap<QString, bool>& states, unsigned int frame);
    void register_display_keyframe(const QString& track_model_name,
                                   const QMap<QString, bool>& ik_states,
                                   const QMap<QString, OpTarget>& op_bindings,
                                   unsigned int frame);
    void delete_keyframe(const QString& track, unsigned int frame, bool record_undo = true);
    void delete_keyframes(const std::set<std::pair<QString, unsigned int>>& keys);
    void delete_frame_line(unsigned int frame);
    std::size_t paste_keyframes(const std::vector<ClipboardKey>& keys, unsigned int anchor,
                                int target);
    void apply_curve_to_selection();
    std::array<std::uint8_t, 4> panel_interpolation() const;
    TrackKeyframe capture_keyframe(const QString& track) const;
    bool is_playing_back() const { return is_playing; }
};

} // namespace hikari::studio
