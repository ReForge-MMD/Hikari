#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "hikari/formats/pmx.hpp"
#include "hikari/formats/vmd.hpp"

namespace {

void append_i32(std::vector<std::uint8_t>& data, std::int32_t value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + 4);
}

void append_f32(std::vector<std::uint8_t>& data, float value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + 4);
}

void append_str(std::vector<std::uint8_t>& data, const char* text) {
    const auto length = static_cast<std::int32_t>(std::strlen(text));
    append_i32(data, length);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text);
    data.insert(data.end(), bytes, bytes + length);
}

void append_u8(std::vector<std::uint8_t>& data, std::uint8_t value) {
    data.push_back(value);
}

std::vector<std::uint8_t> minimal_pmx() {
    std::vector<std::uint8_t> data;
    data.insert(data.end(), {'P', 'M', 'X', ' '});
    append_f32(data, 2.0f);
    append_u8(data, 8);
    for (std::uint8_t value : {std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{4},
                               std::uint8_t{4}, std::uint8_t{4}, std::uint8_t{4},
                               std::uint8_t{4}, std::uint8_t{4}}) {
        append_u8(data, value);
    }
    append_str(data, "Test");
    append_str(data, "Test");
    append_str(data, "");
    append_str(data, "");
    // One vertex.
    append_i32(data, 1);
    for (float value : {0.0f, 0.0f, 0.0f}) append_f32(data, value);
    for (float value : {0.0f, 1.0f, 0.0f}) append_f32(data, value);
    for (float value : {0.0f, 0.0f}) append_f32(data, value);
    append_u8(data, 0); // BDEF1
    append_i32(data, 0);
    append_f32(data, 1.0f);
    // One triangle.
    append_i32(data, 3);
    append_i32(data, 0);
    append_i32(data, 0);
    append_i32(data, 0);
    // No textures.
    append_i32(data, 0);
    // One material.
    append_i32(data, 1);
    append_str(data, "Material");
    append_str(data, "Material");
    for (float value : {1.0f, 1.0f, 1.0f, 1.0f}) append_f32(data, value);
    for (float value : {0.0f, 0.0f, 0.0f}) append_f32(data, value);
    append_f32(data, 10.0f);
    for (float value : {0.5f, 0.5f, 0.5f}) append_f32(data, value);
    append_u8(data, 0);
    for (float value : {0.0f, 0.0f, 0.0f, 1.0f}) append_f32(data, value);
    append_f32(data, 1.0f);
    append_i32(data, -1);
    append_i32(data, -1);
    append_u8(data, 0);
    append_u8(data, 0);
    append_i32(data, -1);
    append_str(data, "");
    append_i32(data, 3);
    // One bone.
    append_i32(data, 1);
    append_str(data, "Center");
    append_str(data, "Center");
    for (float value : {0.0f, 10.0f, 0.0f}) append_f32(data, value);
    append_i32(data, -1);
    append_i32(data, 0);
    data.push_back(0);
    data.push_back(0);
    for (float value : {0.0f, 1.0f, 0.0f}) append_f32(data, value);
    // No morphs, display frames, rigid bodies or joints.
    append_i32(data, 0);
    append_i32(data, 0);
    append_i32(data, 0);
    append_i32(data, 0);
    return data;
}

} // namespace

TEST_CASE("PMX parser reads a minimal model", "[formats]") {
    const auto model = hikari::formats::PmxModel::parse(minimal_pmx());
    REQUIRE(model.version == Catch::Approx(2.0f));
    REQUIRE(model.info.name_local == "Test");
    REQUIRE(model.vertices.size() == 1);
    REQUIRE(model.indices.size() == 3);
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].surface_count == 3);
    REQUIRE(model.bones.size() == 1);
    REQUIRE(model.bones[0].position[1] == Catch::Approx(10.0f));
}

TEST_CASE("VPD parse and serialize round trip", "[formats]") {
    const std::string text =
        "Vocaloid Pose Data file\n"
        "model.osm;\n"
        "1;\n"
        "Bone0{\n"
        "\t// センター\n"
        "\t1.000000,2.000000,3.000000;\n"
        "\t0.000000,0.000000,0.000000,1.000000;\n"
        "}\n";
    const auto pose = hikari::formats::VpdPose::parse(text);
    REQUIRE(pose.model_name == "model.osm");
    REQUIRE(pose.bones.size() == 1);
    REQUIRE(pose.bones[0].name == "センター");
    REQUIRE(pose.bones[0].translation[1] == Catch::Approx(2.0f));

    const auto serialized = pose.to_vpd_string();
    const auto reparsed = hikari::formats::VpdPose::parse(serialized);
    REQUIRE(reparsed.bones.size() == 1);
    REQUIRE(reparsed.bones[0].translation[2] == Catch::Approx(3.0f));
}

TEST_CASE("VMD writer round trips every record type", "[formats]") {
    hikari::formats::VmdMotion motion;
    hikari::formats::VmdBoneFrame bone;
    bone.name = "センター";
    bone.frame = 12;
    bone.position = {1.0f, 2.0f, 3.0f};
    bone.rotation = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
    bone.interpolation[0] = 13;
    bone.interpolation[4] = 114;
    bone.interpolation[8] = 114;
    bone.interpolation[12] = 13;
    motion.bone_frames = {bone};

    hikari::formats::VmdMorphFrame morph;
    morph.name = "まばたき";
    morph.frame = 24;
    morph.weight = 0.75f;
    motion.morph_frames = {morph};

    hikari::formats::VmdCameraFrame camera;
    camera.frame = 36;
    camera.distance = 42.0f;
    camera.position = {3.0f, 4.0f, 5.0f};
    camera.rotation = {0.1f, 0.2f, 0.3f};
    camera.fov = 35;
    camera.perspective = true;
    hikari::formats::VmdCameraFrame ortho_camera = camera;
    ortho_camera.frame = 37;
    ortho_camera.fov = 40;
    ortho_camera.perspective = false;
    motion.camera_frames = {camera, ortho_camera};

    hikari::formats::VmdLightFrame light;
    light.frame = 48;
    light.color = {0.1f, 0.2f, 0.3f};
    light.direction = {-0.4f, -0.5f, -0.6f};
    motion.light_frames = {light};

    hikari::formats::VmdShadowFrame shadow;
    shadow.frame = 60;
    shadow.mode = 2;
    shadow.distance = 8875.0f;
    motion.shadow_frames = {shadow};

    hikari::formats::VmdIkFrame ik;
    ik.frame = 72;
    ik.show = true;
    ik.ik_switches = {{"左足ＩＫ", false}, {"右足ＩＫ", true}};
    motion.ik_frames = {ik};

    const auto bytes = motion.to_bytes("テストモデル");
    const auto parsed = hikari::formats::VmdMotion::parse(bytes);
    REQUIRE(parsed.version == 2);
    REQUIRE(parsed.model_name == "テストモデル");
    REQUIRE(parsed.bone_frames.size() == 1);
    REQUIRE(parsed.bone_frames[0].name == "センター");
    REQUIRE(parsed.bone_frames[0].frame == 12);
    REQUIRE(parsed.bone_frames[0].position[1] == Catch::Approx(2.0f));
    REQUIRE(parsed.bone_frames[0].interpolation[0] == 13);
    REQUIRE(parsed.bone_frames[0].interpolation[8] == 114);
    REQUIRE(parsed.morph_frames.size() == 1);
    REQUIRE(parsed.morph_frames[0].name == "まばたき");
    REQUIRE(parsed.morph_frames[0].weight == Catch::Approx(0.75f));
    REQUIRE(parsed.camera_frames.size() == 2);
    REQUIRE(parsed.camera_frames[0].distance == Catch::Approx(42.0f));
    REQUIRE(parsed.camera_frames[0].fov == 35);
    REQUIRE(parsed.camera_frames[0].perspective);
    REQUIRE(parsed.camera_frames[1].fov == 40);
    REQUIRE_FALSE(parsed.camera_frames[1].perspective);
    REQUIRE(parsed.light_frames.size() == 1);
    REQUIRE(parsed.light_frames[0].direction[2] == Catch::Approx(-0.6f));
    REQUIRE(parsed.shadow_frames.size() == 1);
    REQUIRE(parsed.shadow_frames[0].mode == 2);
    REQUIRE(parsed.ik_frames.size() == 1);
    REQUIRE(parsed.ik_frames[0].frame == 72);
    REQUIRE(parsed.ik_frames[0].ik_switches.size() == 2);
    REQUIRE(parsed.ik_frames[0].ik_switches[0].name == "左足ＩＫ");
    REQUIRE_FALSE(parsed.ik_frames[0].ik_switches[0].enabled);
    REQUIRE(parsed.ik_frames[0].ik_switches[1].name == "右足ＩＫ");
    REQUIRE(parsed.ik_frames[0].ik_switches[1].enabled);
}

TEST_CASE("VMD writer truncates long names on a Shift-JIS boundary", "[formats]") {
    hikari::formats::VmdMotion motion;
    hikari::formats::VmdMorphFrame morph;
    morph.name = "ああああああああ";
    motion.morph_frames = {morph};
    const auto parsed = hikari::formats::VmdMotion::parse(motion.to_bytes("model"));
    REQUIRE(parsed.morph_frames.size() == 1);
    REQUIRE(parsed.morph_frames[0].name == "あああああああ");
}

TEST_CASE("VMD parser reads an empty motion", "[formats]") {
    std::vector<std::uint8_t> data(30, 0);
    const char magic[] = "Vocaloid Motion Data 0002";
    std::memcpy(data.data(), magic, sizeof(magic) - 1);
    std::vector<std::uint8_t> name(20, 0);
    std::memcpy(name.data(), "Test", 4);
    data.insert(data.end(), name.begin(), name.end());
    for (int i = 0; i < 6; ++i) {
        append_i32(data, 0);
    }
    const auto motion = hikari::formats::VmdMotion::parse(data);
    REQUIRE(motion.version == 2);
    REQUIRE(motion.model_name == "Test");
    REQUIRE(motion.bone_frames.empty());
}
