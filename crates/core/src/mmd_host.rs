use std::ffi::{c_char, c_void, CStr};
use std::sync::OnceLock;
use windows::core::PCWSTR;
use windows::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};

type FnExpGetFrameTime = unsafe extern "C" fn() -> f32;
type FnExpGetPmdNum = unsafe extern "C" fn() -> i32;
type FnExpGetPmdFilename = unsafe extern "C" fn(i32) -> *const c_char;
type FnExpGetPmdOrder = unsafe extern "C" fn(i32) -> i32;
type FnExpGetPmdMatNum = unsafe extern "C" fn(i32) -> i32;
type FnExpGetPmdDisp = unsafe extern "C" fn(i32) -> bool;
type FnExpGetPmdID = unsafe extern "C" fn(i32) -> *mut c_void;

type FnExpGetAcsNum = unsafe extern "C" fn() -> i32;
type FnExpGetAcsFilename = unsafe extern "C" fn(i32) -> *const c_char;
type FnExpGetAcsOrder = unsafe extern "C" fn(i32) -> i32;
type FnExpGetAcsMatNum = unsafe extern "C" fn(i32) -> i32;
type FnExpGetAcsDisp = unsafe extern "C" fn(i32) -> bool;
type FnExpGetAcsID = unsafe extern "C" fn(i32) -> *mut c_void;

type FnExpGetCurrentObject = unsafe extern "C" fn() -> i32;
type FnExpGetCurrentMaterial = unsafe extern "C" fn() -> i32;
type FnExpGetCurrentTechnic = unsafe extern "C" fn() -> i32;
type FnExpGetEnglishMode = unsafe extern "C" fn() -> bool;

struct HostExports {
    exp_get_frame_time: Option<FnExpGetFrameTime>,
    exp_get_pmd_num: Option<FnExpGetPmdNum>,
    exp_get_pmd_filename: Option<FnExpGetPmdFilename>,
    exp_get_pmd_order: Option<FnExpGetPmdOrder>,
    exp_get_pmd_mat_num: Option<FnExpGetPmdMatNum>,
    exp_get_pmd_disp: Option<FnExpGetPmdDisp>,
    exp_get_pmd_id: Option<FnExpGetPmdID>,

    exp_get_acs_num: Option<FnExpGetAcsNum>,
    exp_get_acs_filename: Option<FnExpGetAcsFilename>,
    exp_get_acs_order: Option<FnExpGetAcsOrder>,
    exp_get_acs_mat_num: Option<FnExpGetAcsMatNum>,
    exp_get_acs_disp: Option<FnExpGetAcsDisp>,
    exp_get_acs_id: Option<FnExpGetAcsID>,

    exp_get_current_object: Option<FnExpGetCurrentObject>,
    exp_get_current_material: Option<FnExpGetCurrentMaterial>,
    exp_get_current_technic: Option<FnExpGetCurrentTechnic>,
    exp_get_english_mode: Option<FnExpGetEnglishMode>,
}

static HOST_EXPORTS: OnceLock<HostExports> = OnceLock::new();

fn get_exports() -> &'static HostExports {
    HOST_EXPORTS.get_or_init(|| {
        unsafe {
            let h_module = GetModuleHandleW(PCWSTR::null()).unwrap_or_default();
            if h_module.0.is_null() {
                return HostExports {
                    exp_get_frame_time: None,
                    exp_get_pmd_num: None,
                    exp_get_pmd_filename: None,
                    exp_get_pmd_order: None,
                    exp_get_pmd_mat_num: None,
                    exp_get_pmd_disp: None,
                    exp_get_pmd_id: None,
                    exp_get_acs_num: None,
                    exp_get_acs_filename: None,
                    exp_get_acs_order: None,
                    exp_get_acs_mat_num: None,
                    exp_get_acs_disp: None,
                    exp_get_acs_id: None,
                    exp_get_current_object: None,
                    exp_get_current_material: None,
                    exp_get_current_technic: None,
                    exp_get_english_mode: None,
                };
            }

            macro_rules! resolve {
                ($fn_type:ty, $name:literal) => {
                    GetProcAddress(h_module, windows::core::s!($name))
                        .map(|p| std::mem::transmute::<_, $fn_type>(p))
                };
            }

            let exports = HostExports {
                exp_get_frame_time: resolve!(FnExpGetFrameTime, "ExpGetFrameTime"),
                exp_get_pmd_num: resolve!(FnExpGetPmdNum, "ExpGetPmdNum"),
                exp_get_pmd_filename: resolve!(FnExpGetPmdFilename, "ExpGetPmdFilename"),
                exp_get_pmd_order: resolve!(FnExpGetPmdOrder, "ExpGetPmdOrder"),
                exp_get_pmd_mat_num: resolve!(FnExpGetPmdMatNum, "ExpGetPmdMatNum"),
                exp_get_pmd_disp: resolve!(FnExpGetPmdDisp, "ExpGetPmdDisp"),
                exp_get_pmd_id: resolve!(FnExpGetPmdID, "ExpGetPmdID"),

                exp_get_acs_num: resolve!(FnExpGetAcsNum, "ExpGetAcsNum"),
                exp_get_acs_filename: resolve!(FnExpGetAcsFilename, "ExpGetAcsFilename"),
                exp_get_acs_order: resolve!(FnExpGetAcsOrder, "ExpGetAcsOrder"),
                exp_get_acs_mat_num: resolve!(FnExpGetAcsMatNum, "ExpGetAcsMatNum"),
                exp_get_acs_disp: resolve!(FnExpGetAcsDisp, "ExpGetAcsDisp"),
                exp_get_acs_id: resolve!(FnExpGetAcsID, "ExpGetAcsID"),

                exp_get_current_object: resolve!(FnExpGetCurrentObject, "ExpGetCurrentObject"),
                exp_get_current_material: resolve!(FnExpGetCurrentMaterial, "ExpGetCurrentMaterial"),
                exp_get_current_technic: resolve!(FnExpGetCurrentTechnic, "ExpGetCurrentTechnic"),
                exp_get_english_mode: resolve!(FnExpGetEnglishMode, "ExpGetEnglishMode"),
            };

            let bound = exports.exp_get_pmd_num.is_some() && exports.exp_get_current_object.is_some();
            crate::log_msg(&format!(
                "[mmd_host] Initialized host exports from main module. MMD active: {}",
                bound
            ));

            exports
        }
    })
}

pub fn get_frame_time() -> f32 {
    get_exports().exp_get_frame_time.map_or(0.0, |f| unsafe { f() })
}

pub fn get_pmd_num() -> i32 {
    get_exports().exp_get_pmd_num.map_or(0, |f| unsafe { f() })
}

pub fn get_pmd_filename(index: i32) -> String {
    get_exports().exp_get_pmd_filename.map_or_else(String::new, |f| unsafe {
        let ptr = f(index);
        if ptr.is_null() {
            String::new()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    })
}

pub fn get_pmd_order(index: i32) -> i32 {
    get_exports().exp_get_pmd_order.map_or(0, |f| unsafe { f(index) })
}

pub fn get_pmd_mat_num(index: i32) -> i32 {
    get_exports().exp_get_pmd_mat_num.map_or(0, |f| unsafe { f(index) })
}

pub fn get_pmd_disp(index: i32) -> bool {
    get_exports().exp_get_pmd_disp.map_or(true, |f| unsafe { f(index) })
}

pub fn get_pmd_id(index: i32) -> usize {
    get_exports().exp_get_pmd_id.map_or(0, |f| unsafe { f(index) as usize })
}

pub fn get_acs_num() -> i32 {
    get_exports().exp_get_acs_num.map_or(0, |f| unsafe { f() })
}

pub fn get_acs_filename(index: i32) -> String {
    get_exports().exp_get_acs_filename.map_or_else(String::new, |f| unsafe {
        let ptr = f(index);
        if ptr.is_null() {
            String::new()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    })
}

pub fn get_acs_order(index: i32) -> i32 {
    get_exports().exp_get_acs_order.map_or(0, |f| unsafe { f(index) })
}

pub fn get_acs_mat_num(index: i32) -> i32 {
    get_exports().exp_get_acs_mat_num.map_or(0, |f| unsafe { f(index) })
}

pub fn get_acs_disp(index: i32) -> bool {
    get_exports().exp_get_acs_disp.map_or(true, |f| unsafe { f(index) })
}

pub fn get_acs_id(index: i32) -> usize {
    get_exports().exp_get_acs_id.map_or(0, |f| unsafe { f(index) as usize })
}

pub fn get_current_object() -> i32 {
    get_exports().exp_get_current_object.map_or(0, |f| unsafe { f() })
}

pub fn get_current_material() -> i32 {
    get_exports().exp_get_current_material.map_or(-1, |f| unsafe { f() })
}

pub fn get_current_technic() -> i32 {
    get_exports().exp_get_current_technic.map_or(0, |f| unsafe { f() })
}

pub fn is_english_mode() -> bool {
    get_exports().exp_get_english_mode.map_or(false, |f| unsafe { f() })
}

#[derive(Debug, Clone)]
pub struct HostObject {
    pub id: usize,
    pub is_pmd: bool,
    pub index: i32,
    pub order: i32,
    pub filename: String,
    pub mat_count: i32,
    pub disp: bool,
}

/// Enumerates all currently loaded PMD/PMX models and accessories from MMD.
pub fn enumerate_host_objects() -> Vec<HostObject> {
    let mut objects = Vec::new();

    let pmd_count = get_pmd_num();
    for i in 0..pmd_count {
        let id = get_pmd_id(i);
        if id != 0 {
            objects.push(HostObject {
                id,
                is_pmd: true,
                index: i,
                order: get_pmd_order(i),
                filename: get_pmd_filename(i),
                mat_count: get_pmd_mat_num(i),
                disp: get_pmd_disp(i),
            });
        }
    }

    let acs_count = get_acs_num();
    for i in 0..acs_count {
        let id = get_acs_id(i);
        if id != 0 {
            objects.push(HostObject {
                id,
                is_pmd: false,
                index: i,
                order: get_acs_order(i),
                filename: get_acs_filename(i),
                mat_count: get_acs_mat_num(i),
                disp: get_acs_disp(i),
            });
        }
    }

    objects
}

/// Returns true if MMD host exports were detected in the process.
pub fn is_mmd_bound() -> bool {
    let exports = get_exports();
    exports.exp_get_pmd_num.is_some() && exports.exp_get_current_object.is_some()
}

/// Returns a lightweight 64-bit fingerprint of all currently loaded PMD/PMX models and accessories.
/// Changes whenever models or accessories are loaded, unloaded, replaced, or reordered in MMD.
pub fn get_host_objects_fingerprint() -> u64 {
    let pmd_count = get_pmd_num();
    let acs_count = get_acs_num();
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    std::hash::Hash::hash(&(pmd_count, acs_count), &mut hasher);
    for i in 0..pmd_count {
        std::hash::Hash::hash(&get_pmd_id(i), &mut hasher);
        std::hash::Hash::hash(&get_pmd_order(i), &mut hasher);
        std::hash::Hash::hash(&get_pmd_disp(i), &mut hasher);
        std::hash::Hash::hash(&get_pmd_filename(i), &mut hasher);
    }
    for i in 0..acs_count {
        std::hash::Hash::hash(&get_acs_id(i), &mut hasher);
        std::hash::Hash::hash(&get_acs_order(i), &mut hasher);
        std::hash::Hash::hash(&get_acs_disp(i), &mut hasher);
        std::hash::Hash::hash(&get_acs_filename(i), &mut hasher);
    }
    std::hash::Hasher::finish(&hasher)
}
