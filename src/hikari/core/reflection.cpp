#include "hikari/core/reflection.hpp"

#include <windows.h>

#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "hikari/core/log.hpp"

namespace hikari::core {
namespace {

using PfnD3DReflect = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);

PfnD3DReflect get_d3dreflect() {
    static PfnD3DReflect fn = [] {
        HMODULE module = ::LoadLibraryW(L"d3dcompiler_47.dll");
        if (module == nullptr) {
            module = ::LoadLibraryW(L"d3dcompiler_43.dll");
        }
        return module != nullptr
                   ? reinterpret_cast<PfnD3DReflect>(::GetProcAddress(module, "D3DReflect"))
                   : nullptr;
    }();
    return fn;
}

bool equals_ignore_case(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

ShaderReflectionData ShaderReflectionData::reflect(const std::vector<std::uint8_t>& bytecode) {
    const PfnD3DReflect reflect_fn = get_d3dreflect();
    if (reflect_fn == nullptr) {
        throw std::runtime_error("d3dcompiler_47.dll not found");
    }

    ID3D11ShaderReflection* reflector = nullptr;
    const HRESULT hr = reflect_fn(bytecode.data(), bytecode.size(),
                                  IID_ID3D11ShaderReflection,
                                  reinterpret_cast<void**>(&reflector));
    if (FAILED(hr) || reflector == nullptr) {
        throw std::runtime_error("D3DReflect failed");
    }

    ShaderReflectionData out;
    D3D11_SHADER_DESC shader_desc{};
    if (SUCCEEDED(reflector->GetDesc(&shader_desc))) {
        if ((shader_desc.Version >> 16) == 5) {
            UINT x = 0, y = 0, z = 0;
            reflector->GetThreadGroupSize(&x, &y, &z);
            out.thread_group_size = {x, y, z};
        }

        std::unordered_map<std::string, std::uint32_t> cb_slot_map;
        for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflector->GetResourceBindingDesc(i, &bind))) {
                continue;
            }
            const std::string name = bind.Name != nullptr ? bind.Name : "res_" + std::to_string(i);
            ReflectedResource resource;
            resource.name = name;
            resource.bind_point = bind.BindPoint;
            resource.bind_count = bind.BindCount;
            resource.return_type = static_cast<std::uint32_t>(bind.ReturnType);
            resource.dimension = static_cast<std::uint32_t>(bind.Dimension);
            switch (bind.Type) {
            case D3D_SIT_CBUFFER:
            case D3D_SIT_TBUFFER:
                resource.type = ReflectedResourceType::CBuffer;
                cb_slot_map[name] = bind.BindPoint;
                break;
            case D3D_SIT_TEXTURE:
                resource.type = ReflectedResourceType::Texture;
                break;
            case D3D_SIT_SAMPLER:
                resource.type = ReflectedResourceType::Sampler;
                break;
            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                resource.type = ReflectedResourceType::Uav;
                break;
            case D3D_SIT_STRUCTURED:
                resource.type = ReflectedResourceType::StructuredBuffer;
                break;
            case D3D_SIT_BYTEADDRESS:
                resource.type = ReflectedResourceType::ByteAddressBuffer;
                break;
            default:
                resource.type = ReflectedResourceType::Other;
                break;
            }
            out.resources.push_back(std::move(resource));
        }

        for (UINT i = 0; i < shader_desc.ConstantBuffers; ++i) {
            ID3D11ShaderReflectionConstantBuffer* buffer = reflector->GetConstantBufferByIndex(i);
            if (buffer == nullptr) {
                continue;
            }
            D3D11_SHADER_BUFFER_DESC buffer_desc{};
            if (FAILED(buffer->GetDesc(&buffer_desc))) {
                continue;
            }
            ReflectedCBuffer cbuffer;
            cbuffer.name = buffer_desc.Name != nullptr ? buffer_desc.Name : "cbuffer_" + std::to_string(i);
            const auto slot_it = cb_slot_map.find(cbuffer.name);
            cbuffer.slot = slot_it != cb_slot_map.end() ? slot_it->second : i;
            cbuffer.size = buffer_desc.Size;
            for (UINT v = 0; v < buffer_desc.Variables; ++v) {
                ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
                if (variable == nullptr) {
                    continue;
                }
                D3D11_SHADER_VARIABLE_DESC variable_desc{};
                if (FAILED(variable->GetDesc(&variable_desc))) {
                    continue;
                }
                const std::string name =
                    variable_desc.Name != nullptr ? variable_desc.Name : "var_" + std::to_string(v);
                cbuffer.variables[name] = {name, variable_desc.StartOffset, variable_desc.Size};
            }
            out.cbuffers.push_back(std::move(cbuffer));
        }

        for (UINT i = 0; i < shader_desc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflector->GetInputParameterDesc(i, &param))) {
                continue;
            }
            ReflectedInputParam input;
            input.semantic_name =
                param.SemanticName != nullptr ? param.SemanticName : "PARAM_" + std::to_string(i);
            input.semantic_index = param.SemanticIndex;
            input.register_index = param.Register;
            input.component_type = static_cast<std::uint32_t>(param.ComponentType);
            input.mask = param.Mask;
            out.input_parameters.push_back(std::move(input));
        }
    }

    reflector->Release();
    log_msg("D3DReflect complete: " + std::to_string(out.cbuffers.size()) + " constant buffers, " +
            std::to_string(out.resources.size()) + " resources");
    return out;
}

std::optional<std::uint32_t> ShaderReflectionData::find_texture_slot(const std::string& name) const {
    for (const auto& resource : resources) {
        if (resource.type == ReflectedResourceType::Texture && equals_ignore_case(resource.name, name)) {
            return resource.bind_point;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ShaderReflectionData::find_sampler_slot(const std::string& name) const {
    for (const auto& resource : resources) {
        if (resource.type == ReflectedResourceType::Sampler && equals_ignore_case(resource.name, name)) {
            return resource.bind_point;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ShaderReflectionData::find_uav_slot(const std::string& name) const {
    for (const auto& resource : resources) {
        if (resource.type == ReflectedResourceType::Uav && equals_ignore_case(resource.name, name)) {
            return resource.bind_point;
        }
    }
    return std::nullopt;
}

} // namespace hikari::core
