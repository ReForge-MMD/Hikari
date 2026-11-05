#include "hikari/studio/app_state.hpp"

#include <QDir>

#include <algorithm>

#include "hikari/math.hpp"

namespace hikari::studio {

StudioAppState::StudioAppState() {
    category_morph_names[0] = {"まばたき", "笑い", "ウィンク", "じと目"};
    category_morph_names[1] = {"あ", "い", "う", "え", "お", "▲"};
    category_morph_names[2] = {"真面目", "困る", "にこり", "怒り"};
    category_morph_names[3] = {"涙", "照れ", "青ざめ", "星目"};
    selected_morph_names[0] = "まばたき";
    selected_morph_names[1] = "あ";
    selected_morph_names[2] = "真面目";
    selected_morph_names[3] = "涙";

    for (const QString& channel : {QStringLiteral("camera"), QStringLiteral("light"),
                                   QStringLiteral("shadow"), QStringLiteral("gravity")}) {
        track_keyframes[channel][0] = capture_keyframe(channel);
    }
    default_directory = QDir::currentPath();
}

void StudioAppState::set_status(const QString& message) {
    status_message = message;
    status_timer.restart();
}

std::optional<QString> StudioAppState::current_status() const {
    if (!status_message.isEmpty() && status_timer.isValid() && status_timer.elapsed() < 5000) {
        return status_message;
    }
    return std::nullopt;
}

void StudioAppState::toggle_play() {
    is_playing = !is_playing;
}

void StudioAppState::step_frame(int delta) {
    const int next = std::clamp(static_cast<int>(current_frame) + delta, 0, max_timeline_frame);
    current_frame = static_cast<float>(next);
    jump_frame_input = QString::number(next);
}

QString StudioAppState::selected_track_name() const {
    if (mode == EditorMode::Camera) {
        return QStringLiteral("camera");
    }
    if (selected_bone_idx && *selected_bone_idx < model_bone_names.size()) {
        return model_bone_names[*selected_bone_idx];
    }
    return QStringLiteral("center");
}

TrackKeyframe StudioAppState::capture_keyframe(const QString& track) const {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    TrackKeyframe key;
    if (track == QStringLiteral("camera")) {
        key.position = camera_pan;
        key.angles = {camera_yaw, camera_pitch, camera_roll};
        key.distance = camera_distance;
        key.fov = camera_fov_deg;
        return key;
    }
    if (track == QStringLiteral("light")) {
        key.position = light_color;
        key.angles = light_direction;
        return key;
    }
    if (track == QStringLiteral("shadow")) {
        key.weight = static_cast<float>(shadow_mode);
        key.distance = shadow_range;
        key.fov = shadow_detail;
        return key;
    }
    if (track == QStringLiteral("gravity")) {
        key.position = gravity;
        key.weight = gravity_noise;
        return key;
    }
    if (track == QStringLiteral("accessory")) {
        key.position = accessory_pos;
        key.angles = accessory_rot;
        key.weight = accessory_alpha;
        key.scale = accessory_scale;
        return key;
    }

    bool is_morph = false;
    for (const auto& names : category_morph_names) {
        if (names.contains(track)) {
            is_morph = true;
            break;
        }
    }
    if (is_morph) {
        key.weight = morph_weights.value(track, 0.0f);
        return key;
    }

    const Vec3 euler(bone_rot_euler_deg[0] * kDegToRad, bone_rot_euler_deg[1] * kDegToRad,
                     bone_rot_euler_deg[2] * kDegToRad);
    key.position = bone_pos_override;
    key.rotation = Quat::from_euler_xyz(euler).to_array();
    return key;
}

void StudioAppState::register_keyframe(const QString& track, unsigned int frame) {
    track_keyframes[track][frame] = capture_keyframe(track);
    track_keys_dirty = true;
    set_status(QStringLiteral("Registered keyframe for '%1' at frame %2").arg(track).arg(frame));
}

void StudioAppState::delete_keyframe(const QString& track, unsigned int frame) {
    const auto it = track_keyframes.find(track);
    if (it != track_keyframes.end()) {
        it->second.erase(frame);
        track_keys_dirty = true;
        set_status(QStringLiteral("Deleted keyframe for '%1' at frame %2").arg(track).arg(frame));
    }
}

} // namespace hikari::studio
