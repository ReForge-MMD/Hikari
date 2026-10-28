use std::ptr::null_mut;
use hikari_core::d3d11::D3D11Context;
use hikari_core::effect::HikariEffect;
use hikari_core::shader::{compile_hlsl, parse_techniques};

#[test]
fn test_compile_compute_shader_sm5() {
    let hlsl_source = r#"
RWTexture2D<float4> OutputTexture : register(u0);
StructuredBuffer<float4> InputBuffer : register(t0);

cbuffer Params : register(b0) {
    float4 Multiplier;
};

[numthreads(8, 8, 1)]
void CS_Main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint index = dispatchThreadID.y * 512 + dispatchThreadID.x;
    float4 data = InputBuffer[index];
    OutputTexture[dispatchThreadID.xy] = data * Multiplier;
}
"#;

    let result = compile_hlsl(hlsl_source, "CS_Main", "cs_5_0", None, &[]);
    assert!(result.is_ok(), "Compute shader SM 5.0 compilation failed: {:?}", result.err());
    let bytecode = result.unwrap();
    assert!(!bytecode.is_empty(), "Bytecode should not be empty");
    assert_eq!(&bytecode[0..4], b"DXBC", "Bytecode must start with DXBC magic header");
}

#[test]
fn test_compile_tessellation_sm5() {
    let hlsl_source = r#"
struct VS_OUTPUT {
    float4 pos: POSITION;
    float3 normal: TEXCOORD1;
};

struct CONSTANT_HS_OUTPUT {
    float Edges[3]: SV_TessFactor;
    float Inside: SV_InsideTessFactor;
};

struct HS_OUTPUT {
    float3 pos: POSITION;
    float3 normal: TEXCOORD1;
};

struct DS_OUTPUT {
    float4 pos: SV_POSITION;
    float3 normal: TEXCOORD1;
};

CONSTANT_HS_OUTPUT ConstantsHS_Main(InputPatch<VS_OUTPUT, 3> ip, uint PatchID : SV_PrimitiveID) {
    CONSTANT_HS_OUTPUT output;
    output.Edges[0] = 3.0f;
    output.Edges[1] = 3.0f;
    output.Edges[2] = 3.0f;
    output.Inside = 3.0f;
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS_Main")]
HS_OUTPUT HS_Main(InputPatch<VS_OUTPUT, 3> input, uint i : SV_OutputControlPointID) {
    HS_OUTPUT output;
    output.pos = input[i].pos.xyz;
    output.normal = input[i].normal;
    return output;
}

[domain("tri")]
DS_OUTPUT DS_Main(CONSTANT_HS_OUTPUT input, float3 uvw : SV_DomainLocation, const OutputPatch<HS_OUTPUT, 3> patch) {
    DS_OUTPUT output;
    float3 pos = patch[0].pos * uvw.x + patch[1].pos * uvw.y + patch[2].pos * uvw.z;
    output.pos = float4(pos, 1.0f);
    output.normal = patch[0].normal;
    return output;
}
"#;

    // Test Hull Shader (hs_5_0)
    let hs_res = compile_hlsl(hlsl_source, "HS_Main", "hs_5_0", None, &[]);
    assert!(hs_res.is_ok(), "Hull shader SM 5.0 compilation failed: {:?}", hs_res.err());

    // Test Domain Shader (ds_5_0)
    let ds_res = compile_hlsl(hlsl_source, "DS_Main", "ds_5_0", None, &[]);
    assert!(ds_res.is_ok(), "Domain shader SM 5.0 compilation failed: {:?}", ds_res.err());
}

#[test]
fn test_parse_techniques_and_passes() {
    let source = r#"
technique Tessellation {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        HullShader = compile hs_5_0 HS_Main();
        DomainShader = compile ds_5_0 DS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }
}

technique ComputeFilter {
    pass P0 {
        ComputeShader = compile cs_5_0 Filter_CS();
    }
}
"#;

    let techniques = parse_techniques(source);
    assert_eq!(techniques.len(), 2);
    assert_eq!(techniques[0].name, "Tessellation");
    assert_eq!(techniques[0].passes.len(), 1);

    let p0 = &techniques[0].passes[0];
    assert!(p0.vs_entry.is_some());
    assert!(p0.hs_entry.is_some());
    assert!(p0.ds_entry.is_some());
    assert!(p0.ps_entry.is_some());

    assert_eq!(techniques[1].name, "ComputeFilter");
    assert!(techniques[1].passes[0].cs_entry.is_some());
}

#[test]
fn test_hikari_effect_sm5_pipeline() {
    use hikari_core::d3d11::D3D11Context;
    use hikari_core::effect::HikariEffect;
    use windows::Win32::Foundation::HWND;
    use std::ptr::null_mut;

    let ctx = D3D11Context::new(HWND(null_mut()), 0, 0).expect("Headless D3D11Context creation");

    let hlsl = r#"
    struct VS_INPUT {
        float3 pos : POSITION;
        float3 normal : NORMAL;
        float2 uv : TEXCOORD0;
    };
    struct VS_OUTPUT {
        float4 pos : SV_POSITION;
        float2 uv : TEXCOORD0;
    };
    VS_OUTPUT VS_Main(VS_INPUT input) {
        VS_OUTPUT output;
        output.pos = float4(input.pos, 1.0f);
        output.uv = input.uv;
        return output;
    }
    float4 PS_Main(VS_OUTPUT input) : SV_TARGET {
        return float4(input.uv, 0.0f, 1.0f);
    }
    technique11 BasicTec {
        pass P0 {
            VertexShader = compile vs_5_0 VS_Main();
            PixelShader = compile ps_5_0 PS_Main();
        }
    }
    "#;

    let eff_ptr = HikariEffect::new(hlsl, None, &ctx).expect("HikariEffect::new should compile SM 5.0 effect");
    assert!(!eff_ptr.is_null());
    unsafe {
        let eff = &*eff_ptr;
        assert_eq!(eff.techniques.len(), 1);
        assert_eq!(eff.compiled_techniques.len(), 1);
        assert_eq!(eff.compiled_techniques[0].len(), 1);
        let p0 = &eff.compiled_techniques[0][0];
        assert!(p0.vs.is_some());
        assert!(p0.ps.is_some());
        assert!(p0.vs_bytecode.is_some());
        assert!(!p0.vs_bytecode.as_ref().unwrap().is_empty());
        // Clean up
        drop(Box::from_raw(eff_ptr));
    }
}

#[test]
fn test_compile_model_fx_file() {
    let ctx = D3D11Context::new(windows::Win32::Foundation::HWND(null_mut()), 0, 0)
        .expect("Failed to create D3D11Context");
    let fx_path = if std::path::Path::new("test_model.fx").exists() {
        "test_model.fx"
    } else {
        "../../test_model.fx"
    };
    let fx_content = std::fs::read_to_string(fx_path)
        .expect("Failed to read test_model.fx");

    let eff_ptr = HikariEffect::new(&fx_content, Some("test_model.fx"), &ctx)
        .expect("HikariEffect::new should compile test_model.fx cleanly");
    assert!(!eff_ptr.is_null());
    unsafe {
        let eff = &*eff_ptr;
        assert_eq!(eff.techniques.len(), 4, "Should have MainTec, MainTecSS, EdgeTec, ShadowTec");
        // Verify parameters
        assert!(eff.param_by_semantic.contains_key("WORLDVIEWPROJECTION"));
        assert!(eff.param_by_semantic.contains_key("WORLD"));
        assert!(eff.param_by_semantic.contains_key("MATERIALTEXTURE"));

        // Verify MMD ExpGetCurrentTechnic resolution
        assert_eq!(eff.find_technique_for_pass(1), Some(0), "Tech 1 (Self-shadow OFF) -> MainTec");
        assert_eq!(eff.find_technique_for_pass(2), Some(1), "Tech 2 (Self-shadow ON / Camera mode) -> MainTecSS");
        assert_eq!(eff.find_technique_for_pass(4), Some(2), "Tech 4 (Edge) -> EdgeTec");
        assert_eq!(eff.find_technique_for_pass(3), Some(3), "Tech 3 (Shadow) -> ShadowTec");
        assert_eq!(eff.find_technique_for_pass(0), Some(0), "Tech 0 (Other) -> MainTec");
        drop(Box::from_raw(eff_ptr));
    }
}

#[test]
fn test_compile_red_model_fx_file() {
    let ctx = D3D11Context::new(windows::Win32::Foundation::HWND(null_mut()), 0, 0)
        .expect("Failed to create D3D11Context");
    let fx_path = if std::path::Path::new("red_model.fx").exists() {
        "red_model.fx"
    } else {
        "../../red_model.fx"
    };
    let fx_content = std::fs::read_to_string(fx_path)
        .expect("Failed to read red_model.fx");

    let eff_ptr = HikariEffect::new(&fx_content, Some("red_model.fx"), &ctx)
        .expect("HikariEffect::new should compile red_model.fx cleanly");
    assert!(!eff_ptr.is_null());
    unsafe {
        let eff = &*eff_ptr;
        assert!(eff.techniques.len() >= 2);
        assert!(eff.param_by_name.contains_key("redcolor"));
        assert!(eff.param_by_name.contains_key("redglow"));

        // Verify MMD technique resolution for red_model.fx
        assert_eq!(eff.find_technique_for_pass(1), Some(0), "Tech 1 -> MainTec");
        assert_eq!(eff.find_technique_for_pass(2), Some(1), "Tech 2 (Camera mode) -> MainTecSS");
        assert_eq!(eff.find_technique_for_pass(0), Some(0), "Tech 0 -> MainTec");
        assert_eq!(eff.find_technique_for_pass(4), None, "Tech 4 (Edge absent) -> None (fallback to D3D9)");
        drop(Box::from_raw(eff_ptr));
    }
}


