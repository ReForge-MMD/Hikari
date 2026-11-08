#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "hikari/studio/app_settings.hpp"
#include "hikari/studio/app_state.hpp"

using hikari::studio::AppSettings;
using hikari::studio::FpsLimit;
using hikari::studio::PhysicsMode;
using hikari::studio::StudioAppState;

TEST_CASE("Application settings round-trip through a JSON file", "[settings]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));

    StudioAppState source;
    source.default_directory = directory.path();
    source.video_export_format = 4;
    source.video_export_bitrate_mbps = 14;
    source.video_render_fps = 60;
    source.video_export_include_audio = false;
    source.video_render_start_frame = 10;
    source.video_render_end_frame = 240;
    source.show_grid = false;
    source.viewport_white_bg = false;
    source.ground_shadow = false;
    source.transparent_ground_shadow = false;
    source.wireframe = true;
    source.save_cpu_power = true;
    source.info_display = false;
    source.vsync = true;
    source.fps_limit = FpsLimit::Fps30;
    source.show_bones = false;
    source.show_physics_bones = true;
    source.show_all_model_bones = true;
    source.is_local_coord = false;
    source.loop_playback = false;
    source.show_rigid_bodies = true;
    source.show_joints = true;
    source.screen_size_width = 1920;
    source.screen_size_height = 1080;
    source.physics_mode = PhysicsMode::Disabled;
    source.playtime_only = true;
    source.ground_collision = false;
    source.physics_speed = 2.5f;
    source.jiggle_enabled = false;

    QJsonObject presets;
    QJsonObject preset;
    preset[QStringLiteral("distance")] = 12.5;
    presets[QStringLiteral("Close up")] = preset;

    AppSettings writer(path);
    writer.capture_from(source);
    writer.set_camera_presets(presets);
    REQUIRE(writer.save());

    AppSettings reader(path);
    REQUIRE(reader.load());
    StudioAppState loaded;
    reader.apply_to(loaded);

    REQUIRE(loaded.default_directory == directory.path());
    REQUIRE(loaded.video_export_format == 4);
    REQUIRE(loaded.video_export_bitrate_mbps == 14);
    REQUIRE(loaded.video_render_fps == 60);
    REQUIRE_FALSE(loaded.video_export_include_audio);
    REQUIRE(loaded.video_render_start_frame == 10);
    REQUIRE(loaded.video_render_end_frame == 240);
    REQUIRE_FALSE(loaded.show_grid);
    REQUIRE_FALSE(loaded.viewport_white_bg);
    REQUIRE_FALSE(loaded.ground_shadow);
    REQUIRE_FALSE(loaded.transparent_ground_shadow);
    REQUIRE(loaded.wireframe);
    REQUIRE(loaded.save_cpu_power);
    REQUIRE_FALSE(loaded.info_display);
    REQUIRE(loaded.vsync);
    REQUIRE(loaded.fps_limit == FpsLimit::Fps30);
    REQUIRE_FALSE(loaded.show_bones);
    REQUIRE(loaded.show_physics_bones);
    REQUIRE(loaded.show_all_model_bones);
    REQUIRE_FALSE(loaded.is_local_coord);
    REQUIRE_FALSE(loaded.loop_playback);
    REQUIRE(loaded.show_rigid_bodies);
    REQUIRE(loaded.show_joints);
    REQUIRE(loaded.screen_size_width == 1920);
    REQUIRE(loaded.screen_size_height == 1080);
    REQUIRE(loaded.physics_mode == PhysicsMode::Disabled);
    REQUIRE(loaded.playtime_only);
    REQUIRE_FALSE(loaded.ground_collision);
    REQUIRE(loaded.physics_speed == Catch::Approx(2.5f));
    REQUIRE_FALSE(loaded.jiggle_enabled);

    REQUIRE(reader.camera_presets().contains(QStringLiteral("Close up")));
    REQUIRE(reader.camera_presets()
                .value(QStringLiteral("Close up"))
                .toObject()
                .value(QStringLiteral("distance"))
                .toDouble() == Catch::Approx(12.5));
}

TEST_CASE("Missing application settings file keeps defaults", "[settings]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("missing.json"));

    AppSettings settings(path);
    REQUIRE(settings.load());

    StudioAppState state;
    const QString default_directory = state.default_directory;
    const bool show_grid = state.show_grid;
    settings.apply_to(state);

    REQUIRE(state.default_directory == default_directory);
    REQUIRE(state.show_grid == show_grid);
    REQUIRE(settings.camera_presets().isEmpty());
}

TEST_CASE("Corrupt application settings file reports an error", "[settings]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();

    AppSettings settings(path);
    QString error;
    REQUIRE_FALSE(settings.load(&error));
    REQUIRE_FALSE(error.isEmpty());

    StudioAppState state;
    const bool show_grid = state.show_grid;
    settings.apply_to(state);
    REQUIRE(state.show_grid == show_grid);
}

TEST_CASE("Application settings path is user-writable", "[settings]") {
    const QString path = hikari::studio::default_app_settings_path();
    REQUIRE_FALSE(path.isEmpty());
    REQUIRE(path.endsWith(QStringLiteral("settings.json")));
    REQUIRE(QFileInfo(path).isAbsolute());
}
