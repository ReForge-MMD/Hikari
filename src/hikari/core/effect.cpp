#include "hikari/core/effect.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "hikari/core/backend.hpp"
#include "hikari/core/com.hpp"
#include "hikari/core/log.hpp"
#include "hikari/core/reflection.hpp"
#include "hikari/core/shader.hpp"

namespace hikari::core {

std::mutex g_active_pass_mutex;
std::optional<ActivePassInfo> g_active_pass;

const ID3DXEffectVtbl* get_effect_vtbl();

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

template <typename T>
Microsoft::WRL::ComPtr<T> create_shader(ID3D11Device* device,
                                        const std::vector<std::uint8_t>& bytecode,
                                        HRESULT (STDMETHODCALLTYPE ID3D11Device::*create)(
                                            const void*, SIZE_T, ID3D11ClassLinkage*, T**)) {
    Microsoft::WRL::ComPtr<T> shader;
    const HRESULT hr = (device->*create)(bytecode.data(), bytecode.size(), nullptr,
                                         shader.GetAddressOf());
    if (FAILED(hr)) {
        throw std::runtime_error("failed to create a Direct3D 11 shader stage");
    }
    return shader;
}

std::optional<std::size_t> find_offset(
    const std::unordered_map<std::string, std::size_t>& by_semantic,
    const std::unordered_map<std::string, std::size_t>& by_name,
    const std::vector<EffectParameter>& parameters, const std::string& semantic,
    const std::vector<std::string>& alternates) {
    std::optional<std::size_t> index;
    const auto semantic_it = by_semantic.find(semantic);
    if (semantic_it != by_semantic.end()) {
        index = semantic_it->second;
    } else {
        for (const auto& alternate : alternates) {
            const auto it = by_name.find(lower(alternate));
            if (it != by_name.end()) {
                index = it->second;
                break;
            }
        }
    }
    if (index && *index < parameters.size()) {
        const auto& parameter = parameters[*index];
        if (parameter.offset < 65536) {
            return parameter.offset;
        }
    }
    return std::nullopt;
}

} // namespace

bool parse_and_write_default_val(const std::string& value_in, const std::string& type,
                                 std::uint8_t* out, std::size_t size) {
    std::string value = value_in;
    while (!value.empty() && value.back() == ';') {
        value.pop_back();
    }
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos || size == 0) {
        return false;
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);
    const std::string lower_value = lower(value);

    if (type == "bool" || lower_value == "true" || lower_value == "false") {
        const std::uint32_t boolean = (lower_value == "true" || lower_value == "1") ? 1u : 0u;
        if (size >= 4) {
            std::memcpy(out, &boolean, 4);
            return true;
        }
    }
    if (type.rfind("int", 0) == 0) {
        try {
            const std::int32_t integer = std::stoi(value);
            if (size >= 4) {
                std::memcpy(out, &integer, 4);
                return true;
            }
        } catch (...) {
        }
    }

    std::string clean = value;
    for (const char* prefix : {"float4", "float3", "float2", "float"}) {
        if (clean.rfind(prefix, 0) == 0) {
            clean = clean.substr(std::strlen(prefix));
            break;
        }
    }
    while (!clean.empty() && (clean.front() == '(' || clean.front() == '{')) {
        clean.erase(clean.begin());
    }
    while (!clean.empty() && (clean.back() == ')' || clean.back() == '}')) {
        clean.pop_back();
    }

    std::vector<float> parts;
    std::stringstream stream(clean);
    std::string part;
    while (std::getline(stream, part, ',')) {
        const auto first = part.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = part.find_last_not_of(" \t");
        part = part.substr(first, last - first + 1);
        if (!part.empty() && (part.back() == 'f' || part.back() == 'F')) {
            part.pop_back();
        }
        try {
            parts.push_back(std::stof(part));
        } catch (...) {
            return false;
        }
    }
    if (parts.empty()) {
        return false;
    }
    const std::size_t count = std::min(parts.size(), size / 4);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(out + i * 4, &parts[i], 4);
    }
    return true;
}

HikariEffect* HikariEffect::from_handle(void* self) {
    return static_cast<HikariEffect*>(self);
}

HikariEffect* HikariEffect::create(const std::string& source, const std::string& source_name,
                                   D3D11Context& d3d11) {
    auto* effect = new HikariEffect();
    effect->device = d3d11.device;
    effect->context = d3d11.context;
    effect->vtbl = get_effect_vtbl();

    try {
        effect->techniques = parse_techniques(source);
        log_msg("Parsed " + std::to_string(effect->techniques.size()) + " techniques from " +
                source_name);

        std::unordered_map<std::string, std::vector<std::uint8_t>> bytecode_cache;
        const auto compile_cached = [&](const std::string& entry,
                                        const std::string& target) -> std::vector<std::uint8_t> {
            const std::string key = entry + "|" + target;
            const auto it = bytecode_cache.find(key);
            if (it != bytecode_cache.end()) {
                return it->second;
            }
            auto bytecode = compile_hlsl(source, entry, target, source_name);
            bytecode_cache.emplace(key, bytecode);
            return bytecode;
        };

        for (const auto& technique : effect->techniques) {
            std::vector<CompiledPass> passes;
            passes.reserve(technique.passes.size());
            for (const auto& pass : technique.passes) {
                CompiledPass compiled;
                compiled.blend_preset = pass.blend_preset;
                compiled.depth_preset = pass.depth_preset;
                compiled.raster_preset = pass.raster_preset;

                if (pass.vs_entry) {
                    const auto bytecode = compile_cached(pass.vs_entry->first, pass.vs_entry->second);
                    compiled.vs = create_shader<ID3D11VertexShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreateVertexShader);
                    compiled.vs_bytecode = bytecode;
                    const auto reflected = ShaderReflectionData::reflect(bytecode);
                    for (const auto& resource : reflected.resources) {
                        if (resource.type == ReflectedResourceType::Texture) {
                            effect->name_to_texture_slot[lower(resource.name)] = resource.bind_point;
                        }
                    }
                }
                if (pass.ps_entry) {
                    const auto bytecode = compile_cached(pass.ps_entry->first, pass.ps_entry->second);
                    compiled.ps = create_shader<ID3D11PixelShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreatePixelShader);
                    compiled.ps_bytecode = bytecode;
                    const auto reflected = ShaderReflectionData::reflect(bytecode);
                    for (const auto& resource : reflected.resources) {
                        if (resource.type == ReflectedResourceType::Texture) {
                            effect->name_to_texture_slot[lower(resource.name)] = resource.bind_point;
                        }
                    }
                }
                if (pass.cs_entry) {
                    const auto bytecode = compile_cached(pass.cs_entry->first, pass.cs_entry->second);
                    compiled.cs = create_shader<ID3D11ComputeShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreateComputeShader);
                    compiled.cs_bytecode = bytecode;
                    const auto reflected = ShaderReflectionData::reflect(bytecode);
                    if (reflected.thread_group_size[0] > 0) {
                        compiled.thread_group_size = reflected.thread_group_size;
                    }
                }
                if (pass.gs_entry) {
                    const auto bytecode = compile_cached(pass.gs_entry->first, pass.gs_entry->second);
                    compiled.gs = create_shader<ID3D11GeometryShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreateGeometryShader);
                    compiled.gs_bytecode = bytecode;
                }
                if (pass.hs_entry) {
                    const auto bytecode = compile_cached(pass.hs_entry->first, pass.hs_entry->second);
                    compiled.hs = create_shader<ID3D11HullShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreateHullShader);
                    compiled.hs_bytecode = bytecode;
                }
                if (pass.ds_entry) {
                    const auto bytecode = compile_cached(pass.ds_entry->first, pass.ds_entry->second);
                    compiled.ds = create_shader<ID3D11DomainShader>(
                        d3d11.device.Get(), bytecode, &ID3D11Device::CreateDomainShader);
                    compiled.ds_bytecode = bytecode;
                }
                passes.push_back(std::move(compiled));
            }
            effect->compiled_techniques.push_back(std::move(passes));
        }

        for (const auto& technique : effect->techniques) {
            std::vector<std::size_t> technique_annotations;
            for (const auto& [key, value] : technique.annotations) {
                technique_annotations.push_back(effect->annotations.size());
                effect->annotations.push_back({key, value});
            }
            effect->technique_annotations.push_back(std::move(technique_annotations));

            std::vector<std::vector<std::size_t>> pass_annotations;
            for (const auto& pass : technique.passes) {
                std::vector<std::size_t> indices;
                for (const auto& [key, value] : pass.annotations) {
                    indices.push_back(effect->annotations.size());
                    effect->annotations.push_back({key, value});
                }
                pass_annotations.push_back(std::move(indices));
            }
            effect->pass_annotations.push_back(std::move(pass_annotations));
        }

        const auto variable_semantics = parse_variable_semantics(source);
        const auto variable_types = parse_variable_types(source);
        const auto global_vars = parse_global_variables(source);
        std::size_t next_offset = 0;

        for (const auto& global : global_vars) {
            std::vector<std::size_t> annotation_indices;
            std::string ui_label;
            UIWidgetKind ui_widget = UIWidgetKind::None;
            float ui_min = 0.0f;
            float ui_max = 1.0f;
            float ui_step = 0.01f;
            std::vector<std::string> ui_items;
            std::string ui_group;
            std::string ui_help;
            bool has_ui = false;

            for (const auto& [key, value] : global.annotations) {
                annotation_indices.push_back(effect->annotations.size());
                effect->annotations.push_back({key, value});
                const std::string key_lower = lower(key);
                if (key_lower == "uiname" || key_lower == "name") {
                    ui_label = value;
                    has_ui = true;
                } else if (key_lower == "uiwidget" || key_lower == "widget") {
                    has_ui = true;
                    const std::string widget = lower(value);
                    if (widget == "slider") {
                        ui_widget = UIWidgetKind::Slider;
                    } else if (widget == "drag") {
                        ui_widget = UIWidgetKind::Drag;
                    } else if (widget == "checkbox" || widget == "check" || widget == "bool") {
                        ui_widget = UIWidgetKind::Checkbox;
                    } else if (widget == "color") {
                        ui_widget = UIWidgetKind::Color;
                    } else if (widget == "combo" || widget == "dropdown" || widget == "list") {
                        ui_widget = UIWidgetKind::Combo;
                    } else if (widget == "input" || widget == "edit") {
                        ui_widget = UIWidgetKind::Input;
                    } else {
                        ui_widget = UIWidgetKind::None;
                    }
                } else if (key_lower == "uimin" || key_lower == "min") {
                    try {
                        ui_min = std::stof(value);
                        has_ui = true;
                    } catch (...) {
                    }
                } else if (key_lower == "uimax" || key_lower == "max") {
                    try {
                        ui_max = std::stof(value);
                        has_ui = true;
                    } catch (...) {
                    }
                } else if (key_lower == "uistep" || key_lower == "step") {
                    try {
                        ui_step = std::stof(value);
                    } catch (...) {
                    }
                } else if (key_lower == "uiitems" || key_lower == "items") {
                    has_ui = true;
                    std::stringstream stream(value);
                    std::string item;
                    while (std::getline(stream, item, ',')) {
                        const auto first = item.find_first_not_of(" \t;");
                        if (first != std::string::npos) {
                            const auto last = item.find_last_not_of(" \t;");
                            ui_items.push_back(item.substr(first, last - first + 1));
                        }
                    }
                } else if (key_lower == "uigroup" || key_lower == "group") {
                    ui_group = value;
                } else if (key_lower == "uihelp" || key_lower == "help") {
                    ui_help = value;
                }
            }

            const std::string name_lower = lower(global.name);
            std::string semantic = global.semantic;
            if (semantic.empty()) {
                const auto it = variable_semantics.find(name_lower);
                if (it != variable_semantics.end()) {
                    semantic = it->second;
                }
            }
            std::string hlsl_type;
            bool row_major = false;
            const auto type_it = variable_types.find(name_lower);
            if (type_it != variable_types.end()) {
                hlsl_type = type_it->second.first;
                row_major = type_it->second.second;
            }

            if (has_ui) {
                if (ui_label.empty()) {
                    ui_label = global.name;
                }
                if (ui_widget == UIWidgetKind::None) {
                    if (hlsl_type == "bool") {
                        ui_widget = UIWidgetKind::Checkbox;
                    } else if (hlsl_type == "float") {
                        ui_widget = UIWidgetKind::Slider;
                    } else if (hlsl_type == "float3" || hlsl_type == "float4") {
                        ui_widget = UIWidgetKind::Color;
                    } else if (hlsl_type.rfind("int", 0) == 0) {
                        ui_widget = UIWidgetKind::Input;
                    }
                }
            }

            EffectParameter parameter;
            parameter.name = global.name;
            parameter.semantic = semantic;
            parameter.hlsl_type = hlsl_type;
            parameter.row_major = row_major;
            parameter.offset = next_offset;
            parameter.size = 64;
            parameter.annotation_indices = std::move(annotation_indices);
            if (has_ui) {
                parameter.ui = ParameterUIDesc{ui_label, ui_widget, ui_min, ui_max, ui_step,
                                               std::move(ui_items), ui_group, ui_help,
                                               global.default_val};
            }
            next_offset += 64;
            const std::size_t index = effect->parameters.size();
            effect->parameters.push_back(std::move(parameter));
            effect->param_by_name[name_lower] = index;
            if (!semantic.empty()) {
                effect->param_by_semantic[upper(semantic)] = index;
            }
        }

        for (const auto& passes : effect->compiled_techniques) {
            for (const auto& pass : passes) {
                if (!pass.vs_bytecode) {
                    continue;
                }
                ShaderReflectionData reflected;
                try {
                    reflected = ShaderReflectionData::reflect(*pass.vs_bytecode);
                } catch (...) {
                    continue;
                }
                for (const auto& cbuffer : reflected.cbuffers) {
                    for (const auto& [var_name, var_info] : cbuffer.variables) {
                        const std::string name_lower = lower(var_name);
                        const auto existing = effect->param_by_name.find(name_lower);
                        std::optional<std::string> semantic;
                        const auto semantic_it = variable_semantics.find(name_lower);
                        if (semantic_it != variable_semantics.end()) {
                            semantic = semantic_it->second;
                        }
                        if (existing != effect->param_by_name.end()) {
                            auto& parameter = effect->parameters[existing->second];
                            parameter.offset = var_info.offset;
                            parameter.size = var_info.size;
                            if (parameter.semantic.empty() && semantic) {
                                parameter.semantic = *semantic;
                                effect->param_by_semantic[upper(*semantic)] = existing->second;
                            }
                        } else {
                            EffectParameter parameter;
                            parameter.name = var_name;
                            parameter.semantic = semantic.value_or("");
                            parameter.offset = var_info.offset;
                            parameter.size = var_info.size;
                            const auto type_it = variable_types.find(name_lower);
                            if (type_it != variable_types.end()) {
                                parameter.hlsl_type = type_it->second.first;
                                parameter.row_major = type_it->second.second;
                            }
                            const std::size_t index = effect->parameters.size();
                            effect->parameters.push_back(std::move(parameter));
                            effect->param_by_name[name_lower] = index;
                            if (semantic) {
                                effect->param_by_semantic[upper(*semantic)] = index;
                            }
                        }
                    }
                }
            }
        }

        struct StandardSemantic {
            const char* semantic;
            std::size_t size;
        };
        const StandardSemantic standard_semantics[] = {
            {"WORLD", 64},
            {"VIEW", 64},
            {"PROJECTION", 64},
            {"WORLDVIEW", 64},
            {"VIEWPROJECTION", 64},
            {"WORLDVIEWPROJECTION", 64},
            {"WORLDINVERSE", 64},
            {"VIEWINVERSE", 64},
            {"PROJECTIONINVERSE", 64},
            {"WORLDVIEWPROJECTIONINVERSE", 64},
            {"WORLDTRANSPOSE", 64},
            {"VIEWTRANSPOSE", 64},
            {"PROJECTIONTRANSPOSE", 64},
            {"WORLDVIEWTRANSPOSE", 64},
            {"WORLDVIEWPROJECTIONTRANSPOSE", 64},
            {"CAMERA_POSITION", 16},
            {"LIGHT_DIRECTION", 16},
            {"LIGHT_DIFFUSE", 16},
            {"LIGHT_SPECULAR", 16},
            {"LIGHT_AMBIENT", 16},
            {"DIFFUSE", 16},
            {"AMBIENT", 16},
            {"SPECULAR", 16},
            {"EMISSIVE", 16},
            {"POWER", 4},
            {"TIME", 16},
            {"ELAPSED_TIME", 16},
        };

        for (const auto& entry : standard_semantics) {
            const std::string semantic_upper = upper(entry.semantic);
            if (effect->param_by_semantic.count(semantic_upper) != 0) {
                continue;
            }
            const std::string semantic_lower = lower(entry.semantic);
            const auto existing = effect->param_by_name.find(semantic_lower);
            if (existing != effect->param_by_name.end()) {
                auto& parameter = effect->parameters[existing->second];
                if (parameter.semantic.empty()) {
                    parameter.semantic = entry.semantic;
                    effect->param_by_semantic[semantic_upper] = existing->second;
                }
            } else {
                EffectParameter parameter;
                parameter.name = semantic_lower;
                parameter.semantic = entry.semantic;
                parameter.offset = next_offset;
                parameter.size = entry.size;
                const auto type_it = variable_types.find(semantic_lower);
                if (type_it != variable_types.end()) {
                    parameter.hlsl_type = type_it->second.first;
                    parameter.row_major = type_it->second.second;
                }
                const std::size_t index = effect->parameters.size();
                effect->parameters.push_back(std::move(parameter));
                next_offset += entry.size;
                effect->param_by_name[semantic_lower] = index;
                effect->param_by_semantic[semantic_upper] = index;
            }
        }

        std::size_t max_used = 0;
        for (const auto& parameter : effect->parameters) {
            max_used = std::max(max_used, parameter.offset + parameter.size);
        }
        effect->constant_used = std::min<std::size_t>(((max_used + 15) / 16) * 16, 65536);
        effect->constant_buffer = d3d11.create_constant_buffer(65536);
        effect->constant_data.assign(65536, 0);

        for (const auto& parameter : effect->parameters) {
            if (!parameter.ui || parameter.ui->default_val.empty()) {
                continue;
            }
            if (parameter.offset + parameter.size <= effect->constant_data.size()) {
                if (parse_and_write_default_val(parameter.ui->default_val, parameter.hlsl_type,
                                                effect->constant_data.data() + parameter.offset,
                                                parameter.size)) {
                    effect->constant_dirty = true;
                }
            }
        }

        const auto find = [&](const std::string& semantic,
                              const std::vector<std::string>& alternates) {
            return find_offset(effect->param_by_semantic, effect->param_by_name,
                               effect->parameters, semantic, alternates);
        };

        effect->std_offsets.world = find("WORLD", {"World", "WorldMatrix"});
        effect->std_offsets.view = find("VIEW", {"View", "ViewMatrix"});
        effect->std_offsets.proj = find("PROJECTION", {"Projection", "Proj", "ProjMatrix"});
        effect->std_offsets.world_view = find("WORLDVIEW", {"WorldView", "WorldViewMatrix"});
        effect->std_offsets.world_view_proj =
            find("WORLDVIEWPROJECTION", {"WorldViewProj", "WorldViewProjection", "wvp"});
        effect->std_offsets.camera_pos =
            find("POSITION", {"CameraPosition", "CameraPos"});
        if (!effect->std_offsets.camera_pos) {
            effect->std_offsets.camera_pos = find("CAMERAPOSITION", {"CameraPosition", "CameraPos"});
        }
        effect->std_offsets.light_dir = find("DIRECTION", {"LightDirection", "LightDir"});
        if (!effect->std_offsets.light_dir) {
            effect->std_offsets.light_dir = find("LIGHTDIRECTION", {"LightDirection", "LightDir"});
        }
        effect->std_offsets.mat_diffuse = find("DIFFUSE", {"MaterialDiffuse"});
        effect->std_offsets.mat_ambient = find("AMBIENT", {"MaterialAmbient"});
        effect->std_offsets.mat_specular = find("SPECULAR", {"MaterialSpecular"});

        return effect;
    } catch (...) {
        delete effect;
        throw;
    }
}

std::uint32_t HikariEffect::add_ref() {
    return ref_count.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::uint32_t HikariEffect::release() {
    const std::uint32_t remaining = ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

void HikariEffect::update_gpu_constants() {
    if (!constant_dirty || !constant_buffer) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        const std::size_t upload = std::min(constant_used, constant_data.size());
        std::memcpy(mapped.pData, constant_data.data(), upload);
        context->Unmap(constant_buffer.Get(), 0);
    } else {
        context->UpdateSubresource(constant_buffer.Get(), 0, nullptr, constant_data.data(), 0, 0);
    }
    constant_dirty = false;
}

std::optional<float> HikariEffect::get_param_value_f32(std::size_t index) const {
    if (index >= parameters.size()) {
        return std::nullopt;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 > constant_data.size()) {
        return std::nullopt;
    }
    float value = 0.0f;
    std::memcpy(&value, constant_data.data() + parameter.offset, 4);
    return value;
}

void HikariEffect::set_param_value_f32(std::size_t index, float value) {
    if (index >= parameters.size()) {
        return;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 <= constant_data.size()) {
        std::memcpy(constant_data.data() + parameter.offset, &value, 4);
        constant_dirty = true;
    }
}

std::optional<std::array<float, 3>> HikariEffect::get_param_value_float3(
    std::size_t index) const {
    if (index >= parameters.size()) {
        return std::nullopt;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 12 > constant_data.size()) {
        return std::nullopt;
    }
    std::array<float, 3> value{};
    std::memcpy(value.data(), constant_data.data() + parameter.offset, 12);
    return value;
}

void HikariEffect::set_param_value_float3(std::size_t index,
                                          const std::array<float, 3>& value) {
    if (index >= parameters.size()) {
        return;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 12 <= constant_data.size()) {
        std::memcpy(constant_data.data() + parameter.offset, value.data(), 12);
        constant_dirty = true;
    }
}

std::optional<std::int32_t> HikariEffect::get_param_value_i32(std::size_t index) const {
    if (index >= parameters.size()) {
        return std::nullopt;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 > constant_data.size()) {
        return std::nullopt;
    }
    std::int32_t value = 0;
    std::memcpy(&value, constant_data.data() + parameter.offset, 4);
    return value;
}

void HikariEffect::set_param_value_i32(std::size_t index, std::int32_t value) {
    if (index >= parameters.size()) {
        return;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 <= constant_data.size()) {
        std::memcpy(constant_data.data() + parameter.offset, &value, 4);
        constant_dirty = true;
    }
}

std::optional<bool> HikariEffect::get_param_value_bool(std::size_t index) const {
    if (index >= parameters.size()) {
        return std::nullopt;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 > constant_data.size()) {
        return std::nullopt;
    }
    std::uint32_t bits = 0;
    std::memcpy(&bits, constant_data.data() + parameter.offset, 4);
    float as_float = 0.0f;
    std::memcpy(&as_float, &bits, 4);
    return bits != 0 || as_float > 0.5f;
}

void HikariEffect::set_param_value_bool(std::size_t index, bool value) {
    if (index >= parameters.size()) {
        return;
    }
    const auto& parameter = parameters[index];
    if (parameter.offset + 4 <= constant_data.size()) {
        const std::uint32_t bits = value ? 1u : 0u;
        std::memcpy(constant_data.data() + parameter.offset, &bits, 4);
        constant_dirty = true;
    }
}

std::optional<ActivePassInfo> HikariEffect::get_first_pass_info() {
    update_gpu_constants();
    if (compiled_techniques.empty()) {
        return std::nullopt;
    }
    const std::size_t tech_index = std::min(active_tech_idx, compiled_techniques.size() - 1);
    const auto& technique = compiled_techniques[tech_index];
    if (technique.empty()) {
        return std::nullopt;
    }
    const std::size_t pass_index = std::min(active_pass_idx, technique.size() - 1);
    const auto& pass = technique[pass_index];
    if (!pass.vs_bytecode) {
        return std::nullopt;
    }
    ActivePassInfo info;
    info.vs_bytecode = *pass.vs_bytecode;
    info.vs = pass.vs;
    info.ps = pass.ps;
    info.gs = pass.gs;
    info.constant_buffer = constant_buffer;
    info.blend_preset = pass.blend_preset;
    info.depth_preset = pass.depth_preset;
    info.raster_preset = pass.raster_preset;
    return info;
}

std::optional<std::size_t> HikariEffect::find_technique_for_pass(std::int32_t current_tech) const {
    const char* pass_name = "object";
    switch (current_tech) {
    case 1:
        pass_name = "object";
        break;
    case 2:
        pass_name = "object_ss";
        break;
    case 3:
        pass_name = "shadow";
        break;
    case 4:
        pass_name = "edge";
        break;
    case 5:
        pass_name = "zplot";
        break;
    default:
        break;
    }

    for (std::size_t index = 0; index < techniques.size(); ++index) {
        const auto& technique = techniques[index];
        const auto it = technique.annotations.find("MMDPass");
        if (it != technique.annotations.end()) {
            std::string value = lower(it->second);
            if (value == pass_name && index < compiled_techniques.size() &&
                !compiled_techniques[index].empty()) {
                return index;
            }
        }
    }
    for (std::size_t index = 0; index < techniques.size(); ++index) {
        const std::string name = lower(techniques[index].name);
        if (name == pass_name || name.find(pass_name) != std::string::npos) {
            if (index < compiled_techniques.size() && !compiled_techniques[index].empty()) {
                return index;
            }
        }
    }

    if (current_tech == 2) {
        return find_technique_for_pass(1);
    }
    if (current_tech == 5) {
        return find_technique_for_pass(3);
    }
    if (current_tech == 0 || current_tech == 1) {
        if (active_tech_idx < compiled_techniques.size() &&
            !compiled_techniques[active_tech_idx].empty()) {
            return active_tech_idx;
        }
        for (std::size_t index = 0; index < compiled_techniques.size(); ++index) {
            if (!compiled_techniques[index].empty()) {
                return index;
            }
        }
    }
    return std::nullopt;
}

std::optional<ActivePassInfo> HikariEffect::get_technique_pass_info(std::int32_t current_tech) {
    update_gpu_constants();
    const auto tech_index = find_technique_for_pass(current_tech);
    if (!tech_index || *tech_index >= compiled_techniques.size()) {
        return std::nullopt;
    }
    const auto& technique = compiled_techniques[*tech_index];
    if (technique.empty()) {
        return std::nullopt;
    }
    const std::size_t pass_index = std::min(active_pass_idx, technique.size() - 1);
    const auto& pass = technique[pass_index];
    if (!pass.vs_bytecode) {
        return std::nullopt;
    }
    ActivePassInfo info;
    info.vs_bytecode = *pass.vs_bytecode;
    info.vs = pass.vs;
    info.ps = pass.ps;
    info.gs = pass.gs;
    info.constant_buffer = constant_buffer;
    info.blend_preset = pass.blend_preset;
    info.depth_preset = pass.depth_preset;
    info.raster_preset = pass.raster_preset;
    return info;
}

std::optional<std::size_t> HikariEffect::resolve_parameter_index(D3DXHANDLE handle) const {
    const auto value = reinterpret_cast<std::size_t>(handle);
    if (value >= HANDLE_PARAM_BASE && value < HANDLE_ANNO_BASE) {
        const std::size_t index = value - HANDLE_PARAM_BASE;
        if (index < parameters.size()) {
            return index;
        }
    } else if (value > 0 && value <= parameters.size()) {
        return value - 1;
    }
    return std::nullopt;
}

HRESULT HikariEffect::set_param_block(D3DXHANDLE handle, const void* source, std::size_t bytes) {
    if (source == nullptr || bytes == 0) {
        return S_OK;
    }
    const auto index = resolve_parameter_index(handle);
    if (!index) {
        return S_OK;
    }
    const auto& parameter = parameters[*index];
    const std::size_t copy_len = std::min(bytes, parameter.size);
    if (parameter.offset + copy_len <= constant_data.size()) {
        std::memcpy(constant_data.data() + parameter.offset, source, copy_len);
        constant_dirty = true;
    }
    return S_OK;
}

HRESULT HikariEffect::get_param_block(D3DXHANDLE handle, void* destination,
                                      std::size_t bytes) const {
    if (destination == nullptr || bytes == 0) {
        return E_POINTER;
    }
    const auto index = resolve_parameter_index(handle);
    if (!index) {
        return E_FAIL;
    }
    const auto& parameter = parameters[*index];
    const std::size_t copy_len = std::min(bytes, parameter.size);
    if (parameter.offset + copy_len <= constant_data.size()) {
        std::memcpy(destination, constant_data.data() + parameter.offset, copy_len);
        return S_OK;
    }
    return E_FAIL;
}

std::pair<std::uint32_t, std::array<std::uint32_t, 4>> HikariEffect::parameter_type_shape(
    const EffectParameter& parameter) const {
    const std::string type = lower(parameter.hlsl_type);
    if (type.empty()) {
        if (parameter.size >= 64) {
            return {3, {4, 4, 0, 0}};
        }
        if (parameter.size >= 16) {
            return {1, {3, 1, 4, 0}};
        }
        return {0, {3, 1, 1, 0}};
    }
    if (type.rfind("texture", 0) == 0) {
        const std::uint32_t texture_type =
            type.find("cube") != std::string::npos ? 9
            : (type.find("3d") != std::string::npos ? 8 : 7);
        return {4, {texture_type, 0, 0, 0}};
    }
    std::string base = type;
    while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) {
        base.pop_back();
    }
    std::uint32_t scalar_type = 3;
    if (base == "bool") {
        scalar_type = 1;
    } else if (base == "int" || base == "uint") {
        scalar_type = 2;
    }
    const auto x = type.find('x');
    if (x != std::string::npos) {
        const auto parse_digits = [](const std::string& text, std::uint32_t fallback) {
            std::string digits;
            for (char c : text) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    digits.push_back(c);
                }
            }
            if (digits.empty()) {
                return fallback;
            }
            try {
                return static_cast<std::uint32_t>(std::stoul(digits));
            } catch (...) {
                return fallback;
            }
        };
        const std::uint32_t rows = parse_digits(type.substr(0, x), 4);
        const std::uint32_t columns = parse_digits(type.substr(x + 1), 4);
        return {parameter.row_major ? 2u : 3u, {scalar_type, rows, columns, 0}};
    }
    std::uint32_t columns = 1;
    if (!type.empty() && std::isdigit(static_cast<unsigned char>(type.back()))) {
        columns = static_cast<std::uint32_t>(type.back() - '0');
    } else if (parameter.size >= 16) {
        columns = 4;
    }
    if (columns > 1) {
        return {1, {scalar_type, 1, columns, 0}};
    }
    return {0, {scalar_type, 1, 1, 0}};
}

bool is_hikari_effect(void* self) {
    if (self == nullptr) {
        return false;
    }
    const auto* effect = static_cast<const HikariEffect*>(self);
    return effect->vtbl != nullptr;
}

std::string disassemble_hikari_effect(void* self) {
    if (!is_hikari_effect(self)) {
        throw std::runtime_error("not a Hikari effect");
    }
    const auto* effect = static_cast<const HikariEffect*>(self);
    std::ostringstream output;
    output << "//listing of all techniques and passes with embedded asm listings\n\n";
    for (std::size_t tech = 0; tech < effect->compiled_techniques.size(); ++tech) {
        output << "technique " << effect->techniques[tech].name << "\n{\n";
        const auto& passes = effect->compiled_techniques[tech];
        for (std::size_t pass_index = 0; pass_index < passes.size(); ++pass_index) {
            output << "    pass " << effect->techniques[tech].passes[pass_index].name << "\n    {\n";
            const auto& pass = passes[pass_index];
            const std::pair<const char*, const std::optional<std::vector<std::uint8_t>>*> stages[] = {
                {"vertexshader", &pass.vs_bytecode},   {"pixelshader", &pass.ps_bytecode},
                {"geometryshader", &pass.gs_bytecode}, {"hullshader", &pass.hs_bytecode},
                {"domainshader", &pass.ds_bytecode},   {"computeshader", &pass.cs_bytecode},
            };
            for (const auto& [stage_name, bytecode] : stages) {
                if (!*bytecode) {
                    continue;
                }
                const auto assembly = disassemble_dxbc(**bytecode);
                output << "        " << stage_name << " =\n            asm {\n";
                std::string text(assembly.begin(), assembly.end());
                std::stringstream lines(text);
                std::string line;
                while (std::getline(lines, line)) {
                    output << "                " << line << '\n';
                }
                output << "            };\n\n";
            }
            output << "    }\n\n";
        }
        output << "}\n\n";
    }
    return output.str();
}

} // namespace hikari::core
