//! Bone matrix palette constant buffer for GPU skinning.

use hikari_animation::math::Mat4;
use hikari_core::d3d11::D3D11Context;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;

pub const MAX_BONES: usize = 512;

#[repr(C)]
pub struct BoneMatrixBuffer {
    pub matrices: [[[f32; 4]; 4]; MAX_BONES],
}

pub struct GpuSkinningBuffer {
    pub buffer: ID3D11Buffer,
}

impl GpuSkinningBuffer {
    pub fn new(ctx: &D3D11Context) -> Result<Self> {
        let size = std::mem::size_of::<BoneMatrixBuffer>() as u32;
        let desc = D3D11_BUFFER_DESC {
            ByteWidth: size,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_CONSTANT_BUFFER.0 as u32,
            CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
            ..Default::default()
        };
        let mut buffer = None;
        unsafe {
            ctx.device.CreateBuffer(&desc, None, Some(&mut buffer))?;
        }
        Ok(Self {
            buffer: buffer.unwrap(),
        })
    }

    pub fn update(&self, ctx: &D3D11Context, palette: &[Mat4]) -> Result<()> {
        let count = palette.len().min(MAX_BONES);
        unsafe {
            let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
            ctx.context.Map(&self.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut mapped))?;
            let dst = mapped.pData as *mut Mat4;
            if count > 0 {
                std::ptr::copy_nonoverlapping(palette.as_ptr(), dst, count);
                for i in count..MAX_BONES {
                    *dst.add(i) = Mat4::IDENTITY;
                }
            } else {
                for i in 0..MAX_BONES {
                    *dst.add(i) = Mat4::IDENTITY;
                }
            }
            ctx.context.Unmap(&self.buffer, 0);
        }
        Ok(())
    }

    pub fn bind_vs(&self, ctx: &D3D11Context, slot: u32) {
        unsafe {
            let cbs = [Some(self.buffer.clone())];
            ctx.context.VSSetConstantBuffers(slot, Some(&cbs));
        }
    }
}
