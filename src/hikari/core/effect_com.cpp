#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "hikari/core/backend.hpp"
#include "hikari/core/com.hpp"
#include "hikari/core/effect.hpp"
#include "hikari/core/log.hpp"

namespace hikari::core {
namespace {

std::atomic_bool g_wvp_logged = false;
std::atomic_uint64_t g_begin_calls = 0;
std::atomic_uint64_t g_begin_pass_calls = 0;

HikariEffect* effect(void* self) {
    return static_cast<HikariEffect*>(self);
}

std::string cstr(const char* value) {
    return value != nullptr ? std::string(value) : std::string();
}

HRESULT set_matrix_transposed(void* self, D3DXHANDLE handle, const D3DXMATRIX* matrix) {
    if (matrix == nullptr) {
        return S_OK;
    }
    D3DXMATRIX transposed{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            transposed.m[row][col] = matrix->m[col][row];
        }
    }
    return effect(self)->set_param_block(handle, &transposed, sizeof(transposed));
}

HRESULT STDMETHODCALLTYPE query_interface(void* self, const IID*, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    *out = self;
    effect(self)->add_ref();
    return S_OK;
}

ULONG STDMETHODCALLTYPE add_ref(void* self) {
    return effect(self)->add_ref();
}

ULONG STDMETHODCALLTYPE release(void* self) {
    return effect(self)->release();
}

HRESULT STDMETHODCALLTYPE get_desc(void* self, D3DXEFFECT_DESC* desc) {
    if (desc == nullptr) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    desc->Creator = "Hikari DX11 Engine";
    desc->Parameters = static_cast<std::uint32_t>(eff->parameters.size());
    desc->Techniques = static_cast<std::uint32_t>(eff->techniques.size());
    desc->Functions = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_parameter_desc(void* self, D3DXHANDLE handle,
                                             D3DXPARAMETER_DESC* desc) {
    if (desc == nullptr) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(handle);
    if (value >= HANDLE_ANNO_BASE) {
        const std::size_t index = value - HANDLE_ANNO_BASE;
        if (index < eff->annotations.size()) {
            const auto& annotation = eff->annotations[index];
            desc->Name = annotation.name.c_str();
            desc->Semantic = nullptr;
            desc->Class = 0;
            desc->Type = 4;
            desc->Rows = 1;
            desc->Columns = 1;
            desc->Elements = 0;
            desc->Annotations = 0;
            desc->StructMembers = 0;
            desc->Flags = 0;
            desc->Bytes = static_cast<std::uint32_t>(annotation.value.size());
            return S_OK;
        }
    }

    const auto index = eff->resolve_parameter_index(handle);
    if (!index) {
        return E_FAIL;
    }
    const auto& parameter = eff->parameters[*index];
    const auto [parameter_class, shape] = eff->parameter_type_shape(parameter);
    desc->Name = parameter.name.c_str();
    desc->Semantic = parameter.semantic.c_str();
    desc->Class = parameter_class;
    desc->Type = shape[0];
    desc->Rows = shape[1];
    desc->Columns = shape[2];
    desc->Elements = 0;
    desc->Annotations = static_cast<std::uint32_t>(parameter.annotation_indices.size());
    desc->StructMembers = 0;
    desc->Flags = 0;
    desc->Bytes = parameter_class == 4 ? 0 : static_cast<std::uint32_t>(parameter.size);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_technique_desc(void* self, D3DXHANDLE handle,
                                             D3DXTECHNIQUE_DESC* desc) {
    if (desc == nullptr) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(handle);
    std::size_t index = 0;
    if (value >= HANDLE_TECHNIQUE_BASE && value < HANDLE_PASS_BASE) {
        index = value - HANDLE_TECHNIQUE_BASE;
    } else if (value > 0 && value <= eff->techniques.size()) {
        index = value - 1;
    } else {
        return E_FAIL;
    }
    if (index >= eff->techniques.size()) {
        return E_FAIL;
    }
    desc->Name = eff->techniques[index].name.c_str();
    desc->Passes = static_cast<std::uint32_t>(eff->techniques[index].passes.size());
    desc->Annotations = static_cast<std::uint32_t>(eff->technique_annotations[index].size());
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_pass_desc(void* self, D3DXHANDLE handle, D3DXPASS_DESC* desc) {
    if (desc == nullptr) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(handle);
    std::size_t tech_index = eff->active_tech_idx;
    std::size_t pass_index = 0;
    if (value >= HANDLE_PASS_BASE && value < HANDLE_PARAM_BASE) {
        const std::size_t packed = value - HANDLE_PASS_BASE;
        tech_index = packed >> 8;
        pass_index = packed & 0xFF;
    } else if (value > 0) {
        pass_index = value - 1;
    } else {
        return E_FAIL;
    }
    if (tech_index >= eff->techniques.size() ||
        pass_index >= eff->techniques[tech_index].passes.size()) {
        return E_FAIL;
    }
    desc->Name = eff->techniques[tech_index].passes[pass_index].name.c_str();
    desc->Annotations =
        static_cast<std::uint32_t>(eff->pass_annotations[tech_index][pass_index].size());
    desc->pVertexShaderFunction = nullptr;
    desc->pPixelShaderFunction = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_function_desc(void*, D3DXHANDLE, void*) {
    return E_NOTIMPL;
}

D3DXHANDLE STDMETHODCALLTYPE get_parameter(void* self, D3DXHANDLE, std::uint32_t index) {
    auto* eff = effect(self);
    return index < eff->parameters.size()
               ? reinterpret_cast<D3DXHANDLE>(HANDLE_PARAM_BASE + index)
               : nullptr;
}

D3DXHANDLE STDMETHODCALLTYPE get_parameter_by_name(void* self, D3DXHANDLE, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    auto* eff = effect(self);
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = eff->param_by_name.find(key);
    return it == eff->param_by_name.end()
               ? nullptr
               : reinterpret_cast<D3DXHANDLE>(HANDLE_PARAM_BASE + it->second);
}

D3DXHANDLE STDMETHODCALLTYPE get_parameter_by_semantic(void* self, D3DXHANDLE,
                                                       const char* semantic) {
    if (semantic == nullptr) {
        return nullptr;
    }
    auto* eff = effect(self);
    std::string key = semantic;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto semantic_it = eff->param_by_semantic.find(key);
    if (semantic_it != eff->param_by_semantic.end()) {
        return reinterpret_cast<D3DXHANDLE>(HANDLE_PARAM_BASE + semantic_it->second);
    }
    std::string lower_key = semantic;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto name_it = eff->param_by_name.find(lower_key);
    return name_it == eff->param_by_name.end()
               ? nullptr
               : reinterpret_cast<D3DXHANDLE>(HANDLE_PARAM_BASE + name_it->second);
}

D3DXHANDLE STDMETHODCALLTYPE get_parameter_element(void*, D3DXHANDLE, std::uint32_t) {
    return nullptr;
}

D3DXHANDLE STDMETHODCALLTYPE get_technique(void* self, std::uint32_t index) {
    auto* eff = effect(self);
    return index < eff->techniques.size()
               ? reinterpret_cast<D3DXHANDLE>(HANDLE_TECHNIQUE_BASE + index)
               : nullptr;
}

D3DXHANDLE STDMETHODCALLTYPE get_technique_by_name(void* self, const char* name) {
    auto* eff = effect(self);
    if (name != nullptr) {
        const std::string key = name;
        for (std::size_t i = 0; i < eff->techniques.size(); ++i) {
            const auto& candidate = eff->techniques[i].name;
            if (candidate.size() == key.size() &&
                std::equal(candidate.begin(), candidate.end(), key.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                })) {
                return reinterpret_cast<D3DXHANDLE>(HANDLE_TECHNIQUE_BASE + i);
            }
        }
    }
    return eff->techniques.empty() ? nullptr
                                   : reinterpret_cast<D3DXHANDLE>(HANDLE_TECHNIQUE_BASE);
}

D3DXHANDLE STDMETHODCALLTYPE get_pass(void* self, D3DXHANDLE technique, std::uint32_t index) {
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(technique);
    const std::size_t tech_index =
        value >= HANDLE_TECHNIQUE_BASE && value < HANDLE_PASS_BASE
            ? value - HANDLE_TECHNIQUE_BASE
            : eff->active_tech_idx;
    return reinterpret_cast<D3DXHANDLE>(HANDLE_PASS_BASE + ((tech_index << 8) | index));
}

D3DXHANDLE STDMETHODCALLTYPE get_pass_by_name(void* self, D3DXHANDLE technique,
                                              const char* name) {
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(technique);
    const std::size_t tech_index =
        value >= HANDLE_TECHNIQUE_BASE && value < HANDLE_PASS_BASE
            ? value - HANDLE_TECHNIQUE_BASE
            : eff->active_tech_idx;
    if (name != nullptr && tech_index < eff->techniques.size()) {
        const std::string key = name;
        const auto& passes = eff->techniques[tech_index].passes;
        for (std::size_t i = 0; i < passes.size(); ++i) {
            const auto& candidate = passes[i].name;
            if (candidate.size() == key.size() &&
                std::equal(candidate.begin(), candidate.end(), key.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                })) {
                return reinterpret_cast<D3DXHANDLE>(HANDLE_PASS_BASE + ((tech_index << 8) | i));
            }
        }
    }
    return reinterpret_cast<D3DXHANDLE>(HANDLE_PASS_BASE + (tech_index << 8));
}

D3DXHANDLE STDMETHODCALLTYPE get_function(void*, std::uint32_t) {
    return nullptr;
}
D3DXHANDLE STDMETHODCALLTYPE get_function_by_name(void*, const char*) {
    return nullptr;
}

std::vector<std::size_t> annotation_list(HikariEffect* eff, D3DXHANDLE handle) {
    const auto value = reinterpret_cast<std::size_t>(handle);
    if (value >= HANDLE_PARAM_BASE && value < HANDLE_ANNO_BASE) {
        const std::size_t index = value - HANDLE_PARAM_BASE;
        if (index < eff->parameters.size()) {
            return eff->parameters[index].annotation_indices;
        }
    } else if (value >= HANDLE_TECHNIQUE_BASE && value < HANDLE_PASS_BASE) {
        const std::size_t index = value - HANDLE_TECHNIQUE_BASE;
        if (index < eff->technique_annotations.size()) {
            return eff->technique_annotations[index];
        }
    } else if (value >= HANDLE_PASS_BASE && value < HANDLE_PARAM_BASE) {
        const std::size_t packed = value - HANDLE_PASS_BASE;
        const std::size_t tech = packed >> 8;
        const std::size_t pass = packed & 0xFF;
        if (tech < eff->pass_annotations.size() && pass < eff->pass_annotations[tech].size()) {
            return eff->pass_annotations[tech][pass];
        }
    } else if (value > 0 && value <= eff->parameters.size()) {
        return eff->parameters[value - 1].annotation_indices;
    }
    return {};
}

D3DXHANDLE STDMETHODCALLTYPE get_annotation(void* self, D3DXHANDLE handle,
                                            std::uint32_t index) {
    auto* eff = effect(self);
    const auto list = annotation_list(eff, handle);
    return index < list.size()
               ? reinterpret_cast<D3DXHANDLE>(HANDLE_ANNO_BASE + list[index])
               : nullptr;
}

D3DXHANDLE STDMETHODCALLTYPE get_annotation_by_name(void* self, D3DXHANDLE handle,
                                                    const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    auto* eff = effect(self);
    const auto list = annotation_list(eff, handle);
    const std::string key = name;
    for (std::size_t index : list) {
        if (index >= eff->annotations.size()) {
            continue;
        }
        const auto& candidate = eff->annotations[index].name;
        if (candidate.size() == key.size() &&
            std::equal(candidate.begin(), candidate.end(), key.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            })) {
            return reinterpret_cast<D3DXHANDLE>(HANDLE_ANNO_BASE + index);
        }
    }
    return nullptr;
}

HRESULT STDMETHODCALLTYPE set_value(void* self, D3DXHANDLE handle, const void* data,
                                    std::uint32_t bytes) {
    return effect(self)->set_param_block(handle, data, bytes);
}

HRESULT STDMETHODCALLTYPE get_value(void* self, D3DXHANDLE handle, void* data,
                                    std::uint32_t bytes) {
    return effect(self)->get_param_block(handle, data, bytes);
}

HRESULT STDMETHODCALLTYPE set_bool(void* self, D3DXHANDLE handle, BOOL value) {
    const std::uint32_t bits = value ? 1u : 0u;
    return effect(self)->set_param_block(handle, &bits, 4);
}

HRESULT STDMETHODCALLTYPE get_bool(void* self, D3DXHANDLE handle, BOOL* out) {
    std::int32_t value = 0;
    const HRESULT hr = effect(self)->get_param_block(handle, &value, 4);
    if (SUCCEEDED(hr) && out != nullptr) {
        *out = value != 0 ? TRUE : FALSE;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE set_bool_array(void* self, D3DXHANDLE handle, const BOOL* data,
                                         std::uint32_t count) {
    if (data == nullptr || count == 0) {
        return S_OK;
    }
    std::vector<std::int32_t> values;
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        values.push_back(data[i] ? 1 : 0);
    }
    return effect(self)->set_param_block(handle, values.data(), values.size() * 4);
}

HRESULT STDMETHODCALLTYPE get_bool_array(void* self, D3DXHANDLE handle, BOOL* data,
                                         std::uint32_t count) {
    return effect(self)->get_param_block(handle, data, static_cast<std::size_t>(count) * 4);
}

HRESULT STDMETHODCALLTYPE set_int(void* self, D3DXHANDLE handle, int value) {
    return effect(self)->set_param_block(handle, &value, 4);
}
HRESULT STDMETHODCALLTYPE get_int(void* self, D3DXHANDLE handle, int* out) {
    return effect(self)->get_param_block(handle, out, 4);
}
HRESULT STDMETHODCALLTYPE set_int_array(void* self, D3DXHANDLE handle, const int* data,
                                        std::uint32_t count) {
    return effect(self)->set_param_block(handle, data, static_cast<std::size_t>(count) * 4);
}
HRESULT STDMETHODCALLTYPE get_int_array(void* self, D3DXHANDLE handle, int* data,
                                        std::uint32_t count) {
    return effect(self)->get_param_block(handle, data, static_cast<std::size_t>(count) * 4);
}

HRESULT STDMETHODCALLTYPE set_float(void* self, D3DXHANDLE handle, float value) {
    return effect(self)->set_param_block(handle, &value, 4);
}
HRESULT STDMETHODCALLTYPE get_float(void* self, D3DXHANDLE handle, float* out) {
    return effect(self)->get_param_block(handle, out, 4);
}
HRESULT STDMETHODCALLTYPE set_float_array(void* self, D3DXHANDLE handle, const float* data,
                                          std::uint32_t count) {
    return effect(self)->set_param_block(handle, data, static_cast<std::size_t>(count) * 4);
}
HRESULT STDMETHODCALLTYPE get_float_array(void* self, D3DXHANDLE handle, float* data,
                                          std::uint32_t count) {
    return effect(self)->get_param_block(handle, data, static_cast<std::size_t>(count) * 4);
}

HRESULT STDMETHODCALLTYPE set_vector(void* self, D3DXHANDLE handle, const D3DXVECTOR4* value) {
    return effect(self)->set_param_block(handle, value, sizeof(D3DXVECTOR4));
}
HRESULT STDMETHODCALLTYPE get_vector(void* self, D3DXHANDLE handle, D3DXVECTOR4* out) {
    return effect(self)->get_param_block(handle, out, sizeof(D3DXVECTOR4));
}
HRESULT STDMETHODCALLTYPE set_vector_array(void* self, D3DXHANDLE handle,
                                           const D3DXVECTOR4* data, std::uint32_t count) {
    return effect(self)->set_param_block(handle, data,
                                         static_cast<std::size_t>(count) * sizeof(D3DXVECTOR4));
}
HRESULT STDMETHODCALLTYPE get_vector_array(void* self, D3DXHANDLE handle, D3DXVECTOR4* data,
                                           std::uint32_t count) {
    return effect(self)->get_param_block(handle, data,
                                         static_cast<std::size_t>(count) * sizeof(D3DXVECTOR4));
}

HRESULT STDMETHODCALLTYPE set_matrix(void* self, D3DXHANDLE handle, const D3DXMATRIX* value) {
    return effect(self)->set_param_block(handle, value, sizeof(D3DXMATRIX));
}
HRESULT STDMETHODCALLTYPE get_matrix(void* self, D3DXHANDLE handle, D3DXMATRIX* out) {
    return effect(self)->get_param_block(handle, out, sizeof(D3DXMATRIX));
}
HRESULT STDMETHODCALLTYPE set_matrix_array(void* self, D3DXHANDLE handle,
                                           const D3DXMATRIX* data, std::uint32_t count) {
    return effect(self)->set_param_block(handle, data,
                                         static_cast<std::size_t>(count) * sizeof(D3DXMATRIX));
}
HRESULT STDMETHODCALLTYPE get_matrix_array(void* self, D3DXHANDLE handle, D3DXMATRIX* data,
                                           std::uint32_t count) {
    return effect(self)->get_param_block(handle, data,
                                         static_cast<std::size_t>(count) * sizeof(D3DXMATRIX));
}

HRESULT STDMETHODCALLTYPE set_matrix_pointer_array(void* self, D3DXHANDLE handle,
                                                   const D3DXMATRIX* const* data,
                                                   std::uint32_t count) {
    if (data == nullptr || count == 0) {
        return S_OK;
    }
    auto* eff = effect(self);
    const auto index = eff->resolve_parameter_index(handle);
    if (!index) {
        return S_OK;
    }
    auto& parameter = eff->parameters[*index];
    const std::size_t slots = std::min<std::size_t>(count, parameter.size / 64);
    for (std::size_t i = 0; i < slots; ++i) {
        if (data[i] == nullptr) {
            continue;
        }
        if (parameter.offset + (i + 1) * 64 <= eff->constant_data.size()) {
            std::memcpy(eff->constant_data.data() + parameter.offset + i * 64, data[i], 64);
        }
    }
    eff->constant_dirty = true;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_matrix_pointer_array(void* self, D3DXHANDLE handle,
                                                   D3DXMATRIX** data, std::uint32_t count) {
    if (data == nullptr || count == 0) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    const auto index = eff->resolve_parameter_index(handle);
    if (!index) {
        return E_FAIL;
    }
    const auto& parameter = eff->parameters[*index];
    const std::size_t slots = std::min<std::size_t>(count, parameter.size / 64);
    for (std::size_t i = 0; i < slots; ++i) {
        if (data[i] == nullptr) {
            continue;
        }
        if (parameter.offset + (i + 1) * 64 <= eff->constant_data.size()) {
            std::memcpy(data[i], eff->constant_data.data() + parameter.offset + i * 64, 64);
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE set_matrix_transpose(void* self, D3DXHANDLE handle,
                                               const D3DXMATRIX* value) {
    return set_matrix_transposed(self, handle, value);
}
HRESULT STDMETHODCALLTYPE get_matrix_transpose(void* self, D3DXHANDLE handle,
                                               D3DXMATRIX* out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    const HRESULT hr = effect(self)->get_param_block(handle, out, sizeof(D3DXMATRIX));
    if (SUCCEEDED(hr)) {
        D3DXMATRIX copy = *out;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                out->m[row][col] = copy.m[col][row];
            }
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE set_matrix_transpose_array(void* self, D3DXHANDLE handle,
                                                     const D3DXMATRIX* data,
                                                     std::uint32_t count) {
    if (data == nullptr || count == 0) {
        return S_OK;
    }
    auto* eff = effect(self);
    const auto index = eff->resolve_parameter_index(handle);
    if (!index) {
        return S_OK;
    }
    auto& parameter = eff->parameters[*index];
    const std::size_t slots = std::min<std::size_t>(count, parameter.size / 64);
    for (std::size_t i = 0; i < slots; ++i) {
        D3DXMATRIX transposed{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                transposed.m[row][col] = data[i].m[col][row];
            }
        }
        if (parameter.offset + (i + 1) * 64 <= eff->constant_data.size()) {
            std::memcpy(eff->constant_data.data() + parameter.offset + i * 64, &transposed, 64);
        }
    }
    eff->constant_dirty = true;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE get_matrix_transpose_array(void* self, D3DXHANDLE handle,
                                                     D3DXMATRIX* data, std::uint32_t count) {
    const HRESULT hr = get_matrix_array(self, handle, data, count);
    if (SUCCEEDED(hr) && data != nullptr) {
        for (std::uint32_t i = 0; i < count; ++i) {
            D3DXMATRIX copy = data[i];
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    data[i].m[row][col] = copy.m[col][row];
                }
            }
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE set_matrix_transpose_pointer_array(void* self, D3DXHANDLE handle,
                                                             const D3DXMATRIX* const* data,
                                                             std::uint32_t count) {
    return set_matrix_pointer_array(self, handle, data, count);
}
HRESULT STDMETHODCALLTYPE get_matrix_transpose_pointer_array(void* self, D3DXHANDLE handle,
                                                             D3DXMATRIX** data,
                                                             std::uint32_t count) {
    const HRESULT hr = get_matrix_pointer_array(self, handle, data, count);
    if (SUCCEEDED(hr) && data != nullptr) {
        for (std::uint32_t i = 0; i < count; ++i) {
            if (data[i] == nullptr) {
                continue;
            }
            D3DXMATRIX copy = *data[i];
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    data[i]->m[row][col] = copy.m[col][row];
                }
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE set_string(void* self, D3DXHANDLE handle, const char* value) {
    auto* eff = effect(self);
    const auto index = eff->resolve_parameter_index(handle);
    if (index) {
        eff->string_values[*index] = cstr(value);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_string(void* self, D3DXHANDLE handle, const char** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(handle);
    if (value >= HANDLE_ANNO_BASE) {
        const std::size_t index = value - HANDLE_ANNO_BASE;
        if (index < eff->annotations.size()) {
            *out = eff->annotations[index].value.c_str();
            return S_OK;
        }
    }
    const auto index = eff->resolve_parameter_index(handle);
    if (index) {
        *out = eff->string_values[*index].c_str();
        return S_OK;
    }
    return E_FAIL;
}

HRESULT STDMETHODCALLTYPE set_texture(void* self, D3DXHANDLE handle, void* texture) {
    auto* eff = effect(self);
    const auto index = eff->resolve_parameter_index(handle);
    if (!index) {
        return S_OK;
    }
    std::size_t slot = *index;
    std::string parameter_name = eff->parameters[*index].name;
    std::transform(parameter_name.begin(), parameter_name.end(), parameter_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = eff->name_to_texture_slot.find(parameter_name);
    if (it != eff->name_to_texture_slot.end()) {
        slot = it->second;
    }
    slot = std::min<std::size_t>(slot, 15);
    if (texture == nullptr) {
        eff->bound_texture_srvs[slot].Reset();
        g_bound_textures[std::min<std::size_t>(slot, 7)].store(0);
        return S_OK;
    }
    auto* unknown = static_cast<IUnknown*>(texture);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(srv.GetAddressOf())))) {
        eff->bound_texture_srvs[slot] = srv;
    }
    g_bound_textures[std::min<std::size_t>(slot, 7)].store(reinterpret_cast<std::size_t>(texture));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_texture(void*, D3DXHANDLE, void**) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE get_pixel_shader(void*, D3DXHANDLE, void**) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE get_vertex_shader(void*, D3DXHANDLE, void**) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE set_array_range(void*, D3DXHANDLE, std::uint32_t, std::uint32_t) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE get_pool(void*, void**) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE set_technique(void* self, D3DXHANDLE handle) {
    auto* eff = effect(self);
    const auto value = reinterpret_cast<std::size_t>(handle);
    std::size_t index = 0;
    if (value >= HANDLE_TECHNIQUE_BASE && value < HANDLE_PASS_BASE) {
        index = value - HANDLE_TECHNIQUE_BASE;
    } else if (value > 0) {
        index = value - 1;
    }
    if (index < eff->techniques.size()) {
        eff->active_tech_idx = index;
    }
    return S_OK;
}

D3DXHANDLE STDMETHODCALLTYPE get_current_technique(void* self) {
    return reinterpret_cast<D3DXHANDLE>(HANDLE_TECHNIQUE_BASE + effect(self)->active_tech_idx);
}

HRESULT STDMETHODCALLTYPE validate_technique(void*, D3DXHANDLE) {
    return S_OK;
}
HRESULT STDMETHODCALLTYPE find_next_valid_technique(void*, D3DXHANDLE, D3DXHANDLE* out) {
    if (out != nullptr) {
        *out = reinterpret_cast<D3DXHANDLE>(HANDLE_TECHNIQUE_BASE);
    }
    return S_OK;
}
BOOL STDMETHODCALLTYPE is_parameter_used(void*, D3DXHANDLE, D3DXHANDLE) {
    return TRUE;
}

HRESULT STDMETHODCALLTYPE begin(void* self, std::uint32_t* passes, std::uint32_t) {
    const std::uint64_t call = g_begin_calls.fetch_add(1) + 1;
    auto* eff = effect(self);
    if (call <= 5 || call % 300 == 0) {
        log_msg("ID3DXEffect::Begin call #" + std::to_string(call) + " (technique=" +
                std::to_string(eff->active_tech_idx) + ", passes=" +
                std::to_string(eff->active_tech_idx < eff->techniques.size()
                                   ? eff->techniques[eff->active_tech_idx].passes.size()
                                   : 0) +
                ")");
    }
    if (passes != nullptr) {
        *passes = eff->active_tech_idx < eff->techniques.size()
                      ? static_cast<std::uint32_t>(eff->techniques[eff->active_tech_idx].passes.size())
                      : 1;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE begin_pass(void* self, std::uint32_t pass) {
    const std::uint64_t call = g_begin_pass_calls.fetch_add(1) + 1;
    if (call <= 5 || call % 300 == 0) {
        log_msg("ID3DXEffect::BeginPass call #" + std::to_string(call) + " (pass=" +
                std::to_string(pass) + ")");
    }
    g_dx11_frame_rendered.store(true);
    auto* eff = effect(self);
    eff->active_pass_idx = pass;
    eff->update_gpu_constants();

    if (!g_wvp_logged.exchange(true)) {
        const auto it = eff->param_by_semantic.find("WORLDVIEWPROJECTION");
        if (it != eff->param_by_semantic.end()) {
            log_msg("WORLDVIEWPROJECTION at BeginPass: offset=" +
                    std::to_string(eff->parameters[it->second].offset));
        } else {
            log_msg("No WORLDVIEWPROJECTION parameter found at BeginPass");
        }
    }

    if (eff->active_tech_idx >= eff->compiled_techniques.size()) {
        return S_OK;
    }
    const auto& technique = eff->compiled_techniques[eff->active_tech_idx];
    if (pass >= technique.size()) {
        return S_OK;
    }
    const auto& compiled = technique[pass];

    {
        std::lock_guard lock(g_active_pass_mutex);
        if (compiled.vs_bytecode) {
            ActivePassInfo info;
            info.vs_bytecode = *compiled.vs_bytecode;
            info.vs = compiled.vs;
            info.ps = compiled.ps;
            info.gs = compiled.gs;
            info.constant_buffer = eff->constant_buffer;
            info.blend_preset = compiled.blend_preset;
            info.depth_preset = compiled.depth_preset;
            info.raster_preset = compiled.raster_preset;
            g_active_pass = std::move(info);
        } else {
            g_active_pass.reset();
        }
    }

    auto* context = eff->context.Get();
    context->VSSetShader(compiled.vs.Get(), nullptr, 0);
    context->PSSetShader(compiled.ps.Get(), nullptr, 0);
    context->CSSetShader(compiled.cs.Get(), nullptr, 0);
    context->GSSetShader(compiled.gs.Get(), nullptr, 0);
    context->HSSetShader(compiled.hs.Get(), nullptr, 0);
    context->DSSetShader(compiled.ds.Get(), nullptr, 0);

    if (eff->constant_buffer) {
        ID3D11Buffer* buffers[] = {eff->constant_buffer.Get()};
        context->VSSetConstantBuffers(0, 1, buffers);
        context->PSSetConstantBuffers(0, 1, buffers);
        context->CSSetConstantBuffers(0, 1, buffers);
        context->GSSetConstantBuffers(0, 1, buffers);
        context->HSSetConstantBuffers(0, 1, buffers);
        context->DSSetConstantBuffers(0, 1, buffers);
    }

    for (std::size_t slot = 0; slot < eff->bound_texture_srvs.size(); ++slot) {
        auto* srv = eff->bound_texture_srvs[slot].Get();
        if (srv == nullptr) {
            continue;
        }
        context->VSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        context->PSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        context->CSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        if (compiled.gs) {
            context->GSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        }
        if (compiled.hs) {
            context->HSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        }
        if (compiled.ds) {
            context->DSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        }
    }

    if (auto* d3d = g_d3d11.get()) {
        auto* states = d3d->state_manager.get();
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(states->get_blend(compiled.blend_preset), blend_factor,
                                 0xFFFFFFFF);
        context->OMSetDepthStencilState(states->get_depth(compiled.depth_preset), 1);
        context->RSSetState(states->get_rasterizer(compiled.raster_preset));
        auto* sampler = states->get_sampler(SamplerPreset::LinearWrap);
        if (sampler != nullptr) {
            context->PSSetSamplers(0, 1, &sampler);
            context->VSSetSamplers(0, 1, &sampler);
            if (compiled.gs) {
                context->GSSetSamplers(0, 1, &sampler);
            }
            if (compiled.ds) {
                context->DSSetSamplers(0, 1, &sampler);
            }
        }
    }

    if (compiled.cs) {
        context->CSSetShader(compiled.cs.Get(), nullptr, 0);
        context->Dispatch(std::max(compiled.thread_group_size[0], 1u),
                          std::max(compiled.thread_group_size[1], 1u),
                          std::max(compiled.thread_group_size[2], 1u));
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE commit_changes(void* self) {
    effect(self)->update_gpu_constants();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE end_pass(void* self) {
    {
        std::lock_guard lock(g_active_pass_mutex);
        g_active_pass.reset();
    }
    auto* context = effect(self)->context.Get();
    context->CSSetUnorderedAccessViews(0, 0, nullptr, nullptr);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->CSSetShader(nullptr, nullptr, 0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE end(void*) {
    std::lock_guard lock(g_active_pass_mutex);
    g_active_pass.reset();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE get_device(void*, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    // MME expects an IDirect3DDevice9 here; exposing the D3D11 device through that
    // interface would be unsafe, so keep the original diagnostic-only behaviour.
    static std::atomic_bool logged = false;
    if (!logged.exchange(true)) {
        log_msg("ID3DXEffect::GetDevice called");
    }
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE dummy_ok(void*) {
    return S_OK;
}
HRESULT STDMETHODCALLTYPE set_state_manager(void*, void*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE get_state_manager(void*, void**) {
    return E_NOTIMPL;
}
D3DXHANDLE STDMETHODCALLTYPE end_parameter_block(void*) {
    return nullptr;
}
HRESULT STDMETHODCALLTYPE apply_parameter_block(void*, D3DXHANDLE) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE clone_effect(void*, void*, void**) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE set_raw_value(void* self, D3DXHANDLE handle, const void* data,
                                        std::uint32_t, std::uint32_t bytes) {
    return effect(self)->set_param_block(handle, data, bytes);
}

const ID3DXEffectVtbl g_effect_vtbl = {
    query_interface,
    add_ref,
    release,
    get_desc,
    get_parameter_desc,
    get_technique_desc,
    get_pass_desc,
    get_function_desc,
    get_parameter,
    get_parameter_by_name,
    get_parameter_by_semantic,
    get_parameter_element,
    get_technique,
    get_technique_by_name,
    get_pass,
    get_pass_by_name,
    get_function,
    get_function_by_name,
    get_annotation,
    get_annotation_by_name,
    set_value,
    get_value,
    set_bool,
    get_bool,
    set_bool_array,
    get_bool_array,
    set_int,
    get_int,
    set_int_array,
    get_int_array,
    set_float,
    get_float,
    set_float_array,
    get_float_array,
    set_vector,
    get_vector,
    set_vector_array,
    get_vector_array,
    set_matrix,
    get_matrix,
    set_matrix_array,
    get_matrix_array,
    set_matrix_pointer_array,
    get_matrix_pointer_array,
    set_matrix_transpose,
    get_matrix_transpose,
    set_matrix_transpose_array,
    get_matrix_transpose_array,
    set_matrix_transpose_pointer_array,
    get_matrix_transpose_pointer_array,
    set_string,
    get_string,
    set_texture,
    get_texture,
    get_pixel_shader,
    get_vertex_shader,
    set_array_range,
    get_pool,
    set_technique,
    get_current_technique,
    validate_technique,
    find_next_valid_technique,
    is_parameter_used,
    begin,
    begin_pass,
    commit_changes,
    end_pass,
    end,
    get_device,
    dummy_ok,
    dummy_ok,
    set_state_manager,
    get_state_manager,
    dummy_ok,
    end_parameter_block,
    apply_parameter_block,
    apply_parameter_block,
    clone_effect,
    set_raw_value,
};

} // namespace

const ID3DXEffectVtbl* get_effect_vtbl() {
    return &g_effect_vtbl;
}

} // namespace hikari::core
