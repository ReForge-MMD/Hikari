# Hikari (光)

> High-performance Direct3D 11 runtime and Shader Model 5.0 engine for MikuMikuDance (MMD) & MikuMikuEffect (MME), engineered in pure Rust.

Hikari provides a pure Direct3D 11 (Feature Level 11_0) engine and runtime. It unlocks full Shader Model 5.0 capabilities, hardware tessellation, 6-stage graphics pipelines, GPGPU Compute Shaders with Unordered Access Views (UAV), Multiple Render Targets (MRT) for deferred rendering, and a 64KB constant buffer reflection architecture.

---

## Table of Contents
- [Architecture Overview](#architecture-overview)
- [Key Features](#key-features)
  - [Full 6-Stage SM 5.0 Graphics & Compute](#1-full-6-stage-sm-50-graphics--compute)
  - [Dynamic Shader Reflection & 64KB CBuffer Engine](#2-dynamic-shader-reflection--64kb-cbuffer-engine)
  - [MME Annotation & Scripting Subsystem](#3-mme-annotation--scripting-subsystem)
  - [Multiple Render Targets (MRT) & UAV Compute](#4-multiple-render-targets-mrt--uav-compute)
  - [Pipeline State Object Management](#5-pipeline-state-object-management)
  - [PMX 2.0 Vertex Input Layouts](#6-pmx-20-vertex-input-layouts)
  - [Standalone DirectX 11 Studio & Pipeline](#7-standalone-directx-11-studio--pipeline)
- [Effect Porting Guide (D3D9 to D3D11)](#effect-porting-guide-d3d9-to-d3d11)
  - [1. Shifting Shader Compilation Profiles](#1-shifting-shader-compilation-profiles)
  - [2. Separating Textures and Samplers](#2-separating-textures-and-samplers)
  - [3. Constant Buffer Structuring](#3-constant-buffer-structuring)
  - [4. MME Render Scripts & Post-Processing](#4-mme-render-scripts--post-processing)
- [Building & Testing](#building--testing)
- [Installation in MMD](#installation-in-mmd)
- [Known Behaviors & Technical Nuances](#known-behaviors--technical-nuances)
- [License](#license)

---

## Architecture Overview

```
                          ┌───────────────────────────┐
                          │   MMD / MME Application   │
                          └─────────────┬─────────────┘
                                        │ DirectX 11 Host / Studio
                                        ▼
                          ┌───────────────────────────┐
                          │     Hikari Effect Engine  │
                          │  - MME Annotations Parser │
                          │  - 64KB CBuffer Registry  │
                          └─────────────┬─────────────┘
                                        │
           ┌────────────────────────────┼────────────────────────────┐
           ▼                            ▼                            ▼
┌──────────────────────┐     ┌──────────────────────┐     ┌──────────────────────┐
│  Pipeline States     │     │  Shader Reflection   │     │  Targets & Compute   │
│  - BlendState        │     │  - D3DReflect        │     │  - MRT (8 RTVs+DSV)  │
│  - DepthStencilState │     │  - CBuffers & Vars   │     │  - UAVs & Buffers    │
│  - RasterizerState   │     │  - Textures t0..t127 │     │  - DispatchCompute   │
│  - SamplerState      │     │  - Samplers s0..s15  │     │  - Readback Staging  │
└──────────┬───────────┘     └──────────┬───────────┘     └──────────┬───────────┘
           │                            │                            │
           └────────────────────────────┼────────────────────────────┘
                                        │
                                        ▼
                          ┌───────────────────────────┐
                          │      Direct3D 11 Core     │
                          │   VS - HS - DS - GS - PS  │
                          │         CS Pipeline       │
                          │  DrawPrimitiveUP (Quads)  │
                          └───────────────────────────┘
```

---

## Key Features

### 1. Full 6-Stage SM 5.0 Graphics & Compute
- Compiles HLSL source files directly using the system `d3dcompiler_47.dll`.
- Supports all 6 programmable shader stages in Shader Model 5.0:
  - **Vertex Shader (`vs_5_0`)**
  - **Hull Shader (`hs_5_0`)** — hardware tessellation control
  - **Domain Shader (`ds_5_0`)** — hardware tessellation evaluation
  - **Geometry Shader (`gs_5_0`)** — primitive expansion & stream output
  - **Pixel Shader (`ps_5_0`)**
  - **Compute Shader (`cs_5_0`)** — general-purpose GPU compute kernels
- Dispatches compute work via `ID3D11DeviceContext::Dispatch` with asynchronous and staging buffer readbacks.

### 2. Dynamic Shader Reflection & 64KB CBuffer Engine
- Uses `D3DReflect` (`IID_ID3D11ShaderReflection`) to extract:
  - Constant buffer slots (`b0..b13`), variable member byte offsets, and variable sizes.
  - Resource bindings for Shader Resource Views (`t0..t127`), Samplers (`s0..s15`), and UAVs (`u0..u63`).
  - Thread group dimensions `[X, Y, Z]` for Compute Shaders.
- Allocates a unified 64KB constant buffer (`b0`) supporting dynamic updates through `ID3DXEffect` COM methods:
  - `SetFloat`, `SetInt`, `SetBool`, `SetVector`, `SetMatrix`, `SetValue`.
- Features case-insensitive semantic lookup for standard MMD matrices and vectors (`WORLD`, `VIEW`, `PROJECTION`, `WORLDVIEWPROJECTION`, `CAMERA_POSITION`, `LIGHT_DIRECTION`, `TIME`, etc.).

### 3. MME Annotation & Scripting Subsystem
- Quote-aware annotation block parser respecting semicolons within strings and multi-line definitions.
- Full COM API support for technique, pass, and variable annotations:
  - `GetAnnotation(hHandle, Index)`
  - `GetAnnotationByName(hHandle, pName)`
  - `GetString(hHandle, ppString)`
  - `GetValue(hHandle, pData, Bytes)`
  - `GetFloat(hHandle, pFloat)`, `GetInt`, `GetBool`, `GetVector`, `GetMatrix`.
- Transparently exposes MME script directives (`MMDPass`, `Script`, `ResourceName`, `Format`, `ViewportRatio`) to MME's script runner.

### 4. Multiple Render Targets (MRT) & UAV Compute
- `RenderTargetManager` supporting up to 8 simultaneous Render Target Views (`ID3D11RenderTargetView`) + 1 Depth Stencil View (`ID3D11DepthStencilView`).
- Essential for deferred rendering G-buffers (e.g. Albedo, Normal, Roughness, Depth, Motion Vectors).
- Unordered Access Views (`ID3D11UnorderedAccessView`) for GPU-side simulation, physics, ray-tracing, and post-process passes.

### 5. Pipeline State Object Management
- Dedicated `StateManager` with hash caching to minimize Direct3D 11 state object allocations:
  - **Blend States**: `Opaque`, `AlphaBlend`, `Additive`, `Subtractive`.
  - **Depth-Stencil States**: `Default` (read/write), `ReadOnly` (depth test only), `None` (disabled), `AlwaysWrite`.
  - **Rasterizer States**: `CullBackSolid`, `CullNoneSolid`, `CullFrontSolid`, `Wireframe`.
  - **Sampler States**: `LinearWrap`, `LinearClamp`, `PointClamp`, `AnisotropicWrap`.
- Automatically maps HLSL pass state assignments (`AlphaBlendEnable`, `DestBlend`, `ZEnable`, `ZWriteEnable`, `FillMode`, `CullMode`) to D3D11 state objects.

### 6. PMX 2.0 Vertex Input Layouts
- Layout caching for all MMD vertex configurations:
  - **PMX SDEF / Full Skinned** (stride 52: Position, Normal, UV, BlendWeight4, BlendIndices4, SDEF-C, SDEF-R0, SDEF-R1).
  - **PMX BDEF4** (stride 44: Position, Normal, UV, BlendWeight4, BlendIndices4).
  - **PMX BDEF2** (stride 38: Position, Normal, UV, BlendWeight2, BlendIndices2).
  - **Static Model** (stride 32: Position, Normal, UV).
  - **Screen Quad / Post-Process** (stride 20: Position, UV).

### 7. Standalone DirectX 11 Studio & Pipeline
Directly runs as a standalone DirectX 11 runtime and viewer:
- **Pure DX11**: Operates exclusively with Feature Level 11_0, eliminating all legacy D3D9 proxies and wrappers.
- **Hikari Studio**: Interactive real-time PMX/VMD viewer with IK physics solver, facial morphs, and unthrottled GPU pipeline.
- **SM 5.0 Effects**: Full HLSL SM 5.0 rendering with compute shaders, tessellation, and constant buffer reflection.

---

## Effect Porting Guide (D3D9 to D3D11)

When porting complex effects (such as Ray-MMD, SSAO, SSR, ToneMapping, or Volumetric Fog) to Hikari, follow these essential HLSL guidelines:

### 1. Shifting Shader Compilation Profiles
In Direct3D 9, passes declared:
```hlsl
// OLD (D3D9):
pass P0 {
    VertexShader = compile vs_3_0 VS_Main();
    PixelShader = compile ps_3_0 PS_Main();
}
```
Update these declarations to target Shader Model 5.0:
```hlsl
// NEW (DirectX 11 / SM 5.0):
pass P0 {
    VertexShader = compile vs_5_0 VS_Main();
    PixelShader = compile ps_5_0 PS_Main();
}
```

### 2. Separating Textures and Samplers
In Direct3D 9, textures and samplers were combined into a single `sampler2D` block:
```hlsl
// OLD (D3D9):
texture2D DiffuseTexture : MATERIALTEXTURE;
sampler2D DiffuseSampler = sampler_state {
    Texture = <DiffuseTexture>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

float4 PS(float2 uv : TEXCOORD0) : COLOR {
    return tex2D(DiffuseSampler, uv);
}
```
In Shader Model 5.0, decouple the texture resource from the sampler state:
```hlsl
// NEW (DirectX 11 / SM 5.0):
Texture2D DiffuseTexture : MATERIALTEXTURE : register(t0);
SamplerState DiffuseSampler : register(s0);

float4 PS(float2 uv : TEXCOORD0) : SV_Target {
    return DiffuseTexture.Sample(DiffuseSampler, uv);
}
```

### 3. Constant Buffer Structuring
Wrap all parameters, matrices, and lighting variables in explicit `cbuffer` blocks bound to `register(b0)`:
```hlsl
cbuffer GlobalConstants : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    row_major float4x4 World         : WORLD;
    float3 CameraPosition            : CAMERA_POSITION;
    float  Time                      : TIME;
    float4 FogColor;
    float  Exposure;
};
```
*Note*: `row_major` ensures matrix layout matches standard MMD matrix formatting without manual transposition.

### 4. MME Render Scripts & Post-Processing
For offscreen G-buffers and post-processing passes, declare textures with annotations and pass scripts:
```hlsl
texture2D DepthBuffer : RENDERDEPTHSTENCILTARGET <
    float2 ViewportRatio = {1.0, 1.0};
    string Format = "D24S8";
>;

texture2D ColorBuffer : RENDERCOLORTARGET <
    float2 ViewportRatio = {1.0, 1.0};
    string Format = "A16B16G16R16F";
>;

technique DeferredLighting <
    string MMDPass = "object";
> {
    pass GBuffer {
        string Script = "RenderColorTarget0=ColorBuffer; RenderDepthStencilTarget=DepthBuffer; Draw=Geometry;";
        VertexShader = compile vs_5_0 VS_GBuffer();
        PixelShader = compile ps_5_0 PS_GBuffer();
    }

    pass PostProcess {
        string Script = "RenderColorTarget0=; Draw=Buffer;";
        VertexShader = compile vs_5_0 VS_Quad();
        PixelShader = compile ps_5_0 PS_ToneMapping();
    }
}
```
Hikari automatically intercepts `Draw=Buffer;` through `DrawPrimitiveUP` and binds `ColorBuffer` / `DepthBuffer` to D3D11 render targets.

---

## Building & Testing

### Prerequisites
- [Rust toolchain](https://rustup.rs/) (stable, 1.70+ recommended, x86_64-pc-windows-msvc)
- Windows 10 or 11 (64-bit)
- `d3dcompiler_47.dll` (included by default in Windows)

### Running Integration Tests
To execute all 13 unit and integration tests:
```bash
cargo test
```

### Compiling Release Binaries
To build the optimized production binaries:
```bash
cargo build --release
```
The compiled binaries (`hikari_studio.exe`, `hikari_core.dll`, `HikariEffects.dll`) will be output to `target\release\`.

---

## Installation in MMD

1. Hikari runs as a standalone Direct3D 9 proxy loader, directly replacing MMEffect.
2. Build the workspace:
   ```powershell
   cargo build --release
   ```
3. Deploy the binaries and test shaders to your MMD root directory:
   ```powershell
   .\deploy.ps1 -Release -MmdDir "C:\Path\To\MikuMikuDance"
   ```
4. Launch `MikuMikuDance.exe`. Hikari will automatically initialize its DirectX 11 pipeline and inject the effect management menu into the main menu bar. Check `hikari.log` in the MMD root directory for initialization logs.

---

## Known Behaviors & Technical Nuances

- **DirectX 11 Native**: Hikari renders models and effects purely through native DirectX 11 without legacy DirectX 9 runtimes, proxies, or shared texture copies.
- **Fixed-Function Removal**: Direct3D 9 fixed-function pipeline states (such as `LightEnable`, `TextureTransformFlags`, `Ambient`) are unsupported in Shader Model 5.0. Shaders must handle lighting and transformations entirely within the programmable stages.
- **Include Files**: HLSL `#include` statements are supported natively via `D3D_COMPILE_STANDARD_FILE_INCLUDE`. Included files must be located relative to the shader file or the current working directory.

---

## License

Licensed under either of:
- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT License ([LICENSE-MIT](LICENSE-MIT))

at your option.
