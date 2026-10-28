#![allow(non_snake_case, non_camel_case_types, dead_code, unused_variables, function_casts_as_integer)]

pub mod com;
pub mod d3d11;
pub mod effect;
pub mod mmd;
pub mod reflection;
pub mod shader;
pub mod states;
pub mod targets;
pub mod vtable;
pub mod backend;
pub mod ui;
pub mod mmd_host;

use std::ffi::c_void;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use windows::core::BOOL;
use windows::Win32::Foundation::HINSTANCE;

/// Vectored crash tracer: on each distinct access-violation RIP inside the
/// process it writes the RIP and a shallow stack (module + offset) to
/// hikari.log. MMHack raises benign handled exceptions during startup, so a
/// once-per-process record would hide the real crash later; RIP-dedup keeps
/// the log clean while still capturing the fatal fault.
static AV_RIPS: Mutex<Vec<usize>> = Mutex::new(Vec::new());

unsafe extern "system" fn av_tracer(
    info: *mut windows::Win32::System::Diagnostics::Debug::EXCEPTION_POINTERS,
) -> i32 {
    // Only log hard faults with a distinct rip; benign/handled C++ and poly
    // guard exceptions repeat constantly inside MMHack, so dedup on rip and
    // cap the output.
    let guarded = if info.is_null() {
        None
    } else {
        (*info).ExceptionRecord.as_ref()
    };
    let (rip, faulting) = if let Some(record) = guarded {
        // EXCEPTION_ACCESS_VIOLATION = 0xC0000005; MMHack's benign startup
        // faults have other codes (or repeat under the same rip), so filter
        // hard faults only and dedup on rip afterwards.
        if record.ExceptionCode.0 != 0xC0000005u32 as i32 {
            return 0;
        }
        let rip = (*info)
            .ContextRecord
            .as_ref()
            .map(|ctx| ctx.Rip as usize)
            .unwrap_or(0);
        (rip, record.ExceptionAddress as usize)
    } else {
        return 0;
    };

    let mut seen = AV_RIPS.lock().unwrap();
    if seen.contains(&rip) || seen.len() >= 64 {
        drop(seen);
        return 0; // EXCEPTION_CONTINUE_SEARCH
    }
    seen.push(rip);
    drop(seen);
    let mut frames: Vec<String> = Vec::new();
    unsafe {
        use windows::Win32::System::Diagnostics::Debug::*;
        if let Some(ctx) = (*info).ContextRecord.as_ref() {
            let mut context_copy = *ctx;
            let mut frame = STACKFRAME64::default();
            let machine = windows::Win32::System::SystemInformation::IMAGE_FILE_MACHINE_AMD64.0 as u32;
            frame.AddrPC.Offset = context_copy.Rip as u64;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context_copy.Rbp as u64;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context_copy.Rsp as u64;
            frame.AddrStack.Mode = AddrModeFlat;
            let process = windows::Win32::System::Threading::GetCurrentProcess();
            let thread = windows::Win32::System::Threading::GetCurrentThread();
            for depth in 0..20u32 {
                let ok = StackWalk64(
                    machine,
                    process,
                    thread,
                    &mut frame,
                    &mut context_copy as *mut CONTEXT as *mut c_void,
                    None,
                    None,
                    None,
                    None,
                )
                .as_bool();
                if !ok || frame.AddrPC.Offset == 0 {
                    break;
                }
                let pc = frame.AddrPC.Offset as usize;
                let entry = match module_for_address(pc) {
                    Some(module) => format!(" in {module}"),
                    None => " in <unknown>".to_string(),
                };
                frames.push(format!("  frame {depth}: {:#018X}{}", pc, entry));
            }
        }
    }

    let mut lines = vec![format!(
        "exception at {:#018X}, rip={:#018X} (access violation)",
        faulting, rip
    )];
    lines.extend(frames);

    let log_path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("hikari.log")))
        .unwrap_or_else(|| std::path::PathBuf::from("hikari.log"));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&log_path) {
        for line in &lines {
            let _ = writeln!(f, "[Hikari][AV] {}", line);
        }
        let _ = f.flush();
    }
    0
}

fn module_for_address(address: usize) -> Option<String> {
    let mut module = windows::Win32::Foundation::HMODULE::default();
    unsafe {
        windows::Win32::System::LibraryLoader::GetModuleHandleExW(
            windows::Win32::System::LibraryLoader::GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
                | windows::Win32::System::LibraryLoader::GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            windows::core::PCWSTR(address as *const u16),
            &mut module,
        )
        .ok()?;
    }
    let mut name_buf = [0u16; 260];
    let len = unsafe {
        windows::Win32::System::LibraryLoader::GetModuleFileNameW(Some(module), &mut name_buf)
    } as usize;
    let full = String::from_utf16_lossy(&name_buf[..len]);
    let name = full.rsplit(['\\', '/']).next().unwrap_or("").to_string();
    Some(format!("{name}+0x{:#X}", address.saturating_sub(module.0 as usize)))
}

/// Installs the vectored exception tracer once per process.
pub fn install_av_tracer() {
    static INSTALLED: AtomicBool = AtomicBool::new(false);
    if INSTALLED.swap(true, Ordering::Relaxed) {
        return;
    }
    unsafe {
        let _ = windows::Win32::System::Diagnostics::Debug::AddVectoredExceptionHandler(
            1,
            Some(av_tracer),
        );
    }
}

static LOG_MUTEX: Mutex<()> = Mutex::new(());
static LOG_BUFFER: Mutex<Vec<String>> = Mutex::new(Vec::new());
static SESSION_MARKED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub fn get_recent_logs() -> String {
    if let Ok(guard) = LOG_BUFFER.lock() {
        guard.join("\r\n")
    } else {
        String::new()
    }
}

pub fn clear_logs() {
    if let Ok(mut guard) = LOG_BUFFER.lock() {
        guard.clear();
    }
}

pub fn log_msg(msg: &str) {
    if let Ok(mut buf) = LOG_BUFFER.lock() {
        if buf.len() > 2000 {
            buf.drain(0..500);
        }
        buf.push(msg.to_string());
    }
    if let Ok(_guard) = LOG_MUTEX.lock() {
        let log_path = std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|dir| dir.join("hikari.log")))
            .unwrap_or_else(|| std::path::PathBuf::from("hikari.log"));

        if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(log_path) {
            if !SESSION_MARKED.swap(true, std::sync::atomic::Ordering::Relaxed) {
                let _ = writeln!(
                    f,
                    "===== Hikari session start: pid={} quad={} =====",
                    std::process::id(),
                    std::env::var("HIKARI_NATIVE_QUAD").unwrap_or_else(|_| "<unset>".into()),
                );
                let _ = f.flush();
            }
            let _ = writeln!(f, "[Hikari] {}", msg);
            let _ = f.flush();
        }
    }
}

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
        install_av_tracer();
    } else if fdw_reason == DLL_PROCESS_DETACH {
        let mut guard = vtable::GLOBAL_D3D11.lock().unwrap();
        *guard = None;
    }
    BOOL(1)
}
