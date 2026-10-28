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
    ID3D11RasterizerState, ID3D11RenderTargetView, ID3D11SamplerState, ID3D11VertexShader,
    ID3D11PixelShader,
    ID3D11ShaderResourceView, ID3D11Texture2D, ID3D11UnorderedAccessView,
    D3D11_BIND_CONSTANT_BUFFER, D3D11_BIND_DEPTH_STENCIL, D3D11_BIND_INDEX_BUFFER,
    D3D11_BIND_RENDER_TARGET, D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_UNORDERED_ACCESS,
    D3D11_BIND_VERTEX_BUFFER, D3D11_BLEND_DESC, D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BUFFER_DESC,
    D3D11_BUFFER_UAV, D3D11_BUFFEREX_SRV, D3D11_CLEAR_DEPTH, D3D11_CLEAR_STENCIL,
    D3D11_COLOR_WRITE_ENABLE_ALL, D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_LESS_EQUAL,
    D3D11_CPU_ACCESS_WRITE, D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_CREATE_DEVICE_FLAG,
    D3D11_CULL_BACK, D3D11_CULL_NONE, D3D11_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC,
    D3D11_CPU_ACCESS_READ,
    D3D11_DEPTH_WRITE_MASK_ALL, D3D11_DEPTH_WRITE_MASK_ZERO, D3D11_DSV_DIMENSION_TEXTURE2D,
    D3D11_FILL_SOLID, D3D11_FILL_WIREFRAME, D3D11_FILTER_MIN_MAG_MIP_LINEAR,
    D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_INPUT_ELEMENT_DESC, D3D11_INPUT_PER_VERTEX_DATA,
    D3D11_MAP_READ, D3D11_MAP_WRITE_DISCARD, D3D11_MAPPED_SUBRESOURCE, D3D11_RASTERIZER_DESC,
    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
    D3D11_SAMPLER_DESC, D3D11_SDK_VERSION, D3D11_SUBRESOURCE_DATA, D3D11_TEXTURE2D_DESC,
    D3D11_TEX2D_UAV, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP,
    D3D11_UAV_DIMENSION_BUFFER, D3D11_UAV_DIMENSION_TEXTURE2D, D3D11_USAGE_DEFAULT,
    D3D11_USAGE_DYNAMIC, D3D11_USAGE_STAGING, D3D11_VIEWPORT,
};
use windows::Win32::Graphics::Dxgi::Common::{
    DXGI_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM,
    DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16B16A16_UINT,
    DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_ALPHA_MODE_IGNORE, DXGI_FORMAT_UNKNOWN,
    DXGI_MODE_SCALING_UNSPECIFIED, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
    DXGI_RATIONAL, DXGI_SAMPLE_DESC,
};
use windows::Win32::Graphics::Dxgi::{
    IDXGIDevice, IDXGIFactory2, IDXGISwapChain,
    DXGI_PRESENT, DXGI_SWAP_CHAIN_FLAG,
    DXGI_SCALING_STRETCH, DXGI_SWAP_CHAIN_DESC1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC,
    DXGI_SWAP_EFFECT_SEQUENTIAL, DXGI_USAGE_RENDER_TARGET_OUTPUT,
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

    /// Offscreen composite chain used to paint DX11 content into the native
    /// D3D9 backbuffer: the model pass target (transparent background), the
    /// composite canvas and a staging texture for CPU readback.
    pub model_tex: Option<ID3D11Texture2D>,
    pub model_rtv: Option<ID3D11RenderTargetView>,
    pub model_srv: Option<ID3D11ShaderResourceView>,
    pub model_depth: Option<ID3D11DepthStencilView>,
    pub shared_target_tex: Option<ID3D11Texture2D>,
    pub composite_tex: Option<ID3D11Texture2D>,
    pub composite_rtv: Option<ID3D11RenderTargetView>,
    pub composite_staging: Option<ID3D11Texture2D>,
    pub blit_vs: Option<ID3D11VertexShader>,
    pub blit_ps: Option<ID3D11PixelShader>,
    pub composite_sampler: Option<ID3D11SamplerState>,
    pub composite_alpha_blend: Option<ID3D11BlendState>,
    pub composite_size: (u32, u32),
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
            model_tex: None,
            model_rtv: None,
            model_srv: None,
            model_depth: None,
            shared_target_tex: None,
            composite_tex: None,
            composite_rtv: None,
            composite_staging: None,
            blit_vs: None,
            blit_ps: None,
            composite_sampler: None,
            composite_alpha_blend: None,
            composite_size: (0, 0),
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
        let factory: IDXGIFactory2 = unsafe { adapter.GetParent()? };

        let swap_desc = DXGI_SWAP_CHAIN_DESC1 {
            Width: self.width,
            Height: self.height,
            Format: DXGI_FORMAT_B8G8R8A8_UNORM,
            Stereo: false.into(),
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            BufferUsage: DXGI_USAGE_RENDER_TARGET_OUTPUT,
            BufferCount: 2,
            Scaling: DXGI_SCALING_STRETCH,
            // BitBlt exchange model: required for the layered-window color key
            // (FLIP_* swap effects bypass layered composition entirely).
            SwapEffect: DXGI_SWAP_EFFECT_SEQUENTIAL,
            AlphaMode: DXGI_ALPHA_MODE_IGNORE,
            Flags: 0,
        };
        let fullscreen_desc = DXGI_SWAP_CHAIN_FULLSCREEN_DESC {
            RefreshRate: DXGI_RATIONAL { Numerator: 0, Denominator: 0 },
            ScanlineOrdering: DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
            Scaling: DXGI_MODE_SCALING_UNSPECIFIED,
            Windowed: true.into(),
        };

        let swap_chain = unsafe {
            factory.CreateSwapChainForHwnd(
                &self.device,
                hwnd,
                &swap_desc,
                Some(&fullscreen_desc),
                None,
            )?
        };
        self.swap_chain = Some(swap_chain.cast()?);

        self.recreate_views()?;
        Ok(())
    }

    /// Re-creates RTV and DSV after swap chain creation or resize.
    pub fn recreate_views(&mut self) -> Result<()> {
        self.render_target_view = None;
        self.depth_stencil_view = None;
        self.depth_stencil_texture = None;
        unsafe { self.context.OMSetRenderTargets(None, None); }

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

    /// Recreates the offscreen composite chain sized to the MMD backbuffer:
    /// a model pass target with transparent background, a composite canvas
    /// and a CPU-readable staging texture. Binds the model target for draw
    /// hooks, replacing the swapchain backbuffer as the draw destination.
    pub fn recreate_composite(&mut self, width: u32, height: u32) -> Result<()> {
        if width == 0 || height == 0 {
            return Ok(());
        }
        if self.composite_size == (width, height) && self.composite_rtv.is_some() {
            return Ok(());
        }

        self.composite_size = (width, height);
        self.width = width;
        self.height = height;
        self.render_target_view = None;
        self.depth_stencil_view = None;
        unsafe { self.context.OMSetRenderTargets(None, None); }

        // Blit shader pair (big-triangle, SV_VertexID generated).
        if self.blit_vs.is_none() {
            let hlsl = "
struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VOut BlitVS(uint id : SV_VertexID) {
    VOut o;
    float x = (id == 1) ? 3.0f : -1.0f;
    float y = (id == 2) ? 3.0f : -1.0f;
    o.pos = float4(x, y, 0.5f, 1.0f);
    o.uv = float2((x + 1.0f) * 0.5f, 1.0f - (y + 1.0f) * 0.5f);
    return o;
}
Texture2D SrcTexture : register(t0);
SamplerState SrcSampler : register(s0);
float4 BlitPS(VOut input) : SV_Target {
    return SrcTexture.Sample(SrcSampler, input.uv);
}
";
            let vs_bytecode = crate::shader::compile_hlsl(
                hlsl, "BlitVS", self.vs_profile(), None, &[])
                .map_err(|e| {
                    crate::log_msg(&format!("Blit VS compile failed: {e}"));
                    windows::core::Error::from(HRESULT(-1))
                })?;
            let ps_bytecode = crate::shader::compile_hlsl(
                hlsl, "BlitPS", self.ps_profile(), None, &[])
                .map_err(|e| {
                    crate::log_msg(&format!("Blit PS compile failed: {e}"));
                    windows::core::Error::from(HRESULT(-1))
                })?;
            let mut vs: Option<ID3D11VertexShader> = None;
            let mut ps: Option<ID3D11PixelShader> = None;
            unsafe {
                self.device.CreateVertexShader(&vs_bytecode, None, Some(&mut vs))?;
                self.device.CreatePixelShader(&ps_bytecode, None, Some(&mut ps))?;
            }
            self.blit_vs = vs;
            self.blit_ps = ps;
        }

        if self.composite_sampler.is_none() {
            static SAMPLER: std::sync::OnceLock<()> = std::sync::OnceLock::new();
            let _ = SAMPLER.get_or_init(|| ());
        }
        // Model pass target: RGBA with transparent black background.
        let target_desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_B8G8R8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: (D3D11_BIND_RENDER_TARGET.0 | D3D11_BIND_SHADER_RESOURCE.0) as u32,
            ..Default::default()
        };
        let mut model_tex = None;
        unsafe {
            self.device.CreateTexture2D(&target_desc, None, Some(&mut model_tex))?;
        }
        let model_tex = model_tex.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let mut model_rtv = None;
        let mut model_srv = None;
        unsafe {
            self.device.CreateRenderTargetView(&model_tex, None, Some(&mut model_rtv))?;
            self.device.CreateShaderResourceView(&model_tex, None, Some(&mut model_srv))?;
        }

        // Depth buffer for the model pass target.
        let depth_desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
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
        let mut model_depth = None;
        unsafe {
            self.device.CreateDepthStencilView(&depth_tex, Some(&dsv_desc), Some(&mut model_depth))?;
        }

        // Composite canvas + CPU staging for the final readback.
        let mut composite_tex = None;
        unsafe {
            self.device.CreateTexture2D(&target_desc, None, Some(&mut composite_tex))?;
        }
        let composite_tex = composite_tex.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let mut composite_rtv = None;
        unsafe {
            self.device.CreateRenderTargetView(&composite_tex, None, Some(&mut composite_rtv))?;
        }
        let staging_desc = D3D11_TEXTURE2D_DESC {
            Usage: D3D11_USAGE_STAGING,
            BindFlags: 0,
            CPUAccessFlags: D3D11_CPU_ACCESS_READ.0 as u32,
            ..target_desc
        };
        let mut composite_staging = None;
        unsafe {
            self.device.CreateTexture2D(&staging_desc, None, Some(&mut composite_staging))?;
        }
        let composite_staging = composite_staging.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;

        self.model_tex = Some(model_tex);
        self.model_rtv = model_rtv;
        self.model_srv = model_srv;
        self.model_depth = model_depth;
        self.composite_tex = Some(composite_tex);
        self.composite_rtv = composite_rtv;
        self.composite_staging = Some(composite_staging);

        // Draw hooks render into the model target from now on.
        self.render_target_view = self.model_rtv.clone();
        self.depth_stencil_view = self.model_depth.clone();
        self.bind_default_targets();
        crate::log_msg(&format!("Composite targets recreated: {}x{}", width, height));
        Ok(())
    }

    /// Draws the composite canvas: the captured D3D9 scene as background and
    /// the DX11-effect model layer with straight alpha blending.
    pub fn render_composite(&mut self, scene_srv: Option<&ID3D11ShaderResourceView>) -> Result<()> {
        let model_srv = match self.model_srv.clone() {
            Some(srv) => srv,
            None => return Ok(()),
        };
        let composite_rtv = self.composite_rtv.clone().ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let blit_vs = self.blit_vs.clone().ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let blit_ps = self.blit_ps.clone().ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let sampler = match self.state_manager.get_sampler(crate::states::SamplerPreset::LinearClamp).cloned() {
            Some(s) => s,
            None => return Ok(()),
        };
        let alpha_blend = match self.state_manager.get_blend(crate::states::BlendPreset::AlphaBlend).cloned() {
            Some(b) => b,
            None => return Ok(()),
        };
        let opaque_blend = self.state_manager.get_blend(crate::states::BlendPreset::Opaque).cloned();
        let depth_disabled_ref = &self.depth_disabled;

        unsafe {
            let rtv_slice = [Some(composite_rtv)];
            self.context.OMSetRenderTargets(Some(&rtv_slice), None);
            match &opaque_blend {
                Some(b) => self.context.OMSetBlendState(Some(b), Some(&[0.0; 4]), 0xFFFFFFFF),
                None => self.context.OMSetBlendState(None, Some(&[0.0; 4]), 0xFFFFFFFF),
            }
            self.context.OMSetDepthStencilState(depth_disabled_ref, 0xFFFFFFFF);
            self.set_viewport(self.width as f32, self.height as f32);

            self.context.VSSetShader(Some(&blit_vs), None);
            self.context.PSSetShader(Some(&blit_ps), None);
            let sampler_slice = [Some(sampler)];
            self.context.PSSetSamplers(0, Some(&sampler_slice));
            self.context.IASetInputLayout(None);
            self.context.IASetPrimitiveTopology(
                windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            );

            // Background pass: MMD's own scene.
            if let Some(scene) = scene_srv {
                let scene_slice = [Some(scene.clone()), None, None, None, None, None, None, None];
                self.context.PSSetShaderResources(0, Some(&scene_slice));
                self.context.Draw(3, 0);
            }

            // Model pass: DX11-effect layer over the scene.
            self.context.OMSetBlendState(Some(&alpha_blend), Some(&[0.0; 4]), 0xFFFFFFFF);
            let model_slice = [Some(model_srv), None, None, None, None, None, None, None];
            self.context.PSSetShaderResources(0, Some(&model_slice));
            self.context.Draw(3, 0);

            // Detach SRVs (the textures are render targets of the next pass).
            let none_slice: [Option<ID3D11ShaderResourceView>; 8] = [None, None, None, None, None, None, None, None];
            self.context.PSSetShaderResources(0, Some(&none_slice));
            self.context.PSSetShader(None, None);
            self.context.VSSetShader(None, None);
        }
        Ok(())
    }

    /// Copies the model layer into the staging texture and returns its
    /// contents as tightly packed BGRA rows.
    pub fn readback_model_layer(&mut self) -> Result<Vec<u8>> {
        let (model_tex, staging) = match (&self.model_tex, &self.composite_staging) {
            (Some(t), Some(s)) => (t.clone(), s.clone()),
            _ => return Err(windows::core::Error::from(HRESULT(-1))),
        };
        unsafe {
            self.context.CopyResource(&staging, &model_tex);
            let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
            self.context.Map(
                &staging,
                0,
                D3D11_MAP_READ,
                0,
                Some(&mut mapped),
            )?;
            let width = self.composite_size.0 as usize;
            let height = self.composite_size.1 as usize;
            let mut out = Vec::with_capacity(width * height * 4);
            let row_len = (mapped.RowPitch as usize) * height;
            let row = std::slice::from_raw_parts(mapped.pData as *const u8, row_len);
            for y in 0..height {
                let start = y * mapped.RowPitch as usize;
                out.extend_from_slice(&row[start..start + width * 4]);
            }
            self.context.Unmap(&staging, 0);
            Ok(out)
        }
    }

    /// Opens an external D3D9 / D3D9Ex shared texture handle for zero-copy GPU composite.
    pub fn open_shared_d3d9_resource(&mut self, handle: windows::Win32::Foundation::HANDLE) -> Result<()> {
        let mut res: Option<windows::Win32::Graphics::Direct3D11::ID3D11Resource> = None;
        unsafe {
            self.device.OpenSharedResource(handle, &mut res)?;
        }
        let res = res.ok_or_else(|| windows::core::Error::from(HRESULT(-1)))?;
        let tex: ID3D11Texture2D = windows::core::Interface::cast(&res)?;
        self.shared_target_tex = Some(tex);
        crate::log_msg(&format!(
            "[d3d11] Successfully opened DXGI shared D3D9 texture: handle={:?}",
            handle.0
        ));
        Ok(())
    }

    /// Resizes swap chain and views on window resize.
    pub fn resize(&mut self, width: u32, height: u32) -> Result<()> {
        if width == 0 || height == 0 || (width == self.width && height == self.height) {
            return Ok(());
        }
        self.width = width;
        self.height = height;

        unsafe { self.context.OMSetRenderTargets(None, None); }
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
        self.upload_d3d9_texture_to_srv_ex(tex9_ptr, false)
    }

    /// Same as `upload_d3d9_texture_to_srv` with an optional cache bypass for
    /// textures whose content changes every frame (frame captures).
    pub fn upload_d3d9_texture_to_srv_ex(&mut self, tex9_ptr: *mut c_void, force: bool) -> Result<ID3D11ShaderResourceView> {
        let key = tex9_ptr as usize;
        static UPLOAD_LOGS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        static SCENE_LOGS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let log_no = UPLOAD_LOGS.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
        let scene_log_no = SCENE_LOGS.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
        let verbose = log_no <= 6 || (force && scene_log_no <= 10);
        let stage = |s: &str, extra: String| {
            if verbose {
                crate::log_msg(&format!("d3d9_to_srv[{log_no}#{scene_log_no}] {s}: {extra}"));
            }
        };
        stage("enter", format!("ptr={:p} force={force}", tex9_ptr));
        if !force {
            if let Some(srv) = self.texture_cache.get(&key) {
                stage("cache_hit", String::new());
                return Ok(srv.clone());
            }
        }

        let tex9 = unsafe {
            std::mem::ManuallyDrop::new(
                windows::Win32::Graphics::Direct3D9::IDirect3DTexture9::from_raw(tex9_ptr)
            )
        };
        let mut desc = windows::Win32::Graphics::Direct3D9::D3DSURFACE_DESC::default();
        let mut locked_rect = windows::Win32::Graphics::Direct3D9::D3DLOCKED_RECT::default();

        unsafe {
            tex9.GetLevelDesc(0, &mut desc)?;
            stage("getdesc", format!("{:?} {}x{}", desc.Format, desc.Width, desc.Height));
            tex9.LockRect(
                0,
                &mut locked_rect,
                null_mut(),
                windows::Win32::Graphics::Direct3D9::D3DLOCK_READONLY as u32,
            )?;
            stage("lock", format!("pitch={}", locked_rect.Pitch));

            if locked_rect.pBits.is_null() {
                let _ = tex9.UnlockRect(0);
                return Err(windows::core::Error::from(windows::Win32::Foundation::E_POINTER));
            }

            let width = desc.Width.max(1);
            let height = desc.Height.max(1);
            let pitch = locked_rect.Pitch as usize;

            let fmt_val = desc.Format.0 as u32;
            let (dxgi_fmt, row_bytes, num_rows) = match fmt_val {
                // D3DFMT_A8R8G8B8 (21) | D3DFMT_X8R8G8B8 (22)
                21 | 22 => (
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    width as usize * 4,
                    height as usize,
                ),
                // D3DFMT_A8 (28)
                28 => (
                    DXGI_FORMAT_R8_UNORM,
                    width as usize,
                    height as usize,
                ),
                // D3DFMT_DXT1 (0x31545844)
                0x31545844 => {
                    let block_w = (width as usize + 3) / 4;
                    let block_h = (height as usize + 3) / 4;
                    (DXGI_FORMAT_BC1_UNORM, block_w * 8, block_h)
                }
                // D3DFMT_DXT2 / DXT3 (0x32545844, 0x33545844)
                0x32545844 | 0x33545844 => {
                    let block_w = (width as usize + 3) / 4;
                    let block_h = (height as usize + 3) / 4;
                    (DXGI_FORMAT_BC2_UNORM, block_w * 16, block_h)
                }
                // D3DFMT_DXT4 / DXT5 (0x34545844, 0x35545844)
                0x34545844 | 0x35545844 => {
                    let block_w = (width as usize + 3) / 4;
                    let block_h = (height as usize + 3) / 4;
                    (DXGI_FORMAT_BC3_UNORM, block_w * 16, block_h)
                }
                // D3DFMT_A8B8G8R8 (32)
                32 => (
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    width as usize * 4,
                    height as usize,
                ),
                // Fallback: 32-bit BGRA
                _ => (
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    width as usize * 4,
                    height as usize,
                ),
            };

            let copy_row_bytes = row_bytes.min(pitch);
            let mut cpu: Vec<u8> = vec![0u8; row_bytes * num_rows];
            for y in 0..num_rows {
                let src = std::slice::from_raw_parts(
                    (locked_rect.pBits as *const u8).add(y * pitch),
                    copy_row_bytes,
                );
                cpu[y * row_bytes..y * row_bytes + copy_row_bytes].copy_from_slice(src);
            }
            let _ = tex9.UnlockRect(0);
            stage("unlock_copied", format!("{} bytes", cpu.len()));

            let tex_desc = D3D11_TEXTURE2D_DESC {
                Width: width,
                Height: height,
                MipLevels: 1,
                ArraySize: 1,
                Format: dxgi_fmt,
                SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
                Usage: D3D11_USAGE_DEFAULT,
                BindFlags: D3D11_BIND_SHADER_RESOURCE.0 as u32,
                ..Default::default()
            };
            let subresource = D3D11_SUBRESOURCE_DATA {
                pSysMem: cpu.as_ptr() as *const c_void,
                SysMemPitch: row_bytes as u32,
                SysMemSlicePitch: (row_bytes * num_rows) as u32,
            };
            let mut d3d11_tex = None;
            let mut srv_opt = None;
            self.device.CreateTexture2D(&tex_desc, Some(&subresource), Some(&mut d3d11_tex))?;
            stage("d3d11_texture", String::new());
            if let Some(ref t) = d3d11_tex {
                self.device.CreateShaderResourceView(t, None, Some(&mut srv_opt))?;
            }
            stage("srv", String::new());
            let srv = srv_opt.ok_or_else(|| windows::core::Error::from(windows::core::HRESULT(-1)))?;
            self.texture_cache.insert(key, srv.clone());
            Ok(srv)
        }
    }
}
