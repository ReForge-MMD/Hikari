#![allow(
    non_snake_case, non_camel_case_types, unused_variables, dead_code,
    static_mut_refs, function_casts_as_integer, clippy::missing_safety_doc
)]

use std::ffi::c_void;
use std::sync::OnceLock;
use windows::core::{HRESULT, PCSTR, PCWSTR};
use windows::Win32::Foundation::{HMODULE, HWND, S_OK};
use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};
use windows::Win32::System::Memory::{VirtualAlloc, MEM_COMMIT, MEM_RESERVE, PAGE_EXECUTE_READWRITE};
use windows::Win32::System::SystemInformation::GetSystemDirectoryW;

fn get_system_d3d9() -> HMODULE {
    static MODULE: OnceLock<usize> = OnceLock::new();
    let addr = *MODULE.get_or_init(|| unsafe {
        let mut buf = [0u16; 320];
        let len = GetSystemDirectoryW(Some(&mut buf)) as usize;
        if len == 0 || len + 10 >= buf.len() {
            panic!("GetSystemDirectoryW failed");
        }
        let suffix: Vec<u16> = "\\d3d9.dll\0".encode_utf16().collect();
        buf[len..len + suffix.len()].copy_from_slice(&suffix);
        let hmod = LoadLibraryW(PCWSTR(buf.as_ptr())).expect("Failed to load System32\\d3d9.dll");
        hikari_core::log_msg(&format!("[d3d9] Loaded real System32\\d3d9.dll at {:p}", hmod.0));
        hmod.0 as usize
    });
    HMODULE(addr as *mut c_void)
}

unsafe fn get_real_proc(name: &[u8]) -> *const c_void {
    let hmod = get_system_d3d9();
    let proc = GetProcAddress(hmod, PCSTR(name.as_ptr()));
    proc.map(|p| p as *const c_void).unwrap_or(std::ptr::null())
}

// ---------------------------------------------------------------------------
// Standard d3d9 export forwarding
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_BeginEvent(col: u32, wsz_name: PCWSTR) -> i32 {
    type Pfn = unsafe extern "system" fn(u32, PCWSTR) -> i32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_BeginEvent\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(col, wsz_name) } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_EndEvent() -> i32 {
    type Pfn = unsafe extern "system" fn() -> i32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_EndEvent\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_GetStatus() -> u32 {
    type Pfn = unsafe extern "system" fn() -> u32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_GetStatus\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_QueryRepeatFrame() -> i32 {
    type Pfn = unsafe extern "system" fn() -> i32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_QueryRepeatFrame\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_SetMarker(col: u32, wsz_name: PCWSTR) {
    type Pfn = unsafe extern "system" fn(u32, PCWSTR);
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_SetMarker\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(col, wsz_name) }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_SetOptions(options: u32) {
    type Pfn = unsafe extern "system" fn(u32);
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_SetOptions\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(options) }
}

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_SetRegion(col: u32, wsz_name: PCWSTR) {
    type Pfn = unsafe extern "system" fn(u32, PCWSTR);
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"D3DPERF_SetRegion\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(col, wsz_name) }
}

#[no_mangle]
pub unsafe extern "system" fn DebugSetLevel(level: u32) {
    type Pfn = unsafe extern "system" fn(u32);
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"DebugSetLevel\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(level) }
}

#[no_mangle]
pub unsafe extern "system" fn DebugSetMute() -> i32 {
    type Pfn = unsafe extern "system" fn() -> i32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"DebugSetMute\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn CheckFullscreen() -> i32 {
    type Pfn = unsafe extern "system" fn() -> i32;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"CheckFullscreen\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { 0 }
}

#[no_mangle]
pub unsafe extern "system" fn Direct3DShaderValidatorCreate9() -> *mut c_void {
    type Pfn = unsafe extern "system" fn() -> *mut c_void;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"Direct3DShaderValidatorCreate9\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f() } else { std::ptr::null_mut() }
}

#[no_mangle]
pub unsafe extern "system" fn PSGPError() -> i32 { 0 }

#[no_mangle]
pub unsafe extern "system" fn PSGPSampleTexture() -> i32 { 0 }

#[no_mangle]
pub unsafe extern "system" fn Direct3DCreate9Ex(sdk_version: u32, pp: *mut *mut c_void) -> HRESULT {
    type Pfn = unsafe extern "system" fn(u32, *mut *mut c_void) -> HRESULT;
    static FUNC: OnceLock<Option<Pfn>> = OnceLock::new();
    let f = FUNC.get_or_init(|| {
        let p = get_real_proc(b"Direct3DCreate9Ex\0");
        if p.is_null() { None } else { Some(std::mem::transmute(p)) }
    });
    if let Some(f) = f { f(sdk_version, pp) } else { HRESULT(-1) }
}

// ---------------------------------------------------------------------------
// IDirect3D9 vtable hooking — safe: IDirect3D9's vtable is a shared static
// in d3d9.dll's .rdata section (not per-instance heap allocated).
// ---------------------------------------------------------------------------

static mut D3D9_ORIGINAL_VTBL: [usize; 17] = [0; 17];
static mut D3D9_HOOKED_VTBL: [usize; 17] = [0; 17];
static D3D9_HOOKED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

#[no_mangle]
pub unsafe extern "system" fn Direct3DCreate9(sdk_version: u32) -> *mut c_void {
    hikari_core::log_msg(&format!("[d3d9] Direct3DCreate9 called (SDKVersion={})", sdk_version));
    let proc = get_real_proc(b"Direct3DCreate9\0");
    if proc.is_null() {
        hikari_core::log_msg("[d3d9] ERROR: Direct3DCreate9 not found in System32\\d3d9.dll");
        return std::ptr::null_mut();
    }
    let real_create: unsafe extern "system" fn(u32) -> *mut c_void = std::mem::transmute(proc);
    let p_d3d9 = real_create(sdk_version);
    if p_d3d9.is_null() {
        hikari_core::log_msg("[d3d9] ERROR: Real Direct3DCreate9 returned null");
        return std::ptr::null_mut();
    }
    hikari_core::log_msg(&format!("[d3d9] Real IDirect3D9 instance created: {:p}", p_d3d9));
    hook_d3d9_interface(p_d3d9);
    p_d3d9
}

unsafe fn hook_d3d9_interface(p_d3d9: *mut c_void) {
    let vtbl_ptr = *(p_d3d9 as *mut *mut usize);
    if !D3D9_HOOKED.swap(true, std::sync::atomic::Ordering::SeqCst) {
        D3D9_ORIGINAL_VTBL.copy_from_slice(std::slice::from_raw_parts(vtbl_ptr, 17));
        D3D9_HOOKED_VTBL.copy_from_slice(&D3D9_ORIGINAL_VTBL);
        D3D9_HOOKED_VTBL[16] = hook_CreateDevice as usize;
    }
    *(p_d3d9 as *mut *const usize) = D3D9_HOOKED_VTBL.as_ptr();
    hikari_core::log_msg("[d3d9] IDirect3D9 vtable slot 16 (CreateDevice) hooked");
}

type PfnCreateDevice = unsafe extern "system" fn(
    this: *mut c_void,
    adapter: u32,
    device_type: u32,
    h_focus_window: HWND,
    behavior_flags: u32,
    p_presentation_parameters: *mut c_void,
    pp_returned_device_interface: *mut *mut c_void,
) -> HRESULT;

unsafe extern "system" fn hook_CreateDevice(
    this: *mut c_void,
    adapter: u32,
    device_type: u32,
    h_focus_window: HWND,
    behavior_flags: u32,
    p_presentation_parameters: *mut c_void,
    pp_returned_device_interface: *mut *mut c_void,
) -> HRESULT {
    hikari_core::log_msg(&format!(
        "[d3d9] IDirect3D9::CreateDevice called: hwnd={:?} flags=0x{:X}",
        h_focus_window.0, behavior_flags
    ));
    let orig: PfnCreateDevice = std::mem::transmute(D3D9_ORIGINAL_VTBL[16]);
    let hr = orig(
        this, adapter, device_type, h_focus_window, behavior_flags,
        p_presentation_parameters, pp_returned_device_interface,
    );
    hikari_core::log_msg(&format!("[d3d9] CreateDevice hr=0x{:08X}", hr.0 as u32));
    if hr == S_OK && !pp_returned_device_interface.is_null() && !(*pp_returned_device_interface).is_null() {
        let real_device = *pp_returned_device_interface;
        hikari_core::log_msg(&format!("[d3d9] Real IDirect3DDevice9 at {:p}", real_device));
        // Capture the real device vtable ONCE — never write into the real device object.
        ProxyDevice9::capture_real_vtbl(real_device);
        // Initialize DX11 backend with the real device pointer (before proxy substitution).
        hikari_core::backend::on_device_created(real_device, h_focus_window);
        // Return our proxy device to MMD instead of the real one.
        let proxy = ProxyDevice9::alloc(real_device);
        *pp_returned_device_interface = proxy;
        hikari_core::log_msg(&format!("[d3d9] ProxyDevice9 at {:p} returned to MMD", proxy));
        match LoadLibraryW(windows::core::w!("HikariEffects.dll")) {
            Ok(h) => hikari_core::log_msg(&format!("[d3d9] HikariEffects.dll loaded at {:p}", h.0)),
            Err(e) => hikari_core::log_msg(&format!("[d3d9] Failed to load HikariEffects.dll: {:?}", e)),
        }
    }
    hr
}

// ---------------------------------------------------------------------------
// ProxyDevice9 — COM proxy wrapping the real IDirect3DDevice9.
//
// Design constraints:
//   1. The real device object is NEVER modified.
//   2. All 119 IDirect3DDevice9 vtable slots are covered:
//      - 15 intercepted (our typed Rust stubs, correctly forward all args).
//      - 104 forwarded via per-slot assembly trampolines.
//   3. Trampolines use JMP (not CALL) to preserve the stack frame exactly.
//      MMD's stack arguments (5th parameter and beyond) remain at the same
//      RSP offsets the real D3D9 function expects. No frame is added.
//   4. Each trampoline substitutes rcx (this=proxy) with proxy->real
//      and leaves rdx/r8/r9/stack arguments completely untouched.
//
// Trampoline layout (21 bytes each, in PAGE_EXECUTE_READWRITE memory):
//   [+0]  48 8B 41 08        mov rax, [rcx+8]        ; rax = proxy->real
//   [+4]  48 89 C1           mov rcx, rax             ; this = real device
//   [+7]  FF 25 00 00 00 00  jmp qword ptr [rip+0]   ; jump via embedded ptr
//   [+13] XX XX XX XX XX XX XX XX  ; = REAL_DEV_VTBL[N] function pointer value
// ---------------------------------------------------------------------------

/// Snapshot of the original IDirect3DDevice9 vtable (read-only after init).
static mut REAL_DEV_VTBL: [usize; 119] = [0; 119];
static REAL_DEV_VTBL_INIT: std::sync::atomic::AtomicBool =
    std::sync::atomic::AtomicBool::new(false);

#[repr(C)]
struct ProxyDevice9 {
    /// First field = COM vtable pointer (what MMD treats as lpVtbl).
    vtbl: *const [usize; 119],
    /// The unmodified real IDirect3DDevice9 pointer.
    real: *mut c_void,
}

// Safety: D3D9 is used from a single render thread.
unsafe impl Send for ProxyDevice9 {}
unsafe impl Sync for ProxyDevice9 {}

impl ProxyDevice9 {
    /// Snapshot the real device vtable into REAL_DEV_VTBL.  Called once.
    unsafe fn capture_real_vtbl(real: *mut c_void) {
        if !REAL_DEV_VTBL_INIT.swap(true, std::sync::atomic::Ordering::SeqCst) {
            let orig_vtbl = *(real as *const *const usize);
            REAL_DEV_VTBL.copy_from_slice(std::slice::from_raw_parts(orig_vtbl, 119));
            hikari_core::log_msg(&format!(
                "[d3d9] Real device vtable captured from {:p}", orig_vtbl
            ));
        }
    }

    /// Build the 119-slot proxy vtable and allocate a ProxyDevice9 on the heap.
    unsafe fn alloc(real: *mut c_void) -> *mut c_void {
        static VTBL: OnceLock<[usize; 119]> = OnceLock::new();
        let vtbl = VTBL.get_or_init(|| build_proxy_vtbl());
        let proxy = Box::new(ProxyDevice9 { vtbl, real });
        Box::into_raw(proxy) as *mut c_void
    }
}

/// Dereference the real device pointer from a proxy `this`.
#[inline(always)]
unsafe fn real_dev(this: *mut c_void) -> *mut c_void {
    (*(this as *const ProxyDevice9)).real
}

// ---------------------------------------------------------------------------
// Assembly trampoline generator.
//
// For each of the 119 IDirect3DDevice9 slots, we emit a 21-byte trampoline
// into executable memory that:
//   1. Loads proxy->real into rax  (offset 8 within ProxyDevice9)
//   2. Copies rax to rcx           (replace `this` with real device)
//   3. Jumps to REAL_DEV_VTBL[N]   (embedded absolute pointer, no CALL)
//
// Because no CALL is issued (only JMP), the stack is identical to a direct
// call from MMD to the real D3D9 function.  All register and stack arguments
// (rdx, r8, r9, and any 5th/6th/... on the stack) pass through unchanged.
// ---------------------------------------------------------------------------

const TRAMP_SIZE: usize = 21;

unsafe fn generate_trampolines() -> [usize; 119] {
    let total = 119 * TRAMP_SIZE;
    let mem = VirtualAlloc(None, total, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert!(!mem.is_null(), "[d3d9] VirtualAlloc for trampolines failed");

    let base = mem as usize;
    let mut addrs = [0usize; 119];

    for n in 0..119usize {
        let t = base + n * TRAMP_SIZE;
        let code = std::slice::from_raw_parts_mut(t as *mut u8, TRAMP_SIZE);

        // mov rax, [rcx+8]   — load proxy->real (real field at offset 8)
        code[0] = 0x48; code[1] = 0x8B; code[2] = 0x41; code[3] = 0x08;
        // mov rcx, rax       — substitute `this` with real device
        code[4] = 0x48; code[5] = 0x89; code[6] = 0xC1;
        // jmp qword ptr [rip+0]  — dereference the 8-byte pointer embedded right after
        // FF 25 00 00 00 00: RIP after insn = t+7+6 = t+13; rel32=0 → reads [t+13]
        code[7] = 0xFF; code[8] = 0x25;
        code[9] = 0x00; code[10] = 0x00; code[11] = 0x00; code[12] = 0x00;
        // Embedded function pointer: REAL_DEV_VTBL[n] value (filled after capture)
        // We write it now since capture_real_vtbl is called before alloc().
        let fn_ptr: u64 = REAL_DEV_VTBL[n] as u64;
        code[13..21].copy_from_slice(&fn_ptr.to_le_bytes());

        addrs[n] = t;
    }

    extern "system" {
        fn FlushInstructionCache(
            hprocess: windows::Win32::Foundation::HANDLE,
            baseaddress: *const c_void,
            size: usize,
        ) -> windows::core::BOOL;
    }
    let _ = FlushInstructionCache(windows::Win32::System::Threading::GetCurrentProcess(), mem, total);

    addrs
}

/// Build the full 119-slot proxy vtable.
/// 15 intercepted slots → Rust stubs.
/// 104 forwarding slots → assembly trampolines (JMP, stack-transparent).
fn build_proxy_vtbl() -> [usize; 119] {
    // Generate trampolines for all 119 slots first.
    let trampolines = unsafe { generate_trampolines() };

    let mut slots = trampolines;

    // Override intercepted slots with typed Rust stubs.
    slots[0]   = proxy_QueryInterface        as usize;
    slots[1]   = proxy_AddRef                as usize;
    slots[2]   = proxy_Release               as usize;
    slots[17]  = proxy_Present               as usize;
    slots[37]  = proxy_SetRenderTarget       as usize;
    slots[41]  = proxy_BeginScene            as usize;
    slots[42]  = proxy_EndScene              as usize;
    slots[43]  = proxy_Clear                 as usize;
    slots[47]  = proxy_SetViewport           as usize;
    slots[57]  = proxy_SetRenderState        as usize;
    slots[65]  = proxy_SetTexture            as usize;
    slots[67]  = proxy_SetTextureStageState  as usize;
    slots[69]  = proxy_SetSamplerState       as usize;
    slots[81]  = proxy_DrawPrimitive         as usize;
    slots[82]  = proxy_DrawIndexedPrimitive  as usize;
    slots[83]  = proxy_DrawPrimitiveUP       as usize;
    slots[89]  = proxy_SetFVF                as usize;
    slots[92]  = proxy_SetVertexShader       as usize;
    slots[100] = proxy_SetStreamSource       as usize;
    slots[104] = proxy_SetIndices            as usize;
    slots[107] = proxy_SetPixelShader        as usize;

    slots
}

// ---------------------------------------------------------------------------
// IUnknown — explicit typed stubs (return types differ from HRESULT).
// ---------------------------------------------------------------------------

unsafe extern "system" fn proxy_QueryInterface(
    this: *mut c_void,
    riid: *const windows::core::GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, *const windows::core::GUID, *mut *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[0]);
    f(real_dev(this), riid, ppv)
}

unsafe extern "system" fn proxy_AddRef(this: *mut c_void) -> u32 {
    type F = unsafe extern "system" fn(*mut c_void) -> u32;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[1]);
    f(real_dev(this))
}

unsafe extern "system" fn proxy_Release(this: *mut c_void) -> u32 {
    type F = unsafe extern "system" fn(*mut c_void) -> u32;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[2]);
    let count = f(real_dev(this));
    if count == 0 {
        // Real device was destroyed; free the proxy wrapper.
        drop(Box::from_raw(this as *mut ProxyDevice9));
    }
    count
}

// ---------------------------------------------------------------------------
// Intercepted device methods — typed stubs that call hooks then forward.
// ---------------------------------------------------------------------------

unsafe extern "system" fn proxy_Present(
    this: *mut c_void,
    p_source_rect: *const c_void,
    p_dest_rect: *const c_void,
    h_dest_window_override: HWND,
    p_dirty_region: *const c_void,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, *const c_void, *const c_void, HWND, *const c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[17]);
    f(real_dev(this), p_source_rect, p_dest_rect, h_dest_window_override, p_dirty_region)
}

unsafe extern "system" fn proxy_SetRenderTarget(
    this: *mut c_void,
    render_target_index: u32,
    p_render_target: *mut c_void,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, u32, *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[37]);
    f(real_dev(this), render_target_index, p_render_target)
}

unsafe extern "system" fn proxy_BeginScene(this: *mut c_void) -> HRESULT {
    hikari_core::backend::on_begin_scene(real_dev(this));
    type F = unsafe extern "system" fn(*mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[41]);
    f(real_dev(this))
}

unsafe extern "system" fn proxy_EndScene(this: *mut c_void) -> HRESULT {
    hikari_core::backend::on_end_scene(real_dev(this));
    type F = unsafe extern "system" fn(*mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[42]);
    f(real_dev(this))
}

unsafe extern "system" fn proxy_Clear(
    this: *mut c_void,
    count: u32,
    p_rects: *const c_void,
    flags: u32,
    color: u32,
    z: f32,
    stencil: u32,
) -> HRESULT {
    hikari_core::backend::on_clear(flags, color, z, stencil);
    type F = unsafe extern "system" fn(*mut c_void, u32, *const c_void, u32, u32, f32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[43]);
    f(real_dev(this), count, p_rects, flags, color, z, stencil)
}

unsafe extern "system" fn proxy_SetViewport(this: *mut c_void, p_viewport: *const c_void) -> HRESULT {
    if !p_viewport.is_null() {
        let vp = *(p_viewport as *const [u32; 4]);
        hikari_core::backend::on_set_viewport(vp[0], vp[1], vp[2], vp[3]);
    }
    type F = unsafe extern "system" fn(*mut c_void, *const c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[47]);
    f(real_dev(this), p_viewport)
}

unsafe extern "system" fn proxy_SetRenderState(
    this: *mut c_void,
    state: u32,
    value: u32,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[57]);
    f(real_dev(this), state, value)
}

unsafe extern "system" fn proxy_SetTextureStageState(
    this: *mut c_void,
    stage: u32,
    state: u32,
    value: u32,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, u32, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[67]);
    if stage == 0 && (state == 1 || state == 4) {
        static TSS_LOG: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
        let n = TSS_LOG.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        if n < 30 {
            hikari_core::log_msg(&format!(
                "[SetTextureStageState #{n}] stage={stage} state={state} value={value}"
            ));
        }
    }
    f(real_dev(this), stage, state, value)
}

unsafe extern "system" fn proxy_SetSamplerState(
    this: *mut c_void,
    sampler: u32,
    state: u32,
    value: u32,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, u32, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[69]);
    f(real_dev(this), sampler, state, value)
}

static DRAW_LOG_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

unsafe fn log_d3d9_draw_state(dev: *mut c_void, call_name: &str, prim_type: u32, count: u32) {
    let n = DRAW_LOG_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    if n < 40 || n % 1000 == 0 {
        let mut ps: *mut c_void = std::ptr::null_mut();
        let mut vs: *mut c_void = std::ptr::null_mut();
        let mut tex0: *mut c_void = std::ptr::null_mut();
        let mut fvf: u32 = 0;
        let mut colorop: u32 = 0;
        let mut alphaop: u32 = 0;
        let mut blend_en: u32 = 0;

        type PfnGetShader = unsafe extern "system" fn(*mut c_void, *mut *mut c_void) -> HRESULT;
        type PfnGetTex = unsafe extern "system" fn(*mut c_void, u32, *mut *mut c_void) -> HRESULT;
        type PfnGetTSS = unsafe extern "system" fn(*mut c_void, u32, u32, *mut u32) -> HRESULT;
        type PfnGetRS = unsafe extern "system" fn(*mut c_void, u32, *mut u32) -> HRESULT;
        type PfnGetFVF = unsafe extern "system" fn(*mut c_void, *mut u32) -> HRESULT;
        type PfnRelease = unsafe extern "system" fn(*mut c_void) -> u32;

        let get_ps: PfnGetShader = std::mem::transmute(REAL_DEV_VTBL[108]);
        let get_vs: PfnGetShader = std::mem::transmute(REAL_DEV_VTBL[93]);
        let get_tex: PfnGetTex = std::mem::transmute(REAL_DEV_VTBL[64]);
        let get_tss: PfnGetTSS = std::mem::transmute(REAL_DEV_VTBL[66]);
        let get_rs: PfnGetRS = std::mem::transmute(REAL_DEV_VTBL[58]);
        let get_fvf: PfnGetFVF = std::mem::transmute(REAL_DEV_VTBL[90]);

        let _ = get_ps(dev, &mut ps);
        let _ = get_vs(dev, &mut vs);
        let _ = get_tex(dev, 0, &mut tex0);
        let _ = get_tss(dev, 0, 1 /* COLOROP */, &mut colorop);
        let _ = get_tss(dev, 0, 4 /* ALPHAOP */, &mut alphaop);
        let _ = get_rs(dev, 27 /* ALPHABLENDENABLE */, &mut blend_en);
        let _ = get_fvf(dev, &mut fvf);

        hikari_core::log_msg(&format!(
            "[{call_name} #{n}] type={prim_type} prims={count} fvf=0x{fvf:X} ps={ps:p} vs={vs:p} tex0={tex0:p} colorop={colorop} alphaop={alphaop} blend={blend_en}"
        ));

        let release = |p: *mut c_void| {
            if !p.is_null() {
                let v = *(p as *const *const usize);
                let rel: PfnRelease = std::mem::transmute(*v.add(2));
                rel(p);
            }
        };
        release(ps);
        release(vs);
        release(tex0);
    }
}

/// Ensures stage 0 colorop/alphaop are active (MODULATE) when drawing fixed-function with a texture bound.
/// In D3D9, if COLOROP is D3DTOP_DISABLE (1) on stage 0, all stage outputs (including cascade stages)
/// are completely disabled and the rasterizer emits only untextured vertex diffuse color.
unsafe fn ensure_fixed_function_stage0(dev: *mut c_void) {
    type PfnGetShader = unsafe extern "system" fn(*mut c_void, *mut *mut c_void) -> HRESULT;
    type PfnGetTex = unsafe extern "system" fn(*mut c_void, u32, *mut *mut c_void) -> HRESULT;
    type PfnGetTSS = unsafe extern "system" fn(*mut c_void, u32, u32, *mut u32) -> HRESULT;
    type PfnSetTSS = unsafe extern "system" fn(*mut c_void, u32, u32, u32) -> HRESULT;
    type PfnRelease = unsafe extern "system" fn(*mut c_void) -> u32;

    let get_ps: PfnGetShader = std::mem::transmute(REAL_DEV_VTBL[108]);
    let mut ps: *mut c_void = std::ptr::null_mut();
    let _ = get_ps(dev, &mut ps);

    if ps.is_null() {
        let get_tex: PfnGetTex = std::mem::transmute(REAL_DEV_VTBL[64]);
        let mut tex0: *mut c_void = std::ptr::null_mut();
        let _ = get_tex(dev, 0, &mut tex0);

        if !tex0.is_null() {
            let get_tss: PfnGetTSS = std::mem::transmute(REAL_DEV_VTBL[66]);
            let set_tss: PfnSetTSS = std::mem::transmute(REAL_DEV_VTBL[67]);

            let mut colorop = 0u32;
            let mut alphaop = 0u32;
            let _ = get_tss(dev, 0, 1 /* D3DTSS_COLOROP */, &mut colorop);
            let _ = get_tss(dev, 0, 4 /* D3DTSS_ALPHAOP */, &mut alphaop);

            if colorop == 1 /* D3DTOP_DISABLE */ || alphaop == 1 /* D3DTOP_DISABLE */ {
                // Restore standard fixed-function stage 0 modulation
                let _ = set_tss(dev, 0, 1 /* D3DTSS_COLOROP */, 4 /* D3DTOP_MODULATE */);
                let _ = set_tss(dev, 0, 2 /* D3DTSS_COLORARG1 */, 2 /* D3DTA_TEXTURE */);
                let _ = set_tss(dev, 0, 3 /* D3DTSS_COLORARG2 */, 0 /* D3DTA_DIFFUSE */);
                let _ = set_tss(dev, 0, 4 /* D3DTSS_ALPHAOP */, 4 /* D3DTOP_MODULATE */);
                let _ = set_tss(dev, 0, 5 /* D3DTSS_ALPHAARG1 */, 2 /* D3DTA_TEXTURE */);
                let _ = set_tss(dev, 0, 6 /* D3DTSS_ALPHAARG2 */, 0 /* D3DTA_DIFFUSE */);

                static RESTORE_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
                let n = RESTORE_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                if n < 10 {
                    hikari_core::log_msg(&format!(
                        "[FixedFunction] Restored stage 0 COLOROP/ALPHAOP from ({colorop}, {alphaop}) to MODULATE for tex0={tex0:p}"
                    ));
                }
            }

            let v = *(tex0 as *const *const usize);
            let rel: PfnRelease = std::mem::transmute(*v.add(2));
            rel(tex0);
        }
    } else {
        let v = *(ps as *const *const usize);
        let rel: PfnRelease = std::mem::transmute(*v.add(2));
        rel(ps);
    }
}

unsafe extern "system" fn proxy_SetTexture(
    this: *mut c_void,
    stage: u32,
    p_texture: *mut c_void,
) -> HRESULT {
    hikari_core::backend::on_set_texture(stage, p_texture as usize);
    let dev = real_dev(this);
    type F = unsafe extern "system" fn(*mut c_void, u32, *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[65]);
    let hr = f(dev, stage, p_texture);
    if stage == 0 && !p_texture.is_null() {
        ensure_fixed_function_stage0(dev);
    }
    static SET_TEX_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let n = SET_TEX_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    if n < 40 || hr.0 != 0 {
        hikari_core::log_msg(&format!(
            "[SetTexture #{n}] stage={stage} tex={p_texture:p} hr=0x{:08X}", hr.0 as u32
        ));
    }
    hr
}

unsafe extern "system" fn proxy_DrawPrimitive(
    this: *mut c_void,
    prim_type: u32,
    start_vertex: u32,
    prim_count: u32,
) -> HRESULT {
    let dev = real_dev(this);
    ensure_fixed_function_stage0(dev);
    log_d3d9_draw_state(dev, "DrawPrimitive", prim_type, prim_count);
    type F = unsafe extern "system" fn(*mut c_void, u32, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[81]);
    f(dev, prim_type, start_vertex, prim_count)
}

unsafe extern "system" fn proxy_DrawIndexedPrimitive(
    this: *mut c_void,
    prim_type: u32,
    base_vertex_index: i32,
    min_vertex_index: u32,
    num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) -> HRESULT {
    let dev = real_dev(this);
    if hikari_core::backend::should_render_dx11() {
        let rendered = hikari_core::backend::on_draw_indexed_primitive_dx11(
            dev, prim_type, base_vertex_index,
            min_vertex_index, num_vertices, start_index, prim_count,
        );
        if rendered { return S_OK; }
    }
    ensure_fixed_function_stage0(dev);
    log_d3d9_draw_state(dev, "DrawIndexedPrimitive_D3D9", prim_type, prim_count);
    type F = unsafe extern "system" fn(*mut c_void, u32, i32, u32, u32, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[82]);
    f(dev, prim_type, base_vertex_index, min_vertex_index, num_vertices, start_index, prim_count)
}

unsafe extern "system" fn proxy_DrawPrimitiveUP(
    this: *mut c_void,
    prim_type: u32,
    prim_count: u32,
    p_vertex_stream_zero_data: *const c_void,
    vertex_stream_zero_stride: u32,
) -> HRESULT {
    let dev = real_dev(this);
    ensure_fixed_function_stage0(dev);
    type F = unsafe extern "system" fn(*mut c_void, u32, u32, *const c_void, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[83]);
    f(dev, prim_type, prim_count, p_vertex_stream_zero_data, vertex_stream_zero_stride)
}

unsafe extern "system" fn proxy_SetFVF(
    this: *mut c_void,
    fvf: u32,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[89]);
    f(real_dev(this), fvf)
}

unsafe extern "system" fn proxy_SetVertexShader(
    this: *mut c_void,
    p_shader: *mut c_void,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[92]);
    f(real_dev(this), p_shader)
}

unsafe extern "system" fn proxy_SetStreamSource(
    this: *mut c_void,
    stream_number: u32,
    p_stream_data: *mut c_void,
    offset_in_bytes: u32,
    stride: u32,
) -> HRESULT {
    hikari_core::backend::on_set_stream_source(stream_number, p_stream_data as usize, offset_in_bytes, stride);
    type F = unsafe extern "system" fn(*mut c_void, u32, *mut c_void, u32, u32) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[100]);
    f(real_dev(this), stream_number, p_stream_data, offset_in_bytes, stride)
}

unsafe extern "system" fn proxy_SetIndices(this: *mut c_void, p_index_data: *mut c_void) -> HRESULT {
    hikari_core::backend::on_set_indices(p_index_data as usize);
    type F = unsafe extern "system" fn(*mut c_void, *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[104]);
    f(real_dev(this), p_index_data)
}

unsafe extern "system" fn proxy_SetPixelShader(
    this: *mut c_void,
    p_shader: *mut c_void,
) -> HRESULT {
    type F = unsafe extern "system" fn(*mut c_void, *mut c_void) -> HRESULT;
    let f: F = std::mem::transmute(REAL_DEV_VTBL[107]);
    f(real_dev(this), p_shader)
}

