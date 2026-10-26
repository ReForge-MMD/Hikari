#![allow(non_snake_case, non_camel_case_types, dead_code, unused_variables, function_casts_as_integer)]

pub mod com;
pub mod d3d11;
pub mod effect;
pub mod hook;
pub mod mmd;
pub mod reflection;
pub mod shader;
pub mod states;
pub mod targets;
pub mod vtable;

use std::ffi::c_void;
use std::fs::OpenOptions;
use std::io::Write;
use std::ptr::null_mut;
use std::sync::Mutex;
use windows::core::BOOL;
use windows::Win32::Foundation::HINSTANCE;
use vtable::MMDPluginDLL4Instance;

static LOG_MUTEX: Mutex<()> = Mutex::new(());

pub fn log_msg(msg: &str) {
    if let Ok(_guard) = LOG_MUTEX.lock() {
        let log_path = std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|dir| dir.join("hikari.log")))
            .unwrap_or_else(|| std::path::PathBuf::from("hikari.log"));

        if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(log_path) {
            let _ = writeln!(f, "[Hikari] {}", msg);
            let _ = f.flush();
        }
    }
}

/// MMDPlugin ABI version
#[no_mangle]
pub extern "C" fn version() -> i32 {
    log_msg("version() -> 3");
    3
}

#[no_mangle]
pub extern "C" fn create1(_device: *mut c_void) -> *mut c_void {
    log_msg("create1() called");
    vtable::ensure_mme_hooked();
    MMDPluginDLL4Instance::new() as *mut c_void
}

#[no_mangle]
pub extern "C" fn destroy1(p: *mut c_void) {
    MMDPluginDLL4Instance::destroy(p as *mut MMDPluginDLL4Instance);
}

#[no_mangle]
pub extern "C" fn create2(_device: *mut c_void) -> *mut c_void {
    MMDPluginDLL4Instance::new() as *mut c_void
}

#[no_mangle]
pub extern "C" fn destroy2(p: *mut c_void) {
    MMDPluginDLL4Instance::destroy(p as *mut MMDPluginDLL4Instance);
}

#[no_mangle]
pub extern "C" fn create3(_device: *mut c_void) -> *mut c_void {
    MMDPluginDLL4Instance::new() as *mut c_void
}

#[no_mangle]
pub extern "C" fn destroy3(p: *mut c_void) {
    MMDPluginDLL4Instance::destroy(p as *mut MMDPluginDLL4Instance);
}

#[no_mangle]
pub extern "C" fn create4(_device: *mut c_void) -> *mut c_void {
    log_msg("create4() called");
    MMDPluginDLL4Instance::new() as *mut c_void
}

#[no_mangle]
pub extern "C" fn destroy4(p: *mut c_void) {
    MMDPluginDLL4Instance::destroy(p as *mut MMDPluginDLL4Instance);
}

#[no_mangle]
pub extern "C" fn createArray3(_device: *mut c_void, out_array_size: *mut i32) -> *mut *mut c_void {
    if !out_array_size.is_null() {
        unsafe { *out_array_size = 0; }
    }
    null_mut()
}

#[no_mangle]
pub extern "C" fn destroyArray3(_p: *mut *mut c_void) {}

#[no_mangle]
pub extern "C" fn createArray4(_device: *mut c_void, out_array_size: *mut i32) -> *mut *mut c_void {
    if !out_array_size.is_null() {
        unsafe { *out_array_size = 0; }
    }
    null_mut()
}

#[no_mangle]
pub extern "C" fn destroyArray4(_p: *mut *mut c_void) {}

#[no_mangle]
#[allow(non_snake_case)]
pub extern "system" fn DllMain(
    _hinst_dll: HINSTANCE,
    fdw_reason: u32,
    _lpv_reserved: *mut c_void,
) -> BOOL {
    const DLL_PROCESS_ATTACH: u32 = 1;
    const DLL_PROCESS_DETACH: u32 = 0;
    if fdw_reason == DLL_PROCESS_ATTACH {
        vtable::ensure_mme_hooked();
    } else if fdw_reason == DLL_PROCESS_DETACH {
        let mut guard = vtable::GLOBAL_D3D11.lock().unwrap();
        *guard = None;
    }
    BOOL(1)
}
