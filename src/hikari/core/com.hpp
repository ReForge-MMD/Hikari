#pragma once

// COM-compatible declarations for the synthetic ID3DXEffect that Hikari exposes to
// MikuMikuEffect. The layouts mirror d3dx9effect.h and are ABI-compatible with the
// MSVC x64 vtable calling convention used by MME.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hikari::core {

using D3DXHANDLE = const void*;

struct D3DXEFFECT_DESC {
    const char* Creator = nullptr;
    std::uint32_t Parameters = 0;
    std::uint32_t Techniques = 0;
    std::uint32_t Functions = 0;
};

struct D3DXTECHNIQUE_DESC {
    const char* Name = nullptr;
    std::uint32_t Passes = 0;
    std::uint32_t Annotations = 0;
};

struct D3DXPASS_DESC {
    const char* Name = nullptr;
    std::uint32_t Annotations = 0;
    const std::uint32_t* pVertexShaderFunction = nullptr;
    const std::uint32_t* pPixelShaderFunction = nullptr;
};

struct D3DXPARAMETER_DESC {
    const char* Name = nullptr;
    const char* Semantic = nullptr;
    std::uint32_t Class = 0;
    std::uint32_t Type = 0;
    std::uint32_t Rows = 0;
    std::uint32_t Columns = 0;
    std::uint32_t Elements = 0;
    std::uint32_t Annotations = 0;
    std::uint32_t StructMembers = 0;
    std::uint32_t Flags = 0;
    std::uint32_t Bytes = 0;
};

struct D3DXVECTOR4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct D3DXMATRIX {
    float m[4][4]{};
};

struct ID3DXBufferVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void* self, const IID* riid, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(void* self);
    ULONG(STDMETHODCALLTYPE* Release)(void* self);
    void*(STDMETHODCALLTYPE* GetBufferPointer)(void* self);
    SIZE_T(STDMETHODCALLTYPE* GetBufferSize)(void* self);
};

// First member is the vtable pointer, so a HikariBuffer* is a valid COM object.
struct HikariBuffer {
    const ID3DXBufferVtbl* vtbl = nullptr;
    std::atomic<std::uint32_t> ref_count{1};
    std::vector<std::uint8_t> data;
};

HikariBuffer* create_hikari_buffer(std::vector<std::uint8_t> data);

struct ID3DXEffectVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, const IID*, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(void*);
    ULONG(STDMETHODCALLTYPE* Release)(void*);

    HRESULT(STDMETHODCALLTYPE* GetDesc)(void*, D3DXEFFECT_DESC*);
    HRESULT(STDMETHODCALLTYPE* GetParameterDesc)(void*, D3DXHANDLE, D3DXPARAMETER_DESC*);
    HRESULT(STDMETHODCALLTYPE* GetTechniqueDesc)(void*, D3DXHANDLE, D3DXTECHNIQUE_DESC*);
    HRESULT(STDMETHODCALLTYPE* GetPassDesc)(void*, D3DXHANDLE, D3DXPASS_DESC*);
    HRESULT(STDMETHODCALLTYPE* GetFunctionDesc)(void*, D3DXHANDLE, void*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetParameter)(void*, D3DXHANDLE, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetParameterByName)(void*, D3DXHANDLE, const char*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetParameterBySemantic)(void*, D3DXHANDLE, const char*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetParameterElement)(void*, D3DXHANDLE, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetTechnique)(void*, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetTechniqueByName)(void*, const char*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetPass)(void*, D3DXHANDLE, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetPassByName)(void*, D3DXHANDLE, const char*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetFunction)(void*, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetFunctionByName)(void*, const char*);
    D3DXHANDLE(STDMETHODCALLTYPE* GetAnnotation)(void*, D3DXHANDLE, std::uint32_t);
    D3DXHANDLE(STDMETHODCALLTYPE* GetAnnotationByName)(void*, D3DXHANDLE, const char*);

    HRESULT(STDMETHODCALLTYPE* SetValue)(void*, D3DXHANDLE, const void*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetValue)(void*, D3DXHANDLE, void*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetBool)(void*, D3DXHANDLE, BOOL);
    HRESULT(STDMETHODCALLTYPE* GetBool)(void*, D3DXHANDLE, BOOL*);
    HRESULT(STDMETHODCALLTYPE* SetBoolArray)(void*, D3DXHANDLE, const BOOL*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetBoolArray)(void*, D3DXHANDLE, BOOL*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetInt)(void*, D3DXHANDLE, int);
    HRESULT(STDMETHODCALLTYPE* GetInt)(void*, D3DXHANDLE, int*);
    HRESULT(STDMETHODCALLTYPE* SetIntArray)(void*, D3DXHANDLE, const int*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetIntArray)(void*, D3DXHANDLE, int*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetFloat)(void*, D3DXHANDLE, float);
    HRESULT(STDMETHODCALLTYPE* GetFloat)(void*, D3DXHANDLE, float*);
    HRESULT(STDMETHODCALLTYPE* SetFloatArray)(void*, D3DXHANDLE, const float*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetFloatArray)(void*, D3DXHANDLE, float*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetVector)(void*, D3DXHANDLE, const D3DXVECTOR4*);
    HRESULT(STDMETHODCALLTYPE* GetVector)(void*, D3DXHANDLE, D3DXVECTOR4*);
    HRESULT(STDMETHODCALLTYPE* SetVectorArray)(void*, D3DXHANDLE, const D3DXVECTOR4*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetVectorArray)(void*, D3DXHANDLE, D3DXVECTOR4*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetMatrix)(void*, D3DXHANDLE, const D3DXMATRIX*);
    HRESULT(STDMETHODCALLTYPE* GetMatrix)(void*, D3DXHANDLE, D3DXMATRIX*);
    HRESULT(STDMETHODCALLTYPE* SetMatrixArray)(void*, D3DXHANDLE, const D3DXMATRIX*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetMatrixArray)(void*, D3DXHANDLE, D3DXMATRIX*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetMatrixPointerArray)(void*, D3DXHANDLE, const D3DXMATRIX* const*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetMatrixPointerArray)(void*, D3DXHANDLE, D3DXMATRIX**, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetMatrixTranspose)(void*, D3DXHANDLE, const D3DXMATRIX*);
    HRESULT(STDMETHODCALLTYPE* GetMatrixTranspose)(void*, D3DXHANDLE, D3DXMATRIX*);
    HRESULT(STDMETHODCALLTYPE* SetMatrixTransposeArray)(void*, D3DXHANDLE, const D3DXMATRIX*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetMatrixTransposeArray)(void*, D3DXHANDLE, D3DXMATRIX*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetMatrixTransposePointerArray)(void*, D3DXHANDLE, const D3DXMATRIX* const*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetMatrixTransposePointerArray)(void*, D3DXHANDLE, D3DXMATRIX**, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* SetString)(void*, D3DXHANDLE, const char*);
    HRESULT(STDMETHODCALLTYPE* GetString)(void*, D3DXHANDLE, const char**);
    HRESULT(STDMETHODCALLTYPE* SetTexture)(void*, D3DXHANDLE, void*);
    HRESULT(STDMETHODCALLTYPE* GetTexture)(void*, D3DXHANDLE, void**);
    HRESULT(STDMETHODCALLTYPE* GetPixelShader)(void*, D3DXHANDLE, void**);
    HRESULT(STDMETHODCALLTYPE* GetVertexShader)(void*, D3DXHANDLE, void**);
    HRESULT(STDMETHODCALLTYPE* SetArrayRange)(void*, D3DXHANDLE, std::uint32_t, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* GetPool)(void*, void**);

    HRESULT(STDMETHODCALLTYPE* SetTechnique)(void*, D3DXHANDLE);
    D3DXHANDLE(STDMETHODCALLTYPE* GetCurrentTechnique)(void*);
    HRESULT(STDMETHODCALLTYPE* ValidateTechnique)(void*, D3DXHANDLE);
    HRESULT(STDMETHODCALLTYPE* FindNextValidTechnique)(void*, D3DXHANDLE, D3DXHANDLE*);
    BOOL(STDMETHODCALLTYPE* IsParameterUsed)(void*, D3DXHANDLE, D3DXHANDLE);
    HRESULT(STDMETHODCALLTYPE* Begin)(void*, std::uint32_t*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* BeginPass)(void*, std::uint32_t);
    HRESULT(STDMETHODCALLTYPE* CommitChanges)(void*);
    HRESULT(STDMETHODCALLTYPE* EndPass)(void*);
    HRESULT(STDMETHODCALLTYPE* End)(void*);
    HRESULT(STDMETHODCALLTYPE* GetDevice)(void*, void**);
    HRESULT(STDMETHODCALLTYPE* OnLostDevice)(void*);
    HRESULT(STDMETHODCALLTYPE* OnResetDevice)(void*);
    HRESULT(STDMETHODCALLTYPE* SetStateManager)(void*, void*);
    HRESULT(STDMETHODCALLTYPE* GetStateManager)(void*, void**);
    HRESULT(STDMETHODCALLTYPE* BeginParameterBlock)(void*);
    D3DXHANDLE(STDMETHODCALLTYPE* EndParameterBlock)(void*);
    HRESULT(STDMETHODCALLTYPE* ApplyParameterBlock)(void*, D3DXHANDLE);
    HRESULT(STDMETHODCALLTYPE* DeleteParameterBlock)(void*, D3DXHANDLE);
    HRESULT(STDMETHODCALLTYPE* CloneEffect)(void*, void*, void**);
    HRESULT(STDMETHODCALLTYPE* SetRawValue)(void*, D3DXHANDLE, const void*, std::uint32_t, std::uint32_t);
};

} // namespace hikari::core
