//! GPU Mesh buffers (Vertex, Index, and Subsets) for DirectX 11.

use hikari_animation::math::Vec3;
use hikari_core::d3d11::D3D11Context;
use hikari_formats::pmx::{PmxModel, PmxWeight};
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_R32_UINT;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ModelVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
    pub bone_indices: [u32; 4],
    pub bone_weights: [f32; 4],
}

#[derive(Debug, Clone)]
pub struct MeshSubset {
    pub material_index: usize,
    pub index_start: u32,
    pub index_count: u32,
}

pub struct GpuMesh {
    pub vertex_buffer: ID3D11Buffer,
    pub index_buffer: ID3D11Buffer,
    pub vertex_count: u32,
    pub index_count: u32,
    pub subsets: Vec<MeshSubset>,
    pub base_vertices: Vec<ModelVertex>,
    pub current_vertices: Vec<ModelVertex>,
}

impl GpuMesh {
    pub fn from_pmx(ctx: &D3D11Context, pmx: &PmxModel) -> Result<Self> {
        let mut vertices = Vec::with_capacity(pmx.vertices.len());

        for v in &pmx.vertices {
            let (indices, weights) = match &v.weight {
                PmxWeight::Bdef1(b) => ([(*b).max(0) as u32, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]),
                PmxWeight::Bdef2 { bone1, bone2, weight1 } => (
                    [(*bone1).max(0) as u32, (*bone2).max(0) as u32, 0, 0],
                    [*weight1, 1.0 - *weight1, 0.0, 0.0],
                ),
                PmxWeight::Bdef4 { bones, weights } | PmxWeight::Qdef { bones, weights } => (
                    [
                        bones[0].max(0) as u32,
                        bones[1].max(0) as u32,
                        bones[2].max(0) as u32,
                        bones[3].max(0) as u32,
                    ],
                    *weights,
                ),
                PmxWeight::Sdef { bone1, bone2, weight1, .. } => (
                    [(*bone1).max(0) as u32, (*bone2).max(0) as u32, 0, 0],
                    [*weight1, 1.0 - *weight1, 0.0, 0.0],
                ),
            };

            vertices.push(ModelVertex {
                position: v.position,
                normal: v.normal,
                uv: v.uv,
                bone_indices: indices,
                bone_weights: weights,
            });
        }

        let vb_desc = D3D11_BUFFER_DESC {
            ByteWidth: (vertices.len() * std::mem::size_of::<ModelVertex>()) as u32,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_VERTEX_BUFFER.0 as u32,
            CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
            ..Default::default()
        };
        let vb_data = D3D11_SUBRESOURCE_DATA {
            pSysMem: vertices.as_ptr() as *const _,
            ..Default::default()
        };
        let mut vertex_buffer = None;
        unsafe {
            ctx.device.CreateBuffer(&vb_desc, Some(&vb_data), Some(&mut vertex_buffer))?;
        }
        let vertex_buffer = vertex_buffer.unwrap();

        let ib_desc = D3D11_BUFFER_DESC {
            ByteWidth: (pmx.indices.len() * std::mem::size_of::<u32>()) as u32,
            Usage: D3D11_USAGE_IMMUTABLE,
            BindFlags: D3D11_BIND_INDEX_BUFFER.0 as u32,
            ..Default::default()
        };
        let ib_data = D3D11_SUBRESOURCE_DATA {
            pSysMem: pmx.indices.as_ptr() as *const _,
            ..Default::default()
        };
        let mut index_buffer = None;
        unsafe {
            ctx.device.CreateBuffer(&ib_desc, Some(&ib_data), Some(&mut index_buffer))?;
        }
        let index_buffer = index_buffer.unwrap();

        let mut subsets = Vec::with_capacity(pmx.materials.len());
        let mut curr_start = 0u32;
        for (i, m) in pmx.materials.iter().enumerate() {
            let count = m.surface_count as u32;
            subsets.push(MeshSubset {
                material_index: i,
                index_start: curr_start,
                index_count: count,
            });
            curr_start += count;
        }

        let base_vertices = vertices.clone();
        let current_vertices = vertices;

        Ok(Self {
            vertex_buffer,
            index_buffer,
            vertex_count: base_vertices.len() as u32,
            index_count: pmx.indices.len() as u32,
            subsets,
            base_vertices,
            current_vertices,
        })
    }

    /// Dynamically applies cumulative vertex position and UV morph offsets and streams to GPU.
    pub fn apply_morph_offsets(
        &mut self,
        ctx: &D3D11Context,
        pos_offsets: &[Vec3],
        uv_offsets: &[[f32; 2]],
    ) -> Result<()> {
        if pos_offsets.is_empty() && uv_offsets.is_empty() {
            return Ok(());
        }

        let mut changed = false;
        let count = self.base_vertices.len();
        for i in 0..count {
            let base = &self.base_vertices[i];
            let p_off = if i < pos_offsets.len() { pos_offsets[i] } else { Vec3::ZERO };
            let uv_off = if i < uv_offsets.len() { uv_offsets[i] } else { [0.0, 0.0] };

            let has_p = p_off.x != 0.0 || p_off.y != 0.0 || p_off.z != 0.0;
            let has_uv = uv_off[0] != 0.0 || uv_off[1] != 0.0;

            if has_p || has_uv {
                changed = true;
                self.current_vertices[i].position = [
                    base.position[0] + p_off.x,
                    base.position[1] + p_off.y,
                    base.position[2] + p_off.z,
                ];
                self.current_vertices[i].uv = [
                    base.uv[0] + uv_off[0],
                    base.uv[1] + uv_off[1],
                ];
            } else if self.current_vertices[i].position != base.position
                || self.current_vertices[i].uv != base.uv
            {
                changed = true;
                self.current_vertices[i].position = base.position;
                self.current_vertices[i].uv = base.uv;
            }
        }

        if changed {
            unsafe {
                let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
                ctx.context.Map(
                    &self.vertex_buffer,
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    Some(&mut mapped),
                )?;
                std::ptr::copy_nonoverlapping(
                    self.current_vertices.as_ptr(),
                    mapped.pData as *mut ModelVertex,
                    self.current_vertices.len(),
                );
                ctx.context.Unmap(&self.vertex_buffer, 0);
            }
        }

        Ok(())
    }

    pub fn bind(&self, ctx: &D3D11Context) {
        let stride = std::mem::size_of::<ModelVertex>() as u32;
        let offset = 0u32;
        unsafe {
            let vbs = [Some(self.vertex_buffer.clone())];
            ctx.context.IASetVertexBuffers(0, 1, Some(vbs.as_ptr()), Some(&stride), Some(&offset));
            ctx.context.IASetIndexBuffer(&self.index_buffer, DXGI_FORMAT_R32_UINT, 0);
            ctx.context.IASetPrimitiveTopology(
                windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            );
        }
    }
}
