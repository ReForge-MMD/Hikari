#include "hikari/gfx/shader_library.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "hikari/core/shader.hpp"

namespace hikari::gfx {
namespace {

std::vector<std::pair<std::string, std::string>> to_compile_defines(
    const std::vector<ShaderDefine>& defines) {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(defines.size());
    for (const ShaderDefine& define : defines) {
        result.emplace_back(define.name, define.value);
    }
    return result;
}

} // namespace

ShaderLibrary::ShaderLibrary(core::D3D11Context& context) : context_(context) {}

const ShaderProgram& ShaderLibrary::get(const std::string& key, const std::string& source_name,
                                        const std::string& vs_entry, const std::string& ps_entry,
                                        const std::vector<ShaderDefine>& defines,
                                        const std::vector<D3D11_INPUT_ELEMENT_DESC>* layout) {
    std::string cache_key = key + '|' + vs_entry + '|' + ps_entry;
    for (const ShaderDefine& define : defines) {
        cache_key += '|';
        cache_key += define.name;
        cache_key += '=';
        cache_key += define.value;
    }
    const auto existing = cache_.find(cache_key);
    if (existing != cache_.end()) {
        return existing->second;
    }

    const std::string source = load_source(source_name);
    const auto compile_defines = to_compile_defines(defines);
    const auto vs_bytecode =
        core::compile_hlsl(source, vs_entry, context_.vs_profile(), source_name, compile_defines);
    const auto ps_bytecode =
        core::compile_hlsl(source, ps_entry, context_.ps_profile(), source_name, compile_defines);

    ShaderProgram program;
    program.vertex_reflection = core::ShaderReflectionData::reflect(vs_bytecode);
    program.pixel_reflection = core::ShaderReflectionData::reflect(ps_bytecode);
    if (FAILED(context_.device->CreateVertexShader(vs_bytecode.data(), vs_bytecode.size(),
                                                   nullptr, program.vertex.GetAddressOf()))) {
        throw std::runtime_error("CreateVertexShader failed for " + cache_key);
    }
    if (FAILED(context_.device->CreatePixelShader(ps_bytecode.data(), ps_bytecode.size(),
                                                  nullptr, program.pixel.GetAddressOf()))) {
        throw std::runtime_error("CreatePixelShader failed for " + cache_key);
    }
    if (layout != nullptr && !layout->empty()) {
        if (FAILED(context_.device->CreateInputLayout(layout->data(),
                                                      static_cast<UINT>(layout->size()),
                                                      vs_bytecode.data(), vs_bytecode.size(),
                                                      program.input_layout.GetAddressOf()))) {
            throw std::runtime_error("CreateInputLayout failed for " + cache_key);
        }
    }
    return cache_.emplace(std::move(cache_key), std::move(program)).first->second;
}

void ShaderLibrary::reload() {
    cache_.clear();
}

std::string ShaderLibrary::load_source(const std::string& source_name) const {
    if (const char* directory = std::getenv("HIKARI_GFX_SHADER_DIR"); directory != nullptr) {
        const std::filesystem::path path = std::filesystem::path(directory) / source_name;
        std::ifstream file(path, std::ios::binary);
        if (file) {
            std::ostringstream stream;
            stream << file.rdbuf();
            return stream.str();
        }
    }
    const auto& sources = embedded_shader_sources();
    const auto it = sources.find(source_name);
    if (it == sources.end()) {
        throw std::runtime_error("shader source not found: " + source_name);
    }
    return it->second;
}

} // namespace hikari::gfx
