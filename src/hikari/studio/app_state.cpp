#include "hikari/studio/app_state.hpp"

#include <QDir>

#include <algorithm>
#include <cmath>
#include <iterator>

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
    key.interpolation = default_interpolation;
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

void StudioAppState::push_undo_snapshot() {
    undo_stack.push_back({track_keyframes, selected_keyframes});
    if (undo_stack.size() > kUndoLimit) {
        undo_stack.erase(undo_stack.begin());
    }
    redo_stack.clear();
}

void StudioAppState::undo_keyframes() {
    if (undo_stack.empty()) {
        return;
    }
    redo_stack.push_back({track_keyframes, selected_keyframes});
    if (redo_stack.size() > kUndoLimit) {
        redo_stack.erase(redo_stack.begin());
    }
    KeyframeSnapshot snapshot = std::move(undo_stack.back());
    undo_stack.pop_back();
    track_keyframes = std::move(snapshot.track_keyframes);
    selected_keyframes = std::move(snapshot.selection);
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Undo keyframe edit"));
}

void StudioAppState::redo_keyframes() {
    if (redo_stack.empty()) {
        return;
    }
    undo_stack.push_back({track_keyframes, selected_keyframes});
    if (undo_stack.size() > kUndoLimit) {
        undo_stack.erase(undo_stack.begin());
    }
    KeyframeSnapshot snapshot = std::move(redo_stack.back());
    redo_stack.pop_back();
    track_keyframes = std::move(snapshot.track_keyframes);
    selected_keyframes = std::move(snapshot.selection);
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Redo keyframe edit"));
}

void StudioAppState::register_keyframe(const QString& track, unsigned int frame, bool record_undo) {
    if (record_undo) {
        push_undo_snapshot();
    }
    track_keyframes[track][frame] = capture_keyframe(track);
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Registered keyframe for '%1' at frame %2").arg(track).arg(frame));
}

void StudioAppState::register_ik_keyframe(const QMap<QString, bool>& states,
                                         unsigned int frame) {
    push_undo_snapshot();
    TrackKeyframe key;
    key.interpolation = default_interpolation;
    key.ik_enabled = states;
    track_keyframes[QStringLiteral("Disp/IK/OP")][frame] = std::move(key);
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Registered IK state for %1 bone(s) at frame %2")
                   .arg(states.size())
                   .arg(frame));
}

void StudioAppState::delete_keyframe(const QString& track, unsigned int frame, bool record_undo) {
    const auto it = track_keyframes.find(track);
    if (it == track_keyframes.end() || it->second.find(frame) == it->second.end()) {
        return;
    }
    if (record_undo) {
        push_undo_snapshot();
    }
    it->second.erase(frame);
    selected_keyframes.erase({track, frame});
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Deleted keyframe for '%1' at frame %2").arg(track).arg(frame));
}

void StudioAppState::delete_keyframes(
    const std::set<std::pair<QString, unsigned int>>& keys) {
    const auto exists = [this](const std::pair<QString, unsigned int>& key) {
        const auto track = track_keyframes.find(key.first);
        return track != track_keyframes.end() && track->second.find(key.second) != track->second.end();
    };
    const std::size_t found = static_cast<std::size_t>(
        std::count_if(keys.begin(), keys.end(), exists));
    if (found == 0) {
        return;
    }
    push_undo_snapshot();
    for (const auto& key : keys) {
        const auto track = track_keyframes.find(key.first);
        if (track != track_keyframes.end()) {
            track->second.erase(key.second);
        }
        selected_keyframes.erase(key);
    }
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Deleted %1 keyframe(s)").arg(static_cast<int>(found)));
}

void StudioAppState::delete_frame_line(unsigned int frame) {
    bool found = false;
    for (const auto& entry : track_keyframes) {
        if (entry.second.find(frame) != entry.second.end()) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    push_undo_snapshot();
    for (auto& [track, frames] : track_keyframes) {
        frames.erase(frame);
    }
    for (auto it = selected_keyframes.begin(); it != selected_keyframes.end();) {
        it = it->second == frame ? selected_keyframes.erase(it) : std::next(it);
    }
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Deleted keyframes at frame %1").arg(frame));
}

std::size_t StudioAppState::paste_keyframes(const std::vector<ClipboardKey>& keys,
                                            unsigned int anchor, int target) {
    if (keys.empty()) {
        return 0;
    }
    push_undo_snapshot();
    std::set<std::pair<QString, unsigned int>> pasted;
    for (const auto& entry : keys) {
        const int offset = static_cast<int>(entry.frame) - static_cast<int>(anchor);
        const int frame = std::max(0, target + offset);
        const unsigned int pasted_frame = static_cast<unsigned int>(frame);
        track_keyframes[entry.track][pasted_frame] = entry.key;
        pasted.insert({entry.track, pasted_frame});
    }
    selected_keyframes = std::move(pasted);
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Pasted %1 keyframe(s)").arg(static_cast<int>(keys.size())));
    return keys.size();
}

std::array<std::uint8_t, 4> StudioAppState::panel_interpolation() const {
    const auto to_byte = [](float value) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(value * 127.0f), 0L, 127L));
    };
    return {to_byte(curve_p1[0]), to_byte(curve_p1[1]), to_byte(curve_p2[0]),
            to_byte(curve_p2[1])};
}

void StudioAppState::apply_curve_to_selection() {
    const std::array<std::uint8_t, 4> curve =
        curve_auto ? std::array<std::uint8_t, 4>{20, 20, 20, 20} : panel_interpolation();
    if (selected_keyframes.empty()) {
        default_interpolation = curve;
        set_status(QStringLiteral("Interpolation curve set as the default for new keyframes"));
        return;
    }
    push_undo_snapshot();
    std::size_t count = 0;
    for (const auto& [track, frame] : selected_keyframes) {
        const auto track_it = track_keyframes.find(track);
        if (track_it == track_keyframes.end()) {
            continue;
        }
        const auto key_it = track_it->second.find(frame);
        if (key_it == track_it->second.end()) {
            continue;
        }
        key_it->second.interpolation = curve;
        ++count;
    }
    track_keys_dirty = true;
    ++keyframe_revision;
    set_status(QStringLiteral("Applied interpolation curve to %1 keyframe(s)")
                   .arg(static_cast<int>(count)));
}

} // namespace hikari::studio
