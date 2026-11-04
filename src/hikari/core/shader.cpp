#include "hikari/core/shader.hpp"

#include <windows.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "hikari/core/log.hpp"

namespace hikari::core {
namespace {

using PfnD3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                       LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
using PfnD3DDisassemble = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);

HMODULE load_d3dcompiler() {
    static HMODULE module = [] {
        HMODULE loaded = ::LoadLibraryW(L"d3dcompiler_47.dll");
        if (loaded == nullptr) {
            loaded = ::LoadLibraryW(L"d3dcompiler_43.dll");
        }
        return loaded;
    }();
    return module;
}

PfnD3DCompile get_d3dcompile() {
    static PfnD3DCompile fn = [] {
        HMODULE module = load_d3dcompiler();
        return module != nullptr
                   ? reinterpret_cast<PfnD3DCompile>(::GetProcAddress(module, "D3DCompile"))
                   : nullptr;
    }();
    return fn;
}

PfnD3DDisassemble get_d3ddisassemble() {
    static PfnD3DDisassemble fn = [] {
        HMODULE module = load_d3dcompiler();
        return module != nullptr
                   ? reinterpret_cast<PfnD3DDisassemble>(::GetProcAddress(module, "D3DDisassemble"))
                   : nullptr;
    }();
    return fn;
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string to_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> split_whitespace(const std::string& value) {
    std::istringstream stream(value);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::vector<std::string> split_any(const std::string& value, const std::string& separators) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (separators.find(c) != std::string::npos) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::optional<GlobalVarDesc> parse_single_global_var(const std::string& decl) {
    const auto open = decl.find('<');
    if (open == std::string::npos) {
        return std::nullopt;
    }
    const auto close = decl.find('>', open);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    const std::string prefix = trim(decl.substr(0, open));
    const std::string annotations = decl.substr(open + 1, close - open - 1);
    const std::string after = decl.substr(close + 1);

    std::string var_part = prefix;
    std::string semantic;
    const auto colon = prefix.find(':');
    if (colon != std::string::npos) {
        var_part = trim(prefix.substr(0, colon));
        semantic = trim(prefix.substr(colon + 1));
    }

    const auto tokens = split_whitespace(var_part);
    if (tokens.empty()) {
        return std::nullopt;
    }
    GlobalVarDesc result;
    result.name = trim(tokens.back());
    if (result.name.empty()) {
        return std::nullopt;
    }
    result.semantic = semantic;
    result.annotations = parse_annotations_block(annotations);

    const auto equals = after.find('=');
    if (equals != std::string::npos) {
        std::string value = trim(after.substr(equals + 1));
        while (!value.empty() && value.back() == ';') {
            value.pop_back();
        }
        result.default_val = trim(value);
    }
    return result;
}

std::optional<std::pair<std::string, std::string>> parse_shader_stage(const std::string& line,
                                                                      const std::string& default_profile) {
    std::string clean = line;
    for (char& c : clean) {
        if (c == '(' || c == ')' || c == ';') {
            c = ' ';
        }
    }
    const auto tokens = split_whitespace(clean);
    std::string profile = default_profile;
    std::string entry;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string token = tokens[i];
        if (starts_with(token, "vs_") || starts_with(token, "ps_") || starts_with(token, "cs_") ||
            starts_with(token, "gs_") || starts_with(token, "hs_") || starts_with(token, "ds_")) {
            profile = token;
            profile.erase(std::remove(profile.begin(), profile.end(), ','), profile.end());
            if (i + 1 < tokens.size()) {
                entry = tokens[i + 1];
                entry.erase(std::remove(entry.begin(), entry.end(), ','), entry.end());
            }
        }
    }
    if (entry.empty()) {
        return std::nullopt;
    }
    return std::make_pair(entry, profile);
}

} // namespace

std::unordered_map<std::string, std::string> parse_annotations_block(const std::string& block) {
    std::unordered_map<std::string, std::string> result;
    const auto start_it = block.find('<');
    const std::size_t start = start_it == std::string::npos ? 0 : start_it + 1;
    const auto end_it = block.rfind('>');
    const std::size_t end = end_it == std::string::npos ? block.size() : end_it;
    if (start >= end) {
        return result;
    }
    const std::string inner = block.substr(start, end - start);

    std::vector<std::string> statements;
    std::string current;
    bool in_quotes = false;
    char quote_char = '"';
    bool escaped = false;
    for (char c : inner) {
        if (in_quotes) {
            current.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote_char) {
                in_quotes = false;
            }
        } else if (c == '"' || c == '\'') {
            in_quotes = true;
            quote_char = c;
            current.push_back(c);
        } else if (c == ';') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                statements.push_back(trimmed);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    const std::string trimmed = trim(current);
    if (!trimmed.empty()) {
        statements.push_back(trimmed);
    }

    for (const auto& statement : statements) {
        const auto equals = statement.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string lhs = statement.substr(0, equals);
        const auto lhs_tokens = split_whitespace(lhs);
        const std::string key = lhs_tokens.empty() ? std::string() : trim(lhs_tokens.back());
        std::string value = trim(statement.substr(equals + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty()) {
            result[key] = value;
        }
    }
    return result;
}

std::vector<GlobalVarDesc> parse_global_variables(const std::string& source) {
    std::vector<GlobalVarDesc> variables;
    bool in_block = false;
    std::string current_decl;
    for (const auto& line : split_lines(source)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || starts_with(trimmed, "//") || starts_with(trimmed, "#")) {
            continue;
        }
        if (!in_block) {
            if (trimmed.find('<') != std::string::npos && !starts_with(trimmed, "technique") &&
                !starts_with(trimmed, "pass") && !starts_with(trimmed, "cbuffer")) {
                current_decl = trimmed;
                if (trimmed.find('>') != std::string::npos) {
                    if (auto var = parse_single_global_var(current_decl)) {
                        variables.push_back(std::move(*var));
                    }
                    current_decl.clear();
                } else {
                    in_block = true;
                }
            }
        } else {
            current_decl += ' ';
            current_decl += trimmed;
            if (trimmed.find('>') != std::string::npos) {
                in_block = false;
                if (auto var = parse_single_global_var(current_decl)) {
                    variables.push_back(std::move(*var));
                }
                current_decl.clear();
            }
        }
    }
    return variables;
}

std::unordered_map<std::string, std::string> parse_variable_semantics(const std::string& source) {
    std::unordered_map<std::string, std::string> result;
    bool in_struct = false;
    for (const auto& line : split_lines(source)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || starts_with(trimmed, "//") || starts_with(trimmed, "#")) {
            continue;
        }
        if (starts_with(trimmed, "struct")) {
            in_struct = true;
        }
        if (in_struct) {
            if (trimmed.find('}') != std::string::npos) {
                in_struct = false;
            }
            continue;
        }
        if (starts_with(trimmed, "technique") || starts_with(trimmed, "pass") ||
            trimmed.find('(') != std::string::npos) {
            continue;
        }
        const auto colon = trimmed.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string lhs = trimmed.substr(0, colon);
        const auto lhs_tokens = split_whitespace(lhs);
        if (lhs_tokens.empty()) {
            continue;
        }
        std::string name = lhs_tokens.back();
        name.erase(std::remove(name.begin(), name.end(), '*'), name.end());
        std::string semantic = trimmed.substr(colon + 1);
        const auto cut = semantic.find_first_of("<;=");
        if (cut != std::string::npos) {
            semantic = semantic.substr(0, cut);
        }
        semantic = trim(semantic);
        if (!name.empty() && !semantic.empty() && !starts_with(semantic, "register") &&
            !starts_with(semantic, "packoffset")) {
            result[to_lower(name)] = to_upper(semantic);
        }
    }
    return result;
}

std::unordered_map<std::string, std::pair<std::string, bool>> parse_variable_types(
    const std::string& source) {
    std::unordered_map<std::string, std::pair<std::string, bool>> result;
    for (auto line : split_lines(source)) {
        const auto comment = line.find("//");
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty() || starts_with(line, "#")) {
            continue;
        }
        const auto cut = line.find_first_of(":<;=");
        std::string declaration = cut == std::string::npos ? line : line.substr(0, cut);
        declaration = trim(declaration);
        const auto parts = split_whitespace(declaration);
        if (parts.size() < 2) {
            continue;
        }
        std::string name = parts.back();
        name.erase(std::remove(name.begin(), name.end(), '*'), name.end());
        const std::string type = parts[parts.size() - 2];
        if (name.empty() || type.empty() || name == "{" || name == "}") {
            continue;
        }
        bool row_major = false;
        for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
            if (parts[i] == "row_major") {
                row_major = true;
            }
        }
        result[to_lower(name)] = {type, row_major};
    }
    return result;
}

std::vector<std::uint8_t> compile_hlsl(const std::string& source,
                                       const std::string& entry_point,
                                       const std::string& target,
                                       const std::optional<std::string>& source_name,
                                       const std::vector<std::pair<std::string, std::string>>& defines) {
    const PfnD3DCompile compile = get_d3dcompile();
    if (compile == nullptr) {
        throw std::runtime_error("d3dcompiler_47.dll not found on system");
    }

    std::vector<std::pair<std::string, std::string>> macros;
    macros.emplace_back("__HIKARI__", "1");
    macros.emplace_back("DIRECTX_11", "1");
    macros.emplace_back("SHADER_MODEL_5_0", "1");
    macros.insert(macros.end(), defines.begin(), defines.end());

    std::vector<D3D_SHADER_MACRO> macro_descs;
    macro_descs.reserve(macros.size() + 1);
    for (const auto& macro : macros) {
        macro_descs.push_back({macro.first.c_str(), macro.second.c_str()});
    }
    macro_descs.push_back({nullptr, nullptr});

    const std::string name = source_name.value_or("hikari_shader.hlsl");
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = compile(source.data(), source.size(), name.c_str(), macro_descs.data(),
                               D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point.c_str(),
                               target.c_str(), flags, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string message;
        if (errors != nullptr) {
            message.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            while (!message.empty() && message.back() == '\0') {
                message.pop_back();
            }
            errors->Release();
        } else {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "D3DCompile failed with HRESULT 0x%08lX",
                          static_cast<unsigned long>(hr));
            message = buffer;
        }
        if (code != nullptr) {
            code->Release();
        }
        throw std::runtime_error(message);
    }

    if (code == nullptr) {
        throw std::runtime_error("D3DCompile returned no bytecode");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(code->GetBufferPointer());
    std::vector<std::uint8_t> result(bytes, bytes + code->GetBufferSize());
    code->Release();
    if (errors != nullptr) {
        errors->Release();
    }
    return result;
}

std::vector<std::uint8_t> disassemble_dxbc(const std::vector<std::uint8_t>& bytecode) {
    const PfnD3DDisassemble disassemble = get_d3ddisassemble();
    if (disassemble == nullptr) {
        throw std::runtime_error("D3DDisassemble is unavailable");
    }
    ID3DBlob* blob = nullptr;
    const HRESULT hr = disassemble(bytecode.data(), bytecode.size(), 0, nullptr, &blob);
    if (FAILED(hr) || blob == nullptr) {
        throw std::runtime_error("D3DDisassemble failed");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
    std::vector<std::uint8_t> result(bytes, bytes + blob->GetBufferSize());
    blob->Release();
    return result;
}

std::vector<TechniqueDesc> parse_techniques(const std::string& source) {
    std::vector<TechniqueDesc> techniques;
    std::optional<TechniqueDesc> current_tech;
    std::optional<PassDesc> current_pass;
    std::optional<std::string> tech_anno_buf;
    std::optional<std::string> pass_anno_buf;

    for (const auto& line : split_lines(source)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || starts_with(trimmed, "//")) {
            continue;
        }

        if (tech_anno_buf) {
            tech_anno_buf->push_back(' ');
            *tech_anno_buf += trimmed;
            if (trimmed.find('>') != std::string::npos) {
                if (current_tech) {
                    current_tech->annotations = parse_annotations_block(*tech_anno_buf);
                }
                tech_anno_buf.reset();
            }
            continue;
        }
        if (pass_anno_buf) {
            pass_anno_buf->push_back(' ');
            *pass_anno_buf += trimmed;
            if (trimmed.find('>') != std::string::npos) {
                if (current_pass) {
                    current_pass->annotations = parse_annotations_block(*pass_anno_buf);
                }
                pass_anno_buf.reset();
            }
            continue;
        }

        if (starts_with(trimmed, "technique")) {
            const bool eleven = starts_with(trimmed, "technique11");
            const std::string after = trim(trimmed.substr(eleven ? 11 : 9));
            std::string name;
            std::optional<std::string> rest;
            const auto lt = after.find('<');
            const auto brace = after.find('{');
            if (lt != std::string::npos) {
                name = trim(after.substr(0, lt));
                rest = "<" + after.substr(lt + 1);
            } else if (brace != std::string::npos) {
                name = trim(after.substr(0, brace));
            } else {
                const auto tokens = split_whitespace(after);
                name = tokens.empty() ? "MainTec" : tokens.front();
            }

            TechniqueDesc tech;
            tech.name = name;
            if (rest) {
                const auto end = rest->find('>');
                if (end != std::string::npos) {
                    tech.annotations = parse_annotations_block(rest->substr(0, end + 1));
                } else {
                    tech_anno_buf = *rest;
                }
            }
            current_tech = std::move(tech);
            continue;
        }

        if (current_tech) {
            if (starts_with(trimmed, "pass")) {
                const std::string after = trim(trimmed.substr(4));
                std::string name;
                std::optional<std::string> rest;
                const auto lt = after.find('<');
                const auto brace = after.find('{');
                if (lt != std::string::npos) {
                    name = trim(after.substr(0, lt));
                    rest = "<" + after.substr(lt + 1);
                } else if (brace != std::string::npos) {
                    name = trim(after.substr(0, brace));
                } else {
                    const auto tokens = split_whitespace(after);
                    name = tokens.empty() ? "P0" : tokens.front();
                }
                if (name.empty()) {
                    name = "P" + std::to_string(current_tech->passes.size());
                }

                PassDesc pass;
                pass.name = name;
                if (rest) {
                    const auto end = rest->find('>');
                    if (end != std::string::npos) {
                        pass.annotations = parse_annotations_block(rest->substr(0, end + 1));
                    } else {
                        pass_anno_buf = *rest;
                    }
                }
                current_pass = std::move(pass);
                continue;
            }

            if (current_pass) {
                if (starts_with(trimmed, "<") && current_pass->annotations.empty()) {
                    const auto end = trimmed.find('>');
                    if (end != std::string::npos) {
                        current_pass->annotations = parse_annotations_block(trimmed.substr(0, end + 1));
                    } else {
                        pass_anno_buf = trimmed;
                    }
                    continue;
                }

                if (trimmed.find("VertexShader") != std::string::npos) {
                    current_pass->vs_entry = parse_shader_stage(trimmed, "vs_5_0");
                } else if (trimmed.find("PixelShader") != std::string::npos) {
                    current_pass->ps_entry = parse_shader_stage(trimmed, "ps_5_0");
                } else if (trimmed.find("ComputeShader") != std::string::npos) {
                    current_pass->cs_entry = parse_shader_stage(trimmed, "cs_5_0");
                } else if (trimmed.find("GeometryShader") != std::string::npos) {
                    current_pass->gs_entry = parse_shader_stage(trimmed, "gs_5_0");
                } else if (trimmed.find("HullShader") != std::string::npos) {
                    current_pass->hs_entry = parse_shader_stage(trimmed, "hs_5_0");
                } else if (trimmed.find("DomainShader") != std::string::npos) {
                    current_pass->ds_entry = parse_shader_stage(trimmed, "ds_5_0");
                }

                const std::string upper = to_upper(trimmed);
                if (upper.find("ALPHABLENDENABLE") != std::string::npos ||
                    upper.find("BLENDENABLE") != std::string::npos) {
                    if (upper.find("TRUE") != std::string::npos) {
                        current_pass->blend_preset = BlendPreset::AlphaBlend;
                    }
                } else if (upper.find("DESTBLEND") != std::string::npos &&
                           upper.find("ONE") != std::string::npos) {
                    current_pass->blend_preset = BlendPreset::Additive;
                } else if (upper.find("ZENABLE") != std::string::npos ||
                           upper.find("DEPTHENABLE") != std::string::npos) {
                    if (upper.find("FALSE") != std::string::npos) {
                        current_pass->depth_preset = DepthPreset::Disabled;
                    }
                } else if (upper.find("ZWRITEENABLE") != std::string::npos ||
                           upper.find("DEPTHWRITE") != std::string::npos) {
                    if (upper.find("FALSE") != std::string::npos) {
                        current_pass->depth_preset = DepthPreset::ReadOnlyLessEqual;
                    }
                } else if (upper.find("FILLMODE") != std::string::npos &&
                           upper.find("WIREFRAME") != std::string::npos) {
                    current_pass->raster_preset = RasterizerPreset::Wireframe;
                } else if (upper.find("CULLMODE") != std::string::npos) {
                    if (upper.find("NONE") != std::string::npos) {
                        current_pass->raster_preset = RasterizerPreset::CullNoneSolid;
                    } else if (upper.find("CW") != std::string::npos ||
                               upper.find("FRONT") != std::string::npos) {
                        current_pass->raster_preset = RasterizerPreset::CullFrontSolid;
                    } else if (upper.find("CCW") != std::string::npos ||
                               upper.find("BACK") != std::string::npos) {
                        current_pass->raster_preset = RasterizerPreset::CullBackSolid;
                    } else {
                        current_pass->raster_preset = RasterizerPreset::CullNoneSolid;
                    }
                }

                if (trimmed.find('}') != std::string::npos) {
                    current_tech->passes.push_back(std::move(*current_pass));
                    current_pass.reset();
                }
            } else if (trimmed.find('}') != std::string::npos) {
                techniques.push_back(std::move(*current_tech));
                current_tech.reset();
            }
        }
    }

    if (techniques.empty()) {
        PassDesc pass;
        pass.name = "DefaultPass";
        if (source.find("VS_Main") != std::string::npos || source.find("VS") != std::string::npos) {
            pass.vs_entry = std::make_pair("VS_Main", "vs_5_0");
        }
        if (source.find("PS_Main") != std::string::npos || source.find("PS") != std::string::npos) {
            pass.ps_entry = std::make_pair("PS_Main", "ps_5_0");
        }
        if (source.find("CS_Main") != std::string::npos || source.find("CS") != std::string::npos) {
            pass.cs_entry = std::make_pair("CS_Main", "cs_5_0");
        }
        TechniqueDesc tech;
        tech.name = "MainTec";
        tech.passes.push_back(std::move(pass));
        techniques.push_back(std::move(tech));
    }

    return techniques;
}

} // namespace hikari::core
