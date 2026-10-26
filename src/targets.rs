//! Multiple Render Target (MRT) and Unordered Access View (UAV) Engine for Hikari.
//!
//! Provides creation, binding, and management of Direct3D 11 Render Targets,
//! Depth-Stencil Buffers, RWStructuredBuffers, and G-Buffer pipelines.

use std::collections::HashMap;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::*;

/// Represents an allocated offscreen 2D render target with RTV and SRV.
pub struct OffscreenRenderTarget {
    pub texture: ID3D11Texture2D,
    pub rtv: ID3D11RenderTargetView,
    pub srv: ID3D11ShaderResourceView,
    pub uav: Option<ID3D11UnorderedAccessView>,
    pub width: u32,
    pub height: u32,
    pub format: DXGI_FORMAT,
}

/// Represents an allocated depth-stencil surface with DSV and SRV.
pub struct DepthStencilTarget {
    pub texture: ID3D11Texture2D,
    pub dsv: ID3D11DepthStencilView,
    pub srv: Option<ID3D11ShaderResourceView>,
    pub width: u32,
    pub height: u32,
}

/// Represents a GPU Structured Buffer with optional UAV and SRV.
pub struct StructuredGpuBuffer {
    pub buffer: ID3D11Buffer,
    pub srv: ID3D11ShaderResourceView,
    pub uav: Option<ID3D11UnorderedAccessView>,
    pub element_size: u32,
    pub element_count: u32,
}

/// Manages multiple render targets (MRT), depth buffers, and UAV resources.
pub struct RenderTargetManager {
    named_targets: HashMap<String, OffscreenRenderTarget>,
    named_depths: HashMap<String, DepthStencilTarget>,
    named_buffers: HashMap<String, StructuredGpuBuffer>,
    active_rtvs: [Option<ID3D11RenderTargetView>; 8],
    active_dsv: Option<ID3D11DepthStencilView>,
    active_uavs: [Option<ID3D11UnorderedAccessView>; 8],
}

impl RenderTargetManager {
    pub fn new() -> Self {
        Self {
            named_targets: HashMap::new(),
            named_depths: HashMap::new(),
            named_buffers: HashMap::new(),
            active_rtvs: [None, None, None, None, None, None, None, None],
            active_dsv: None,
            active_uavs: [None, None, None, None, None, None, None, None],
        }
    }

    /// Allocates or updates a named 2D render target (e.g., G-Buffer normal, albedo, HDR accumulation).
    pub fn create_target(
        &mut self,
        device: &ID3D11Device,
        name: &str,
        width: u32,
        height: u32,
        format: DXGI_FORMAT,
        enable_uav: bool,
    ) -> Result<()> {
        let width = width.max(1);
        let height = height.max(1);

        let needs_creation = match self.named_targets.get(name) {
            Some(t) => t.width != width || t.height != height || t.format != format,
            None => true,
        };

        if needs_creation {
            let mut bind_flags = (D3D11_BIND_RENDER_TARGET.0 | D3D11_BIND_SHADER_RESOURCE.0) as u32;
            if enable_uav {
                bind_flags |= D3D11_BIND_UNORDERED_ACCESS.0 as u32;
            }

            let tex_desc = D3D11_TEXTURE2D_DESC {
                Width: width,
                Height: height,
                MipLevels: 1,
                ArraySize: 1,
                Format: format,
                SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
                Usage: D3D11_USAGE_DEFAULT,
                BindFlags: bind_flags,
                CPUAccessFlags: 0,
                MiscFlags: 0,
            };

            let mut texture = None;
            unsafe { device.CreateTexture2D(&tex_desc, None, Some(&mut texture)) }?;
            let texture = texture.unwrap();

            let mut rtv = None;
            unsafe { device.CreateRenderTargetView(&texture, None, Some(&mut rtv)) }?;
            let rtv = rtv.unwrap();

            let mut srv = None;
            unsafe { device.CreateShaderResourceView(&texture, None, Some(&mut srv)) }?;
            let srv = srv.unwrap();

            let uav = if enable_uav {
                let mut u = None;
                unsafe { device.CreateUnorderedAccessView(&texture, None, Some(&mut u)) }?;
                Some(u.unwrap())
            } else {
                None
            };

            self.named_targets.insert(
                name.to_string(),
                OffscreenRenderTarget {
                    texture,
                    rtv,
                    srv,
                    uav,
                    width,
                    height,
                    format,
                },
            );
        }

        Ok(())
    }

    /// Allocates or updates a named depth-stencil target with optional shader sampling.
    pub fn create_depth(
        &mut self,
        device: &ID3D11Device,
        name: &str,
        width: u32,
        height: u32,
    ) -> Result<()> {
        let width = width.max(1);
        let height = height.max(1);

        let needs_creation = match self.named_depths.get(name) {
            Some(d) => d.width != width || d.height != height,
            None => true,
        };

        if needs_creation {
            let tex_desc = D3D11_TEXTURE2D_DESC {
                Width: width,
                Height: height,
                MipLevels: 1,
                ArraySize: 1,
                Format: DXGI_FORMAT_R24G8_TYPELESS,
                SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
                Usage: D3D11_USAGE_DEFAULT,
                BindFlags: (D3D11_BIND_DEPTH_STENCIL.0 | D3D11_BIND_SHADER_RESOURCE.0) as u32,
                CPUAccessFlags: 0,
                MiscFlags: 0,
            };

            let mut texture = None;
            unsafe { device.CreateTexture2D(&tex_desc, None, Some(&mut texture)) }?;
            let texture = texture.unwrap();

            let dsv_desc = D3D11_DEPTH_STENCIL_VIEW_DESC {
                Format: DXGI_FORMAT_D24_UNORM_S8_UINT,
                ViewDimension: D3D11_DSV_DIMENSION_TEXTURE2D,
                Flags: 0,
                Anonymous: unsafe { std::mem::zeroed() },
            };

            let mut dsv = None;
            unsafe { device.CreateDepthStencilView(&texture, Some(&dsv_desc), Some(&mut dsv)) }?;
            let dsv = dsv.unwrap();

            let srv_desc = D3D11_SHADER_RESOURCE_VIEW_DESC {
                Format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                ViewDimension: windows::Win32::Graphics::Direct3D::D3D_SRV_DIMENSION_TEXTURE2D,
                Anonymous: unsafe {
                    let mut a: D3D11_SHADER_RESOURCE_VIEW_DESC_0 = std::mem::zeroed();
                    a.Texture2D.MipLevels = 1;
                    a.Texture2D.MostDetailedMip = 0;
                    a
                },
            };

            let mut srv = None;
            unsafe { device.CreateShaderResourceView(&texture, Some(&srv_desc), Some(&mut srv)) }?;

            self.named_depths.insert(
                name.to_string(),
                DepthStencilTarget {
                    texture,
                    dsv,
                    srv,
                    width,
                    height,
                },
            );
        }

        Ok(())
    }

    /// Creates a Structured Buffer for compute / particle / skinning passes.
    pub fn create_structured_buffer(
        &mut self,
        device: &ID3D11Device,
        name: &str,
        element_size: u32,
        element_count: u32,
        enable_uav: bool,
    ) -> Result<()> {
        let total_bytes = (element_size * element_count).max(16);
        let mut bind_flags = D3D11_BIND_SHADER_RESOURCE.0 as u32;
        if enable_uav {
            bind_flags |= D3D11_BIND_UNORDERED_ACCESS.0 as u32;
        }

        let desc = D3D11_BUFFER_DESC {
            ByteWidth: total_bytes,
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: bind_flags,
            CPUAccessFlags: 0,
            MiscFlags: D3D11_RESOURCE_MISC_BUFFER_STRUCTURED.0 as u32,
            StructureByteStride: element_size,
        };

        let mut buffer = None;
        unsafe { device.CreateBuffer(&desc, None, Some(&mut buffer)) }?;
        let buffer = buffer.unwrap();

        let srv_desc = D3D11_SHADER_RESOURCE_VIEW_DESC {
            Format: DXGI_FORMAT_UNKNOWN,
            ViewDimension: windows::Win32::Graphics::Direct3D::D3D_SRV_DIMENSION_BUFFEREX,
            Anonymous: unsafe {
                let mut a: D3D11_SHADER_RESOURCE_VIEW_DESC_0 = std::mem::zeroed();
                a.BufferEx.NumElements = element_count;
                a.BufferEx.Flags = 0;
                a
            },
        };

        let mut srv = None;
        unsafe { device.CreateShaderResourceView(&buffer, Some(&srv_desc), Some(&mut srv)) }?;
        let srv = srv.unwrap();

        let uav = if enable_uav {
            let uav_desc = D3D11_UNORDERED_ACCESS_VIEW_DESC {
                Format: DXGI_FORMAT_UNKNOWN,
                ViewDimension: D3D11_UAV_DIMENSION_BUFFER,
                Anonymous: unsafe {
                    let mut a: D3D11_UNORDERED_ACCESS_VIEW_DESC_0 = std::mem::zeroed();
                    a.Buffer.NumElements = element_count;
                    a.Buffer.Flags = 0;
                    a
                },
            };
            let mut u = None;
            unsafe { device.CreateUnorderedAccessView(&buffer, Some(&uav_desc), Some(&mut u)) }?;
            Some(u.unwrap())
        } else {
            None
        };

        self.named_buffers.insert(
            name.to_string(),
            StructuredGpuBuffer {
                buffer,
                srv,
                uav,
                element_size,
                element_count,
            },
        );

        Ok(())
    }

    pub fn get_target(&self, name: &str) -> Option<&OffscreenRenderTarget> {
        self.named_targets.get(name)
    }

    pub fn get_rtv(&self, name: &str) -> Option<ID3D11RenderTargetView> {
        self.named_targets.get(name).map(|t| t.rtv.clone())
    }

    pub fn get_depth(&self, name: &str) -> Option<&DepthStencilTarget> {
        self.named_depths.get(name)
    }

    pub fn get_dsv(&self, name: &str) -> Option<ID3D11DepthStencilView> {
        self.named_depths.get(name).map(|d| d.dsv.clone())
    }

    pub fn get_buffer(&self, name: &str) -> Option<&StructuredGpuBuffer> {
        self.named_buffers.get(name)
    }

    /// Binds multiple render targets (MRT) and depth stencil to the Output Merger (OM).
    pub fn bind_mrt(
        &mut self,
        context: &ID3D11DeviceContext,
        rtvs: &[Option<ID3D11RenderTargetView>],
        dsv: Option<&ID3D11DepthStencilView>,
    ) {
        let count = rtvs.len().min(8);
        for i in 0..8 {
            self.active_rtvs[i] = if i < count { rtvs[i].clone() } else { None };
        }
        self.active_dsv = dsv.cloned();

        let rtv_slice: Vec<Option<ID3D11RenderTargetView>> = self.active_rtvs[..count].to_vec();
        unsafe {
            context.OMSetRenderTargets(Some(&rtv_slice), dsv);
        }
    }

    /// Clears a specific render target slot with an RGBA color.
    pub fn clear_render_target(&self, context: &ID3D11DeviceContext, slot: usize, color: [f32; 4]) {
        if slot < 8 {
            if let Some(ref rtv) = self.active_rtvs[slot] {
                unsafe {
                    context.ClearRenderTargetView(rtv, &color);
                }
            }
        }
    }

    /// Clears the active depth-stencil view.
    pub fn clear_depth_stencil(&self, context: &ID3D11DeviceContext, clear_depth: bool, depth: f32, clear_stencil: bool, stencil: u8) {
        if let Some(ref dsv) = self.active_dsv {
            let mut flags = 0u32;
            if clear_depth {
                flags |= D3D11_CLEAR_DEPTH.0;
            }
            if clear_stencil {
                flags |= D3D11_CLEAR_STENCIL.0;
            }
            if flags != 0 {
                unsafe {
                    context.ClearDepthStencilView(dsv, flags, depth, stencil);
                }
            }
        }
    }
}
