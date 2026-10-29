// ==============================================================================
// Hikari Direct3D 11 Post-Process Controller Effect (Shader Model 5.0)
// Demonstrates automatic companion auto-binding and dynamic offscreen tabs
// ==============================================================================

// --- Offscreen Render Targets (Declaring these automatically creates tabs in Hikari UI) ---

texture2D DepthBuffer : OFFSCREENRENDERTARGET <
    string Description = "Z-Buffer and Depth Stencil render target";
    string DefaultEffect = "*.pmx=./depth.fx; *=hide;";
>;

texture2D NormalMap : OFFSCREENRENDERTARGET <
    string Description = "World Normal and Geometric Specular buffer";
    string DefaultEffect = "*.pmx=./normal.fx; *=hide;";
>;

texture2D BloomBuffer : OFFSCREENRENDERTARGET <
    string Description = "HDR Bloom and Emission Extraction";
    string DefaultEffect = "*=none;";
>;

// --- Constant Buffer (Slot b0) with UI Parameters ---
cbuffer AccessoryConstants : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    row_major float4x4 World         : WORLD;
    row_major float4x4 View          : VIEW;

    float4 LightDirection            : DIRECTION < string Object = "Light"; >;
    float4 CameraPosition            : POSITION  < string Object = "Camera"; >;

    // --- Interactive Win32 UI Parameters ---
    float BloomThreshold <
        string UIName = "Порог свечения (Bloom Threshold)";
        string UIWidget = "Slider";
        float UIMin = 0.0;
        float UIMax = 2.0;
        float UIStep = 0.05;
        string UIGroup = "Постобработка";
        string UIHelp = "Минимальная яркость для эффекта свечения";
    > = 0.8;

    float BloomIntensity <
        string UIName = "Интенсивность Bloom";
        string UIWidget = "Slider";
        float UIMin = 0.0;
        float UIMax = 5.0;
        float UIStep = 0.1;
        string UIGroup = "Постобработка";
    > = 1.2;

    float3 VignetteColor <
        string UIName = "Цвет виньетки";
        string UIWidget = "Color";
        string UIGroup = "Виньетка";
    > = float3(0.0, 0.0, 0.0);

    bool EnablePostProcess <
        string UIName = "Включить постобработку";
        string UIWidget = "Checkbox";
        string UIGroup = "Эффекты";
    > = true;
};

// --- Shaders ---
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

    // Cyan glowing controller mesh
    float3 color = float3(0.2, 0.8, 1.0) * lighting;
    return float4(color, 1.0);
}

// --- Standard Object Techniques ---
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

// --- Offscreen Target Techniques ---
technique DepthTech <
    string MMDPass = "DepthBuffer";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}

technique NormalTech <
    string MMDPass = "NormalMap";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}

technique BloomTech <
    string MMDPass = "BloomBuffer";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}
