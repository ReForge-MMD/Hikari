//! Native Direct3D 11 device, context, swap chain, and state management for Hikari.

use std::collections::HashMap;
use std::ffi::c_void;
use std::ptr::null_mut;
use windows::core::{Interface, Result, HRESULT};
use crate::states::StateManager;
use crate::targets::RenderTargetManager;
use windows::Win32::Foundation::{HMODULE, HWND};
use windows::Win32::Graphics::Direct3D::{
    D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP,
    D3D_FEATURE_LEVEL, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1,
};
use windows::Win32::Graphics::Direct3D11::{
    D3D11CreateDevice, ID3D11BlendState, ID3D11Buffer, ID3D11DepthStencilState,
    ID3D11DepthStencilView, ID3D11Device, ID3D11DeviceContext, ID3D11InputLayout,
    ID3D11RasterizerState, ID3D11RenderTargetView, ID3D11SamplerState,
    ID3D11ShaderResourceView, ID3D11Texture2D, ID3D11UnorderedAccessView,
    D3D11_BIND_CONSTANT_BUFFER, D3D11_BIND_DEPTH_STENCIL, D3D11_BIND_INDEX_BUFFER,
    D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_UNORDERED_ACCESS,
    D3D11_BIND_VERTEX_BUFFER, D3D11_BLEND_DESC, D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BUFFER_DESC,
    D3D11_BUFFER_UAV, D3D11_BUFFEREX_SRV, D3D11_CLEAR_DEPTH, D3D11_CLEAR_STENCIL,
    D3D11_COLOR_WRITE_ENABLE_ALL, D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_LESS_EQUAL,
    D3D11_CPU_ACCESS_WRITE, D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_CREATE_DEVICE_FLAG,
    D3D11_CULL_BACK, D3D11_CULL_NONE, D3D11_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC,
    D3D11_DEPTH_WRITE_MASK_ALL, D3D11_DEPTH_WRITE_MASK_ZERO, D3D11_DSV_DIMENSION_TEXTURE2D,
    D3D11_FILL_SOLID, D3D11_FILL_WIREFRAME, D3D11_FILTER_MIN_MAG_MIP_LINEAR,
    D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_INPUT_ELEMENT_DESC, D3D11_INPUT_PER_VERTEX_DATA,
    D3D11_MAP_WRITE_DISCARD, D3D11_MAPPED_SUBRESOURCE, D3D11_RASTERIZER_DESC,
    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
    D3D11_SAMPLER_DESC, D3D11_SDK_VERSION, D3D11_SUBRESOURCE_DATA, D3D11_TEXTURE2D_DESC,
    D3D11_TEX2D_UAV, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP,
    D3D11_UAV_DIMENSION_BUFFER, D3D11_UAV_DIMENSION_TEXTURE2D, D3D11_USAGE_DEFAULT,
    D3D11_USAGE_DYNAMIC, D3D11_VIEWPORT,
};
use windows::Win32::Graphics::Dxgi::Common::{
    DXGI_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_UNKNOWN, DXGI_MODE_DESC, DXGI_RATIONAL, DXGI_SAMPLE_DESC,
};
use windows::Win32::Graphics::Dxgi::{
    IDXGIDevice, IDXGIFactory1, IDXGISwapChain,
    DXGI_PRESENT, DXGI_SWAP_CHAIN_DESC, DXGI_SWAP_CHAIN_FLAG,
    DXGI_SWAP_EFFECT_DISCARD, DXGI_USAGE_RENDER_TARGET_OUTPUT,
};

/// Encapsulates native Direct3D 11 rendering infrastructure.
pub struct D3D11Context {
    pub device: ID3D11Device,
    pub context: ID3D11DeviceContext,
    pub swap_chain: Option<IDXGISwapChain>,
    pub render_target_view: Option<ID3D11RenderTargetView>,
    pub depth_stencil_view: Option<ID3D11DepthStencilView>,
    pub depth_stencil_texture: Option<ID3D11Texture2D>,
    pub width: u32,
    pub height: u32,
    pub hwnd: HWND,
    pub feature_level: D3D_FEATURE_LEVEL,

    // Pre-allocated pipeline states
    pub raster_solid_cull_none: ID3D11RasterizerState,
    pub raster_solid_cull_back: ID3D11RasterizerState,
    pub raster_wireframe: ID3D11RasterizerState,
    pub depth_enabled: ID3D11DepthStencilState,
    pub depth_disabled: ID3D11DepthStencilState,
    pub blend_opaque: ID3D11BlendState,
    pub blend_alpha: ID3D11BlendState,
    pub sampler_linear_wrap: ID3D11SamplerState,
    pub sampler_linear_clamp: ID3D11SamplerState,
    pub sampler_point_clamp: ID3D11SamplerState,

    // Dynamic geometry buffer & layout caches for MMD draw bridging
    pub dynamic_vb: Option<ID3D11Buffer>,
    pub dynamic_vb_size: usize,
    pub dynamic_ib: Option<ID3D11Buffer>,
    pub dynamic_ib_size: usize,
    pub input_layout_cache: HashMap<(usize, u32), ID3D11InputLayout>,
    pub texture_cache: HashMap<usize, ID3D11ShaderResourceView>,
    pub state_manager: StateManager,
    pub render_targets: RenderTargetManager,
}

unsafe impl Send for D3D11Context {}
unsafe impl Sync for D3D11Context {}

impl D3D11Context {
    /// Dynamic HLSL shader profile for vertex shaders based on active feature level.
    pub fn vs_profile(&self) -> &'static str {
        match self.feature_level {
            D3D_FEATURE_LEVEL_11_1 | D3D_FEATURE_LEVEL_11_0 => "vs_5_0",
            D3D_FEATURE_LEVEL_10_1 => "vs_4_1",
            D3D_FEATURE_LEVEL_10_0 => "vs_4_0",
            _ => "vs_5_0",
        }
    }

    /// Dynamic HLSL shader profile for pixel shaders based on active feature level.
    pub fn ps_profile(&self) -> &'static str {
        match self.feature_level {
            D3D_FEATURE_LEVEL_11_1 | D3D_FEATURE_LEVEL_11_0 => "ps_5_0",
            D3D_FEATURE_LEVEL_10_1 => "ps_4_1",
            D3D_FEATURE_LEVEL_10_0 => "ps_4_0",
            _ => "ps_5_0",
        }
    }

    /// Dispatches a Compute Shader grid (X, Y, Z thread groups).
    pub fn dispatch_compute(&self, thread_group_count_x: u32, thread_group_count_y: u32, thread_group_count_z: u32) {
        unsafe {
            self.context.Dispatch(
                thread_group_count_x.max(1),
                thread_group_count_y.max(1),
                thread_group_count_z.max(1),
            );
        }
    }

    /// Dispatches a Compute Shader grid with arguments from a GPU buffer (indirect compute).
    pub fn dispatch_compute_indirect(&self, buffer: &ID3D11Buffer, aligned_byte_offset_for_args: u32) {
        unsafe {
            self.context.DispatchIndirect(buffer, aligned_byte_offset_for_args);
        }
    }

    /// Binds Unordered Access Views (UAVs) to the Compute Shader stage.
    pub fn cs_set_uavs(&self, start_slot: u32, uavs: &[Option<ID3D11UnorderedAccessView>]) {
        unsafe {
            let ptr = if uavs.is_empty() { None } else { Some(uavs.as_ptr()) };
            self.context.CSSetUnorderedAccessViews(start_slot, uavs.len() as u32, ptr, None);
        }
    }

    /// Creates a headless or window-bound Direct3D 11 context.
    pub fn new(hwnd: HWND, width: u32, height: u32) -> Result<Self> {
        let feature_levels = [D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0];
        let mut device_opt: Option<ID3D11Device> = None;
        let mut context_opt: Option<ID3D11DeviceContext> = None;
        let mut feature_level = D3D_FEATURE_LEVEL_11_0;

        let flags = D3D11_CREATE_DEVICE_FLAG(D3D11_CREATE_DEVICE_BGRA_SUPPORT.0);

        // Try hardware driver first, fall back to WARP
        let mut hr = unsafe {
            D3D11CreateDevice(
                None,
                D3D_DRIVER_TYPE_HARDWARE,
                HMODULE(null_mut()),
                flags,
                Some(&feature_levels),
                D3D11_SDK_VERSION,
                Some(&mut device_opt),
                Some(&mut feature_level),
                Some(&mut context_opt),
            )
        };

        if hr.is_err() {
            hr = unsafe {
                D3D11CreateDevice(
                    None,
                    D3D_DRIVER_TYPE_WARP,
                    HMODULE(null_mut()),
                    flags,
                    Some(&feature_levels),
                    D3D11_SDK_VERSION,
                    Some(&mut device_opt),
                    Some(&mut feature_level),
                    Some(&mut context_opt),
                )
            };
        }

        hr?;

        let device = device_opt.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let context = context_opt.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;

        // Initialize default pipeline states
        let raster_solid_cull_none = Self::create_rasterizer_state(&device, D3D11_CULL_NONE, D3D11_FILL_SOLID)?;
        let raster_solid_cull_back = Self::create_rasterizer_state(&device, D3D11_CULL_BACK, D3D11_FILL_SOLID)?;
        let raster_wireframe = Self::create_rasterizer_state(&device, D3D11_CULL_NONE, D3D11_FILL_WIREFRAME)?;

        let depth_enabled = Self::create_depth_stencil_state(&device, true, true)?;
        let depth_disabled = Self::create_depth_stencil_state(&device, false, false)?;

        let blend_opaque = Self::create_blend_state(&device, false)?;
        let blend_alpha = Self::create_blend_state(&device, true)?;

        let sampler_linear_wrap = Self::create_sampler_state(&device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP)?;
        let sampler_linear_clamp = Self::create_sampler_state(&device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP)?;
        let sampler_point_clamp = Self::create_sampler_state(&device, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP)?;

        let state_manager = StateManager::new(&device)?;
        let render_targets = RenderTargetManager::new();

        let mut ctx = Self {
            device,
            context,
            swap_chain: None,
            render_target_view: None,
            depth_stencil_view: None,
            depth_stencil_texture: None,
            width: width.max(1),
            height: height.max(1),
            hwnd,
            feature_level,
            raster_solid_cull_none,
            raster_solid_cull_back,
            raster_wireframe,
            depth_enabled,
            depth_disabled,
            blend_opaque,
            blend_alpha,
            sampler_linear_wrap,
            sampler_linear_clamp,
            sampler_point_clamp,
            dynamic_vb: None,
            dynamic_vb_size: 0,
            dynamic_ib: None,
            dynamic_ib_size: 0,
            input_layout_cache: HashMap::new(),
            texture_cache: HashMap::new(),
            state_manager,
            render_targets,
        };

        if !hwnd.0.is_null() && width > 0 && height > 0 {
            ctx.init_swap_chain(hwnd, width, height)?;
        }

        Ok(ctx)
    }

    /// Initializes swap chain and view targets for a given HWND.
    pub fn init_swap_chain(&mut self, hwnd: HWND, width: u32, height: u32) -> Result<()> {
        self.hwnd = hwnd;
        self.width = width.max(1);
        self.height = height.max(1);

        let dxgi_device: IDXGIDevice = self.device.cast()?;
        let adapter = unsafe { dxgi_device.GetAdapter()? };
        let factory: IDXGIFactory1 = unsafe { adapter.GetParent()? };

        let swap_desc = DXGI_SWAP_CHAIN_DESC {
            BufferDesc: DXGI_MODE_DESC {
                Width: self.width,
                Height: self.height,
                RefreshRate: DXGI_RATIONAL { Numerator: 60, Denominator: 1 },
                Format: DXGI_FORMAT_R8G8B8A8_UNORM,
                ..Default::default()
            },
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            BufferUsage: DXGI_USAGE_RENDER_TARGET_OUTPUT,
            BufferCount: 1,
            OutputWindow: hwnd,
            Windowed: true.into(),
            SwapEffect: DXGI_SWAP_EFFECT_DISCARD,
            Flags: 0,
        };

        let mut swap_chain_opt: Option<IDXGISwapChain> = None;
        let hr = unsafe { factory.CreateSwapChain(&self.device, &swap_desc, &mut swap_chain_opt) };
        if hr.is_err() {
            return Err(windows::core::Error::from(hr));
        }

        self.swap_chain = swap_chain_opt;

        self.recreate_views()?;
        Ok(())
    }

    /// Re-creates RTV and DSV after swap chain creation or resize.
    pub fn recreate_views(&mut self) -> Result<()> {
        self.render_target_view = None;
        self.depth_stencil_view = None;
        self.depth_stencil_texture = None;

        let swap_chain = match &self.swap_chain {
            Some(sc) => sc,
            None => return Ok(()),
        };

        // Create RTV from backbuffer
        let back_buffer: ID3D11Texture2D = unsafe { swap_chain.GetBuffer(0)? };
        let mut rtv = None;
        unsafe {
            self.device.CreateRenderTargetView(&back_buffer, None, Some(&mut rtv))?;
        }
        self.render_target_view = rtv;

        // Create Depth Stencil buffer & DSV
        let depth_desc = D3D11_TEXTURE2D_DESC {
            Width: self.width,
            Height: self.height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_D24_UNORM_S8_UINT,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: D3D11_BIND_DEPTH_STENCIL.0 as u32,
            ..Default::default()
        };

        let mut depth_tex = None;
        unsafe {
            self.device.CreateTexture2D(&depth_desc, None, Some(&mut depth_tex))?;
        }
        let depth_tex = depth_tex.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;

        let dsv_desc = D3D11_DEPTH_STENCIL_VIEW_DESC {
            Format: DXGI_FORMAT_D24_UNORM_S8_UINT,
            ViewDimension: D3D11_DSV_DIMENSION_TEXTURE2D,
            ..Default::default()
        };

        let mut dsv = None;
        unsafe {
            self.device.CreateDepthStencilView(&depth_tex, Some(&dsv_desc), Some(&mut dsv))?;
        }
        self.depth_stencil_texture = Some(depth_tex);
        self.depth_stencil_view = dsv;

        self.bind_default_targets();
        Ok(())
    }

    /// Binds default backbuffer and depth buffer to the output merger.
    pub fn bind_default_targets(&self) {
        if let Some(rtv) = &self.render_target_view {
            let rtv_slice = [Some(rtv.clone())];
            unsafe {
                self.context.OMSetRenderTargets(
                    Some(&rtv_slice),
                    self.depth_stencil_view.as_ref(),
                );
            }
        }
        self.set_viewport(self.width as f32, self.height as f32);
    }

    /// Sets viewport dimensions.
    pub fn set_viewport(&self, width: f32, height: f32) {
        let vp = [D3D11_VIEWPORT {
            TopLeftX: 0.0,
            TopLeftY: 0.0,
            Width: width,
            Height: height,
            MinDepth: 0.0,
            MaxDepth: 1.0,
        }];
        unsafe {
            self.context.RSSetViewports(Some(&vp));
        }
    }

    /// Clears active render target and depth stencil views.
    pub fn clear(&self, color: [f32; 4], depth: f32, stencil: u8) {
        if let Some(rtv) = &self.render_target_view {
            unsafe {
                self.context.ClearRenderTargetView(rtv, &color);
            }
        }
        if let Some(dsv) = &self.depth_stencil_view {
            unsafe {
                self.context.ClearDepthStencilView(
                    dsv,
                    (D3D11_CLEAR_DEPTH.0 | D3D11_CLEAR_STENCIL.0) as u32,
                    depth,
                    stencil,
                );
            }
        }
    }

    /// Presents swap chain to the window.
    pub fn present(&self, sync_interval: u32) -> Result<()> {
        if let Some(swap_chain) = &self.swap_chain {
            let hr = unsafe {
                swap_chain.Present(sync_interval, DXGI_PRESENT(0))
            };
            if hr.is_err() {
                return Err(windows::core::Error::from(hr));
            }
        }
        Ok(())
    }

    /// Resizes swap chain and views on window resize.
    pub fn resize(&mut self, width: u32, height: u32) -> Result<()> {
        if width == 0 || height == 0 || (width == self.width && height == self.height) {
            return Ok(());
        }
        self.width = width;
        self.height = height;

        self.render_target_view = None;
        self.depth_stencil_view = None;
        self.depth_stencil_texture = None;

        if let Some(swap_chain) = &self.swap_chain {
            unsafe {
                swap_chain.ResizeBuffers(
                    0,
                    width,
                    height,
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_SWAP_CHAIN_FLAG(0),
                )?;
            }
        }

        self.recreate_views()
    }

    // Helper: Buffer Creation
    pub fn create_buffer(&self, desc: &D3D11_BUFFER_DESC, initial_data: Option<&[u8]>) -> Result<ID3D11Buffer> {
        let subresource = initial_data.map(|data| D3D11_SUBRESOURCE_DATA {
            pSysMem: data.as_ptr() as *const c_void,
            SysMemPitch: 0,
            SysMemSlicePitch: 0,
        });

        let mut buffer = None;
        unsafe {
            self.device.CreateBuffer(desc, subresource.as_ref().map(|s| s as *const _), Some(&mut buffer))?;
        }
        buffer.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))
    }

    /// Creates a 16-byte aligned dynamic Constant Buffer.
    pub fn create_constant_buffer(&self, size_bytes: usize) -> Result<ID3D11Buffer> {
        let aligned_size = ((size_bytes + 15) / 16) * 16;
        let desc = D3D11_BUFFER_DESC {
            ByteWidth: aligned_size as u32,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_CONSTANT_BUFFER.0 as u32,
            CPUAccessFlags: 0x00010000, // D3D11_CPU_ACCESS_WRITE
            ..Default::default()
        };
        self.create_buffer(&desc, None)
    }

    /// Creates a Structured Buffer and optional SRV / UAV for Compute Shader operations.
    pub fn create_structured_buffer(
        &self,
        element_size: u32,
        element_count: u32,
        uav: bool,
    ) -> Result<(ID3D11Buffer, Option<ID3D11ShaderResourceView>, Option<ID3D11UnorderedAccessView>)> {
        let total_bytes = element_size * element_count;
        let mut bind_flags = D3D11_BIND_SHADER_RESOURCE.0 as u32;
        if uav {
            bind_flags |= D3D11_BIND_UNORDERED_ACCESS.0 as u32;
        }

        let desc = D3D11_BUFFER_DESC {
            ByteWidth: total_bytes,
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: bind_flags,
            MiscFlags: D3D11_RESOURCE_MISC_BUFFER_STRUCTURED.0 as u32,
            StructureByteStride: element_size,
            ..Default::default()
        };

        let buffer = self.create_buffer(&desc, None)?;

        let srv = unsafe {
            let srv_desc = windows::Win32::Graphics::Direct3D11::D3D11_SHADER_RESOURCE_VIEW_DESC {
                Format: DXGI_FORMAT_UNKNOWN,
                ViewDimension: windows::Win32::Graphics::Direct3D::D3D_SRV_DIMENSION_BUFFEREX,
                Anonymous: windows::Win32::Graphics::Direct3D11::D3D11_SHADER_RESOURCE_VIEW_DESC_0 {
                    BufferEx: D3D11_BUFFEREX_SRV {
                        FirstElement: 0,
                        NumElements: element_count,
                        Flags: 0,
                    },
                },
            };
            let mut s = None;
            self.device.CreateShaderResourceView(&buffer, Some(&srv_desc), Some(&mut s))?;
            s
        };

        let uav_view = if uav {
            unsafe {
                let uav_desc = windows::Win32::Graphics::Direct3D11::D3D11_UNORDERED_ACCESS_VIEW_DESC {
                    Format: DXGI_FORMAT_UNKNOWN,
                    ViewDimension: D3D11_UAV_DIMENSION_BUFFER,
                    Anonymous: windows::Win32::Graphics::Direct3D11::D3D11_UNORDERED_ACCESS_VIEW_DESC_0 {
                        Buffer: D3D11_BUFFER_UAV {
                            FirstElement: 0,
                            NumElements: element_count,
                            Flags: 0,
                        },
                    },
                };
                let mut u = None;
                self.device.CreateUnorderedAccessView(&buffer, Some(&uav_desc), Some(&mut u))?;
                u
            }
        } else {
            None
        };

        Ok((buffer, srv, uav_view))
    }

    /// Creates a 2D Texture and optional SRV / UAV (e.g. RWTexture2D).
    pub fn create_texture2d(
        &self,
        width: u32,
        height: u32,
        format: DXGI_FORMAT,
        uav: bool,
    ) -> Result<(ID3D11Texture2D, Option<ID3D11ShaderResourceView>, Option<ID3D11UnorderedAccessView>)> {
        let mut bind_flags = D3D11_BIND_SHADER_RESOURCE.0 as u32;
        if uav {
            bind_flags |= D3D11_BIND_UNORDERED_ACCESS.0 as u32;
        }

        let desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: format,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: bind_flags,
            ..Default::default()
        };

        let mut texture_opt = None;
        unsafe {
            self.device.CreateTexture2D(&desc, None, Some(&mut texture_opt))?;
        }
        let texture = texture_opt.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;

        let srv = unsafe {
            let mut s = None;
            self.device.CreateShaderResourceView(&texture, None, Some(&mut s))?;
            s
        };

        let uav_view = if uav {
            unsafe {
                let uav_desc = windows::Win32::Graphics::Direct3D11::D3D11_UNORDERED_ACCESS_VIEW_DESC {
                    Format: format,
                    ViewDimension: D3D11_UAV_DIMENSION_TEXTURE2D,
                    Anonymous: windows::Win32::Graphics::Direct3D11::D3D11_UNORDERED_ACCESS_VIEW_DESC_0 {
                        Texture2D: D3D11_TEX2D_UAV { MipSlice: 0 },
                    },
                };
                let mut u = None;
                self.device.CreateUnorderedAccessView(&texture, Some(&uav_desc), Some(&mut u))?;
                u
            }
        } else {
            None
        };

        Ok((texture, srv, uav_view))
    }

    // Pipeline State Helpers
    fn create_rasterizer_state(
        device: &ID3D11Device,
        cull_mode: windows::Win32::Graphics::Direct3D11::D3D11_CULL_MODE,
        fill_mode: windows::Win32::Graphics::Direct3D11::D3D11_FILL_MODE,
    ) -> Result<ID3D11RasterizerState> {
        let desc = D3D11_RASTERIZER_DESC {
            FillMode: fill_mode,
            CullMode: cull_mode,
            FrontCounterClockwise: false.into(),
            DepthClipEnable: true.into(),
            ..Default::default()
        };
        let mut state = None;
        unsafe { device.CreateRasterizerState(&desc, Some(&mut state))?; }
        state.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))
    }

    fn create_depth_stencil_state(device: &ID3D11Device, enable: bool, write: bool) -> Result<ID3D11DepthStencilState> {
        let desc = D3D11_DEPTH_STENCIL_DESC {
            DepthEnable: enable.into(),
            DepthWriteMask: if write { D3D11_DEPTH_WRITE_MASK_ALL } else { D3D11_DEPTH_WRITE_MASK_ZERO },
            DepthFunc: D3D11_COMPARISON_LESS_EQUAL,
            ..Default::default()
        };
        let mut state = None;
        unsafe { device.CreateDepthStencilState(&desc, Some(&mut state))?; }
        state.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))
    }

    fn create_blend_state(device: &ID3D11Device, enable_alpha: bool) -> Result<ID3D11BlendState> {
        let mut desc = D3D11_BLEND_DESC::default();
        let rt = &mut desc.RenderTarget[0];
        if enable_alpha {
            rt.BlendEnable = true.into();
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL.0 as u8;
        } else {
            rt.BlendEnable = false.into();
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL.0 as u8;
        }
        let mut state = None;
        unsafe { device.CreateBlendState(&desc, Some(&mut state))?; }
        state.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))
    }

    fn create_sampler_state(
        device: &ID3D11Device,
        filter: windows::Win32::Graphics::Direct3D11::D3D11_FILTER,
        address: windows::Win32::Graphics::Direct3D11::D3D11_TEXTURE_ADDRESS_MODE,
    ) -> Result<ID3D11SamplerState> {
        let desc = D3D11_SAMPLER_DESC {
            Filter: filter,
            AddressU: address,
            AddressV: address,
            AddressW: address,
            ComparisonFunc: D3D11_COMPARISON_ALWAYS,
            MaxLOD: f32::MAX,
            ..Default::default()
        };
        let mut state = None;
        unsafe { device.CreateSamplerState(&desc, Some(&mut state))?; }
        state.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))
    }

    /// Uploads raw vertex bytes to a dynamic DX11 vertex buffer.
    pub fn upload_dynamic_vertex_buffer(&mut self, data: &[u8]) -> Result<ID3D11Buffer> {
        if self.dynamic_vb.is_none() || self.dynamic_vb_size < data.len() {
            let new_size = data.len().next_power_of_two().max(65536);
            let desc = D3D11_BUFFER_DESC {
                ByteWidth: new_size as u32,
                Usage: D3D11_USAGE_DYNAMIC,
                BindFlags: D3D11_BIND_VERTEX_BUFFER.0 as u32,
                CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
                ..Default::default()
            };
            let buf = self.create_buffer(&desc, None)?;
            self.dynamic_vb = Some(buf);
            self.dynamic_vb_size = new_size;
        }

        let vb = self.dynamic_vb.as_ref().unwrap();
        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        unsafe {
            self.context.Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut mapped))?;
            std::ptr::copy_nonoverlapping(data.as_ptr(), mapped.pData as *mut u8, data.len());
            self.context.Unmap(vb, 0);
        }
        Ok(vb.clone())
    }

    /// Uploads raw index bytes to a dynamic DX11 index buffer.
    pub fn upload_dynamic_index_buffer(&mut self, data: &[u8]) -> Result<ID3D11Buffer> {
        if self.dynamic_ib.is_none() || self.dynamic_ib_size < data.len() {
            let new_size = data.len().next_power_of_two().max(32768);
            let desc = D3D11_BUFFER_DESC {
                ByteWidth: new_size as u32,
                Usage: D3D11_USAGE_DYNAMIC,
                BindFlags: D3D11_BIND_INDEX_BUFFER.0 as u32,
                CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
                ..Default::default()
            };
            let buf = self.create_buffer(&desc, None)?;
            self.dynamic_ib = Some(buf);
            self.dynamic_ib_size = new_size;
        }

        let ib = self.dynamic_ib.as_ref().unwrap();
        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        unsafe {
            self.context.Map(ib, 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut mapped))?;
            std::ptr::copy_nonoverlapping(data.as_ptr(), mapped.pData as *mut u8, data.len());
            self.context.Unmap(ib, 0);
        }
        Ok(ib.clone())
    }

    /// Obtains or creates an ID3D11InputLayout matching the Vertex Shader and MMD vertex stride.
    pub fn get_or_create_input_layout(&mut self, vs_bytecode: &[u8], stride: u32) -> Result<ID3D11InputLayout> {
        let key = (vs_bytecode.as_ptr() as usize, stride);
        if let Some(il) = self.input_layout_cache.get(&key) {
            return Ok(il.clone());
        }

        let descs = if stride >= 52 {
            // Full PMX 2.0 SDEF / BDEF4 layout
            vec![
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("POSITION"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 0,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("BLENDWEIGHT"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32A32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 12,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("BLENDINDICES"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R16G16B16A16_UINT,
                    InputSlot: 0,
                    AlignedByteOffset: 28,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("NORMAL"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 36,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("TEXCOORD"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 48,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
            ]
        } else if stride >= 38 {
            // Skinned PMX layout (Pos, Weight, Indices, Normal, UV)
            vec![
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("POSITION"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 0,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("BLENDWEIGHT"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 12,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("BLENDINDICES"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R16G16_UINT,
                    InputSlot: 0,
                    AlignedByteOffset: 16,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("NORMAL"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 20,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("TEXCOORD"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 32,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
            ]
        } else if stride >= 32 {
            // Standard static layout: Position(12), Normal(12), TexCoord(8) = 32 bytes
            vec![
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("POSITION"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 0,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("NORMAL"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 12,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("TEXCOORD"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 24,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
            ]
        } else {
            // Minimal 2D screen quad: Position(12), TexCoord(8) = 20 bytes
            vec![
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("POSITION"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32B32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 0,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
                D3D11_INPUT_ELEMENT_DESC {
                    SemanticName: windows::core::s!("TEXCOORD"),
                    SemanticIndex: 0,
                    Format: DXGI_FORMAT_R32G32_FLOAT,
                    InputSlot: 0,
                    AlignedByteOffset: 12,
                    InputSlotClass: D3D11_INPUT_PER_VERTEX_DATA,
                    InstanceDataStepRate: 0,
                },
            ]
        };

        let mut layout_opt: Option<ID3D11InputLayout> = None;
        unsafe {
            self.device.CreateInputLayout(&descs, vs_bytecode, Some(&mut layout_opt))?;
        }
        let layout = layout_opt.ok_or_else(|| windows::core::Error::from(windows::core::HRESULT(-1)))?;
        self.input_layout_cache.insert(key, layout.clone());
        Ok(layout)
    }

    /// Converts an incoming Direct3D 9 texture pointer to an ID3D11ShaderResourceView.
    pub fn upload_d3d9_texture_to_srv(&mut self, tex9_ptr: *mut c_void) -> Result<ID3D11ShaderResourceView> {
        let key = tex9_ptr as usize;
        if let Some(srv) = self.texture_cache.get(&key) {
            return Ok(srv.clone());
        }

        let tex9: windows::Win32::Graphics::Direct3D9::IDirect3DTexture9 = unsafe {
            std::mem::transmute(tex9_ptr)
        };
        let mut desc = windows::Win32::Graphics::Direct3D9::D3DSURFACE_DESC::default();
        let mut locked_rect = windows::Win32::Graphics::Direct3D9::D3DLOCKED_RECT::default();

        unsafe {
            tex9.GetLevelDesc(0, &mut desc)?;
            tex9.LockRect(0, &mut locked_rect, null_mut(), 0)?;
        }

        let width = desc.Width.max(1);
        let height = desc.Height.max(1);
        let pitch = locked_rect.Pitch as usize;

        let subresource = D3D11_SUBRESOURCE_DATA {
            pSysMem: locked_rect.pBits,
            SysMemPitch: pitch as u32,
            SysMemSlicePitch: (pitch * height as usize) as u32,
        };

        let tex_desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_B8G8R8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: D3D11_BIND_SHADER_RESOURCE.0 as u32,
            ..Default::default()
        };

        let mut d3d11_tex = None;
        let mut srv_opt = None;
        unsafe {
            self.device.CreateTexture2D(&tex_desc, Some(&subresource), Some(&mut d3d11_tex))?;
            let _ = tex9.UnlockRect(0);
            std::mem::forget(tex9); // Do not release MMD's texture!

            if let Some(ref t) = d3d11_tex {
                self.device.CreateShaderResourceView(t, None, Some(&mut srv_opt))?;
            }
        }

        let srv = srv_opt.ok_or_else(|| windows::core::Error::from(windows::core::HRESULT(-1)))?;
        self.texture_cache.insert(key, srv.clone());
        Ok(srv)
    }
}
