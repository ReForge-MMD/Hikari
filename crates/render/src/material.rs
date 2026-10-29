//! Material parameters and constant buffers for DirectX 11.

use hikari_core::d3d11::D3D11Context;
use hikari_formats::pmx::PmxMaterial;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct MaterialConstants {
    pub diffuse: [f32; 4],
    pub specular: [f32; 3],
    pub shininess: f32,
    pub ambient: [f32; 3],
    pub edge_size: f32,
    pub edge_color: [f32; 4],
}

pub struct GpuMaterial {
    pub name: String,
    pub constants: MaterialConstants,
    pub constant_buffer: ID3D11Buffer,
    pub texture_index: i32,
}

impl GpuMaterial {
    pub fn from_pmx_material(ctx: &D3D11Context, mat: &PmxMaterial) -> Result<Self> {
        let constants = MaterialConstants {
            diffuse: mat.diffuse,
            specular: mat.specular,
            shininess: mat.shininess,
            ambient: mat.ambient,
            edge_size: mat.edge_size,
            edge_color: mat.edge_color,
        };

        let desc = D3D11_BUFFER_DESC {
            ByteWidth: std::mem::size_of::<MaterialConstants>() as u32,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_CONSTANT_BUFFER.0 as u32,
            CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
            ..Default::default()
        };
        let data = D3D11_SUBRESOURCE_DATA {
            pSysMem: &constants as *const _ as *const _,
            ..Default::default()
        };
        let mut buffer = None;
        unsafe {
            ctx.device.CreateBuffer(&desc, Some(&data), Some(&mut buffer))?;
        }

        Ok(Self {
            name: mat.name_local.clone(),
            constants,
            constant_buffer: buffer.unwrap(),
            texture_index: mat.texture_index,
        })
    }

    pub fn bind(&self, ctx: &D3D11Context, slot: u32) {
        unsafe {
            let cbs = [Some(self.constant_buffer.clone())];
            ctx.context.PSSetConstantBuffers(slot, Some(&cbs));
            ctx.context.VSSetConstantBuffers(slot, Some(&cbs));
        }
    }
}
