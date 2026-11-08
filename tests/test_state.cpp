#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "hikari/math.hpp"
#include "hikari/studio/app_state.hpp"
#include "hikari/studio/localization.hpp"
#include "hikari/studio/model_instance.hpp"

using hikari::Quat;
using hikari::Vec3;
using hikari::studio::ModelTransformState;
using hikari::studio::StudioAppState;

TEST_CASE("Model transform applies scale, then rotation, then translation", "[state]") {
    hikari::studio::ModelInstance model;
    model.offset = Vec3(1.0f, 0.0f, 0.0f);
    model.rotation = Quat::from_axis_angle(Vec3::axis_y(), 1.5707963f);
    model.scale = 2.0f;

    const Vec3 point = model.model_point(Vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(point.x == Catch::Approx(1.0f).margin(1.0e-4f));
    REQUIRE(point.y == Catch::Approx(0.0f).margin(1.0e-4f));
    REQUIRE(point.z == Catch::Approx(-2.0f).margin(1.0e-4f));
}

TEST_CASE("Scene snapshot restores camera, model transforms and selection", "[state]") {
    StudioAppState state;
    state.camera_pan = {1.0f, 2.0f, 3.0f};
    state.camera_yaw = 0.25f;
    state.light_color = {0.1f, 0.2f, 0.3f};
    state.audio_offset_frames = 12.0f;
    state.model_transforms = {ModelTransformState{Vec3(4.0f, 5.0f, 6.0f),
                                                  Quat::identity(), 1.5f, true}};
    state.snapshot_active_model = 0;
    state.track_keyframes[QStringLiteral("center")][5].position = {7.0f, 8.0f, 9.0f};
    state.selected_keyframes.insert({QStringLiteral("center"), 5});

    state.push_undo_snapshot();

    state.camera_yaw = 1.0f;
    state.camera_pan = {9.0f, 9.0f, 9.0f};
    state.light_color = {0.9f, 0.9f, 0.9f};
    state.audio_offset_frames = 99.0f;
    state.model_transforms[0].offset = Vec3(0.0f, 0.0f, 0.0f);
    state.track_keyframes.clear();
    state.selected_keyframes.clear();

    state.undo_keyframes();

    REQUIRE(state.camera_yaw == Catch::Approx(0.25f));
    REQUIRE(state.camera_pan[1] == Catch::Approx(2.0f));
    REQUIRE(state.light_color[2] == Catch::Approx(0.3f));
    REQUIRE(state.audio_offset_frames == Catch::Approx(12.0f));
    REQUIRE(state.model_transforms.size() == 1);
    REQUIRE(state.model_transforms[0].offset.z == Catch::Approx(6.0f));
    REQUIRE(state.track_keyframes.at(QStringLiteral("center")).at(5).position[0] ==
            Catch::Approx(7.0f));
    REQUIRE(state.selected_keyframes.count({QStringLiteral("center"), 5}) == 1);

    state.redo_keyframes();
    REQUIRE(state.camera_yaw == Catch::Approx(1.0f));
    REQUIRE(state.model_transforms[0].offset.z == Catch::Approx(0.0f));
}

TEST_CASE("A new mutation clears the redo stack", "[state]") {
    StudioAppState state;
    state.camera_yaw = 0.0f;
    state.push_undo_snapshot();
    state.camera_yaw = 1.0f;
    state.undo_keyframes();
    REQUIRE(state.can_redo());

    state.push_undo_snapshot();
    REQUIRE_FALSE(state.can_redo());
}

TEST_CASE("Motion data is snapshotted only for VMD edits", "[state]") {    StudioAppState state;
    state.capture_motion = [] {
        hikari::formats::VmdMotion motion;
        motion.version = 2;
        return std::optional<hikari::formats::VmdMotion>(motion);
    };
    bool restored = false;
    state.restore_motion = [&restored](const hikari::formats::VmdMotion&) { restored = true; };

    state.push_undo_snapshot();
    REQUIRE_FALSE(state.undo_stack.back().motion);

    state.begin_motion_edit();
    state.push_undo_snapshot();
    REQUIRE(state.undo_stack.back().motion);
    state.undo_keyframes();
    REQUIRE(restored);
}

TEST_CASE("Localization translates labels and formatted statuses", "[state]") {
    hikari::studio::localization::set_language(hikari::studio::localization::Language::Russian);
    REQUIRE(hikari::studio::localization::text(QStringLiteral("Camera")) ==
            QStringLiteral("Камера"));
    REQUIRE(hikari::studio::localization::message(QStringLiteral("Moved 2 keyframe(s)")) ==
            QStringLiteral("Перемещено ключей: 2"));
    REQUIRE(hikari::studio::localization::message(
                QStringLiteral("Registered keyframe for 'center' at frame 7")) ==
            QStringLiteral("Ключ для «center» записан на кадре 7"));

    hikari::studio::localization::set_language(hikari::studio::localization::Language::Japanese);
    REQUIRE(hikari::studio::localization::text(QStringLiteral("Model")) ==
            QStringLiteral("モデル"));

    hikari::studio::localization::set_language(hikari::studio::localization::Language::English);
    REQUIRE(hikari::studio::localization::text(QStringLiteral("Model")) ==
            QStringLiteral("Model"));
}
