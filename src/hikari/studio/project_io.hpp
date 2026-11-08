#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>

#include "hikari/formats/vmd.hpp"
#include "hikari/math.hpp"
#include "hikari/studio/app_state.hpp"

namespace hikari::studio {

// Model-level transform carried by Hikari Motion files. Standard VMD has no
// channel for model scale, so these keys travel beside the VMD payload.
struct ModelTransformKey {
    std::uint32_t frame = 0;
    float scale = 1.0f;
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    // MMD Euler angles in radians: x = pitch, y = yaw, z = roll.
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    bool visible = true;
};

// VMD-compatible motion plus the Hikari-owned model transform track.
struct HikariMotion {
    formats::VmdMotion vmd;
    std::vector<ModelTransformKey> model_keys;
};

struct ProjectModel {
    QString path;
    QString name;
    Vec3 offset{0.0f, 0.0f, 0.0f};
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float scale = 1.0f;
    bool visible = true;
};

struct ProjectScene {
    std::array<float, 3> camera_pan{0.0f, 10.0f, 0.0f};
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    float camera_roll = 0.0f;
    float camera_distance = 45.0f;
    float camera_fov_deg = 30.0f;
    bool camera_orthographic = false;
    CameraTarget camera_target = CameraTarget::Free;
    bool camera_follow = false;
    int camera_parent_model = -1;
    int camera_parent_bone = 0;
    std::array<float, 3> camera_follow_offset{0.0f, 0.0f, 0.0f};
    float camera_near_clip = 0.1f;
    float camera_far_clip = 1000.0f;
    QString audio_path;
    float audio_offset_frames = 0.0f;
    float audio_volume = 1.0f;
    std::array<float, 3> light_color{0.6f, 0.6f, 0.6f};
    std::array<float, 3> light_direction{-0.5f, -1.0f, -0.5f};
    ShadowMode shadow_mode = ShadowMode::Mode1;
    float shadow_range = 8875.0f;
    float shadow_detail = 0.5f;
    std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
    float gravity_noise = 0.0f;
};

struct ProjectTrack {
    QString name;
    unsigned int frame = 0;
    TrackKeyframe key;
};

struct ProjectData {
    std::vector<ProjectModel> models;
    int active_model = -1;
    int current_frame = 0;
    int max_frame = 300;
    int play_start = 0;
    int play_end = 300;
    bool camera_mode = true;
    ProjectScene scene;
    std::vector<ProjectTrack> tracks;
    std::optional<HikariMotion> motion;
};

QByteArray hkm_to_json(const HikariMotion& motion);
std::optional<HikariMotion> hkm_from_json(const QByteArray& bytes, QString* error = nullptr);

QByteArray hkp_to_json(const ProjectData& project);
std::optional<ProjectData> hkp_from_json(const QByteArray& bytes, QString* error = nullptr);

bool write_project_file(const QString& path, const QByteArray& bytes, QString* error = nullptr);
std::optional<QByteArray> read_project_file(const QString& path, QString* error = nullptr);

} // namespace hikari::studio
