// ==============================================================================
// Hikari Direct3D 11 Simple Model Effect (Shader Model 5.0)
// ==============================================================================

cbuffer ModelConstants : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    row_major float4x4 World         : WORLD;
    row_major float4x4 View          : VIEW;

    float4 LightDirection            : DIRECTION < string Object = "Light"; >;
    float4 CameraPosition            : POSITION  < string Object = "Camera"; >;

    float4 MaterialDiffuse           : DIFFUSE  < string Object = "Geometry"; >;
    float4 MaterialAmbient           : AMBIENT  < string Object = "Geometry"; >;
    float4 MaterialSpecular          : SPECULAR < string Object = "Geometry"; >;
};

Texture2D MaterialTexture : MATERIALTEXTURE;
SamplerState SamplerLinear : register(s0);

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

VS_OUTPUT VS_Main(VS_INPUT input) {
    VS_OUTPUT output;
    output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProj);
    output.WorldPos = mul(float4(input.Pos, 1.0f), World).xyz;
    output.Normal = normalize(mul(float4(input.Normal, 0.0f), World).xyz);
    output.TexCoord = input.TexCoord;
    return output;
}

float4 PS_Main(VS_OUTPUT input) : SV_Target {
    float3 normal = normalize(input.Normal);
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 lighting = 0.4 + 0.6 * NdotL;

    float4 tex = MaterialTexture.Sample(SamplerLinear, input.TexCoord);
    float3 baseColor = (tex.a > 0.05) ? tex.rgb : float3(0.2, 0.8, 1.0);
    return float4(baseColor * lighting, 1.0);
}

technique MainTec <
    string MMDPass = "object";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}

technique MainTecSS <
    string MMDPass = "object_ss";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}
