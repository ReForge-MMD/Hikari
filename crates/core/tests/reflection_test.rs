use hikari_core::reflection::{ReflectedResourceType, ShaderReflectionData};
use hikari_core::shader::compile_hlsl;

#[test]
fn test_shader_reflection_cbuffers_and_resources() {
    let hlsl = r#"
cbuffer ModelConstants : register(b0) {
    float4x4 matWorld;
    float4x4 matView;
    float4x4 matProj;
    float3 cameraPos;
    float time;
};

cbuffer MaterialConstants : register(b1) {
    float4 diffuseColor;
    float roughness;
    float metallic;
    float2 padding;
};

Texture2D g_AlbedoTexture : register(t0);
TextureCube g_EnvCube : register(t1);
SamplerState g_LinearSampler : register(s0);
RWStructuredBuffer<float4> g_OutputData : register(u0);

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT VS_Main(VS_INPUT input) {
    PS_INPUT output;
    float4 worldPos = mul(float4(input.position, 1.0), matWorld);
    float4 viewPos = mul(worldPos, matView);
    output.pos = mul(viewPos, matProj);
    output.uv = input.uv;
    return output;
}

float4 PS_Main(PS_INPUT input) : SV_Target {
    float4 albedo = g_AlbedoTexture.Sample(g_LinearSampler, input.uv);
    return albedo * diffuseColor;
}
"#;

    // Compile VS
    let vs_bytes = compile_hlsl(hlsl, "VS_Main", "vs_5_0", None, &[]).expect("Failed to compile VS");
    let vs_refl = ShaderReflectionData::reflect(&vs_bytes).expect("Failed to reflect VS");

    // Verify ModelConstants cbuffer
    let model_cb = vs_refl.cbuffers.iter().find(|cb| cb.name == "ModelConstants").expect("ModelConstants not found");
    assert_eq!(model_cb.slot, 0);
    assert!(model_cb.variables.contains_key("matWorld"));
    assert!(model_cb.variables.contains_key("matView"));
    assert!(model_cb.variables.contains_key("matProj"));
    assert!(model_cb.variables.contains_key("cameraPos"));

    // Verify input parameters (POSITION, NORMAL, TEXCOORD)
    assert!(vs_refl.input_parameters.iter().any(|p| p.semantic_name == "POSITION"));
    assert!(vs_refl.input_parameters.iter().any(|p| p.semantic_name == "NORMAL"));
    assert!(vs_refl.input_parameters.iter().any(|p| p.semantic_name == "TEXCOORD"));

    // Compile PS
    let ps_bytes = compile_hlsl(hlsl, "PS_Main", "ps_5_0", None, &[]).expect("Failed to compile PS");
    let ps_refl = ShaderReflectionData::reflect(&ps_bytes).expect("Failed to reflect PS");

    // Verify Texture and Sampler slots
    let albedo_slot = ps_refl.find_texture_slot("g_AlbedoTexture");
    assert_eq!(albedo_slot, Some(0), "g_AlbedoTexture should be in slot t0");

    let sampler_slot = ps_refl.find_sampler_slot("g_LinearSampler");
    assert_eq!(sampler_slot, Some(0), "g_LinearSampler should be in slot s0");

    // Verify MaterialConstants cbuffer in PS
    let mat_cb = ps_refl.cbuffers.iter().find(|cb| cb.name == "MaterialConstants").expect("MaterialConstants not found");
    assert_eq!(mat_cb.slot, 1);
    assert!(mat_cb.variables.contains_key("diffuseColor"));
    assert!(mat_cb.variables.contains_key("roughness"));
}

#[test]
fn test_compute_shader_reflection() {
    let hlsl = r#"
RWStructuredBuffer<float4> g_Particles : register(u0);

[numthreads(64, 2, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID) {
    g_Particles[id.x] = float4(id.x * 0.1, id.y * 0.2, id.z * 0.3, 1.0);
}
"#;

    let cs_bytes = compile_hlsl(hlsl, "CS_Main", "cs_5_0", None, &[]).expect("Failed to compile CS");
    let cs_refl = ShaderReflectionData::reflect(&cs_bytes).expect("Failed to reflect CS");

    assert_eq!(cs_refl.thread_group_size, [64, 2, 1], "Thread group size should be [64, 2, 1]");

    let uav = cs_refl.resources.iter().find(|r| r.name == "g_Particles").expect("g_Particles UAV not found");
    assert_eq!(uav.resource_type, ReflectedResourceType::Uav);
    assert_eq!(uav.bind_point, 0);
}
