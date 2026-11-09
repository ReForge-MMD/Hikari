// Native Hikari graphics pipeline, phase 2.
// This file is compiled at runtime by ShaderLibrary. Keep it self-contained:
// the renderer concatenates no external includes.

cbuffer FrameParams : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ViewProjection;
    row_major float4x4 InverseView;
    float4 CameraPos;
    float4 SunDirection;
    float4 SunColor;
    float4 AmbientColor;
    float4 Params0; // near, far, exposure, time
    float4 Params1; // ortho, viewport width, viewport height, unused
    float4 Params2; // projection 00, projection 11, ortho width, ortho height
    float4 BackgroundColor;
};

cbuffer MaterialParams : register(b1) {
    float4 BaseColor;
    float4 SpecularRoughness;
    float4 EmissiveMetallic;
    float4 CustomData; // customA, customB, visibility, packed shading model/flags
    float4 UvTransform; // scale xy, offset zw
};

StructuredBuffer<float4> BoneRows : register(t8);
Texture2D DiffuseMap : register(t8);
SamplerState LinearSampler : register(s0);

Texture2D GB0Tex : register(t0);
Texture2D GB1Tex : register(t1);
Texture2D GB2Tex : register(t2);
Texture2D GB3Tex : register(t3);
Texture2D GB4Tex : register(t4);
Texture2D DepthTex : register(t5);
Texture2D HdrTex : register(t6);

static const float PI = 3.14159265358979323846;

struct VSIn {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_indices : BLENDINDICES;
    float4 bone_weights : BLENDWEIGHT;
};

struct VSOut {
    float4 pos_clip : SV_POSITION;
    float3 pos_world : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct FSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct GBufferOut {
    float4 gb0 : SV_Target0;
    float4 gb1 : SV_Target1;
    float4 gb2 : SV_Target2;
    float4 gb3 : SV_Target3;
    float4 gb4 : SV_Target4;
};

float4x4 load_bone(uint index) {
    const uint base = index * 4u;
    return float4x4(BoneRows[base], BoneRows[base + 1u], BoneRows[base + 2u],
                    BoneRows[base + 3u]);
}

VSOut GBufferVS(VSIn input) {
    VSOut output;
    const float total_weight = input.bone_weights.x + input.bone_weights.y +
                               input.bone_weights.z + input.bone_weights.w;
    const float4 weights = (total_weight > 0.0001f)
                               ? (input.bone_weights / total_weight)
                               : float4(1.0f, 0.0f, 0.0f, 0.0f);
    const float4x4 skin = load_bone(input.bone_indices.x) * weights.x +
                          load_bone(input.bone_indices.y) * weights.y +
                          load_bone(input.bone_indices.z) * weights.z +
                          load_bone(input.bone_indices.w) * weights.w;
    const float4 pos_world = mul(float4(input.position, 1.0f), skin);
    const float3 normal_world = normalize(mul(float4(input.normal, 0.0f), skin).xyz);
    output.pos_world = pos_world.xyz;
    output.normal_world = normal_world;
    output.uv = input.uv * UvTransform.xy + UvTransform.zw;
    output.pos_clip = mul(pos_world, ViewProjection);
    return output;
}

FSOut FullscreenVS(uint id : SV_VertexID) {
    FSOut output;
    const float2 uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.uv = float2(uv.x, 1.0f - uv.y);
    return output;
}

float3 srgb_to_linear(float3 value) {
    return pow(max(value, 0.0f), 2.2f);
}

float2 encode_octahedral(float3 n) {
    n = normalize(n);
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1.0e-6f);
    float2 xy = n.xy;
    if (n.z < 0.0f) {
        xy = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f,
                                         n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return xy * 0.5f + 0.5f;
}

float3 decode_octahedral(float2 value) {
    float2 f = value * 2.0f - 1.0f;
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    if (n.z < 0.0f) {
        n.xy = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f,
                                            n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(n);
}

float pack_shading_model(int model, int flags) {
    return float(model) + float(flags) / 256.0f;
}

GBufferOut GBufferPS(VSOut input) {
    GBufferOut output;
    const float4 texel = DiffuseMap.Sample(LinearSampler, input.uv);
    const float alpha = texel.a * BaseColor.a;
    if (alpha < 0.02f) {
        discard;
    }
    const float3 base_linear = srgb_to_linear(texel.rgb) * BaseColor.rgb;
    const float3 normal = normalize(input.normal_world);

    output.gb0 = float4(base_linear, alpha);
    output.gb1 = float4(encode_octahedral(normal), SpecularRoughness.a, EmissiveMetallic.a);
    output.gb2 = float4(SpecularRoughness.rgb, CustomData.x);
    output.gb3 = float4(CustomData.y, CustomData.y, CustomData.y, CustomData.z);
    output.gb4 = float4(EmissiveMetallic.rgb, CustomData.w);
    return output;
}

float3 F_Schlick(float cos_theta, float3 f0) {
    const float f = pow(saturate(1.0f - cos_theta), 5.0f);
    return f0 + (1.0f - f0) * f;
}

float3 F_AdobeF82(float cos_theta, float3 f0) {
    const float f = pow(saturate(1.0f - cos_theta), 5.0f);
    const float3 tint = lerp(float3(1.0f, 1.0f, 1.0f), f0, 0.5f);
    return f0 + (tint - f0) * f;
}

float D_GGX(float no_h, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = no_h * no_h * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1.0e-7f);
}

float V_SmithGGXCorrelated(float no_v, float no_l, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float lambda_v = no_l * sqrt(max(no_v * no_v * (1.0f - a2) + a2, 1.0e-6f));
    const float lambda_l = no_v * sqrt(max(no_l * no_l * (1.0f - a2) + a2, 1.0e-6f));
    return 0.5f / max(lambda_v + lambda_l, 1.0e-6f);
}

float3 diffuse_burley(float3 base, float no_l, float no_v, float roughness) {
    const float fd90 = 0.5f + 2.0f * roughness * roughness * no_l * no_l;
    const float light_scatter = 1.0f + (fd90 - 1.0f) * pow(1.0f - no_l, 5.0f);
    const float view_scatter = 1.0f + (fd90 - 1.0f) * pow(1.0f - no_v, 5.0f);
    return base * (1.0f / PI) * light_scatter * view_scatter;
}

float3 diffuse_eon(float3 base, float3 n, float3 l, float3 v, float no_l, float no_v,
                   float roughness) {
    const float sigma2 = roughness * roughness;
    const float a = 1.0f - 0.5f * sigma2 / (sigma2 + 0.33f);
    const float b = 0.45f * sigma2 / (sigma2 + 0.09f);
    const float theta_i = acos(saturate(no_l));
    const float theta_r = acos(saturate(no_v));
    const float alpha = max(theta_i, theta_r);
    const float beta = min(theta_i, theta_r);
    const float3 tangent_l = normalize(l - n * no_l + 1.0e-5f);
    const float3 tangent_v = normalize(v - n * no_v + 1.0e-5f);
    const float gamma = dot(tangent_l, tangent_v);
    const float term = b * max(0.0f, gamma) * sin(alpha) * tan(min(beta, 1.55f));
    return base * (1.0f / PI) * max(0.0f, a + term);
}

float3 reconstruct_world(float2 uv, float depth_value) {
    const float near_z = Params0.x;
    const float far_z = Params0.y;
    const bool orthographic = Params1.x > 0.5f;
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float3 view_pos;
    if (orthographic) {
        const float view_z = near_z + depth_value * (far_z - near_z);
        view_pos = float3(ndc.x * Params2.z * 0.5f, ndc.y * Params2.w * 0.5f, view_z);
    } else {
        const float view_z = (near_z * far_z) / max(far_z - depth_value * (far_z - near_z),
                                                    1.0e-6f);
        view_pos = float3(ndc.x * view_z / max(Params2.x, 1.0e-6f),
                          ndc.y * view_z / max(Params2.y, 1.0e-6f), view_z);
    }
    return mul(float4(view_pos, 1.0f), InverseView).xyz;
}

float4 LightingPS(FSOut input) : SV_Target {
    const float4 gb0 = GB0Tex.Sample(LinearSampler, input.uv);
    const float4 gb1 = GB1Tex.Sample(LinearSampler, input.uv);
    const float4 gb2 = GB2Tex.Sample(LinearSampler, input.uv);
    const float4 gb3 = GB3Tex.Sample(LinearSampler, input.uv);
    const float4 gb4 = GB4Tex.Sample(LinearSampler, input.uv);
    const float depth_value = DepthTex.Sample(LinearSampler, input.uv).r;
    if (depth_value >= 0.99999f || gb0.a < 0.001f) {
        return BackgroundColor;
    }

    const float3 base = gb0.rgb;
    const float3 normal = decode_octahedral(gb1.xy);
    const float roughness = saturate(gb1.z);
    const float metallic = saturate(gb1.w);
    const float3 specular = gb2.rgb;
    const float3 emissive = gb4.rgb;
    const float3 world_pos = reconstruct_world(input.uv, depth_value);
    const float3 n = normalize(normal);
    const float3 v = normalize(CameraPos.xyz - world_pos);
    const float3 l = normalize(-SunDirection.xyz);
    const float no_l = saturate(dot(n, l));
    const float no_v = saturate(dot(n, v));
    const float3 h = normalize(l + v);
    float no_h = saturate(dot(n, h));

    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), specular, metallic);
#if BRDF_FRESNEL_TYPE == 0
    const float3 fresnel = F_Schlick(saturate(dot(h, v)), f0);
#else
    const float3 fresnel = F_AdobeF82(saturate(dot(h, v)), f0);
#endif

#if SUN_SPECULAR_DISK_ENABLE
    const float sin_alpha = sin(SUN_ANGULAR_RADIUS);
    no_h = sqrt(saturate(1.0f - sin_alpha * sin_alpha +
                         sin_alpha * sin_alpha * no_h * no_h));
#endif

    const float3 diffuse_base = base * (1.0f - metallic);
#if DIFFUSE_BRDF_TYPE == 0
    const float3 diffuse = diffuse_burley(diffuse_base, no_l, no_v, roughness);
#else
    const float3 diffuse = diffuse_eon(diffuse_base, n, l, v, no_l, no_v, roughness);
#endif

    const float d = D_GGX(no_h, max(roughness, 0.04f));
    const float vis = V_SmithGGXCorrelated(max(no_v, 1.0e-4f), max(no_l, 1.0e-4f),
                                           max(roughness, 0.04f));
    const float3 specular_term = d * vis * fresnel;
    float sun_limb = 1.0f;
#if SUN_LIMB_DARKENING
    sun_limb = lerp(0.82f, 1.0f, saturate(no_l));
#endif

    float3 sun_diffuse = float3(0.0f, 0.0f, 0.0f);
    float3 sun_specular = float3(0.0f, 0.0f, 0.0f);
#if SUN_LIGHT_ENABLE != 0
    sun_diffuse = diffuse * SunColor.rgb * no_l;
    sun_specular = specular_term * SunColor.rgb * no_l * sun_limb;
#endif

    const float hemisphere = saturate(n.y * 0.5f + 0.5f);
    const float3 ambient = AmbientColor.rgb * base * (1.0f - metallic) *
                           lerp(0.55f, 1.0f, hemisphere);

    const float3 color = sun_diffuse + sun_specular + ambient + emissive;
    return float4(max(color, 0.0f), 1.0f);
}

float4 ForwardPS(VSOut input) : SV_Target {
    const float4 texel = DiffuseMap.Sample(LinearSampler, input.uv);
    const float alpha = texel.a * BaseColor.a;
    if (alpha < 0.01f) {
        discard;
    }
    const float3 base = srgb_to_linear(texel.rgb) * BaseColor.rgb;
    const float3 n = normalize(input.normal_world);
    const float3 l = normalize(-SunDirection.xyz);
    const float no_l = saturate(dot(n, l));
    const float3 color = base * (AmbientColor.rgb + SunColor.rgb * no_l * 0.75f) +
                         EmissiveMetallic.rgb;
    return float4(color, alpha);
}

float3 aces_fitted(float3 color) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 agx_contrast(float3 x) {
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x +
           0.4298f * x2 + 0.1191f * x - 0.00232f;
}

float3 agx(float3 color) {
    const float3x3 agx_in = float3x3(
        0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
        0.0784335999999992f, 0.878468636469772f, 0.0784336f,
        0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    const float3x3 agx_out = float3x3(
        0.856627153315983f, 0.0951212405381588f, 0.0482510387262912f,
        0.137318972929847f, 0.761241990602591f, 0.101439036467562f,
        0.11189821299995f, 0.0767994186031903f, 0.811302368396859f);
    const float min_ev = -12.47393f;
    const float max_ev = 4.026069f;
    color = max(color, float3(1.0e-10f, 1.0e-10f, 1.0e-10f));
    color = mul(agx_in, color);
    color = clamp(log2(color), min_ev, max_ev);
    color = (color - min_ev) / (max_ev - min_ev);
    color = agx_contrast(color);
    color = mul(agx_out, color);
    return saturate(color);
}

float4 TonemapPS(FSOut input) : SV_Target {
    const float3 hdr = HdrTex.Sample(LinearSampler, input.uv).rgb * Params0.z;
    float3 color;
#if TONEMAP_OPERATOR == 0
    color = hdr;
#elif TONEMAP_OPERATOR == 1
    color = hdr / (1.0f + hdr);
#elif TONEMAP_OPERATOR == 5
    color = aces_fitted(hdr);
#else
    color = agx(hdr);
#endif
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, 1.0f);
}

struct GridIn {
    float3 position : POSITION;
    float4 color : COLOR;
};

struct GridOut {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

GridOut GridVS(GridIn input) {
    GridOut output;
    output.pos = mul(float4(input.position, 1.0f), ViewProjection);
    output.color = input.color;
    return output;
}

float4 GridPS(GridOut input) : SV_Target {
    return input.color;
}
