#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "hikari/core/d3d11.hpp"
#include "hikari/core/reflection.hpp"

namespace hikari::gfx {

struct ShaderDefine {
    std::string name;
    std::string value;
};

struct ShaderProgram {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
    core::ShaderReflectionData vertex_reflection;
    core::ShaderReflectionData pixel_reflection;
};

struct ComputeProgram {
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> compute;
};

// Compiles and caches shader permutations. Sources are embedded at build time;
// setting HIKARI_GFX_SHADER_DIR to a directory containing gfx_core.hlsl enables
// hot reload from disk for the lab.
class ShaderLibrary {
public:
    explicit ShaderLibrary(core::D3D11Context& context);

    const ShaderProgram& get(const std::string& key, const std::string& source_name,
                             const std::string& vs_entry, const std::string& ps_entry,
                             const std::vector<ShaderDefine>& defines = {},
                             const std::vector<D3D11_INPUT_ELEMENT_DESC>* layout = nullptr);

    // Compute entry points are cached separately because they have no vertex
    // stage and no input layout.
    const ComputeProgram& get_compute(const std::string& key, const std::string& source_name,
                                      const std::string& cs_entry,
                                      const std::vector<ShaderDefine>& defines = {});

    void reload();

private:
    std::string load_source(const std::string& source_name) const;

    core::D3D11Context& context_;
    std::unordered_map<std::string, ShaderProgram> cache_;
    std::unordered_map<std::string, ComputeProgram> compute_cache_;
};

const std::unordered_map<std::string, std::string>& embedded_shader_sources();

} // namespace hikari::gfx
