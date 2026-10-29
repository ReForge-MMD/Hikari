//! Direct3D 11 Texture and Shader Resource View loader for Hikari Render.

use hikari_core::d3d11::D3D11Context;
use std::path::Path;
use windows::core::Result;
use windows::Win32::Graphics::Direct3D::*;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::*;

#[derive(Clone)]
pub struct GpuTexture {
    pub texture: ID3D11Texture2D,
    pub srv: ID3D11ShaderResourceView,
    pub width: u32,
    pub height: u32,
}

impl GpuTexture {
    /// Creates a DirectX 11 texture from raw 8-bit RGBA pixel slice.
    pub fn from_rgba_bytes(
        ctx: &D3D11Context,
        width: u32,
        height: u32,
        pixels: &[u8],
    ) -> Result<Self> {
        let desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Usage: D3D11_USAGE_IMMUTABLE,
            BindFlags: D3D11_BIND_SHADER_RESOURCE.0 as u32,
            CPUAccessFlags: 0,
            MiscFlags: 0,
        };

        let subresource = D3D11_SUBRESOURCE_DATA {
            pSysMem: pixels.as_ptr() as *const _,
            SysMemPitch: width * 4,
            SysMemSlicePitch: 0,
        };

        let mut texture: Option<ID3D11Texture2D> = None;
        unsafe {
            ctx.device
                .CreateTexture2D(&desc, Some(&subresource), Some(&mut texture))?;
        }
        let texture = texture.unwrap();

        let srv_desc = D3D11_SHADER_RESOURCE_VIEW_DESC {
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            ViewDimension: D3D_SRV_DIMENSION_TEXTURE2D,
            Anonymous: D3D11_SHADER_RESOURCE_VIEW_DESC_0 {
                Texture2D: D3D11_TEX2D_SRV {
                    MostDetailedMip: 0,
                    MipLevels: 1,
                },
            },
        };

        let mut srv: Option<ID3D11ShaderResourceView> = None;
        unsafe {
            ctx.device
                .CreateShaderResourceView(&texture, Some(&srv_desc), Some(&mut srv))?;
        }

        Ok(Self {
            texture,
            srv: srv.unwrap(),
            width,
            height,
        })
    }

    /// Loads and decodes an image file (PNG, JPEG, BMP, TGA, DDS, etc.) into a D3D11 Texture2D.
    pub fn from_file<P: AsRef<Path>>(ctx: &D3D11Context, path: P) -> Result<Self> {
        let bytes = std::fs::read(path.as_ref())
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e.to_string()))?;
        Self::from_memory(ctx, &bytes)
    }

    /// Decodes an image from memory bytes.
    pub fn from_memory(ctx: &D3D11Context, bytes: &[u8]) -> Result<Self> {
        let img = image::load_from_memory(bytes)
            .map_err(|e| windows::core::Error::new(windows::core::HRESULT(-1), e.to_string()))?;
        let rgba = img.to_rgba8();
        Self::from_rgba_bytes(ctx, rgba.width(), rgba.height(), &rgba)
    }

    /// Creates a 1x1 solid colored texture (e.g. [255, 255, 255, 255] for default white).
    pub fn create_solid_color(ctx: &D3D11Context, color: [u8; 4]) -> Result<Self> {
        Self::from_rgba_bytes(ctx, 1, 1, &color)
    }
}
