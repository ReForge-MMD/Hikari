//! DirectX 11 rendering bridge, D3D9/DX11 composite and draw call execution.

use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use windows::core::{Interface, HRESULT};
use windows::Win32::Foundation::S_OK;

use crate::d3d11::D3D11Context;
use windows::Win32::Graphics::Direct3D9 as Direct3D9;
use windows::Win32::Graphics::Direct3D9::{D3DFORMAT, D3DLOCKED_RECT, D3DSURFACE_DESC};

pub static GLOBAL_D3D11: Mutex<Option<D3D11Context>> = Mutex::new(None);
pub static DX11_FRAME_RENDERED: AtomicBool = AtomicBool::new(false);
pub static DRAW_INDEXED_TOTAL: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
pub static DRAW_INDEXED_ACTIVE: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
static DX11_FRAME_CLEARED: AtomicBool = AtomicBool::new(false);
pub static BOUND_VB: Mutex<Option<(usize, u32, u32)>> = Mutex::new(None);
pub static BOUND_IB: Mutex<Option<usize>> = Mutex::new(None);
pub static BOUND_TEXTURES: Mutex<[Option<usize>; 8]> = Mutex::new([None; 8]);

pub struct TexPair {
    pub sys_tex: Direct3D9::IDirect3DTexture9,
    pub gpu_tex: Option<Direct3D9::IDirect3DTexture9>,
    pub format: D3DFORMAT,
    pub size: (u32, u32),
}

unsafe impl Send for TexPair {}
unsafe impl Sync for TexPair {}

pub static TEX_PAIR: Mutex<Option<TexPair>> = Mutex::new(None);

pub struct SharedBridge {
    pub d3d9_tex: Direct3D9::IDirect3DTexture9,
    pub shared_handle: windows::Win32::Foundation::HANDLE,
    pub size: (u32, u32),
}

unsafe impl Send for SharedBridge {}
unsafe impl Sync for SharedBridge {}

pub static SHARED_BRIDGE: Mutex<Option<SharedBridge>> = Mutex::new(None);

pub fn release_d3d9_resources() {
    crate::log_msg("[vtable] release_d3d9_resources: dropping D3D9 default-pool surfaces");
    *SHARED_BRIDGE.lock().unwrap() = None;
    *TEX_PAIR.lock().unwrap() = None;
    *BOUND_VB.lock().unwrap() = None;
    *BOUND_IB.lock().unwrap() = None;
    *BOUND_TEXTURES.lock().unwrap() = [None; 8];
    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ref mut ctx) = *guard {
            ctx.texture_cache.clear();
            ctx.shared_target_tex = None;
        }
    }
}
static DRAW_FAILURE_LOGGED: AtomicBool = AtomicBool::new(false);
static DRAW_SUCCESS_LOGGED: AtomicBool = AtomicBool::new(false);
static DRAW_WAIT_LOGGED: AtomicBool = AtomicBool::new(false);
static FRAME_CLEAR_LOGGED: AtomicBool = AtomicBool::new(false);

fn clear_frame_targets_once(ctx: &mut D3D11Context) {
    if !DX11_FRAME_CLEARED.swap(true, Ordering::AcqRel) {
        // Transparent black (alpha = 0.0) so untouched pixels let the native MMD scene show through
        ctx.clear([0.0, 0.0, 0.0, 0.0], 1.0, 0);
        ctx.current_bound_srvs = [None; 8];
        if !FRAME_CLEAR_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg("Cleared DX11 overlay color/depth targets before the first model draw");
        }
    }
}

/// Cached raw IDirect3DDevice9 pointer discovered from a captured D3D9 vertex
/// buffer (resource GetDevice). Stored as an address; the borrowed reference
/// never owns a COM ref count.
static D3D9_DEVICE: Mutex<Option<usize>> = Mutex::new(None);
static COMPOSITE_LOGGED: AtomicBool = AtomicBool::new(false);

/// Obtains the real in-process D3D9 device through a captured D3D9 resource.
unsafe fn resolve_d3d9_device() -> Option<std::mem::ManuallyDrop<Direct3D9::IDirect3DDevice9>> {
    let cached = *D3D9_DEVICE.lock().unwrap();
    if let Some(addr) = cached {
        if addr != 0 {
            return Some(std::mem::ManuallyDrop::new(std::mem::transmute(addr)));
        }
    }
    let vb_addr = match *BOUND_VB.lock().unwrap() {
        Some(v) => v.0,
        None => return None,
    };
    if vb_addr == 0 {
        return None;
    }
    let vb: Direct3D9::IDirect3DVertexBuffer9 = std::mem::transmute(vb_addr as *mut c_void);
    let resource: Direct3D9::IDirect3DResource9 = std::mem::transmute(vb);
    let device = Direct3D9::IDirect3DResource9::GetDevice(&resource).ok()?;
    // Keep a borrowed handle: drop() would Release() the device ref that the
    // vertex buffer already owns. Store the raw id and re-wraph it later.
    let address = device.as_raw() as usize;
    std::mem::forget(device);
    if address != 0 {
        *D3D9_DEVICE.lock().unwrap() = Some(address);
        Some(std::mem::ManuallyDrop::new(std::mem::transmute(address)))
    } else {
        None
    }
}

/// EndScene interception (plugin slot 43, D3D9 slot 42). Composites Hikari's
/// DX11 render (captured MMD scene as background + effect model layer) into
/// the native D3D9 backbuffer right before the device finishes the scene.
unsafe extern "system" fn hook_end_scene(this: *mut c_void) -> HRESULT {
    let _ = this;
    if !DX11_FRAME_RENDERED.swap(false, Ordering::AcqRel) {
        return S_OK;
    }

    static COMPOSITE_RUNS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let run_no = COMPOSITE_RUNS.fetch_add(1, Ordering::Relaxed) + 1;
    let log_wheel = |stage: &str, extra: String| {
        if run_no <= 6 || run_no % 300 == 0 {
            crate::log_msg(&format!("composite[{run_no}] {stage}: {extra}"));
        }
    };

    let outcome: Result<(), String> = catch_run(|| {
        log_wheel("start", format!("frame_rendered"));
        let device = resolve_d3d9_device()
            .ok_or("no D3D9 device discovered")?;
        log_wheel("device", format!("{:p}", device.as_raw()));
        let mut guard = GLOBAL_D3D11.lock().unwrap();
        let ctx = guard.as_mut().ok_or("no D3D11 context")?;

        // Backbuffer description drives target sizes.
        let back = Direct3D9::IDirect3DDevice9::GetBackBuffer(
            &device, 0, 0, Direct3D9::D3DBACKBUFFER_TYPE_MONO,
        ).map_err(|e| format!("GetBackBuffer failed: {e}"))?;
        let mut desc = D3DSURFACE_DESC::default();
        back.GetDesc(&mut desc)
            .map_err(|e| format!("backbuffer GetDesc failed: {e}"))?;
        let (w, h) = (desc.Width, desc.Height);
        log_wheel("backbuffer", format!("{w}x{h}, format=0x{:X}", desc.Format.0));
        if w == 0 || h == 0 {
            return Err("backbuffer is empty".to_string());
        }
        ctx.recreate_composite(w, h).map_err(|e| format!("recreate composite failed: {e}"))?;
        log_wheel("targets_ready", String::new());

        let draw_quad = std::env::var("HIKARI_NATIVE_QUAD").ok().as_deref() != Some("0");

        // 1. Zero-Copy DXGI Shared Handle Composite (GPU-to-GPU, Direct3D 9Ex <-> D3D11)
        let mut zero_copy_rendered = false;
        {
            let mut bridge_slot = SHARED_BRIDGE.lock().unwrap();
            let needs_bridge_rebuild = match bridge_slot.as_ref() {
                Some(b) => b.size != (w, h),
                None => true,
            };

            if needs_bridge_rebuild {
                let mut shared_handle = windows::Win32::Foundation::HANDLE::default();
                let mut d3d9_shared_tex: Option<Direct3D9::IDirect3DTexture9> = None;
                let hr = Direct3D9::IDirect3DDevice9::CreateTexture(
                    &device,
                    w,
                    h,
                    1,
                    Direct3D9::D3DUSAGE_RENDERTARGET as u32,
                    Direct3D9::D3DFMT_A8R8G8B8,
                    Direct3D9::D3DPOOL_DEFAULT,
                    &mut d3d9_shared_tex,
                    &mut shared_handle,
                );
                crate::log_msg(&format!(
                    "[vtable] CreateTexture(shared) {}x{} hr=0x{:08X}, handle={:?}",
                    w, h, hr.as_ref().err().map_or(0, |e| e.code().0 as u32), shared_handle.0
                ));
                if hr.is_ok() && !shared_handle.0.is_null() {
                    if let Some(tex) = d3d9_shared_tex {
                        if let Err(e) = ctx.open_shared_d3d9_resource(shared_handle) {
                            crate::log_msg(&format!("[vtable] open_shared_d3d9_resource failed: {:?}", e));
                        } else {
                            *bridge_slot = Some(SharedBridge {
                                d3d9_tex: tex,
                                shared_handle,
                                size: (w, h),
                            });
                            crate::log_msg(&format!(
                                "[vtable] Zero-Copy DXGI Shared Texture bound: {}x{}, handle={:?}",
                                w, h, shared_handle.0
                            ));
                        }
                    }
                }
            }

            if let Some(ref b) = *bridge_slot {
                if let Some(ref d3d11_shared) = ctx.shared_target_tex {
                    if let Some(ref model_tex) = ctx.model_tex {
                        // Direct GPU-to-GPU copy from D3D11 model layer to shared D3D9 texture
                        ctx.context.CopyResource(d3d11_shared, model_tex);
                        ctx.context.Flush();
                        log_wheel("gpu_shared_copy", format!("zero-copy GPU blit {w}x{h}"));

                        if draw_quad {
                            draw_model_layer_d3d9(&device, &b.d3d9_tex, w, h)?;
                        }
                        log_wheel("native_quad_shared", if draw_quad { "drawn".to_string() } else { "skipped".to_string() });
                        zero_copy_rendered = true;
                    }
                }
            }
        }

        // 2. Fallback: CPU readback with D3DFMT_A8R8G8B8 if shared handle is not supported
        if !zero_copy_rendered {
            let rebuild = {
                let slot = TEX_PAIR.lock().unwrap();
                match slot.as_ref() {
                    Some(pair) => pair.size != (w, h) || pair.format != Direct3D9::D3DFMT_A8R8G8B8,
                    None => true,
                }
            };
            if rebuild {
                log_wheel("create_sys_tex", format!("{}x{} fmt=A8R8G8B8 sysmem", w, h));
                let mut sys_tex: Option<Direct3D9::IDirect3DTexture9> = None;
                Direct3D9::IDirect3DDevice9::CreateTexture(
                    &device, w, h, 1, 0, Direct3D9::D3DFMT_A8R8G8B8, Direct3D9::D3DPOOL_SYSTEMMEM,
                    &mut sys_tex, std::ptr::null_mut(),
                ).map_err(|e| format!("CreateTexture(sys) failed: {e}"))?;
                let sys_tex = sys_tex.ok_or("CreateTexture returned none")?;
                let mut gpu_tex: Option<Direct3D9::IDirect3DTexture9> = None;
                Direct3D9::IDirect3DDevice9::CreateTexture(
                    &device, w, h, 1, 0, Direct3D9::D3DFMT_A8R8G8B8, Direct3D9::D3DPOOL_DEFAULT,
                    &mut gpu_tex, std::ptr::null_mut(),
                ).map_err(|e| format!("CreateTexture(gpu) failed: {e}"))?;
                let gpu_tex = gpu_tex.ok_or("CreateTexture returned none")?;
                log_wheel("textures_created", format!("sys={:p} gpu={:p}", sys_tex.as_raw(), gpu_tex.as_raw()));
                *TEX_PAIR.lock().unwrap() = Some(TexPair {
                    sys_tex,
                    gpu_tex: Some(gpu_tex),
                    format: Direct3D9::D3DFMT_A8R8G8B8,
                    size: (w, h),
                });
                log_wheel("texpair_commit", String::new());
            }
            let (sys_tex, gpu_tex) = {
                let slot = TEX_PAIR.lock().unwrap();
                match slot.as_ref() {
                    Some(pair) => {
                        let gpu = pair.gpu_tex.clone().ok_or("gpu texture missing".to_string())?;
                        Ok((pair.sys_tex.clone(), gpu))
                    }
                    None => Err("texture pair missing".to_string()),
                }
            }?;
            log_wheel("texpair_fetch", String::new());

            let use_d3d9 = std::env::var("HIKARI_D9TEX").ok().as_deref() != Some("0");
            if !use_d3d9 {
                let bytes = ctx.readback_model_layer()
                    .map_err(|e| format!("model layer readback failed: {e}"))?;
                log_wheel("d9tex_disabled", format!("readback ok ({} bytes, no D3D9 calls)", bytes.len()));
                return Ok(());
            }
            let bytes = ctx.readback_model_layer()
                .map_err(|e| format!("model layer readback failed: {e}"))?;
            let mut non_zero_alpha = 0usize;
            let mut max_a = 0u8;
            let mut sample = [0u8; 4];
            for p in bytes.chunks_exact(4) {
                if p[3] > 0 {
                    if non_zero_alpha == 0 { sample = [p[0], p[1], p[2], p[3]]; }
                    non_zero_alpha += 1;
                    if p[3] > max_a { max_a = p[3]; }
                }
            }
            log_wheel("model_readback", format!("{} bytes, non_zero_a={}, max_a={}, sample_bgra={:?}", bytes.len(), non_zero_alpha, max_a, sample));

            write_system_texture(&sys_tex, w, h, &bytes)?;
            log_wheel("write_result", String::new());

            let sys_surface = Direct3D9::IDirect3DTexture9::GetSurfaceLevel(&sys_tex, 0)
                .map_err(|e| format!("GetSurfaceLevel(sys) failed: {e}"))?;
            let gpu_surface = Direct3D9::IDirect3DTexture9::GetSurfaceLevel(&gpu_tex, 0)
                .map_err(|e| format!("GetSurfaceLevel(gpu) failed: {e}"))?;
            Direct3D9::IDirect3DDevice9::UpdateSurface(
                &device, &sys_surface, std::ptr::null(), &gpu_surface, std::ptr::null(),
            ).map_err(|e| format!("UpdateSurface failed: {}", e.code()))?;
            log_wheel("update_surface", String::new());

            if draw_quad {
                draw_model_layer_d3d9(&device, &gpu_tex, w, h)
                    .map_err(|e| e)?;
            }
            log_wheel("native_quad_fallback", if draw_quad { "drawn".to_string() } else { "skipped (HIKARI_NATIVE_QUAD=0)".to_string() });
        }

        if !COMPOSITE_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg(&format!(
                "Composite drawn into native backbuffer: {}x{} (zero_copy={})",
                w, h, zero_copy_rendered
            ));
        }
        Ok(())
    });

    DX11_FRAME_CLEARED.store(false, Ordering::Release);

    match outcome {
        Ok(()) => {
            if run_no <= 6 || run_no % 300 == 0 {
                crate::log_msg(&format!("composite[{run_no}] end: ok"));
            }
            S_OK
        },
        Err(error) => {
            static ERROR_LOGGED: AtomicBool = AtomicBool::new(false);
            if !ERROR_LOGGED.swap(true, Ordering::Relaxed) {
                crate::log_msg(&format!("DX11 composite skipped: {error}"));
            }
            S_OK
        }
    }
}

/// Writes tightly packed BGRA bytes into a D3D9 system-memory texture.
unsafe fn write_system_texture(
    tex: &Direct3D9::IDirect3DTexture9,
    width: u32,
    height: u32,
    bytes: &[u8],
) -> Result<(), String> {
    let row_bytes = width as usize * 4;
    let expected = row_bytes * height as usize;
    if bytes.len() < expected {
        return Err(format!("composite bytes {} < expected {}", bytes.len(), expected));
    }
    let mut locked = D3DLOCKED_RECT::default();
    Direct3D9::IDirect3DTexture9::LockRect(tex, 0, &mut locked, std::ptr::null(), 0)
        .map_err(|e| format!("LockRect failed: {:?}", e.code()))?;
    if locked.pBits.is_null() {
        return Err("LockRect returned null bits".to_string());
    }
    let pitch = locked.Pitch as usize;
    let dst = std::slice::from_raw_parts_mut(locked.pBits as *mut u8, pitch * height as usize);
    for y in 0..height as usize {
        let src_start = y * row_bytes;
        dst[y * pitch..y * pitch + row_bytes].copy_from_slice(&bytes[src_start..src_start + row_bytes]);
    }
    let _ = Direct3D9::IDirect3DTexture9::UnlockRect(tex, 0).is_ok();
    Ok(())
}

fn catch_run<T>(f: impl FnOnce() -> Result<T, String>) -> Result<T, String> {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(_) => Err("panic inside composite".to_string()),
    }
}

/// Draws the DX11 model layer texture over the native D3D9 backbuffer with
/// the real device: fixed-function textured quad, straight alpha blending.
/// Runs inside the device's BeginScene (EndScene hook time).
unsafe fn draw_model_layer_d3d9(
    device: &Direct3D9::IDirect3DDevice9,
    layer_tex: &Direct3D9::IDirect3DTexture9,
    width: u32,
    height: u32,
) -> Result<(), String> {
    use windows::Win32::Graphics::Direct3D9::{
        D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
        D3DRS_ZENABLE, D3DRS_LIGHTING, D3DRS_CLIPPING, D3DRS_CULLMODE, D3DCULL_NONE,
        D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA,
        D3DTSS_ALPHAOP, D3DTSS_COLOROP, D3DTOP_SELECTARG1, D3DTOP_DISABLE,
        D3DTSS_COLORARG1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
        D3DFVF_XYZRHW, D3DFVF_TEX1, D3DPT_TRIANGLELIST,
    };

    #[repr(C)]
    struct RhwVertex {
        x: f32, y: f32, z: f32, rhw: f32,
        u: f32, v: f32,
    }
    let w = width as f32;
    let h = height as f32;
    let verts = [
        RhwVertex { x: 0.0, y: h, z: 0.5, rhw: 1.0, u: 0.0, v: 1.0 },
        RhwVertex { x: 0.0, y: 0.0, z: 0.5, rhw: 1.0, u: 0.0, v: 0.0 },
        RhwVertex { x: w,  y: 0.0, z: 0.5, rhw: 1.0, u: 1.0, v: 0.0 },
        RhwVertex { x: 0.0, y: h, z: 0.5, rhw: 1.0, u: 0.0, v: 1.0 },
        RhwVertex { x: w,  y: 0.0, z: 0.5, rhw: 1.0, u: 1.0, v: 0.0 },
        RhwVertex { x: w,  y: h, z: 0.5, rhw: 1.0, u: 1.0, v: 1.0 },
    ];

    // Save every state this overlay touch alters; MMD's next frame relies on
    // the device state it left behind (an unrepaired save once landed the
    // following frame in MMD's CRT with an access violation).
    let mut old = [0u32; 4];
    for (slot, state) in [
        D3DRS_ZENABLE, D3DRS_LIGHTING, D3DRS_CLIPPING, D3DRS_ALPHATESTENABLE,
    ].iter().enumerate() {
        let mut v = 0u32;
        Direct3D9::IDirect3DDevice9::GetRenderState(device, *state, &mut v).ok();
        old[slot] = v;
    }
    let mut old_cull = 0u32;
    Direct3D9::IDirect3DDevice9::GetRenderState(device, D3DRS_CULLMODE, &mut old_cull).ok();
    let mut rs_blend = 0u32;
    Direct3D9::IDirect3DDevice9::GetRenderState(device, D3DRS_ALPHABLENDENABLE, &mut rs_blend).ok();
    let mut rs_src = 0u32;
    Direct3D9::IDirect3DDevice9::GetRenderState(device, D3DRS_SRCBLEND, &mut rs_src).ok();
    let mut rs_dst = 0u32;
    Direct3D9::IDirect3DDevice9::GetRenderState(device, D3DRS_DESTBLEND, &mut rs_dst).ok();
    let mut ts_color = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 0, D3DTSS_COLOROP, &mut ts_color).ok();
    let mut ts_color_arg1 = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 0, D3DTSS_COLORARG1, &mut ts_color_arg1).ok();
    let mut ts_alpha = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 0, D3DTSS_ALPHAOP, &mut ts_alpha).ok();
    let mut ts_alpha_arg1 = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 0, D3DTSS_ALPHAARG1, &mut ts_alpha_arg1).ok();
    let mut ts1_color = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 1, D3DTSS_COLOROP, &mut ts1_color).ok();
    let mut ts1_alpha = 0u32;
    Direct3D9::IDirect3DDevice9::GetTextureStageState(device, 1, D3DTSS_ALPHAOP, &mut ts1_alpha).ok();
    let mut old_fvf = 0u32;
    Direct3D9::IDirect3DDevice9::GetFVF(device, &mut old_fvf).ok();
    let old_decl = Direct3D9::IDirect3DDevice9::GetVertexDeclaration(device).ok();
    let old_tex = Direct3D9::IDirect3DDevice9::GetTexture(device, 0).ok();
    let old_vs = Direct3D9::IDirect3DDevice9::GetVertexShader(device).ok();
    let old_ps = Direct3D9::IDirect3DDevice9::GetPixelShader(device).ok();

    Direct3D9::IDirect3DDevice9::SetVertexShader(device, None).ok();
    Direct3D9::IDirect3DDevice9::SetPixelShader(device, None).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ZENABLE, 0)
        .map_err(|e| format!("SetRenderState(Z) failed: {}", e.code()))?;
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_LIGHTING, 0).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_CLIPPING, 0).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHATESTENABLE, 0).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHABLENDENABLE, 1)
        .map_err(|e| format!("SetRenderState(blend) failed: {}", e.code()))?;
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE.0 as u32).ok();

    Direct3D9::IDirect3DDevice9::SetFVF(device, D3DFVF_XYZRHW | D3DFVF_TEX1)
        .map_err(|e| format!("SetFVF failed: {}", e.code()))?;
    Direct3D9::IDirect3DDevice9::SetTexture(device, 0, layer_tex)
        .map_err(|e| format!("SetTexture failed: {}", e.code()))?;
    Direct3D9::IDirect3DDevice9::DrawPrimitiveUP(
        device, D3DPT_TRIANGLELIST, 2, verts.as_ptr() as *const c_void, 24,
    ).map_err(|e| format!("DrawPrimitiveUP failed: {}", e.code()))?;

    // Restore the device exactly as MMD had left it.
    Direct3D9::IDirect3DDevice9::SetTexture(device, 0, old_tex.as_ref()).ok();
    if let Some(ref decl) = old_decl {
        Direct3D9::IDirect3DDevice9::SetVertexDeclaration(device, Some(decl)).ok();
    } else {
        Direct3D9::IDirect3DDevice9::SetFVF(device, old_fvf).ok();
    }
    Direct3D9::IDirect3DDevice9::SetVertexShader(device, old_vs.as_ref()).ok();
    Direct3D9::IDirect3DDevice9::SetPixelShader(device, old_ps.as_ref()).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ZENABLE, old[0]).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_LIGHTING, old[1]).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_CLIPPING, old[2]).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHATESTENABLE, old[3]).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_CULLMODE, old_cull).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHABLENDENABLE, rs_blend).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_SRCBLEND, rs_src).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_DESTBLEND, rs_dst).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLOROP, ts_color).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLORARG1, ts_color_arg1).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAOP, ts_alpha).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, ts_alpha_arg1).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 1, D3DTSS_COLOROP, ts1_color).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 1, D3DTSS_ALPHAOP, ts1_alpha).ok();
    Ok(())
}

unsafe extern "system" fn hook_draw_indexed_primitive(
    _this: *mut c_void,
    prim_type: u32,
    base_vertex_index: i32,
    min_vertex_index: u32,
    num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) -> bool {
    let active_pass = {
        let guard = crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap();
        guard.clone()
    };

    let current_obj = crate::mmd_host::get_current_object();
    let current_mat = crate::mmd_host::get_current_material();
    let current_tech = crate::mmd_host::get_current_technic();

    // When there is no explicit active pass, only intercept model mesh passes:
    // Material < 0 indicates non-mesh objects (bones, bone select boxes, axis guides, UI handles).
    if active_pass.is_none() && current_mat < 0 {
        return false;
    }

    static PASS_STATE_LOG: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    let total = DRAW_INDEXED_TOTAL.fetch_add(1, Ordering::Relaxed) + 1;
    let pass_info = match active_pass {
        Some(p) => {
            DRAW_INDEXED_ACTIVE.fetch_add(1, Ordering::Relaxed);
            if !PASS_STATE_LOG.swap(true, Ordering::Relaxed) {
                crate::log_msg("hook_draw_indexed_primitive: SM5 effect pass is ACTIVE - intercepting and drawing on DirectX 11");
            }
            p
        }
        None => {
            if let Some(p) = crate::effect::get_assigned_effect_pass(current_obj, current_mat, current_tech) {
                DRAW_INDEXED_ACTIVE.fetch_add(1, Ordering::Relaxed);
                if !PASS_STATE_LOG.swap(true, Ordering::Relaxed) {
                    crate::log_msg(&format!(
                        "hook_draw_indexed_primitive: Model/Subset assigned effect is ACTIVE (obj={current_obj}, mat={current_mat}, tech={current_tech}) - intercepting and drawing on DirectX 11"
                    ));
                }
                p
            } else if let Some(p) = crate::effect::get_active_direct_effect_pass() {
                DRAW_INDEXED_ACTIVE.fetch_add(1, Ordering::Relaxed);
                if !PASS_STATE_LOG.swap(true, Ordering::Relaxed) {
                    crate::log_msg("hook_draw_indexed_primitive: Direct Hikari effect is ACTIVE - intercepting and drawing on DirectX 11");
                }
                p
            } else {
                if total % 300 == 0 {
                    let active = DRAW_INDEXED_ACTIVE.load(Ordering::Relaxed);
                    crate::log_msg(&format!(
                        "DrawIndexedPrimitive heartbeat: total={total}, SM5-active={active}, obj={current_obj}, mat={current_mat}, tech={current_tech}, the rest are passed through to the D3D9/MMDX11 path"
                    ));
                }
                return false;
            }
        }
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    let ctx = match guard.as_mut() {
        Some(c) => c,
        None => {
            log_draw_failure_once("D3D11 context is missing");
            return false;
        }
    };
    if ctx.render_target_view.is_none() {
        let device = resolve_d3d9_device();
        if let Some(dev) = device {
            if let Ok(back) = Direct3D9::IDirect3DDevice9::GetBackBuffer(&dev, 0, 0, Direct3D9::D3DBACKBUFFER_TYPE_MONO) {
                let mut desc = D3DSURFACE_DESC::default();
                if back.GetDesc(&mut desc).is_ok() && desc.Width > 0 && desc.Height > 0 {
                    let _ = ctx.recreate_composite(desc.Width, desc.Height);
                }
            }
        }
    }
    if ctx.render_target_view.is_none() {
        if !DRAW_WAIT_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg("Skipping initial DrawIndexedPrimitive until the composite model target exists");
        }
        return false;
    }
    clear_frame_targets_once(ctx);

    let (vb_addr, stride, _offset) = match *BOUND_VB.lock().unwrap() {
        Some(v) => v,
        None => {
            log_draw_failure_once("no vertex buffer was captured before the indexed draw");
            return false;
        }
    };

    let ib_addr = match *BOUND_IB.lock().unwrap() {
        Some(i) => i,
        None => {
            log_draw_failure_once("no index buffer was captured before the indexed draw");
            return false;
        }
    };

    let vb9: windows::Win32::Graphics::Direct3D9::IDirect3DVertexBuffer9 = unsafe {
        std::mem::transmute(vb_addr as *mut c_void)
    };
    let d3d11_vb = match ctx.get_or_upload_vertex_buffer(vb_addr, &vb9) {
        Ok(b) => {
            std::mem::forget(vb9);
            b
        }
        Err(error) => {
            std::mem::forget(vb9);
            log_draw_failure_once(&format!("uploading the vertex buffer failed: {error}"));
            return false;
        }
    };

    let ib9: windows::Win32::Graphics::Direct3D9::IDirect3DIndexBuffer9 = unsafe {
        std::mem::transmute(ib_addr as *mut c_void)
    };
    let (d3d11_ib, is_32bit) = match ctx.get_or_upload_index_buffer(ib_addr, &ib9) {
        Ok(res) => {
            std::mem::forget(ib9);
            res
        }
        Err(error) => {
            std::mem::forget(ib9);
            log_draw_failure_once(&format!("uploading the index buffer failed: {error}"));
            return false;
        }
    };

    let input_layout = match ctx.get_or_create_input_layout(&pass_info.vs_bytecode, stride) {
        Ok(l) => l,
        Err(error) => {
            log_draw_failure_once(&format!("creating the vertex input layout failed (stride {stride}): {error}"));
            return false;
        }
    };

    let textures = *BOUND_TEXTURES.lock().unwrap();
    for (slot, tex_opt) in textures.iter().enumerate() {
        if let Some(tex_addr) = *tex_opt {
            if ctx.current_bound_srvs[slot] != Some(tex_addr) {
                if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(tex_addr as *mut c_void) {
                    let srvs = [Some(srv)];
                    unsafe {
                        ctx.context.PSSetShaderResources(slot as u32, Some(&srvs));
                        ctx.context.VSSetShaderResources(slot as u32, Some(&srvs));
                    }
                    ctx.current_bound_srvs[slot] = Some(tex_addr);
                }
            }
        } else if ctx.current_bound_srvs[slot].is_some() {
            let srvs: [Option<windows::Win32::Graphics::Direct3D11::ID3D11ShaderResourceView>; 1] = [None];
            unsafe {
                ctx.context.PSSetShaderResources(slot as u32, Some(&srvs));
                ctx.context.VSSetShaderResources(slot as u32, Some(&srvs));
            }
            ctx.current_bound_srvs[slot] = None;
        }
    }

    let index_count = prim_count * 3;
    unsafe {
        ctx.context.VSSetShader(pass_info.vs.as_ref(), None);
        ctx.context.PSSetShader(pass_info.ps.as_ref(), None);
        ctx.context.GSSetShader(pass_info.gs.as_ref(), None);
        if let Some(ref cb) = pass_info.constant_buffer {
            let cbs = [Some(cb.clone()), Some(cb.clone()), Some(cb.clone()), Some(cb.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cbs));
            ctx.context.PSSetConstantBuffers(0, Some(&cbs));
            if pass_info.gs.is_some() {
                ctx.context.GSSetConstantBuffers(0, Some(&cbs));
            }
        }
        ctx.context.IASetInputLayout(&input_layout);
        let vbs = [Some(d3d11_vb)];
        let strides = [stride];
        let offsets = [0u32];
        ctx.context.IASetVertexBuffers(0, 1, Some(vbs.as_ptr()), Some(strides.as_ptr()), Some(offsets.as_ptr()));

        let index_format = if is_32bit {
            windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_R32_UINT
        } else {
            windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_R16_UINT
        };
        ctx.context.IASetIndexBuffer(&d3d11_ib, index_format, 0);

        ctx.context.IASetPrimitiveTopology(
            windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        );

        if let Some(raster) = ctx.state_manager.get_rasterizer(pass_info.raster_preset.clone()) {
            ctx.context.RSSetState(Some(raster));
        }
        if let Some(depth) = ctx.state_manager.get_depth(pass_info.depth_preset.clone()) {
            ctx.context.OMSetDepthStencilState(Some(depth), 1);
        }
        if let Some(blend) = ctx.state_manager.get_blend(pass_info.blend_preset.clone()) {
            ctx.context.OMSetBlendState(Some(blend), Some(&[0.0, 0.0, 0.0, 0.0]), 0xFFFFFFFF);
        }

        if let Some(ref rtv) = ctx.render_target_view {
            ctx.context.OMSetRenderTargets(Some(&[Some(rtv.clone())]), ctx.depth_stencil_view.as_ref());
        }

        let vp = crate::backend::CURRENT_VIEWPORT.lock().unwrap().unwrap_or(
            windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
                TopLeftX: 0.0,
                TopLeftY: 0.0,
                Width: ctx.width as f32,
                Height: ctx.height as f32,
                MinDepth: 0.0,
                MaxDepth: 1.0,
            }
        );
        ctx.context.RSSetViewports(Some(&[vp]));

        ctx.context.DrawIndexed(index_count, start_index, base_vertex_index);
    }

    if !DRAW_SUCCESS_LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg(&format!(
            "D3D11 indexed shader draw submitted: prim_type={prim_type}, base_vertex={base_vertex_index}, min_vertex={min_vertex_index}, num_vertices={num_vertices}, start_index={start_index}, prim_count={prim_count}, stride={stride}, indices={index_count}, IB_32bit={is_32bit}, VS={}, PS={}",
            pass_info.vs.is_some(),
            pass_info.ps.is_some(),
        ));
    }

    DX11_FRAME_RENDERED.store(true, Ordering::Release);
    true
}

fn log_draw_failure_once(message: &str) {
    if !DRAW_FAILURE_LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg(&format!("D3D11 indexed shader draw skipped: {message}"));
    }
}

pub unsafe fn register_d3d9_device(device: *mut c_void) {
    if !device.is_null() {
        *D3D9_DEVICE.lock().unwrap() = Some(device as usize);
    }
}

pub unsafe fn ensure_composite_targets_initialized(device_ptr: *mut c_void) {
    if device_ptr.is_null() {
        return;
    }
    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ctx) = guard.as_mut() {
            let dev: Direct3D9::IDirect3DDevice9 = std::mem::transmute(device_ptr);
            if let Ok(back) = dev.GetBackBuffer(0, 0, Direct3D9::D3DBACKBUFFER_TYPE_MONO) {
                let mut desc = D3DSURFACE_DESC::default();
                if back.GetDesc(&mut desc).is_ok() && desc.Width > 0 && desc.Height > 0 {
                    let needs_recreate = ctx.render_target_view.is_none()
                        || ctx.width != desc.Width
                        || ctx.height != desc.Height;
                    if needs_recreate {
                        let _ = ctx.recreate_composite(desc.Width, desc.Height);
                        crate::log_msg(&format!(
                            "[backend] Auto-initialized D3D11 composite targets: {}x{}",
                            desc.Width, desc.Height
                        ));
                    }
                }
            }
            std::mem::forget(dev);
        }
    }
}

pub unsafe fn execute_dx11_composite(device: *mut c_void) {
    let _ = hook_end_scene(device);
}

pub unsafe fn execute_dx11_draw_indexed(
    this: *mut c_void,
    prim_type: u32,
    base_vertex_index: i32,
    min_vertex_index: u32,
    num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) -> bool {
    hook_draw_indexed_primitive(
        this,
        prim_type,
        base_vertex_index,
        min_vertex_index,
        num_vertices,
        start_index,
        prim_count,
    )
}
