// ==============================================================================
// Hikari Direct3D 11 Test Model Effect (Shader Model 5.0)
// Compatible with MikuMikuDance & MikuMikuEffect (via Hikari Translation Layer)
// ==============================================================================

// --- Constant Buffer (Slot b0) ---
cbuffer ModelConstants : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    row_major float4x4 World         : WORLD;
    row_major float4x4 View          : VIEW;

    float4 LightDirection            : DIRECTION < string Object = "Light"; >;
    float4 CameraPosition            : POSITION  < string Object = "Camera"; >;

    float4 MaterialDiffuse           : DIFFUSE  < string Object = "Geometry"; >;
    float4 MaterialAmbient           : AMBIENT  < string Object = "Geometry"; >;
    float4 MaterialSpecular          : SPECULAR < string Object = "Geometry"; >;

    float  Time                      : TIME;
    bool   use_texture;
};

// --- Material Texture & Sampler ---
Texture2D MaterialTexture : MATERIALTEXTURE <
    string ResourceName = "";
>;

SamplerState SamplerLinear : register(s0);

// --- Vertex Input / Output ---
struct VS_INPUT {
    float3 Pos      : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 Pos      : SV_POSITION;
    float3 WorldPos : POSITION1;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

// --- Main Vertex Shader ---
VS_OUTPUT VS_Main(VS_INPUT input) {
    VS_OUTPUT output;

    // Transform position to clip space
    output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProj);

    // Transform position and normal to world space
    output.WorldPos = mul(float4(input.Pos, 1.0f), World).xyz;
    output.Normal = normalize(mul(float4(input.Normal, 0.0f), World).xyz);

    output.TexCoord = input.TexCoord;
    return output;
}

// --- Main Pixel Shader (solid red test output) ---
float4 PS_Main(VS_OUTPUT input) : SV_Target {
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

// --- Outline / Edge Shaders ---
struct VS_EDGE_OUTPUT {
    float4 Pos : SV_POSITION;
};

VS_EDGE_OUTPUT VS_Edge(VS_INPUT input) {
    VS_EDGE_OUTPUT output;
    float3 extrudedPos = input.Pos + input.Normal * 0.03f;
    output.Pos = mul(float4(extrudedPos, 1.0f), WorldViewProj);
    return output;
}

float4 PS_Edge(VS_EDGE_OUTPUT input) : SV_Target {
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

// ==============================================================================
// Techniques for MikuMikuEffect
// ==============================================================================

// Primary technique applied to 3D PMD/PMX models
technique MainTec <
    string MMDPass = "object";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}

// Self-shadow pass technique
technique MainTecSS <
    string MMDPass = "object_ss";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}

// Edge / Outline pass technique
technique EdgeTec <
    string MMDPass = "edge";
> {
    pass P0 {
        CullMode = CW;
        VertexShader = compile vs_5_0 VS_Edge();
        PixelShader  = compile ps_5_0 PS_Edge();
    }
}

// Ground shadow pass technique
technique ShadowTec <
    string MMDPass = "shadow";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}
