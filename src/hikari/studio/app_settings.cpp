#include "hikari/studio/app_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

#include "hikari/studio/app_state.hpp"

namespace hikari::studio {
namespace {

template <typename Enum>
Enum read_enum(const QJsonValue& value, Enum fallback, int low, int high) {
    return static_cast<Enum>(
        std::clamp(value.toInt(static_cast<int>(fallback)), low, high));
}

} // namespace

QString default_app_settings_path() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) {
        base = QCoreApplication::applicationDirPath();
    }
    return QDir(base).filePath(QStringLiteral("settings.json"));
}

AppSettings::AppSettings() : path_(default_app_settings_path()) {}

AppSettings::AppSettings(QString path) : path_(std::move(path)) {}

bool AppSettings::load(QString* error) {
    root_ = {};
    camera_presets_ = {};
    QFile file(path_);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot open settings file: %1").arg(path_);
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot parse settings file: %1")
                         .arg(parse.errorString());
        }
        return false;
    }
    root_ = document.object();
    camera_presets_ = root_.value(QStringLiteral("camera_presets")).toObject();
    return true;
}

bool AppSettings::save(QString* error) const {
    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create settings directory: %1")
                         .arg(info.absolutePath());
        }
        return false;
    }
    QJsonObject root = root_;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("camera_presets")] = camera_presets_;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot write settings file: %1").arg(path_);
        }
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot save settings file: %1").arg(path_);
        }
        return false;
    }
    return true;
}

void AppSettings::apply_to(StudioAppState& state) const {
    const QString directory =
        root_.value(QStringLiteral("default_directory")).toString();
    if (!directory.isEmpty() && QDir(directory).exists()) {
        state.default_directory = directory;
    }

    const QJsonObject export_settings = root_.value(QStringLiteral("export")).toObject();
    state.video_render_start_frame =
        std::max(export_settings.value(QStringLiteral("start_frame"))
                     .toInt(state.video_render_start_frame),
                 0);
    state.video_render_end_frame =
        std::max(export_settings.value(QStringLiteral("end_frame"))
                     .toInt(state.video_render_end_frame),
                 0);
    state.video_render_fps =
        std::clamp(export_settings.value(QStringLiteral("fps")).toInt(state.video_render_fps),
                   1, 240);
    state.video_export_format =
        std::max(export_settings.value(QStringLiteral("format"))
                     .toInt(state.video_export_format),
                 0);
    state.video_export_bitrate_mbps =
        std::clamp(export_settings.value(QStringLiteral("bitrate_mbps"))
                       .toInt(state.video_export_bitrate_mbps),
                   1, 50);
    state.video_export_include_audio =
        export_settings.value(QStringLiteral("include_audio"))
            .toBool(state.video_export_include_audio);

    const QJsonObject view = root_.value(QStringLiteral("view")).toObject();
    state.show_grid = view.value(QStringLiteral("show_grid")).toBool(state.show_grid);
    state.viewport_white_bg =
        view.value(QStringLiteral("white_background")).toBool(state.viewport_white_bg);
    state.ground_shadow =
        view.value(QStringLiteral("ground_shadow")).toBool(state.ground_shadow);
    state.transparent_ground_shadow =
        view.value(QStringLiteral("transparent_ground_shadow"))
            .toBool(state.transparent_ground_shadow);
    state.wireframe = view.value(QStringLiteral("wireframe")).toBool(state.wireframe);
    state.save_cpu_power =
        view.value(QStringLiteral("save_cpu_power")).toBool(state.save_cpu_power);
    state.info_display =
        view.value(QStringLiteral("info_display")).toBool(state.info_display);
    state.vsync = view.value(QStringLiteral("vsync")).toBool(state.vsync);
    state.fps_limit =
        read_enum<FpsLimit>(view.value(QStringLiteral("fps_limit")), state.fps_limit, 0, 2);
    state.show_bones =
        view.value(QStringLiteral("show_bones")).toBool(state.show_bones);
    state.show_physics_bones =
        view.value(QStringLiteral("show_physics_bones")).toBool(state.show_physics_bones);
    state.show_all_model_bones =
        view.value(QStringLiteral("show_all_model_bones")).toBool(state.show_all_model_bones);
    state.is_local_coord =
        view.value(QStringLiteral("local_coordinates")).toBool(state.is_local_coord);
    state.loop_playback =
        view.value(QStringLiteral("loop_playback")).toBool(state.loop_playback);
    state.show_rigid_bodies =
        view.value(QStringLiteral("show_rigid_bodies")).toBool(state.show_rigid_bodies);
    state.show_joints = view.value(QStringLiteral("show_joints")).toBool(state.show_joints);
    state.screen_size_width =
        std::max(view.value(QStringLiteral("screen_width")).toInt(state.screen_size_width), 0);
    state.screen_size_height =
        std::max(view.value(QStringLiteral("screen_height")).toInt(state.screen_size_height), 0);

    const QJsonObject physics = root_.value(QStringLiteral("physics")).toObject();
    state.physics_mode = read_enum<PhysicsMode>(physics.value(QStringLiteral("mode")),
                                                state.physics_mode, 0, 3);
    state.playtime_only =
        physics.value(QStringLiteral("playtime_only")).toBool(state.playtime_only);
    state.ground_collision =
        physics.value(QStringLiteral("ground_collision")).toBool(state.ground_collision);
    state.physics_speed =
        std::clamp(static_cast<float>(physics.value(QStringLiteral("speed"))
                                          .toDouble(state.physics_speed)),
                   1.0f, 3.5f);
    state.jiggle_enabled =
        physics.value(QStringLiteral("jiggle_enabled")).toBool(state.jiggle_enabled);
}

void AppSettings::capture_from(const StudioAppState& state) {
    root_[QStringLiteral("version")] = 1;
    root_[QStringLiteral("default_directory")] = state.default_directory;

    QJsonObject export_settings;
    export_settings[QStringLiteral("start_frame")] = state.video_render_start_frame;
    export_settings[QStringLiteral("end_frame")] = state.video_render_end_frame;
    export_settings[QStringLiteral("fps")] = state.video_render_fps;
    export_settings[QStringLiteral("format")] = state.video_export_format;
    export_settings[QStringLiteral("bitrate_mbps")] = state.video_export_bitrate_mbps;
    export_settings[QStringLiteral("include_audio")] = state.video_export_include_audio;
    root_[QStringLiteral("export")] = export_settings;

    QJsonObject view;
    view[QStringLiteral("show_grid")] = state.show_grid;
    view[QStringLiteral("white_background")] = state.viewport_white_bg;
    view[QStringLiteral("ground_shadow")] = state.ground_shadow;
    view[QStringLiteral("transparent_ground_shadow")] = state.transparent_ground_shadow;
    view[QStringLiteral("wireframe")] = state.wireframe;
    view[QStringLiteral("save_cpu_power")] = state.save_cpu_power;
    view[QStringLiteral("info_display")] = state.info_display;
    view[QStringLiteral("vsync")] = state.vsync;
    view[QStringLiteral("fps_limit")] = static_cast<int>(state.fps_limit);
    view[QStringLiteral("show_bones")] = state.show_bones;
    view[QStringLiteral("show_physics_bones")] = state.show_physics_bones;
    view[QStringLiteral("show_all_model_bones")] = state.show_all_model_bones;
    view[QStringLiteral("local_coordinates")] = state.is_local_coord;
    view[QStringLiteral("loop_playback")] = state.loop_playback;
    view[QStringLiteral("show_rigid_bodies")] = state.show_rigid_bodies;
    view[QStringLiteral("show_joints")] = state.show_joints;
    view[QStringLiteral("screen_width")] = state.screen_size_width;
    view[QStringLiteral("screen_height")] = state.screen_size_height;
    root_[QStringLiteral("view")] = view;

    QJsonObject physics;
    physics[QStringLiteral("mode")] = static_cast<int>(state.physics_mode);
    physics[QStringLiteral("playtime_only")] = state.playtime_only;
    physics[QStringLiteral("ground_collision")] = state.ground_collision;
    physics[QStringLiteral("speed")] = state.physics_speed;
    physics[QStringLiteral("jiggle_enabled")] = state.jiggle_enabled;
    root_[QStringLiteral("physics")] = physics;

    root_[QStringLiteral("camera_presets")] = camera_presets_;
}

void AppSettings::set_camera_presets(const QJsonObject& presets) {
    camera_presets_ = presets;
    root_[QStringLiteral("camera_presets")] = presets;
}

} // namespace hikari::studio
