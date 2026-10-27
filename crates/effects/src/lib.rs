#![allow(non_snake_case)]

use windows::core::BOOL;
use windows::Win32::Foundation::HINSTANCE;

#[no_mangle]
pub unsafe extern "system" fn DllMain(
    _hinst: HINSTANCE,
    _reason: u32,
    _reserved: *mut std::ffi::c_void,
) -> BOOL {
    BOOL(1)
}
