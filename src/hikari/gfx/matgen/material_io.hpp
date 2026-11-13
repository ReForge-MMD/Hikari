#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "hikari/gfx/material.hpp"
#include "hikari/gfx/matgen/material_graph.hpp"

namespace hikari::gfx::matgen {

struct MaterialFileData {
    std::string name;
    int version = 1;
    MaterialResponse response;
    std::optional<MaterialGraph> graph;
};

class MaterialIO {
public:
    static std::string serialize_hkmat(const MaterialFileData& data);
    static std::string serialize_response(std::string_view name, const MaterialResponse& response);

    static std::optional<MaterialFileData> deserialize_hkmat(std::string_view json_text);

    static bool save_file(std::string_view filepath, const MaterialFileData& data);
    static std::optional<MaterialFileData> load_file(std::string_view filepath);
};

} // namespace hikari::gfx::matgen
