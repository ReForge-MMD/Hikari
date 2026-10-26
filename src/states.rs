//! Direct3D 11 Pipeline State Object Engine for Hikari.
//!
//! Provides creation, caching, and binding of immutable DirectX 11 pipeline state objects:
//! - ID3D11BlendState
//! - ID3D11DepthStencilState
//! - ID3D11RasterizerState
//! - ID3D11SamplerState

use std::collections::HashMap;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D11::*;

/// High-level Blend state configuration.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum BlendPreset {
    Opaque,
    AlphaBlend,
    Additive,
    Subtractive,
    Multiply,
}

/// High-level Depth-Stencil state configuration.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum DepthPreset {
    ReadWriteLessEqual,
    ReadOnlyLessEqual,
    Disabled,
    ShadowComparison,
}

/// High-level Rasterizer state configuration.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum RasterizerPreset {
    CullNoneSolid,
    CullBackSolid,
    CullFrontSolid,
    Wireframe,
}

/// High-level Sampler state configuration.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum SamplerPreset {
    LinearWrap,
    LinearClamp,
    PointClamp,
    PointWrap,
    Anisotropic16Wrap,
    ComparisonLinearClamp,
}

/// Manages and caches Direct3D 11 pipeline state objects.
pub struct StateManager {
    blend_cache: HashMap<BlendPreset, ID3D11BlendState>,
    depth_cache: HashMap<DepthPreset, ID3D11DepthStencilState>,
    raster_cache: HashMap<RasterizerPreset, ID3D11RasterizerState>,
    sampler_cache: HashMap<SamplerPreset, ID3D11SamplerState>,
}

impl StateManager {
    /// Creates a new StateManager and pre-populates standard state presets.
    pub fn new(device: &ID3D11Device) -> Result<Self> {
        let mut mgr = Self {
            blend_cache: HashMap::new(),
            depth_cache: HashMap::new(),
            raster_cache: HashMap::new(),
            sampler_cache: HashMap::new(),
        };

        // Pre-create blend states
        mgr.blend_cache.insert(BlendPreset::Opaque, Self::create_blend_state(device, BlendPreset::Opaque)?);
        mgr.blend_cache.insert(BlendPreset::AlphaBlend, Self::create_blend_state(device, BlendPreset::AlphaBlend)?);
        mgr.blend_cache.insert(BlendPreset::Additive, Self::create_blend_state(device, BlendPreset::Additive)?);
        mgr.blend_cache.insert(BlendPreset::Subtractive, Self::create_blend_state(device, BlendPreset::Subtractive)?);
        mgr.blend_cache.insert(BlendPreset::Multiply, Self::create_blend_state(device, BlendPreset::Multiply)?);

        // Pre-create depth-stencil states
        mgr.depth_cache.insert(DepthPreset::ReadWriteLessEqual, Self::create_depth_state(device, DepthPreset::ReadWriteLessEqual)?);
        mgr.depth_cache.insert(DepthPreset::ReadOnlyLessEqual, Self::create_depth_state(device, DepthPreset::ReadOnlyLessEqual)?);
        mgr.depth_cache.insert(DepthPreset::Disabled, Self::create_depth_state(device, DepthPreset::Disabled)?);
        mgr.depth_cache.insert(DepthPreset::ShadowComparison, Self::create_depth_state(device, DepthPreset::ShadowComparison)?);

        // Pre-create rasterizer states
        mgr.raster_cache.insert(RasterizerPreset::CullNoneSolid, Self::create_raster_state(device, RasterizerPreset::CullNoneSolid)?);
        mgr.raster_cache.insert(RasterizerPreset::CullBackSolid, Self::create_raster_state(device, RasterizerPreset::CullBackSolid)?);
        mgr.raster_cache.insert(RasterizerPreset::CullFrontSolid, Self::create_raster_state(device, RasterizerPreset::CullFrontSolid)?);
        mgr.raster_cache.insert(RasterizerPreset::Wireframe, Self::create_raster_state(device, RasterizerPreset::Wireframe)?);

        // Pre-create sampler states
        mgr.sampler_cache.insert(SamplerPreset::LinearWrap, Self::create_sampler_state(device, SamplerPreset::LinearWrap)?);
        mgr.sampler_cache.insert(SamplerPreset::LinearClamp, Self::create_sampler_state(device, SamplerPreset::LinearClamp)?);
        mgr.sampler_cache.insert(SamplerPreset::PointClamp, Self::create_sampler_state(device, SamplerPreset::PointClamp)?);
        mgr.sampler_cache.insert(SamplerPreset::PointWrap, Self::create_sampler_state(device, SamplerPreset::PointWrap)?);
        mgr.sampler_cache.insert(SamplerPreset::Anisotropic16Wrap, Self::create_sampler_state(device, SamplerPreset::Anisotropic16Wrap)?);
        mgr.sampler_cache.insert(SamplerPreset::ComparisonLinearClamp, Self::create_sampler_state(device, SamplerPreset::ComparisonLinearClamp)?);

        Ok(mgr)
    }

    pub fn get_blend(&self, preset: BlendPreset) -> Option<&ID3D11BlendState> {
        self.blend_cache.get(&preset)
    }

    pub fn get_depth(&self, preset: DepthPreset) -> Option<&ID3D11DepthStencilState> {
        self.depth_cache.get(&preset)
    }

    pub fn get_rasterizer(&self, preset: RasterizerPreset) -> Option<&ID3D11RasterizerState> {
        self.raster_cache.get(&preset)
    }

    pub fn get_sampler(&self, preset: SamplerPreset) -> Option<&ID3D11SamplerState> {
        self.sampler_cache.get(&preset)
    }

    // Direct3D 11 state constructors
    fn create_blend_state(device: &ID3D11Device, preset: BlendPreset) -> Result<ID3D11BlendState> {
        let mut desc: D3D11_BLEND_DESC = unsafe { std::mem::zeroed() };
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL.0 as u8;

        match preset {
            BlendPreset::Opaque => {
                desc.RenderTarget[0].BlendEnable = false.into();
            }
            BlendPreset::AlphaBlend => {
                desc.RenderTarget[0].BlendEnable = true.into();
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            }
            BlendPreset::Additive => {
                desc.RenderTarget[0].BlendEnable = true.into();
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            }
            BlendPreset::Subtractive => {
                desc.RenderTarget[0].BlendEnable = true.into();
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            }
            BlendPreset::Multiply => {
                desc.RenderTarget[0].BlendEnable = true.into();
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            }
        }

        let mut state = None;
        unsafe { device.CreateBlendState(&desc, Some(&mut state)) }?;
        Ok(state.unwrap())
    }

    fn create_depth_state(device: &ID3D11Device, preset: DepthPreset) -> Result<ID3D11DepthStencilState> {
        let mut desc: D3D11_DEPTH_STENCIL_DESC = unsafe { std::mem::zeroed() };

        match preset {
            DepthPreset::ReadWriteLessEqual => {
                desc.DepthEnable = true.into();
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
                desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            }
            DepthPreset::ReadOnlyLessEqual => {
                desc.DepthEnable = true.into();
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            }
            DepthPreset::Disabled => {
                desc.DepthEnable = false.into();
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            }
            DepthPreset::ShadowComparison => {
                desc.DepthEnable = true.into();
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
                desc.DepthFunc = D3D11_COMPARISON_LESS;
            }
        }

        let mut state = None;
        unsafe { device.CreateDepthStencilState(&desc, Some(&mut state)) }?;
        Ok(state.unwrap())
    }

    fn create_raster_state(device: &ID3D11Device, preset: RasterizerPreset) -> Result<ID3D11RasterizerState> {
        let mut desc: D3D11_RASTERIZER_DESC = unsafe { std::mem::zeroed() };
        desc.DepthClipEnable = true.into();

        match preset {
            RasterizerPreset::CullNoneSolid => {
                desc.FillMode = D3D11_FILL_SOLID;
                desc.CullMode = D3D11_CULL_NONE;
            }
            RasterizerPreset::CullBackSolid => {
                desc.FillMode = D3D11_FILL_SOLID;
                desc.CullMode = D3D11_CULL_BACK;
            }
            RasterizerPreset::CullFrontSolid => {
                desc.FillMode = D3D11_FILL_SOLID;
                desc.CullMode = D3D11_CULL_FRONT;
            }
            RasterizerPreset::Wireframe => {
                desc.FillMode = D3D11_FILL_WIREFRAME;
                desc.CullMode = D3D11_CULL_NONE;
            }
        }

        let mut state = None;
        unsafe { device.CreateRasterizerState(&desc, Some(&mut state)) }?;
        Ok(state.unwrap())
    }

    fn create_sampler_state(device: &ID3D11Device, preset: SamplerPreset) -> Result<ID3D11SamplerState> {
        let mut desc: D3D11_SAMPLER_DESC = unsafe { std::mem::zeroed() };
        desc.MaxLOD = f32::MAX;

        match preset {
            SamplerPreset::LinearWrap => {
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            }
            SamplerPreset::LinearClamp => {
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            }
            SamplerPreset::PointClamp => {
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            }
            SamplerPreset::PointWrap => {
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            }
            SamplerPreset::Anisotropic16Wrap => {
                desc.Filter = D3D11_FILTER_ANISOTROPIC;
                desc.MaxAnisotropy = 16;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            }
            SamplerPreset::ComparisonLinearClamp => {
                desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
                desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            }
        }

        let mut state = None;
        unsafe { device.CreateSamplerState(&desc, Some(&mut state)) }?;
        Ok(state.unwrap())
    }
}
