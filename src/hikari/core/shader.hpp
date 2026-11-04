#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hikari/core/states.hpp"

namespace hikari::core {

struct PassDesc {
    std::string name;
    std::unordered_map<std::string, std::string> annotations;
    std::optional<std::pair<std::string, std::string>> vs_entry;
    std::optional<std::pair<std::string, std::string>> ps_entry;
    std::optional<std::pair<std::string, std::string>> cs_entry;
    std::optional<std::pair<std::string, std::string>> gs_entry;
    std::optional<std::pair<std::string, std::string>> hs_entry;
    std::optional<std::pair<std::string, std::string>> ds_entry;
    BlendPreset blend_preset = BlendPreset::Opaque;
    DepthPreset depth_preset = DepthPreset::ReadWriteLessEqual;
    RasterizerPreset raster_preset = RasterizerPreset::CullNoneSolid;
};

struct TechniqueDesc {
    std::string name;
    std::unordered_map<std::string, std::string> annotations;
    std::vector<PassDesc> passes;
};

struct GlobalVarDesc {
    std::string name;
    std::string semantic;
    std::string default_val;
    std::unordered_map<std::string, std::string> annotations;
};

std::unordered_map<std::string, std::string> parse_annotations_block(const std::string& block);
std::vector<GlobalVarDesc> parse_global_variables(const std::string& source);
std::unordered_map<std::string, std::string> parse_variable_semantics(const std::string& source);
std::unordered_map<std::string, std::pair<std::string, bool>> parse_variable_types(
    const std::string& source);

std::vector<std::uint8_t> compile_hlsl(const std::string& source,
                                       const std::string& entry_point,
                                       const std::string& target,
                                       const std::optional<std::string>& source_name = std::nullopt,
                                       const std::vector<std::pair<std::string, std::string>>& defines = {});
std::vector<std::uint8_t> disassemble_dxbc(const std::vector<std::uint8_t>& bytecode);

std::vector<TechniqueDesc> parse_techniques(const std::string& source);

} // namespace hikari::core
