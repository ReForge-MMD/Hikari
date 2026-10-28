//! Synthetic ID3DXEffect COM proxy and DirectX 11 pipeline binder for Hikari.

use std::collections::HashMap;
use std::ffi::{c_void, CStr, CString};
use std::collections::HashSet;
use std::ptr::{null, null_mut};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Mutex;
use windows::core::{BOOL, GUID, HRESULT};
use windows::Win32::Foundation::{E_FAIL, E_NOTIMPL, E_POINTER, S_OK};
use windows::Win32::Graphics::Direct3D11::{
    ID3D11Buffer, ID3D11ComputeShader, ID3D11Device, ID3D11DeviceContext,
    ID3D11DomainShader, ID3D11GeometryShader, ID3D11HullShader, ID3D11PixelShader,
    ID3D11ShaderResourceView, ID3D11VertexShader,
};

use crate::com::*;
use crate::d3d11::D3D11Context;
use crate::reflection::ShaderReflectionData;
use crate::shader::{compile_hlsl, parse_techniques, TechniqueDesc};
use crate::states::{BlendPreset, DepthPreset, RasterizerPreset};

pub const HANDLE_TECHNIQUE_BASE: usize = 0x10000;
pub const HANDLE_PASS_BASE: usize      = 0x20000;
pub const HANDLE_PARAM_BASE: usize     = 0x30000;
pub const HANDLE_ANNO_BASE: usize      = 0x40000;

/// Internal compiled stages for a single pass.
#[derive(Clone)]
pub struct CompiledPass {
    pub vs: Option<ID3D11VertexShader>,
    pub vs_bytecode: Option<Vec<u8>>,
    pub ps: Option<ID3D11PixelShader>,
    pub ps_bytecode: Option<Vec<u8>>,
    pub cs: Option<ID3D11ComputeShader>,
    pub cs_bytecode: Option<Vec<u8>>,
    pub gs: Option<ID3D11GeometryShader>,
    pub gs_bytecode: Option<Vec<u8>>,
    pub hs: Option<ID3D11HullShader>,
    pub hs_bytecode: Option<Vec<u8>>,
    pub ds: Option<ID3D11DomainShader>,
    pub ds_bytecode: Option<Vec<u8>>,
    pub blend_preset: BlendPreset,
    pub depth_preset: DepthPreset,
    pub raster_preset: RasterizerPreset,
    pub thread_group_size: [u32; 3],
}

#[derive(Clone)]
pub struct ActivePassInfo {
    pub vs_bytecode: Vec<u8>,
    pub vs: Option<ID3D11VertexShader>,
    pub ps: Option<ID3D11PixelShader>,
    pub constant_buffer: Option<ID3D11Buffer>,
    pub blend_preset: BlendPreset,
    pub depth_preset: DepthPreset,
    pub raster_preset: RasterizerPreset,
}

pub static ACTIVE_EFFECT_PASS: Mutex<Option<ActivePassInfo>> = Mutex::new(None);
static WVP_VALUE_LOGGED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// Logs the first calls to an unimplemented stub so the caller can be
/// identified from hikari.log. Returns E_NOTIMPL.
fn unimplemented_stub(name: &'static str) -> HRESULT {
    static LOGGED_NAMES: std::sync::LazyLock<Mutex<HashSet<&'static str>>> = std::sync::LazyLock::new(|| Mutex::new(HashSet::new()));
    let first = match LOGGED_NAMES.lock() {        Ok(mut set) => set.insert(name),
        Err(_) => false,
    };
    if first {
        crate::log_msg(&format!(
            "ID3DXEffect::{} called - the stub returns E_NOTIMPL (0x80004001)",
            name
        ));
    }
    E_NOTIMPL
}

/// Represents an annotation parsed from `< string Name = "Value"; >`.
#[derive(Clone, Debug)]
pub struct EffectAnnotation {
    pub name: String,
    pub value: String,
    pub c_name: CString,
    pub c_val: CString,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UIWidgetKind {
    None,
    Slider,
    Drag,
    Checkbox,
    Color,
    Combo,
    Input,
}

#[derive(Clone, Debug)]
pub struct ParameterUIDesc {
    pub label: String,
    pub widget: UIWidgetKind,
    pub min: f32,
    pub max: f32,
    pub step: f32,
    pub items: Vec<String>,
    pub group: String,
    pub help: String,
    pub default_val: String,
}

/// Represents a parameter tracked by the effect.
#[derive(Clone, Debug)]
pub struct EffectParameter {
    pub name: String,
    pub semantic: String,
    pub hlsl_type: String,
    pub row_major: bool,
    pub c_name: CString,
    pub c_semantic: CString,
    pub offset: usize,
    pub size: usize,
    pub annotation_indices: Vec<usize>,
    pub ui: Option<ParameterUIDesc>,
}

/// Synthetic ID3DXEffect implementation in Rust.
#[repr(C)]
pub struct HikariEffect {
    pub vtbl: *const ID3DXEffectVtbl,
    pub ref_count: AtomicU32,
    pub techniques: Vec<TechniqueDesc>,
    pub compiled_techniques: Vec<Vec<CompiledPass>>,
    pub active_tech_idx: usize,
    pub active_pass_idx: usize,
    pub technique_c_names: Vec<CString>,
    pub pass_c_names: Vec<Vec<CString>>,
    pub technique_annotations: Vec<Vec<usize>>,
    pub pass_annotations: Vec<Vec<Vec<usize>>>,
    pub annotations: Vec<EffectAnnotation>,
    pub parameters: Vec<EffectParameter>,
    pub param_by_name: HashMap<String, usize>,
    pub param_by_semantic: HashMap<String, usize>,
    pub name_to_texture_slot: HashMap<String, u32>,
    pub bound_texture_srvs: [Option<ID3D11ShaderResourceView>; 16],
    pub constant_buffer: Option<ID3D11Buffer>,
    pub constant_data: Vec<u8>,
    pub constant_dirty: bool,
    pub device: ID3D11Device,
    pub context: ID3D11DeviceContext,
}

impl HikariEffect {
    /// Creates and compiles a new DirectX 11 native effect from HLSL source.
    pub fn new(
        source: &str,
        source_name: Option<&str>,
        d3d11: &D3D11Context,
    ) -> Result<*mut HikariEffect, String> {
        let techniques = parse_techniques(source);
        crate::log_msg(&format!(
            "Parsed {} techniques from {}",
            techniques.len(),
            source_name.unwrap_or("<memory>")
        ));
        let mut compiled_techniques = Vec::new();

        let mut name_to_texture_slot = HashMap::new();

        // Compile all passes for each technique
        for tech in &techniques {
            let mut compiled_passes = Vec::new();
            for pass in &tech.passes {
                let mut cp = CompiledPass {
                    vs: None,
                    vs_bytecode: None,
                    ps: None,
                    ps_bytecode: None,
                    cs: None,
                    cs_bytecode: None,
                    gs: None,
                    gs_bytecode: None,
                    hs: None,
                    hs_bytecode: None,
                    ds: None,
                    ds_bytecode: None,
                    blend_preset: pass.blend_preset.clone(),
                    depth_preset: pass.depth_preset.clone(),
                    raster_preset: pass.raster_preset.clone(),
                    thread_group_size: [1, 1, 1],
                };

                // Compile Vertex Shader
                if let Some((entry, target)) = &pass.vs_entry {
                    crate::log_msg(&format!("Compiling vertex shader {entry} ({target})"));
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    crate::log_msg(&format!("Compiled vertex shader {entry} ({} bytes)", bytecode.len()));
                    let mut vs = None;
                    unsafe {
                        d3d11.device.CreateVertexShader(&bytecode, None, Some(&mut vs))
                            .map_err(|e| format!("CreateVertexShader error: {e}"))?;
                    }
                    crate::log_msg(&format!("Created D3D11 vertex shader {entry}"));
                    if let Ok(refl) = ShaderReflectionData::reflect(&bytecode) {
                        for res in refl.resources {
                            if res.resource_type == crate::reflection::ReflectedResourceType::Texture {
                                name_to_texture_slot.insert(res.name.to_lowercase(), res.bind_point);
                            }
                        }
                    }
                    cp.vs = vs;
                    cp.vs_bytecode = Some(bytecode);
                }

                // Compile Pixel Shader
                if let Some((entry, target)) = &pass.ps_entry {
                    crate::log_msg(&format!("Compiling pixel shader {entry} ({target})"));
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    crate::log_msg(&format!("Compiled pixel shader {entry} ({} bytes)", bytecode.len()));
                    let mut ps = None;
                    unsafe {
                        d3d11.device.CreatePixelShader(&bytecode, None, Some(&mut ps))
                            .map_err(|e| format!("CreatePixelShader error: {e}"))?;
                    }
                    crate::log_msg(&format!("Created D3D11 pixel shader {entry}"));
                    if let Ok(refl) = ShaderReflectionData::reflect(&bytecode) {
                        for res in refl.resources {
                            if res.resource_type == crate::reflection::ReflectedResourceType::Texture {
                                name_to_texture_slot.insert(res.name.to_lowercase(), res.bind_point);
                            }
                        }
                    }
                    cp.ps = ps;
                    cp.ps_bytecode = Some(bytecode);
                }

                // Compile Compute Shader
                if let Some((entry, target)) = &pass.cs_entry {
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    let mut cs = None;
                    unsafe {
                        d3d11.device.CreateComputeShader(&bytecode, None, Some(&mut cs))
                            .map_err(|e| format!("CreateComputeShader error: {e}"))?;
                    }
                    if let Ok(refl) = ShaderReflectionData::reflect(&bytecode) {
                        if refl.thread_group_size[0] > 0 {
                            cp.thread_group_size = refl.thread_group_size;
                        }
                    }
                    cp.cs = cs;
                    cp.cs_bytecode = Some(bytecode);
                }

                // Compile Geometry Shader
                if let Some((entry, target)) = &pass.gs_entry {
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    let mut gs = None;
                    unsafe {
                        d3d11.device.CreateGeometryShader(&bytecode, None, Some(&mut gs))
                            .map_err(|e| format!("CreateGeometryShader error: {e}"))?;
                    }
                    cp.gs = gs;
                    cp.gs_bytecode = Some(bytecode);
                }

                // Compile Hull Shader
                if let Some((entry, target)) = &pass.hs_entry {
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    let mut hs = None;
                    unsafe {
                        d3d11.device.CreateHullShader(&bytecode, None, Some(&mut hs))
                            .map_err(|e| format!("CreateHullShader error: {e}"))?;
                    }
                    cp.hs = hs;
                    cp.hs_bytecode = Some(bytecode);
                }

                // Compile Domain Shader
                if let Some((entry, target)) = &pass.ds_entry {
                    let bytecode = compile_hlsl(source, entry, target, source_name, &[])?;
                    let mut ds = None;
                    unsafe {
                        d3d11.device.CreateDomainShader(&bytecode, None, Some(&mut ds))
                            .map_err(|e| format!("CreateDomainShader error: {e}"))?;
                    }
                    cp.ds = ds;
                    cp.ds_bytecode = Some(bytecode);
                }

                compiled_passes.push(cp);
            }
            compiled_techniques.push(compiled_passes);
        }

        // 1. Precompute technique and pass CStrings and annotations
        let mut annotations = Vec::new();
        let mut technique_c_names = Vec::new();
        let mut pass_c_names = Vec::new();
        let mut technique_annotations = Vec::new();
        let mut pass_annotations = Vec::new();

        for tech in &techniques {
            technique_c_names.push(CString::new(tech.name.as_str()).unwrap_or_default());
            let mut t_annos = Vec::new();
            for (k, v) in &tech.annotations {
                let a_idx = annotations.len();
                annotations.push(EffectAnnotation {
                    name: k.clone(),
                    value: v.clone(),
                    c_name: CString::new(k.as_str()).unwrap_or_default(),
                    c_val: CString::new(v.as_str()).unwrap_or_default(),
                });
                t_annos.push(a_idx);
            }
            technique_annotations.push(t_annos);

            let mut pass_names = Vec::new();
            let mut p_annos_list = Vec::new();
            for pass in &tech.passes {
                pass_names.push(CString::new(pass.name.as_str()).unwrap_or_default());
                let mut p_annos = Vec::new();
                for (k, v) in &pass.annotations {
                    let a_idx = annotations.len();
                    annotations.push(EffectAnnotation {
                        name: k.clone(),
                        value: v.clone(),
                        c_name: CString::new(k.as_str()).unwrap_or_default(),
                        c_val: CString::new(v.as_str()).unwrap_or_default(),
                    });
                    p_annos.push(a_idx);
                }
                p_annos_list.push(p_annos);
            }
            pass_c_names.push(pass_names);
            pass_annotations.push(p_annos_list);
        }

        // 2. Global variables and annotations
        let var_semantics = crate::shader::parse_variable_semantics(source);
        let var_types = crate::shader::parse_variable_types(source);
        let global_vars = crate::shader::parse_global_variables(source);
        let mut parameters = Vec::new();
        let mut param_by_name = HashMap::new();
        let mut param_by_semantic = HashMap::new();
        let mut next_cb_offset = 0usize;

        for g in global_vars {
            let mut anno_indices = Vec::new();
            let mut ui_label = String::new();
            let mut ui_widget = UIWidgetKind::None;
            let mut ui_min = 0.0f32;
            let mut ui_max = 1.0f32;
            let mut ui_step = 0.01f32;
            let mut ui_items = Vec::new();
            let mut ui_group = String::new();
            let mut ui_help = String::new();
            let mut has_ui = false;

            for (k, v) in &g.annotations {
                let a_idx = annotations.len();
                annotations.push(EffectAnnotation {
                    name: k.clone(),
                    value: v.clone(),
                    c_name: CString::new(k.as_str()).unwrap_or_default(),
                    c_val: CString::new(v.as_str()).unwrap_or_default(),
                });
                anno_indices.push(a_idx);

                let lower_k = k.to_lowercase();
                match lower_k.as_str() {
                    "uiname" | "name" => {
                        ui_label = v.clone();
                        has_ui = true;
                    }
                    "uiwidget" | "widget" => {
                        has_ui = true;
                        let lw = v.to_lowercase();
                        ui_widget = match lw.as_str() {
                            "slider" => UIWidgetKind::Slider,
                            "drag" => UIWidgetKind::Drag,
                            "checkbox" | "check" | "bool" => UIWidgetKind::Checkbox,
                            "color" => UIWidgetKind::Color,
                            "combo" | "dropdown" | "list" => UIWidgetKind::Combo,
                            "input" | "edit" => UIWidgetKind::Input,
                            _ => UIWidgetKind::None,
                        };
                    }
                    "uimin" | "min" => {
                        if let Ok(val) = v.parse::<f32>() {
                            ui_min = val;
                            has_ui = true;
                        }
                    }
                    "uimax" | "max" => {
                        if let Ok(val) = v.parse::<f32>() {
                            ui_max = val;
                            has_ui = true;
                        }
                    }
                    "uistep" | "step" => {
                        if let Ok(val) = v.parse::<f32>() {
                            ui_step = val;
                        }
                    }
                    "uiitems" | "items" => {
                        has_ui = true;
                        ui_items = v
                            .split([',', ';', '\0'])
                            .map(|s| s.trim().to_string())
                            .filter(|s| !s.is_empty())
                            .collect();
                    }
                    "uigroup" | "group" => {
                        ui_group = v.clone();
                    }
                    "uihelp" | "help" => {
                        ui_help = v.clone();
                    }
                    _ => {}
                }
            }

            let lower = g.name.to_lowercase();
            let semantic = if !g.semantic.is_empty() {
                g.semantic.clone()
            } else if let Some(sem) = var_semantics.get(&lower) {
                sem.clone()
            } else {
                String::new()
            };

            let c_name = CString::new(g.name.as_str()).unwrap_or_default();
            let c_semantic = CString::new(semantic.as_str()).unwrap_or_default();
            let (hlsl_type, row_major) = var_types.get(&lower).cloned().unwrap_or_default();

            if has_ui {
                if ui_label.is_empty() {
                    ui_label = g.name.clone();
                }
                if ui_widget == UIWidgetKind::None {
                    ui_widget = match hlsl_type.as_str() {
                        "bool" => UIWidgetKind::Checkbox,
                        "float" => UIWidgetKind::Slider,
                        "float3" | "float4" => UIWidgetKind::Color,
                        "int" => UIWidgetKind::Input,
                        _ => UIWidgetKind::None,
                    };
                }
            }

            let ui_desc = if has_ui {
                Some(ParameterUIDesc {
                    label: ui_label,
                    widget: ui_widget,
                    min: ui_min,
                    max: ui_max,
                    step: ui_step,
                    items: ui_items,
                    group: ui_group,
                    help: ui_help,
                    default_val: g.default_val.clone(),
                })
            } else {
                None
            };

            let p_idx = parameters.len();
            parameters.push(EffectParameter {
                name: g.name.clone(),
                semantic: semantic.clone(),
                hlsl_type,
                row_major,
                c_name,
                c_semantic,
                offset: next_cb_offset,
                size: 64,
                annotation_indices: anno_indices,
                ui: ui_desc,
            });
            next_cb_offset += 64;
            param_by_name.insert(lower, p_idx);
            if !semantic.is_empty() {
                param_by_semantic.insert(semantic.to_uppercase(), p_idx);
            }
        }

        // 3. Reflect compiled stages to capture cbuffer variable offsets
        for tech in &compiled_techniques {
            for pass in tech {
                if let Some(ref bc) = pass.vs_bytecode {
                    if let Ok(refl) = ShaderReflectionData::reflect(bc) {
                        for cb in &refl.cbuffers {
                            for (var_name, var_info) in &cb.variables {
                                let offset = var_info.offset as usize;
                                let size = var_info.size as usize;
                                let lower = var_name.to_lowercase();
                                let sem_opt = var_semantics.get(&lower).cloned();

                                if let Some(&existing_idx) = param_by_name.get(&lower) {
                                    parameters[existing_idx].offset = offset;
                                    parameters[existing_idx].size = size;
                                    if parameters[existing_idx].semantic.is_empty() {
                                        if let Some(sem) = sem_opt {
                                            parameters[existing_idx].semantic = sem.clone();
                                            parameters[existing_idx].c_semantic = CString::new(sem.as_str()).unwrap_or_default();
                                            param_by_semantic.insert(sem.to_uppercase(), existing_idx);
                                        }
                                    }
                                } else {
                                    let p_idx = parameters.len();
                                    let semantic = sem_opt.unwrap_or_default();
                                    let c_name = CString::new(var_name.as_str()).unwrap_or_default();
                                    let c_sem = CString::new(semantic.as_str()).unwrap_or_default();
                                    let (hlsl_type, row_major) = var_types.get(&lower).cloned().unwrap_or_default();
                                    parameters.push(EffectParameter {
                                        name: var_name.clone(),
                                        semantic: semantic.clone(),
                                        hlsl_type,
                                        row_major,
                                        c_name,
                                        c_semantic: c_sem,
                                        offset,
                                        size,
                                        annotation_indices: Vec::new(),
                                        ui: None,
                                    });
                                    param_by_name.insert(lower, p_idx);
                                    if !semantic.is_empty() {
                                        param_by_semantic.insert(semantic.to_uppercase(), p_idx);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 4. Standard MMD semantics
        let std_semantics = [
            ("WORLD", 64),
            ("VIEW", 64),
            ("PROJECTION", 64),
            ("WORLDVIEW", 64),
            ("VIEWPROJECTION", 64),
            ("WORLDVIEWPROJECTION", 64),
            ("WORLDINVERSE", 64),
            ("VIEWINVERSE", 64),
            ("PROJECTIONINVERSE", 64),
            ("WORLDVIEWPROJECTIONINVERSE", 64),
            ("WORLDTRANSPOSE", 64),
            ("VIEWTRANSPOSE", 64),
            ("PROJECTIONTRANSPOSE", 64),
            ("WORLDVIEWTRANSPOSE", 64),
            ("WORLDVIEWPROJECTIONTRANSPOSE", 64),
            ("CAMERA_POSITION", 16),
            ("LIGHT_DIRECTION", 16),
            ("LIGHT_DIFFUSE", 16),
            ("LIGHT_SPECULAR", 16),
            ("LIGHT_AMBIENT", 16),
            ("DIFFUSE", 16),
            ("AMBIENT", 16),
            ("SPECULAR", 16),
            ("EMISSIVE", 16),
            ("POWER", 4),
            ("TIME", 16),
            ("ELAPSED_TIME", 16),
        ];

        for (sem, size) in std_semantics {
            let upper = sem.to_uppercase();
            if !param_by_semantic.contains_key(&upper) {
                let lower = sem.to_lowercase();
                if let Some(&idx) = param_by_name.get(&lower) {
                    if parameters[idx].semantic.is_empty() {
                        parameters[idx].semantic = sem.to_string();
                        parameters[idx].c_semantic = CString::new(sem).unwrap_or_default();
                        param_by_semantic.insert(upper, idx);
                    }
                } else {
                    let p_idx = parameters.len();
                    let c_name = CString::new(lower.as_str()).unwrap_or_default();
                    let c_sem = CString::new(sem).unwrap_or_default();
                    let (hlsl_type, row_major) = var_types.get(&lower).cloned().unwrap_or_default();
                    parameters.push(EffectParameter {
                        name: lower.clone(),
                        semantic: sem.to_string(),
                        hlsl_type,
                        row_major,
                        c_name,
                        c_semantic: c_sem,
                        offset: next_cb_offset,
                        size,
                        annotation_indices: Vec::new(),
                        ui: None,
                    });
                    next_cb_offset += size;
                    param_by_name.insert(lower, p_idx);
                    param_by_semantic.insert(upper, p_idx);
                }
            }
        }

        // Allocate a 64KB constant buffer (register b0)
        let cb_size = 65536;
        let constant_buffer = d3d11.create_constant_buffer(cb_size).ok();
        let mut constant_data = vec![0u8; cb_size];
        let mut constant_dirty = false;

        for p in &parameters {
            if let Some(ref ui_desc) = p.ui {
                if !ui_desc.default_val.is_empty() && p.offset + p.size <= constant_data.len() {
                    let slice = &mut constant_data[p.offset..p.offset + p.size];
                    if parse_and_write_default_val(&ui_desc.default_val, &p.hlsl_type, slice) {
                        constant_dirty = true;
                    }
                }
            }
        }

        static VTBL: ID3DXEffectVtbl = ID3DXEffectVtbl {
            QueryInterface: effect_query_interface,
            AddRef: effect_add_ref,
            Release: effect_release,

            GetDesc: effect_get_desc,
            GetParameterDesc: effect_get_parameter_desc,
            GetTechniqueDesc: effect_get_technique_desc,
            GetPassDesc: effect_get_pass_desc,
            GetFunctionDesc: effect_dummy_func_desc,
            GetParameter: effect_get_parameter,
            GetParameterByName: effect_get_parameter_by_name,
            GetParameterBySemantic: effect_get_parameter_by_semantic,
            GetParameterElement: effect_dummy_handle_index,
            GetTechnique: effect_get_technique,
            GetTechniqueByName: effect_get_technique_by_name,
            GetPass: effect_get_pass,
            GetPassByName: effect_get_pass_by_name,
            GetFunction: effect_dummy_func_index,
            GetFunctionByName: effect_dummy_func_name,
            GetAnnotation: effect_get_annotation,
            GetAnnotationByName: effect_get_annotation_by_name,

            SetValue: effect_set_value,
            GetValue: effect_get_value,
            SetBool: effect_set_bool,
            GetBool: effect_get_bool,
            SetBoolArray: effect_set_bool_array,
            GetBoolArray: effect_get_bool_array,
            SetInt: effect_set_int,
            GetInt: effect_get_int,
            SetIntArray: effect_set_int_array,
            GetIntArray: effect_get_int_array,
            SetFloat: effect_set_float,
            GetFloat: effect_get_float,
            SetFloatArray: effect_set_float_array,
            GetFloatArray: effect_get_float_array,
            SetVector: effect_set_vector,
            GetVector: effect_get_vector,
            SetVectorArray: effect_set_vector_array,
            GetVectorArray: effect_get_vector_array,
            SetMatrix: effect_set_matrix,
            GetMatrix: effect_get_matrix,
            SetMatrixArray: effect_set_matrix_array,
            GetMatrixArray: effect_get_matrix_array,
            SetMatrixPointerArray: effect_set_matrix_pointer_array,
            GetMatrixPointerArray: effect_get_matrix_pointer_array,
            SetMatrixTranspose: effect_set_matrix_transpose,
            GetMatrixTranspose: effect_get_matrix_transpose,
            SetMatrixTransposeArray: effect_set_matrix_transpose_array,
            GetMatrixTransposeArray: effect_get_matrix_transpose_array,
            SetMatrixTransposePointerArray: effect_set_matrix_transpose_pointer_array,
            GetMatrixTransposePointerArray: effect_get_matrix_transpose_pointer_array,
            SetString: effect_dummy_setstring,
            GetString: effect_get_string,
            SetTexture: effect_set_texture,
            GetTexture: effect_dummy_gettexture,
            GetPixelShader: effect_dummy_getpixelshader,
            GetVertexShader: effect_dummy_getvertexshader,
            SetArrayRange: effect_dummy_setarrayrange,
            GetPool: effect_dummy_getpool,

            SetTechnique: effect_set_technique,
            GetCurrentTechnique: effect_get_current_technique,
            ValidateTechnique: effect_validate_technique,
            FindNextValidTechnique: effect_find_next_valid_technique,
            IsParameterUsed: effect_is_parameter_used,
            Begin: effect_begin,
            BeginPass: effect_begin_pass,
            CommitChanges: effect_commit_changes,
            EndPass: effect_end_pass,
            End: effect_end,
            GetDevice: effect_get_device,
            OnLostDevice: effect_dummy_ok,
            OnResetDevice: effect_dummy_ok,
            SetStateManager: effect_dummy_setstatemanager,
            GetStateManager: effect_dummy_getstatemanager,
            BeginParameterBlock: effect_dummy_ok,
            EndParameterBlock: effect_dummy_handle_block,
            ApplyParameterBlock: effect_dummy_apply_block,
            DeleteParameterBlock: effect_dummy_apply_block,
            CloneEffect: effect_clone_effect,
            SetRawValue: effect_set_raw_value,
        };

        let effect = Box::new(HikariEffect {
            vtbl: &VTBL,
            ref_count: AtomicU32::new(1),
            techniques,
            compiled_techniques,
            active_tech_idx: 0,
            active_pass_idx: 0,
            technique_c_names,
            pass_c_names,
            technique_annotations,
            pass_annotations,
            annotations,
            parameters,
            param_by_name,
            param_by_semantic,
            name_to_texture_slot,
            bound_texture_srvs: [
                None, None, None, None, None, None, None, None,
                None, None, None, None, None, None, None, None,
            ],
            constant_buffer,
            constant_data,
            constant_dirty,
            device: d3d11.device.clone(),
            context: d3d11.context.clone(),
        });

        Ok(Box::into_raw(effect))
    }

    /// Uploads dirty constant buffers to the GPU.
    pub fn update_gpu_constants(&mut self) {
        if !self.constant_dirty {
            return;
        }
        if let Some(ref cb) = self.constant_buffer {
            unsafe {
                self.context.UpdateSubresource(
                    cb,
                    0,
                    None,
                    self.constant_data.as_ptr() as *const c_void,
                    0,
                    0,
                );
            }
        }
        self.constant_dirty = false;
    }

    pub fn get_param_value_f32(&self, param_idx: usize) -> Option<f32> {
        let p = self.parameters.get(param_idx)?;
        if p.offset + 4 <= self.constant_data.len() {
            let mut bytes = [0u8; 4];
            bytes.copy_from_slice(&self.constant_data[p.offset..p.offset + 4]);
            Some(f32::from_ne_bytes(bytes))
        } else {
            None
        }
    }

    pub fn set_param_value_f32(&mut self, param_idx: usize, val: f32) {
        if let Some(p) = self.parameters.get(param_idx) {
            if p.offset + 4 <= self.constant_data.len() {
                self.constant_data[p.offset..p.offset + 4].copy_from_slice(&val.to_ne_bytes());
                self.constant_dirty = true;
            }
        }
    }

    pub fn get_param_value_float3(&self, param_idx: usize) -> Option<[f32; 3]> {
        let p = self.parameters.get(param_idx)?;
        if p.offset + 12 <= self.constant_data.len() {
            let mut res = [0.0f32; 3];
            for i in 0..3 {
                let off = p.offset + i * 4;
                let mut bytes = [0u8; 4];
                bytes.copy_from_slice(&self.constant_data[off..off + 4]);
                res[i] = f32::from_ne_bytes(bytes);
            }
            Some(res)
        } else {
            None
        }
    }

    pub fn set_param_value_float3(&mut self, param_idx: usize, val: [f32; 3]) {
        if let Some(p) = self.parameters.get(param_idx) {
            if p.offset + 12 <= self.constant_data.len() {
                for i in 0..3 {
                    let off = p.offset + i * 4;
                    self.constant_data[off..off + 4].copy_from_slice(&val[i].to_ne_bytes());
                }
                self.constant_dirty = true;
            }
        }
    }

    pub fn get_param_value_i32(&self, param_idx: usize) -> Option<i32> {
        let p = self.parameters.get(param_idx)?;
        if p.offset + 4 <= self.constant_data.len() {
            let mut bytes = [0u8; 4];
            bytes.copy_from_slice(&self.constant_data[p.offset..p.offset + 4]);
            Some(i32::from_ne_bytes(bytes))
        } else {
            None
        }
    }

    pub fn set_param_value_i32(&mut self, param_idx: usize, val: i32) {
        if let Some(p) = self.parameters.get(param_idx) {
            if p.offset + 4 <= self.constant_data.len() {
                self.constant_data[p.offset..p.offset + 4].copy_from_slice(&val.to_ne_bytes());
                self.constant_dirty = true;
            }
        }
    }

    pub fn get_param_value_bool(&self, param_idx: usize) -> Option<bool> {
        let p = self.parameters.get(param_idx)?;
        if p.offset + 4 <= self.constant_data.len() {
            let mut bytes = [0u8; 4];
            bytes.copy_from_slice(&self.constant_data[p.offset..p.offset + 4]);
            let i = u32::from_ne_bytes(bytes);
            let f = f32::from_ne_bytes(bytes);
            Some(i != 0 || f > 0.5)
        } else {
            None
        }
    }

    pub fn set_param_value_bool(&mut self, param_idx: usize, val: bool) {
        if let Some(p) = self.parameters.get(param_idx) {
            if p.offset + 4 <= self.constant_data.len() {
                let v = if val { 1u32 } else { 0u32 };
                self.constant_data[p.offset..p.offset + 4].copy_from_slice(&v.to_ne_bytes());
                self.constant_dirty = true;
            }
        }
    }

    pub fn get_first_pass_info(&mut self) -> Option<ActivePassInfo> {
        self.update_gpu_constants();
        if self.compiled_techniques.is_empty() {
            return None;
        }
        let tech_idx = self.active_tech_idx.min(self.compiled_techniques.len() - 1);
        let tech = &self.compiled_techniques[tech_idx];
        if tech.is_empty() {
            return None;
        }
        let pass_idx = self.active_pass_idx.min(tech.len() - 1);
        let pass = &tech[pass_idx];
        let bc = pass.vs_bytecode.as_ref()?;
        Some(ActivePassInfo {
            vs_bytecode: bc.clone(),
            vs: pass.vs.clone(),
            ps: pass.ps.clone(),
            constant_buffer: self.constant_buffer.clone(),
            blend_preset: pass.blend_preset.clone(),
            depth_preset: pass.depth_preset.clone(),
            raster_preset: pass.raster_preset.clone(),
        })
    }

    pub fn find_technique_for_pass(&self, current_tech: i32) -> Option<usize> {
        let pass_name = match current_tech {
            0 => "object",
            1 => "object_ss",
            2 => "edge",
            3 => "shadow",
            _ => "object",
        };

        // 1. Check technique annotations for MMDPass matching pass_name (case-insensitive)
        for (idx, tech) in self.techniques.iter().enumerate() {
            if let Some(val) = tech.annotations.get("MMDPass") {
                if val.eq_ignore_ascii_case(pass_name) {
                    if idx < self.compiled_techniques.len() && !self.compiled_techniques[idx].is_empty() {
                        return Some(idx);
                    }
                }
            }
        }

        // 2. Check technique name containing pass_name
        for (idx, tech) in self.techniques.iter().enumerate() {
            let name_lower = tech.name.to_lowercase();
            if name_lower == pass_name || name_lower.contains(pass_name) {
                if idx < self.compiled_techniques.len() && !self.compiled_techniques[idx].is_empty() {
                    return Some(idx);
                }
            }
        }

        // 3. Fallbacks
        if current_tech == 1 {
            // Fallback object_ss to object
            return self.find_technique_for_pass(0);
        }

        if current_tech == 0 {
            // For standard object pass, fallback to active_tech_idx or first valid technique
            if self.active_tech_idx < self.compiled_techniques.len() && !self.compiled_techniques[self.active_tech_idx].is_empty() {
                return Some(self.active_tech_idx);
            }
            for (idx, passes) in self.compiled_techniques.iter().enumerate() {
                if !passes.is_empty() {
                    return Some(idx);
                }
            }
        }

        // Edge or shadow pass: if effect does not define an edge/shadow technique, return None
        None
    }

    pub fn get_technique_pass_info(&mut self, current_tech: i32) -> Option<ActivePassInfo> {
        self.update_gpu_constants();
        let tech_idx = self.find_technique_for_pass(current_tech)?;
        let tech = self.compiled_techniques.get(tech_idx)?;
        if tech.is_empty() {
            return None;
        }
        let pass_idx = self.active_pass_idx.min(tech.len() - 1);
        let pass = &tech[pass_idx];
        let bc = pass.vs_bytecode.as_ref()?;
        Some(ActivePassInfo {
            vs_bytecode: bc.clone(),
            vs: pass.vs.clone(),
            ps: pass.ps.clone(),
            constant_buffer: self.constant_buffer.clone(),
            blend_preset: pass.blend_preset.clone(),
            depth_preset: pass.depth_preset.clone(),
            raster_preset: pass.raster_preset.clone(),
        })
    }
}

pub fn parse_and_write_default_val(val_str: &str, ty: &str, out: &mut [u8]) -> bool {
    let val = val_str.trim().trim_end_matches(';').trim();
    if val.is_empty() || out.is_empty() {
        return false;
    }
    let lower_val = val.to_lowercase();
    if ty == "bool" || lower_val == "true" || lower_val == "false" {
        let b = if lower_val == "true" || lower_val == "1" { 1u32 } else { 0u32 };
        if out.len() >= 4 {
            out[..4].copy_from_slice(&b.to_ne_bytes());
            return true;
        }
    }
    if ty.starts_with("int") {
        if let Ok(i) = val.parse::<i32>() {
            if out.len() >= 4 {
                out[..4].copy_from_slice(&i.to_ne_bytes());
                return true;
            }
        }
    }
    let clean = val
        .trim_start_matches("float4")
        .trim_start_matches("float3")
        .trim_start_matches("float2")
        .trim_start_matches("float")
        .trim_matches(['(', ')', '{', '}']);
    let parts: Vec<f32> = clean
        .split(',')
        .filter_map(|s| s.trim().trim_end_matches(['f', 'F']).parse::<f32>().ok())
        .collect();
    if !parts.is_empty() {
        let count = parts.len().min(out.len() / 4);
        for i in 0..count {
            out[i * 4..(i + 1) * 4].copy_from_slice(&parts[i].to_ne_bytes());
        }
        return true;
    }
    false
}

#[derive(Clone)]
pub struct EffectItemInfo {
    pub path: String,
    pub name: String,
    pub effect: *mut HikariEffect,
    pub enabled: bool,
}

unsafe impl Send for EffectItemInfo {}
unsafe impl Sync for EffectItemInfo {}

pub static EFFECT_REGISTRY: Mutex<Vec<EffectItemInfo>> = Mutex::new(Vec::new());

pub fn load_effect_from_file(path: &str) -> Result<usize, String> {
    let source = std::fs::read_to_string(path)
        .map_err(|e| format!("Failed to read effect file '{}': {}", path, e))?;

    let d3d11_guard = crate::vtable::GLOBAL_D3D11.lock().unwrap();
    let d3d11 = d3d11_guard
        .as_ref()
        .ok_or_else(|| "DirectX 11 context is not initialized yet".to_string())?;

    let effect_ptr = HikariEffect::new(&source, Some(path), d3d11)?;

    let file_name = std::path::Path::new(path)
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| path.to_string());

    let mut reg = EFFECT_REGISTRY.lock().unwrap();
    let idx = reg.len();
    reg.push(EffectItemInfo {
        path: path.to_string(),
        name: file_name,
        effect: effect_ptr,
        enabled: true,
    });

    crate::log_msg(&format!("Successfully loaded Hikari effect #{idx}: '{path}'"));
    Ok(idx)
}

pub fn reload_effect(idx: usize) -> Result<(), String> {
    let mut reg = EFFECT_REGISTRY.lock().unwrap();
    let item = reg.get_mut(idx).ok_or_else(|| "Effect index out of bounds".to_string())?;

    let source = std::fs::read_to_string(&item.path)
        .map_err(|e| format!("Failed to read effect file '{}': {}", item.path, e))?;

    let d3d11_guard = crate::vtable::GLOBAL_D3D11.lock().unwrap();
    let d3d11 = d3d11_guard
        .as_ref()
        .ok_or_else(|| "DirectX 11 context is not initialized yet".to_string())?;

    let new_effect_ptr = HikariEffect::new(&source, Some(&item.path), d3d11)?;

    if !item.effect.is_null() {
        unsafe {
            let old = &mut *item.effect;
            let new_eff = &mut *new_effect_ptr;
            for (p_name, &old_p_idx) in &old.param_by_name {
                if let Some(&new_p_idx) = new_eff.param_by_name.get(p_name) {
                    let old_p = &old.parameters[old_p_idx];
                    let new_p = &new_eff.parameters[new_p_idx];
                    if old_p.size == new_p.size
                        && old_p.offset + old_p.size <= old.constant_data.len()
                        && new_p.offset + new_p.size <= new_eff.constant_data.len()
                    {
                        new_eff.constant_data[new_p.offset..new_p.offset + new_p.size]
                            .copy_from_slice(&old.constant_data[old_p.offset..old_p.offset + old_p.size]);
                        new_eff.constant_dirty = true;
                    }
                }
            }
            drop(Box::from_raw(item.effect));
        }
    }

    item.effect = new_effect_ptr;
    crate::log_msg(&format!("Successfully reloaded Hikari effect #{idx}: '{}'", item.path));
    Ok(())
}

pub fn get_active_direct_effect_pass() -> Option<ActivePassInfo> {
    let reg = EFFECT_REGISTRY.lock().unwrap();
    for item in reg.iter() {
        if item.enabled && !item.effect.is_null() {
            unsafe {
                let eff = &mut *item.effect;
                crate::backend::update_effect_standard_matrices(eff);
                eff.update_gpu_constants();
                return eff.get_first_pass_info();
            }
        }
    }
    None
}

#[derive(Clone, Debug, Default)]
pub struct ModelSubsetAssignment {
    pub effect_path: String,
    pub effect: Option<*mut HikariEffect>,
    pub enabled: bool,
}

unsafe impl Send for ModelSubsetAssignment {}
unsafe impl Sync for ModelSubsetAssignment {}

#[derive(Clone, Debug)]
pub struct ModelAssignment {
    pub object_id: usize,
    pub filename: String,
    pub effect_path: String,
    pub effect: Option<*mut HikariEffect>,
    pub subsets: HashMap<i32, ModelSubsetAssignment>,
    pub enabled: bool,
}

unsafe impl Send for ModelAssignment {}
unsafe impl Sync for ModelAssignment {}

pub struct ModelAssignmentRegistry {
    pub default_effect_path: String,
    pub default_effect: Option<*mut HikariEffect>,
    pub models: HashMap<usize, ModelAssignment>,
    pub order_to_id: HashMap<i32, usize>,
    pub expanded_models: HashSet<usize>,
}

unsafe impl Send for ModelAssignmentRegistry {}
unsafe impl Sync for ModelAssignmentRegistry {}

impl ModelAssignmentRegistry {
    pub fn new() -> Self {
        Self {
            default_effect_path: String::new(),
            default_effect: None,
            models: HashMap::new(),
            order_to_id: HashMap::new(),
            expanded_models: HashSet::new(),
        }
    }

    pub fn update_has_assigned(&self) {
        let has = self.default_effect.is_some() || self.models.values().any(|m| {
            m.effect.is_some() || m.subsets.values().any(|s| s.effect.is_some())
        });
        HAS_ASSIGNED_EFFECTS.store(has, Ordering::Release);
    }

    pub fn assign_default_effect(&mut self, path: &str) -> Result<(), String> {
        if path.is_empty() {
            if let Some(old) = self.default_effect.take() {
                defer_drop_effect(old);
            }
            self.default_effect_path.clear();
            self.update_has_assigned();
            return Ok(());
        }

        let new_effect = create_effect_instance(path)?;
        if let Some(old) = self.default_effect.replace(new_effect) {
            defer_drop_effect(old);
        }
        self.default_effect_path = path.to_string();
        HAS_ASSIGNED_EFFECTS.store(true, Ordering::Release);
        crate::log_msg(&format!("[Assignment] Set default effect: '{path}'"));
        Ok(())
    }

    pub fn assign_model_effect(&mut self, object_id: usize, filename: &str, path: &str) -> Result<(), String> {
        let model = self.models.entry(object_id).or_insert_with(|| ModelAssignment {
            object_id,
            filename: filename.to_string(),
            effect_path: String::new(),
            effect: None,
            subsets: HashMap::new(),
            enabled: true,
        });

        if path.is_empty() {
            if let Some(old) = model.effect.take() {
                defer_drop_effect(old);
            }
            model.effect_path.clear();
            self.update_has_assigned();
            return Ok(());
        }

        let new_effect = create_effect_instance(path)?;
        if let Some(old) = model.effect.replace(new_effect) {
            defer_drop_effect(old);
        }
        model.effect_path = path.to_string();
        model.enabled = true;
        HAS_ASSIGNED_EFFECTS.store(true, Ordering::Release);
        crate::log_msg(&format!("[Assignment] Set model (id=0x{object_id:x}) effect: '{path}'"));
        Ok(())
    }

    pub fn assign_subset_effect(&mut self, object_id: usize, filename: &str, subset: i32, path: &str) -> Result<(), String> {
        let model = self.models.entry(object_id).or_insert_with(|| ModelAssignment {
            object_id,
            filename: filename.to_string(),
            effect_path: String::new(),
            effect: None,
            subsets: HashMap::new(),
            enabled: true,
        });

        if path.is_empty() {
            if let Some(sub) = model.subsets.get_mut(&subset) {
                if let Some(old) = sub.effect.take() {
                    defer_drop_effect(old);
                }
                sub.effect_path.clear();
            }
            self.update_has_assigned();
            return Ok(());
        }

        let new_effect = create_effect_instance(path)?;
        let sub = model.subsets.entry(subset).or_insert_with(|| ModelSubsetAssignment {
            effect_path: String::new(),
            effect: None,
            enabled: true,
        });

        if let Some(old) = sub.effect.replace(new_effect) {
            defer_drop_effect(old);
        }
        sub.effect_path = path.to_string();
        sub.enabled = true;
        HAS_ASSIGNED_EFFECTS.store(true, Ordering::Release);
        crate::log_msg(&format!("[Assignment] Set model (id=0x{object_id:x}) subset #{subset} effect: '{path}'"));
        Ok(())
    }

    pub fn toggle_subset_expand(&mut self, object_id: usize) {
        if self.expanded_models.contains(&object_id) {
            self.expanded_models.remove(&object_id);
        } else {
            self.expanded_models.insert(object_id);
        }
    }

    pub fn is_subset_expanded(&self, object_id: usize) -> bool {
        self.expanded_models.contains(&object_id)
    }

    pub fn sync_host_objects(&mut self) {
        let host_objs = crate::mmd_host::enumerate_host_objects();
        self.order_to_id.clear();
        for obj in host_objs {
            self.order_to_id.insert(obj.order, obj.id);
            self.models.entry(obj.id).or_insert_with(|| ModelAssignment {
                object_id: obj.id,
                filename: obj.filename.clone(),
                effect_path: String::new(),
                effect: None,
                subsets: HashMap::new(),
                enabled: obj.disp,
            });
        }
    }

    pub fn resolve_effect(&self, object_order: i32, subset_index: i32) -> Option<*mut HikariEffect> {
        if object_order != 0 {
            let mut resolved_model = self.order_to_id.get(&object_order).and_then(|id| self.models.get(id));
            if resolved_model.is_none() {
                resolved_model = self.models.get(&(object_order as usize));
            }

            if let Some(model) = resolved_model {
                if model.enabled {
                    // Check subset override
                    if subset_index >= 0 {
                        if let Some(sub) = model.subsets.get(&subset_index) {
                            if sub.enabled && sub.effect.is_some() {
                                return sub.effect;
                            }
                        }
                    }
                    // Check model effect
                    if let Some(eff) = model.effect {
                        return Some(eff);
                    }
                }
            }
        } else if subset_index >= 0 {
            // In Camera mode, MMD sets ExpGetCurrentObject() to 0 while still rendering model subsets.
            // If models are loaded and assigned effects, resolve against the active model.
            for model in self.models.values() {
                if model.enabled {
                    if let Some(sub) = model.subsets.get(&subset_index) {
                        if sub.enabled && sub.effect.is_some() {
                            return sub.effect;
                        }
                    }
                    if let Some(eff) = model.effect {
                        return Some(eff);
                    }
                }
            }
        }

        // Fallback to default effect
        self.default_effect
    }

    pub fn is_effect_alive(&self, ptr: *mut HikariEffect) -> bool {
        if ptr.is_null() {
            return false;
        }
        if self.default_effect == Some(ptr) {
            return true;
        }
        for model in self.models.values() {
            if model.effect == Some(ptr) {
                return true;
            }
            for sub in model.subsets.values() {
                if sub.effect == Some(ptr) {
                    return true;
                }
            }
        }
        false
    }
}

static DEFERRED_EFFECTS: Mutex<Vec<usize>> = Mutex::new(Vec::new());

pub fn defer_drop_effect(eff: *mut HikariEffect) {
    if !eff.is_null() {
        DEFERRED_EFFECTS.lock().unwrap().push(eff as usize);
    }
}

pub fn drain_deferred_effects() {
    let list = {
        let mut guard = DEFERRED_EFFECTS.lock().unwrap();
        std::mem::take(&mut *guard)
    };
    for eff_addr in list {
        if eff_addr != 0 {
            unsafe {
                drop(Box::from_raw(eff_addr as *mut HikariEffect));
            }
        }
    }
}

pub static HAS_ASSIGNED_EFFECTS: AtomicBool = AtomicBool::new(false);

pub static ASSIGNMENT_REGISTRY: std::sync::LazyLock<Mutex<ModelAssignmentRegistry>> =
    std::sync::LazyLock::new(|| Mutex::new(ModelAssignmentRegistry::new()));

pub fn get_assigned_effect_pass(object_order: i32, subset_index: i32, current_tech: i32) -> Option<ActivePassInfo> {
    if !HAS_ASSIGNED_EFFECTS.load(Ordering::Relaxed) {
        return None;
    }
    let reg = ASSIGNMENT_REGISTRY.lock().ok()?;
    if let Some(eff_ptr) = reg.resolve_effect(object_order, subset_index) {
        if !eff_ptr.is_null() {
            unsafe {
                let eff = &mut *eff_ptr;
                crate::backend::update_effect_standard_matrices(eff);
                eff.update_gpu_constants();
                return eff.get_technique_pass_info(current_tech);
            }
        }
    }
    None
}

pub fn create_effect_instance(path: &str) -> Result<*mut HikariEffect, String> {
    if path.is_empty() {
        return Err("Empty path".to_string());
    }
    let source = std::fs::read_to_string(path)
        .map_err(|e| format!("Failed to read effect file '{}': {}", path, e))?;

    let d3d11_guard = crate::vtable::GLOBAL_D3D11.lock().unwrap();
    let d3d11 = d3d11_guard
        .as_ref()
        .ok_or_else(|| "DirectX 11 context is not initialized yet".to_string())?;

    HikariEffect::new(&source, Some(path), d3d11)
}


pub unsafe fn is_hikari_effect(this: *mut c_void) -> bool {
    if this.is_null() {
        return false;
    }
    let effect = &*(this as *const HikariEffect);
    !effect.vtbl.is_null() && ((*effect.vtbl).GetDesc as usize) == (effect_get_desc as usize)
}

pub unsafe fn disassemble_hikari_effect(this: *mut c_void) -> Result<Vec<u8>, String> {
    if !is_hikari_effect(this) {
        return Err("not a Hikari effect".to_string());
    }
    let effect = &*(this as *const HikariEffect);
    let mut output = String::from("//listing of all techniques and passes with embedded asm listings\n\n");
    let stages: [(&str, fn(&CompiledPass) -> Option<&[u8]>); 6] = [
        ("vertexshader", |pass| pass.vs_bytecode.as_deref()),
        ("pixelshader", |pass| pass.ps_bytecode.as_deref()),
        ("geometryshader", |pass| pass.gs_bytecode.as_deref()),
        ("hullshader", |pass| pass.hs_bytecode.as_deref()),
        ("domainshader", |pass| pass.ds_bytecode.as_deref()),
        ("computeshader", |pass| pass.cs_bytecode.as_deref()),
    ];

    for (tech_index, passes) in effect.compiled_techniques.iter().enumerate() {
        output.push_str(&format!("technique {}\n{{\n", effect.technique_c_names[tech_index].to_string_lossy()));
        for (pass_index, pass) in passes.iter().enumerate() {
            output.push_str(&format!("    pass {}\n    {{\n", effect.pass_c_names[tech_index][pass_index].to_string_lossy()));
            for (stage_name, get_bytecode) in stages {
                if let Some(bytecode) = get_bytecode(pass) {
                    let assembly = crate::shader::disassemble_dxbc(bytecode)?;
                    output.push_str(&format!("        {stage_name} =\n            asm {{\n"));
                    for line in String::from_utf8_lossy(&assembly).lines() {
                        output.push_str("                ");
                        output.push_str(line);
                        output.push('\n');
                    }
                    output.push_str("            };\n\n");
                }
            }
            output.push_str("    }\n\n");
        }
        output.push_str("}\n\n");
    }
    Ok(output.into_bytes())
}

// COM vtable implementations
unsafe extern "system" fn effect_query_interface(this: *mut c_void, riid: *const GUID, ppv: *mut *mut c_void) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::QueryInterface(this={:p}, riid={:p})", this, riid));
    if ppv.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    *ppv = this;
    effect_add_ref(this);
    S_OK
}

unsafe extern "system" fn effect_add_ref(this: *mut c_void) -> u32 {
    let eff = &*(this as *mut HikariEffect);
    eff.ref_count.fetch_add(1, Ordering::Relaxed) + 1
}

unsafe extern "system" fn effect_release(this: *mut c_void) -> u32 {
    let eff = &*(this as *mut HikariEffect);
    let count = eff.ref_count.fetch_sub(1, Ordering::Release) - 1;
    if count == 0 {
        std::sync::atomic::fence(Ordering::Acquire);
        drop(Box::from_raw(this as *mut HikariEffect));
    }
    count
}

unsafe extern "system" fn effect_get_desc(this: *mut c_void, pDesc: *mut D3DXEFFECT_DESC) -> HRESULT {
    crate::log_msg("ID3DXEffect::GetDesc");
    if pDesc.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    (*pDesc).Creator = windows::core::s!("Hikari DX11 Engine").as_ptr() as *const i8;
    (*pDesc).Techniques = eff.techniques.len() as u32;
    (*pDesc).Parameters = eff.parameters.len() as u32;
    (*pDesc).Functions = 0;
    S_OK
}

unsafe extern "system" fn effect_get_technique_desc(this: *mut c_void, hTechnique: D3DXHANDLE, pDesc: *mut D3DXTECHNIQUE_DESC) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::GetTechniqueDesc(handle={:p})", hTechnique));
    if pDesc.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hTechnique as usize;
    let idx = if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        h - HANDLE_TECHNIQUE_BASE
    } else if h > 0 && h <= eff.techniques.len() {
        h - 1
    } else {
        return windows::core::HRESULT(-2147467259);
    };

    if idx < eff.techniques.len() {
        let tech = &eff.techniques[idx];
        (*pDesc).Name = eff.technique_c_names[idx].as_ptr();
        (*pDesc).Passes = tech.passes.len() as u32;
        (*pDesc).Annotations = eff.technique_annotations[idx].len() as u32;
        S_OK
    } else {
        windows::core::HRESULT(-2147467259)
    }
}

unsafe extern "system" fn effect_get_pass_desc(this: *mut c_void, hPass: D3DXHANDLE, pDesc: *mut D3DXPASS_DESC) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::GetPassDesc(handle={:p})", hPass));
    if pDesc.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hPass as usize;
    let (t_idx, p_idx) = if h >= HANDLE_PASS_BASE && h < HANDLE_PARAM_BASE {
        let val = h - HANDLE_PASS_BASE;
        (val >> 8, val & 0xFF)
    } else if h > 0 {
        (eff.active_tech_idx, h - 1)
    } else {
        return windows::core::HRESULT(-2147467259);
    };

    if t_idx < eff.techniques.len() && p_idx < eff.techniques[t_idx].passes.len() {
        (*pDesc).Name = eff.pass_c_names[t_idx][p_idx].as_ptr();
        (*pDesc).Annotations = eff.pass_annotations[t_idx][p_idx].len() as u32;
        (*pDesc).pVertexShaderFunction = std::ptr::null();
        (*pDesc).pPixelShaderFunction = std::ptr::null();
        S_OK
    } else {
        windows::core::HRESULT(-2147467259)
    }
}

unsafe extern "system" fn effect_get_parameter_desc(this: *mut c_void, hParameter: D3DXHANDLE, pDesc: *mut D3DXPARAMETER_DESC) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::GetParameterDesc(handle={:p})", hParameter));
    if pDesc.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hParameter as usize;
    if h >= HANDLE_ANNO_BASE {
        let anno_idx = h - HANDLE_ANNO_BASE;
        if let Some(anno) = eff.annotations.get(anno_idx) {
            (*pDesc).Name = anno.c_name.as_ptr();
            (*pDesc).Semantic = null();
            (*pDesc).Class = 0;
            (*pDesc).Type = 4; // String
            (*pDesc).Rows = 1;
            (*pDesc).Columns = 1;
            (*pDesc).Elements = 0;
            (*pDesc).Annotations = 0;
            (*pDesc).StructMembers = 0;
            (*pDesc).Bytes = anno.value.len() as u32;
            return S_OK;
        }
    }

    let idx = if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE {
        h - HANDLE_PARAM_BASE
    } else if h > 0 && h <= eff.parameters.len() {
        h - 1
    } else {
        return windows::core::HRESULT(-2147467259);
    };

    if idx < eff.parameters.len() {
        let p = &eff.parameters[idx];
        let (class, ty, rows, columns) = parameter_type_shape(p);
        crate::log_msg(&format!(
            "Parameter desc {} semantic={} hlsl={} size={} => class={} type={} rows={} cols={}",
            p.name, p.semantic, p.hlsl_type, p.size, class, ty, rows, columns
        ));
        (*pDesc).Name = p.c_name.as_ptr();
        (*pDesc).Semantic = p.c_semantic.as_ptr();
        (*pDesc).Class = class;
        (*pDesc).Type = ty;
        (*pDesc).Rows = rows;
        (*pDesc).Columns = columns;
        (*pDesc).Elements = 0;
        (*pDesc).Annotations = p.annotation_indices.len() as u32;
        (*pDesc).StructMembers = 0;
        (*pDesc).Flags = 0;
        (*pDesc).Bytes = if class == 4 { 0 } else { p.size as u32 };
        S_OK
    } else {
        windows::core::HRESULT(-2147467259)
    }
}

fn parameter_type_shape(parameter: &EffectParameter) -> (u32, u32, u32, u32) {
    // D3DXPARAMETER_CLASS: scalar=0, vector=1, matrix rows=2,
    // matrix columns=3, object=4. D3DXPARAMETER_TYPE: bool=1, int=2,
    // float=3, texture2D=7, texture3D=8, textureCube=9.
    let ty = parameter.hlsl_type.to_ascii_lowercase();
    if ty.is_empty() {
        return if parameter.size >= 64 {
            (3, 3, 4, 4)
        } else if parameter.size >= 16 {
            (1, 3, 1, 4)
        } else {
            (0, 3, 1, 1)
        };
    }
    if ty.starts_with("texture") {
        let texture_type = if ty.contains("cube") { 9 } else if ty.contains("3d") { 8 } else { 7 };
        // MME validates texture semantics with Rows * Columns == 0.
        return (4, texture_type, 0, 0);
    }
    let base = ty.trim_end_matches(|c: char| c.is_ascii_digit());
    let scalar_type = match base {
        "bool" => 1,
        "int" | "uint" => 2,
        _ => 3,
    };
    if let Some((rows, columns)) = ty.split_once('x') {
        let row_digits: String = rows.chars().filter(|c| c.is_ascii_digit()).collect();
        let rows = row_digits.parse().unwrap_or(4);
        let columns = columns.parse().unwrap_or(4);
        return (if parameter.row_major { 2 } else { 3 }, scalar_type, rows, columns);
    }
    let columns = base.len().checked_sub(base.trim_end_matches(|c: char| c.is_ascii_digit()).len())
        .and_then(|_| ty.chars().last()?.to_digit(10))
        .unwrap_or(if parameter.size >= 16 { 4 } else { 1 });
    if columns > 1 {
        (1, scalar_type, 1, columns)
    } else {
        (0, scalar_type, 1, 1)
    }
}
unsafe extern "system" fn effect_get_parameter(this: *mut c_void, _hParameter: D3DXHANDLE, Index: u32) -> D3DXHANDLE {
    let eff = &*(this as *mut HikariEffect);
    if (Index as usize) < eff.parameters.len() {
        (HANDLE_PARAM_BASE + Index as usize) as D3DXHANDLE
    } else {
        null()
    }
}

unsafe extern "system" fn effect_get_parameter_by_name(this: *mut c_void, _hParameter: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE {
    let name = if pName.is_null() { "<null>".to_string() } else { CStr::from_ptr(pName).to_string_lossy().into_owned() };
    crate::log_msg(&format!("ID3DXEffect::GetParameterByName({name})"));
    if pName.is_null() {
        return null();
    }
    let eff = &*(this as *mut HikariEffect);
    if let Ok(name_str) = CStr::from_ptr(pName).to_str() {
        if let Some(&idx) = eff.param_by_name.get(&name_str.to_lowercase()) {
            return (HANDLE_PARAM_BASE + idx) as D3DXHANDLE;
        }
    }
    null()
}

unsafe extern "system" fn effect_get_parameter_by_semantic(this: *mut c_void, _hParameter: D3DXHANDLE, pSemantic: *const i8) -> D3DXHANDLE {
    let semantic = if pSemantic.is_null() { "<null>".to_string() } else { CStr::from_ptr(pSemantic).to_string_lossy().into_owned() };
    crate::log_msg(&format!("ID3DXEffect::GetParameterBySemantic({semantic})"));
    if pSemantic.is_null() {
        return null();
    }
    let eff = &*(this as *mut HikariEffect);
    if let Ok(sem_str) = CStr::from_ptr(pSemantic).to_str() {
        let upper = sem_str.to_uppercase();
        if let Some(&idx) = eff.param_by_semantic.get(&upper) {
            return (HANDLE_PARAM_BASE + idx) as D3DXHANDLE;
        }
        if let Some(&idx) = eff.param_by_semantic.get(sem_str) {
            return (HANDLE_PARAM_BASE + idx) as D3DXHANDLE;
        }
        if let Some(&idx) = eff.param_by_name.get(&sem_str.to_lowercase()) {
            return (HANDLE_PARAM_BASE + idx) as D3DXHANDLE;
        }
    }
    null()
}

unsafe extern "system" fn effect_get_technique(this: *mut c_void, Index: u32) -> D3DXHANDLE {
    let eff = &*(this as *mut HikariEffect);
    if (Index as usize) < eff.techniques.len() {
        (HANDLE_TECHNIQUE_BASE + Index as usize) as D3DXHANDLE
    } else {
        null()
    }
}

unsafe extern "system" fn effect_get_technique_by_name(this: *mut c_void, pName: *const i8) -> D3DXHANDLE {
    let name = if pName.is_null() { "<null>".to_string() } else { CStr::from_ptr(pName).to_string_lossy().into_owned() };
    crate::log_msg(&format!("ID3DXEffect::GetTechniqueByName({name})"));
    if pName.is_null() {
        return null();
    }
    let eff = &*(this as *mut HikariEffect);
    if let Ok(name_str) = CStr::from_ptr(pName).to_str() {
        for (i, t) in eff.techniques.iter().enumerate() {
            if t.name.eq_ignore_ascii_case(name_str) {
                return (HANDLE_TECHNIQUE_BASE + i) as D3DXHANDLE;
            }
        }
    }
    if !eff.techniques.is_empty() {
        HANDLE_TECHNIQUE_BASE as D3DXHANDLE
    } else {
        null()
    }
}

unsafe extern "system" fn effect_get_pass(this: *mut c_void, hTechnique: D3DXHANDLE, Index: u32) -> D3DXHANDLE {
    let eff = &*(this as *mut HikariEffect);
    let h = hTechnique as usize;
    let t_idx = if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        h - HANDLE_TECHNIQUE_BASE
    } else {
        eff.active_tech_idx
    };
    (HANDLE_PASS_BASE + ((t_idx << 8) | (Index as usize))) as D3DXHANDLE
}

unsafe extern "system" fn effect_get_pass_by_name(this: *mut c_void, hTechnique: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE {
    let name = if pName.is_null() { "<null>".to_string() } else { CStr::from_ptr(pName).to_string_lossy().into_owned() };
    crate::log_msg(&format!("ID3DXEffect::GetPassByName({name})"));
    if pName.is_null() {
        return null();
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hTechnique as usize;
    let t_idx = if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        h - HANDLE_TECHNIQUE_BASE
    } else {
        eff.active_tech_idx
    };
    if let Ok(name_str) = CStr::from_ptr(pName).to_str() {
        if let Some(tech) = eff.techniques.get(t_idx) {
            for (p_idx, p) in tech.passes.iter().enumerate() {
                if p.name.eq_ignore_ascii_case(name_str) {
                    return (HANDLE_PASS_BASE + ((t_idx << 8) | p_idx)) as D3DXHANDLE;
                }
            }
        }
    }
    (HANDLE_PASS_BASE + (t_idx << 8)) as D3DXHANDLE
}

unsafe extern "system" fn effect_get_annotation(this: *mut c_void, hObject: D3DXHANDLE, Index: u32) -> D3DXHANDLE {
    let eff = &*(this as *mut HikariEffect);
    let h = hObject as usize;
    let anno_list = if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE {
        let p_idx = h - HANDLE_PARAM_BASE;
        eff.parameters.get(p_idx).map(|p| &p.annotation_indices)
    } else if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        let t_idx = h - HANDLE_TECHNIQUE_BASE;
        eff.technique_annotations.get(t_idx)
    } else if h >= HANDLE_PASS_BASE && h < HANDLE_PARAM_BASE {
        let val = h - HANDLE_PASS_BASE;
        let t_idx = val >> 8;
        let p_idx = val & 0xFF;
        eff.pass_annotations.get(t_idx).and_then(|passes| passes.get(p_idx))
    } else if h > 0 && h <= eff.parameters.len() {
        eff.parameters.get(h - 1).map(|p| &p.annotation_indices)
    } else {
        None
    };

    if let Some(list) = anno_list {
        if let Some(&anno_idx) = list.get(Index as usize) {
            return (HANDLE_ANNO_BASE + anno_idx) as D3DXHANDLE;
        }
    }
    null()
}

unsafe extern "system" fn effect_get_annotation_by_name(this: *mut c_void, hObject: D3DXHANDLE, pName: *const i8) -> D3DXHANDLE {
    if pName.is_null() {
        return null();
    }
    let name_str = match CStr::from_ptr(pName).to_str() {
        Ok(s) => s,
        Err(_) => return null(),
    };
    let eff = &*(this as *mut HikariEffect);
    let h = hObject as usize;
    let anno_list = if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE {
        let p_idx = h - HANDLE_PARAM_BASE;
        eff.parameters.get(p_idx).map(|p| &p.annotation_indices)
    } else if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        let t_idx = h - HANDLE_TECHNIQUE_BASE;
        eff.technique_annotations.get(t_idx)
    } else if h >= HANDLE_PASS_BASE && h < HANDLE_PARAM_BASE {
        let val = h - HANDLE_PASS_BASE;
        let t_idx = val >> 8;
        let p_idx = val & 0xFF;
        eff.pass_annotations.get(t_idx).and_then(|passes| passes.get(p_idx))
    } else if h > 0 && h <= eff.parameters.len() {
        eff.parameters.get(h - 1).map(|p| &p.annotation_indices)
    } else {
        None
    };

    if let Some(list) = anno_list {
        for &anno_idx in list {
            if eff.annotations[anno_idx].name.eq_ignore_ascii_case(name_str) {
                return (HANDLE_ANNO_BASE + anno_idx) as D3DXHANDLE;
            }
        }
    }
    null()
}

unsafe extern "system" fn effect_get_string(this: *mut c_void, hParam: D3DXHANDLE, ppString: *mut *const i8) -> HRESULT {
    if ppString.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hParam as usize;
    if h >= HANDLE_ANNO_BASE {
        let anno_idx = h - HANDLE_ANNO_BASE;
        if let Some(anno) = eff.annotations.get(anno_idx) {
            *ppString = anno.c_val.as_ptr();
            return S_OK;
        }
    }
    windows::core::HRESULT(-2147467259)
}

unsafe extern "system" fn effect_set_technique(this: *mut c_void, hTechnique: D3DXHANDLE) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::SetTechnique(handle={:p})", hTechnique));
    let eff = &mut *(this as *mut HikariEffect);
    let h = hTechnique as usize;
    let idx = if h >= HANDLE_TECHNIQUE_BASE && h < HANDLE_PASS_BASE {
        h - HANDLE_TECHNIQUE_BASE
    } else if h > 0 {
        h - 1
    } else {
        0
    };
    if idx < eff.techniques.len() {
        eff.active_tech_idx = idx;
    }
    S_OK
}

unsafe extern "system" fn effect_get_current_technique(this: *mut c_void) -> D3DXHANDLE {
    let eff = &*(this as *mut HikariEffect);
    (HANDLE_TECHNIQUE_BASE + eff.active_tech_idx) as D3DXHANDLE
}

unsafe extern "system" fn effect_validate_technique(_this: *mut c_void, _hTechnique: D3DXHANDLE) -> HRESULT {
    S_OK
}

unsafe extern "system" fn effect_find_next_valid_technique(_this: *mut c_void, _hTechnique: D3DXHANDLE, pTechnique: *mut D3DXHANDLE) -> HRESULT {
    if !pTechnique.is_null() {
        *pTechnique = HANDLE_TECHNIQUE_BASE as D3DXHANDLE;
    }
    S_OK
}

unsafe extern "system" fn effect_is_parameter_used(_this: *mut c_void, _hParameter: D3DXHANDLE, _hTechnique: D3DXHANDLE) -> BOOL {
    true.into()
}

// Parameter setters & getters
unsafe extern "system" fn effect_set_value(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const c_void, Bytes: u32) -> HRESULT {
    if pData.is_null() || Bytes == 0 {
        return S_OK;
    }
    let eff = &mut *(this as *mut HikariEffect);
    let h = hParameter as usize;
    let idx = if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE {
        h - HANDLE_PARAM_BASE
    } else if h > 0 && h <= eff.parameters.len() {
        h - 1
    } else {
        return S_OK;
    };

    if idx < eff.parameters.len() {
        let p = &eff.parameters[idx];
        let copy_len = (Bytes as usize).min(p.size);
        if p.offset + copy_len <= eff.constant_data.len() {
            std::ptr::copy_nonoverlapping(
                pData as *const u8,
                eff.constant_data.as_mut_ptr().add(p.offset),
                copy_len,
            );
            eff.constant_dirty = true;
        }
    }
    S_OK
}

unsafe extern "system" fn effect_get_value(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut c_void, Bytes: u32) -> HRESULT {
    if pData.is_null() || Bytes == 0 {
        return windows::core::HRESULT(-2147467261);
    }
    let eff = &*(this as *mut HikariEffect);
    let h = hParameter as usize;
    let idx = if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE {
        h - HANDLE_PARAM_BASE
    } else if h > 0 && h <= eff.parameters.len() {
        h - 1
    } else {
        return windows::core::HRESULT(-2147467259);
    };

    if idx < eff.parameters.len() {
        let p = &eff.parameters[idx];
        let copy_len = (Bytes as usize).min(p.size);
        if p.offset + copy_len <= eff.constant_data.len() {
            std::ptr::copy_nonoverlapping(
                eff.constant_data.as_ptr().add(p.offset),
                pData as *mut u8,
                copy_len,
            );
            return S_OK;
        }
    }
    windows::core::HRESULT(-2147467259)
}

unsafe extern "system" fn effect_set_matrix(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX) -> HRESULT {
    effect_set_value(this, hParameter, pMatrix as *const c_void, std::mem::size_of::<D3DXMATRIX>() as u32)
}

unsafe extern "system" fn effect_get_matrix(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX) -> HRESULT {
    effect_get_value(this, hParameter, pMatrix as *mut c_void, std::mem::size_of::<D3DXMATRIX>() as u32)
}

/// Resolves a D3DXHANDLE to a parameter index, mirroring the setter logic.
unsafe fn resolve_parameter_index(effect: &HikariEffect, hParameter: D3DXHANDLE) -> Option<usize> {
    let h = hParameter as usize;
    if h >= HANDLE_PARAM_BASE && h < HANDLE_ANNO_BASE && (h - HANDLE_PARAM_BASE) < effect.parameters.len() {
        Some(h - HANDLE_PARAM_BASE)
    } else if h > 0 && h <= effect.parameters.len() {
        Some(h - 1)
    } else {
        None
    }
}

/// Copies a parameter-sized value into the constant buffer staging area.
unsafe fn set_param_block(this: *mut c_void, hParameter: D3DXHANDLE, src: *const c_void, bytes: usize) -> HRESULT {
    if src.is_null() || bytes == 0 {
        return S_OK;
    }
    let eff = &mut *(this as *mut HikariEffect);
    let Some(idx) = resolve_parameter_index(eff, hParameter) else {
        return S_OK;
    };
    let param = &eff.parameters[idx];
    let copy_len = bytes.min(param.size);
    if param.offset + copy_len <= eff.constant_data.len() {
        std::ptr::copy_nonoverlapping(
            src as *const u8,
            eff.constant_data.as_mut_ptr().add(param.offset),
            copy_len,
        );
        eff.constant_dirty = true;
    }
    S_OK
}

unsafe fn get_param_block(this: *mut c_void, hParameter: D3DXHANDLE, dst: *mut c_void, bytes: usize) -> HRESULT {
    if dst.is_null() || bytes == 0 {
        return E_POINTER;
    }
    let eff = &*(this as *mut HikariEffect);
    let Some(idx) = resolve_parameter_index(eff, hParameter) else {
        return E_FAIL;
    };
    let param = &eff.parameters[idx];
    let copy_len = bytes.min(param.size);
    if param.offset + copy_len <= eff.constant_data.len() {
        std::ptr::copy_nonoverlapping(
            eff.constant_data.as_ptr().add(param.offset),
            dst as *mut u8,
            copy_len,
        );
        return S_OK;
    }
    E_FAIL
}

/// Writes one transposed matrix into the constant buffer.
unsafe fn set_matrix_transposed(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX) -> HRESULT {
    if pMatrix.is_null() {
        return S_OK;
    }
    let mut transposed = *pMatrix;
    for row in 0..4 {
        for col in 0..4 {
            transposed.m[row][col] = (*pMatrix).m[col][row];
        }
    }
    set_param_block(this, hParameter, &transposed as *const _ as *const c_void, 64)
}

unsafe extern "system" fn effect_set_float_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const f32, Count: u32) -> HRESULT {
    set_param_block(this, hParameter, pData as *const c_void, (Count as usize).saturating_mul(4))
}

unsafe extern "system" fn effect_get_float_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut f32, Count: u32) -> HRESULT {
    get_param_block(this, hParameter, pData as *mut c_void, (Count as usize).saturating_mul(4))
}

unsafe extern "system" fn effect_set_int_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const i32, Count: u32) -> HRESULT {
    set_param_block(this, hParameter, pData as *const c_void, (Count as usize).saturating_mul(4))
}

unsafe extern "system" fn effect_get_int_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut i32, Count: u32) -> HRESULT {
    get_param_block(this, hParameter, pData as *mut c_void, (Count as usize).saturating_mul(4))
}

unsafe extern "system" fn effect_set_bool_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const BOOL, Count: u32) -> HRESULT {
    if pData.is_null() || Count == 0 {
        return S_OK;
    }
    // BOOL (c int) is stored as its 0/1 integer value.
    let values: Vec<i32> = std::slice::from_raw_parts(pData, Count as usize)
        .iter()
        .map(|b| b.as_bool() as i32)
        .collect();
    set_param_block(this, hParameter, values.as_ptr() as *const c_void, values.len() * 4)
}

unsafe extern "system" fn effect_get_bool_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut BOOL, Count: u32) -> HRESULT {
    get_param_block(this, hParameter, pData as *mut c_void, (Count as usize).saturating_mul(4))
}

unsafe extern "system" fn effect_set_vector_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const D3DXVECTOR4, Count: u32) -> HRESULT {
    set_param_block(this, hParameter, pData as *const c_void, (Count as usize).saturating_mul(std::mem::size_of::<D3DXVECTOR4>()))
}

unsafe extern "system" fn effect_get_vector_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut D3DXVECTOR4, Count: u32) -> HRESULT {
    get_param_block(this, hParameter, pData as *mut c_void, (Count as usize).saturating_mul(std::mem::size_of::<D3DXVECTOR4>()))
}

unsafe extern "system" fn effect_set_matrix_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const D3DXMATRIX, Count: u32) -> HRESULT {
    set_param_block(this, hParameter, pData as *const c_void, (Count as usize).saturating_mul(64))
}

unsafe extern "system" fn effect_get_matrix_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut D3DXMATRIX, Count: u32) -> HRESULT {
    get_param_block(this, hParameter, pData as *mut c_void, (Count as usize).saturating_mul(64))
}

unsafe extern "system" fn effect_set_matrix_pointer_array(this: *mut c_void, hParameter: D3DXHANDLE, ppData: *const *const D3DXMATRIX, Count: u32) -> HRESULT {
    if ppData.is_null() || Count == 0 {
        return S_OK;
    }
    let eff = &mut *(this as *mut HikariEffect);
    let Some(idx) = resolve_parameter_index(eff, hParameter) else {
        return S_OK;
    };
    for i in 0..(Count as usize).min(eff.parameters[idx].size / 64) {
        let mat = std::slice::from_raw_parts(ppData, i + 1)[i];
        if mat.is_null() {
            continue;
        }
        let src = *mat;
        if eff.parameters[idx].offset + (i + 1) * 64 <= eff.constant_data.len() {
            std::ptr::copy_nonoverlapping(
                &src as *const _ as *const u8,
                eff.constant_data.as_mut_ptr().add(eff.parameters[idx].offset + i * 64),
                64,
            );
        }
    }
    eff.constant_dirty = true;
    S_OK
}

unsafe extern "system" fn effect_get_matrix_pointer_array(this: *mut c_void, hParameter: D3DXHANDLE, ppData: *mut *mut D3DXMATRIX, Count: u32) -> HRESULT {
    if ppData.is_null() || Count == 0 {
        return E_POINTER;
    }
    let eff = &*(this as *mut HikariEffect);
    let Some(idx) = resolve_parameter_index(eff, hParameter) else {
        return E_FAIL;
    };
    for (i, dst) in std::slice::from_raw_parts_mut(ppData, Count as usize).iter().enumerate().take(eff.parameters[idx].size / 64) {
        if dst.is_null() {
            continue;
        }
        let mat = *dst;
        if eff.parameters[idx].offset + (i + 1) * 64 <= eff.constant_data.len() {
            std::ptr::copy_nonoverlapping(
                eff.constant_data.as_ptr().add(eff.parameters[idx].offset + i * 64),
                mat as *mut u8,
                64,
            );
        }
    }
    S_OK
}

unsafe extern "system" fn effect_set_matrix_transpose(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *const D3DXMATRIX) -> HRESULT {
    set_matrix_transposed(this, hParameter, pMatrix)
}

unsafe extern "system" fn effect_get_matrix_transpose(this: *mut c_void, hParameter: D3DXHANDLE, pMatrix: *mut D3DXMATRIX) -> HRESULT {
    if pMatrix.is_null() {
        return E_POINTER;
    }
    let hr = get_param_block(this, hParameter, pMatrix as *mut c_void, 64);
    if hr.is_ok() {
        let mut transposed = *pMatrix;
        for row in 0..4 {
            for col in 0..4 {
                transposed.m[row][col] = (*pMatrix).m[col][row];
            }
        }
        *pMatrix = transposed;
    }
    hr
}

unsafe extern "system" fn effect_set_matrix_transpose_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const D3DXMATRIX, Count: u32) -> HRESULT {
    if pData.is_null() || Count == 0 {
        return S_OK;
    }
    let eff = &mut *(this as *mut HikariEffect);
    let Some(idx) = resolve_parameter_index(eff, hParameter) else {
        return S_OK;
    };
    let matrices = std::slice::from_raw_parts(pData, Count as usize);
    let slots = (eff.parameters[idx].size / 64).min(matrices.len());
    for (i, mat) in matrices.iter().take(slots).enumerate() {
        let mut transposed = *mat;
        for row in 0..4 {
            for col in 0..4 {
                transposed.m[row][col] = (*mat).m[col][row];
            }
        }
        if eff.parameters[idx].offset + (i + 1) * 64 <= eff.constant_data.len() {
            std::ptr::copy_nonoverlapping(
                &transposed as *const _ as *const u8,
                eff.constant_data.as_mut_ptr().add(eff.parameters[idx].offset + i * 64),
                64,
            );
        }
    }
    eff.constant_dirty = true;
    S_OK
}

unsafe extern "system" fn effect_get_matrix_transpose_array(this: *mut c_void, hParameter: D3DXHANDLE, pData: *mut D3DXMATRIX, Count: u32) -> HRESULT {
    let hr = effect_get_matrix_array(this, hParameter, pData, Count);
    if hr.is_ok() && !pData.is_null() {
        for mat in std::slice::from_raw_parts_mut(pData, Count as usize) {
            let mut transposed = *mat;
            for row in 0..4 {
                for col in 0..4 {
                    transposed.m[row][col] = mat.m[col][row];
                }
            }
            *mat = transposed;
        }
    }
    hr
}

unsafe extern "system" fn effect_set_matrix_transpose_pointer_array(this: *mut c_void, hParameter: D3DXHANDLE, ppData: *const *const D3DXMATRIX, Count: u32) -> HRESULT {
    effect_set_matrix_pointer_array(this, hParameter, ppData, Count)
}

unsafe extern "system" fn effect_get_matrix_transpose_pointer_array(this: *mut c_void, hParameter: D3DXHANDLE, ppData: *mut *mut D3DXMATRIX, Count: u32) -> HRESULT {
    let hr = effect_get_matrix_pointer_array(this, hParameter, ppData, Count);
    if hr.is_ok() && !ppData.is_null() {
        for dst in std::slice::from_raw_parts_mut(ppData, Count as usize) {
            if dst.is_null() {
                continue;
            }
            let mat = *dst;
            let mut transposed = *mat;
            for row in 0..4 {
                for col in 0..4 {
                    transposed.m[row][col] = (*mat).m[col][row];
                }
            }
            *mat = transposed;
        }
    }
    hr
}

unsafe extern "system" fn effect_set_vector(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *const D3DXVECTOR4) -> HRESULT {
    effect_set_value(this, hParameter, pVector as *const c_void, std::mem::size_of::<D3DXVECTOR4>() as u32)
}

unsafe extern "system" fn effect_get_vector(this: *mut c_void, hParameter: D3DXHANDLE, pVector: *mut D3DXVECTOR4) -> HRESULT {
    effect_get_value(this, hParameter, pVector as *mut c_void, std::mem::size_of::<D3DXVECTOR4>() as u32)
}

unsafe extern "system" fn effect_set_float(this: *mut c_void, hParameter: D3DXHANDLE, f: f32) -> HRESULT {
    effect_set_value(this, hParameter, &f as *const f32 as *const c_void, 4)
}

unsafe extern "system" fn effect_get_float(this: *mut c_void, hParameter: D3DXHANDLE, pf: *mut f32) -> HRESULT {
    effect_get_value(this, hParameter, pf as *mut c_void, 4)
}

unsafe extern "system" fn effect_set_int(this: *mut c_void, hParameter: D3DXHANDLE, n: i32) -> HRESULT {
    effect_set_value(this, hParameter, &n as *const i32 as *const c_void, 4)
}

unsafe extern "system" fn effect_get_int(this: *mut c_void, hParameter: D3DXHANDLE, pn: *mut i32) -> HRESULT {
    effect_get_value(this, hParameter, pn as *mut c_void, 4)
}

unsafe extern "system" fn effect_set_bool(this: *mut c_void, hParameter: D3DXHANDLE, b: BOOL) -> HRESULT {
    let val: u32 = if b.as_bool() { 1 } else { 0 };
    effect_set_value(this, hParameter, &val as *const u32 as *const c_void, 4)
}

unsafe extern "system" fn effect_get_bool(this: *mut c_void, hParameter: D3DXHANDLE, pb: *mut BOOL) -> HRESULT {
    let mut int_val: i32 = 0;
    let hr = effect_get_value(this, hParameter, &mut int_val as *mut i32 as *mut c_void, 4);
    if hr.is_ok() && !pb.is_null() {
        *pb = (int_val != 0).into();
    }
    hr
}

unsafe extern "system" fn effect_set_texture(this: *mut c_void, hParameter: D3DXHANDLE, pTexture: *mut c_void) -> HRESULT {
    let eff = &mut *(this as *mut HikariEffect);
    let idx = (hParameter as usize).saturating_sub(1);
    let slot = if idx < eff.parameters.len() {
        let name = eff.parameters[idx].name.to_lowercase();
        eff.name_to_texture_slot.get(&name).copied().unwrap_or(idx as u32).min(15) as usize
    } else {
        (idx as u32).min(15) as usize
    };

    if pTexture.is_null() {
        eff.bound_texture_srvs[slot] = None;
        crate::vtable::BOUND_TEXTURES.lock().unwrap()[slot.min(7)] = None;
    } else {
        crate::vtable::BOUND_TEXTURES.lock().unwrap()[slot.min(7)] = Some(pTexture as usize);
        if let Ok(mut guard) = crate::vtable::GLOBAL_D3D11.lock() {
            if let Some(ref mut ctx) = *guard {
                if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(pTexture) {
                    eff.bound_texture_srvs[slot] = Some(srv);
                }
            }
        }
    }
    S_OK
}

unsafe extern "system" fn effect_set_raw_value(this: *mut c_void, hParameter: D3DXHANDLE, pData: *const c_void, _ByteOffset: u32, Bytes: u32) -> HRESULT {
    effect_set_value(this, hParameter, pData, Bytes)
}

// Pass Execution & Pipeline Binding
unsafe extern "system" fn effect_begin(this: *mut c_void, pPasses: *mut u32, _Flags: u32) -> HRESULT {
    static BEGIN_LOG_COUNT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let count = BEGIN_LOG_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
    let eff = &*(this as *mut HikariEffect);
    if count <= 5 || count % 300 == 0 {
        crate::log_msg(&format!(
            "ID3DXEffect::Begin call #{} (technique={}, passes={})",
            count,
            eff.active_tech_idx,
            eff.techniques.get(eff.active_tech_idx).map(|t| t.passes.len()).unwrap_or(0)
        ));
    }
    if !pPasses.is_null() {
        if eff.active_tech_idx < eff.techniques.len() {
            *pPasses = eff.techniques[eff.active_tech_idx].passes.len() as u32;
        } else {
            *pPasses = 1;
        }
    }
    S_OK
}

unsafe extern "system" fn effect_begin_pass(this: *mut c_void, Pass: u32) -> HRESULT {
    static BEGIN_PASS_LOG_COUNT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let call_no = BEGIN_PASS_LOG_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
    if call_no <= 5 || call_no % 300 == 0 {
        crate::log_msg(&format!("ID3DXEffect::BeginPass call #{} (pass={Pass})", call_no));
    }
    crate::vtable::DX11_FRAME_RENDERED.store(true, std::sync::atomic::Ordering::Release);
    let eff = &mut *(this as *mut HikariEffect);
    eff.active_pass_idx = Pass as usize;

    // Update dirty constant buffers before executing pass
    eff.update_gpu_constants();

    if !WVP_VALUE_LOGGED.swap(true, Ordering::Relaxed) {
        if let Some(parameter) = eff.parameters.iter().find(|p| p.semantic.eq_ignore_ascii_case("WORLDVIEWPROJECTION")) {
            let mut matrix = [0.0f32; 16];
            if parameter.offset + 64 <= eff.constant_data.len() {
                std::ptr::copy_nonoverlapping(
                    eff.constant_data.as_ptr().add(parameter.offset),
                    matrix.as_mut_ptr() as *mut u8,
                    64,
                );
            }
            crate::log_msg(&format!(
                "WORLDVIEWPROJECTION at BeginPass: offset={}, rows={:?}",
                parameter.offset,
                matrix.chunks_exact(4).collect::<Vec<_>>()
            ));
        } else {
            crate::log_msg("No WORLDVIEWPROJECTION parameter found at BeginPass");
        }
    }

    if eff.active_tech_idx < eff.compiled_techniques.len() {
        let tech = &eff.compiled_techniques[eff.active_tech_idx];
        if eff.active_pass_idx < tech.len() {
            let pass = &tech[eff.active_pass_idx];

            if let Some(ref bc) = pass.vs_bytecode {
                *ACTIVE_EFFECT_PASS.lock().unwrap() = Some(ActivePassInfo {
                    vs_bytecode: bc.clone(),
                    vs: pass.vs.clone(),
                    ps: pass.ps.clone(),
                    constant_buffer: eff.constant_buffer.clone(),
                    blend_preset: pass.blend_preset.clone(),
                    depth_preset: pass.depth_preset.clone(),
                    raster_preset: pass.raster_preset.clone(),
                });
            } else {
                *ACTIVE_EFFECT_PASS.lock().unwrap() = None;
            }

            // Bind compiled shaders
            eff.context.VSSetShader(pass.vs.as_ref(), None);
            eff.context.PSSetShader(pass.ps.as_ref(), None);
            eff.context.CSSetShader(pass.cs.as_ref(), None);
            eff.context.GSSetShader(pass.gs.as_ref(), None);
            eff.context.HSSetShader(pass.hs.as_ref(), None);
            eff.context.DSSetShader(pass.ds.as_ref(), None);

            // Bind parameter constant buffer to slot 0 across all shader stages
            if let Some(ref cb) = eff.constant_buffer {
                let cb_slice = [Some(cb.clone())];
                eff.context.VSSetConstantBuffers(0, Some(&cb_slice));
                eff.context.PSSetConstantBuffers(0, Some(&cb_slice));
                eff.context.CSSetConstantBuffers(0, Some(&cb_slice));
                eff.context.GSSetConstantBuffers(0, Some(&cb_slice));
                eff.context.HSSetConstantBuffers(0, Some(&cb_slice));
                eff.context.DSSetConstantBuffers(0, Some(&cb_slice));
            }
            // Bind all active textures across VS, PS, CS
            for slot in 0..16 {
                if let Some(ref srv) = eff.bound_texture_srvs[slot] {
                    let srv_slice = [Some(srv.clone())];
                    eff.context.VSSetShaderResources(slot as u32, Some(&srv_slice));
                    eff.context.PSSetShaderResources(slot as u32, Some(&srv_slice));
                    eff.context.CSSetShaderResources(slot as u32, Some(&srv_slice));
                }
            }

            // Apply pipeline states from StateManager
            if let Ok(guard) = crate::vtable::GLOBAL_D3D11.lock() {
                if let Some(ref ctx) = *guard {
                    if let Some(blend) = ctx.state_manager.get_blend(pass.blend_preset.clone()) {
                        eff.context.OMSetBlendState(Some(blend), Some(&[0.0, 0.0, 0.0, 0.0]), 0xFFFFFFFF);
                    }
                    if let Some(depth) = ctx.state_manager.get_depth(pass.depth_preset.clone()) {
                        eff.context.OMSetDepthStencilState(Some(depth), 1);
                    }
                    if let Some(raster) = ctx.state_manager.get_rasterizer(pass.raster_preset.clone()) {
                        eff.context.RSSetState(Some(raster));
                    }
                    if let Some(sampler) = ctx.state_manager.get_sampler(crate::states::SamplerPreset::LinearWrap) {
                        let samplers = [Some(sampler.clone())];
                        eff.context.PSSetSamplers(0, Some(&samplers));
                        eff.context.VSSetSamplers(0, Some(&samplers));
                    }
                }
            }

            // If compute shader is present in pass, auto-dispatch grid
            if let Some(ref cs) = pass.cs {
                eff.context.CSSetShader(Some(cs), None);
                let gx = pass.thread_group_size[0].max(1);
                let gy = pass.thread_group_size[1].max(1);
                let gz = pass.thread_group_size[2].max(1);
                eff.context.Dispatch(gx, gy, gz);
            }
        }
    }

    S_OK
}

unsafe extern "system" fn effect_commit_changes(this: *mut c_void) -> HRESULT {
    let eff = &mut *(this as *mut HikariEffect);
    eff.update_gpu_constants();
    S_OK
}

unsafe extern "system" fn effect_end_pass(this: *mut c_void) -> HRESULT {
    *ACTIVE_EFFECT_PASS.lock().unwrap() = None;
    let eff = &*(this as *mut HikariEffect);
    // Unbind UAVs to avoid hazards
    eff.context.CSSetUnorderedAccessViews(0, 0, None, None);
    S_OK
}

unsafe extern "system" fn effect_end(_this: *mut c_void) -> HRESULT {
    *ACTIVE_EFFECT_PASS.lock().unwrap() = None;
    S_OK
}

unsafe extern "system" fn effect_get_device(_this: *mut c_void, ppDevice: *mut *mut c_void) -> HRESULT {
    static LOGGED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    if !LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg("ID3DXEffect::GetDevice called");
    }
    if !ppDevice.is_null() {
        *ppDevice = null_mut();
    }
    S_OK
}

/// Creates a full copy of the compiled HikariEffect: same virtual table and
/// compiled pipelines, a separate constant buffer and copied constant data.
unsafe fn clone_hikari_effect(source: *mut HikariEffect) -> Result<*mut HikariEffect, String> {
    let effect = &*source;
    let cb_size = effect.constant_data.len();
    let mut constant_buffer: Option<ID3D11Buffer> = None;
    effect
        .device
        .CreateBuffer(
            &windows::Win32::Graphics::Direct3D11::D3D11_BUFFER_DESC {
                ByteWidth: (((cb_size + 15) / 16) * 16) as u32,
                Usage: windows::Win32::Graphics::Direct3D11::D3D11_USAGE_DYNAMIC,
                BindFlags: windows::Win32::Graphics::Direct3D11::D3D11_BIND_CONSTANT_BUFFER.0 as u32,
                CPUAccessFlags: 0x00010000, // D3D11_CPU_ACCESS_WRITE
                ..Default::default()
            },
            None,
            Some(&mut constant_buffer),
        )
        .map_err(|e| format!("CreateBuffer failed: {e}"))?;

    let cloned = Box::new(HikariEffect {
        vtbl: effect.vtbl,
        ref_count: AtomicU32::new(1),
        techniques: effect.techniques.clone(),
        compiled_techniques: effect.compiled_techniques.clone(),
        active_tech_idx: effect.active_tech_idx,
        active_pass_idx: effect.active_pass_idx,
        technique_c_names: effect.technique_c_names.clone(),
        pass_c_names: effect.pass_c_names.clone(),
        technique_annotations: effect.technique_annotations.clone(),
        pass_annotations: effect.pass_annotations.clone(),
        annotations: effect.annotations.clone(),
        parameters: effect.parameters.clone(),
        param_by_name: effect.param_by_name.clone(),
        param_by_semantic: effect.param_by_semantic.clone(),
        name_to_texture_slot: effect.name_to_texture_slot.clone(),
        bound_texture_srvs: effect.bound_texture_srvs.clone(),
        constant_buffer,
        constant_data: effect.constant_data.clone(),
        constant_dirty: true,
        device: effect.device.clone(),
        context: effect.context.clone(),
    });
    Ok(Box::into_raw(cloned))
}

unsafe extern "system" fn effect_clone_effect(this: *mut c_void, _pDevice: *mut c_void, ppEffect: *mut *mut c_void) -> HRESULT {
    crate::log_msg(&format!("ID3DXEffect::CloneEffect(this={:p})", this));
    if ppEffect.is_null() {
        return E_POINTER;
    }
    let cloned = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        clone_hikari_effect(this as *mut HikariEffect)
    }));
    match cloned {
        Ok(Ok(ptr)) => {
            crate::log_msg(&format!("Cloned HikariEffect {:p} -> {:p}", this, ptr));
            *ppEffect = ptr as *mut c_void;
            S_OK
        }
        Ok(Err(error)) => {
            crate::log_msg(&format!("HikariEffect clone failed: {error}"));
            E_FAIL
        }
        Err(_) => {
            crate::log_msg("HikariEffect clone panicked");
            E_FAIL
        }
    }
}

// Typed stubs for unused ID3DXEffect methods
unsafe extern "system" fn effect_dummy_ok(_this: *mut c_void) -> HRESULT { S_OK }
unsafe extern "system" fn effect_dummy_func_desc(_this: *mut c_void, _hShader: D3DXHANDLE, _pDesc: *mut c_void) -> HRESULT { S_OK }
unsafe extern "system" fn effect_dummy_func_index(_this: *mut c_void, _Index: u32) -> D3DXHANDLE { null() }
unsafe extern "system" fn effect_dummy_func_name(_this: *mut c_void, _pName: *const i8) -> D3DXHANDLE { null() }
unsafe extern "system" fn effect_dummy_handle_index(_this: *mut c_void, _hObject: D3DXHANDLE, _Index: u32) -> D3DXHANDLE { null() }
unsafe extern "system" fn effect_dummy_handle_name(_this: *mut c_void, _hObject: D3DXHANDLE, _pName: *const i8) -> D3DXHANDLE { null() }
unsafe extern "system" fn effect_dummy_handle_block(_this: *mut c_void) -> D3DXHANDLE { null() }
unsafe extern "system" fn effect_dummy_apply_block(_this: *mut c_void, _hBlock: D3DXHANDLE) -> HRESULT { S_OK }
unsafe extern "system" fn effect_dummy_setstatemanager(_this: *mut c_void, _pManager: *mut c_void) -> HRESULT { S_OK }
unsafe extern "system" fn effect_dummy_getstatemanager(_this: *mut c_void, _ppManager: *mut *mut c_void) -> HRESULT { unimplemented_stub("GetStateManager") }
unsafe extern "system" fn effect_dummy_setarrayrange(_this: *mut c_void, _hParam: D3DXHANDLE, _uStart: u32, _uEnd: u32) -> HRESULT { S_OK }
unsafe extern "system" fn effect_dummy_setstring(_this: *mut c_void, _hParam: D3DXHANDLE, _pStr: *const i8) -> HRESULT { S_OK }

unsafe extern "system" fn effect_dummy_gettexture(_this: *mut c_void, _hParam: D3DXHANDLE, _ppTexture: *mut *mut c_void) -> HRESULT { unimplemented_stub("GetTexture") }
unsafe extern "system" fn effect_dummy_getpixelshader(_this: *mut c_void, _hParam: D3DXHANDLE, _ppPShader: *mut *mut c_void) -> HRESULT { unimplemented_stub("GetPixelShader") }
unsafe extern "system" fn effect_dummy_getvertexshader(_this: *mut c_void, _hParam: D3DXHANDLE, _ppVShader: *mut *mut c_void) -> HRESULT { unimplemented_stub("GetVertexShader") }
unsafe extern "system" fn effect_dummy_getpool(_this: *mut c_void, _ppPool: *mut *mut c_void) -> HRESULT { unimplemented_stub("GetPool") }

