//! MSVC x64 C++ vtable layouts and call interception for MMDPlugin.

use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, OnceLock};
use windows::core::{HRESULT, PCWSTR, Interface};
use windows::Win32::Foundation::{HINSTANCE, HWND, LPARAM, LRESULT, S_OK, WPARAM};
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::com::*;
use crate::d3d11::D3D11Context;
use crate::effect::HikariEffect;
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
pub static ACTIVE_RENDER_TARGETS: Mutex<[Option<usize>; 4]> = Mutex::new([None; 4]);
pub static ACTIVE_DEPTH_STENCIL: Mutex<Option<usize>> = Mutex::new(None);

pub struct TexPair {
    pub sys_tex: Direct3D9::IDirect3DTexture9,
    pub gpu_tex: Option<Direct3D9::IDirect3DTexture9>,
    pub format: D3DFORMAT,
    pub size: (u32, u32),
}

unsafe impl Send for TexPair {}
unsafe impl Sync for TexPair {}

pub static TEX_PAIR: Mutex<Option<TexPair>> = Mutex::new(None);

pub fn release_d3d9_resources() {
    crate::log_msg("[vtable] release_d3d9_resources: dropping D3D9 default-pool surfaces");
    *TEX_PAIR.lock().unwrap() = None;
    *BOUND_VB.lock().unwrap() = None;
    *BOUND_IB.lock().unwrap() = None;
    *BOUND_TEXTURES.lock().unwrap() = [None; 8];
    *ACTIVE_RENDER_TARGETS.lock().unwrap() = [None; 4];
    *ACTIVE_DEPTH_STENCIL.lock().unwrap() = None;
    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ref mut ctx) = *guard {
            ctx.texture_cache.clear();
        }
    }
}
static DRAW_FAILURE_LOGGED: AtomicBool = AtomicBool::new(false);
static DRAW_SUCCESS_LOGGED: AtomicBool = AtomicBool::new(false);
static DRAW_WAIT_LOGGED: AtomicBool = AtomicBool::new(false);
static PRESENT_FAILURE_LOGGED: AtomicBool = AtomicBool::new(false);
static OVERLAY_WINDOW_FAILURE_LOGGED: AtomicBool = AtomicBool::new(false);
static FRAME_CLEAR_LOGGED: AtomicBool = AtomicBool::new(false);
static MMD_PLUGIN_DLL4_VTBL: OnceLock<[usize; 250]> = OnceLock::new();
type PfnD3DXDisassembleShader = unsafe extern "system" fn(
    pShader: *const u32,
    EnableColorCode: i32,
    pComments: *const i8,
    ppDisassembly: *mut *mut c_void,
) -> HRESULT;
static D3DX_DISASSEMBLE_ORIGINAL: OnceLock<Option<PfnD3DXDisassembleShader>> = OnceLock::new();
type PfnD3DXDisassembleEffect = unsafe extern "system" fn(
    effect: *mut c_void,
    enable_color_code: i32,
    disassembly: *mut *mut c_void,
) -> HRESULT;
static D3DX_DISASSEMBLE_EFFECT_ORIGINAL: OnceLock<Option<PfnD3DXDisassembleEffect>> = OnceLock::new();

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

/// Keeps a top-level layered overlay window aligned with the MMD viewport.
/// Solid black pixels of the swap chain act as a color key, so MMD's own
/// rendered scene stays visible beneath the DX11-drawn effect content.
fn ensure_overlay_window(parent: HWND, x: i32, y: i32, w: u32, h: u32) -> HWND {
    let mut state = OVERLAY.lock().unwrap();

    static CREATED_LOGGED: AtomicBool = AtomicBool::new(false);
    if parent.0.is_null() || !unsafe { IsWindow(Some(parent)) }.as_bool() {
        crate::log_msg(&format!("Cannot create DX11 overlay: invalid parent HWND {:p}", parent.0));
        return HWND(null_mut());
    }

    // Convert the client-area dest rect to absolute screen coordinates.
    let mut points = [
        windows::Win32::Foundation::POINT { x, y },
        windows::Win32::Foundation::POINT { x: x + (w as i32).max(1), y: y + (h as i32).max(1) },
    ];
    unsafe {
        windows::Win32::Graphics::Gdi::MapWindowPoints(Some(parent), None, &mut points);
    }
    let sx = points[0].x;
    let sy = points[0].y;
    let (sw, sh) = ((points[1].x - points[0].x) as u32, (points[1].y - points[0].y) as u32);

    if state.hwnd.0.is_null() {
        let class_name = windows::core::w!("HikariDX11Overlay");
        let hinstance = unsafe {
            windows::Win32::System::LibraryLoader::GetModuleHandleW(
                windows::core::w!("hikari.dll"),
            )
        }
        .map(|module| HINSTANCE(module.0))
        .unwrap_or_default();
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(overlay_wndproc),
            hInstance: hinstance,
            lpszClassName: class_name,
            ..Default::default()
        };
        unsafe {
            let class_atom = RegisterClassExW(&wc);
            if class_atom == 0 && !OVERLAY_WINDOW_FAILURE_LOGGED.swap(true, Ordering::Relaxed) {
                crate::log_msg(&format!(
                    "RegisterClassExW for DX11 overlay failed (HINSTANCE={:p}, last_error={})",
                    hinstance.0,
                    windows::Win32::Foundation::GetLastError().0,
                ));
            }
            // Top-level layered popup: composes above the host's flip-model
            // output; mouse input passes through; focus stays with MMD.
            let ex_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            let hwnd = CreateWindowExW(
                ex_style,
                class_name,
                windows::core::w!("Hikari DX11 Overlay"),
                WS_POPUP,
                sx,
                sy,
                sw as i32,
                sh as i32,
                None,
                None,
                Some(hinstance),
                None,
            ).unwrap_or_else(|error| {
                if !OVERLAY_WINDOW_FAILURE_LOGGED.swap(true, Ordering::Relaxed) {
                    crate::log_msg(&format!(
                        "CreateWindowExW for DX11 overlay failed (HINSTANCE={:p}, HRESULT=0x{:08X}): {error}",
                        hinstance.0,
                        error.code().0 as u32,
                    ));
                }
                HWND(null_mut())
            });
            if !hwnd.0.is_null() {
                // Pure black swap-chain pixels become transparent color keys.
                let _ = SetLayeredWindowAttributes(hwnd, windows::Win32::Foundation::COLORREF(0), 255, LWA_COLORKEY);
            }
            state.hwnd = hwnd;
            state.parent = parent;
            state.x = sx;
            state.y = sy;
            state.w = sw;
            state.h = sh;
            state.visible = false;
            if !CREATED_LOGGED.swap(true, Ordering::Relaxed) {
                crate::log_msg(&format!(
                    "Overlay window created: hwnd={:p}, screen rect=({sx},{sy}) {sw}x{sh} (layered color-key)",
                    hwnd.0
                ));
            }
        }
    } else if state.x != sx || state.y != sy || state.w != sw || state.h != sh {
        unsafe {
            let _ = SetWindowPos(
                state.hwnd,
                Some(HWND_TOPMOST),
                sx,
                sy,
                sw as i32,
                sh as i32,
                SWP_NOACTIVATE,
            );
        }        state.x = sx;
        state.y = sy;
        state.w = sw;
        state.h = sh;
    }

    if !state.visible && !state.hwnd.0.is_null() {
        unsafe {
            let _ = SetWindowPos(
                state.hwnd,
                Some(HWND_TOPMOST),
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
            );
            let _ = ShowWindow(state.hwnd, SW_SHOWNOACTIVATE);
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

fn destroy_overlay_window() {
    let mut state = OVERLAY.lock().unwrap();
    if !state.hwnd.0.is_null() {
        unsafe {
            if IsWindow(Some(state.hwnd)).as_bool() {
                let _ = DestroyWindow(state.hwnd);
            }
        }
    }
    *state = OverlayState {
        hwnd: HWND(null_mut()),
        parent: HWND(null_mut()),
        x: 0,
        y: 0,
        w: 0,
        h: 0,
        visible: false,
    };
}

fn clear_frame_targets_once(ctx: &D3D11Context) {
    if !DX11_FRAME_CLEARED.swap(true, Ordering::AcqRel) {
        // Transparent black (alpha = 0.0) so untouched pixels let the native MMD scene show through
        ctx.clear([0.0, 0.0, 0.0, 0.0], 1.0, 0);
        if !FRAME_CLEAR_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg("Cleared DX11 overlay color/depth targets before the first model draw");
        }
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
    let mut guard = GLOBAL_D3D11.lock().unwrap();
    *guard = None;
    drop(guard);
    DX11_FRAME_CLEARED.store(false, Ordering::Release);
    destroy_overlay_window();
}

unsafe extern "system" fn hook_get_plugin_title(_this: *mut c_void) -> *const i8 {
    windows::core::s!("Hikari DX11 Runtime").as_ptr() as *const i8
}

unsafe extern "system" fn hook_present(
    _this: *mut c_void,
    _pSourceRect: *const c_void,
    _pDestRect: *const c_void,
    _hDestWindow: HWND,
    _pDirtyRegion: *const c_void,
) {
    // The native MMD Present composites Hikari's DX11 content: the composite
    // is copied into the D3D9 backbuffer at EndScene time (hook_end_scene),
    // so this callback stays passive; the let dispatcher forwards the real
    // Present afterwards.
    static PASSIVE_LOG: AtomicBool = AtomicBool::new(false);
    if !PASSIVE_LOG.swap(true, Ordering::Relaxed) {
        crate::log_msg("hook_present is passive: native Present path owns the output");
    }
}

fn log_present_failure_once(message: &str) {
    if !PRESENT_FAILURE_LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg(&format!("DX11 overlay: {message}"));
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
    static HOOKED_DISASM: AtomicBool = AtomicBool::new(false);
    static ATTEMPTED_HOST_DISASM: AtomicBool = AtomicBool::new(false);

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

        if !HOOKED_DISASM.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXDisassembleShader",
                hook_d3dx_disassemble_shader as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXDisassembleShader in MMEffect.dll!");
                HOOKED_DISASM.store(true, Ordering::Relaxed);
            } else {
                crate::log_msg("D3DXDisassembleShader is not imported by MMEffect.dll");
            }
        }

        if !ATTEMPTED_HOST_DISASM.load(Ordering::Relaxed) {
            let host_module = std::env::current_exe()
                .ok()
                .and_then(|path| path.file_name().map(|name| name.to_string_lossy().into_owned()));
            if let Some(host_module) = host_module {
                if crate::hook::hook_iat(
                    &host_module,
                    "d3dx9_43.dll",
                    "D3DXDisassembleShader",
                    hook_d3dx_disassemble_shader as usize,
                ) {
                    crate::log_msg(&format!(">>> Successfully hooked D3DXDisassembleShader in {host_module}!"));
                } else {
                    crate::log_msg(&format!("D3DXDisassembleShader is not imported by {host_module}"));
                }
            }
            ATTEMPTED_HOST_DISASM.store(true, Ordering::Relaxed);
        }

        if !HOOKED_DISASSEMBLE_EFFECT.load(Ordering::Relaxed) {
            if crate::hook::hook_iat(
                "MMEffect.dll",
                "d3dx9_43.dll",
                "D3DXDisassembleEffect",
                hook_d3dx_disassemble_effect as usize,
            ) {
                crate::log_msg(">>> Successfully hooked D3DXDisassembleEffect in MMEffect.dll!");
                HOOKED_DISASSEMBLE_EFFECT.store(true, Ordering::Relaxed);
            } else {
                crate::log_msg("D3DXDisassembleEffect is not imported by MMEffect.dll");
            }
        }
    }
}

static HOOKED_DISASSEMBLE_EFFECT: AtomicBool = AtomicBool::new(false);

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

        // Static reusable D3D9 textures: the system-memory staging texture the
        // DX11 model layer is copied into, and the DEFAULT-pool texture the
        // native device samples for the overlay quad.
        let rebuild = {
            let slot = TEX_PAIR.lock().unwrap();
            match slot.as_ref() {
                Some(pair) => pair.size != (w, h) || pair.format != desc.Format,
                None => true,
            }
        };
        if rebuild {
            log_wheel("create_sys_tex", format!("{}x{} fmt=0x{:X} sysmem", w, h, desc.Format.0));
            let mut sys_tex: Option<Direct3D9::IDirect3DTexture9> = None;
            Direct3D9::IDirect3DDevice9::CreateTexture(
                &device, w, h, 1, 0, desc.Format, Direct3D9::D3DPOOL_SYSTEMMEM,
                &mut sys_tex, std::ptr::null_mut(),
            ).map_err(|e| format!("CreateTexture(sys) failed: {e}"))?;
            let sys_tex = sys_tex.ok_or("CreateTexture returned none")?;
            let mut gpu_tex: Option<Direct3D9::IDirect3DTexture9> = None;
            Direct3D9::IDirect3DDevice9::CreateTexture(
                &device, w, h, 1, 0, desc.Format, Direct3D9::D3DPOOL_DEFAULT,
                &mut gpu_tex, std::ptr::null_mut(),
            ).map_err(|e| format!("CreateTexture(gpu) failed: {e}"))?;
            let gpu_tex = gpu_tex.ok_or("CreateTexture returned none")?;
            log_wheel("textures_created", format!("sys={:p} gpu={:p}", sys_tex.as_raw(), gpu_tex.as_raw()));
            *TEX_PAIR.lock().unwrap() = Some(TexPair {
                sys_tex,
                gpu_tex: Some(gpu_tex),
                format: desc.Format,
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

    // 1. Read back the DX11 model layer (transparent background, model
    //    pixels opaque). D9TEX=0 disables the whole D3D9 back-buffer interop
    //    (no CreateTexture/UpdateSurface/native quad) to isolate crash
    //    sources; only the DX11-side readback runs.
    let use_d3d9 = std::env::var("HIKARI_D9TEX").ok().as_deref() != Some("0");
    if !use_d3d9 {
        let bytes = ctx.readback_model_layer()
            .map_err(|e| format!("model layer readback failed: {e}"))?;
        log_wheel("d9tex_disabled", format!("readback ok ({} bytes, no D3D9 calls)", bytes.len()));
        return Ok(());
    }
    let bytes = ctx.readback_model_layer()
        .map_err(|e| format!("model layer readback failed: {e}"))?;
    log_wheel("model_readback", format!("{} bytes", bytes.len()));

        // 2. Copy into the D3D9 system-memory texture.
        write_system_texture(&sys_tex, w, h, &bytes)?;
        log_wheel("write_result", String::new());

        // 3. UpdateSurface into the DEFAULT-pool sampling texture.
        let sys_surface = Direct3D9::IDirect3DTexture9::GetSurfaceLevel(&sys_tex, 0)
            .map_err(|e| format!("GetSurfaceLevel(sys) failed: {e}"))?;
        let gpu_surface = Direct3D9::IDirect3DTexture9::GetSurfaceLevel(&gpu_tex, 0)
            .map_err(|e| format!("GetSurfaceLevel(gpu) failed: {e}"))?;
        Direct3D9::IDirect3DDevice9::UpdateSurface(
            &device, &sys_surface, std::ptr::null(), &gpu_surface, std::ptr::null(),
        ).map_err(|e| format!("UpdateSurface failed: {}", e.code()))?;
        log_wheel("update_surface", String::new());

        // 4. Draw the model layer over the native backbuffer with the real
        //    D3D9 device, straight alpha blending, before its own EndScene.
        //    Toggle with HIKARI_NATIVE_QUAD=0 to isolate the crash source.
        let draw_quad = std::env::var("HIKARI_NATIVE_QUAD").ok().as_deref() != Some("0");
        if draw_quad {
            draw_model_layer_d3d9(&device, &gpu_tex, w, h)
                .map_err(|e| e)?;
        }
        log_wheel("native_quad", if draw_quad { "drawn".to_string() } else { "skipped (HIKARI_NATIVE_QUAD=0)".to_string() });

        if !COMPOSITE_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg(&format!(
                "Composite drawn into native backbuffer: {}x{}", w, h
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
        D3DRS_ZENABLE, D3DRS_LIGHTING, D3DRS_CLIPPING,
        D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA,
        D3DTSS_ALPHAOP, D3DTSS_COLOROP, D3DTOP_SELECTARG1,
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
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHABLENDENABLE, 1)
        .map_err(|e| format!("SetRenderState(blend) failed: {}", e.code()))?;
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1.0 as u32).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE).ok();

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
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_ALPHABLENDENABLE, rs_blend).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_SRCBLEND, rs_src).ok();
    Direct3D9::IDirect3DDevice9::SetRenderState(device, D3DRS_DESTBLEND, rs_dst).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLOROP, ts_color).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_COLORARG1, ts_color_arg1).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAOP, ts_alpha).ok();
    Direct3D9::IDirect3DDevice9::SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, ts_alpha_arg1).ok();
    Ok(())
}

unsafe extern "system" fn hook_d3dx_disassemble_effect(
    effect: *mut c_void,
    enable_color_code: i32,
    pp_disassembly: *mut *mut c_void,
) -> HRESULT {
    if pp_disassembly.is_null() {
        return windows::core::HRESULT(-2147467261);
    }
    if crate::effect::is_hikari_effect(effect) {
        return match std::panic::catch_unwind(|| crate::effect::disassemble_hikari_effect(effect)) {
            Ok(Ok(text)) => {
                *pp_disassembly = HikariBuffer::new(text);
                S_OK
            }
            Ok(Err(error)) => {
                crate::log_msg(&format!("Hikari effect disassembly failed: {error}"));
                windows::core::HRESULT(-2147467259)
            }
            Err(_) => {
                crate::log_msg("Hikari effect disassembly panicked");
                windows::core::HRESULT(-2147467259)
            }
        };
    }
    static FOREIGN_DISASSEMBLE_LOGGED: AtomicBool = AtomicBool::new(false);
    if !FOREIGN_DISASSEMBLE_LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg(&format!("D3DXDisassembleEffect called on a foreign (non-Hikari) effect {:p}", effect));
    }

    let original = *D3DX_DISASSEMBLE_EFFECT_ORIGINAL.get_or_init(|| {
        let module = windows::Win32::System::LibraryLoader::GetModuleHandleW(windows::core::w!("d3dx9_43.dll"))
            .ok()
            .or_else(|| windows::Win32::System::LibraryLoader::LoadLibraryW(windows::core::w!("d3dx9_43.dll")).ok());
        module.and_then(|module| {
            windows::Win32::System::LibraryLoader::GetProcAddress(module, windows::core::s!("D3DXDisassembleEffect"))
                .map(|proc| std::mem::transmute::<_, PfnD3DXDisassembleEffect>(proc))
        })
    });
    match original {
        Some(disassemble) => disassemble(effect, enable_color_code, pp_disassembly),
        None => {
            static LOGGED: AtomicBool = AtomicBool::new(false);
            if !LOGGED.swap(true, Ordering::Relaxed) {
                crate::log_msg("D3DXDisassembleEffect called on a foreign effect and d3dx9_43.dll original could not be resolved -> returning E_NOTIMPL (0x80004001)");
            }
            windows::core::HRESULT(-2147467263)
        }
    }
}

unsafe extern "system" fn hook_d3dx_disassemble_shader(
    pShader: *const u32,
    EnableColorCode: i32,
    pComments: *const i8,
    ppDisassembly: *mut *mut c_void,
) -> HRESULT {
    if pShader.is_null() || ppDisassembly.is_null() {
        return windows::core::HRESULT(-2147467261);
    }

    let bytes = pShader as *const u8;
    if std::slice::from_raw_parts(bytes, 4) == b"DXBC" {
        let header = std::slice::from_raw_parts(bytes, 28);
        let bytecode_size = u32::from_le_bytes([header[24], header[25], header[26], header[27]]) as usize;
        if bytecode_size < 32 || bytecode_size > 64 * 1024 * 1024 {
            crate::log_msg(&format!("Rejected invalid DXBC size in shader disassembly: {bytecode_size}"));
            return windows::core::HRESULT(-2147467259);
        }
        let bytecode = std::slice::from_raw_parts(bytes, bytecode_size);
        return match crate::shader::disassemble_dxbc(bytecode) {
            Ok(disassembly) => {
                crate::log_msg(&format!("Disassembled DXBC shader ({} bytes)", bytecode_size));
                *ppDisassembly = HikariBuffer::new(disassembly);
                S_OK
            }
            Err(error) => {
                crate::log_msg(&format!("DXBC disassembly failed: {error}"));
                windows::core::HRESULT(-2147467259)
            }
        };
    }

    let original = *D3DX_DISASSEMBLE_ORIGINAL.get_or_init(|| {
        let module = windows::Win32::System::LibraryLoader::GetModuleHandleW(windows::core::w!("d3dx9_43.dll"))
            .ok()
            .or_else(|| windows::Win32::System::LibraryLoader::LoadLibraryW(windows::core::w!("d3dx9_43.dll")).ok());
        module.and_then(|module| {
            windows::Win32::System::LibraryLoader::GetProcAddress(module, windows::core::s!("D3DXDisassembleShader"))
                .map(|proc| std::mem::transmute::<_, PfnD3DXDisassembleShader>(proc))
        })
    });
    match original {
        Some(disassemble) => disassemble(pShader, EnableColorCode, pComments, ppDisassembly),
        None => {
            static LOGGED: AtomicBool = AtomicBool::new(false);
            if !LOGGED.swap(true, Ordering::Relaxed) {
                crate::log_msg("D3DXDisassembleShader called on a D3D9 shader and d3dx9_43.dll original could not be resolved -> returning E_NOTIMPL (0x80004001)");
            }
            windows::core::HRESULT(-2147467263)
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
        // MME validates effects against the host's D3D9 caps before handing
        // passes to Hikari's D3D11 renderer. Report its maximum accepted
        // profile here; the bytecode itself remains DXBC and is never run by
        // the D3D9 device.
        crate::log_msg("D3DXGetShaderVersion: reporting SM 3.0 for DXBC during MME D3D9 capability validation");
        return 0x0300;
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
    crate::log_msg(&format!("Read effect source ({} bytes): {}", source.len(), file_path));

    let mut guard = match GLOBAL_D3D11.lock() {
        Ok(guard) => guard,
        Err(poisoned) => {
            crate::log_msg("GLOBAL_D3D11 mutex was poisoned; recovering its D3D11 context");
            poisoned.into_inner()
        }
    };
    if guard.is_none() {
        crate::log_msg("No D3D11 context available; creating one for effect loading");
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

    crate::log_msg(&format!("Starting DX11 effect compilation: {}", file_path));
    let effect_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        HikariEffect::new(&source, Some(&file_path), d3d11)
    }));

    match effect_result {
        Ok(Ok(eff_ptr)) => {
            crate::log_msg(&format!("Successfully compiled DX11 SM 5.0 effect for: {}", file_path));
            if !ppEffect.is_null() {
                *ppEffect = eff_ptr as *mut c_void;
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = null_mut();
            }
            crate::log_msg(&format!("Returning ID3DXEffect {:p} to MME for: {}", eff_ptr, file_path));
            S_OK
        }
        Ok(Err(err)) => {
            crate::log_msg(&format!("REJECTED non-DX11 effect (legacy syntax error) for {}: {}", file_path, err));
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(err.into_bytes());
            }
            windows::core::HRESULT(-2147467259)
        }
        Err(payload) => {
            let message = payload
                .downcast_ref::<&str>()
                .copied()
                .or_else(|| payload.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("unknown Rust panic while creating effect");
            crate::log_msg(&format!("Panic while creating DX11 effect {}: {}", file_path, message));
            if !ppEffect.is_null() {
                *ppEffect = null_mut();
            }
            if !ppCompilationErrors.is_null() {
                *ppCompilationErrors = HikariBuffer::new(message.as_bytes().to_vec());
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

/// Placeholder used only in the carrier comparison above.
unsafe extern "system" fn overlay_dummy_forward_probe() {}

unsafe extern "system" fn hook_clear(
    this: *mut c_void,
    _count: u32,
    _p_rects: *const c_void,
    _flags: u32,
    color: u32,
    z: f32,
    stencil: u32,
) {
    // The hook carrier determines what `this` is: a D3D9 device/wrapper object
    // (forwarding possible) or this plugin's own callback instance.
    static CARRIER_LOGGED: AtomicBool = AtomicBool::new(false);
    if !CARRIER_LOGGED.swap(true, Ordering::Relaxed) && !this.is_null() {
        let vtbl = *(this as *const *const usize);
        let slot17 = *vtbl.add(17);
        let is_ours = slot17 == (hook_present as usize)
            || slot17 == (overlay_dummy_forward_probe as usize);
        crate::log_msg(&format!(
            "D3D9 hook carrier: this={:p}, vtbl[17](Present)={:p}, ours={}",
            this,
            slot17 as *const c_void,
            is_ours
        ));
    }
    // HIKARI_DEBUG_FILL=1 overrides the MMD clear color with opaque red so the
    // DX11 model layer can be distinguished. Otherwise the model pass target
    // must stay transparent black: alpha 0 lets the native quad blend only the
    // model pixels onto MMD's backbuffer.
    let (r, g, b, a) = if std::env::var("HIKARI_DEBUG_FILL").ok().as_deref() == Some("1") {
        (1.0f32, 0.0f32, 0.0f32, 1.0f32)
    } else {
        (0.0f32, 0.0f32, 0.0f32, 0.0f32)
    };
    if let Ok(guard) = GLOBAL_D3D11.lock() {
        if let Some(ref ctx) = *guard {
            ctx.clear([r, g, b, a], z, stencil as u8);
        }
    }
    let _ = color; // MMD's own clear color belongs to its device; Hikari keys on black
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

/// BeginScene interception (plugin slot 42, D3D9 slot 41). Observes the frame
/// start so a crash between post-composite EndScene and the next frame can be
/// localized to the exact boundary.
unsafe extern "system" fn hook_begin_scene(this: *mut c_void, not_edit_mode: u32) -> HRESULT {
    static BEGIN_CALLS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let calls = BEGIN_CALLS.fetch_add(1, Ordering::Relaxed) + 1;
    if calls <= 8 || calls % 300 == 0 {
        crate::log_msg(&format!("BeginScene observed (call #{calls}, mode={not_edit_mode})"));
    }
    let _ = this;
    // The dispatcher forwards the real BeginScene; we only observe.
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
    if ctx.render_target_view.is_none() {
        if !DRAW_WAIT_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg("Skipping initial DrawPrimitiveUP until the composite model target exists");
        }
        return S_OK;
    }
    clear_frame_targets_once(ctx);

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
        ctx.context.VSSetShader(pass_info.vs.as_ref(), None);
        ctx.context.PSSetShader(pass_info.ps.as_ref(), None);
        if let Some(ref cb) = pass_info.constant_buffer {
            let cbs = [Some(cb.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cbs));
            ctx.context.PSSetConstantBuffers(0, Some(&cbs));
        }
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
        None => match crate::effect::get_active_direct_effect_pass() {
            Some(p) => p,
            None => return,
        },
    };

    let mut guard = GLOBAL_D3D11.lock().unwrap();
    let ctx = match guard.as_mut() {
        Some(c) => c,
        None => return,
    };
    if ctx.render_target_view.is_none() {
        if !DRAW_WAIT_LOGGED.swap(true, Ordering::Relaxed) {
            crate::log_msg("Skipping initial DrawPrimitive until the composite model target exists");
        }
        return;
    }
    clear_frame_targets_once(ctx);

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
        if vb9.GetDesc(&mut vb_desc).is_ok()
            && vb_desc.Size > 0
            && vb9.Lock(0, vb_desc.Size, &mut p_vb_data, Direct3D9::D3DLOCK_READONLY as u32).is_ok()
            && !p_vb_data.is_null()
        {
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
        ctx.context.VSSetShader(pass_info.vs.as_ref(), None);
        ctx.context.PSSetShader(pass_info.ps.as_ref(), None);
        if let Some(ref cb) = pass_info.constant_buffer {
            let cbs = [Some(cb.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cbs));
            ctx.context.PSSetConstantBuffers(0, Some(&cbs));
        }
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

    // When there is no explicit MME active pass, only intercept model mesh passes:
    // Technic == 0 means non-mesh objects (bones, bone select boxes, axis guides, UI handles).
    // Material < 0 means no model subset is active.
    if active_pass.is_none() && (current_tech == 0 || current_mat < 0) {
        return false;
    }

    // Synchronize live camera and world matrices from the real D3D9 device
    let raw_dev = if !_this.is_null() {
        _this
    } else {
        resolve_d3d9_device().map(|d| d.as_raw()).unwrap_or(std::ptr::null_mut())
    };
    if !raw_dev.is_null() {
        let dev = std::mem::ManuallyDrop::new(std::mem::transmute::<*mut c_void, Direct3D9::IDirect3DDevice9>(raw_dev));
        let mut w_mat = [0.0f32; 16];
        let mut v_mat = [0.0f32; 16];
        let mut p_mat = [0.0f32; 16];

        if dev.GetTransform(Direct3D9::D3DTRANSFORMSTATETYPE(256), w_mat.as_mut_ptr() as *mut _).is_ok() {
            *crate::backend::CURRENT_WORLD_MATRIX.lock().unwrap() = w_mat;
        }
        if dev.GetTransform(Direct3D9::D3DTRANSFORMSTATETYPE(2), v_mat.as_mut_ptr() as *mut _).is_ok() {
            *crate::backend::CURRENT_VIEW_MATRIX.lock().unwrap() = v_mat;
        }
        if dev.GetTransform(Direct3D9::D3DTRANSFORMSTATETYPE(3), p_mat.as_mut_ptr() as *mut _).is_ok() {
            *crate::backend::CURRENT_PROJ_MATRIX.lock().unwrap() = p_mat;
        }

        static MATRIX_LOGGED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
        if !MATRIX_LOGGED.swap(true, Ordering::Relaxed) {
            let v = *crate::backend::CURRENT_VIEW_MATRIX.lock().unwrap();
            let p = *crate::backend::CURRENT_PROJ_MATRIX.lock().unwrap();
            crate::log_msg(&format!(
                "Captured live camera from D3D9: View._11={:.3}, Proj._11={:.3}, Proj._33={:.3}",
                v[0], p[0], p[10]
            ));
        }
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
            if let Some(p) = crate::effect::get_assigned_effect_pass(current_obj, current_mat) {
                DRAW_INDEXED_ACTIVE.fetch_add(1, Ordering::Relaxed);
                if !PASS_STATE_LOG.swap(true, Ordering::Relaxed) {
                    crate::log_msg(&format!(
                        "hook_draw_indexed_primitive: Model/Subset assigned effect is ACTIVE (obj={current_obj}, mat={current_mat}) - intercepting and drawing on DirectX 11"
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
                        "DrawIndexedPrimitive heartbeat: total={total}, SM5-active={active}, obj={current_obj}, mat={current_mat}, the rest are passed through to the D3D9/MMDX11 path"
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
    let mut vb_desc = windows::Win32::Graphics::Direct3D9::D3DVERTEXBUFFER_DESC::default();
    let mut p_vb_data: *mut c_void = null_mut();
    let vb_bytes = unsafe {
        if vb9.GetDesc(&mut vb_desc).is_ok()
            && vb_desc.Size > 0
            && vb9.Lock(0, vb_desc.Size, &mut p_vb_data, Direct3D9::D3DLOCK_READONLY as u32).is_ok()
            && !p_vb_data.is_null()
        {
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
        None => {
            log_draw_failure_once("failed to read the D3D9 vertex buffer");
            return false;
        }
    };

    let ib9: windows::Win32::Graphics::Direct3D9::IDirect3DIndexBuffer9 = unsafe {
        std::mem::transmute(ib_addr as *mut c_void)
    };
    let mut ib_desc = windows::Win32::Graphics::Direct3D9::D3DINDEXBUFFER_DESC::default();
    let mut p_ib_data: *mut c_void = null_mut();
    let (ib_bytes, is_32bit) = unsafe {
        if ib9.GetDesc(&mut ib_desc).is_ok()
            && ib_desc.Size > 0
            && ib9.Lock(0, ib_desc.Size, &mut p_ib_data, Direct3D9::D3DLOCK_READONLY as u32).is_ok()
            && !p_ib_data.is_null()
        {
            let is_32 = ib_desc.Format == Direct3D9::D3DFMT_INDEX32;
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
        None => {
            log_draw_failure_once("failed to read the D3D9 index buffer");
            return false;
        }
    };

    let d3d11_vb = match ctx.upload_dynamic_vertex_buffer(&vb_bytes) {
        Ok(b) => b,
        Err(error) => {
            log_draw_failure_once(&format!("uploading the vertex buffer failed: {error}"));
            return false;
        }
    };
    let d3d11_ib = match ctx.upload_dynamic_index_buffer(&ib_bytes) {
        Ok(b) => b,
        Err(error) => {
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
            if let Ok(srv) = ctx.upload_d3d9_texture_to_srv(tex_addr as *mut c_void) {
                let srvs = [Some(srv)];
                unsafe {
                    ctx.context.PSSetShaderResources(slot as u32, Some(&srvs));
                    ctx.context.VSSetShaderResources(slot as u32, Some(&srvs));
                }
            }
        }
    }

    let index_count = prim_count * 3;
    unsafe {
        ctx.context.VSSetShader(pass_info.vs.as_ref(), None);
        ctx.context.PSSetShader(pass_info.ps.as_ref(), None);
        if let Some(ref cb) = pass_info.constant_buffer {
            let cbs = [Some(cb.clone())];
            ctx.context.VSSetConstantBuffers(0, Some(&cbs));
            ctx.context.PSSetConstantBuffers(0, Some(&cbs));
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

        let vp = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
            TopLeftX: 0.0,
            TopLeftY: 0.0,
            Width: ctx.width as f32,
            Height: ctx.height as f32,
            MinDepth: 0.0,
            MaxDepth: 1.0,
        };
        ctx.context.RSSetViewports(Some(&[vp]));

        ctx.context.DrawIndexed(index_count, start_index, base_vertex_index);
    }

    if !DRAW_SUCCESS_LOGGED.swap(true, Ordering::Relaxed) {
        crate::log_msg(&format!(
            "D3D11 indexed shader draw submitted: prim_type={prim_type}, base_vertex={base_vertex_index}, min_vertex={min_vertex_index}, num_vertices={num_vertices}, start_index={start_index}, prim_count={prim_count}, stride={stride}, vertices={}, indices={index_count}, VB_bytes={}, IB_bytes={}, IB_32bit={is_32bit}, VS={}, PS={}",
            vb_bytes.len() / stride.max(1) as usize,
            vb_bytes.len(),
            ib_bytes.len(),
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
            // MMD D3D9 device events (slot = device vtable index + 1)
            v[42] = hook_begin_scene as *const () as usize; // BeginScene
            v[43] = hook_end_scene as *const () as usize;   // EndScene

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
