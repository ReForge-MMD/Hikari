#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <QByteArray>

#include "hikari/studio/project_io.hpp"

namespace {

hikari::studio::HikariMotion sample_motion() {
    hikari::studio::HikariMotion motion;
    motion.vmd.version = 2;
    motion.vmd.model_name = "テストモデル";

    hikari::formats::VmdBoneFrame bone;
    bone.name = "センター";
    bone.frame = 12;
    bone.position = {1.0f, 2.0f, 3.0f};
    bone.rotation = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
    bone.interpolation[0] = 13;
    bone.interpolation[4] = 114;
    motion.vmd.bone_frames = {bone};

    hikari::formats::VmdMorphFrame morph;
    morph.name = "まばたき";
    morph.frame = 24;
    morph.weight = 0.75f;
    motion.vmd.morph_frames = {morph};

    hikari::formats::VmdCameraFrame camera;
    camera.frame = 36;
    camera.distance = 42.0f;
    camera.position = {3.0f, 4.0f, 5.0f};
    camera.rotation = {0.1f, 0.2f, 0.3f};
    camera.fov = 35;
    camera.perspective = false;
    motion.vmd.camera_frames = {camera};

    hikari::formats::VmdLightFrame light;
    light.frame = 48;
    light.color = {0.1f, 0.2f, 0.3f};
    light.direction = {-0.4f, -0.5f, -0.6f};
    motion.vmd.light_frames = {light};

    hikari::formats::VmdShadowFrame shadow;
    shadow.frame = 60;
    shadow.mode = 2;
    shadow.distance = 8875.0f;
    motion.vmd.shadow_frames = {shadow};

    hikari::formats::VmdIkFrame ik;
    ik.frame = 72;
    ik.show = true;
    ik.ik_switches = {{"左足ＩＫ", false}, {"右足ＩＫ", true}};
    motion.vmd.ik_frames = {ik};

    motion.model_keys.push_back({0, 0.1f, {0.0f, 1.0f, 2.0f}, {0.0f, 0.159f, 0.0f}, true});
    motion.model_keys.push_back({24, 2.0f, {3.0f, 4.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, false});
    return motion;
}

} // namespace

TEST_CASE("HKM round trips VMD tracks and model transform keys", "[project]") {
    const auto motion = sample_motion();
    const QByteArray bytes = hikari::studio::hkm_to_json(motion);
    REQUIRE_FALSE(bytes.isEmpty());

    QString error;
    const auto parsed = hikari::studio::hkm_from_json(bytes, &error);
    REQUIRE(parsed.has_value());
    REQUIRE(error.isEmpty());

    REQUIRE(parsed->vmd.model_name == "テストモデル");
    REQUIRE(parsed->vmd.bone_frames.size() == 1);
    REQUIRE(parsed->vmd.bone_frames[0].name == "センター");
    REQUIRE(parsed->vmd.bone_frames[0].position[1] == Catch::Approx(2.0f));
    REQUIRE(parsed->vmd.bone_frames[0].interpolation[4] == 114);
    REQUIRE(parsed->vmd.morph_frames.size() == 1);
    REQUIRE(parsed->vmd.morph_frames[0].name == "まばたき");
    REQUIRE(parsed->vmd.camera_frames.size() == 1);
    REQUIRE_FALSE(parsed->vmd.camera_frames[0].perspective);
    REQUIRE(parsed->vmd.light_frames.size() == 1);
    REQUIRE(parsed->vmd.shadow_frames.size() == 1);
    REQUIRE(parsed->vmd.ik_frames.size() == 1);
    REQUIRE(parsed->vmd.ik_frames[0].ik_switches.size() == 2);
    REQUIRE_FALSE(parsed->vmd.ik_frames[0].ik_switches[0].enabled);

    REQUIRE(parsed->model_keys.size() == 2);
    REQUIRE(parsed->model_keys[0].scale == Catch::Approx(0.1f));
    REQUIRE(parsed->model_keys[0].position[2] == Catch::Approx(2.0f));
    REQUIRE(parsed->model_keys[0].rotation[1] == Catch::Approx(0.159f));
    REQUIRE(parsed->model_keys[1].frame == 24);
    REQUIRE(parsed->model_keys[1].scale == Catch::Approx(2.0f));
    REQUIRE_FALSE(parsed->model_keys[1].visible);
}

TEST_CASE("HKP round trips scene, models, authored tracks and motion", "[project]") {
    hikari::studio::ProjectData project;
    project.active_model = 1;
    project.current_frame = 15;
    project.max_frame = 120;
    project.play_start = 5;
    project.play_end = 100;
    project.camera_mode = false;
    project.scene.camera_pan = {1.0f, 2.0f, 3.0f};
    project.scene.camera_yaw = 0.3f;
    project.scene.camera_distance = -45.0f;
    project.scene.camera_target = hikari::studio::CameraTarget::SelectedBone;
    project.scene.camera_follow = true;
    project.scene.camera_parent_model = 1;
    project.scene.camera_parent_bone = 7;
    project.scene.camera_follow_offset = {1.0f, 2.0f, 3.0f};
    project.scene.camera_near_clip = 0.5f;
    project.scene.camera_far_clip = 5000.0f;
    project.scene.shadow_mode = hikari::studio::ShadowMode::Mode2;
    project.scene.gravity = {0.0f, -9.8f, 0.5f};

    hikari::studio::ProjectModel first;
    first.path = "models/first.pmx";
    first.name = "First";
    first.offset = {1.0f, 0.0f, -1.0f};
    first.rotation = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
    first.scale = 0.5f;
    hikari::studio::ProjectModel second;
    second.path = "models/second.pmx";
    second.name = "Second";
    second.scale = 2.0f;
    second.visible = false;
    project.models = {first, second};

    hikari::studio::TrackKeyframe key;
    key.position = {4.0f, 5.0f, 6.0f};
    key.rotation = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
    key.scale = 1.25f;
    key.interpolation = {13, 114, 114, 13};
    key.ik_enabled.insert("左足ＩＫ", false);
    key.parent_model = 1;
    key.parent_bone = 7;
    key.follow_offset = {4.0f, 5.0f, 6.0f};
    key.op_bindings.insert(hikari::studio::op_root_subject(),
                           hikari::studio::OpTarget{QStringLiteral("Second"),
                                                    QStringLiteral("センター")});
    key.model_ik[QStringLiteral("First")].insert(QStringLiteral("左足ＩＫ"), false);
    key.model_op[QStringLiteral("First")].insert(
        hikari::studio::op_root_subject(),
        hikari::studio::OpTarget{QStringLiteral("Second"), QStringLiteral("センター")});
    project.tracks.push_back({QStringLiteral("センター"), 12, key});
    project.tracks.push_back({QStringLiteral("model scale"), 12, key});
    project.motion = sample_motion();

    const QByteArray bytes = hikari::studio::hkp_to_json(project);
    REQUIRE_FALSE(bytes.isEmpty());
    QString error;
    const auto parsed = hikari::studio::hkp_from_json(bytes, &error);
    REQUIRE(parsed.has_value());
    REQUIRE(error.isEmpty());

    REQUIRE(parsed->active_model == 1);
    REQUIRE(parsed->current_frame == 15);
    REQUIRE(parsed->max_frame == 120);
    REQUIRE(parsed->play_start == 5);
    REQUIRE(parsed->play_end == 100);
    REQUIRE_FALSE(parsed->camera_mode);
    REQUIRE(parsed->scene.camera_pan[1] == Catch::Approx(2.0f));
    REQUIRE(parsed->scene.camera_distance == Catch::Approx(-45.0f));
    REQUIRE(parsed->scene.camera_target == hikari::studio::CameraTarget::SelectedBone);
    REQUIRE(parsed->scene.camera_follow);
    REQUIRE(parsed->scene.camera_parent_model == 1);
    REQUIRE(parsed->scene.camera_parent_bone == 7);
    REQUIRE(parsed->scene.camera_follow_offset[2] == Catch::Approx(3.0f));
    REQUIRE(parsed->scene.camera_near_clip == Catch::Approx(0.5f));
    REQUIRE(parsed->scene.camera_far_clip == Catch::Approx(5000.0f));
    REQUIRE(parsed->scene.shadow_mode == hikari::studio::ShadowMode::Mode2);
    REQUIRE(parsed->models.size() == 2);
    REQUIRE(parsed->models[0].path == "models/first.pmx");
    REQUIRE(parsed->models[0].scale == Catch::Approx(0.5f));
    REQUIRE(parsed->models[0].rotation.y == Catch::Approx(0.7071068f));
    REQUIRE(parsed->models[0].rotation.w == Catch::Approx(0.7071068f));
    REQUIRE_FALSE(parsed->models[1].visible);
    REQUIRE(parsed->tracks.size() == 2);
    REQUIRE(parsed->tracks[0].name == "センター");
    REQUIRE(parsed->tracks[0].key.interpolation[1] == 114);
    REQUIRE(parsed->tracks[0].key.ik_enabled.value("左足ＩＫ", true) == false);
    REQUIRE(parsed->tracks[0].key.parent_model == 1);
    REQUIRE(parsed->tracks[0].key.parent_bone == 7);
    REQUIRE(parsed->tracks[0].key.follow_offset[1] == Catch::Approx(5.0f));
    const auto op = parsed->tracks[0].key.op_bindings.value(
        hikari::studio::op_root_subject());
    REQUIRE(op.model == "Second");
    REQUIRE(op.bone == "センター");
    REQUIRE_FALSE(parsed->tracks[0].key.model_ik.value("First").value("左足ＩＫ", true));
    const auto model_op = parsed->tracks[0].key.model_op.value("First").value(
        hikari::studio::op_root_subject());
    REQUIRE(model_op.model == "Second");
    REQUIRE(parsed->motion.has_value());
    REQUIRE(parsed->motion->model_keys.size() == 2);
}

TEST_CASE("HKP round trips per-subset material overrides by name", "[project]") {
    hikari::studio::ProjectData project;
    hikari::studio::ProjectModel model;
    model.path = "models/coat.pmx";
    model.name = "Coat";
    hikari::studio::ProjectMaterialOverride first;
    first.subset = 3;
    first.response.specular = {0.11f, 0.12f, 0.13f};
    first.response.roughness = 0.27f;
    first.response.emissive = {0.5f, 0.25f, 0.125f};
    first.response.metallic = 0.9f;
    first.response.custom_a = 2.5f;
    first.response.custom_b = -0.5f;
    first.response.shading_model = static_cast<int>(hikari::gfx::ShadingModel::ClearCoat);
    hikari::studio::ProjectMaterialOverride second;
    second.subset = 0;
    second.response.shading_model = static_cast<int>(hikari::gfx::ShadingModel::Eye);
    model.materials = {first, second};
    project.models = {model};

    const QByteArray bytes = hikari::studio::hkp_to_json(project);
    QString error;
    const auto parsed = hikari::studio::hkp_from_json(bytes, &error);
    REQUIRE(parsed.has_value());
    REQUIRE(error.isEmpty());
    REQUIRE(parsed->models.size() == 1);
    const std::vector<hikari::studio::ProjectMaterialOverride>& materials =
        parsed->models[0].materials;
    REQUIRE(materials.size() == 2);
    REQUIRE(materials[0].subset == 3);
    REQUIRE(materials[0].response.shading_model ==
            static_cast<int>(hikari::gfx::ShadingModel::ClearCoat));
    REQUIRE(materials[0].response.specular[2] == Catch::Approx(0.13f));
    REQUIRE(materials[0].response.roughness == Catch::Approx(0.27f));
    REQUIRE(materials[0].response.emissive[1] == Catch::Approx(0.25f));
    REQUIRE(materials[0].response.metallic == Catch::Approx(0.9f));
    REQUIRE(materials[0].response.custom_a == Catch::Approx(2.5f));
    REQUIRE(materials[0].response.custom_b == Catch::Approx(-0.5f));
    REQUIRE(materials[1].subset == 0);
    REQUIRE(materials[1].response.shading_model ==
            static_cast<int>(hikari::gfx::ShadingModel::Eye));

    // The enum travels as a stable name, so the payload carries the name and not
    // the number.
    REQUIRE(bytes.contains("\"coat\""));
}

TEST_CASE("An unknown shading model name falls back to the default", "[project]") {
    const QByteArray bytes = R"({
        "format": "hikari-project",
        "version": 1,
        "models": [{
            "path": "future.pmx",
            "name": "Future",
            "materials": [{"subset": 2, "shading_model": "unobtainium", "roughness": 0.4}]
        }]
    })";
    QString error;
    const auto parsed = hikari::studio::hkp_from_json(bytes, &error);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->models.size() == 1);
    REQUIRE(parsed->models[0].materials.size() == 1);
    REQUIRE(parsed->models[0].materials[0].subset == 2);
    REQUIRE(parsed->models[0].materials[0].response.shading_model == 0);
    REQUIRE(parsed->models[0].materials[0].response.roughness == Catch::Approx(0.4f));
}

TEST_CASE("Old HKP files without model rotation load as identity", "[project]") {
    const QByteArray bytes = R"({
        "format": "hikari-project",
        "version": 1,
        "models": [{"path": "old.pmx", "name": "Old", "offset": [1, 2, 3], "scale": 1.0}]
    })";
    QString error;
    const auto parsed = hikari::studio::hkp_from_json(bytes, &error);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->models.size() == 1);
    REQUIRE(parsed->models[0].rotation.w == Catch::Approx(1.0f));
    REQUIRE(parsed->models[0].rotation.x == Catch::Approx(0.0f));
    // No materials array means no overrides, not an error.
    REQUIRE(parsed->models[0].materials.empty());
}
