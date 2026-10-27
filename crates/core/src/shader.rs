//! Native DirectX 11 / Shader Model 5.0 HLSL compiler and reflection engine for Hikari.

use std::collections::HashMap;
use std::ffi::{c_void, CString};
use std::ptr::{null, null_mut};
use std::sync::OnceLock;
use windows::core::HRESULT;
use crate::states::{BlendPreset, DepthPreset, RasterizerPreset};
use windows::Win32::Foundation::HMODULE;
use windows::Win32::Graphics::Direct3D::ID3DBlob;
use windows::Win32::Graphics::Direct3D11::{
    ID3D11ComputeShader, ID3D11DomainShader, ID3D11GeometryShader,
    ID3D11HullShader, ID3D11PixelShader, ID3D11VertexShader,
};
use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};

type PfnD3DCompile = unsafe extern "system" fn(
    pSrcData: *const c_void,
    SrcDataSize: usize,
    pSourceName: *const i8,
    pDefines: *const c_void,
    pInclude: *mut c_void,
    pEntrypoint: *const i8,
    pTarget: *const i8,
    Flags1: u32,
    Flags2: u32,
    ppCode: *mut Option<ID3DBlob>,
    ppErrorMsgs: *mut Option<ID3DBlob>,
) -> HRESULT;

type PfnD3DDisassemble = unsafe extern "system" fn(
    pSrcData: *const c_void,
    SrcDataSize: usize,
    Flags: u32,
    szComments: *const i8,
    ppDisassembly: *mut Option<ID3DBlob>,
) -> HRESULT;

static COMPILER_FN: OnceLock<Option<PfnD3DCompile>> = OnceLock::new();
static DISASSEMBLER_FN: OnceLock<Option<PfnD3DDisassemble>> = OnceLock::new();

pub fn disassemble_dxbc(bytecode: &[u8]) -> Result<Vec<u8>, String> {
    let disassemble = (*DISASSEMBLER_FN.get_or_init(|| unsafe {
        let name: Vec<u16> = "d3dcompiler_47.dll\0".encode_utf16().collect();
        let module = LoadLibraryW(windows::core::PCWSTR(name.as_ptr())).unwrap_or(HMODULE(null_mut()));
        if module.0.is_null() {
            return None;
        }
        GetProcAddress(module, windows::core::s!("D3DDisassemble"))
            .map(|proc| std::mem::transmute::<_, PfnD3DDisassemble>(proc))
    }))
    .ok_or_else(|| "D3DDisassemble is unavailable".to_string())?;

    let mut blob: Option<ID3DBlob> = None;
    let hr = unsafe {
        disassemble(bytecode.as_ptr() as *const c_void, bytecode.len(), 0, null(), &mut blob)
    };
    if hr.is_err() {
        return Err(format!("D3DDisassemble failed: 0x{:08X}", hr.0));
    }
    let blob = blob.ok_or_else(|| "D3DDisassemble returned no output".to_string())?;
    unsafe {
        let ptr = blob.GetBufferPointer() as *const u8;
        Ok(std::slice::from_raw_parts(ptr, blob.GetBufferSize()).to_vec())
    }
}

fn get_d3dcompile() -> Option<PfnD3DCompile> {
    *COMPILER_FN.get_or_init(|| {
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
            let proc = GetProcAddress(hmod, windows::core::s!("D3DCompile"));
            proc.map(|p| std::mem::transmute::<_, PfnD3DCompile>(p))
        }
    })
}

/// Represents a compiled DirectX 11 shader stage.
pub enum CompiledShaderStage {
    Vertex(ID3D11VertexShader),
    Pixel(ID3D11PixelShader),
    Compute(ID3D11ComputeShader),
    Geometry(ID3D11GeometryShader),
    Hull(ID3D11HullShader),
    Domain(ID3D11DomainShader),
}

/// Description of an active pass within a technique.
#[derive(Debug, Clone)]
pub struct PassDesc {
    pub name: String,
    pub annotations: HashMap<String, String>,
    pub vs_entry: Option<(String, String)>, // (entry_point, target e.g. "vs_5_0")
    pub ps_entry: Option<(String, String)>,
    pub cs_entry: Option<(String, String)>,
    pub gs_entry: Option<(String, String)>,
    pub hs_entry: Option<(String, String)>,
    pub ds_entry: Option<(String, String)>,
    pub blend_preset: BlendPreset,
    pub depth_preset: DepthPreset,
    pub raster_preset: RasterizerPreset,
}

impl Default for PassDesc {
    fn default() -> Self {
        Self {
            name: String::new(),
            annotations: HashMap::new(),
            vs_entry: None,
            ps_entry: None,
            cs_entry: None,
            gs_entry: None,
            hs_entry: None,
            ds_entry: None,
            blend_preset: BlendPreset::Opaque,
            depth_preset: DepthPreset::ReadWriteLessEqual,
            raster_preset: RasterizerPreset::CullBackSolid,
        }
    }
}

/// Description of a technique containing one or more passes.
#[derive(Debug, Clone, Default)]
pub struct TechniqueDesc {
    pub name: String,
    pub annotations: HashMap<String, String>,
    pub passes: Vec<PassDesc>,
}

/// Description of a global variable or texture declaration with annotations.
#[derive(Debug, Clone, Default)]
pub struct GlobalVarDesc {
    pub name: String,
    pub semantic: String,
    pub annotations: HashMap<String, String>,
}

/// Parses HLSL annotation blocks formatted as `< string MMDPass = "object"; ... >`
pub fn parse_annotations_block(block: &str) -> HashMap<String, String> {
    let mut map = HashMap::new();
    let start = match block.find('<') {
        Some(idx) => idx + 1,
        None => 0,
    };
    let end = match block.rfind('>') {
        Some(idx) => idx,
        None => block.len(),
    };
    if start >= end {
        return map;
    }
    let inner = &block[start..end];

    let mut statements = Vec::new();
    let mut current_stmt = String::new();
    let mut in_quotes = false;
    let mut quote_char = '"';
    let mut escaped = false;

    for c in inner.chars() {
        if in_quotes {
            current_stmt.push(c);
            if escaped {
                escaped = false;
            } else if c == '\\' {
                escaped = true;
            } else if c == quote_char {
                in_quotes = false;
            }
        } else if c == '"' || c == '\'' {
            in_quotes = true;
            quote_char = c;
            current_stmt.push(c);
        } else if c == ';' {
            let trimmed = current_stmt.trim();
            if !trimmed.is_empty() {
                statements.push(trimmed.to_string());
            }
            current_stmt.clear();
        } else {
            current_stmt.push(c);
        }
    }
    let trimmed = current_stmt.trim();
    if !trimmed.is_empty() {
        statements.push(trimmed.to_string());
    }

    for stmt in statements {
        if let Some((lhs, rhs)) = stmt.split_once('=') {
            let key = lhs.split_whitespace().last().unwrap_or("").trim();
            let mut val = rhs.trim();
            if (val.starts_with('"') && val.ends_with('"')) || (val.starts_with('\'') && val.ends_with('\'')) {
                if val.len() >= 2 {
                    val = &val[1..val.len() - 1];
                }
            }
            if !key.is_empty() {
                map.insert(key.to_string(), val.to_string());
            }
        }
    }
    map
}

/// Extracts global variables/textures with annotations from HLSL source.
pub fn parse_global_variables(source: &str) -> Vec<GlobalVarDesc> {
    let mut list = Vec::new();
    let mut in_block = false;
    let mut current_decl = String::new();

    for line in source.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.starts_with('#') || trimmed.is_empty() {
            continue;
        }

        if !in_block {
            if trimmed.contains('<') && !trimmed.starts_with("technique") && !trimmed.starts_with("pass") && !trimmed.starts_with("cbuffer") {
                current_decl.clear();
                current_decl.push_str(trimmed);
                if trimmed.contains('>') {
                    if let Some(desc) = parse_single_global_var(&current_decl) {
                        list.push(desc);
                    }
                    current_decl.clear();
                } else {
                    in_block = true;
                }
            }
        } else {
            current_decl.push(' ');
            current_decl.push_str(trimmed);
            if trimmed.contains('>') {
                in_block = false;
                if let Some(desc) = parse_single_global_var(&current_decl) {
                    list.push(desc);
                }
                current_decl.clear();
            }
        }
    }
    list
}

fn parse_single_global_var(decl: &str) -> Option<GlobalVarDesc> {
    let (prefix, rest) = decl.split_once('<')?;
    let (anno_str, _) = rest.split_once('>')?;

    let prefix = prefix.trim();
    let (var_part, semantic) = if let Some((v, s)) = prefix.split_once(':') {
        (v.trim(), s.trim().to_string())
    } else {
        (prefix, String::new())
    };

    let name = var_part.split_whitespace().last()?.trim().to_string();
    if name.is_empty() {
        return None;
    }

    Some(GlobalVarDesc {
        name,
        semantic,
        annotations: parse_annotations_block(anno_str),
    })
}

/// Extracts mapping of variable names to their declared HLSL semantics (e.g. WorldViewProj -> WORLDVIEWPROJECTION).
pub fn parse_variable_semantics(source: &str) -> HashMap<String, String> {
    let mut map = HashMap::new();
    let mut in_struct = false;

    for line in source.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.starts_with('#') || trimmed.is_empty() {
            continue;
        }

        if trimmed.starts_with("struct") {
            in_struct = true;
        }
        if in_struct {
            if trimmed.contains('}') {
                in_struct = false;
            }
            continue;
        }

        if trimmed.starts_with("technique") || trimmed.starts_with("pass") || trimmed.contains('(') {
            continue;
        }

        if let Some((lhs, rhs)) = trimmed.split_once(':') {
            let var_name = lhs.split_whitespace().last().unwrap_or("").trim_matches('*');
            let sem_part = rhs.split(['<', ';', '=']).next().unwrap_or("").trim();
            if !var_name.is_empty() && !sem_part.is_empty() {
                if !sem_part.starts_with("register") && !sem_part.starts_with("packoffset") {
                    map.insert(var_name.to_lowercase(), sem_part.to_uppercase());
                }
            }
        }
    }
    map
}

/// Extracts basic HLSL variable types and matrix packing from declarations.
pub fn parse_variable_types(source: &str) -> HashMap<String, (String, bool)> {
    let mut map = HashMap::new();
    for line in source.lines() {
        let line = line.split("//").next().unwrap_or("").trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let declaration = line.split([':', '<', ';', '=']).next().unwrap_or("").trim();
        let parts: Vec<&str> = declaration.split_whitespace().collect();
        if parts.len() < 2 {
            continue;
        }
        let name = parts[parts.len() - 1].trim_end_matches('*');
        let ty = parts[parts.len() - 2];
        if name.is_empty() || ty.is_empty() || name == "{" || name == "}" {
            continue;
        }
        map.insert(name.to_lowercase(), (ty.to_string(), parts[..parts.len() - 2].contains(&"row_major")));
    }
    map
}


/// Metadata about a variable located inside a cbuffer.
#[derive(Debug, Clone)]
pub struct CBufferVariableDesc {
    pub name: String,
    pub offset: usize,
    pub size: usize,
}

/// Represents a parsed Constant Buffer layout.
#[derive(Debug, Clone)]
pub struct CBufferLayout {
    pub name: String,
    pub slot: u32,
    pub size: usize,
    pub variables: HashMap<String, CBufferVariableDesc>,
}

/// Compiles HLSL 5.0 source code to bytecode using system d3dcompiler.
pub fn compile_hlsl(
    source: &str,
    entry_point: &str,
    target: &str,
    source_name: Option<&str>,
    defines: &[(&str, &str)],
) -> Result<Vec<u8>, String> {
    // Legacy D3D9 profiles (vs_2_0..ps_3_0) are compiled as-is: Hikari
    // sometimes hands D3D9-shaped stub bytecode to the native MME/D3D9
    // pipeline, and rewriting it to SM5 recreates the DXBC-in-D3D9 crash.
    let compile_target = target.to_string();

    let compile_fn = get_d3dcompile().ok_or_else(|| "d3dcompiler_47.dll not found on system".to_string())?;

    let c_source = source.as_bytes();
    let c_entry = CString::new(entry_point).map_err(|e| e.to_string())?;
    let c_target = CString::new(compile_target.as_str()).map_err(|e| e.to_string())?;
    let c_source_name = CString::new(source_name.unwrap_or("hikari_shader.hlsl"))
        .unwrap_or_else(|_| CString::new("hikari_shader.hlsl").unwrap());

    // Prepare macro defines
    #[repr(C)]
    struct D3D_SHADER_MACRO {
        Name: *const i8,
        Definition: *const i8,
    }

    let mut macro_storage: Vec<(CString, CString)> = Vec::new();
    let mut c_macros: Vec<D3D_SHADER_MACRO> = Vec::new();

    // Default macro: __HIKARI__ = 1
    macro_storage.push((CString::new("__HIKARI__").unwrap(), CString::new("1").unwrap()));
    macro_storage.push((CString::new("DIRECTX_11").unwrap(), CString::new("1").unwrap()));
    macro_storage.push((CString::new("SHADER_MODEL_5_0").unwrap(), CString::new("1").unwrap()));

    for (k, v) in defines {
        if let (Ok(ck), Ok(cv)) = (CString::new(*k), CString::new(*v)) {
            macro_storage.push((ck, cv));
        }
    }

    for (k, v) in &macro_storage {
        c_macros.push(D3D_SHADER_MACRO {
            Name: k.as_ptr(),
            Definition: v.as_ptr(),
        });
    }
    // Null-terminator macro
    c_macros.push(D3D_SHADER_MACRO {
        Name: null(),
        Definition: null(),
    });

    let flags1 = 0x00000001 | 0x00008000; // D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL3
    let mut code_blob: Option<ID3DBlob> = None;
    let mut error_blob: Option<ID3DBlob> = None;
    const D3D_COMPILE_STANDARD_FILE_INCLUDE: *mut c_void = 1 as *mut c_void;

    let hr = unsafe {
        compile_fn(
            c_source.as_ptr() as *const c_void,
            c_source.len(),
            c_source_name.as_ptr(),
            c_macros.as_ptr() as *const c_void,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            c_entry.as_ptr(),
            c_target.as_ptr(),
            flags1,
            0,
            &mut code_blob,
            &mut error_blob,
        )
    };

    if hr.is_err() {
        let compiler_error = if let Some(errors) = error_blob {
            unsafe {
                let ptr = errors.GetBufferPointer() as *const u8;
                let len = errors.GetBufferSize();
                let slice = std::slice::from_raw_parts(ptr, len);
                String::from_utf8_lossy(slice).trim_end_matches('\0').to_string()
            }
        } else {
            format!("D3DCompile failed with HRESULT: 0x{:08X}", hr.0)
        };
        return Err(compiler_error);
    }

    let blob = code_blob.ok_or_else(|| "D3DCompile returned no bytecode".to_string())?;
    unsafe {
        let ptr = blob.GetBufferPointer() as *const u8;
        let len = blob.GetBufferSize();
        Ok(std::slice::from_raw_parts(ptr, len).to_vec())
    }
}

/// Parses techniques and passes from HLSL source code.
/// Supports standard syntax:
/// ```text
/// technique TechniqueName {
///     pass P0 {
///         VertexShader = compile vs_5_0 VS_Main();
///         PixelShader = compile ps_5_0 PS_Main();
///         ComputeShader = compile cs_5_0 CS_Main();
///     }
/// }
/// ```
pub fn parse_techniques(source: &str) -> Vec<TechniqueDesc> {
    let mut techniques = Vec::new();
    let mut current_tech: Option<TechniqueDesc> = None;
    let mut current_pass: Option<PassDesc> = None;
    let mut tech_anno_buf: Option<String> = None;
    let mut pass_anno_buf: Option<String> = None;

    for line in source.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.is_empty() {
            continue;
        }

        // Buffer multi-line technique annotations
        if let Some(ref mut buf) = tech_anno_buf {
            buf.push(' ');
            buf.push_str(trimmed);
            if trimmed.contains('>') {
                if let Some(ref mut tech) = current_tech {
                    tech.annotations = parse_annotations_block(buf);
                }
                tech_anno_buf = None;
            }
            continue;
        }

        // Buffer multi-line pass annotations
        if let Some(ref mut buf) = pass_anno_buf {
            buf.push(' ');
            buf.push_str(trimmed);
            if trimmed.contains('>') {
                if let Some(ref mut pass) = current_pass {
                    pass.annotations = parse_annotations_block(buf);
                }
                pass_anno_buf = None;
            }
            continue;
        }

        // Detect technique definition
        if trimmed.starts_with("technique") || trimmed.starts_with("technique11") {
            let after = if trimmed.starts_with("technique11") {
                &trimmed["technique11".len()..]
            } else {
                &trimmed["technique".len()..]
            }.trim();

            let (name_part, rest) = if let Some((n, r)) = after.split_once('<') {
                (n.trim(), Some(format!("<{r}")))
            } else if let Some((n, _)) = after.split_once('{') {
                (n.trim(), None)
            } else {
                (after.split_whitespace().next().unwrap_or("MainTec"), None)
            };

            let name = name_part.trim().to_string();
            let mut tech = TechniqueDesc {
                name,
                annotations: HashMap::new(),
                passes: Vec::new(),
            };

            if let Some(r) = rest {
                if let Some(end_idx) = r.find('>') {
                    let anno_str = &r[..=end_idx];
                    tech.annotations = parse_annotations_block(anno_str);
                } else {
                    tech_anno_buf = Some(r);
                }
            }

            current_tech = Some(tech);
            continue;
        }

        // Detect pass definition
        if let Some(ref mut tech) = current_tech {
            if trimmed.starts_with("pass") {
                let after = trimmed["pass".len()..].trim();
                let (name_part, rest) = if let Some((n, r)) = after.split_once('<') {
                    (n.trim(), Some(format!("<{r}")))
                } else if let Some((n, _)) = after.split_once('{') {
                    (n.trim(), None)
                } else {
                    (after.split_whitespace().next().unwrap_or("P0"), None)
                };

                let name = if name_part.is_empty() {
                    format!("P{}", tech.passes.len())
                } else {
                    name_part.to_string()
                };

                let mut pass = PassDesc {
                    name,
                    ..Default::default()
                };

                if let Some(r) = rest {
                    if let Some(end_idx) = r.find('>') {
                        let anno_str = &r[..=end_idx];
                        pass.annotations = parse_annotations_block(anno_str);
                    } else {
                        pass_anno_buf = Some(r);
                    }
                }

                current_pass = Some(pass);
                continue;
            }

            // Check if pass has annotation on subsequent line
            if let Some(ref mut pass) = current_pass {
                if trimmed.starts_with('<') && pass.annotations.is_empty() {
                    if let Some(end_idx) = trimmed.find('>') {
                        pass.annotations = parse_annotations_block(&trimmed[..=end_idx]);
                    } else {
                        pass_anno_buf = Some(trimmed.to_string());
                    }
                    continue;
                }
            }

            // Detect shader assignments inside pass
            if let Some(ref mut pass) = current_pass {
                if trimmed.contains("VertexShader") {
                    pass.vs_entry = parse_shader_stage(trimmed, "vs_5_0");
                } else if trimmed.contains("PixelShader") {
                    pass.ps_entry = parse_shader_stage(trimmed, "ps_5_0");
                } else if trimmed.contains("ComputeShader") {
                    pass.cs_entry = parse_shader_stage(trimmed, "cs_5_0");
                } else if trimmed.contains("GeometryShader") {
                    pass.gs_entry = parse_shader_stage(trimmed, "gs_5_0");
                } else if trimmed.contains("HullShader") {
                    pass.hs_entry = parse_shader_stage(trimmed, "hs_5_0");
                } else if trimmed.contains("DomainShader") {
                    pass.ds_entry = parse_shader_stage(trimmed, "ds_5_0");
                }

                // Detect pipeline state overrides in pass
                let upper = trimmed.to_uppercase();
                if upper.contains("ALPHABLENDENABLE") || upper.contains("BLENDENABLE") {
                    if upper.contains("TRUE") {
                        pass.blend_preset = BlendPreset::AlphaBlend;
                    }
                } else if upper.contains("DESTBLEND") && upper.contains("ONE") {
                    pass.blend_preset = BlendPreset::Additive;
                } else if upper.contains("ZENABLE") || upper.contains("DEPTHENABLE") {
                    if upper.contains("FALSE") {
                        pass.depth_preset = DepthPreset::Disabled;
                    }
                } else if upper.contains("ZWRITEENABLE") || upper.contains("DEPTHWRITE") {
                    if upper.contains("FALSE") {
                        pass.depth_preset = DepthPreset::ReadOnlyLessEqual;
                    }
                } else if upper.contains("FILLMODE") && upper.contains("WIREFRAME") {
                    pass.raster_preset = RasterizerPreset::Wireframe;
                } else if upper.contains("CULLMODE") {
                    if upper.contains("NONE") {
                        pass.raster_preset = RasterizerPreset::CullNoneSolid;
                    } else if upper.contains("CW") || upper.contains("FRONT") {
                        pass.raster_preset = RasterizerPreset::CullFrontSolid;
                    } else {
                        pass.raster_preset = RasterizerPreset::CullBackSolid;
                    }
                }

                if trimmed.contains('}') {
                    tech.passes.push(pass.clone());
                    current_pass = None;
                }
            } else if trimmed.contains('}') {
                techniques.push(tech.clone());
                current_tech = None;
            }
        }
    }

    // Default fallback technique if none explicitly defined
    if techniques.is_empty() {
        let mut default_pass = PassDesc {
            name: "DefaultPass".to_string(),
            ..Default::default()
        };

        if source.contains("VS_Main") || source.contains("VS") {
            default_pass.vs_entry = Some(("VS_Main".to_string(), "vs_5_0".to_string()));
        }
        if source.contains("PS_Main") || source.contains("PS") {
            default_pass.ps_entry = Some(("PS_Main".to_string(), "ps_5_0".to_string()));
        }
        if source.contains("CS_Main") || source.contains("CS") {
            default_pass.cs_entry = Some(("CS_Main".to_string(), "cs_5_0".to_string()));
        }

        techniques.push(TechniqueDesc {
            name: "MainTec".to_string(),
            annotations: HashMap::new(),
            passes: vec![default_pass],
        });
    }

    techniques
}

/// Helper to parse: `VertexShader = compile vs_5_0 VS_Main();` or `SetVertexShader(CompileShader(vs_5_0, VS_Main()));`
fn parse_shader_stage(line: &str, default_profile: &str) -> Option<(String, String)> {
    let clean = line.replace('(', " ").replace(')', " ").replace(';', " ");
    let tokens: Vec<&str> = clean.split_whitespace().collect();

    let mut profile = default_profile.to_string();
    let mut entry = String::new();

    for (i, &tok) in tokens.iter().enumerate() {
        if tok.starts_with("vs_")
            || tok.starts_with("ps_")
            || tok.starts_with("cs_")
            || tok.starts_with("gs_")
            || tok.starts_with("hs_")
            || tok.starts_with("ds_")
        {
            profile = tok.to_string();

            if i + 1 < tokens.len() {
                entry = tokens[i + 1].trim_matches(',').to_string();
            }
        }
    }

    if !entry.is_empty() {
        Some((entry, profile))
    } else {
        None
    }
}
