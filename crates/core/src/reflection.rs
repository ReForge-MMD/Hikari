//! Native DirectX 11 Shader Reflection Engine for Hikari.
//!
//! Utilizes `d3dcompiler_47.dll!D3DReflect` to dynamically inspect compiled SM 5.0
//! bytecodes and extract constant buffers, variable layouts, bound resources
//! (textures, samplers, UAVs), and input signatures.

use std::collections::HashMap;
use std::ffi::{c_void, CStr};
use std::ptr::null_mut;
use std::sync::OnceLock;
use windows::core::{GUID, HRESULT};
use windows::Win32::Foundation::HMODULE;
use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};

const IID_ID3D11_SHADER_REFLECTION: GUID = GUID::from_values(
    0x8d536ca1,
    0x0cca,
    0x4956,
    [0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84],
);

type PfnD3DReflect = unsafe extern "system" fn(
    pSrcData: *const c_void,
    SrcDataSize: usize,
    pInterface: *const GUID,
    ppReflector: *mut *mut c_void,
) -> HRESULT;

static REFLECT_FN: OnceLock<Option<PfnD3DReflect>> = OnceLock::new();

fn get_d3dreflect() -> Option<PfnD3DReflect> {
    *REFLECT_FN.get_or_init(|| {
        unsafe {
            let lib_name: Vec<u16> = "d3dcompiler_47.dll\0".encode_utf16().collect();
            let mut hmod = LoadLibraryW(windows::core::PCWSTR(lib_name.as_ptr())).unwrap_or(HMODULE(null_mut()));
            if hmod.0.is_null() {
                let fallback: Vec<u16> = "d3dcompiler_43.dll\0".encode_utf16().collect();
                hmod = LoadLibraryW(windows::core::PCWSTR(fallback.as_ptr())).unwrap_or(HMODULE(null_mut()));
            }
            if hmod.0.is_null() {
                return None;
            }
            let proc = GetProcAddress(hmod, windows::core::s!("D3DReflect"));
            proc.map(|p| std::mem::transmute::<_, PfnD3DReflect>(p))
        }
    })
}

#[repr(C)]
struct D3D11_SHADER_DESC {
    pub Version: u32,
    pub Creator: *const i8,
    pub Flags: u32,
    pub ConstantBuffers: u32,
    pub BoundResources: u32,
    pub InputParameters: u32,
    pub OutputParameters: u32,
    pub InstructionCount: u32,
    pub TempRegisterCount: u32,
    pub TempArrayCount: u32,
    pub DefCount: u32,
    pub DclCount: u32,
    pub TextureNormalInstructions: u32,
    pub TextureLoadInstructions: u32,
    pub TextureCompInstructions: u32,
    pub TextureBiasInstructions: u32,
    pub TextureGradientInstructions: u32,
    pub FloatInstructionCount: u32,
    pub IntInstructionCount: u32,
    pub UintInstructionCount: u32,
    pub StaticFlowControlCount: u32,
    pub DynamicFlowControlCount: u32,
    pub MacroInstructionCount: u32,
    pub ArrayInstructionCount: u32,
    pub CutInstructionCount: u32,
    pub EmitInstructionCount: u32,
    pub GSOutputTopology: u32,
    pub GSMaxOutputVertexCount: u32,
    pub InputPrimitive: u32,
    pub PatchConstantParameters: u32,
    pub cGSInstanceCount: u32,
    pub cControlPoints: u32,
    pub HSOutputPrimitive: u32,
    pub HSPartitioning: u32,
    pub TessellatorDomain: u32,
    pub cBarrierInstructions: u32,
    pub cInterlockedInstructions: u32,
    pub cTextureStoreInstructions: u32,
}

#[repr(C)]
struct D3D11_SHADER_INPUT_BIND_DESC {
    pub Name: *const i8,
    pub Type: u32,
    pub BindPoint: u32,
    pub BindCount: u32,
    pub uFlags: u32,
    pub ReturnType: u32,
    pub Dimension: u32,
    pub NumSamples: u32,
}

#[repr(C)]
struct D3D11_SHADER_BUFFER_DESC {
    pub Name: *const i8,
    pub Type: u32,
    pub Variables: u32,
    pub Size: u32,
    pub uFlags: u32,
}

#[repr(C)]
struct D3D11_SHADER_VARIABLE_DESC {
    pub Name: *const i8,
    pub StartOffset: u32,
    pub Size: u32,
    pub uFlags: u32,
    pub DefaultValue: *const c_void,
    pub StartTexture: u32,
    pub TextureSize: u32,
    pub StartSampler: u32,
    pub SamplerSize: u32,
}

#[repr(C)]
struct D3D11_SIGNATURE_PARAMETER_DESC {
    pub SemanticName: *const i8,
    pub SemanticIndex: u32,
    pub Register: u32,
    pub SystemValueType: u32,
    pub ComponentType: u32,
    pub Mask: u8,
    pub ReadWriteMask: u8,
    pub Stream: u32,
    pub MinPrecision: u32,
}

/// Variable inside a Constant Buffer.
#[derive(Debug, Clone, Default)]
pub struct ReflectedVariable {
    pub name: String,
    pub offset: u32,
    pub size: u32,
}

/// Constant Buffer description.
#[derive(Debug, Clone, Default)]
pub struct ReflectedCBuffer {
    pub name: String,
    pub slot: u32,
    pub size: u32,
    pub variables: HashMap<String, ReflectedVariable>,
}

/// Direct3D 11 bound resource type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReflectedResourceType {
    CBuffer,
    Texture,
    Sampler,
    Uav,
    StructuredBuffer,
    ByteAddressBuffer,
    Other(u32),
}

impl Default for ReflectedResourceType {
    fn default() -> Self {
        Self::Texture
    }
}

/// Bound resource in a shader stage (texture, sampler, UAV, etc.).
#[derive(Debug, Clone, Default)]
pub struct ReflectedResource {
    pub name: String,
    pub resource_type: ReflectedResourceType,
    pub bind_point: u32,
    pub bind_count: u32,
    pub return_type: u32,
    pub dimension: u32,
}

/// Input signature parameter (for vertex layouts).
#[derive(Debug, Clone, Default)]
pub struct ReflectedInputParam {
    pub semantic_name: String,
    pub semantic_index: u32,
    pub register: u32,
    pub component_type: u32,
    pub mask: u8,
}

/// Complete reflection data for a compiled HLSL 5.0 shader.
#[derive(Debug, Clone, Default)]
pub struct ShaderReflectionData {
    pub cbuffers: Vec<ReflectedCBuffer>,
    pub resources: Vec<ReflectedResource>,
    pub input_parameters: Vec<ReflectedInputParam>,
    pub thread_group_size: [u32; 3],
}

impl ShaderReflectionData {
    /// Inspects compiled bytecode and returns full reflection metadata.
    pub fn reflect(bytecode: &[u8]) -> Result<Self, String> {
        let reflect_fn = get_d3dreflect().ok_or_else(|| "d3dcompiler_47.dll not found".to_string())?;
        crate::log_msg(&format!("D3DReflect start ({} bytes)", bytecode.len()));

        let mut reflector: *mut c_void = null_mut();
        let hr = unsafe {
            reflect_fn(
                bytecode.as_ptr() as *const c_void,
                bytecode.len(),
                &IID_ID3D11_SHADER_REFLECTION,
                &mut reflector,
            )
        };

        if hr.is_err() || reflector.is_null() {
            return Err(format!("D3DReflect failed with HRESULT: {:?}", hr));
        }
        crate::log_msg("D3DReflect returned a shader reflection interface");

        let mut out = Self::default();

        unsafe {
            // Function pointers from ID3D11ShaderReflection vtable
            let vtable = *(reflector as *const *const usize);

            type PfnRelease = unsafe extern "system" fn(this: *mut c_void) -> u32;
            type PfnGetDesc = unsafe extern "system" fn(this: *mut c_void, pDesc: *mut D3D11_SHADER_DESC) -> HRESULT;
            type PfnGetCBufferByIndex = unsafe extern "system" fn(this: *mut c_void, Index: u32) -> *mut c_void;
            type PfnGetResourceBindingDesc = unsafe extern "system" fn(this: *mut c_void, ResourceIndex: u32, pDesc: *mut D3D11_SHADER_INPUT_BIND_DESC) -> HRESULT;
            type PfnGetInputParamDesc = unsafe extern "system" fn(this: *mut c_void, ParameterIndex: u32, pDesc: *mut D3D11_SIGNATURE_PARAMETER_DESC) -> HRESULT;
            type PfnGetThreadGroupSize = unsafe extern "system" fn(this: *mut c_void, pX: *mut u32, pY: *mut u32, pZ: *mut u32) -> u32;

            let release_fn: PfnRelease = std::mem::transmute(*vtable.add(2));
            let get_desc_fn: PfnGetDesc = std::mem::transmute(*vtable.add(3));
            let get_cb_by_index: PfnGetCBufferByIndex = std::mem::transmute(*vtable.add(4));
            let get_res_binding: PfnGetResourceBindingDesc = std::mem::transmute(*vtable.add(6));
            let get_input_param: PfnGetInputParamDesc = std::mem::transmute(*vtable.add(7));
            let get_thread_group_size: PfnGetThreadGroupSize = std::mem::transmute(*vtable.add(20));

            let mut shader_desc: D3D11_SHADER_DESC = std::mem::zeroed();
            if get_desc_fn(reflector, &mut shader_desc).is_ok() {
                crate::log_msg(&format!(
                    "Shader reflection desc: version=0x{:08X}, cbuffers={}, resources={}",
                    shader_desc.Version, shader_desc.ConstantBuffers, shader_desc.BoundResources
                ));
                if shader_desc.Version >> 16 == 5 {
                    let mut gx = 0u32;
                    let mut gy = 0u32;
                    let mut gz = 0u32;
                    get_thread_group_size(reflector, &mut gx, &mut gy, &mut gz);
                    out.thread_group_size = [gx, gy, gz];
                }

                let mut cb_slot_map: HashMap<String, u32> = HashMap::new();

                // Reflect Bound Resources (Textures, Samplers, UAVs, CBuffers)
                for i in 0..shader_desc.BoundResources {
                    let mut bind_desc: D3D11_SHADER_INPUT_BIND_DESC = std::mem::zeroed();
                    if get_res_binding(reflector, i, &mut bind_desc).is_ok() {
                        let res_name = if !bind_desc.Name.is_null() {
                            CStr::from_ptr(bind_desc.Name).to_string_lossy().into_owned()
                        } else {
                            format!("res_{i}")
                        };

                        if bind_desc.Type == 0 || bind_desc.Type == 1 {
                            cb_slot_map.insert(res_name.clone(), bind_desc.BindPoint);
                        }

                        let res_type = match bind_desc.Type {
                            0 => ReflectedResourceType::CBuffer,
                            2 => ReflectedResourceType::Texture,
                            3 => ReflectedResourceType::Sampler,
                            4 => ReflectedResourceType::Uav,
                            5 => ReflectedResourceType::StructuredBuffer,
                            6 => ReflectedResourceType::Uav,
                            7 => ReflectedResourceType::ByteAddressBuffer,
                            8 => ReflectedResourceType::Uav,
                            other => ReflectedResourceType::Other(other),
                        };

                        out.resources.push(ReflectedResource {
                            name: res_name,
                            resource_type: res_type,
                            bind_point: bind_desc.BindPoint,
                            bind_count: bind_desc.BindCount,
                            return_type: bind_desc.ReturnType,
                            dimension: bind_desc.Dimension,
                        });
                    }
                }

                // Reflect Constant Buffers
                for i in 0..shader_desc.ConstantBuffers {
                    let cb_ptr = get_cb_by_index(reflector, i);
                    if !cb_ptr.is_null() {
                        let cb_vtable = *(cb_ptr as *const *const usize);
                        type PfnCBGetDesc = unsafe extern "system" fn(this: *mut c_void, pDesc: *mut D3D11_SHADER_BUFFER_DESC) -> HRESULT;
                        type PfnCBGetVarByIndex = unsafe extern "system" fn(this: *mut c_void, Index: u32) -> *mut c_void;

                        let cb_get_desc: PfnCBGetDesc = std::mem::transmute(*cb_vtable.add(0));
                        let cb_get_var: PfnCBGetVarByIndex = std::mem::transmute(*cb_vtable.add(1));

                        let mut buf_desc: D3D11_SHADER_BUFFER_DESC = std::mem::zeroed();
                        if cb_get_desc(cb_ptr, &mut buf_desc).is_ok() {
                            let cb_name = if !buf_desc.Name.is_null() {
                                CStr::from_ptr(buf_desc.Name).to_string_lossy().into_owned()
                            } else {
                                format!("cbuffer_{i}")
                            };

                            let slot = *cb_slot_map.get(&cb_name).unwrap_or(&i);

                            let mut rcb = ReflectedCBuffer {
                                name: cb_name,
                                slot,
                                size: buf_desc.Size,
                                variables: HashMap::new(),
                            };

                            for v in 0..buf_desc.Variables {
                                let var_ptr = cb_get_var(cb_ptr, v);
                                if !var_ptr.is_null() {
                                    let var_vtable = *(var_ptr as *const *const usize);
                                    type PfnVarGetDesc = unsafe extern "system" fn(this: *mut c_void, pDesc: *mut D3D11_SHADER_VARIABLE_DESC) -> HRESULT;
                                    let var_get_desc: PfnVarGetDesc = std::mem::transmute(*var_vtable.add(0));

                                    let mut var_desc: D3D11_SHADER_VARIABLE_DESC = std::mem::zeroed();
                                    if var_get_desc(var_ptr, &mut var_desc).is_ok() {
                                        let var_name = if !var_desc.Name.is_null() {
                                            CStr::from_ptr(var_desc.Name).to_string_lossy().into_owned()
                                        } else {
                                            format!("var_{v}")
                                        };

                                        rcb.variables.insert(
                                            var_name.clone(),
                                            ReflectedVariable {
                                                name: var_name,
                                                offset: var_desc.StartOffset,
                                                size: var_desc.Size,
                                            },
                                        );
                                    }
                                }
                            }

                            out.cbuffers.push(rcb);
                        }
                    }
                }

                // Reflect Input Signature Parameters
                for i in 0..shader_desc.InputParameters {
                    let mut param_desc: D3D11_SIGNATURE_PARAMETER_DESC = std::mem::zeroed();
                    if get_input_param(reflector, i, &mut param_desc).is_ok() {
                        let sem_name = if !param_desc.SemanticName.is_null() {
                            CStr::from_ptr(param_desc.SemanticName).to_string_lossy().into_owned()
                        } else {
                            format!("PARAM_{i}")
                        };

                        out.input_parameters.push(ReflectedInputParam {
                            semantic_name: sem_name,
                            semantic_index: param_desc.SemanticIndex,
                            register: param_desc.Register,
                            component_type: param_desc.ComponentType,
                            mask: param_desc.Mask,
                        });
                    }
                }
            }

            release_fn(reflector);
        }

        crate::log_msg(&format!(
            "D3DReflect complete: {} constant buffers, {} resources",
            out.cbuffers.len(), out.resources.len()
        ));
        Ok(out)
    }

    /// Finds a texture register slot by variable/semantic name.
    pub fn find_texture_slot(&self, name: &str) -> Option<u32> {
        self.resources.iter().find_map(|r| {
            if r.resource_type == ReflectedResourceType::Texture && r.name.eq_ignore_ascii_case(name) {
                Some(r.bind_point)
            } else {
                None
            }
        })
    }

    /// Finds a sampler register slot by variable name.
    pub fn find_sampler_slot(&self, name: &str) -> Option<u32> {
        self.resources.iter().find_map(|r| {
            if r.resource_type == ReflectedResourceType::Sampler && r.name.eq_ignore_ascii_case(name) {
                Some(r.bind_point)
            } else {
                None
            }
        })
    }

    /// Finds a UAV register slot by variable name.
    pub fn find_uav_slot(&self, name: &str) -> Option<u32> {
        self.resources.iter().find_map(|r| {
            if r.resource_type == ReflectedResourceType::Uav && r.name.eq_ignore_ascii_case(name) {
                Some(r.bind_point)
            } else {
                None
            }
        })
    }
}
