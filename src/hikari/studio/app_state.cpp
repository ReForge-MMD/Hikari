#include "hikari/studio/app_state.hpp"

#include <QDir>

#include <algorithm>

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
        track_keyframes[channel].insert(0);
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

void StudioAppState::register_keyframe(const QString& track, unsigned int frame) {
    track_keyframes[track].insert(frame);
    set_status(QStringLiteral("Registered keyframe for '%1' at frame %2").arg(track).arg(frame));
}

void StudioAppState::delete_keyframe(const QString& track, unsigned int frame) {
    const auto it = track_keyframes.find(track);
    if (it != track_keyframes.end()) {
        it->second.erase(frame);
        set_status(QStringLiteral("Deleted keyframe for '%1' at frame %2").arg(track).arg(frame));
    }
}

} // namespace hikari::studio
