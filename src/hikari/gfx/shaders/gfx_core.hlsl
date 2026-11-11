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
Texture2DArray<float> ShadowMapArray : register(t7);
Texture2D SkyVisibilityTex : register(t9);
Texture2D ShadowMaskTex : register(t10);

SamplerState PointSampler : register(s1);
SamplerComparisonState ShadowCmpSampler : register(s2);

cbuffer ShadowParams : register(b2) {
    row_major float4x4 LightViewProjection[4];
    row_major float4x4 SkyVisibilityMatrix;
    float4 CascadeSplits;      // far linear view depth per cascade
    float4 CascadeTexelWorld;  // world size of one shadow texel per cascade
    float4 CascadeLightRange;  // orthographic depth range per cascade
    float4 ShadowParams0;      // x depth bias, y normal bias (texels), z pcss, w blur radius
    float4 ShadowParams1;      // x light radius (radians), y contact length, z contact thickness, w ssgs length
    float4 ShadowParams2;      // x ssgs thickness, y sky height bias, z cascade count, w sky texel uv
    float4 ShadowParams3;      // x sky visibility mode, y unused, z contact steps, w ssgs steps
};

cbuffer ShadowDrawParams : register(b3) {
    row_major float4x4 ShadowDrawMatrix;
    float4 ShadowDrawParams;
};

#ifndef SUN_SHADOW_PCSS
#define SUN_SHADOW_PCSS 1
#endif
#ifndef CONTACT_SHADOW_STEPS
#define CONTACT_SHADOW_STEPS 0
#endif
#ifndef SSGS_STEPS
#define SSGS_STEPS 0
#endif
#ifndef SHADOW_BLUR_RADIUS
#define SHADOW_BLUR_RADIUS 2
#endif
#ifndef SHADOW_BLUR_DIR
#define SHADOW_BLUR_DIR 0
#endif
#ifndef SHADOW_MASK_ENABLE
#define SHADOW_MASK_ENABLE 0
#endif
#ifndef SKY_VISIBILITY_ENABLE
#define SKY_VISIBILITY_ENABLE 0
#endif
#ifndef DEBUG_VIEW
#define DEBUG_VIEW 0
#endif

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

VSOut ShadowVS(VSIn input) {
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
    output.pos_world = pos_world.xyz;
    output.normal_world = normalize(mul(float4(input.normal, 0.0f), skin).xyz);
    output.uv = input.uv * UvTransform.xy + UvTransform.zw;
    output.pos_clip = mul(pos_world, ShadowDrawMatrix);
    return output;
}

float4 ShadowPS(VSOut input) : SV_Target {
    const float4 texel = DiffuseMap.SampleLevel(LinearSampler, input.uv, 0);
    if (texel.a * BaseColor.a < 0.02f) {
        discard;
    }
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float4 SkyHeightPS(VSOut input) : SV_Target {
    const float4 texel = DiffuseMap.SampleLevel(LinearSampler, input.uv, 0);
    if (texel.a * BaseColor.a < 0.02f) {
        discard;
    }
    return float4(input.pos_world.y, 1.0f, 0.0f, 1.0f);
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

float linear_view_depth(float depth_value) {
    if (Params1.x > 0.5f) {
        return Params0.x + depth_value * (Params0.y - Params0.x);
    }
    return (Params0.x * Params0.y) /
           max(Params0.y - depth_value * (Params0.y - Params0.x), 1.0e-6f);
}

int choose_cascade(float view_z) {
    int cascade = 0;
    [unroll]
    for (int i = 0; i < 3; ++i) {
        if (view_z > CascadeSplits[i]) {
            cascade = i + 1;
        }
    }
    return min(cascade, (int)ShadowParams2.z - 1);
}

static const float2 kPoisson16[16] = {
    float2(-0.94201624f, -0.39906216f),
    float2( 0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f),
    float2( 0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f),
    float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f),
    float2( 0.97484398f,  0.75648379f),
    float2( 0.44323325f, -0.97511554f),
    float2( 0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f),
    float2( 0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f),
    float2(-0.81409955f,  0.91437590f),
    float2( 0.19984126f,  0.78641367f),
    float2( 0.14383161f, -0.14100790f)
};

float2 rotate_poisson(float2 sample_pos, float2 rot) {
    return float2(sample_pos.x * rot.x - sample_pos.y * rot.y,
                  sample_pos.x * rot.y + sample_pos.y * rot.x);
}

float2 vogel_disc_sample(int sample_index, int sample_count, float phi) {
    const float golden_angle = 2.39996323f;
    const float r = sqrt((float(sample_index) + 0.5f) / float(sample_count));
    const float theta = float(sample_index) * golden_angle + phi;
    return float2(cos(theta), sin(theta)) * r;
}

#ifndef PCSS_BLOCKER_SAMPLES
#define PCSS_BLOCKER_SAMPLES 16
#endif
#ifndef PCSS_FILTER_SAMPLES
#define PCSS_FILTER_SAMPLES 16
#endif

float2 get_receiver_plane_depth_bias(float3 shadow_coord) {
    const float3 dx = ddx(shadow_coord);
    const float3 dy = ddy(shadow_coord);
    float2 bias_uv;
    bias_uv.x = dy.y * dx.z - dx.y * dy.z;
    bias_uv.y = dx.x * dy.z - dy.x * dx.z;
    const float det = (dx.x * dy.y) - (dx.y * dy.x);
    if (abs(det) < 1.0e-6f) {
        return float2(0.0f, 0.0f);
    }
    const float2 grad = bias_uv * (1.0f / det);
    const float grad_len = length(grad);
    return (grad_len > 2.0f) ? (grad * (2.0f / grad_len)) : grad;
}

float3 contact_shadow_geometric_normal(float3 tangent_x, float3 tangent_y, float3 shading_normal) {
    float3 geometric_normal = cross(tangent_y, tangent_x);
    const float len_sq = dot(geometric_normal, geometric_normal);
    if (len_sq > 1.0e-10f) {
        geometric_normal *= rsqrt(len_sq);
    } else {
        geometric_normal = shading_normal;
    }
    return geometric_normal * (dot(geometric_normal, shading_normal) < 0.0f ? -1.0f : 1.0f);
}

float sample_single_cascade(float3 world_pos, float3 normal, int cascade, float no_l,
                            float rotation) {
    const float texel_world = max(CascadeTexelWorld[cascade], 1.0e-6f);
    const float normal_bias_scale = saturate(1.0f - saturate(no_l));
    const float3 offset_pos =
        world_pos + normal * (texel_world * (ShadowParams0.y * (0.8f + 1.2f * normal_bias_scale)));
    float4 light_clip = mul(float4(offset_pos, 1.0f), LightViewProjection[cascade]);
    light_clip.xyz /= max(light_clip.w, 1.0e-6f);

    const float2 uv = float2(light_clip.x * 0.5f + 0.5f, 0.5f - light_clip.y * 0.5f);
    if (uv.x < 0.001f || uv.x > 0.999f || uv.y < 0.001f || uv.y > 0.999f ||
        light_clip.z < 0.0f || light_clip.z > 1.0f) {
        return 1.0f;
    }

    const float receiver = saturate(light_clip.z);
    const float base_bias = ShadowParams0.x * (1.0f + 1.5f * normal_bias_scale);
    const float2 texel_uv = float2(ShadowParams3.y, ShadowParams3.y);
    const float2 rot = float2(cos(rotation), sin(rotation));
    const float2 receiver_plane_bias = get_receiver_plane_depth_bias(float3(uv, receiver));

#if SUN_SHADOW_PCSS
    // Step 1: Blocker Search (NVIDIA PCSS with Vogel Disc)
    const float light_scale = max(ShadowParams1.x / 0.015f, 0.5f);
    const float search_radius = clamp(28.0f * light_scale * texel_uv.x, texel_uv.x * 3.0f, texel_uv.x * 48.0f);

    float blocker_sum = 0.0f;
    int blockers = 0;
    [unroll]
    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i) {
        const float2 offset = vogel_disc_sample(i, PCSS_BLOCKER_SAMPLES, rotation) * search_radius;
        const float plane_bias = clamp(dot(offset, receiver_plane_bias), -0.00008f, 0.00008f);
        const float sample_depth = ShadowMapArray.SampleLevel(
            PointSampler, float3(uv + offset, (float)cascade), 0);
        if (sample_depth < (receiver - base_bias - plane_bias) && sample_depth < 0.9999f) {
            blocker_sum += sample_depth;
            blockers += 1;
        }
    }

    if (blockers == 0) {
        return 1.0f;
    }

    // Step 2: Penumbra Estimation (NVIDIA formula for directional sunlight)
    // Filter radius is strictly bounded by search_radius to prevent penumbra cutoff artifacts
    const float avg_blocker = blocker_sum / float(blockers);
    const float world_blocker_dist = max(0.0f, receiver - avg_blocker) * CascadeLightRange[cascade];
    const float penumbra_texels = (world_blocker_dist * ShadowParams1.x) / texel_world;
    const float penumbra_uv = penumbra_texels * texel_uv.x;
    const float min_filter_radius = texel_uv.x * 1.5f;
    const float max_filter_radius = search_radius;
    const float filter_radius = clamp(penumbra_uv, min_filter_radius, max_filter_radius);

    // Step 3: PCF Filtering with dynamic penumbra footprint and hardware bilinear comparison
    float lit = 0.0f;
    [unroll]
    for (int j = 0; j < PCSS_FILTER_SAMPLES; ++j) {
        const float2 offset = vogel_disc_sample(j, PCSS_FILTER_SAMPLES, rotation) * filter_radius;
        const float plane_bias = clamp(dot(offset, receiver_plane_bias), -0.00008f, 0.00008f);
        const float comp_depth = receiver - base_bias - plane_bias;
        lit += ShadowMapArray.SampleCmpLevelZero(
            ShadowCmpSampler, float3(uv + offset, (float)cascade), comp_depth);
    }
    return lit / float(PCSS_FILTER_SAMPLES);
#else
    float lit = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 offset = float2(float(x), float(y)) * texel_uv;
            const float plane_bias = clamp(dot(offset, receiver_plane_bias), -0.00008f, 0.00008f);
            lit += ShadowMapArray.SampleCmpLevelZero(
                ShadowCmpSampler, float3(uv + offset, (float)cascade), receiver - base_bias - plane_bias);
        }
    }
    return lit / 9.0f;
#endif
}

float sample_sun_shadow(float3 world_pos, float3 normal, float view_z, float no_l,
                        float rotation, out int cascade_used) {
    if (no_l <= 0.0f) {
        cascade_used = 0;
        return 0.0f;
    }
    const float terminator = smoothstep(0.0f, 0.12f, no_l);
    const int max_cascades = min((int)ShadowParams2.z, 4);

    const int target_cascade = choose_cascade(view_z);
    cascade_used = target_cascade;

    float factor = sample_single_cascade(world_pos, normal, target_cascade, no_l, rotation);

    if (target_cascade + 1 < max_cascades) {
        const float split = CascadeSplits[target_cascade];
        const float fade_start = split * 0.85f;
        if (view_z > fade_start) {
            const float z_weight = saturate((split - view_z) / max(split - fade_start, 1.0e-4f));
            const float secondary =
                sample_single_cascade(world_pos, normal, target_cascade + 1, no_l, rotation);
            factor = lerp(secondary, factor, z_weight);
        }
    }

    const int last_cascade = max_cascades - 1;
    const float last_split = CascadeSplits[last_cascade];
    const float last_fade_start = last_split * 0.90f;
    if (view_z > last_fade_start) {
        const float fade =
            saturate((view_z - last_fade_start) / max(last_split - last_fade_start, 1.0e-4f));
        factor = lerp(factor, 1.0f, fade);
    }
    return lerp(0.0f, factor, terminator);
}

float trace_contact_shadow(float3 world_pos, float3 normal, float3 light_dir, int steps,
                           float rotation) {
    if (steps <= 0) {
        return 1.0f;
    }
    const float3 view_pos = mul(float4(world_pos, 1.0f), View).xyz;
    const float3 view_normal = normalize(mul(float4(normal, 0.0f), View).xyz);
    const float3 view_light = normalize(mul(float4(light_dir, 0.0f), View).xyz);

    const float n_dot_l = dot(view_normal, view_light);
    if (n_dot_l <= 0.0f) {
        return 1.0f;
    }

    const float terminator_fade = smoothstep(0.0f, 0.15f, n_dot_l);

    const float3 view_pos_dx = ddx(view_pos);
    const float3 view_pos_dy = ddy(view_pos);
    const float3 geom_normal =
        contact_shadow_geometric_normal(view_pos_dx, view_pos_dy, view_normal);

    float pixel_footprint = max(length(view_pos_dx), length(view_pos_dy));
    pixel_footprint = min(pixel_footprint, max(0.01f, view_pos.z * 0.01f));
    const float slope_scale = 1.0f - saturate(n_dot_l);
    const float normal_bias =
        max(0.003f, pixel_footprint * 0.5f) * (1.0f + 2.0f * slope_scale);

    // Adaptive max distance based on view depth (Ray-MMD formula)
    const float max_distance = min(1.0f, max(0.08f, view_pos.z * 0.04f));
    const float3 ray_origin = view_pos + geom_normal * normal_bias;
    const float3 ray_end = ray_origin + view_light * max_distance;

    const float4 clip_start = mul(float4(ray_origin, 1.0f), Projection);
    const float4 clip_end = mul(float4(ray_end, 1.0f), Projection);

    const float tan_cone_angle = 0.055f;
    const float vs_end_radius = tan_cone_angle * max_distance;

    float occlusion = 0.0f;
    float hit_t = 1.0f;
    const float jitter = frac(rotation);

    [loop]
    for (int i = 0; i < steps; ++i) {
        const float norm_step = (float(i) + jitter) / float(steps);
        const float ray_t = pow(norm_step, 1.25f);
        const float ray_z = ray_origin.z + view_light.z * (ray_t * max_distance);
        const float4 clip_pos = lerp(clip_start, clip_end, ray_t);
        if (clip_pos.w <= 1.0e-4f) {
            break;
        }
        const float2 sample_uv = float2(clip_pos.x / clip_pos.w * 0.5f + 0.5f,
                                        0.5f - clip_pos.y / clip_pos.w * 0.5f);
        if (sample_uv.x < 0.001f || sample_uv.x > 0.999f ||
            sample_uv.y < 0.001f || sample_uv.y > 0.999f) {
            break;
        }

        const float travel_dist = ray_t * max_distance;
        const float cone_radius = max(0.004f, vs_end_radius * ray_t + normal_bias * 0.4f);
        const float cone_thickness = cone_radius * 2.8f + max(0.012f, view_pos.z * 0.003f);

        const float scene_depth_raw = DepthTex.SampleLevel(PointSampler, sample_uv, 0).r;
        if (scene_depth_raw >= 0.99999f) {
            continue;
        }
        const float scene_depth = linear_view_depth(scene_depth_raw);
        const float penetration = ray_z - scene_depth;

        if (penetration > 0.0f && penetration <= cone_thickness) {
            const float weight = 1.0f - abs(penetration / cone_thickness - 0.5f) * 2.0f;
            const float contact_falloff = saturate(1.0f - travel_dist / max_distance);
            const float soft_occl = saturate(weight * 1.8f) * terminator_fade * contact_falloff;
            if (soft_occl > occlusion) {
                occlusion = soft_occl;
                hit_t = ray_t;
            }
            if (occlusion >= 0.95f) {
                break;
            }
        }
    }

    const float distance_fade = 1.0f - smoothstep(0.65f, 1.0f, hit_t);
    const float shadow = 1.0f - occlusion * distance_fade;
    // Square the shadow attenuation for crisp, noticeable crevice and contact grounding (Ray-MMD)
    return shadow * shadow;
}

float trace_ssgs(float3 world_pos, float3 normal, float3 light_dir, int steps,
                 float max_distance, float rotation) {
    if (steps <= 0 || max_distance <= 0.0f) {
        return 1.0f;
    }
    const float3 view_pos = mul(float4(world_pos, 1.0f), View).xyz;
    const float3 view_normal = normalize(mul(float4(normal, 0.0f), View).xyz);
    const float3 view_light = normalize(mul(float4(light_dir, 0.0f), View).xyz);

    const float n_dot_l = dot(view_normal, view_light);
    if (n_dot_l <= 0.0f) {
        return 1.0f;
    }

    const float terminator_fade = smoothstep(0.0f, 0.20f, n_dot_l);

    const float3 view_pos_dx = ddx(view_pos);
    const float3 view_pos_dy = ddy(view_pos);
    const float3 geom_normal =
        contact_shadow_geometric_normal(view_pos_dx, view_pos_dy, view_normal);

    float pixel_footprint = max(length(view_pos_dx), length(view_pos_dy));
    pixel_footprint = min(pixel_footprint, max(0.01f, view_pos.z * 0.01f));
    const float slope_scale = 1.0f - saturate(n_dot_l);
    const float normal_bias = max(0.03f, pixel_footprint * 1.2f) * (1.0f + 2.0f * slope_scale);

    const float3 ray_origin = view_pos + geom_normal * normal_bias;
    const float3 ray_end = ray_origin + view_light * max_distance;

    const float4 clip_start = mul(float4(ray_origin, 1.0f), Projection);
    const float4 clip_end = mul(float4(ray_end, 1.0f), Projection);

    float occlusion = 0.0f;
    float hit_t = 1.0f;
    const float jitter = frac(rotation);

    [loop]
    for (int i = 0; i < steps; ++i) {
        const float norm_step = (float(i) + jitter) / float(steps);
        const float ray_t = pow(norm_step, 1.35f);
        const float ray_z = ray_origin.z + view_light.z * (ray_t * max_distance);
        const float4 clip_pos = lerp(clip_start, clip_end, ray_t);
        if (clip_pos.w <= 1.0e-4f) {
            break;
        }
        const float2 sample_uv = float2(clip_pos.x / clip_pos.w * 0.5f + 0.5f,
                                        0.5f - clip_pos.y / clip_pos.w * 0.5f);
        if (sample_uv.x < 0.001f || sample_uv.x > 0.999f ||
            sample_uv.y < 0.001f || sample_uv.y > 0.999f) {
            break;
        }

        const float travel_dist = ray_t * max_distance;
        // Volumetric thickness that scales with distance to capture real geometric limbs & bodies
        const float thickness = max(0.08f, travel_dist * 0.09f + normal_bias * 2.0f);

        const float scene_depth_raw = DepthTex.SampleLevel(PointSampler, sample_uv, 0).r;
        if (scene_depth_raw >= 0.99999f) {
            continue;
        }
        const float scene_depth = linear_view_depth(scene_depth_raw);
        const float penetration = ray_z - scene_depth;

        if (penetration > 0.0f && penetration <= thickness) {
            // Smooth parabolic bell-curve thickness weighting to eliminate step banding
            const float weight = 1.0f - abs(penetration / thickness - 0.5f) * 2.0f;
            const float soft_occl = saturate(weight * 1.6f) * terminator_fade;
            if (soft_occl > occlusion) {
                occlusion = soft_occl;
                hit_t = ray_t;
            }
            if (occlusion >= 0.95f) {
                break;
            }
        }
    }

    const float distance_fade = 1.0f - smoothstep(0.70f, 1.0f, hit_t);
    return 1.0f - occlusion * distance_fade;
}

float sample_sky_visibility(float3 world_pos, float3 normal, out float3 out_bent_normal) {
    out_bent_normal = normal;
    const float4 clip = mul(float4(world_pos, 1.0f), SkyVisibilityMatrix);
    const float2 uv = float2(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f);
    if (uv.x < 0.001f || uv.x > 0.999f || uv.y < 0.001f || uv.y > 0.999f) {
        return 1.0f;
    }

    // Edge fade: smoothly transition across the outer 8% of the coverage area
    const float2 dist_from_edge = saturate(min(uv, 1.0f - uv) * 12.5f);
    const float edge_fade = dist_from_edge.x * dist_from_edge.y;

    static const float2 s_dirs[8] = {
        float2( 1.0f,  0.0f),
        float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f),
        float2( 0.0f, -1.0f),
        float2( 0.70710678f,  0.70710678f),
        float2(-0.70710678f,  0.70710678f),
        float2( 0.70710678f, -0.70710678f),
        float2(-0.70710678f, -0.70710678f)
    };

#if SKY_VISIBILITY_ENABLE == 1
    const int NUM_DIRS = 4;
    const int NUM_STEPS = 3;
    static const float s_radii[3] = { 0.75f, 2.5f, 8.0f };
#else
    const int NUM_DIRS = 8;
    const int NUM_STEPS = 4;
    static const float s_radii[4] = { 0.5f, 1.6f, 4.5f, 12.0f };
#endif

    const float range = ShadowParams2.w > 1.0f ? ShadowParams2.w : 160.0f;

    // Interleaved gradient noise for jittering azimuthal direction per pixel to prevent banding
    const float noise = frac(52.9829189f * frac(dot(uv * 1024.0f, float2(0.06711056f, 0.00583715f))));
    const float angle = noise * (6.2831853f / float(NUM_DIRS));
    const float cos_a = cos(angle);
    const float sin_a = sin(angle);

    float total_visibility = 0.0f;
    float2 unoccluded_dir_sum = float2(0.0f, 0.0f);

    [unroll]
    for (int d = 0; d < NUM_DIRS; ++d) {
        const float2 base_dir = s_dirs[d];
        const float2 dir = float2(base_dir.x * cos_a - base_dir.y * sin_a,
                                  base_dir.x * sin_a + base_dir.y * cos_a);

        float max_dir_occlusion = 0.0f;

        [unroll]
        for (int s = 0; s < NUM_STEPS; ++s) {
            const float r = s_radii[s];
            const float2 sample_uv = uv + float2(dir.x, -dir.y) * (r / range);
            if (sample_uv.x >= 0.0f && sample_uv.x <= 1.0f &&
                sample_uv.y >= 0.0f && sample_uv.y <= 1.0f) {
                const float2 s_data = SkyVisibilityTex.SampleLevel(PointSampler, sample_uv, 0).rg;
                if (s_data.y > 0.5f) {
                    const float dh = s_data.x - world_pos.y;
                    if (dh > 0.05f) {
                        const float dist = sqrt(dh * dh + r * r);
                        const float sin_theta = dh / dist;

                        // Occluder unit direction vector in 3D
                        const float3 to_occluder = float3(dir.x * r, dh, dir.y * r) / dist;
                        const float n_dot_occ = dot(normal, to_occluder);

                        if (n_dot_occ > 0.0f) {
                            // Distance falloff (Far Cry 4 / LSAO): smooth quadratic attenuation up to 18 units
                            const float max_dist = 18.0f;
                            float dist_falloff = saturate(1.0f - dist / max_dist);
                            dist_falloff = dist_falloff * dist_falloff;

                            // Height clearance falloff: ambient light enters beneath high ceilings
                            const float max_height = 10.0f;
                            const float height_falloff = saturate(1.0f - dh / max_height);

                            const float step_occlusion = sin_theta * dist_falloff * height_falloff *
                                                         saturate(n_dot_occ * 1.25f);
                            max_dir_occlusion = max(max_dir_occlusion, step_occlusion);
                        }
                    }
                }
            }
        }

        const float dir_vis = 1.0f - saturate(max_dir_occlusion);
        total_visibility += dir_vis;
        unoccluded_dir_sum += dir * dir_vis;
    }

    float avg_visibility = total_visibility / float(NUM_DIRS);

    // Subtle undersurface darkening for faces pointing purely downward away from the sky
    if (normal.y < 0.0f) {
        avg_visibility *= lerp(1.0f, 0.65f, saturate(-normal.y));
    }

    const float strength = 0.70f;
    const float min_floor = 0.18f;
    float final_vis = saturate(lerp(1.0f, avg_visibility, strength));
    final_vis = max(min_floor, final_vis);

    // Compute bent normal toward open unoccluded sky
    const float unocc_len = length(unoccluded_dir_sum);
    float3 bent = normal;
    if (unocc_len > 1.0e-4f) {
        const float2 bent_horiz = unoccluded_dir_sum / unocc_len;
        const float bend_factor = (1.0f - final_vis) * 0.55f;
        bent = normalize(normal + float3(bent_horiz.x, 0.0f, bent_horiz.y) * bend_factor);
    }
    out_bent_normal = normalize(lerp(normal, bent, edge_fade));

    return lerp(1.0f, final_vis, edge_fade);
}

float4 ShadowMaskPS(FSOut input) : SV_Target {
    const float depth_value = DepthTex.SampleLevel(PointSampler, input.uv, 0).r;
    if (depth_value >= 0.99999f) {
        return float4(1.0f, 1.0f, 1.0f, 0.125f);
    }
    const float4 gb1 = GB1Tex.SampleLevel(PointSampler, input.uv, 0);
    const float3 world_pos = reconstruct_world(input.uv, depth_value);
    const float3 n = decode_octahedral(gb1.xy);
    const float view_z = mul(float4(world_pos, 1.0f), View).z;
    const float3 l = normalize(-SunDirection.xyz);
    const float no_l = saturate(dot(n, l));
    const float rotation =
        frac(sin(dot(input.uv, float2(12.9898f, 78.233f))) * 43758.5453f) * 6.2831853f;

    int cascade = 0;
    const float csm = sample_sun_shadow(world_pos, n, view_z, no_l, rotation, cascade);
    float contact = 1.0f;
#if CONTACT_SHADOW_STEPS > 0
    contact = trace_contact_shadow(world_pos, n, l, CONTACT_SHADOW_STEPS, rotation);
#endif
    float ssgs = 1.0f;
#if SSGS_STEPS > 0
    ssgs = trace_ssgs(world_pos, n, l, SSGS_STEPS, ShadowParams1.w, rotation + 1.7f);
#endif
    float shadow = csm;
#if CONTACT_SHADOW_STEPS > 0
    shadow *= contact;
#endif
#if SSGS_STEPS > 0
    shadow *= ssgs;
#endif
    return float4(shadow, contact, ssgs, float(cascade) / 4.0f + 0.125f);
}

float4 ShadowBlurPS(FSOut input) : SV_Target {
    const float2 texel = 1.0f / max(Params1.yz, float2(1.0f, 1.0f));
    const float4 center = ShadowMaskTex.SampleLevel(PointSampler, input.uv, 0);
    const float center_depth =
        linear_view_depth(DepthTex.SampleLevel(PointSampler, input.uv, 0).r);
    const float3 center_normal = decode_octahedral(GB1Tex.SampleLevel(PointSampler, input.uv, 0).xy);
    float3 sum = center.rgb;
    float weight_sum = 1.0f;
    const float depth_tolerance = max(center_depth * 0.025f, 0.04f);
    const float blur_sigma = float(SHADOW_BLUR_RADIUS) * 0.45f;
    const float blur_falloff = 1.0f / (2.0f * blur_sigma * blur_sigma);

    [unroll]
    for (int i = -SHADOW_BLUR_RADIUS; i <= SHADOW_BLUR_RADIUS; ++i) {
        if (i == 0) {
            continue;
        }
#if SHADOW_BLUR_DIR == 0
        const float2 offset = float2(float(i), 0.0f) * texel;
#else
        const float2 offset = float2(0.0f, float(i)) * texel;
#endif
        const float4 sample_value =
            ShadowMaskTex.SampleLevel(PointSampler, input.uv + offset, 0);
        const float sample_depth = linear_view_depth(
            DepthTex.SampleLevel(PointSampler, input.uv + offset, 0).r);
        const float3 sample_normal = decode_octahedral(
            GB1Tex.SampleLevel(PointSampler, input.uv + offset, 0).xy);

        const float ddiff = abs(sample_depth - center_depth) / depth_tolerance;
        const float ndot = saturate(dot(center_normal, sample_normal));

        const float spatial_weight = exp(-float(i * i) * blur_falloff);
        const float depth_weight = exp(-ddiff * ddiff);
        const float normal_weight = ndot * ndot;
        const float weight = spatial_weight * depth_weight * normal_weight;

        sum += sample_value.rgb * weight;
        weight_sum += weight;
    }
    return float4(sum / weight_sum, center.a);
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

    float specular_roughness = max(roughness, 0.04f);
#if SUN_SPECULAR_DISK_ENABLE
    const float sin_alpha = sin(SUN_ANGULAR_RADIUS);
    specular_roughness =
        sqrt(specular_roughness * specular_roughness + sin_alpha * sin_alpha);
#endif

    const float3 diffuse_base = base * (1.0f - metallic);
#if DIFFUSE_BRDF_TYPE == 0
    const float3 diffuse = diffuse_burley(diffuse_base, no_l, no_v, roughness);
#else
    const float3 diffuse = diffuse_eon(diffuse_base, n, l, v, no_l, no_v, roughness);
#endif

    const float d = D_GGX(no_h, specular_roughness);
    const float vis = V_SmithGGXCorrelated(max(no_v, 1.0e-4f), max(no_l, 1.0e-4f),
                                           specular_roughness);
    const float3 specular_term = d * vis * fresnel;
    float sun_limb = 1.0f;
#if SUN_LIMB_DARKENING
    sun_limb = lerp(0.82f, 1.0f, saturate(no_l));
#endif

    float3 sun_diffuse = float3(0.0f, 0.0f, 0.0f);
    float3 sun_specular = float3(0.0f, 0.0f, 0.0f);
    float4 shadow_mask = float4(1.0f, 1.0f, 1.0f, 0.125f);
#if SHADOW_MASK_ENABLE
    shadow_mask = ShadowMaskTex.SampleLevel(PointSampler, input.uv, 0);
#endif
    const float sun_shadow = shadow_mask.r;
#if SUN_LIGHT_ENABLE != 0
    sun_diffuse = diffuse * SunColor.rgb * no_l * sun_shadow;
    sun_specular = specular_term * SunColor.rgb * no_l * sun_limb * sun_shadow;
#endif

    float sky_visibility = 1.0f;
    float3 bent_n = n;
#if SKY_VISIBILITY_ENABLE > 0
    sky_visibility = sample_sky_visibility(world_pos, n, bent_n);
#endif
    const float bent_hemi = saturate(bent_n.y * 0.5f + 0.5f);
    const float3 ambient_tint = lerp(float3(0.40f, 0.40f, 0.45f), float3(1.0f, 1.0f, 1.0f), bent_hemi);
    const float3 ambient = AmbientColor.rgb * ambient_tint * base * (1.0f - metallic) * sky_visibility;

    float3 color = sun_diffuse + sun_specular + ambient + emissive;
#if DEBUG_VIEW == 8
    const float3 cascade_colors[4] = {float3(0.85f, 0.15f, 0.10f), float3(0.15f, 0.80f, 0.25f),
                                      float3(0.15f, 0.40f, 0.95f), float3(0.95f, 0.85f, 0.15f)};
    color = cascade_colors[(int)floor(shadow_mask.a * 4.0f)];
#elif DEBUG_VIEW == 9
    color = shadow_mask.rrr;
#elif DEBUG_VIEW == 10
    color = shadow_mask.ggg;
#elif DEBUG_VIEW == 11
    color = shadow_mask.bbb;
#elif DEBUG_VIEW == 12
    color = sky_visibility.rrr;
#endif
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

float luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 tonemap_reinhard(float3 color, float white_pt) {
    color = max(0.0f, color);
    const float luma = luminance(color);
    const float tone_mapped_luma = luma * (1.0f + luma / (white_pt * white_pt)) / (1.0f + luma);
    color *= tone_mapped_luma / max(luma, 1.0e-6f);
    return saturate(color);
}

float4 tonemap_hable_curve(float4 x) {
    const float a = 0.22f;
    const float b = 0.30f;
    const float c = 0.10f;
    const float d = 0.20f;
    const float e = 0.01f;
    const float f = 0.30f;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - (e / f);
}

float3 tonemap_hable(float3 color, float white_pt) {
    color = max(0.0f, color);
    float4 curr = tonemap_hable_curve(float4(color * 2.0f, white_pt));
    curr /= curr.w;
    return saturate(curr.rgb);
}

float3 tonemap_hejl(float3 hdr, float white_pt) {
    hdr = max(0.0f, hdr);
    const float4 vh = float4(hdr, white_pt);
    const float4 va = 1.425f * vh + 0.05f;
    const float4 vf = (vh * va + 0.004f) / (vh * (va + 0.55f) + 0.0491f) - 0.0821f;
    return saturate(vf.rgb / vf.www);
}

float3 tonemap_aces_srgb(float3 x) {
    x = max(0.0f, x);
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 tonemap_naughty_dog(float3 x) {
    x = max(0.0f, x);
    const float a = -2586.3655f;
    const float b = 0.6900f;
    const float c = -767.6706f;
    const float d = -8.5706f;
    const float e = 2.8784f;
    const float f = 107.4683f;
    return saturate(((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - (e / f));
}

float3 tonemap_aces_fitted(float3 color) {
    color = max(0.0f, color);
    static const float3x3 aces_input_mat = {
        0.59719f, 0.35458f, 0.04823f,
        0.07600f, 0.90834f, 0.01566f,
        0.02840f, 0.13383f, 0.83777f
    };
    static const float3x3 aces_output_mat = {
        1.60475f, -0.53108f, -0.07367f,
       -0.10208f,  1.10813f, -0.00605f,
       -0.00327f, -0.07276f,  1.07602f
    };
    color = mul(aces_input_mat, color);
    const float3 a = color * (color + 0.0245786f) - 0.000090537f;
    const float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;
    // Maps directly to sRGB display encoding; linearize so subsequent pipeline gamma behaves correctly
    return srgb_to_linear(saturate(mul(aces_output_mat, color)));
}

float3 tonemap_agx(float3 val, int look) {
    static const float3x3 agx_inset_mat = {
        0.842479065f, 0.078433600f, 0.079223745f,
        0.042328242f, 0.878468640f, 0.079166127f,
        0.042375655f, 0.078433600f, 0.879142973f
    };
    static const float3x3 agx_out_mat = {
        1.19687900f, -0.09802088f, -0.09902974f,
       -0.05289685f,  1.15190313f, -0.09896118f,
       -0.05297164f, -0.09804345f,  1.15107367f
    };

    val = max(1.0e-6f, val);
    val = mul(agx_inset_mat, val);

    // Normalized log2 encoding over [-12.47393 EV, +4.026069 EV] range (16.5 stops dynamic range)
    val = clamp((log2(val) + 12.47393f) / 16.5f, 0.0f, 1.0f);

    // Exact 6th-order polynomial fit of the official Blender 4.0 / OCIO AgX Base Contrast S-Curve
    val = val * (val * (val * (val * (val * (15.5f * val - 40.14f) + 31.96f) - 6.868f) + 0.4298f) + 0.1191f) - 0.00232f;

    if (look == 1) {
        // AgX Punchy look (CDL slope 1.0, power 1.35, sat 1.4)
        val = pow(max(0.0f, val), 1.35f);
        const float luma = dot(val, float3(0.2126f, 0.7152f, 0.0722f));
        val = luma + 1.4f * (val - luma);
    } else if (look == 2) {
        // AgX Golden look (CDL slope [1.0, 0.9, 0.5], power 0.8, sat 1.3)
        val = pow(max(0.0f, val * float3(1.0f, 0.9f, 0.5f)), 0.8f);
        const float luma = dot(val, float3(0.2126f, 0.7152f, 0.0722f));
        val = luma + 1.3f * (val - luma);
    }

    val = mul(agx_out_mat, val);

    // Maps directly to sRGB display encoding; linearize so subsequent pipeline gamma behaves correctly
    return srgb_to_linear(saturate(val));
}

float3 tonemap_pbr_neutral(float3 color) {
    const float start_compression = 0.8f - 0.04f;
    const float desaturation = 0.15f;

    color = max(0.0f, color);

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) {
        return saturate(color);
    }

    const float d = 1.0f - start_compression;
    const float new_peak = 1.0f - (d * d) / (peak + d - start_compression);
    color *= new_peak / peak;

    const float g = 1.0f - 1.0f / (desaturation * (peak - new_peak) + 1.0f);
    return saturate(lerp(color, new_peak.xxx, g));
}

float gt7_curve_eval(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    const float mid_point = 0.538f;
    const float toe_strength = 1.280f;
    const float linear_limit = 1.11f;
    const float ka = 2.96333f;
    const float kb = -3.37335f;
    const float kc = -0.53957f;

    if (x < linear_limit) {
        const float w = smoothstep(0.0f, mid_point, x);
        const float toe = mid_point * pow(max(x / mid_point, 0.0f), toe_strength);
        return (1.0f - w) * toe + w * x;
    }
    return ka + kb * exp(x * kc);
}

float3 tonemap_gt7(float3 color) {
    color = max(0.0f, color);
    const float3 curve_color = float3(
        gt7_curve_eval(color.r),
        gt7_curve_eval(color.g),
        gt7_curve_eval(color.b)
    );
    const float sdr_factor = 0.4f;
    const float3 mapped = curve_color * sdr_factor;

    const float peak = max(mapped.r, max(mapped.g, mapped.b));
    const float luma = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    const float desat = saturate((peak - 0.85f) * 4.0f);
    return saturate(lerp(mapped, luma.xxx, desat * 0.35f));
}

float4 TonemapPS(FSOut input) : SV_Target {
    const float3 hdr = max(0.0f, HdrTex.Sample(LinearSampler, input.uv).rgb * Params0.z);
    float3 color;
#if TONEMAP_OPERATOR == 0
    color = hdr;
#elif TONEMAP_OPERATOR == 1
    color = tonemap_reinhard(hdr, 4.0f);
#elif TONEMAP_OPERATOR == 2
    color = tonemap_hable(hdr, 4.0f);
#elif TONEMAP_OPERATOR == 3
    color = tonemap_hable(hdr, 8.0f);
#elif TONEMAP_OPERATOR == 4
    color = tonemap_hejl(hdr, 4.0f);
#elif TONEMAP_OPERATOR == 5
    color = tonemap_aces_srgb(hdr);
#elif TONEMAP_OPERATOR == 6
    color = tonemap_naughty_dog(hdr);
#elif TONEMAP_OPERATOR == 7
    color = tonemap_aces_fitted(hdr);
#elif TONEMAP_OPERATOR == 8
    color = tonemap_agx(hdr, 0);
#elif TONEMAP_OPERATOR == 9
    color = tonemap_agx(hdr, 1);
#elif TONEMAP_OPERATOR == 10
    color = tonemap_pbr_neutral(hdr);
#elif TONEMAP_OPERATOR == 11
    color = tonemap_gt7(hdr);
#elif TONEMAP_OPERATOR == 12
    color = tonemap_agx(hdr, 2);
#else
    color = hdr;
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
