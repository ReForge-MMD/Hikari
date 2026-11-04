#include "hikari/formats/pmx.hpp"

#include <windows.h>

#include <cstring>
#include <limits>

namespace hikari::formats {
namespace {

std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                          nullptr, nullptr);
    return out;
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size, PmxHeader header)
        : data_(data), size_(size), header_(header) {}

    std::size_t remaining() const { return size_ - position_; }

    void check_count(std::int32_t count, std::size_t min_bytes) const {
        if (count < 0) {
            throw FormatError("negative element count");
        }
        const auto n = static_cast<std::size_t>(count);
        if (min_bytes != 0 && n > remaining() / min_bytes) {
            throw FormatError("element count exceeds remaining data");
        }
    }

    std::uint8_t read_u8() {
        require(1);
        return data_[position_++];
    }

    std::uint16_t read_u16() {
        require(2);
        const std::uint16_t v = static_cast<std::uint16_t>(data_[position_]) |
                                (static_cast<std::uint16_t>(data_[position_ + 1]) << 8);
        position_ += 2;
        return v;
    }

    std::int32_t read_i32() {
        require(4);
        const std::uint32_t v = static_cast<std::uint32_t>(data_[position_]) |
                                (static_cast<std::uint32_t>(data_[position_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(data_[position_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(data_[position_ + 3]) << 24);
        position_ += 4;
        return static_cast<std::int32_t>(v);
    }

    float read_f32() {
        const std::int32_t bits = read_i32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::array<float, 2> read_vec2() { return {read_f32(), read_f32()}; }
    std::array<float, 3> read_vec3() { return {read_f32(), read_f32(), read_f32()}; }
    std::array<float, 4> read_vec4() { return {read_f32(), read_f32(), read_f32(), read_f32()}; }

    std::string read_string() {
        const std::int32_t len = read_i32();
        if (len <= 0) {
            return {};
        }
        const auto n = static_cast<std::size_t>(len);
        require(n);
        const std::uint8_t* ptr = data_ + position_;
        position_ += n;
        if (header_.encoding == PmxEncoding::Utf16Le) {
            return utf16le_to_utf8(ptr, n);
        }
        return std::string(reinterpret_cast<const char*>(ptr), n);
    }

    std::int32_t read_index(std::uint8_t size, bool is_signed) {
        switch (size) {
        case 1: {
            const std::uint8_t v = read_u8();
            return is_signed ? static_cast<std::int32_t>(static_cast<std::int8_t>(v))
                             : static_cast<std::int32_t>(v);
        }
        case 2: {
            require(2);
            const std::uint16_t v = read_u16();
            return is_signed ? static_cast<std::int32_t>(static_cast<std::int16_t>(v))
                             : static_cast<std::int32_t>(v);
        }
        case 4:
            return read_i32();
        default:
            throw FormatError("invalid index size");
        }
    }

    std::uint32_t read_vertex_index() {
        return static_cast<std::uint32_t>(read_index(header_.vertex_index_size, false));
    }
    std::int32_t read_bone_index() { return read_index(header_.bone_index_size, true); }
    std::int32_t read_texture_index() { return read_index(header_.texture_index_size, true); }
    std::int32_t read_material_index() { return read_index(header_.material_index_size, true); }
    std::int32_t read_morph_index() { return read_index(header_.morph_index_size, true); }
    std::int32_t read_rigid_body_index() { return read_index(header_.rigid_body_index_size, true); }

private:
    void require(std::size_t n) const {
        if (n > remaining()) {
            throw FormatError("unexpected end of PMX data");
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    PmxHeader header_;
};

PmxWeight read_weight(Reader& r) {
    PmxWeight weight;
    const auto type = static_cast<PmxWeightType>(r.read_u8());
    weight.type = type;
    switch (type) {
    case PmxWeightType::Bdef1:
        weight.bdef1.bone = r.read_bone_index();
        break;
    case PmxWeightType::Bdef2:
        weight.bdef2.bone1 = r.read_bone_index();
        weight.bdef2.bone2 = r.read_bone_index();
        weight.bdef2.weight1 = r.read_f32();
        break;
    case PmxWeightType::Bdef4:
        for (auto& b : weight.bdef4.bones) b = r.read_bone_index();
        for (auto& w : weight.bdef4.weights) w = r.read_f32();
        break;
    case PmxWeightType::Sdef:
        weight.sdef.bone1 = r.read_bone_index();
        weight.sdef.bone2 = r.read_bone_index();
        weight.sdef.weight1 = r.read_f32();
        weight.sdef.c = r.read_vec3();
        weight.sdef.r0 = r.read_vec3();
        weight.sdef.r1 = r.read_vec3();
        break;
    case PmxWeightType::Qdef:
        for (auto& b : weight.qdef.bones) b = r.read_bone_index();
        for (auto& w : weight.qdef.weights) w = r.read_f32();
        break;
    default:
        throw FormatError("unknown vertex weight type");
    }
    return weight;
}

} // namespace

std::string utf16le_to_utf8(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 2) {
        return {};
    }
    const std::size_t units = size / 2;
    std::wstring wide(units, L'\0');
    for (std::size_t i = 0; i < units; ++i) {
        wide[i] = static_cast<wchar_t>(static_cast<std::uint16_t>(data[i * 2]) |
                                       (static_cast<std::uint16_t>(data[i * 2 + 1]) << 8));
    }
    return wide_to_utf8(wide);
}

std::string sjis_to_utf8(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }
    const int wide_size = ::MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data),
                                                static_cast<int>(size), nullptr, 0);
    if (wide_size <= 0) {
        return std::string(reinterpret_cast<const char*>(data), size);
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    ::MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), static_cast<int>(size),
                          wide.data(), wide_size);
    return wide_to_utf8(wide);
}

PmxModel PmxModel::parse(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "PMX ", 4) != 0) {
        throw FormatError("invalid PMX magic header");
    }

    PmxModel model;
    std::memcpy(&model.version, bytes.data() + 4, sizeof(float));
    const std::size_t globals_count = bytes[8];
    if (globals_count < 8 || bytes.size() < 9 + globals_count) {
        throw FormatError("incomplete PMX globals header");
    }
    const std::uint8_t* globals = bytes.data() + 9;
    model.header.encoding = globals[0] == 0 ? PmxEncoding::Utf16Le : PmxEncoding::Utf8;
    model.header.additional_vec4_count = globals[1];
    model.header.vertex_index_size = globals[2];
    model.header.texture_index_size = globals[3];
    model.header.material_index_size = globals[4];
    model.header.bone_index_size = globals[5];
    model.header.morph_index_size = globals[6];
    model.header.rigid_body_index_size = globals[7];

    Reader r(bytes.data() + 9 + globals_count, bytes.size() - 9 - globals_count, model.header);

    model.info.name_local = r.read_string();
    model.info.name_universal = r.read_string();
    model.info.comment_local = r.read_string();
    model.info.comment_universal = r.read_string();

    const std::int32_t vertex_count = r.read_i32();
    r.check_count(vertex_count, 38);
    model.vertices.reserve(static_cast<std::size_t>(vertex_count));
    for (std::int32_t i = 0; i < vertex_count; ++i) {
        PmxVertex v;
        v.position = r.read_vec3();
        v.normal = r.read_vec3();
        v.uv = r.read_vec2();
        v.extra_vec4.reserve(model.header.additional_vec4_count);
        for (std::uint8_t e = 0; e < model.header.additional_vec4_count; ++e) {
            v.extra_vec4.push_back(r.read_vec4());
        }
        v.weight = read_weight(r);
        v.edge_scale = r.read_f32();
        model.vertices.push_back(std::move(v));
    }

    const std::int32_t index_count = r.read_i32();
    r.check_count(index_count, 1);
    model.indices.reserve(static_cast<std::size_t>(index_count));
    for (std::int32_t i = 0; i < index_count; ++i) {
        model.indices.push_back(r.read_vertex_index());
    }

    const std::int32_t texture_count = r.read_i32();
    r.check_count(texture_count, 4);
    model.textures.reserve(static_cast<std::size_t>(texture_count));
    for (std::int32_t i = 0; i < texture_count; ++i) {
        model.textures.push_back(r.read_string());
    }

    const std::int32_t material_count = r.read_i32();
    r.check_count(material_count, 32);
    model.materials.reserve(static_cast<std::size_t>(material_count));
    for (std::int32_t i = 0; i < material_count; ++i) {
        PmxMaterial m;
        m.name_local = r.read_string();
        m.name_universal = r.read_string();
        m.diffuse = r.read_vec4();
        m.specular = r.read_vec3();
        m.shininess = r.read_f32();
        m.ambient = r.read_vec3();
        m.flags = r.read_u8();
        m.edge_color = r.read_vec4();
        m.edge_size = r.read_f32();
        m.texture_index = r.read_texture_index();
        m.sphere_texture_index = r.read_texture_index();
        m.sphere_mode = r.read_u8();
        m.toon_flag = r.read_u8();
        m.toon_index = m.toon_flag == 0 ? r.read_texture_index() : static_cast<std::int32_t>(r.read_u8());
        m.comment = r.read_string();
        m.surface_count = r.read_i32();
        model.materials.push_back(std::move(m));
    }

    const std::int32_t bone_count = r.read_i32();
    r.check_count(bone_count, 32);
    model.bones.reserve(static_cast<std::size_t>(bone_count));
    for (std::int32_t i = 0; i < bone_count; ++i) {
        PmxBone b;
        b.name_local = r.read_string();
        b.name_universal = r.read_string();
        b.position = r.read_vec3();
        b.parent_index = r.read_bone_index();
        b.deform_layer = r.read_i32();
        b.flags = r.read_u16();

        if (b.flags & 0x0001) {
            b.has_tail_bone = true;
            b.tail_bone = r.read_bone_index();
        } else {
            b.tail_offset = r.read_vec3();
        }

        if (b.flags & (0x0100 | 0x0200)) {
            b.has_append_parent = true;
            b.append_parent = r.read_bone_index();
            b.append_ratio = r.read_f32();
        }

        if (b.flags & 0x0400) {
            b.has_fixed_axis = true;
            b.fixed_axis = r.read_vec3();
        }

        if (b.flags & 0x0800) {
            b.has_local_axis = true;
            b.local_axis_x = r.read_vec3();
            b.local_axis_z = r.read_vec3();
        }

        if (b.flags & 0x2000) {
            b.has_external_parent_key = true;
            b.external_parent_key = r.read_i32();
        }

        if (b.flags & 0x0020) {
            b.has_ik = true;
            b.ik.target_bone = r.read_bone_index();
            b.ik.loop_count = r.read_i32();
            b.ik.angle_limit = r.read_f32();
            const std::int32_t link_count = r.read_i32();
            r.check_count(link_count, 5);
            b.ik.links.reserve(static_cast<std::size_t>(link_count));
            for (std::int32_t l = 0; l < link_count; ++l) {
                PmxIkLink link;
                link.bone_index = r.read_bone_index();
                link.has_limits = r.read_u8() != 0;
                if (link.has_limits) {
                    link.limit_min = r.read_vec3();
                    link.limit_max = r.read_vec3();
                }
                b.ik.links.push_back(link);
            }
        }

        model.bones.push_back(std::move(b));
    }

    const std::int32_t morph_count = r.read_i32();
    r.check_count(morph_count, 8);
    model.morphs.reserve(static_cast<std::size_t>(morph_count));
    for (std::int32_t i = 0; i < morph_count; ++i) {
        PmxMorph morph;
        morph.name_local = r.read_string();
        morph.name_universal = r.read_string();
        morph.category = r.read_u8();
        morph.type = static_cast<PmxMorphType>(r.read_u8());
        const std::int32_t count = r.read_i32();
        r.check_count(count, 5);
        const auto n = static_cast<std::size_t>(count);

        switch (morph.type) {
        case PmxMorphType::Group:
            morph.group_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                morph.group_items.push_back({r.read_morph_index(), r.read_f32()});
            }
            break;
        case PmxMorphType::Vertex:
            morph.vertex_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                morph.vertex_items.push_back({static_cast<std::int32_t>(r.read_vertex_index()), r.read_vec3()});
            }
            break;
        case PmxMorphType::Bone:
            morph.bone_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                morph.bone_items.push_back({r.read_bone_index(), r.read_vec3(), r.read_vec4()});
            }
            break;
        case PmxMorphType::Uv:
        case PmxMorphType::ExtraUv1:
        case PmxMorphType::ExtraUv2:
        case PmxMorphType::ExtraUv3:
        case PmxMorphType::ExtraUv4:
            morph.uv_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                morph.uv_items.push_back({static_cast<std::int32_t>(r.read_vertex_index()), r.read_vec4()});
            }
            morph.extra_uv_index = static_cast<std::uint8_t>(static_cast<std::uint8_t>(morph.type) - 4);
            break;
        case PmxMorphType::Material:
            morph.material_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                PmxMaterialMorphItem item;
                item.material_index = r.read_material_index();
                item.calc_mode = r.read_u8();
                item.diffuse = r.read_vec4();
                item.specular = r.read_vec3();
                item.shininess = r.read_f32();
                item.ambient = r.read_vec3();
                item.edge_color = r.read_vec4();
                item.edge_size = r.read_f32();
                item.texture_tint = r.read_vec4();
                item.sphere_tint = r.read_vec4();
                item.toon_tint = r.read_vec4();
                morph.material_items.push_back(item);
            }
            break;
        case PmxMorphType::Flip:
            morph.group_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                morph.group_items.push_back({r.read_morph_index(), r.read_f32()});
            }
            break;
        case PmxMorphType::Impulse:
            morph.impulse_items.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                PmxImpulseMorphItem item;
                item.rigid_body_index = r.read_rigid_body_index();
                item.local = r.read_u8();
                item.velocity = r.read_vec3();
                item.torque = r.read_vec3();
                morph.impulse_items.push_back(item);
            }
            break;
        }
        model.morphs.push_back(std::move(morph));
    }

    const std::int32_t frame_count = r.read_i32();
    r.check_count(frame_count, 8);
    model.display_frames.reserve(static_cast<std::size_t>(frame_count));
    for (std::int32_t i = 0; i < frame_count; ++i) {
        PmxDisplayFrame frame;
        frame.name_local = r.read_string();
        frame.name_universal = r.read_string();
        frame.is_special = r.read_u8() != 0;
        const std::int32_t count = r.read_i32();
        r.check_count(count, 2);
        frame.items.reserve(static_cast<std::size_t>(count));
        for (std::int32_t k = 0; k < count; ++k) {
            const bool is_morph = r.read_u8() != 0;
            const std::int32_t index = is_morph ? r.read_morph_index() : r.read_bone_index();
            frame.items.push_back({is_morph, index});
        }
        model.display_frames.push_back(std::move(frame));
    }

    const std::int32_t body_count = r.read_i32();
    r.check_count(body_count, 64);
    model.rigid_bodies.reserve(static_cast<std::size_t>(body_count));
    for (std::int32_t i = 0; i < body_count; ++i) {
        PmxRigidBody body;
        body.name_local = r.read_string();
        body.name_universal = r.read_string();
        body.bone_index = r.read_bone_index();
        body.collision_group = r.read_u8();
        body.collision_mask = r.read_u16();
        body.shape = r.read_u8();
        body.size = r.read_vec3();
        body.position = r.read_vec3();
        body.rotation = r.read_vec3();
        body.mass = r.read_f32();
        body.move_damping = r.read_f32();
        body.rot_damping = r.read_f32();
        body.restitution = r.read_f32();
        body.friction = r.read_f32();
        body.mode = r.read_u8();
        model.rigid_bodies.push_back(std::move(body));
    }

    const std::int32_t joint_count = r.read_i32();
    r.check_count(joint_count, 64);
    model.joints.reserve(static_cast<std::size_t>(joint_count));
    for (std::int32_t i = 0; i < joint_count; ++i) {
        PmxJoint joint;
        joint.name_local = r.read_string();
        joint.name_universal = r.read_string();
        joint.joint_type = r.read_u8();
        joint.body_a = r.read_rigid_body_index();
        joint.body_b = r.read_rigid_body_index();
        joint.position = r.read_vec3();
        joint.rotation = r.read_vec3();
        joint.pos_min = r.read_vec3();
        joint.pos_max = r.read_vec3();
        joint.rot_min = r.read_vec3();
        joint.rot_max = r.read_vec3();
        joint.spring_pos = r.read_vec3();
        joint.spring_rot = r.read_vec3();
        model.joints.push_back(std::move(joint));
    }

    return model;
}

} // namespace hikari::formats
