//! COM utilities and Direct3D 9 / D3DX9 interface definitions for Hikari.

use std::ffi::c_void;
use windows::core::{BOOL, GUID, HRESULT};
use windows::Win32::Foundation::S_OK;

pub type D3DXHANDLE = *const c_void;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXEFFECT_DESC {
    pub Creator: *const i8,
    pub Parameters: u32,
    pub Techniques: u32,
    pub Functions: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXTECHNIQUE_DESC {
    pub Name: *const i8,
    pub Passes: u32,
    pub Annotations: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXPASS_DESC {
    pub Name: *const i8,
    pub Annotations: u32,
    pub pVertexShaderFunction: *const u32,
    pub pPixelShaderFunction: *const u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXPARAMETER_DESC {
    pub Name: *const i8,
    pub Semantic: *const i8,
    pub Class: u32,
    pub Type: u32,
    pub Rows: u32,
    pub Columns: u32,
    pub Elements: u32,
    pub Annotations: u32,
    pub StructMembers: u32,
    pub Flags: u32,
    pub Bytes: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXVECTOR4 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct D3DXMATRIX {
    pub m: [[f32; 4]; 4],
}

/// D3DX buffer interface for compilation errors and raw byte blobs.
#[repr(C)]
pub struct ID3DXBufferVtbl {
    pub QueryInterface: unsafe extern "system" fn(this: *mut c_void, riid: *const GUID, ppv: *mut *mut c_void) -> HRESULT,
    pub AddRef: unsafe extern "system" fn(this: *mut c_void) -> u32,
    pub Release: unsafe extern "system" fn(this: *mut c_void) -> u32,
    pub GetBufferPointer: unsafe extern "system" fn(this: *mut c_void) -> *mut c_void,
    pub GetBufferSize: unsafe extern "system" fn(this: *mut c_void) -> usize,
}

#[repr(C)]
pub struct HikariBuffer {
    pub vtbl: *const ID3DXBufferVtbl,
    pub ref_count: std::sync::atomic::AtomicU32,
    pub data: Vec<u8>,
}

impl HikariBuffer {
    pub fn new(data: Vec<u8>) -> *mut c_void {
        static VTBL: ID3DXBufferVtbl = ID3DXBufferVtbl {
            QueryInterface: buffer_query_interface,
            AddRef: buffer_add_ref,
            Release: buffer_release,
            GetBufferPointer: buffer_get_pointer,
            GetBufferSize: buffer_get_size,
        };

        let buffer = Box::new(HikariBuffer {
            vtbl: &VTBL,
            ref_count: std::sync::atomic::AtomicU32::new(1),
            data,
        });

        Box::into_raw(buffer) as *mut c_void
    }
}

unsafe extern "system" fn buffer_query_interface(this: *mut c_void, _riid: *const GUID, ppv: *mut *mut c_void) -> HRESULT {
    if ppv.is_null() {
        return windows::core::HRESULT(-2147467261); // E_POINTER
    }
    *ppv = this;
    buffer_add_ref(this);
    S_OK
}

unsafe extern "system" fn buffer_add_ref(this: *mut c_void) -> u32 {
    let buf = &*(this as *mut HikariBuffer);
    buf.ref_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1
}

unsafe extern "system" fn buffer_release(this: *mut c_void) -> u32 {
    let buf = &*(this as *mut HikariBuffer);
    let count = buf.ref_count.fetch_sub(1, std::sync::atomic::Ordering::Release) - 1;
    if count == 0 {
        std::sync::atomic::fence(std::sync::atomic::Ordering::Acquire);
        drop(Box::from_raw(this as *mut HikariBuffer));
    }
    count
}

unsafe extern "system" fn buffer_get_pointer(this: *mut c_void) -> *mut c_void {
    let buf = &*(this as *mut HikariBuffer);
    buf.data.as_ptr() as *mut c_void
}

unsafe extern "system" fn buffer_get_size(this: *mut c_void) -> usize {
    let buf = &*(this as *mut HikariBuffer);
    buf.data.len()
}

/// Complete MSVC x64 C++ vtable layout for ID3DXEffect
#[repr(C)]
pub struct ID3DXEffectVtbl {
    // IUnknown
    pub QueryInterface: unsafe extern "system" fn(this: *mut c_void, riid: *const GUID, ppv: *mut *mut c_void) -> HRESULT,
    pub AddRef: unsafe extern "system" fn(this: *mut c_void) -> u32,
    pub Release: unsafe extern "system" fn(this: *mut c_void) -> u32,

    // ID3DXBaseEffect
    pub GetDesc: unsafe extern "system" fn(this: *mut c_void, pDesc: *mut D3DXEFFECT_DESC) -> HRESULT,
    pub GetParameterDesc: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pDesc: *mut D3DXPARAMETER_DESC) -> HRESULT,
    pub GetTechniqueDesc: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE, pDesc: *mut D3DXTECHNIQUE_DESC) -> HRESULT,
    pub GetPassDesc: unsafe extern "system" fn(this: *mut c_void, hPass: D3DXHANDLE, pDesc: *mut D3DXPASS_DESC) -> HRESULT,
    pub GetFunctionDesc: unsafe extern "system" fn(this: *mut c_void, hShader: D3DXHANDLE, pDesc: *mut c_void) -> HRESULT,
    pub GetParameter: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, Index: u32) -> D3DXHANDLE,
    pub GetParameterByName: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE,
    pub GetParameterBySemantic: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pSemantic: *const i8) -> D3DXHANDLE,
    pub GetParameterElement: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, Index: u32) -> D3DXHANDLE,
    pub GetTechnique: unsafe extern "system" fn(this: *mut c_void, Index: u32) -> D3DXHANDLE,
    pub GetTechniqueByName: unsafe extern "system" fn(this: *mut c_void, pName: *const i8) -> D3DXHANDLE,
    pub GetPass: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE, Index: u32) -> D3DXHANDLE,
    pub GetPassByName: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE,
    pub GetFunction: unsafe extern "system" fn(this: *mut c_void, Index: u32) -> D3DXHANDLE,
    pub GetFunctionByName: unsafe extern "system" fn(this: *mut c_void, pName: *const i8) -> D3DXHANDLE,
    pub GetAnnotation: unsafe extern "system" fn(this: *mut c_void, hObject: D3DXHANDLE, Index: u32) -> D3DXHANDLE,
    pub GetAnnotationByName: unsafe extern "system" fn(this: *mut c_void, hObject: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE,

    // Parameter Setters/Getters
    pub SetValue: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const c_void, Bytes: u32) -> HRESULT,
    pub GetValue: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut c_void, Bytes: u32) -> HRESULT,
    pub SetBool: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, b: BOOL) -> HRESULT,
    pub GetBool: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pb: *mut BOOL) -> HRESULT,
    pub SetBoolArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pb: *const BOOL, Count: u32) -> HRESULT,
    pub GetBoolArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pb: *mut BOOL, Count: u32) -> HRESULT,
    pub SetInt: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, n: i32) -> HRESULT,
    pub GetInt: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pn: *mut i32) -> HRESULT,
    pub SetIntArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pn: *const i32, Count: u32) -> HRESULT,
    pub GetIntArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pn: *mut i32, Count: u32) -> HRESULT,
    pub SetFloat: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, f: f32) -> HRESULT,
    pub GetFloat: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pf: *mut f32) -> HRESULT,
    pub SetFloatArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pf: *const f32, Count: u32) -> HRESULT,
    pub GetFloatArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pf: *mut f32, Count: u32) -> HRESULT,
    pub SetVector: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *const D3DXVECTOR4) -> HRESULT,
    pub GetVector: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *mut D3DXVECTOR4) -> HRESULT,
    pub SetVectorArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *const D3DXVECTOR4, Count: u32) -> HRESULT,
    pub GetVectorArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *mut D3DXVECTOR4, Count: u32) -> HRESULT,
    pub SetMatrix: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX) -> HRESULT,
    pub GetMatrix: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX) -> HRESULT,
    pub SetMatrixArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX, Count: u32) -> HRESULT,
    pub GetMatrixArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX, Count: u32) -> HRESULT,
    pub SetMatrixPointerArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppMatrix: *const *const D3DXMATRIX, Count: u32) -> HRESULT,
    pub GetMatrixPointerArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppMatrix: *mut *mut D3DXMATRIX, Count: u32) -> HRESULT,
    pub SetMatrixTranspose: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX) -> HRESULT,
    pub GetMatrixTranspose: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX) -> HRESULT,
    pub SetMatrixTransposeArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX, Count: u32) -> HRESULT,
    pub GetMatrixTransposeArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX, Count: u32) -> HRESULT,
    pub SetMatrixTransposePointerArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppMatrix: *const *const D3DXMATRIX, Count: u32) -> HRESULT,
    pub GetMatrixTransposePointerArray: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppMatrix: *mut *mut D3DXMATRIX, Count: u32) -> HRESULT,
    pub SetString: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pString: *const i8) -> HRESULT,
    pub GetString: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppString: *mut *const i8) -> HRESULT,
    pub SetTexture: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pTexture: *mut c_void) -> HRESULT,
    pub GetTexture: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppTexture: *mut *mut c_void) -> HRESULT,
    pub GetPixelShader: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppPShader: *mut *mut c_void) -> HRESULT,
    pub GetVertexShader: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, ppVShader: *mut *mut c_void) -> HRESULT,
    pub SetArrayRange: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, uStart: u32, uEnd: u32) -> HRESULT,
    pub GetPool: unsafe extern "system" fn(this: *mut c_void, ppPool: *mut *mut c_void) -> HRESULT,

    // ID3DXEffect Methods
    pub SetTechnique: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE) -> HRESULT,
    pub GetCurrentTechnique: unsafe extern "system" fn(this: *mut c_void) -> D3DXHANDLE,
    pub ValidateTechnique: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE) -> HRESULT,
    pub FindNextValidTechnique: unsafe extern "system" fn(this: *mut c_void, hTechnique: D3DXHANDLE, pTechnique: *mut D3DXHANDLE) -> HRESULT,
    pub IsParameterUsed: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, hTechnique: D3DXHANDLE) -> BOOL,
    pub Begin: unsafe extern "system" fn(this: *mut c_void, pPasses: *mut u32, Flags: u32) -> HRESULT,
    pub BeginPass: unsafe extern "system" fn(this: *mut c_void, Pass: u32) -> HRESULT,
    pub CommitChanges: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub EndPass: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub End: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub GetDevice: unsafe extern "system" fn(this: *mut c_void, ppDevice: *mut *mut c_void) -> HRESULT,
    pub OnLostDevice: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub OnResetDevice: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub SetStateManager: unsafe extern "system" fn(this: *mut c_void, pManager: *mut c_void) -> HRESULT,
    pub GetStateManager: unsafe extern "system" fn(this: *mut c_void, ppManager: *mut *mut c_void) -> HRESULT,
    pub BeginParameterBlock: unsafe extern "system" fn(this: *mut c_void) -> HRESULT,
    pub EndParameterBlock: unsafe extern "system" fn(this: *mut c_void) -> D3DXHANDLE,
    pub ApplyParameterBlock: unsafe extern "system" fn(this: *mut c_void, hParameterBlock: D3DXHANDLE) -> HRESULT,
    pub DeleteParameterBlock: unsafe extern "system" fn(this: *mut c_void, hParameterBlock: D3DXHANDLE) -> HRESULT,
    pub CloneEffect: unsafe extern "system" fn(this: *mut c_void, pDevice: *mut c_void, ppEffect: *mut *mut c_void) -> HRESULT,
    pub SetRawValue: unsafe extern "system" fn(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const c_void, ByteOffset: u32, Bytes: u32) -> HRESULT,
}
