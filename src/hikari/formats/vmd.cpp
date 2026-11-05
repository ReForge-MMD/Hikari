#include "hikari/formats/vmd.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace hikari::formats {
namespace {

std::string decode_fixed_name(const std::uint8_t* data, std::size_t size) {
    std::size_t len = 0;
    while (len < size && data[len] != 0) {
        ++len;
    }
    return sjis_to_utf8(data, len);
}

class VmdReader {
public:
    VmdReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::size_t remaining() const { return bytes_.size() - position_; }

    void check_count(std::uint32_t count, std::size_t min_bytes) const {
        const auto n = static_cast<std::size_t>(count);
        if (min_bytes != 0 && n > remaining() / min_bytes) {
            throw FormatError("VMD record count exceeds remaining data");
        }
    }

    void read_exact(std::uint8_t* out, std::size_t size) {
        require(size);
        std::memcpy(out, bytes_.data() + position_, size);
        position_ += size;
    }

    std::uint8_t read_u8() {
        require(1);
        return bytes_[position_++];
    }

    std::uint32_t read_u32() {
        std::uint8_t b[4];
        read_exact(b, 4);
        return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
               (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    }

    float read_f32() {
        const std::uint32_t bits = read_u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::array<float, 3> read_vec3() { return {read_f32(), read_f32(), read_f32()}; }
    std::array<float, 4> read_vec4() { return {read_f32(), read_f32(), read_f32(), read_f32()}; }

private:
    void require(std::size_t n) const {
        if (n > remaining()) {
            throw FormatError("unexpected end of VMD data");
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool valid_utf8(const std::uint8_t* data, std::size_t size) {
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t c = data[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t continuation = 0;
        if ((c & 0xE0) == 0xC0) {
            continuation = 1;
            if (c < 0xC2) return false;
        } else if ((c & 0xF0) == 0xE0) {
            continuation = 2;
        } else if ((c & 0xF8) == 0xF0) {
            continuation = 3;
            if (c > 0xF4) return false;
        } else {
            return false;
        }
        if (i + continuation >= size) {
            return false;
        }
        for (std::size_t k = 1; k <= continuation; ++k) {
            if ((data[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }
        i += continuation + 1;
    }
    return true;
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool parse_components(const std::string& line, std::vector<float>& out) {
    std::string trimmed = trim(line);
    while (!trimmed.empty() && trimmed.back() == ';') {
        trimmed.pop_back();
        trimmed = trim(trimmed);
    }
    if (trimmed.empty() || trimmed.rfind("//", 0) == 0) {
        return false;
    }
    out.clear();
    std::stringstream stream(trimmed);
    std::string part;
    while (std::getline(stream, part, ',')) {
        try {
            out.push_back(std::stof(trim(part)));
        } catch (...) {
            return false;
        }
    }
    return !out.empty();
}

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void append_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}

std::string truncate_sjis(const std::string& text, std::size_t max_bytes) {
    const std::string encoded = utf8_to_sjis(text);
    if (encoded.size() <= max_bytes) {
        return encoded;
    }
    std::size_t length = max_bytes;
    const unsigned char previous = static_cast<unsigned char>(encoded[length - 1]);
    if ((previous >= 0x81 && previous <= 0x9F) || (previous >= 0xE0 && previous <= 0xFC)) {
        --length;
    }
    return encoded.substr(0, length);
}

void append_name(std::vector<std::uint8_t>& out, const std::string& name,
                 std::size_t field_size) {
    const std::string encoded = truncate_sjis(name, field_size);
    out.insert(out.end(), encoded.begin(), encoded.end());
    out.insert(out.end(), field_size - encoded.size(), 0);
}

std::string parse_block_name(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        const std::string trimmed = trim(line);
        if (trimmed.rfind("//", 0) == 0) {
            return trim(trimmed.substr(2));
        }
        std::vector<float> values;
        if (!parse_components(trimmed, values)) {
            std::string name = trimmed;
            while (!name.empty() && name.back() == ';') {
                name.pop_back();
            }
            return trim(name);
        }
    }
    return {};
}

} // namespace

VmdMotion VmdMotion::parse(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 50) {
        throw FormatError("VMD data too short");
    }

    VmdReader r(bytes);
    std::uint8_t magic[30];
    r.read_exact(magic, sizeof(magic));

    VmdMotion motion;
    if (std::memcmp(magic, "Vocaloid Motion Data 0002", 25) == 0) {
        motion.version = 2;
    } else if (std::memcmp(magic, "Vocaloid Motion Data file", 25) == 0) {
        motion.version = 1;
    } else {
        throw FormatError("invalid VMD magic header");
    }

    const std::size_t name_len = motion.version == 2 ? 20 : 10;
    std::vector<std::uint8_t> name_bytes(name_len);
    r.read_exact(name_bytes.data(), name_len);
    motion.model_name = decode_fixed_name(name_bytes.data(), name_bytes.size());

    const std::uint32_t bone_count = r.read_u32();
    r.check_count(bone_count, 111);
    motion.bone_frames.reserve(bone_count);
    for (std::uint32_t i = 0; i < bone_count; ++i) {
        std::uint8_t name_buf[15];
        r.read_exact(name_buf, sizeof(name_buf));
        VmdBoneFrame frame;
        frame.name = decode_fixed_name(name_buf, sizeof(name_buf));
        frame.frame = r.read_u32();
        frame.position = r.read_vec3();
        frame.rotation = r.read_vec4();
        r.read_exact(frame.interpolation.data(), frame.interpolation.size());
        motion.bone_frames.push_back(std::move(frame));
    }

    const std::uint32_t morph_count = r.read_u32();
    r.check_count(morph_count, 23);
    motion.morph_frames.reserve(morph_count);
    for (std::uint32_t i = 0; i < morph_count; ++i) {
        std::uint8_t name_buf[15];
        r.read_exact(name_buf, sizeof(name_buf));
        VmdMorphFrame frame;
        frame.name = decode_fixed_name(name_buf, sizeof(name_buf));
        frame.frame = r.read_u32();
        frame.weight = r.read_f32();
        motion.morph_frames.push_back(std::move(frame));
    }

    const std::uint32_t camera_count = r.read_u32();
    r.check_count(camera_count, 61);
    motion.camera_frames.reserve(camera_count);
    for (std::uint32_t i = 0; i < camera_count; ++i) {
        VmdCameraFrame frame;
        frame.frame = r.read_u32();
        frame.distance = r.read_f32();
        frame.position = r.read_vec3();
        frame.rotation = r.read_vec3();
        r.read_exact(frame.interpolation.data(), frame.interpolation.size());
        frame.fov = r.read_u32();
        frame.perspective = r.read_u8() == 0;
        motion.camera_frames.push_back(frame);
    }

    const std::uint32_t light_count = r.read_u32();
    r.check_count(light_count, 28);
    motion.light_frames.reserve(light_count);
    for (std::uint32_t i = 0; i < light_count; ++i) {
        VmdLightFrame frame;
        frame.frame = r.read_u32();
        frame.color = r.read_vec3();
        frame.direction = r.read_vec3();
        motion.light_frames.push_back(frame);
    }

    const std::uint32_t shadow_count = r.read_u32();
    r.check_count(shadow_count, 9);
    motion.shadow_frames.reserve(shadow_count);
    for (std::uint32_t i = 0; i < shadow_count; ++i) {
        VmdShadowFrame frame;
        frame.frame = r.read_u32();
        frame.mode = r.read_u8();
        frame.distance = r.read_f32();
        motion.shadow_frames.push_back(frame);
    }

    const std::uint32_t ik_count = r.read_u32();
    r.check_count(ik_count, 9);
    motion.ik_frames.reserve(ik_count);
    for (std::uint32_t i = 0; i < ik_count; ++i) {
        VmdIkFrame frame;
        frame.frame = r.read_u32();
        frame.show = r.read_u8() != 0;
        const std::uint32_t switch_count = r.read_u32();
        r.check_count(switch_count, 21);
        frame.ik_switches.reserve(switch_count);
        for (std::uint32_t k = 0; k < switch_count; ++k) {
            std::uint8_t name_buf[20];
            r.read_exact(name_buf, sizeof(name_buf));
            VmdIkSwitch sw;
            sw.name = decode_fixed_name(name_buf, sizeof(name_buf));
            sw.enabled = r.read_u8() != 0;
            frame.ik_switches.push_back(std::move(sw));
        }
        motion.ik_frames.push_back(std::move(frame));
    }

    return motion;
}

std::vector<std::uint8_t> VmdMotion::to_bytes(const std::string& model_name_out) const {
    std::vector<std::uint8_t> out;
    const char magic[] = "Vocaloid Motion Data 0002";
    const std::size_t magic_size = sizeof(magic) - 1;
    out.insert(out.end(), magic, magic + magic_size);
    out.insert(out.end(), 30 - magic_size, 0);
    append_name(out, model_name_out, 20);

    append_u32(out, static_cast<std::uint32_t>(bone_frames.size()));
    for (const auto& frame : bone_frames) {
        append_name(out, frame.name, 15);
        append_u32(out, frame.frame);
        for (float value : frame.position) {
            append_f32(out, value);
        }
        for (float value : frame.rotation) {
            append_f32(out, value);
        }
        out.insert(out.end(), frame.interpolation.begin(), frame.interpolation.end());
    }

    append_u32(out, static_cast<std::uint32_t>(morph_frames.size()));
    for (const auto& frame : morph_frames) {
        append_name(out, frame.name, 15);
        append_u32(out, frame.frame);
        append_f32(out, frame.weight);
    }

    append_u32(out, static_cast<std::uint32_t>(camera_frames.size()));
    for (const auto& frame : camera_frames) {
        append_u32(out, frame.frame);
        append_f32(out, frame.distance);
        for (float value : frame.position) {
            append_f32(out, value);
        }
        for (float value : frame.rotation) {
            append_f32(out, value);
        }
        out.insert(out.end(), frame.interpolation.begin(), frame.interpolation.end());
        append_u32(out, frame.fov);
        append_u8(out, frame.perspective ? 0 : 1);
    }

    append_u32(out, static_cast<std::uint32_t>(light_frames.size()));
    for (const auto& frame : light_frames) {
        append_u32(out, frame.frame);
        for (float value : frame.color) {
            append_f32(out, value);
        }
        for (float value : frame.direction) {
            append_f32(out, value);
        }
    }

    append_u32(out, static_cast<std::uint32_t>(shadow_frames.size()));
    for (const auto& frame : shadow_frames) {
        append_u32(out, frame.frame);
        append_u8(out, frame.mode);
        append_f32(out, frame.distance);
    }

    append_u32(out, static_cast<std::uint32_t>(ik_frames.size()));
    for (const auto& frame : ik_frames) {
        append_u32(out, frame.frame);
        append_u8(out, frame.show ? 1 : 0);
        append_u32(out, static_cast<std::uint32_t>(frame.ik_switches.size()));
        for (const auto& sw : frame.ik_switches) {
            append_name(out, sw.name, 20);
            append_u8(out, sw.enabled ? 1 : 0);
        }
    }

    return out;
}

VpdPose VpdPose::from_bytes(const std::vector<std::uint8_t>& bytes) {
    if (valid_utf8(bytes.data(), bytes.size())) {
        return parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    return parse(sjis_to_utf8(bytes.data(), bytes.size()));
}

VpdPose VpdPose::parse(const std::string& content) {
    std::istringstream stream(content);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }

    if (lines.empty() || lines[0].rfind("Vocaloid Pose Data file", 0) != 0) {
        throw FormatError("invalid VPD header");
    }

    VpdPose pose;
    std::size_t index = 1;
    if (index < lines.size()) {
        std::string model_line = lines[index++];
        while (!model_line.empty() && model_line.back() == ';') {
            model_line.pop_back();
        }
        pose.model_name = trim(model_line);
    }
    if (index < lines.size()) {
        ++index; // Bone count line, unused.
    }

    std::string block_name;
    std::vector<std::string> block_lines;
    for (; index < lines.size(); ++index) {
        const std::string& current = lines[index];
        const auto brace = current.find('{');
        if (brace != std::string::npos) {
            block_name = trim(current.substr(0, brace));
            block_lines.clear();
            const std::string rest = trim(current.substr(brace + 1));
            if (!rest.empty()) {
                block_lines.push_back(rest);
            }
        } else if (current.rfind('}', 0) == 0) {
            if (block_name.rfind("Bone", 0) == 0) {
                try {
                    VpdBonePose bone;
                    bone.index = static_cast<std::uint32_t>(std::stoul(block_name.substr(4)));
                    bone.name = parse_block_name(block_lines);
                    for (const auto& bl : block_lines) {
                        std::vector<float> values;
                        if (!parse_components(bl, values)) {
                            continue;
                        }
                        if (values.size() == 3) {
                            bone.translation = {values[0], values[1], values[2]};
                        } else if (values.size() == 4) {
                            bone.rotation = {values[0], values[1], values[2], values[3]};
                        }
                    }
                    pose.bones.push_back(std::move(bone));
                } catch (...) {
                    // Ignore malformed blocks the same way the original parser did.
                }
            } else if (block_name.rfind("Morph", 0) == 0) {
                try {
                    VpdMorphPose morph;
                    morph.index = static_cast<std::uint32_t>(std::stoul(block_name.substr(5)));
                    morph.name = parse_block_name(block_lines);
                    for (const auto& bl : block_lines) {
                        std::vector<float> values;
                        if (parse_components(bl, values) && values.size() == 1) {
                            morph.weight = values[0];
                            break;
                        }
                    }
                    pose.morphs.push_back(std::move(morph));
                } catch (...) {
                }
            }
            block_name.clear();
            block_lines.clear();
        } else if (!current.empty()) {
            block_lines.push_back(current);
        }
    }

    return pose;
}

std::string VpdPose::to_vpd_string() const {
    std::ostringstream out;
    out << "Vocaloid Pose Data file\n\n";
    std::string model = model_name;
    if (model.size() < 4 || model.compare(model.size() - 4, 4, ".osm") != 0) {
        model += ".osm";
    }
    out << model << ";\n";
    out << (bones.size() + morphs.size()) << ";\n\n";

    for (const auto& bone : bones) {
        out << "Bone" << bone.index << "{\n";
        out << "\t// " << bone.name << "\n";
        out << "\t" << bone.translation[0] << "," << bone.translation[1] << "," << bone.translation[2] << ";\n";
        out << "\t" << bone.rotation[0] << "," << bone.rotation[1] << "," << bone.rotation[2] << ","
            << bone.rotation[3] << ";\n";
        out << "}\n\n";
    }

    for (const auto& morph : morphs) {
        out << "Morph" << morph.index << "{\n";
        out << "\t// " << morph.name << "\n";
        out << "\t" << morph.weight << ";\n";
        out << "}\n\n";
    }

    return out.str();
}

} // namespace hikari::formats
