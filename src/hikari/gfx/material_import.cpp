#include "hikari/gfx/material_import.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>

namespace hikari::gfx {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// Ray-MMD material files carry their whole configuration in comments and in a
// flat constant block, so the converter only has to read that block. Removing
// comments first keeps a commented-out define from being imported.
std::string strip_comments(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    bool in_line_comment = false;
    bool in_block_comment = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char current = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        if (in_line_comment) {
            if (current == '\n') {
                in_line_comment = false;
                out.push_back(current);
            }
            continue;
        }
        if (in_block_comment) {
            if (current == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            } else if (current == '\n') {
                out.push_back(current);
            }
            continue;
        }
        if (current == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (current == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        out.push_back(current);
    }
    return out;
}

std::optional<float> parse_scalar(std::string_view text) {
    text = trim(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    float value = 0.0f;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Ray-MMD writes a colour either as a broadcast scalar or as `float3(r, g, b)`.
std::optional<std::array<float, 3>> parse_color(std::string_view text) {
    text = trim(text);
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos) {
        const std::optional<float> scalar = parse_scalar(text);
        if (!scalar) {
            return std::nullopt;
        }
        return std::array<float, 3>{*scalar, *scalar, *scalar};
    }
    const std::size_t close = text.rfind(')');
    if (close == std::string_view::npos || close < open) {
        return std::nullopt;
    }
    std::string_view list = text.substr(open + 1, close - open - 1);
    std::array<float, 3> color{0.0f, 0.0f, 0.0f};
    int count = 0;
    while (count < 3 && !list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string_view item = trim(list.substr(0, comma));
        const std::optional<float> value = parse_scalar(item);
        if (!value) {
            return std::nullopt;
        }
        color[static_cast<std::size_t>(count)] = *value;
        ++count;
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    if (count == 0) {
        return std::nullopt;
    }
    if (count == 1) {
        color = {color[0], color[0], color[0]};
    }
    return color;
}

struct ParsedParameter {
    std::string name;
    std::string value;
    bool shader_body = false;
};

std::vector<ParsedParameter> scan(std::string_view source) {
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        source.remove_prefix(3);
    }
    const std::string text = strip_comments(source);
    std::vector<ParsedParameter> parameters;
    std::string_view rest = text;
    while (!rest.empty()) {
        const std::size_t newline = rest.find('\n');
        std::string_view line = rest.substr(0, newline);
        if (newline == std::string_view::npos) {
            rest = {};
        } else {
            rest.remove_prefix(newline + 1);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.starts_with("#include")) {
            std::string_view value = trim(line.substr(std::string_view("#include").size()));
            if (value.size() >= 2 && value.front() == '"') {
                const std::size_t quote = value.find('"', 1);
                if (quote != std::string_view::npos) {
                    value = value.substr(1, quote - 1);
                }
            }
            parameters.push_back({"shader body", std::string(value), true});
            continue;
        }
        if (line.starts_with("#define")) {
            std::string_view body = trim(line.substr(std::string_view("#define").size()));
            const std::size_t split = body.find_first_of(" \t");
            if (split == std::string_view::npos) {
                continue;
            }
            const std::string_view name = body.substr(0, split);
            const std::string_view value = trim(body.substr(split));
            // A function-like macro carries no material parameter.
            if (name.empty() || name.find('(') != std::string_view::npos || value.empty()) {
                continue;
            }
            parameters.push_back({std::string(name), std::string(value), false});
            continue;
        }
        if (line.starts_with("const ")) {
            std::string_view body = trim(line.substr(std::string_view("const ").size()));
            const std::size_t type_end = body.find_first_of(" \t");
            if (type_end == std::string_view::npos) {
                continue;
            }
            body = trim(body.substr(type_end));
            const std::size_t assign = body.find('=');
            if (assign == std::string_view::npos) {
                continue;
            }
            const std::string_view name = trim(body.substr(0, assign));
            std::string_view value = trim(body.substr(assign + 1));
            if (!value.empty() && value.back() == ';') {
                value.remove_suffix(1);
            }
            if (name.empty()) {
                continue;
            }
            parameters.push_back({std::string(name), std::string(trim(value)), false});
        }
    }
    return parameters;
}

const ParsedParameter* find_parameter(const std::vector<ParsedParameter>& parameters,
                                      std::string_view name) {
    for (const ParsedParameter& parameter : parameters) {
        if (!parameter.shader_body && parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

bool is_map_parameter(std::string_view name) {
    return name.find("_MAP_") != std::string_view::npos || name.ends_with("MapLoopNum");
}

std::string fixed(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
    return buffer;
}

std::string fixed(const std::array<float, 3>& value) {
    return fixed(value[0]) + " " + fixed(value[1]) + " " + fixed(value[2]);
}

void append(ImportReport& report, std::string name, ImportStatus status, std::string detail) {
    switch (status) {
    case ImportStatus::Applied:
        ++report.applied;
        break;
    case ImportStatus::Approximate:
        ++report.approximate;
        break;
    case ImportStatus::Unsupported:
        ++report.unsupported;
        break;
    }
    report.parameters.push_back({std::move(name), status, std::move(detail)});
}

} // namespace

std::string_view import_status_name(ImportStatus status) {
    switch (status) {
    case ImportStatus::Applied:
        return "applied";
    case ImportStatus::Approximate:
        return "approximate";
    case ImportStatus::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

RayMmdImport import_ray_mmd_material(std::string_view source) {
    RayMmdImport result;
    const std::vector<ParsedParameter> parameters = scan(source);

    std::size_t material_parameters = 0;
    for (const ParsedParameter& parameter : parameters) {
        if (!parameter.shader_body) {
            ++material_parameters;
        }
    }
    if (material_parameters == 0) {
        result.report.error = "no Ray-MMD material parameters found";
        return result;
    }

    const ParsedParameter* enable = find_parameter(parameters, "EMISSIVE_ENABLE");
    const std::optional<float> enable_value =
        enable != nullptr ? parse_scalar(enable->value) : std::nullopt;
    const bool emission_on = enable_value.has_value() && *enable_value > 0.5f;
    const ParsedParameter* intensity = find_parameter(parameters, "emissiveIntensity");
    const std::optional<float> intensity_value =
        intensity != nullptr ? parse_scalar(intensity->value) : std::nullopt;

    MaterialResponse& response = result.response;
    for (const ParsedParameter& parameter : parameters) {
        if (parameter.shader_body) {
            append(result.report, parameter.name, ImportStatus::Unsupported,
                   parameter.value + " is read as data and never compiled");
            continue;
        }
        const std::string_view name = parameter.name;

        if (name == "CUSTOM_ENABLE") {
            const std::optional<float> value = parse_scalar(parameter.value);
            const int model = value.has_value() ? static_cast<int>(std::lround(*value)) : -1;
            const std::string_view label = shading_model_label(model);
            if (label.empty()) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "shading model id " + parameter.value + " has no native equivalent");
            } else {
                response.shading_model = model;
                append(result.report, parameter.name, ImportStatus::Applied,
                       "shading model " + std::string(label));
            }
            continue;
        }
        if (name == "smoothness") {
            const std::optional<float> value = parse_scalar(parameter.value);
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a number");
                continue;
            }
            response.roughness = std::clamp(1.0f - *value, 0.0f, 1.0f);
            append(result.report, parameter.name, ImportStatus::Applied,
                   "smoothness " + fixed(*value) + " -> roughness " + fixed(response.roughness));
            continue;
        }
        if (name == "metalness") {
            const std::optional<float> value = parse_scalar(parameter.value);
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a number");
                continue;
            }
            response.metallic = std::clamp(*value, 0.0f, 1.0f);
            append(result.report, parameter.name, ImportStatus::Applied,
                   "metalness " + fixed(response.metallic));
            continue;
        }
        if (name == "specular") {
            const std::optional<std::array<float, 3>> value = parse_color(parameter.value);
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a colour");
                continue;
            }
            for (std::size_t i = 0; i < response.specular.size(); ++i) {
                response.specular[i] = std::clamp((*value)[i], 0.0f, 1.0f);
            }
            append(result.report, parameter.name, ImportStatus::Applied,
                   "F0 " + fixed(response.specular));
            continue;
        }
        if (name == "emissive") {
            const std::optional<std::array<float, 3>> value = parse_color(parameter.value);
            if (!emission_on) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "EMISSIVE_ENABLE is 0, so the native response keeps emission off");
                continue;
            }
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a colour");
                continue;
            }
            const float gain = intensity_value.value_or(1.0f);
            for (std::size_t i = 0; i < response.emissive.size(); ++i) {
                response.emissive[i] = std::max((*value)[i] * gain, 0.0f);
            }
            append(result.report, parameter.name, ImportStatus::Applied,
                   "emission " + fixed(response.emissive));
            continue;
        }
        if (name == "emissiveIntensity") {
            const bool usable = emission_on && intensity_value.has_value();
            append(result.report, parameter.name,
                   usable ? ImportStatus::Applied : ImportStatus::Unsupported,
                   usable ? "folds into the emission colour"
                          : "EMISSIVE_ENABLE is 0, so the gain has nothing to scale");
            continue;
        }
        if (name == "customA") {
            const std::optional<float> value = parse_scalar(parameter.value);
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a number");
                continue;
            }
            response.custom_a = *value;
            append(result.report, parameter.name, ImportStatus::Applied,
                   "custom A " + fixed(*value));
            continue;
        }
        if (name == "customB") {
            const std::optional<std::array<float, 3>> value = parse_color(parameter.value);
            if (!value) {
                append(result.report, parameter.name, ImportStatus::Unsupported,
                       "the value is not a colour");
                continue;
            }
            // Ray-MMD carries a colour here, while the native surface models read
            // one scalar, so the chroma cannot survive the conversion.
            const std::array<float, 3>& color = *value;
            response.custom_b =
                0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
            append(result.report, parameter.name, ImportStatus::Approximate,
                   "colour " + fixed(color) + " reduced to luminance " +
                       fixed(response.custom_b));
            continue;
        }
        if (name == "albedo" || name == "albedoSub") {
            append(result.report, parameter.name, ImportStatus::Unsupported,
                   "a per-subset override keeps the authored PMX colour");
            continue;
        }
        if (name == "alpha") {
            append(result.report, parameter.name, ImportStatus::Unsupported,
                   "a per-subset override keeps the authored PMX alpha");
            continue;
        }
        if (is_map_parameter(name)) {
            append(result.report, parameter.name, ImportStatus::Unsupported,
                   "texture slots and UV loops are not imported");
            continue;
        }
        append(result.report, parameter.name, ImportStatus::Unsupported,
               "no native equivalent");
    }

    result.report.ok = true;
    return result;
}

} // namespace hikari::gfx
