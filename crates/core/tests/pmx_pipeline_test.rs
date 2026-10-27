use hikari_core::d3d11::D3D11Context;
use hikari_core::shader::compile_hlsl;
use windows::Win32::Foundation::HWND;

#[test]
fn test_pmx_vertex_input_layouts() {
    let mut ctx = D3D11Context::new(HWND(std::ptr::null_mut()), 0, 0)
        .expect("Failed to create headless D3D11 context");

    // 1. Skinned PMX Shader
    let vs_pmx_source = r#"
struct VS_PMX_INPUT {
    float3 position : POSITION;
    float4 weights  : BLENDWEIGHT;
    uint4  indices  : BLENDINDICES;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

float4 VS_Main(VS_PMX_INPUT input) : SV_POSITION {
    return float4(input.position, 1.0);
}
"#;

    let vs_pmx_bc = compile_hlsl(vs_pmx_source, "VS_Main", "vs_5_0", None, &[])
        .expect("Failed to compile VS for PMX layout");

    // SDEF / BDEF4 layout (stride 52)
    let il_sdef = ctx.get_or_create_input_layout(&vs_pmx_bc, 52);
    assert!(il_sdef.is_ok(), "Input layout creation for SDEF (stride 52) failed");

    // BDEF2 layout (stride 38)
    let il_bdef2 = ctx.get_or_create_input_layout(&vs_pmx_bc, 38);
    assert!(il_bdef2.is_ok(), "Input layout creation for BDEF2 (stride 38) failed");

    // 2. Static Mesh Shader
    let vs_static_source = r#"
struct VS_STATIC_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

float4 VS_Static(VS_STATIC_INPUT input) : SV_POSITION {
    return float4(input.position, 1.0);
}
"#;

    let vs_static_bc = compile_hlsl(vs_static_source, "VS_Static", "vs_5_0", None, &[])
        .expect("Failed to compile VS for static layout");

    // Static mesh layout (stride 32)
    let il_static = ctx.get_or_create_input_layout(&vs_static_bc, 32);
    assert!(il_static.is_ok(), "Input layout creation for static mesh (stride 32) failed");

    // 3. Screen Quad / Minimal Shader
    let vs_quad_source = r#"
struct VS_QUAD_INPUT {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
};

float4 VS_Quad(VS_QUAD_INPUT input) : SV_POSITION {
    return float4(input.position, 1.0);
}
"#;

    let vs_quad_bc = compile_hlsl(vs_quad_source, "VS_Quad", "vs_5_0", None, &[])
        .expect("Failed to compile VS for quad layout");

    // Screen quad layout (stride 20)
    let il_quad = ctx.get_or_create_input_layout(&vs_quad_bc, 20);
    assert!(il_quad.is_ok(), "Input layout creation for quad (stride 20) failed");
}
