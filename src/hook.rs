//! PE Import Address Table (IAT) hooking engine for intercepting MME effect calls.

use std::ffi::{c_void, CStr};
use windows::core::PCWSTR;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;

#[repr(C)]
struct ImageDosHeader {
    e_magic: u16,
    _pad: [u16; 29],
    e_lfanew: i32,
}

#[repr(C)]
struct ImageDataDirectory {
    virtual_address: u32,
    size: u32,
}

#[repr(C)]
struct ImageImportDescriptor {
    original_first_thunk: u32,
    time_date_stamp: u32,
    forwarder_chain: u32,
    name: u32,
    first_thunk: u32,
}

#[link(name = "kernel32")]
extern "system" {
    fn VirtualProtect(
        lpAddress: *const c_void,
        dwSize: usize,
        flNewProtect: u32,
        lpflOldProtect: *mut u32,
    ) -> i32;
    fn FlushInstructionCache(
        hProcess: *mut c_void,
        lpBaseAddress: *const c_void,
        dwSize: usize,
    ) -> i32;
    fn GetCurrentProcess() -> *mut c_void;
}

const PAGE_READWRITE: u32 = 0x04;

/// Intercepts an imported function in a specified loaded module's IAT.
pub unsafe fn hook_iat(
    module_name: &str,
    target_dll: &str,
    func_name: &str,
    new_func: usize,
) -> bool {
    let wide_name: Vec<u16> = module_name.encode_utf16().chain(std::iter::once(0)).collect();
    let hmod = GetModuleHandleW(PCWSTR(wide_name.as_ptr())).unwrap_or_default();
    if hmod.0.is_null() {
        return false;
    }

    let base = hmod.0 as usize;
    let dos = &*(base as *const ImageDosHeader);
    if dos.e_magic != 0x5A4D {
        return false;
    }

    let nt_ptr = base + dos.e_lfanew as usize;
    if *(nt_ptr as *const u32) != 0x00004550 {
        return false;
    }

    // IMAGE_OPTIONAL_HEADER64 DataDirectory[1] is IMAGE_DIRECTORY_ENTRY_IMPORT
    // In PE64, OptionalHeader is at nt_ptr + 24, and DataDirectory is at offset 112 inside OptionalHeader.
    // Entry 1 (Import) is at offset 112 + 1 * 8 = 120.
    let import_dir_ptr = (nt_ptr + 24 + 120) as *const ImageDataDirectory;
    let import_rva = (*import_dir_ptr).virtual_address;
    if import_rva == 0 {
        return false;
    }

    let mut desc = (base + import_rva as usize) as *const ImageImportDescriptor;

    while (*desc).name != 0 {
        let name_ptr = (base + (*desc).name as usize) as *const i8;
        let dll_name = CStr::from_ptr(name_ptr).to_string_lossy();

        if dll_name.eq_ignore_ascii_case(target_dll) {
            let mut thunk = (base + (*desc).first_thunk as usize) as *mut usize;
            let mut orig_thunk = if (*desc).original_first_thunk != 0 {
                (base + (*desc).original_first_thunk as usize) as *const usize
            } else {
                thunk as *const usize
            };

            while *thunk != 0 {
                let val = *orig_thunk;
                // Check that it is imported by name, not ordinal (MSB is 0 in 64-bit)
                if val & (1usize << 63) == 0 {
                    // IMAGE_IMPORT_BY_NAME: u16 hint followed by ASCII function name
                    let by_name = (base + val + 2) as *const i8;
                    let imported_name = CStr::from_ptr(by_name).to_string_lossy();

                    if imported_name == func_name {
                        let mut old_protect = 0u32;
                        if VirtualProtect(thunk as *const c_void, 8, PAGE_READWRITE, &mut old_protect) != 0 {
                            *thunk = new_func;
                            VirtualProtect(thunk as *const c_void, 8, old_protect, &mut old_protect);
                            FlushInstructionCache(GetCurrentProcess(), thunk as *const c_void, 8);
                            return true;
                        }
                    }
                }

                thunk = thunk.add(1);
                orig_thunk = orig_thunk.add(1);
            }
        }

        desc = desc.add(1);
    }

    false
}
