#pragma once

#include "hikari/gfx/matgen/material_graph.hpp"
#include <string>
#include <vector>

namespace hikari::gfx::matgen {

struct UniformProperty {
    std::string name;
    SocketType type = SocketType::Float;
    uint32_t byte_offset = 0;
    uint32_t byte_size = 4;
    SocketValue initial_value;
};

struct CompiledMaterialShader {
    std::string hlsl_source;
    std::vector<uint8_t> compiled_bytecode;
    uint64_t cache_hash = 0;
    uint32_t cbuffer_size_bytes = 0;
    std::vector<UniformProperty> uniforms;
    std::vector<std::string> texture_slots;
    bool has_alpha_discard = false;
    bool has_hashed_alpha = false;
    bool writes_normal = false;
    bool writes_emissive = false;
    int32_t target_shading_model = 0;
};

class MaterialCompiler {
public:
    static CompiledMaterialShader compile(const MaterialGraph& graph);

private:
    static std::string emit_hlsl(const MaterialGraph& graph,
                                const std::vector<uint32_t>& sorted_nodes,
                                std::vector<UniformProperty>& out_uniforms,
                                std::vector<std::string>& out_textures);
};

} // namespace hikari::gfx::matgen
