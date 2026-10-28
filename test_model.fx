// ==============================================================================
// Hikari Direct3D 11 Test Model Effect (Shader Model 5.0)
// Compatible with MikuMikuDance & Hikari Effects Manager (Direct Win32 UI)
// ==============================================================================

// --- UI Configurable Parameters (Tweaked live via Hikari F10 Menu) ---
float Brightness <
    string UIName = "Яркость (Exposure)";
    string UIWidget = "Slider";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.05;
    string UIGroup = "Освещение";
    string UIHelp = "Регулировка общей экспозиции и яркости модели";
> = 1.0;

float3 TintColor <
    string UIName = "Оттенок (Tint Color)";
    string UIWidget = "Color";
    string UIGroup = "Цветокоррекция";
    string UIHelp = "Базовый оттенок модели";
> = float3(1.0, 1.0, 1.0);

bool EnableShading <
    string UIName = "Динамическое затенение";
    string UIWidget = "Checkbox";
    string UIGroup = "Шейдинг";
    string UIHelp = "Включение расчета диффузного освещения";
> = true;

int ShadingMode <
    string UIName = "Режим затенения";
    string UIWidget = "Combo";
    string UIItems = "Без освещения (Unlit),Ламберт (Lambert),Стилизованный (Toon),Контурный (RimLight)";
    string UIGroup = "Шейдинг";
> = 1;

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

    float3 baseColor = TintColor;

    if (EnableShading) {
        float NdotL = max(dot(normal, lightDir), 0.0);

        if (ShadingMode == 0) {
            // Unlit
        } else if (ShadingMode == 1) {
            // Lambert
            float3 diffuse = baseColor * (0.3 + 0.7 * NdotL);
            baseColor = diffuse;
        } else if (ShadingMode == 2) {
            // Toon
            float toon = smoothstep(0.3, 0.35, NdotL) * 0.6 + 0.4;
            baseColor *= toon;
        } else if (ShadingMode == 3) {
            // Rim light
            float rim = 1.0 - max(dot(viewDir, normal), 0.0);
            rim = pow(rim, 3.0);
            baseColor = baseColor * (0.4 + 0.6 * NdotL) + float3(1.0, 1.0, 1.0) * rim * 0.8;
        }
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
    float3 extrudedPos = input.Pos + input.Normal * 0.03f;
    output.Pos = mul(float4(extrudedPos, 1.0f), WorldViewProj);
    return output;
}

float4 PS_Edge(VS_EDGE_OUTPUT input) : SV_Target {
    return float4(0.1f, 0.1f, 0.1f, 1.0f);
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
        CullMode = CW;
        VertexShader = compile vs_5_0 VS_Edge();
        PixelShader  = compile ps_5_0 PS_Edge();
    }
}

technique ShadowTec <
    string MMDPass = "shadow";
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
    }
}
