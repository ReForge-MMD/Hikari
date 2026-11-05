#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hikari::formats {

class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& message) : std::runtime_error(message) {}
};

enum class PmxEncoding : std::uint8_t { Utf16Le = 0, Utf8 = 1 };

struct PmxHeader {
    PmxEncoding encoding = PmxEncoding::Utf8;
    std::uint8_t additional_vec4_count = 0;
    std::uint8_t vertex_index_size = 4;
    std::uint8_t texture_index_size = 4;
    std::uint8_t material_index_size = 4;
    std::uint8_t bone_index_size = 4;
    std::uint8_t morph_index_size = 4;
    std::uint8_t rigid_body_index_size = 4;
};

struct PmxModelInfo {
    std::string name_local;
    std::string name_universal;
    std::string comment_local;
    std::string comment_universal;
};

struct PmxBdef1 { std::int32_t bone = 0; };
struct PmxBdef2 { std::int32_t bone1 = 0; std::int32_t bone2 = 0; float weight1 = 1.0f; };
struct PmxBdef4 { std::array<std::int32_t, 4> bones{}; std::array<float, 4> weights{}; };
struct PmxSdef {
    std::int32_t bone1 = 0;
    std::int32_t bone2 = 0;
    float weight1 = 1.0f;
    std::array<float, 3> c{};
    std::array<float, 3> r0{};
    std::array<float, 3> r1{};
};
struct PmxQdef { std::array<std::int32_t, 4> bones{}; std::array<float, 4> weights{}; };

enum class PmxWeightType : std::uint8_t { Bdef1, Bdef2, Bdef4, Sdef, Qdef };

struct PmxWeight {
    PmxWeightType type = PmxWeightType::Bdef1;
    PmxBdef1 bdef1;
    PmxBdef2 bdef2;
    PmxBdef4 bdef4;
    PmxSdef sdef;
    PmxQdef qdef;
};

struct PmxVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
    std::vector<std::array<float, 4>> extra_vec4;
    PmxWeight weight;
    float edge_scale = 1.0f;
};

struct PmxMaterial {
    std::string name_local;
    std::string name_universal;
    std::array<float, 4> diffuse{};
    std::array<float, 3> specular{};
    float shininess = 0.0f;
    std::array<float, 3> ambient{};
    std::uint8_t flags = 0;
    std::array<float, 4> edge_color{};
    float edge_size = 0.0f;
    std::int32_t texture_index = -1;
    std::int32_t sphere_texture_index = -1;
    std::uint8_t sphere_mode = 0;
    std::uint8_t toon_flag = 0;
    std::int32_t toon_index = -1;
    std::string comment;
    std::int32_t surface_count = 0;
};

struct PmxIkLink {
    std::int32_t bone_index = -1;
    bool has_limits = false;
    std::array<float, 3> limit_min{};
    std::array<float, 3> limit_max{};
};

struct PmxIk {
    std::int32_t target_bone = -1;
    std::int32_t loop_count = 1;
    float angle_limit = 0.0f;
    std::vector<PmxIkLink> links;
};

struct PmxBone {
    std::string name_local;
    std::string name_universal;
    std::array<float, 3> position{};
    std::int32_t parent_index = -1;
    std::int32_t deform_layer = 0;
    std::uint16_t flags = 0;
    bool has_tail_bone = false;
    std::int32_t tail_bone = -1;
    std::array<float, 3> tail_offset{};
    bool has_append_parent = false;
    std::int32_t append_parent = -1;
    float append_ratio = 0.0f;
    bool has_fixed_axis = false;
    std::array<float, 3> fixed_axis{};
    bool has_local_axis = false;
    std::array<float, 3> local_axis_x{};
    std::array<float, 3> local_axis_z{};
    bool has_external_parent_key = false;
    std::int32_t external_parent_key = -1;
    bool has_ik = false;
    PmxIk ik;
};

enum class PmxMorphType : std::uint8_t {
    Group = 0,
    Vertex = 1,
    Bone = 2,
    Uv = 3,
    ExtraUv1 = 4,
    ExtraUv2 = 5,
    ExtraUv3 = 6,
    ExtraUv4 = 7,
    Material = 8,
    Flip = 9,
    Impulse = 10,
};

struct PmxGroupMorphItem { std::int32_t morph_index = -1; float weight = 0.0f; };
struct PmxVertexMorphItem { std::int32_t vertex_index = 0; std::array<float, 3> offset{}; };
struct PmxBoneMorphItem {
    std::int32_t bone_index = -1;
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};
struct PmxUvMorphItem { std::int32_t vertex_index = 0; std::array<float, 4> offset{}; };
struct PmxMaterialMorphItem {
    std::int32_t material_index = -1;
    std::uint8_t calc_mode = 0;
    std::array<float, 4> diffuse{};
    std::array<float, 3> specular{};
    float shininess = 0.0f;
    std::array<float, 3> ambient{};
    std::array<float, 4> edge_color{};
    float edge_size = 0.0f;
    std::array<float, 4> texture_tint{};
    std::array<float, 4> sphere_tint{};
    std::array<float, 4> toon_tint{};
};
struct PmxImpulseMorphItem {
    std::int32_t rigid_body_index = -1;
    std::uint8_t local = 0;
    std::array<float, 3> velocity{};
    std::array<float, 3> torque{};
};

struct PmxMorph {
    std::string name_local;
    std::string name_universal;
    std::uint8_t category = 0;
    PmxMorphType type = PmxMorphType::Group;
    std::vector<PmxGroupMorphItem> group_items;
    std::vector<PmxVertexMorphItem> vertex_items;
    std::vector<PmxBoneMorphItem> bone_items;
    std::vector<PmxUvMorphItem> uv_items;
    std::uint8_t extra_uv_index = 0;
    std::vector<PmxMaterialMorphItem> material_items;
    std::vector<PmxImpulseMorphItem> impulse_items;
};

struct PmxDisplayFrameItem {
    bool is_morph = false;
    std::int32_t index = -1;
};

struct PmxDisplayFrame {
    std::string name_local;
    std::string name_universal;
    bool is_special = false;
    std::vector<PmxDisplayFrameItem> items;
};

struct PmxRigidBody {
    std::string name_local;
    std::string name_universal;
    std::int32_t bone_index = -1;
    std::uint8_t collision_group = 0;
    std::uint16_t collision_mask = 0;
    std::uint8_t shape = 0;
    std::array<float, 3> size{};
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    float mass = 1.0f;
    float move_damping = 0.5f;
    float rot_damping = 0.5f;
    float restitution = 0.0f;
    float friction = 0.0f;
    std::uint8_t mode = 0;
};

struct PmxJoint {
    std::string name_local;
    std::string name_universal;
    std::uint8_t joint_type = 0;
    std::int32_t body_a = -1;
    std::int32_t body_b = -1;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    std::array<float, 3> pos_min{};
    std::array<float, 3> pos_max{};
    std::array<float, 3> rot_min{};
    std::array<float, 3> rot_max{};
    std::array<float, 3> spring_pos{};
    std::array<float, 3> spring_rot{};
};

struct PmxModel {
    float version = 2.0f;
    PmxHeader header;
    PmxModelInfo info;
    std::vector<PmxVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::string> textures;
    std::vector<PmxMaterial> materials;
    std::vector<PmxBone> bones;
    std::vector<PmxMorph> morphs;
    std::vector<PmxDisplayFrame> display_frames;
    std::vector<PmxRigidBody> rigid_bodies;
    std::vector<PmxJoint> joints;

    static PmxModel parse(const std::vector<std::uint8_t>& bytes);
};

std::string sjis_to_utf8(const std::uint8_t* data, std::size_t size);
std::string utf8_to_sjis(const std::string& text);
std::string utf16le_to_utf8(const std::uint8_t* data, std::size_t size);

} // namespace hikari::formats
