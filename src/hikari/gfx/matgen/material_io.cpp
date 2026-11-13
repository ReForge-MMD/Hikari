#include "hikari/gfx/matgen/material_io.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hikari::gfx::matgen {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::string_view> find_json_string(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find('"', pos + 1);
    if (pos == std::string_view::npos) return std::nullopt;
    size_t end = json.find('"', pos + 1);
    if (end == std::string_view::npos) return std::nullopt;
    return json.substr(pos + 1, end - pos - 1);
}

std::optional<float> find_json_float(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.size() && (json[end] == '-' || json[end] == '+' || json[end] == '.' ||
                                 (json[end] >= '0' && json[end] <= '9') || json[end] == 'e' || json[end] == 'E')) {
        end++;
    }
    if (end == pos) return std::nullopt;
    float val = 0.0f;
    auto [ptr, ec] = std::from_chars(json.data() + pos, json.data() + end, val);
    if (ec == std::errc{}) return val;
    return std::nullopt;
}

std::optional<bool> find_json_bool(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    if (json.substr(pos).find("true") != std::string_view::npos &&
        json.substr(pos).find("true") < json.substr(pos).find(',')) {
        return true;
    }
    if (json.substr(pos).find("false") != std::string_view::npos &&
        json.substr(pos).find("false") < json.substr(pos).find(',')) {
        return false;
    }
    return std::nullopt;
}

std::optional<std::array<float, 3>> find_json_vec3(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find('[', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    size_t end = json.find(']', pos + 1);
    if (end == std::string_view::npos) return std::nullopt;
    std::string_view arr = json.substr(pos + 1, end - pos - 1);

    std::array<float, 3> result{0.0f, 0.0f, 0.0f};
    int idx = 0;
    size_t curr = 0;
    while (curr < arr.size() && idx < 3) {
        while (curr < arr.size() && (arr[curr] == ' ' || arr[curr] == ',' || arr[curr] == '\t')) curr++;
        if (curr >= arr.size()) break;
        size_t next_comma = arr.find(',', curr);
        if (next_comma == std::string_view::npos) next_comma = arr.size();
        std::string_view item = trim(arr.substr(curr, next_comma - curr));
        float val = 0.0f;
        auto [ptr, ec] = std::from_chars(item.data(), item.data() + item.size(), val);
        if (ec == std::errc{}) {
            result[idx++] = val;
        }
        curr = next_comma + 1;
    }
    return result;
}

} // namespace

std::string MaterialIO::serialize_hkmat(const MaterialFileData& data) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);

    ss << "{\n";
    ss << "  \"format\": \"hikari_material\",\n";
    ss << "  \"version\": " << data.version << ",\n";
    ss << "  \"name\": \"" << data.name << "\",\n";
    ss << "  \"response\": {\n";
    ss << "    \"shading_model\": \"" << shading_model_name(data.response.shading_model) << "\",\n";
    if (data.response.albedo.has_value()) {
        ss << "    \"albedo\": [" << (*data.response.albedo)[0] << ", " << (*data.response.albedo)[1] << ", " << (*data.response.albedo)[2] << "],\n";
    }
    ss << "    \"roughness\": " << data.response.roughness << ",\n";
    ss << "    \"specular\": [" << data.response.specular[0] << ", " << data.response.specular[1] << ", " << data.response.specular[2] << "],\n";
    ss << "    \"metallic\": " << data.response.metallic << ",\n";
    ss << "    \"emissive\": [" << data.response.emissive[0] << ", " << data.response.emissive[1] << ", " << data.response.emissive[2] << "],\n";
    ss << "    \"custom_a\": " << data.response.custom_a << ",\n";
    ss << "    \"custom_b\": " << data.response.custom_b << ",\n";
    ss << "    \"hashed_alpha\": " << (data.response.hashed_alpha ? "true" : "false") << ",\n";
    ss << "    \"hex_tiling_scale\": " << data.response.hex_tiling_scale;
    if (data.response.custom_b_color[0] != 0.0f || data.response.custom_b_color[1] != 0.0f || data.response.custom_b_color[2] != 0.0f) {
        ss << ",\n    \"custom_b_color\": [" << data.response.custom_b_color[0] << ", " << data.response.custom_b_color[1] << ", " << data.response.custom_b_color[2] << "]";
    }
    if (data.response.procedural_hair) {
        ss << ",\n    \"procedural_hair\": true";
        ss << ",\n    \"procedural_hair_scale\": " << data.response.procedural_hair_scale;
        ss << ",\n    \"procedural_hair_intensity\": " << data.response.procedural_hair_intensity;
    }
    if (data.response.glitter) {
        ss << ",\n    \"glitter\": true";
        ss << ",\n    \"glitter_density\": " << data.response.glitter_density;
        ss << ",\n    \"glitter_roughness\": " << data.response.glitter_roughness;
        ss << ",\n    \"glitter_intensity\": " << data.response.glitter_intensity;
    }
    if (data.response.eye_parallax) {
        ss << ",\n    \"eye_parallax\": true";
        ss << ",\n    \"eye_iris_depth\": " << data.response.eye_iris_depth;
        ss << ",\n    \"eye_cornea_ior\": " << data.response.eye_cornea_ior;
    }
    if (!data.response.albedo_map.empty()) {
        ss << ",\n    \"albedo_map\": \"" << data.response.albedo_map << "\"";
    }
    if (!data.response.normal_map.empty()) {
        ss << ",\n    \"normal_map\": \"" << data.response.normal_map << "\"";
    }
    if (!data.response.roughness_map.empty()) {
        ss << ",\n    \"roughness_map\": \"" << data.response.roughness_map << "\"";
    }
    if (!data.response.metallic_map.empty()) {
        ss << ",\n    \"metallic_map\": \"" << data.response.metallic_map << "\"";
    }
    if (!data.response.emissive_map.empty()) {
        ss << ",\n    \"emissive_map\": \"" << data.response.emissive_map << "\"";
    }
    if (!data.response.occlusion_map.empty()) {
        ss << ",\n    \"occlusion_map\": \"" << data.response.occlusion_map << "\"";
    }
    ss << "\n  }\n";

    if (data.graph) {
        ss << ",\n  \"graph\": {\n";
        ss << "    \"nodes\": [\n";
        const auto& nodes = data.graph->nodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            ss << "      {\"id\": " << n.id << ", \"type\": \"" << node_type_name(n.type)
               << "\", \"pos\": [" << n.pos_x << ", " << n.pos_y << "]}";
            if (i + 1 < nodes.size()) ss << ",";
            ss << "\n";
        }
        ss << "    ],\n";
        ss << "    \"links\": [\n";
        const auto& links = data.graph->links();
        for (size_t i = 0; i < links.size(); ++i) {
            const auto& l = links[i];
            ss << "      {\"id\": " << l.id << ", \"from_node\": " << l.from_node_id
               << ", \"from_sock\": " << l.from_socket_id << ", \"to_node\": " << l.to_node_id
               << ", \"to_sock\": " << l.to_socket_id << "}";
            if (i + 1 < links.size()) ss << ",";
            ss << "\n";
        }
        ss << "    ]\n";
        ss << "  }\n";
    }

    ss << "}\n";
    return ss.str();
}

std::string MaterialIO::serialize_response(std::string_view name, const MaterialResponse& response) {
    MaterialFileData data;
    data.name = std::string(name);
    data.version = 1;
    data.response = response;
    return serialize_hkmat(data);
}

std::optional<MaterialFileData> MaterialIO::deserialize_hkmat(std::string_view json_text) {
    if (json_text.find("\"format\": \"hikari_material\"") == std::string_view::npos &&
        json_text.find("\"format\":\"hikari_material\"") == std::string_view::npos) {
        return std::nullopt;
    }

    MaterialFileData data;
    auto name_opt = find_json_string(json_text, "name");
    data.name = name_opt ? std::string(*name_opt) : "unnamed";

    auto model_str = find_json_string(json_text, "shading_model");
    if (model_str) {
        data.response.shading_model = shading_model_from_name(*model_str);
        if (data.response.shading_model < 0) {
            data.response.shading_model = 0;
        }
    }

    if (auto alb = find_json_vec3(json_text, "albedo")) data.response.albedo = *alb;
    if (auto r = find_json_float(json_text, "roughness")) data.response.roughness = *r;
    if (auto m = find_json_float(json_text, "metallic")) data.response.metallic = *m;
    if (auto ca = find_json_float(json_text, "custom_a")) data.response.custom_a = *ca;
    if (auto cb = find_json_float(json_text, "custom_b")) data.response.custom_b = *cb;
    if (auto cbc = find_json_vec3(json_text, "custom_b_color")) data.response.custom_b_color = *cbc;
    if (auto ha = find_json_bool(json_text, "hashed_alpha")) data.response.hashed_alpha = *ha;
    if (auto ht = find_json_float(json_text, "hex_tiling_scale")) data.response.hex_tiling_scale = *ht;

    if (auto ph = find_json_bool(json_text, "procedural_hair")) data.response.procedural_hair = *ph;
    if (auto phs = find_json_float(json_text, "procedural_hair_scale")) data.response.procedural_hair_scale = *phs;
    if (auto phi = find_json_float(json_text, "procedural_hair_intensity")) data.response.procedural_hair_intensity = *phi;

    if (auto gl = find_json_bool(json_text, "glitter")) data.response.glitter = *gl;
    if (auto gld = find_json_float(json_text, "glitter_density")) data.response.glitter_density = *gld;
    if (auto glr = find_json_float(json_text, "glitter_roughness")) data.response.glitter_roughness = *glr;
    if (auto gli = find_json_float(json_text, "glitter_intensity")) data.response.glitter_intensity = *gli;

    if (auto ep = find_json_bool(json_text, "eye_parallax")) data.response.eye_parallax = *ep;
    if (auto eid = find_json_float(json_text, "eye_iris_depth")) data.response.eye_iris_depth = *eid;
    if (auto eir = find_json_float(json_text, "eye_cornea_ior")) data.response.eye_cornea_ior = *eir;

    if (auto spec = find_json_vec3(json_text, "specular")) data.response.specular = *spec;
    if (auto em = find_json_vec3(json_text, "emissive")) data.response.emissive = *em;
    if (auto alb = find_json_string(json_text, "albedo_map")) data.response.albedo_map = std::string(*alb);
    if (auto nm = find_json_string(json_text, "normal_map")) data.response.normal_map = std::string(*nm);
    if (auto rm = find_json_string(json_text, "roughness_map")) data.response.roughness_map = std::string(*rm);
    if (auto mm = find_json_string(json_text, "metallic_map")) data.response.metallic_map = std::string(*mm);
    if (auto emm = find_json_string(json_text, "emissive_map")) data.response.emissive_map = std::string(*emm);
    if (auto om = find_json_string(json_text, "occlusion_map")) data.response.occlusion_map = std::string(*om);

    return data;
}

bool MaterialIO::save_file(std::string_view filepath, const MaterialFileData& data) {
    std::string text = serialize_hkmat(data);
    std::ofstream file(std::string(filepath), std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

std::optional<MaterialFileData> MaterialIO::load_file(std::string_view filepath) {
    std::ifstream file(std::string(filepath), std::ios::in | std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << file.rdbuf();
    return deserialize_hkmat(ss.str());
}

} // namespace hikari::gfx::matgen
