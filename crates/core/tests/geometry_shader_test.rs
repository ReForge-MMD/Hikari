use std::ptr::null_mut;
use windows::Win32::Foundation::HWND;
use hikari_core::d3d11::D3D11Context;
use hikari_core::effect::HikariEffect;
use hikari_core::shader::{compile_hlsl, parse_techniques};

#[test]
fn test_compile_geometry_shader_sm5() {
    let hlsl_source = r#"
struct VS_OUTPUT {
    float4 pos: SV_POSITION;
    float3 normal: NORMAL;
    float2 uv: TEXCOORD0;
};

struct GS_OUTPUT {
    float4 pos: SV_POSITION;
    float3 normal: NORMAL;
    float2 uv: TEXCOORD0;
};

[maxvertexcount(3)]
void GS_Main(triangle VS_OUTPUT input[3], inout TriangleStream<GS_OUTPUT> outputStream) {
    for (int i = 0; i < 3; i++) {
        GS_OUTPUT output;
        output.pos = input[i].pos;
        output.normal = input[i].normal;
        output.uv = input[i].uv;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}
"#;

    let result = compile_hlsl(hlsl_source, "GS_Main", "gs_5_0", None, &[]);
    assert!(result.is_ok(), "Geometry Shader SM 5.0 compilation failed: {:?}", result.err());
    let bytecode = result.unwrap();
    assert!(!bytecode.is_empty(), "Bytecode should not be empty");
    assert_eq!(&bytecode[0..4], b"DXBC", "Bytecode must start with DXBC magic header");
}

#[test]
fn test_geometry_shader_pipeline_technique_and_pass() {
    let hlsl_effect = r#"
cbuffer TransformBuffer : register(b0) {
    float4x4 WorldViewProj;
};

struct VS_INPUT {
    float4 pos: POSITION;
    float3 normal: NORMAL;
    float2 uv: TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos: SV_POSITION;
    float3 normal: NORMAL;
    float2 uv: TEXCOORD0;
};

struct GS_OUTPUT {
    float4 pos: SV_POSITION;
    float3 normal: NORMAL;
    float2 uv: TEXCOORD0;
};

VS_OUTPUT VS_Pass(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(input.pos, WorldViewProj);
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

[maxvertexcount(3)]
void GS_Pass(triangle VS_OUTPUT input[3], inout TriangleStream<GS_OUTPUT> stream) {
    for (int i = 0; i < 3; i++) {
        GS_OUTPUT outVert;
        outVert.pos = input[i].pos;
        outVert.normal = input[i].normal;
        outVert.uv = input[i].uv;
        stream.Append(outVert);
    }
}

float4 PS_Pass(GS_OUTPUT input) : SV_Target {
    return float4(input.normal * 0.5f + 0.5f, 1.0f);
}

technique11 GeometryPipeline {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Pass();
        GeometryShader = compile gs_5_0 GS_Pass();
        PixelShader = compile ps_5_0 PS_Pass();
    }
}
"#;

    let parsed = parse_techniques(hlsl_effect);
    assert_eq!(parsed.len(), 1, "Should find 1 technique");
    assert_eq!(parsed[0].name, "GeometryPipeline");
    assert_eq!(parsed[0].passes.len(), 1, "Should have 1 pass");

    let p0 = &parsed[0].passes[0];
    assert!(p0.vs_entry.is_some(), "VS must be defined");
    assert!(p0.gs_entry.is_some(), "GS must be defined");
    assert!(p0.ps_entry.is_some(), "PS must be defined");
    assert_eq!(p0.gs_entry.as_ref().unwrap().0, "GS_Pass");
    assert_eq!(p0.gs_entry.as_ref().unwrap().1, "gs_5_0");

    let ctx = D3D11Context::new(HWND(null_mut()), 0, 0).expect("Headless D3D11Context creation");

    let eff_ptr = HikariEffect::new(hlsl_effect, None, &ctx).expect("HikariEffect::new with GS should succeed");
    assert!(!eff_ptr.is_null());

    unsafe {
        let eff = &mut *eff_ptr;
        let pass_info = eff.get_first_pass_info();
        assert!(pass_info.is_some(), "Must have active pass");
        let pass_info = pass_info.unwrap();
        assert!(pass_info.vs.is_some(), "VS must be present");
        assert!(pass_info.gs.is_some(), "GS must be present in ActivePassInfo");
        assert!(pass_info.ps.is_some(), "PS must be present");
    }
}
