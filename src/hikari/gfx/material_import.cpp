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
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                           text.front() == '\r')) {
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
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                      value, std::chars_format::general);
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
  std::string_view suffix = trim(text.substr(close + 1));
  float scale = 1.0f;
  if (suffix.starts_with('/')) {
    suffix.remove_prefix(1);
    auto div = parse_scalar(suffix);
    if (div && *div != 0.0f) {
      scale = 1.0f / *div;
    }
  } else if (suffix.starts_with('*')) {
    suffix.remove_prefix(1);
    auto mul = parse_scalar(suffix);
    if (mul) {
      scale = *mul;
    }
  }
  for (float &c : color) {
    c *= scale;
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
      std::string_view value =
          trim(line.substr(std::string_view("#include").size()));
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
      std::string_view body =
          trim(line.substr(std::string_view("#define").size()));
      const std::size_t split = body.find_first_of(" \t");
      if (split == std::string_view::npos) {
        continue;
      }
      const std::string_view name = body.substr(0, split);
      const std::string_view value = trim(body.substr(split));
      // A function-like macro carries no material parameter.
      if (name.empty() || name.find('(') != std::string_view::npos ||
          value.empty()) {
        continue;
      }
      parameters.push_back({std::string(name), std::string(value), false});
      continue;
    }
    if (line.starts_with("const ")) {
      std::string_view body =
          trim(line.substr(std::string_view("const ").size()));
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
      parameters.push_back(
          {std::string(name), std::string(trim(value)), false});
    }
  }
  return parameters;
}

const ParsedParameter *
find_parameter(const std::vector<ParsedParameter> &parameters,
               std::string_view name) {
  for (const ParsedParameter &parameter : parameters) {
    if (!parameter.shader_body && parameter.name == name) {
      return &parameter;
    }
  }
  return nullptr;
}

bool is_map_parameter(std::string_view name) {
  return name.find("_MAP_") != std::string_view::npos ||
         name.ends_with("MapLoopNum");
}

std::string fixed(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
  return buffer;
}

std::string fixed(const std::array<float, 3> &value) {
  return fixed(value[0]) + " " + fixed(value[1]) + " " + fixed(value[2]);
}

void append(ImportReport &report, std::string name, ImportStatus status,
            std::string detail) {
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
  for (const ParsedParameter &parameter : parameters) {
    if (!parameter.shader_body) {
      ++material_parameters;
    }
  }
  if (material_parameters == 0) {
    result.report.error = "no Ray-MMD material parameters found";
    return result;
  }

  const ParsedParameter *enable = find_parameter(parameters, "EMISSIVE_ENABLE");
  const std::optional<float> enable_value =
      enable != nullptr ? parse_scalar(enable->value) : std::nullopt;
  const bool emission_on = enable_value.has_value() && *enable_value > 0.5f;
  const ParsedParameter *intensity =
      find_parameter(parameters, "emissiveIntensity");
  const std::optional<float> intensity_value =
      intensity != nullptr ? parse_scalar(intensity->value) : std::nullopt;

  MaterialResponse &response = result.response;
  for (const ParsedParameter &parameter : parameters) {
    if (parameter.shader_body) {
      append(result.report, parameter.name, ImportStatus::Unsupported,
             parameter.value + " is read as data and never compiled");
      continue;
    }
    const std::string_view name = parameter.name;

    if (name == "CUSTOM_ENABLE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      const int model =
          value.has_value() ? static_cast<int>(std::lround(*value)) : -1;
      const std::string_view label = shading_model_label(model);
      if (label.empty()) {
        append(result.report, parameter.name, ImportStatus::Unsupported,
               "shading model id " + parameter.value +
                   " has no native equivalent");
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
             "smoothness " + fixed(*value) + " -> roughness " +
                 fixed(response.roughness));
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
      const std::optional<std::array<float, 3>> value =
          parse_color(parameter.value);
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
      const std::optional<std::array<float, 3>> value =
          parse_color(parameter.value);
      if (!emission_on) {
        append(
            result.report, parameter.name, ImportStatus::Unsupported,
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
      const std::optional<std::array<float, 3>> value =
          parse_color(parameter.value);
      if (!value) {
        append(result.report, parameter.name, ImportStatus::Unsupported,
               "the value is not a colour");
        continue;
      }
      const std::array<float, 3> &color = *value;
      response.custom_b_color = color;
      response.custom_b =
          0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
      append(result.report, parameter.name, ImportStatus::Approximate,
             "colour " + fixed(color) + " reduced to luminance " +
                 fixed(response.custom_b));
      continue;
    }
    if (name == "HEX_TILING_ENABLE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value && *value > 0.5f) {
        if (response.hex_tiling_scale <= 0.01f) {
          response.hex_tiling_scale = 4.0f;
        }
        append(result.report, parameter.name, ImportStatus::Applied,
               "hex-tiling enabled (scale " + fixed(response.hex_tiling_scale) +
                   ")");
      } else {
        response.hex_tiling_scale = 0.0f;
        append(result.report, parameter.name, ImportStatus::Applied,
               "hex-tiling disabled");
      }
      continue;
    }
    if (name == "HEX_TILING_SCALE" || name == "hexTilingScale") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value && *value > 0.01f) {
        response.hex_tiling_scale = *value;
        append(result.report, parameter.name, ImportStatus::Applied,
               "hex-tiling scale " + fixed(*value));
      }
      continue;
    }
    if (name == "NORMAL_MAP_HEX_TILING") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value && *value > 0.5f) {
        if (response.hex_tiling_scale <= 0.01f) {
          response.hex_tiling_scale = 4.0f;
        }
        append(result.report, parameter.name, ImportStatus::Applied,
               "hex-tiling enabled from normal map (scale " +
                   fixed(response.hex_tiling_scale) + ")");
      }
      continue;
    }
    if (name == "ALBEDO_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "albedo.png" && val != "albedo.bmp" &&
          val != "albedo.tga") {
        response.albedo_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "albedo map texture " + std::string(val));
      }
      continue;
    }
    if (name == "NORMAL_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "normal.png" && val != "normal.bmp" &&
          val != "normal.tga") {
        response.normal_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "normal map texture " + std::string(val));
      } else {
        append(result.report, parameter.name, ImportStatus::Applied,
               "default flat normal");
      }
      continue;
    }
    if (name == "NORMAL_SUB_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "normal.png" && (response.normal_map.empty() || response.normal_map == "normal.png")) {
        response.normal_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "detail normal map texture " + std::string(val));
      }
      continue;
    }
    if (name == "SMOOTHNESS_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "smoothness.png" && val != "smoothness.bmp") {
        response.roughness_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "roughness map texture " + std::string(val));
      }
      continue;
    }
    if (name == "METALNESS_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "metalness.png" && val != "metalness.bmp") {
        response.metallic_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "metallic map texture " + std::string(val));
      }
      continue;
    }
    if (name == "OCCLUSION_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "occlusion.png" && val != "occlusion.bmp") {
        response.occlusion_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "occlusion map texture " + std::string(val));
      }
      continue;
    }
    if (name == "EMISSIVE_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "emissive.png" && val != "emissive.bmp") {
        response.emissive_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "emissive map texture " + std::string(val));
      }
      continue;
    }
    if (name == "NORMAL_MAP_FROM") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value && *value == 3.0f) {
        response.normal_map = "auto";
        append(result.report, parameter.name, ImportStatus::Applied,
               "auto-normal from diffuse height");
      } else {
        append(result.report, parameter.name, ImportStatus::Applied,
               "normal map source mode " + parameter.value);
      }
      continue;
    }
    if (name == "PROCEDURAL_HAIR") {
      const std::optional<float> value = parse_scalar(parameter.value);
      response.procedural_hair = (value && *value > 0.5f);
      append(result.report, parameter.name, ImportStatus::Applied,
             response.procedural_hair
                 ? "procedural hair tangent perturbation enabled"
                 : "disabled");
      continue;
    }
    if (name == "PROCEDURAL_HAIR_SCALE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.procedural_hair_scale = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "hair scale " + fixed(response.procedural_hair_scale));
      continue;
    }
    if (name == "PROCEDURAL_HAIR_INTENSITY") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.procedural_hair_intensity = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "hair intensity " + fixed(response.procedural_hair_intensity));
      continue;
    }
    if (name == "GLITTER_ENABLE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      response.glitter = (value && *value > 0.5f);
      append(result.report, parameter.name, ImportStatus::Applied,
             response.glitter ? "micro-glitter stochastic sparkle enabled"
                              : "disabled");
      continue;
    }
    if (name == "GLITTER_DENSITY") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.glitter_density = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "glitter density " + fixed(response.glitter_density));
      continue;
    }
    if (name == "GLITTER_ROUGHNESS") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.glitter_roughness = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "glitter roughness " + fixed(response.glitter_roughness));
      continue;
    }
    if (name == "GLITTER_INTENSITY") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.glitter_intensity = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "glitter intensity " + fixed(response.glitter_intensity));
      continue;
    }
    if (name == "EYE_PARALLAX_ENABLE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      response.eye_parallax = (value && *value > 0.5f);
      append(result.report, parameter.name, ImportStatus::Applied,
             response.eye_parallax ? "cornea-iris refraction parallax enabled"
                                   : "disabled");
      continue;
    }
    if (name == "EYE_IRIS_DEPTH") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.eye_iris_depth = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "iris depth " + fixed(response.eye_iris_depth));
      continue;
    }
    if (name == "EYE_CORNEA_IOR") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value)
        response.eye_cornea_ior = *value;
      append(result.report, parameter.name, ImportStatus::Applied,
             "cornea IOR " + fixed(response.eye_cornea_ior));
      continue;
    }
    if (name == "HASHED_ALPHA_ENABLE" || name == "ALPHAHASH_ENABLE" ||
        name == "HASHED_ALPHA_TEST_ENABLE") {
      const std::optional<float> value = parse_scalar(parameter.value);
      response.hashed_alpha = (value && *value > 0.5f);
      append(result.report, parameter.name, ImportStatus::Applied,
             response.hashed_alpha ? "hashed alpha enabled"
                                   : "hashed alpha disabled");
      continue;
    }
    if (name == "normalMapLoopNum" || name == "normalSubMapLoopNum" ||
        name == "albedoMapLoopNum" || name == "smoothnessMapLoopNum") {
      const std::optional<float> value = parse_scalar(parameter.value);
      if (value && *value > 1.01f) {
        if (response.hex_tiling_scale <= 0.01f) {
          response.hex_tiling_scale = *value;
        }
        append(result.report, parameter.name, ImportStatus::Applied,
               "texture map tiling loop " + fixed(*value));
      } else {
        append(result.report, parameter.name, ImportStatus::Applied,
               "neutral 1x tiling");
      }
      continue;
    }
    if (name == "DETAIL_NORMAL_MAP_FILE") {
      std::string_view val = parameter.value;
      val = trim(val);
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
      }
      const std::size_t slash = val.find_last_of("/\\");
      if (slash != std::string_view::npos) {
        val = val.substr(slash + 1);
      }
      if (!val.empty() && val != "normal.png" && (response.normal_map.empty() || response.normal_map == "normal.png")) {
        response.normal_map = std::string(val);
        append(result.report, parameter.name, ImportStatus::Applied,
               "detail normal map texture " + std::string(val));
      }
      continue;
    }
    if (name == "albedo") {
      const std::optional<std::array<float, 3>> value =
          parse_color(parameter.value);
      if (!value) {
        append(result.report, parameter.name, ImportStatus::Unsupported,
               "the value is not a colour");
        continue;
      }
      const bool is_neutral_white = (std::abs((*value)[0] - 1.0f) < 1.0e-3f &&
                                     std::abs((*value)[1] - 1.0f) < 1.0e-3f &&
                                     std::abs((*value)[2] - 1.0f) < 1.0e-3f);
      if (is_neutral_white) {
        append(result.report, parameter.name, ImportStatus::Unsupported,
               "a per-subset override keeps the authored PMX colour");
      } else {
        response.albedo = *value;
        append(result.report, parameter.name, ImportStatus::Applied,
               "albedo base tint " + fixed(*value));
      }
      continue;
    }
    if (name == "albedoSub") {
      append(result.report, parameter.name, ImportStatus::Unsupported,
             "secondary albedo sub-layer is not imported");
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
