//! High-level scene, skeletal model, and viewport grid renderer for DirectX 11.

use crate::camera::Camera;
use crate::material::GpuMaterial;
use crate::mesh::GpuMesh;
use crate::skinning::GpuSkinningBuffer;
use crate::texture::GpuTexture;
use hikari_animation::math::Mat4;
use hikari_core::d3d11::D3D11Context;
use hikari_core::shader::compile_hlsl;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::*;

#[repr(C)]
pub struct CameraConstants {
    pub view: [[f32; 4]; 4],
    pub projection: [[f32; 4]; 4],
    pub view_projection: [[f32; 4]; 4],
    pub camera_pos: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GridVertex {
    pub position: [f32; 3],
    pub color: [f32; 4],
}

pub struct SceneRenderer {
    pub camera_buffer: ID3D11Buffer,
    pub skinning_buffer: GpuSkinningBuffer,
    pub default_white_texture: GpuTexture,
    pub default_vs: ID3D11VertexShader,
    pub default_ps: ID3D11PixelShader,
    pub input_layout: ID3D11InputLayout,
    pub grid_vb: ID3D11Buffer,
    pub grid_vertex_count: u32,
    pub grid_vs: ID3D11VertexShader,
    pub grid_ps: ID3D11PixelShader,
    pub grid_layout: ID3D11InputLayout,
}

impl SceneRenderer {
    pub fn new(ctx: &D3D11Context) -> Result<Self> {
        let cam_desc = D3D11_BUFFER_DESC {
            ByteWidth: std::mem::size_of::<CameraConstants>() as u32,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_CONSTANT_BUFFER.0 as u32,
            CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
            ..Default::default()
        };
        let mut camera_buffer = None;
        unsafe {
            ctx.device.CreateBuffer(&cam_desc, None, Some(&mut camera_buffer))?;
        }
        let camera_buffer = camera_buffer.unwrap();

        let skinning_buffer = GpuSkinningBuffer::new(ctx)?;

        // Compile default standard MMD/SM5 vertex and pixel shaders
        let hlsl = r#"
cbuffer CameraParams : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ViewProjection;
    float4 CameraPos;
};

cbuffer MaterialParams : register(b1) {
    float4 Diffuse;
    float3 Specular;
    float Shininess;
    float3 Ambient;
    float EdgeSize;
    float4 EdgeColor;
};

cbuffer BoneMatrices : register(b2) {
    row_major float4x4 Bones[512];
};

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    uint4 bone_indices : BLENDINDICES;
    float4 bone_weights : BLENDWEIGHT;
};

struct VS_OUTPUT {
    float4 pos_clip : SV_POSITION;
    float3 normal_world : NORMAL;
    float2 uv : TEXCOORD;
    float3 pos_world : TEXCOORD1;
};

VS_OUTPUT VS_Main(VS_INPUT input) {
    VS_OUTPUT output;
    
    float total_w = input.bone_weights.x + input.bone_weights.y + input.bone_weights.z + input.bone_weights.w;
    float4 weights = (total_w > 0.0001f) ? (input.bone_weights / total_w) : float4(1.0f, 0.0f, 0.0f, 0.0f);
    
    // Skeletal skinning calculation
    float4x4 skin_matrix = Bones[input.bone_indices.x] * weights.x
                        + Bones[input.bone_indices.y] * weights.y
                        + Bones[input.bone_indices.z] * weights.z
                        + Bones[input.bone_indices.w] * weights.w;
    
    float4 pos_world = mul(float4(input.position, 1.0f), skin_matrix);
    float3 norm_world = normalize(mul(float4(input.normal, 0.0f), skin_matrix).xyz);
    
    output.pos_world = pos_world.xyz;
    output.normal_world = norm_world;
    output.uv = input.uv;
    output.pos_clip = mul(pos_world, ViewProjection);
    return output;
}

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

float4 PS_Main(VS_OUTPUT input) : SV_Target {
    float3 light_dir = normalize(float3(0.35f, 0.85f, -0.4f));
    float n_dot_l = dot(normalize(input.normal_world), light_dir);
    
    // Half-Lambert factor [0.0, 1.0]
    float half_lambert = n_dot_l * 0.5f + 0.5f;
    
    // 2-tone anime cell shading with smooth contour transition
    float toon = smoothstep(0.32f, 0.48f, half_lambert) * 0.4f + 0.6f;
    
    // Sample material diffuse texture
    float4 tex_color = DiffuseMap.Sample(LinearSampler, input.uv);
    
    // Alpha clip for transparent surfaces (hair fringes, eyelashes, lace)
    float alpha = tex_color.a * Diffuse.a;
    if (alpha < 0.05f) {
        discard;
    }
    
    // Rim lighting along model silhouettes for enhanced 3D volume
    float3 view_dir = normalize(CameraPos.xyz - input.pos_world);
    float rim = 1.0f - max(dot(normalize(input.normal_world), view_dir), 0.0f);
    float rim_term = pow(rim, 3.5f) * 0.2f;
    
    float3 base_color = Diffuse.rgb * tex_color.rgb;
    if (Diffuse.r + Diffuse.g + Diffuse.b < 0.01f) {
        base_color = tex_color.rgb;
    }
    
    float3 lit_color = base_color * toon + float3(rim_term, rim_term, rim_term);
    return float4(saturate(lit_color), alpha);
}
"#;

        let vs_bc = compile_hlsl(hlsl, "VS_Main", "vs_5_0", None, &[])
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e))?;
        let ps_bc = compile_hlsl(hlsl, "PS_Main", "ps_5_0", None, &[])
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e))?;

        let mut default_vs = None;
        let mut default_ps = None;
        unsafe {
            ctx.device.CreateVertexShader(&vs_bc, None, Some(&mut default_vs))?;
            ctx.device.CreatePixelShader(&ps_bc, None, Some(&mut default_ps))?;
        }

        // Input layout: POSITION (float3), NORMAL (float3), TEXCOORD (float2), BLENDINDICES (uint4), BLENDWEIGHT (float4)
        let layout_descs = [
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"POSITION\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 0,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"NORMAL\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 12,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"TEXCOORD\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 24,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"BLENDINDICES\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32A32_UINT,
                InputSlot: 0,
                AlignedByteOffset: 32,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"BLENDWEIGHT\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32A32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 48,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
        ];

        let mut input_layout = None;
        unsafe {
            ctx.device.CreateInputLayout(&layout_descs, &vs_bc, Some(&mut input_layout))?;
        }

        // Build Studio Floor Grid
        let mut grid_vertices = Vec::new();
        let extent = 30.0f32;
        let step = 2.0f32;
        let mut z = -extent;
        while z <= extent + 1e-4 {
            let color = if z.abs() < 1e-4 {
                [0.85, 0.25, 0.25, 1.0] // X Axis (Red)
            } else {
                [0.26, 0.26, 0.30, 1.0]
            };
            grid_vertices.push(GridVertex { position: [-extent, 0.0, z], color });
            grid_vertices.push(GridVertex { position: [extent, 0.0, z], color });
            z += step;
        }
        let mut x = -extent;
        while x <= extent + 1e-4 {
            let color = if x.abs() < 1e-4 {
                [0.25, 0.45, 0.85, 1.0] // Z Axis (Blue)
            } else {
                [0.26, 0.26, 0.30, 1.0]
            };
            grid_vertices.push(GridVertex { position: [x, 0.0, -extent], color });
            grid_vertices.push(GridVertex { position: [x, 0.0, extent], color });
            x += step;
        }

        let grid_vb_desc = D3D11_BUFFER_DESC {
            ByteWidth: (grid_vertices.len() * std::mem::size_of::<GridVertex>()) as u32,
            Usage: D3D11_USAGE_IMMUTABLE,
            BindFlags: D3D11_BIND_VERTEX_BUFFER.0 as u32,
            ..Default::default()
        };
        let grid_data = D3D11_SUBRESOURCE_DATA {
            pSysMem: grid_vertices.as_ptr() as *const _,
            ..Default::default()
        };
        let mut grid_vb = None;
        unsafe {
            ctx.device.CreateBuffer(&grid_vb_desc, Some(&grid_data), Some(&mut grid_vb))?;
        }

        let grid_hlsl = r#"
cbuffer CameraParams : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ViewProjection;
    float4 CameraPos;
};
struct GridIn {
    float3 pos : POSITION;
    float4 col : COLOR;
};
struct GridOut {
    float4 pos_clip : SV_POSITION;
    float4 col : COLOR;
};
GridOut Grid_VS(GridIn input) {
    GridOut output;
    output.pos_clip = mul(float4(input.pos, 1.0f), ViewProjection);
    output.col = input.col;
    return output;
}
float4 Grid_PS(GridOut input) : SV_Target {
    return input.col;
}
"#;
        let grid_vs_bc = compile_hlsl(grid_hlsl, "Grid_VS", "vs_5_0", None, &[])
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e))?;
        let grid_ps_bc = compile_hlsl(grid_hlsl, "Grid_PS", "ps_5_0", None, &[])
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e))?;

        let mut grid_vs = None;
        let mut grid_ps = None;
        unsafe {
            ctx.device.CreateVertexShader(&grid_vs_bc, None, Some(&mut grid_vs))?;
            ctx.device.CreatePixelShader(&grid_ps_bc, None, Some(&mut grid_ps))?;
        }

        let grid_layout_descs = [
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"POSITION\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 0,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
            D3D11_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(b"COLOR\0".as_ptr()),
                SemanticIndex: 0,
                Format: DXGI_FORMAT_R32G32B32A32_FLOAT,
                InputSlot: 0,
                AlignedByteOffset: 12,
                InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                InstanceDataStepRate: 0,
            },
        ];
        let mut grid_layout = None;
        unsafe {
            ctx.device.CreateInputLayout(&grid_layout_descs, &grid_vs_bc, Some(&mut grid_layout))?;
        }

        let default_white_texture = GpuTexture::create_solid_color(ctx, [255, 255, 255, 255])?;

        Ok(Self {
            camera_buffer,
            skinning_buffer,
            default_white_texture,
            default_vs: default_vs.unwrap(),
            default_ps: default_ps.unwrap(),
            input_layout: input_layout.unwrap(),
            grid_vb: grid_vb.unwrap(),
            grid_vertex_count: grid_vertices.len() as u32,
            grid_vs: grid_vs.unwrap(),
            grid_ps: grid_ps.unwrap(),
            grid_layout: grid_layout.unwrap(),
        })
    }

    pub fn update_camera(&self, ctx: &D3D11Context, camera: &Camera) -> Result<()> {
        let view = camera.view_matrix();
        let proj = camera.projection_matrix();
        let vp = camera.view_projection_matrix();

        let constants = CameraConstants {
            view: view.m,
            projection: proj.m,
            view_projection: vp.m,
            camera_pos: [camera.position.x, camera.position.y, camera.position.z, 1.0],
        };

        unsafe {
            let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
            ctx.context.Map(&self.camera_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut mapped))?;
            *(mapped.pData as *mut CameraConstants) = constants;
            ctx.context.Unmap(&self.camera_buffer, 0);
        }
        Ok(())
    }

    /// Renders the 3D ground plane coordinate grid.
    pub fn render_grid(&self, ctx: &D3D11Context, camera: &Camera) -> Result<()> {
        self.update_camera(ctx, camera)?;
        ctx.set_viewport(ctx.width as f32, ctx.height as f32);

        unsafe {
            ctx.context.IASetInputLayout(&self.grid_layout);
            ctx.context.VSSetShader(&self.grid_vs, None);
            ctx.context.PSSetShader(&self.grid_ps, None);

            let cam_cbs = [Some(self.camera_buffer.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cam_cbs));

            let stride = std::mem::size_of::<GridVertex>() as u32;
            let offset = 0u32;
            let vbs = [Some(self.grid_vb.clone())];
            ctx.context.IASetVertexBuffers(0, 1, Some(vbs.as_ptr()), Some(&stride), Some(&offset));
            ctx.context.IASetPrimitiveTopology(
                windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
            );

            if let Some(depth) = ctx.state_manager.get_depth(hikari_core::states::DepthPreset::ReadWriteLessEqual) {
                ctx.context.OMSetDepthStencilState(Some(depth), 1);
            }
            if let Some(blend) = ctx.state_manager.get_blend(hikari_core::states::BlendPreset::Opaque) {
                ctx.context.OMSetBlendState(Some(blend), Some(&[0.0; 4]), 0xFFFFFFFF);
            }

            ctx.context.Draw(self.grid_vertex_count, 0);
        }
        Ok(())
    }

    /// Renders a skinned PMX mesh with materials and textures on DirectX 11.
    pub fn render_mesh(
        &self,
        ctx: &D3D11Context,
        mesh: &GpuMesh,
        materials: &[GpuMaterial],
        textures: &[Option<GpuTexture>],
        skinning_palette: &[Mat4],
        camera: &Camera,
    ) -> Result<()> {
        self.update_camera(ctx, camera)?;
        self.skinning_buffer.update(ctx, skinning_palette)?;

        unsafe {
            ctx.set_viewport(ctx.width as f32, ctx.height as f32);

            // Bind shaders and input layout
            ctx.context.IASetInputLayout(&self.input_layout);
            ctx.context.VSSetShader(&self.default_vs, None);
            ctx.context.PSSetShader(&self.default_ps, None);

            // Bind camera buffer (b0) and skinning buffer (b2)
            let cam_cbs = [Some(self.camera_buffer.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cam_cbs));
            ctx.context.PSSetConstantBuffers(0, Some(&cam_cbs));

            self.skinning_buffer.bind_vs(ctx, 2);

            // Bind texture sampler (LinearWrap)
            if let Some(sampler) = ctx.state_manager.get_sampler(hikari_core::states::SamplerPreset::LinearWrap) {
                let samplers = [Some(sampler.clone())];
                ctx.context.PSSetSamplers(0, Some(&samplers));
            }

            // Set pipeline states (CullNoneSolid ensures all double-sided PMX mesh parts like hair and clothes render cleanly)
            if let Some(raster) = ctx.state_manager.get_rasterizer(hikari_core::states::RasterizerPreset::CullNoneSolid) {
                ctx.context.RSSetState(Some(raster));
            }
            if let Some(depth) = ctx.state_manager.get_depth(hikari_core::states::DepthPreset::ReadWriteLessEqual) {
                ctx.context.OMSetDepthStencilState(Some(depth), 1);
            }
            if let Some(blend) = ctx.state_manager.get_blend(hikari_core::states::BlendPreset::AlphaBlend) {
                ctx.context.OMSetBlendState(Some(blend), Some(&[0.0; 4]), 0xFFFFFFFF);
            }

            // Bind mesh buffers
            mesh.bind(ctx);

            // Draw each material subset with its diffuse texture
            for subset in &mesh.subsets {
                let mut srv_to_bind = &self.default_white_texture.srv;
                if subset.material_index < materials.len() {
                    let mat = &materials[subset.material_index];
                    mat.bind(ctx, 1);
                    if mat.texture_index >= 0 && (mat.texture_index as usize) < textures.len() {
                        if let Some(ref tex) = textures[mat.texture_index as usize] {
                            srv_to_bind = &tex.srv;
                        }
                    }
                }
                let srvs = [Some(srv_to_bind.clone())];
                ctx.context.PSSetShaderResources(0, Some(&srvs));
                ctx.context.DrawIndexed(subset.index_count, subset.index_start, 0);
            }
        }

        Ok(())
    }
}
