// ==============================================================================
// Hikari Direct3D 11 Red Tint Effect (Shader Model 5.0)
// Compatible with MikuMikuDance & Hikari Effects Manager
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

    // --- UI Configurable Parameters (Live Tweaks via Hikari F10 Menu) ---
    float Brightness <
        string UIName = "Яркость (Exposure)";
        string UIWidget = "Slider";
        float UIMin = 0.0;
        float UIMax = 3.0;
        float UIStep = 0.05;
        string UIGroup = "Освещение";
        string UIHelp = "Регулировка общей экспозиции и свечения";
    > = 1.0;

    float3 RedColor <
        string UIName = "Красный оттенок (Red Tint)";
        string UIWidget = "Color";
        string UIGroup = "Цвет и стиль";
        string UIHelp = "Основной красный цвет заливки модели";
    > = float3(1.0, 0.08, 0.12);

    float RedGlow <
        string UIName = "Интенсивность свечения (Rim Glow)";
        string UIWidget = "Slider";
        float UIMin = 0.0;
        float UIMax = 2.0;
        float UIStep = 0.05;
        string UIGroup = "Цвет и стиль";
        string UIHelp = "Сила неонового контурного свечения по краям";
    > = 0.6;

    bool EnableShading <
        string UIName = "Динамическое освещение";
        string UIWidget = "Checkbox";
        string UIGroup = "Шейдинг";
        string UIHelp = "Включение расчета светотени";
    > = true;

    int ShadingMode <
        string UIName = "Режим затенения";
        string UIWidget = "Combo";
        string UIItems = "Чистый красный (Unlit),Ламберт (Lambert),Аниме-Тун (Toon),Неоновый контур (Neon Rim)";
        string UIGroup = "Шейдинг";
    > = 1;

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

    output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProj);
    output.WorldPos = mul(float4(input.Pos, 1.0f), World).xyz;
    output.Normal = normalize(mul(float4(input.Normal, 0.0f), World).xyz);
    output.TexCoord = input.TexCoord;
    return output;
}

// --- Main Pixel Shader ---
float4 PS_Main(VS_OUTPUT input) : SV_Target {
    float3 normal = normalize(input.Normal);
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float3 viewDir = normalize(CameraPosition.xyz - input.WorldPos);

    float3 baseColor = RedColor;

    if (EnableShading) {
        float NdotL = max(dot(normal, lightDir), 0.0);

        if (ShadingMode == 0) {
            // Unlit - flat red
        } else if (ShadingMode == 1) {
            // Lambert red shading
            float3 diffuse = baseColor * (0.25 + 0.75 * NdotL);
            baseColor = diffuse;
        } else if (ShadingMode == 2) {
            // Toon red cel-shading
            float toon = smoothstep(0.3, 0.35, NdotL) * 0.6 + 0.4;
            baseColor *= toon;
        } else if (ShadingMode == 3) {
            // Neon Rim red
            float rim = 1.0 - max(dot(viewDir, normal), 0.0);
            rim = pow(rim, 3.0);
            baseColor = baseColor * (0.3 + 0.7 * NdotL) + float3(1.0, 0.2, 0.2) * rim * RedGlow;
        }
    }

    // Additional rim glow if RedGlow > 0
    if (RedGlow > 0.0) {
        float rim = 1.0 - max(dot(viewDir, normal), 0.0);
        rim = pow(rim, 3.5);
        baseColor += float3(1.0, 0.1, 0.15) * rim * RedGlow;
    }

    baseColor *= Brightness;
    return float4(baseColor, 1.0);
}

// --- Outline / Edge Shaders ---
struct VS_EDGE_OUTPUT {
    float4 Pos : SV_POSITION;
};

VS_EDGE_OUTPUT VS_Edge(VS_INPUT input) {
    VS_EDGE_OUTPUT output;
    float3 extrudedPos = input.Pos + input.Normal * 0.025f;
    output.Pos = mul(float4(extrudedPos, 1.0f), WorldViewProj);
    return output;
}

float4 PS_Edge(VS_EDGE_OUTPUT input) : SV_Target {
    return float4(0.4f, 0.0f, 0.0f, 1.0f); // Dark red outline
}

// ==============================================================================
// Techniques for MikuMikuEffect & Hikari Direct Mode
// ==============================================================================

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

technique EdgeTec <
    string MMDPass = "edge";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Edge();
        PixelShader  = compile ps_5_0 PS_Edge();
    }
}
