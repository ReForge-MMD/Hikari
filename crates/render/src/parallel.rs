//! Multi-threaded pass-based scene rendering infrastructure for DirectX 11.
//!
//! Enables recording independent render passes (Grid, Shadow, Models, Stages, Accessories)
//! concurrently across worker threads into ID3D11CommandList instances using pooled
//! deferred contexts, and executing them sequentially on the immediate context.

use crate::material::GpuMaterial;
use crate::mesh::GpuMesh;
use crate::scene_renderer::SceneRenderer;
use crate::skinning::GpuSkinningBuffer;
use crate::texture::GpuTexture;
use hikari_core::d3d11::{D3D11Context, SendCommandList, SendDeviceContext};
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::{ID3D11CommandList, ID3D11DeviceContext};

/// A discrete render pass item that can be recorded on an independent thread.
pub enum ScenePassItem<'a> {
    /// 3D floor coordinate grid pass
    Grid,
    /// Skinned mesh model pass (renders all or a subset range of materials)
    Mesh {
        mesh: &'a GpuMesh,
        materials: &'a [GpuMaterial],
        textures: &'a [Option<GpuTexture>],
        skinning_buffer: &'a GpuSkinningBuffer,
        subset_range: std::ops::Range<usize>,
    },
}

/// High-level multi-threaded frame renderer that executes render passes in parallel.
pub struct ParallelFrameRenderer;

impl ParallelFrameRenderer {
    /// Records an arbitrary list of render passes concurrently on worker threads using D3D11 deferred contexts.
    ///
    /// Preserves strict sequential submission order when executed on the immediate context.
    pub fn record_passes_parallel(
        ctx: &D3D11Context,
        renderer: &SceneRenderer,
        passes: &[ScenePassItem],
    ) -> Result<Vec<ID3D11CommandList>> {
        if passes.is_empty() {
            return Ok(Vec::new());
        }

        if passes.len() == 1 {
            let deferred = ctx.acquire_deferred_context()?;
            let cl = Self::record_single_pass(ctx, renderer, &deferred, &passes[0])?;
            ctx.recycle_deferred_context(deferred);
            return Ok(vec![cl]);
        }

        let mut deferred_contexts = Vec::with_capacity(passes.len());
        for _ in 0..passes.len() {
            let d = ctx.acquire_deferred_context()?;
            deferred_contexts.push(SendDeviceContext(d));
        }

        let results: Vec<std::result::Result<SendCommandList, String>> = std::thread::scope(|s| {
            let mut handles = Vec::new();
            for (pass, d_ctx) in passes.iter().zip(deferred_contexts.iter()) {
                let d_ref = &d_ctx.0;
                let handle = s.spawn(move || -> std::result::Result<SendCommandList, String> {
                    match Self::record_single_pass(ctx, renderer, d_ref, pass) {
                        Ok(cl) => Ok(SendCommandList(cl)),
                        Err(e) => Err(format!("Pass recording failed: {:?}", e)),
                    }
                });
                handles.push(handle);
            }

            handles
                .into_iter()
                .map(|h| h.join().unwrap_or_else(|_| Err("Worker thread panicked".into())))
                .collect()
        });

        for d in deferred_contexts {
            ctx.recycle_deferred_context(d.0);
        }

        let mut command_lists = Vec::with_capacity(results.len());
        for res in results {
            let send_cl = res.map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e))?;
            command_lists.push(send_cl.0);
        }

        Ok(command_lists)
    }

    /// Records a single pass on a given deferred context.
    pub fn record_single_pass(
        ctx: &D3D11Context,
        renderer: &SceneRenderer,
        deferred_ctx: &ID3D11DeviceContext,
        pass: &ScenePassItem,
    ) -> Result<ID3D11CommandList> {
        match pass {
            ScenePassItem::Grid => {
                renderer.record_grid(ctx, deferred_ctx)
            }
            ScenePassItem::Mesh {
                mesh,
                materials,
                textures,
                skinning_buffer,
                subset_range,
            } => {
                renderer.record_mesh_subsets(
                    ctx,
                    deferred_ctx,
                    mesh,
                    materials,
                    textures,
                    skinning_buffer,
                    subset_range.clone(),
                )
            }
        }
    }
}
