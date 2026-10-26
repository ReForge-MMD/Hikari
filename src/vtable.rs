//! MSVC x64 C++ vtable layouts and call interception for MMDPlugin.

use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, OnceLock};
use windows::core::{HRESULT, PCWSTR};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, RECT, S_OK, WPARAM};
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::com::*;
use crate::d3d11::D3D11Context;
use crate::effect::HikariEffect;

pub static GLOBAL_D3D11: Mutex<Option<D3D11Context>> = Mutex::new(None);
pub static DX11_FRAME_RENDERED: AtomicBool = AtomicBool::new(false);
pub static BOUND_VB: Mutex<Option<(usize, u32, u32)>> = Mutex::new(None);
pub static BOUND_IB: Mutex<Option<usize>> = Mutex::new(None);
pub static BOUND_TEXTURES: Mutex<[Option<usize>; 8]> = Mutex::new([None; 8]);
pub static ACTIVE_RENDER_TARGETS: Mutex<[Option<usize>; 4]> = Mutex::new([None; 4]);
pub static ACTIVE_DEPTH_STENCIL: Mutex<Option<usize>> = Mutex::new(None);
static MMD_PLUGIN_DLL4_VTBL: OnceLock<[usize; 250]> = OnceLock::new();

struct OverlayState {
    hwnd: HWND,
    parent: HWND,
    x: i32,
    y: i32,
    w: u32,
    h: u32,
    visible: bool,
}

unsafe impl Send for OverlayState {}
unsafe impl Sync for OverlayState {}

static OVERLAY: Mutex<OverlayState> = Mutex::new(OverlayState {
    hwnd: HWND(null_mut()),
    parent: HWND(null_mut()),
    x: 0,
    y: 0,
    w: 0,
    h: 0,
    visible: false,
});

/// Passthrough WndProc for the dedicated DX11 viewport overlay window.
/// Transparent to mouse input so camera and model manipulation pass directly to MMD.
unsafe extern "system" fn overlay_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_NCHITTEST => LRESULT(HTTRANSPARENT as isize),
        WM_ERASEBKGND => LRESULT(1),
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
    }
}

fn ensure_overlay_window(parent: HWND, x: i32, y: i32, w: u32, h: u32) -> HWND {
    let mut state = OVERLAY.lock().unwrap();

    if state.hwnd.0.is_null() || state.parent != parent {
        let class_name = windows::core::w!("HikariDX11Overlay");
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(overlay_wndproc),
            ..Default::default()
        };
        unsafe {
            let _ = RegisterClassExW(&wc);
            let hwnd = CreateWindowExW(
                WS_EX_NOPARENTNOTIFY,
                class_name,
                windows::core::w!("Hikari DX11 Overlay"),
                WS_CHILD | WS_CLIPSIBLINGS,
                x,
                y,
                w as i32,
                h as i32,
                Some(parent),
                None,
                None,
                None,
            ).unwrap_or(HWND(null_mut()));
            state.hwnd = hwnd;
            state.parent = parent;
            state.x = x;
            state.y = y;
            state.w = w;
            state.h = h;
            state.visible = false;
        }
    } else if state.x != x || state.y != y || state.w != w || state.h != h {
        unsafe {
            let _ = SetWindowPos(
                state.hwnd,
                None,
                x,
                y,
                w as i32,
                h as i32,
                SWP_NOZORDER | SWP_NOACTIVATE,
            );
        }
        state.x = x;
        state.y = y;
        state.w = w;
        state.h = h;
    }

    if !state.visible && !state.hwnd.0.is_null() {
        unsafe {
            let _ = ShowWindow(state.hwnd, SW_SHOWNA);
        }
        state.visible = true;
    }

    state.hwnd
}

fn hide_overlay_if_visible() {
    let mut state = OVERLAY.lock().unwrap();
    if state.visible && !state.hwnd.0.is_null() {
        unsafe {
            let _ = ShowWindow(state.hwnd, SW_HIDE);
        }
        state.visible = false;
    }
}

/// Global MMDPlugin API: getHWND export from MMDPlugin.dll
type PfnGetHWND = unsafe extern "C" fn() -> HWND;

fn get_mmd_hwnd() -> HWND {
    unsafe {
        let mmdplugin_mod = windows::Win32::System::LibraryLoader::GetModuleHandleW(windows::core::w!("MMDPlugin.dll")).unwrap_or_default();
        if !mmdplugin_mod.0.is_null() {
            if let Some(proc) = windows::Win32::System::LibraryLoader::GetProcAddress(mmdplugin_mod, windows::core::s!("getHWND")) {
                let func: PfnGetHWND = std::mem::transmute(proc);
                return func();
            }
        }
        HWND(null_mut())
    }
}

/// Generic dummy function for unused virtual callbacks
unsafe extern "system" fn dummy_vfunc(_this: *mut c_void) {}

// Critical hook handlers
unsafe extern "system" fn hook_start(_this: *mut c_void) {
    crate::log_msg("hook_start() called - initializing D3D11 context");
    ensure_mme_hooked();
    let mut guard = GLOBAL_D3D11.lock().unwrap();
    if guard.is_none() {
        // Initialize headless D3D11 context without creating any swapchain yet
        if let Ok(ctx) = D3D11Context::new(HWND(null_mut()), 0, 0) {
            crate::log_msg("D3D11Context successfully initialized");
            *guard = Some(ctx);
        } else {
            crate::log_msg("D3D11Context failed to initialize");
        }
    }
}

unsafe extern "system" fn hook_stop(_this: *mut c_void) {
    crate::log_msg("hook_stop() called");
    hide_overlay_if_visible();
    let mut guard = GLOBAL_D3D11.lock().unwrap();
    *guard = None;
}

unsafe extern "system" fn hook_get_plugin_title(_this: *mut c_void) -> *const i8 {
    windows::core::s!("Hikari DX11 Runtime").as_ptr() as *const i8
}

unsafe extern "system" fn hook_present(
    _this: *mut c_void,
    _pSourceRect: *const c_void,
    pDestRect: *const c_void,
    hDestWindow: HWND,
    _pDirtyRegion: *const c_void,
) {
    ensure_mme_hooked();

    // Guard against presenting when no DX11 shader was rendered this frame.
    // Prevents swapchain contention with MMD's D3D9 device and avoids GUI flickering.
    if !DX11_FRAME_RENDERED.swap(false, Ordering::AcqRel) {
        hide_overlay_if_visible();
        return;
    }

    static PRESENT_LOG_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let pcount = PRESENT_LOG_COUNT.fetch_add(1, Ordering::Relaxed);
    if pcount == 0 || pcount % 300 == 0 {
        crate::log_msg(&format!("DX11 Present active frame #{}", pcount));
    }

    let dest_rect = if !pDestRect.is_null() {
        *(pDestRect as *const RECT)
    } else {
        RECT { left: 0, top: 0, right: 1280, bottom: 720 }
    };

    let vx = dest_rect.left;
    let vy = dest_rect.top;
    let vw = (dest_rect.right - dest_rect.left).max(1) as u32;
    let vh = (dest_rect.bottom - dest_rect.top).max(1) as u32;

    let parent_hwnd = if !hDestWindow.0.is_null() { hDestWindow } else { get_mmd_hwnd() };
    let overlay_hwnd = ensure_overlay_window(parent_hwnd, vx, vy, vw, vh);

    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ref mut ctx) = *guard {
            if ctx.swap_chain.is_none() || ctx.hwnd != overlay_hwnd {
                let _ = ctx.init_swap_chain(overlay_hwnd, vw, vh);
            } else if ctx.width != vw || ctx.height != vh {
                let _ = ctx.resize(vw, vh);
            }
            let _ = ctx.present(1);
        }
    }
}

unsafe extern "system" fn hook_reset(_this: *mut c_void, _pPresentationParameters: *mut c_void) {
    // Viewport dimensions and positioning are dynamically synchronized in hook_present from pDestRect
}

pub fn ensure_mme_hooked() {
    static HOOKED_EFFECT: AtomicBool = AtomicBool::new(false);
    static HOOKED_VS: AtomicBool = AtomicBool::new(false);
    static HOOKED_PS: AtomicBool = AtomicBool::new(false);
    static HOOKED_VER: AtomicBool = AtomicBool::new(false);

    unsafe {
        if !HOOKED_EFFECT.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXCreateEffectFromFileW",
                hook_d3dx_create_effect_from_file_w as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXCreateEffectFromFileW in MMEffect.dll!");
                HOOKED_EFFECT.store(true, Ordering::Relaxed);
            }
        }

        if !HOOKED_VS.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXGetVertexShaderProfile",
                hook_d3dx_get_vertex_shader_profile as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXGetVertexShaderProfile in MMEffect.dll!");
                HOOKED_VS.store(true, Ordering::Relaxed);
            }
        }

        if !HOOKED_PS.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXGetPixelShaderProfile",
                hook_d3dx_get_pixel_shader_profile as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXGetPixelShaderProfile in MMEffect.dll!");
                HOOKED_PS.store(true, Ordering::Relaxed);
            }
        }

        if !HOOKED_VER.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXGetShaderVersion",
                hook_d3dx_get_shader_version as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXGetShaderVersion in MMEffect.dll!");
                HOOKED_VER.store(true, Ordering::Relaxed);
            }
        }
    }
}

pub unsafe extern "system" fn hook_d3dx_get_vertex_shader_profile(_p_device: *mut c_void) -> *const i8 {
    let mut guard = GLOBAL_D3D11.lock().unwrap();
    if guard.is_none() {
        if let Ok(ctx) = D3D11Context::new(HWND(null_mut()), 0, 0) {
            *guard = Some(ctx);
        }
    }
    let profile: &[u8] = if let Some(ref ctx) = *guard {
        match ctx.vs_profile() {
            "vs_5_0" => b"vs_5_0\0",
            "vs_4_1" => b"vs_4_1\0",
            "vs_4_0" => b"vs_4_0\0",
            _ => b"vs_5_0\0",
        }
    } else {
        b"vs_5_0\0"
    };
    crate::log_msg(&format!(">>> D3DXGetVertexShaderProfile intercepted -> {}", std::str::from_utf8(profile).unwrap_or("")));
    profile.as_ptr() as *const i8
}

pub unsafe extern "system" fn hook_d3dx_get_pixel_shader_profile(_p_device: *mut c_void) -> *const i8 {
    let mut guard = GLOBAL_D3D11.lock().unwrap();
    if guard.is_none() {
        if let Ok(ctx) = D3D11Context::new(HWND(null_mut()), 0, 0) {
            *guard = Some(ctx);
        }
    }
    let profile: &[u8] = if let Some(ref ctx) = *guard {
        match ctx.ps_profile() {
            "ps_5_0" => b"ps_5_0\0",
            "ps_4_1" => b"ps_4_1\0",
            "ps_4_0" => b"ps_4_0\0",
            _ => b"ps_5_0\0",
        }
    } else {
        b"ps_5_0\0"
    };
    crate::log_msg(&format!(">>> D3DXGetPixelShaderProfile intercepted -> {}", std::str::from_utf8(profile).unwrap_or("")));
    profile.as_ptr() as *const i8
}

pub unsafe extern "system" fn hook_d3dx_get_shader_version(p_function: *const u32) -> u32 {
    if p_function.is_null() {
        return 0;
    }
    // Check for DirectX 11 DXBC container magic (0x43425844)
    if *p_function == 0x43425844 {
        return 0x0500;
    }
    *p_function & 0xFFFF
}


pub unsafe extern "system" fn hook_d3dx_create_effect_from_file_w(
    _pDevice: *mut c_void,
    pSrcFile: *const u16,
    _pDefines: *const c_void,
    _pInclude: *mut c_void,
    _Flags: u32,
    _pPool: *mut c_void,
    ppEffect: *mut *mut c_void,
    ppCompilationErrors: *mut *mut c_void,
) -> HRESULT {
    if pSrcFile.is_null() {
        return windows::core::HRESULT(-2147467261);
    }

    let file_path = PCWSTR(pSrcFile).to_string().unwrap_or_default();
    crate::log_msg(&format!(">>> INTERCEPTED D3DXCreateEffectFromFileW from MME: {}", file_path));

    let source = match std::fs::read_to_string(&file_path) {
        Ok(s) => s,
        Err(e) => {
            crate::log_msg(&format!("Failed to read effect file {}: {}", file_path, e));
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(format!("Failed to open effect file: {e}").into_bytes());
            }
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            return windows::core::HRESULT(-2147467259);
        }
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    if guard.is_none() {
        if let Ok(ctx) = D3D11Context::new(HWND(null_mut()), 0, 0) {
            *guard = Some(ctx);
        }
    }

    let d3d11 = match guard.as_ref() {
        Some(ctx) => ctx,
        None => {
            crate::log_msg("D3D11Context is None during effect creation");
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            return windows::core::HRESULT(-2147467259);
        }
    };

    match HikariEffect::new(&source, Some(&file_path), d3d11) {
        Ok(eff_ptr) => {
            crate::log_msg(&format!("Successfully compiled DX11 SM 5.0 effect for: {}", file_path));
            if !ppEffect.is_null() {
                *ppEffect = eff_ptr as *mut c_void;
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = null_mut();
            }
            S_OK
        }
        Err(err) => {
            crate::log_msg(&format!("REJECTED non-DX11 effect (legacy syntax error) for {}: {}", file_path, err));
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(err.into_bytes());
            }
            windows::core::HRESULT(-2147467259)
        }
    }
}

unsafe extern "system" fn hook_pre_d3dx_create_effect_from_file_ex_w(
    _this: *mut c_void,
    _pDevice: *mut *mut c_void,
    pSrcFile: *mut *const u16,
    _pDefines: *mut *const c_void,
    _pInclude: *mut *mut c_void,
    _pSkipConstants: *mut *const i8,
    _Flags: *mut u32,
    _pPool: *mut *mut c_void,
    _ppEffect: *mut *mut c_void,
    _ppCompilationErrors: *mut *mut c_void,
) {
    if !pSrcFile.is_null() && !(*pSrcFile).is_null() {
        let file_path = PCWSTR(*pSrcFile).to_string().unwrap_or_default();
        crate::log_msg(&format!("PRE-hook D3DXCreateEffectFromFileExW called: {}", file_path));
    }
}

unsafe extern "system" fn hook_post_d3dx_create_effect_from_file_ex_w(
    _this: *mut c_void,
    _pDevice: *mut *mut c_void,
    pSrcFile: *mut *const u16,
    _pDefines: *mut *const c_void,
    _pInclude: *mut *mut c_void,
    _pSkipConstants: *mut *const i8,
    _Flags: *mut u32,
    _pPool: *mut *mut c_void,
    ppEffect: *mut *mut c_void,
    ppCompilationErrors: *mut *mut c_void,
    res: *mut HRESULT,
) {
    if pSrcFile.is_null() || (*pSrcFile).is_null() {
        return;
    }

    let file_path = PCWSTR(*pSrcFile).to_string().unwrap_or_default();
    if file_path.is_empty() {
        return;
    }

    crate::log_msg(&format!("POST-hook D3DXCreateEffectFromFileExW called: {}", file_path));

    // Read shader file contents
    let source = match std::fs::read_to_string(&file_path) {
        Ok(s) => s,
        Err(e) => {
            crate::log_msg(&format!("Failed to read file: {e}"));
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(format!("Failed to open effect file: {e}").into_bytes());
            }
            if !res.is_null() {
                *res = windows::core::HRESULT(-2147467259);
            }
            return;
        }
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    if guard.is_none() {
        if let Ok(ctx) = D3D11Context::new(HWND(null_mut()), 0, 0) {
            *guard = Some(ctx);
        }
    }

    let d3d11 = match guard.as_ref() {
        Some(ctx) => ctx,
        None => {
            crate::log_msg("D3D11Context is None during effect creation");
            if !res.is_null() {
                *res = windows::core::HRESULT(-2147467259);
            }
            return;
        }
    };

    // Compile using Hikari DX11 native pipeline
    match HikariEffect::new(&source, Some(&file_path), d3d11) {
        Ok(eff_ptr) => {
            crate::log_msg(&format!("Successfully compiled DX11 HikariEffect for {}", file_path));
            if !ppEffect.is_null() {
                *ppEffect = eff_ptr as *mut c_void;
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = null_mut();
            }
            if !res.is_null() {
                *res = S_OK;
            }
        }
        Err(err) => {
            crate::log_msg(&format!("Failed to compile DX11 HikariEffect for {}: {}", file_path, err));
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(err.into_bytes());
            }
            if !res.is_null() {
                *res = windows::core::HRESULT(-2147467259);
            }
        }
    }
}

unsafe extern "system" fn hook_clear(
    _this: *mut c_void,
    _count: u32,
    _p_rects: *const c_void,
    _flags: u32,
    color: u32,
    z: f32,
    stencil: u32,
) {
    if let Ok(guard) = GLOBAL_D3D11.lock() {
        if let Some(ref ctx) = *guard {
            let a = ((color >> 24) & 0xFF) as f32 / 255.0;
            let r = ((color >> 16) & 0xFF) as f32 / 255.0;
            let g = ((color >> 8) & 0xFF) as f32 / 255.0;
            let b = (color & 0xFF) as f32 / 255.0;
            ctx.clear([r, g, b, a], z, stencil as u8);
        }
    }
}

unsafe extern "system" fn hook_set_texture(
    _this: *mut c_void,
    stage: u32,
    p_texture: *mut c_void,
) {
    if (stage as usize) < 8 {
        let mut guard = BOUND_TEXTURES.lock().unwrap();
        guard[stage as usize] = if p_texture.is_null() { None } else { Some(p_texture as usize) };
    }
}

unsafe extern "system" fn hook_set_stream_source(
    _this: *mut c_void,
    stream_number: u32,
    p_stream_data: *mut c_void,
    offset_in_bytes: u32,
    stride: u32,
) {
    if stream_number == 0 {
        let mut guard = BOUND_VB.lock().unwrap();
        *guard = if p_stream_data.is_null() {
            None
        } else {
            Some((p_stream_data as usize, stride, offset_in_bytes))
        };
    }
}

unsafe extern "system" fn hook_set_indices(
    _this: *mut c_void,
    p_index_data: *mut c_void,
) {
    let mut guard = BOUND_IB.lock().unwrap();
    *guard = if p_index_data.is_null() {
        None
    } else {
        Some(p_index_data as usize)
    };
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct D3DVIEWPORT9 {
    pub X: u32,
    pub Y: u32,
    pub Width: u32,
    pub Height: u32,
    pub MinZ: f32,
    pub MaxZ: f32,
}

unsafe extern "system" fn hook_set_render_target(
    _this: *mut c_void,
    render_target_index: u32,
    p_render_target: *mut c_void,
) -> HRESULT {
    if (render_target_index as usize) < 4 {
        let mut guard = ACTIVE_RENDER_TARGETS.lock().unwrap();
        guard[render_target_index as usize] = if p_render_target.is_null() {
            None
        } else {
            Some(p_render_target as usize)
        };
    }
    S_OK
}

unsafe extern "system" fn hook_set_depth_stencil_surface(
    _this: *mut c_void,
    p_new_zstencil: *mut c_void,
) -> HRESULT {
    let mut guard = ACTIVE_DEPTH_STENCIL.lock().unwrap();
    *guard = if p_new_zstencil.is_null() {
        None
    } else {
        Some(p_new_zstencil as usize)
    };
    S_OK
}

unsafe extern "system" fn hook_set_viewport(
    _this: *mut c_void,
    p_viewport: *const D3DVIEWPORT9,
) -> HRESULT {
    if !p_viewport.is_null() {
        let vp9 = *p_viewport;
        if let Ok(guard) = GLOBAL_D3D11.lock() {
            if let Some(ref ctx) = *guard {
                let vp11 = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
                    TopLeftX: vp9.X as f32,
                    TopLeftY: vp9.Y as f32,
                    Width: vp9.Width as f32,
                    Height: vp9.Height as f32,
                    MinDepth: vp9.MinZ,
                    MaxDepth: vp9.MaxZ,
                };
                unsafe {
                    ctx.context.RSSetViewports(Some(&[vp11]));
                }
            }
        }
    }
    S_OK
}

unsafe extern "system" fn hook_draw_primitive_up(
    _this: *mut c_void,
    primitive_type: u32,
    primitive_count: u32,
    p_vertex_data: *const c_void,
    stride: u32,
) -> HRESULT {
    if p_vertex_data.is_null() || primitive_count == 0 || stride == 0 {
        return S_OK;
    }

    let active_pass = {
        let guard = crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap();
        guard.clone()
    };

    let pass_info = match active_pass {
        Some(p) => p,
        None => return S_OK,
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    let ctx = match guard.as_mut() {
        Some(c) => c,
        None => return S_OK,
    };

    let vertex_count = match primitive_type {
        1 => primitive_count,
        2 => primitive_count * 2,
        3 => primitive_count + 1,
        4 => primitive_count * 3,
        5 => primitive_count + 2,
        6 => primitive_count + 2,
        _ => primitive_count * 3,
    };

    let total_bytes = (vertex_count * stride) as usize;
    let slice = unsafe { std::slice::from_raw_parts(p_vertex_data as *const u8, total_bytes) };

    let d3d11_vb = match ctx.upload_dynamic_vertex_buffer(slice) {
        Ok(b) => b,
        Err(_) => return S_OK,
    };

    let input_layout = match ctx.get_or_create_input_layout(&pass_info.vs_bytecode, stride) {
        Ok(l) => l,
        Err(_) => return S_OK,
    };

    // Bind all active textures
    {
        let textures = BOUND_TEXTURES.lock().unwrap();
        for slot in 0..8 {
            if let Some(tex_addr) = textures[slot] {
                if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(tex_addr as *mut c_void) {
                    let srvs = [Some(srv)];
                    unsafe {
                        ctx.context.PSSetShaderResources(slot as u32, Some(&srvs));
                        ctx.context.VSSetShaderResources(slot as u32, Some(&srvs));
                    }
                }
            }
        }
    }

    unsafe {
        ctx.context.IASetInputLayout(&input_layout);
        let vbs = [Some(d3d11_vb)];
        let strides = [stride];
        let offsets = [0u32];
        ctx.context.IASetVertexBuffers(0, 1, Some(vbs.as_ptr()), Some(strides.as_ptr()), Some(offsets.as_ptr()));

        let topology = match primitive_type {
            5 => windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
            _ => windows::Win32::Graphics::Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        };
        ctx.context.IASetPrimitiveTopology(topology);

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

        ctx.context.Draw(vertex_count, 0);
    }

    DX11_FRAME_RENDERED.store(true, Ordering::Release);
    S_OK
}

unsafe extern "system" fn hook_draw_primitive(
    _this: *mut c_void,
    _prim_type: u32,
    start_vertex: u32,
    prim_count: u32,
) {
    let active_pass = {
        let guard = crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap();
        guard.clone()
    };

    let pass_info = match active_pass {
        Some(p) => p,
        None => return,
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    let ctx = match guard.as_mut() {
        Some(c) => c,
        None => return,
    };

    let (vb_addr, stride, _offset) = match *BOUND_VB.lock().unwrap() {
        Some(v) => v,
        None => return,
    };

    let vb9: windows::Win32::Graphics::Direct3D9::IDirect3DVertexBuffer9 = unsafe {
        std::mem::transmute(vb_addr as *mut c_void)
    };
    let mut vb_desc = windows::Win32::Graphics::Direct3D9::D3DVERTEXBUFFER_DESC::default();
    let mut p_vb_data: *mut c_void = null_mut();
    let vb_bytes = unsafe {
        if vb9.GetDesc(&mut vb_desc).is_ok() && vb9.Lock(0, vb_desc.Size, &mut p_vb_data, 0).is_ok() {
            let slice = std::slice::from_raw_parts(p_vb_data as *const u8, vb_desc.Size as usize);
            let bytes = slice.to_vec();
            let _ = vb9.Unlock();
            std::mem::forget(vb9);
            Some(bytes)
        } else {
            std::mem::forget(vb9);
            None
        }
    };
    let vb_bytes = match vb_bytes {
        Some(b) => b,
        None => return,
    };

    let d3d11_vb = match ctx.upload_dynamic_vertex_buffer(&vb_bytes) {
        Ok(b) => b,
        Err(_) => return,
    };

    let input_layout = match ctx.get_or_create_input_layout(&pass_info.vs_bytecode, stride) {
        Ok(l) => l,
        Err(_) => return,
    };

    let tex0_opt = BOUND_TEXTURES.lock().unwrap()[0];
    if let Some(tex_addr) = tex0_opt {
        if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(tex_addr as *mut c_void) {
            let srvs = [Some(srv)];
            unsafe {
                ctx.context.PSSetShaderResources(0, Some(&srvs));
            }
        }
    }

    unsafe {
        ctx.context.IASetInputLayout(&input_layout);
        let vbs = [Some(d3d11_vb)];
        let strides = [stride];
        let offsets = [0u32];
        ctx.context.IASetVertexBuffers(0, 1, Some(vbs.as_ptr()), Some(strides.as_ptr()), Some(offsets.as_ptr()));

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

        let vp = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
            TopLeftX: 0.0,
            TopLeftY: 0.0,
            Width: ctx.width as f32,
            Height: ctx.height as f32,
            MinDepth: 0.0,
            MaxDepth: 1.0,
        };
        ctx.context.RSSetViewports(Some(&[vp]));

        let vertex_count = prim_count * 3;
        ctx.context.Draw(vertex_count, start_vertex);
    }

    DX11_FRAME_RENDERED.store(true, Ordering::Release);
}

unsafe extern "system" fn hook_draw_indexed_primitive(
    _this: *mut c_void,
    _prim_type: u32,
    base_vertex_index: i32,
    _min_vertex_index: u32,
    _num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) {
    let active_pass = {
        let guard = crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap();
        guard.clone()
    };

    static PASS_STATE_LOG: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    let pass_info = match active_pass {
        Some(p) => {
            if !PASS_STATE_LOG.swap(true, Ordering::Relaxed) {
                crate::log_msg("hook_draw_indexed_primitive: SM5 effect pass is ACTIVE - intercepting and drawing on DirectX 11");
            }
            p
        }
        None => return,
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    let ctx = match guard.as_mut() {
        Some(c) => c,
        None => return,
    };

    let (vb_addr, stride, _offset) = match *BOUND_VB.lock().unwrap() {
        Some(v) => v,
        None => return,
    };

    let ib_addr = match *BOUND_IB.lock().unwrap() {
        Some(i) => i,
        None => return,
    };

    let vb9: windows::Win32::Graphics::Direct3D9::IDirect3DVertexBuffer9 = unsafe {
        std::mem::transmute(vb_addr as *mut c_void)
    };
    let mut vb_desc = windows::Win32::Graphics::Direct3D9::D3DVERTEXBUFFER_DESC::default();
    let mut p_vb_data: *mut c_void = null_mut();
    let vb_bytes = unsafe {
        if vb9.GetDesc(&mut vb_desc).is_ok() && vb9.Lock(0, vb_desc.Size, &mut p_vb_data, 0).is_ok() {
            let slice = std::slice::from_raw_parts(p_vb_data as *const u8, vb_desc.Size as usize);
            let bytes = slice.to_vec();
            let _ = vb9.Unlock();
            std::mem::forget(vb9);
            Some(bytes)
        } else {
            std::mem::forget(vb9);
            None
        }
    };
    let vb_bytes = match vb_bytes {
        Some(b) => b,
        None => return,
    };

    let ib9: windows::Win32::Graphics::Direct3D9::IDirect3DIndexBuffer9 = unsafe {
        std::mem::transmute(ib_addr as *mut c_void)
    };
    let mut ib_desc = windows::Win32::Graphics::Direct3D9::D3DINDEXBUFFER_DESC::default();
    let mut p_ib_data: *mut c_void = null_mut();
    let (ib_bytes, is_32bit) = unsafe {
        if ib9.GetDesc(&mut ib_desc).is_ok() && ib9.Lock(0, ib_desc.Size, &mut p_ib_data, 0).is_ok() {
            let is_32 = ib_desc.Format == windows::Win32::Graphics::Direct3D9::D3DFMT_INDEX32;
            let slice = std::slice::from_raw_parts(p_ib_data as *const u8, ib_desc.Size as usize);
            let bytes = slice.to_vec();
            let _ = ib9.Unlock();
            std::mem::forget(ib9);
            (Some(bytes), is_32)
        } else {
            std::mem::forget(ib9);
            (None, false)
        }
    };
    let ib_bytes = match ib_bytes {
        Some(b) => b,
        None => return,
    };

    let d3d11_vb = match ctx.upload_dynamic_vertex_buffer(&vb_bytes) {
        Ok(b) => b,
        Err(_) => return,
    };
    let d3d11_ib = match ctx.upload_dynamic_index_buffer(&ib_bytes) {
        Ok(b) => b,
        Err(_) => return,
    };

    let input_layout = match ctx.get_or_create_input_layout(&pass_info.vs_bytecode, stride) {
        Ok(l) => l,
        Err(_) => return,
    };

    let textures = *BOUND_TEXTURES.lock().unwrap();
    for (slot, tex_opt) in textures.iter().enumerate() {
        if let Some(tex_addr) = *tex_opt {
            if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(tex_addr as *mut c_void) {
                let srvs = [Some(srv)];
                unsafe {
                    ctx.context.PSSetShaderResources(slot as u32, Some(&srvs));
                    ctx.context.VSSetShaderResources(slot as u32, Some(&srvs));
                }
            }
        }
    }

    unsafe {
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

        let vp = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
            TopLeftX: 0.0,
            TopLeftY: 0.0,
            Width: ctx.width as f32,
            Height: ctx.height as f32,
            MinDepth: 0.0,
            MaxDepth: 1.0,
        };
        ctx.context.RSSetViewports(Some(&[vp]));

        let index_count = prim_count * 3;
        ctx.context.DrawIndexed(index_count, start_index, base_vertex_index);
    }

    DX11_FRAME_RENDERED.store(true, Ordering::Release);
}

#[repr(C)]
pub struct MMDPluginDLL4Instance {
    pub vtbl: *const usize,
}

impl MMDPluginDLL4Instance {
    pub fn new() -> *mut Self {
        let vtbl = MMD_PLUGIN_DLL4_VTBL.get_or_init(|| {
            let mut v = [dummy_vfunc as *const () as usize; 250];

            // MMDPluginDLL1 overrides
            v[17] = hook_reset as *const () as usize;   // Reset
            v[18] = hook_present as *const () as usize; // Present
            v[38] = hook_set_render_target as *const () as usize; // SetRenderTarget
            v[40] = hook_set_depth_stencil_surface as *const () as usize; // SetDepthStencilSurface
            v[44] = hook_clear as *const () as usize;   // Clear
            v[48] = hook_set_viewport as *const () as usize; // SetViewport
            v[66] = hook_set_texture as *const () as usize; // SetTexture
            v[82] = hook_draw_primitive as *const () as usize; // DrawPrimitive
            v[83] = hook_draw_indexed_primitive as *const () as usize; // DrawIndexedPrimitive
            v[84] = hook_draw_primitive_up as *const () as usize; // DrawPrimitiveUP
            v[101] = hook_set_stream_source as *const () as usize; // SetStreamSource
            v[105] = hook_set_indices as *const () as usize; // SetIndices

            // MMDPluginDLL3 overrides
            v[239] = hook_get_plugin_title as *const () as usize;
            v[240] = hook_start as *const () as usize; // start()
            v[241] = hook_stop as *const () as usize;  // stop()

            // MMDPluginDLL4 overrides
            v[248] = hook_pre_d3dx_create_effect_from_file_ex_w as *const () as usize;
            v[249] = hook_post_d3dx_create_effect_from_file_ex_w as *const () as usize;

            v
        });

        let instance = Box::new(Self {
            vtbl: vtbl.as_ptr(),
        });
        Box::into_raw(instance)
    }

    pub fn destroy(ptr: *mut Self) {
        if !ptr.is_null() {
            unsafe {
                drop(Box::from_raw(ptr));
            }
        }
    }
}
