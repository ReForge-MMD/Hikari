//! Native Windows Effect Management GUI and MMD integration for Hikari.
//!
//! Zero-overhead pure Win32 UI (comctl32 / user32) matching MMEffect (MikuMikuEffect)
//! dialog specifications and control layout with top-right menu injection (MFT_RIGHTJUSTIFY),
//! multi-tab offscreen render target support, and live HLSL parameter tuning.

use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

use windows::core::{w, PCWSTR, PWSTR};
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::*;
use windows::Win32::UI::Controls::Dialogs::*;
use windows::Win32::UI::Controls::*;
use windows::Win32::UI::Input::KeyboardAndMouse::{EnableWindow, GetKeyState};
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::effect::{HikariEffect, UIWidgetKind, ASSIGNMENT_REGISTRY};

const WM_REFRESH_HOST_OBJECTS: u32 = WM_USER + 102;
static LAST_DIALOG_FINGERPRINT: AtomicU64 = AtomicU64::new(0);

// Injected MMEffect Top Menu items (MikuMikuDance main menu bar)
pub const ID_MENU_ROOT: u32 = 0x4800;
pub const ID_MME_EFFECT_MAPPING: u32 = 40005;
pub const ID_MME_ENABLE_EFFECT: u32 = 40020;
pub const ID_MME_AUTO_RELOAD: u32 = 40001;
pub const ID_MME_AUTO_SAVE_MAPPING: u32 = 40022;
pub const ID_MME_RELOAD_ALL: u32 = 40004;
pub const ID_MME_LOG_WINDOW: u32 = 40002;
pub const ID_MME_ABOUT: u32 = 40003;

// Map Effect File Dialog Menu: File(F)
pub const ID_FILE_OPEN: u32 = 40009;
pub const ID_FILE_SAVE: u32 = 40010;
pub const ID_FILE_OPEN_MODEL: u32 = 40016;
pub const ID_FILE_SAVE_MODEL: u32 = 40017;
pub const ID_FILE_CLOSE: u32 = 40011;

// Map Effect File Dialog Menu: Edit(E)
pub const ID_EDIT_SET_EFFECT: u32 = 40007;
pub const ID_EDIT_REMOVE_EFFECT: u32 = 40012;
pub const ID_EDIT_HIDE_SHOW: u32 = 40013;
pub const ID_EDIT_RESET_DEFAULT: u32 = 40008;
pub const ID_EDIT_SUBSET_EXTRACT: u32 = 40006;
pub const ID_EDIT_SELECT_ALL: u32 = 40025;
pub const ID_EDIT_CLEAR_SEL: u32 = 40026;

// Map Effect File Dialog Menu: Tool(T)
pub const ID_TOOL_DISASSEMBLE: u32 = 40014;
pub const ID_TOOL_SKIP_VALIDATION: u32 = 40028;
pub const ID_TOOL_ADD_TAB: u32 = 40029;
pub const ID_TOOL_TOGGLE_PARAMS: u32 = 40030;

// Dialog Control IDs (matching MME resource IDs)
pub const ID_TAB_CONTROL: u32 = 1002;
pub const ID_LISTVIEW: u32 = 1003;
pub const ID_CAPTION_LABEL: u32 = 1004;
pub const ID_APPLY_BTN: u32 = 1005;
pub const ID_GROUP_SELECTED: u32 = 1006;
pub const ID_SET_EFFECT_BTN: u32 = 1007;
pub const ID_REMOVE_EFFECT_BTN: u32 = 1008;
pub const ID_HIDE_SHOW_BTN: u32 = 1011;
pub const ID_OK_BTN: u32 = 1;
pub const ID_CANCEL_BTN: u32 = 2;
pub const ID_STATUS_LABEL: u32 = 1012;

const PARAM_CONTROL_ID_BASE: u32 = 2000;

// Trackbar message constants
const TBM_GETPOS: u32 = 0x0400;
const TBM_SETPOS: u32 = 0x0405;
const TBM_SETRANGE: u32 = 0x0406;

// ListView constants
const LVM_FIRST: u32 = 0x1000;
const LVM_DELETEALLITEMS: u32 = LVM_FIRST + 9;
const LVM_GETNEXTITEM: u32 = LVM_FIRST + 12;
const LVM_SETITEMSTATE: u32 = LVM_FIRST + 43;
const LVM_GETITEMSTATE: u32 = LVM_FIRST + 44;
const LVM_SETEXTENDEDLISTVIEWSTYLE: u32 = LVM_FIRST + 54;
const LVM_SETITEMW: u32 = LVM_FIRST + 76;
const LVM_INSERTITEMW: u32 = LVM_FIRST + 77;
const LVM_INSERTCOLUMNW: u32 = LVM_FIRST + 97;

const LVS_REPORT: u32 = 0x0001;
const LVS_SHOWSELALWAYS: u32 = 0x0008;
const LVS_EX_FULLROWSELECT: u32 = 0x00000020;
const LVS_EX_GRIDLINES: u32 = 0x00000001;
const LVS_EX_CHECKBOXES: u32 = 0x00000004;

const LVCF_TEXT: u32 = 0x0004;
const LVCF_WIDTH: u32 = 0x0002;
const LVCF_SUBITEM: u32 = 0x0008;

const LVIF_TEXT: u32 = 0x0001;
const LVIF_PARAM: u32 = 0x0004;
const LVIF_STATE: u32 = 0x0008;

const LVIS_SELECTED: u32 = 0x0002;
const LVIS_STATEIMAGEMASK: u32 = 0xF000;

const LVNI_SELECTED: u32 = 0x0002;

const NM_CLICK: u32 = 0u32.wrapping_sub(2);
const NM_DBLCLK: u32 = 0u32.wrapping_sub(3);
const LVN_ITEMCHANGED: u32 = 0u32.wrapping_sub(101);

// Tab Control constants
const TCM_FIRST: u32 = 0x1300;
const TCM_DELETEALLITEMS: u32 = TCM_FIRST + 9;
const TCM_GETCURSEL: u32 = TCM_FIRST + 11;
const TCM_SETCURSEL: u32 = TCM_FIRST + 12;
const TCM_INSERTITEMW: u32 = TCM_FIRST + 62;

const TCIF_TEXT: u32 = 0x0001;
const TCN_SELCHANGE: u32 = 0u32.wrapping_sub(551);

#[repr(C)]
struct TCITEMW {
    mask: u32,
    dw_state: u32,
    dw_state_mask: u32,
    psz_text: *mut u16,
    cch_text_max: i32,
    i_image: i32,
    l_param: isize,
}

#[repr(C)]
struct LVCOLUMNW {
    mask: u32,
    fmt: i32,
    cx: i32,
    psz_text: *mut u16,
    cch_text_max: i32,
    i_sub_item: i32,
    i_image: i32,
    i_order: i32,
    cx_min: i32,
    cx_default: i32,
    cx_ideal: i32,
}

#[repr(C)]
struct LVITEMW {
    mask: u32,
    i_item: i32,
    i_sub_item: i32,
    state: u32,
    state_mask: u32,
    psz_text: *mut u16,
    cch_text_max: i32,
    i_image: i32,
    l_param: isize,
    i_indent: i32,
    i_group_id: i32,
    c_columns: u32,
    pu_columns: *mut u32,
    pi_col_fmt: *mut i32,
    i_group: i32,
}

#[repr(C)]
struct NMHDR {
    hwnd_from: HWND,
    id_from: usize,
    code: u32,
}

#[repr(C)]
struct NMLISTVIEW {
    hdr: NMHDR,
    i_item: i32,
    i_sub_item: i32,
    u_new_state: u32,
    u_old_state: u32,
    u_changed: u32,
    l_param: isize,
}

static IS_REFRESHING: AtomicBool = AtomicBool::new(false);
static MENU_INJECTED: AtomicBool = AtomicBool::new(false);
static MMD_SUBMENU: Mutex<Option<usize>> = Mutex::new(None);
static ORIGINAL_WNDPROC: Mutex<Option<usize>> = Mutex::new(None);
static MANAGER_HWND: Mutex<Option<usize>> = Mutex::new(None);
static LOG_WINDOW_HWND: Mutex<Option<usize>> = Mutex::new(None);
static PARAM_CONTAINER_HWND: Mutex<Option<usize>> = Mutex::new(None);
static SHOW_PARAMS_PANEL: AtomicBool = AtomicBool::new(true);
static IS_DIRTY: AtomicBool = AtomicBool::new(false);

fn set_dirty_state(parent: HWND, dirty: bool) {
    IS_DIRTY.store(dirty, Ordering::Release);
    if !parent.0.is_null() {
        if let Ok(apply_btn) = unsafe { GetDlgItem(Some(parent), ID_APPLY_BTN as i32) } {
            unsafe {
                let _ = EnableWindow(apply_btn, dirty);
            }
        }
    }
}

fn sync_selection_controls(hwnd: HWND, listview: HWND) {
    if hwnd.0.is_null() || listview.0.is_null() {
        return;
    }
    let sel = unsafe { send_msg(listview, LVM_GETNEXTITEM, usize::MAX, LVNI_SELECTED as isize) };
    let has_sel = sel >= 0;
    let is_default = sel == 0;

    let is_model_row = if has_sel && !is_default {
        let rows = UI_ROWS.lock().unwrap();
        rows.get(sel as usize).map(|r| r.subset == -1).unwrap_or(false)
    } else {
        false
    };

    let enable_set = has_sel;
    let enable_rem = has_sel;
    let enable_hide_show = has_sel && !is_default;
    let enable_extract = is_model_row;

    unsafe {
        if let Ok(btn) = GetDlgItem(Some(hwnd), ID_SET_EFFECT_BTN as i32) {
            let _ = EnableWindow(btn, enable_set);
        }
        if let Ok(btn) = GetDlgItem(Some(hwnd), ID_REMOVE_EFFECT_BTN as i32) {
            let _ = EnableWindow(btn, enable_rem);
        }
        if let Ok(btn) = GetDlgItem(Some(hwnd), ID_HIDE_SHOW_BTN as i32) {
            let _ = EnableWindow(btn, enable_hide_show);
        }

        let hmenu = GetMenu(hwnd);
        if !hmenu.0.is_null() {
            let _ = EnableMenuItem(hmenu, ID_EDIT_SET_EFFECT, if enable_set { MF_ENABLED } else { MF_GRAYED });
            let _ = EnableMenuItem(hmenu, ID_EDIT_REMOVE_EFFECT, if enable_rem { MF_ENABLED } else { MF_GRAYED });
            let _ = EnableMenuItem(hmenu, ID_EDIT_HIDE_SHOW, if enable_hide_show { MF_ENABLED } else { MF_GRAYED });
            let _ = EnableMenuItem(hmenu, ID_EDIT_SUBSET_EXTRACT, if enable_extract { MF_ENABLED } else { MF_GRAYED });
        }
    }
}

static UI_FONT: Mutex<Option<usize>> = Mutex::new(None);
static BOLD_FONT: Mutex<Option<usize>> = Mutex::new(None);
static MONO_FONT: Mutex<Option<usize>> = Mutex::new(None);

#[derive(Clone, Debug)]
pub struct UiRowRef {
    pub object_id: Option<usize>, // None for (default) row
    pub subset: i32,              // -1 for whole model, >=0 for material subset
    pub filename: String,
    pub effect_path: String,
    pub shown: bool,
}

static UI_ROWS: Mutex<Vec<UiRowRef>> = Mutex::new(Vec::new());
static SELECTED_ROW: Mutex<Option<usize>> = Mutex::new(None);
static ACTIVE_TARGET_EFFECT: Mutex<Option<usize>> = Mutex::new(None); // *mut HikariEffect as usize

#[derive(Clone, Debug)]
struct ActiveParamControl {
    param_idx: usize,
    widget: UIWidgetKind,
    trackbar_hwnd: HWND,
    edit_hwnd: HWND,
    checkbox_hwnd: HWND,
    color_btn_hwnd: HWND,
    color_label_hwnd: HWND,
    combo_hwnd: HWND,
    min_val: f32,
    max_val: f32,
    step: f32,
}

unsafe impl Send for ActiveParamControl {}
unsafe impl Sync for ActiveParamControl {}

static ACTIVE_PARAM_CONTROLS: Mutex<Vec<ActiveParamControl>> = Mutex::new(Vec::new());

#[inline]
unsafe fn send_msg(hwnd: HWND, msg: u32, wparam: usize, lparam: isize) -> isize {
    SendMessageW(hwnd, msg, Some(WPARAM(wparam)), Some(LPARAM(lparam))).0
}

#[inline]
unsafe fn make_child(
    ex_style: WINDOW_EX_STYLE,
    class_name: PCWSTR,
    window_name: PCWSTR,
    style: WINDOW_STYLE,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    parent: HWND,
    id: Option<u32>,
) -> HWND {
    let menu = id.map(|i| HMENU(i as usize as *mut c_void));
    CreateWindowExW(
        ex_style,
        class_name,
        window_name,
        style | WS_CHILD | WS_VISIBLE,
        x,
        y,
        w,
        h,
        Some(parent),
        menu,
        None,
        None,
    ).unwrap_or_default()
}

/// Ensures the MMEffect menu item is injected into MMD's top menu bar (right-justified).
pub fn ensure_ui_injected() {
    if MENU_INJECTED.load(Ordering::SeqCst) {
        return;
    }
    let hwnd_opt = *crate::backend::GLOBAL_HWND.lock().unwrap();
    if let Some(h) = hwnd_opt {
        init_ui(HWND(h as *mut c_void));
    }
}

/// Synchronizes checkmarks for MMEffect menu items.
pub fn sync_menu_checkmarks() {
    if let Some(m) = *MMD_SUBMENU.lock().unwrap() {
        let menu = HMENU(m as *mut c_void);
        let eff_en = crate::effect::EFFECTS_ENABLED.load(Ordering::Acquire);
        let auto_rel = crate::effect::AUTO_RELOAD.load(Ordering::Acquire);
        let auto_sav = crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire);

        unsafe {
            let eff_flag = (MF_BYCOMMAND.0 | if eff_en { MF_CHECKED.0 } else { MF_UNCHECKED.0 }) as u32;
            let auto_rel_flag = (MF_BYCOMMAND.0 | if auto_rel { MF_CHECKED.0 } else { MF_UNCHECKED.0 }) as u32;
            let auto_sav_flag = (MF_BYCOMMAND.0 | if auto_sav { MF_CHECKED.0 } else { MF_UNCHECKED.0 }) as u32;

            let _ = CheckMenuItem(menu, ID_MME_ENABLE_EFFECT, eff_flag);
            let _ = CheckMenuItem(menu, 40006, eff_flag);
            let _ = CheckMenuItem(menu, ID_MME_AUTO_RELOAD, auto_rel_flag);
            let _ = CheckMenuItem(menu, ID_MME_AUTO_SAVE_MAPPING, auto_sav_flag);
        }
    }
}

/// Initializes native UI hooks into MMD's top-level menu bar and window procedure.
pub fn init_ui(hwnd: HWND) {
    if hwnd.0.is_null() {
        return;
    }

    let root_hwnd = unsafe { GetAncestor(hwnd, GA_ROOT) };
    let main_hwnd = if !root_hwnd.0.is_null() { root_hwnd } else { hwnd };

    unsafe {
        let mut icex = INITCOMMONCONTROLSEX {
            dwSize: std::mem::size_of::<INITCOMMONCONTROLSEX>() as u32,
            dwICC: ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES,
        };
        let _ = InitCommonControlsEx(&mut icex);

        let menu = GetMenu(main_hwnd);
        if menu.0.is_null() {
            return; // Will retry in EndScene
        }

        // Subclass MMD window proc if needed
        if ORIGINAL_WNDPROC.lock().unwrap().is_none() {
            let prev_wndproc = SetWindowLongPtrW(main_hwnd, GWLP_WNDPROC, mmd_subclass_wndproc as usize as isize);
            if prev_wndproc != 0 {
                *ORIGINAL_WNDPROC.lock().unwrap() = Some(prev_wndproc as usize);
                crate::log_msg("[UI] Subclassed MMD window procedure for F10/F5/Ctrl+Shift+E hotkeys");
            }
        }

        // Build popup submenu matching MME English menu specification
        let submenu = CreatePopupMenu().unwrap_or_default();
        if submenu.0.is_null() {
            return;
        }

        let _ = AppendMenuW(submenu, MF_STRING, ID_MME_EFFECT_MAPPING as usize, w!("Effect Mapping(M)\tF10"));
        let _ = AppendMenuW(submenu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(submenu, MF_STRING | MF_CHECKED, ID_MME_ENABLE_EFFECT as usize, w!("Enable Effect(E)\tCtrl+Shift+E"));
        let _ = AppendMenuW(submenu, MF_STRING | MF_CHECKED, ID_MME_AUTO_RELOAD as usize, w!("Auto Reload(O)"));
        let _ = AppendMenuW(submenu, MF_STRING | MF_CHECKED, ID_MME_AUTO_SAVE_MAPPING as usize, w!("Auto Save Mapping File(S)"));
        let _ = AppendMenuW(submenu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(submenu, MF_STRING, ID_MME_RELOAD_ALL as usize, w!("Reload All(R)\tF5"));
        let _ = AppendMenuW(submenu, MF_STRING, ID_MME_LOG_WINDOW as usize, w!("Log Window(L)"));
        let _ = AppendMenuW(submenu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(submenu, MF_STRING, ID_MME_ABOUT as usize, w!("About Hikari Effects(A)"));

        *MMD_SUBMENU.lock().unwrap() = Some(submenu.0 as usize);

        // Insert right-justified menu item (MFT_RIGHTJUSTIFY = 0x4000)
        let title_utf16: Vec<u16> = "Hikari Effects\0".encode_utf16().collect();
        let mut info = MENUITEMINFOW::default();
        info.cbSize = std::mem::size_of::<MENUITEMINFOW>() as u32;
        info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
        info.fType = MENU_ITEM_TYPE(0x4000); // MFT_RIGHTJUSTIFY
        info.wID = ID_MENU_ROOT;
        info.dwTypeData = PWSTR(title_utf16.as_ptr() as *mut _);
        info.cch = (title_utf16.len() - 1) as u32;
        info.hSubMenu = submenu;

        if InsertMenuItemW(menu, 100, true, &info).is_ok() {
            MENU_INJECTED.store(true, Ordering::SeqCst);
            let _ = DrawMenuBar(main_hwnd);
            sync_menu_checkmarks();
            crate::log_msg("[UI] Injected 'Hikari Effects' menu item to top-right corner of MMD menu bar (MFT_RIGHTJUSTIFY)");
        }
    }
}

/// Subclassed WndProc for MMD's root window to intercept MMEffect shortcuts & menu commands.
unsafe extern "system" fn mmd_subclass_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    let handled = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        match msg {
            WM_COMMAND => {
                let cmd_id = (wparam.0 & 0xFFFF) as u32;
                match cmd_id {
                    ID_MME_EFFECT_MAPPING => {
                        toggle_effects_manager(hwnd);
                        return Some(LRESULT(0));
                    }
                    ID_MME_ENABLE_EFFECT | 40006 => {
                        let cur = crate::effect::EFFECTS_ENABLED.load(Ordering::Acquire);
                        crate::effect::EFFECTS_ENABLED.store(!cur, Ordering::Release);
                        sync_menu_checkmarks();
                        crate::log_msg(&format!("[UI] Effects Enabled: {}", !cur));
                        return Some(LRESULT(0));
                    }
                    ID_MME_AUTO_RELOAD => {
                        let cur = crate::effect::AUTO_RELOAD.load(Ordering::Acquire);
                        crate::effect::AUTO_RELOAD.store(!cur, Ordering::Release);
                        sync_menu_checkmarks();
                        crate::log_msg(&format!("[UI] Auto Reload: {}", !cur));
                        return Some(LRESULT(0));
                    }
                    ID_MME_AUTO_SAVE_MAPPING => {
                        let cur = crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire);
                        crate::effect::AUTO_SAVE_MAPPING.store(!cur, Ordering::Release);
                        sync_menu_checkmarks();
                        crate::log_msg(&format!("[UI] Auto Save Mapping: {}", !cur));
                        return Some(LRESULT(0));
                    }
                    ID_MME_RELOAD_ALL => {
                        let _ = ASSIGNMENT_REGISTRY.lock().unwrap().reload_all_effects();
                        if let Some(h) = *MANAGER_HWND.lock().unwrap() {
                            let parent = HWND(h as *mut c_void);
                            if let Ok(lv) = GetDlgItem(Some(parent), ID_LISTVIEW as i32) {
                                refresh_assignment_list(lv);
                            }
                        }
                        return Some(LRESULT(0));
                    }
                    ID_MME_LOG_WINDOW => {
                        show_log_window(hwnd);
                        return Some(LRESULT(0));
                    }
                    ID_MME_ABOUT => {
                        MessageBoxW(
                            Some(hwnd),
                            w!("Hikari DirectX 11 Pipeline & Shader Runtime\nVersion 0.1.0\n\nHigh-performance SM5 DX11 renderer and effect management for MikuMikuDance."),
                            w!("About Hikari Effects"),
                            MB_OK | MB_ICONINFORMATION,
                        );
                        return Some(LRESULT(0));
                    }
                    _ => {}
                }
            }
            WM_KEYDOWN => {
                let vk = (wparam.0 & 0xFF) as u32;
                let ctrl_down = (GetKeyState(0x11 /*VK_CONTROL*/) as u16 & 0x8000) != 0;
                let shift_down = (GetKeyState(0x10 /*VK_SHIFT*/) as u16 & 0x8000) != 0;

                if vk == 0x79 { // VK_F10
                    toggle_effects_manager(hwnd);
                    return Some(LRESULT(0));
                } else if vk == 0x74 { // VK_F5
                    let _ = ASSIGNMENT_REGISTRY.lock().unwrap().reload_all_effects();
                    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
                        let parent = HWND(h as *mut c_void);
                        if let Ok(lv) = GetDlgItem(Some(parent), ID_LISTVIEW as i32) {
                            refresh_assignment_list(lv);
                        }
                    }
                    return Some(LRESULT(0));
                } else if ctrl_down && shift_down && (vk == 0x45 /* 'E' */) {
                    let cur = crate::effect::EFFECTS_ENABLED.load(Ordering::Acquire);
                    crate::effect::EFFECTS_ENABLED.store(!cur, Ordering::Release);
                    sync_menu_checkmarks();
                    return Some(LRESULT(0));
                }
            }
            _ => {}
        }
        None
    }));

    if let Ok(Some(lr)) = handled {
        return lr;
    }

    let orig = *ORIGINAL_WNDPROC.lock().unwrap();
    if let Some(orig_fn_ptr) = orig {
        let orig_fn: WNDPROC = std::mem::transmute(orig_fn_ptr);
        CallWindowProcW(orig_fn, hwnd, msg, wparam, lparam)
    } else {
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}

/// Shows or toggles the Effects Manager window ("Map Effect File").
pub fn toggle_effects_manager(parent: HWND) {
    let mut lock = MANAGER_HWND.lock().unwrap();
    if let Some(h) = *lock {
        let hwnd = HWND(h as *mut c_void);
        if unsafe { IsWindow(Some(hwnd)) }.as_bool() {
            if unsafe { IsWindowVisible(hwnd) }.as_bool() {
                unsafe { let _ = ShowWindow(hwnd, SW_HIDE); }
            } else {
                refresh_effects_manager_dialog(hwnd);
                unsafe {
                    let _ = ShowWindow(hwnd, SW_SHOW);
                    let _ = SetForegroundWindow(hwnd);
                }
            }
            return;
        }
    }

    let hwnd = create_effects_manager_window(parent);
    *lock = Some(hwnd.0 as usize);
}

fn get_or_create_fonts() -> (HFONT, HFONT) {
    let mut font_lock = UI_FONT.lock().unwrap();
    let mut bold_lock = BOLD_FONT.lock().unwrap();

    let font = match *font_lock {
        Some(f) => HFONT(f as *mut c_void),
        None => {
            let f = unsafe {
                CreateFontW(
                    -12, 0, 0, 0, FW_NORMAL.0 as i32, 0, 0, 0,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, (DEFAULT_PITCH.0 | FF_DONTCARE.0) as u32,
                    w!("Segoe UI"),
                )
            };
            *font_lock = Some(f.0 as usize);
            f
        }
    };

    let bold = match *bold_lock {
        Some(f) => HFONT(f as *mut c_void),
        None => {
            let f = unsafe {
                CreateFontW(
                    -13, 0, 0, 0, FW_BOLD.0 as i32, 0, 0, 0,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, (DEFAULT_PITCH.0 | FF_DONTCARE.0) as u32,
                    w!("Segoe UI"),
                )
            };
            *bold_lock = Some(f.0 as usize);
            f
        }
    };

    (font, bold)
}

fn get_or_create_mono_font() -> HFONT {
    let mut mono_lock = MONO_FONT.lock().unwrap();
    match *mono_lock {
        Some(f) => HFONT(f as *mut c_void),
        None => {
            let f = unsafe {
                CreateFontW(
                    -13, 0, 0, 0, FW_NORMAL.0 as i32, 0, 0, 0,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, (DEFAULT_PITCH.0 | FF_DONTCARE.0) as u32,
                    w!("Consolas"),
                )
            };
            *mono_lock = Some(f.0 as usize);
            f
        }
    }
}

fn set_control_font(ctrl: HWND, font: HFONT) {
    if !ctrl.0.is_null() && !font.0.is_null() {
        unsafe {
            send_msg(ctrl, WM_SETFONT, font.0 as usize, 1);
        }
    }
}

/// Creates the native MME "Map Effect File" window with File(F), Edit(E), Tool(T) menus,
/// TabControl, Caption label, ListView with checkboxes, and Selected Items controls.
fn create_effects_manager_window(parent: HWND) -> HWND {
    let class_name: Vec<u16> = "HikariEffectsManager\0".encode_utf16().collect();
    let container_class_name: Vec<u16> = "HikariParamContainer\0".encode_utf16().collect();

    unsafe {
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(effects_manager_wndproc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: HINSTANCE(null_mut()),
            hIcon: HICON(null_mut()),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: HBRUSH((COLOR_BTNFACE.0 + 1) as isize as *mut c_void),
            lpszMenuName: PCWSTR::null(),
            lpszClassName: PCWSTR(class_name.as_ptr()),
            hIconSm: HICON(null_mut()),
        };
        let _ = RegisterClassExW(&wc);

        let container_wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(param_container_wndproc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: HINSTANCE(null_mut()),
            hIcon: HICON(null_mut()),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: HBRUSH((COLOR_WINDOW.0 + 1) as isize as *mut c_void),
            lpszMenuName: PCWSTR::null(),
            lpszClassName: PCWSTR(container_class_name.as_ptr()),
            hIconSm: HICON(null_mut()),
        };
        let _ = RegisterClassExW(&container_wc);

        // Build Dialog Menu Bar: File(F), Edit(E), Tool(T)
        let main_menu = CreateMenu().unwrap_or_default();
        let file_menu = CreatePopupMenu().unwrap_or_default();
        let _ = AppendMenuW(file_menu, MF_STRING, ID_FILE_OPEN as usize, w!("Open(O)"));
        let _ = AppendMenuW(file_menu, MF_STRING, ID_FILE_SAVE as usize, w!("Save(S)"));
        let _ = AppendMenuW(file_menu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(file_menu, MF_STRING, ID_FILE_OPEN_MODEL as usize, w!("Open by Model(M)"));
        let _ = AppendMenuW(file_menu, MF_STRING, ID_FILE_SAVE_MODEL as usize, w!("Save by Model(N)"));
        let _ = AppendMenuW(file_menu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(file_menu, MF_STRING, ID_FILE_CLOSE as usize, w!("Close(C)"));
        let _ = AppendMenuW(main_menu, MF_POPUP, file_menu.0 as usize, w!("File(F)"));

        let edit_menu = CreatePopupMenu().unwrap_or_default();
        let _ = AppendMenuW(edit_menu, MF_STRING, ID_EDIT_SET_EFFECT as usize, w!("Set Effect File(F)"));
        let _ = AppendMenuW(edit_menu, MF_STRING, ID_EDIT_REMOVE_EFFECT as usize, w!("Remove Effect(N)"));
        let _ = AppendMenuW(edit_menu, MF_STRING, ID_EDIT_HIDE_SHOW as usize, w!("Hide/Show(H)"));
        let _ = AppendMenuW(edit_menu, MF_STRING, ID_EDIT_RESET_DEFAULT as usize, w!("Reset with default(R)"));
        let _ = AppendMenuW(edit_menu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(edit_menu, MF_STRING, ID_EDIT_SUBSET_EXTRACT as usize, w!("Subset-Extract(E)"));
        let _ = AppendMenuW(main_menu, MF_POPUP, edit_menu.0 as usize, w!("Edit(E)"));

        let tool_menu = CreatePopupMenu().unwrap_or_default();
        let _ = AppendMenuW(tool_menu, MF_STRING, ID_TOOL_DISASSEMBLE as usize, w!("Disassemble Effect(D)"));
        let _ = AppendMenuW(tool_menu, MF_SEPARATOR, 0, PCWSTR::null());
        let skip_val = crate::effect::SKIP_DEVICE_VALIDATION.load(Ordering::Acquire);
        let skip_flags = MF_STRING | if skip_val { MF_CHECKED } else { MF_UNCHECKED };
        let _ = AppendMenuW(tool_menu, skip_flags, ID_TOOL_SKIP_VALIDATION as usize, w!("Skip Device Validation(S)"));
        let _ = AppendMenuW(tool_menu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(tool_menu, MF_STRING | MF_CHECKED, ID_TOOL_TOGGLE_PARAMS as usize, w!("Show HLSL Parameters Panel"));
        let _ = AppendMenuW(main_menu, MF_POPUP, tool_menu.0 as usize, w!("Tool(T)"));

        let show_params = SHOW_PARAMS_PANEL.load(Ordering::Acquire);
        let win_w = if show_params { 920 } else { 590 };

        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            PCWSTR(class_name.as_ptr()),
            w!("Map Effect File"),
            WS_OVERLAPPEDWINDOW & !(WS_MAXIMIZEBOX | WS_THICKFRAME),
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            win_w,
            535,
            Some(parent),
            Some(main_menu),
            None,
            None,
        ).unwrap_or_default();

        let (font, bold) = get_or_create_fonts();

        // 1. TabControl (ID 1002)
        let tab_ctrl = CreateWindowExW(
            WINDOW_EX_STYLE::default(),
            w!("SysTabControl32"),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
            12, 10, 556, 422,
            Some(hwnd),
            Some(HMENU(ID_TAB_CONTROL as usize as *mut c_void)),
            None,
            None,
        ).unwrap_or_default();
        set_control_font(tab_ctrl, font);

        // 2. Caption Label (ID 1004)
        let caption_lbl = make_child(
            WINDOW_EX_STYLE::default(),
            w!("STATIC"),
            w!("Main Render Target"),
            WINDOW_STYLE::default(),
            24, 38, 532, 18,
            hwnd,
            Some(ID_CAPTION_LABEL),
        );
        set_control_font(caption_lbl, bold);

        // 3. ListView (ID 1003) with checkboxes
        let listview = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            w!("SysListView32"),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | WINDOW_STYLE(LVS_REPORT | LVS_SHOWSELALWAYS),
            20, 58, 540, 364,
            Some(hwnd),
            Some(HMENU(ID_LISTVIEW as usize as *mut c_void)),
            None,
            None,
        ).unwrap_or_default();
        set_control_font(listview, font);

        // Extended style: full row select + grid lines + checkboxes
        let _ = send_msg(
            listview,
            LVM_SETEXTENDEDLISTVIEWSTYLE,
            0,
            (LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES) as isize,
        );

        // Add columns
        let mut col_obj_text: Vec<u16> = "Object\0".encode_utf16().collect();
        let mut col = LVCOLUMNW {
            mask: LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM,
            fmt: 0,
            cx: 270,
            psz_text: col_obj_text.as_mut_ptr(),
            cch_text_max: col_obj_text.len() as i32,
            i_sub_item: 0,
            i_image: 0,
            i_order: 0,
            cx_min: 0,
            cx_default: 0,
            cx_ideal: 0,
        };
        let _ = send_msg(listview, LVM_INSERTCOLUMNW, 0, &col as *const _ as isize);

        let mut col_fx_text: Vec<u16> = "Effect File\0".encode_utf16().collect();
        col.cx = 250;
        col.psz_text = col_fx_text.as_mut_ptr();
        col.cch_text_max = col_fx_text.len() as i32;
        col.i_sub_item = 1;
        let _ = send_msg(listview, LVM_INSERTCOLUMNW, 1, &col as *const _ as isize);

        // 4. "Selected Items" Group Box (ID 1006)
        let group_box = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Selected Items"),
            WINDOW_STYLE(BS_GROUPBOX as u32),
            12, 438, 260, 48,
            hwnd,
            Some(ID_GROUP_SELECTED),
        );
        set_control_font(group_box, font);

        let set_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Set Effect"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            20, 454, 74, 24,
            hwnd,
            Some(ID_SET_EFFECT_BTN),
        );
        set_control_font(set_btn, font);

        let rem_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Remove Effect"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            98, 454, 86, 24,
            hwnd,
            Some(ID_REMOVE_EFFECT_BTN),
        );
        set_control_font(rem_btn, font);

        let hide_show_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Hide/Show"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            188, 454, 76, 24,
            hwnd,
            Some(ID_HIDE_SHOW_BTN),
        );
        set_control_font(hide_show_btn, font);

        // 5. Bottom action buttons: OK (1), Cancel (2), Apply (1005)
        let ok_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("OK"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            320, 454, 74, 24,
            hwnd,
            Some(ID_OK_BTN),
        );
        set_control_font(ok_btn, bold);

        let cancel_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Cancel"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            402, 454, 74, 24,
            hwnd,
            Some(ID_CANCEL_BTN),
        );
        set_control_font(cancel_btn, font);

        let apply_btn = make_child(
            WINDOW_EX_STYLE::default(),
            w!("BUTTON"),
            w!("Apply"),
            WINDOW_STYLE(BS_PUSHBUTTON as u32),
            484, 454, 74, 24,
            hwnd,
            Some(ID_APPLY_BTN),
        );
        set_control_font(apply_btn, font);
        let _ = EnableWindow(apply_btn, false);
        IS_DIRTY.store(false, Ordering::Release);

        // 6. Right panel: Parameter tweaks container
        let param_lbl = make_child(
            WINDOW_EX_STYLE::default(),
            w!("STATIC"),
            w!("HLSL Shader Parameters:"),
            WINDOW_STYLE::default(),
            584, 12, 310, 18,
            hwnd,
            None,
        );
        set_control_font(param_lbl, bold);

        let container = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            PCWSTR(container_class_name.as_ptr()),
            PCWSTR::null(),
            WS_CHILD | (if show_params { WS_VISIBLE } else { WINDOW_STYLE::default() }) | WS_VSCROLL | WS_CLIPCHILDREN,
            584, 34, 316, 452,
            Some(hwnd),
            None,
            None,
            None,
        ).unwrap_or_default();
        *PARAM_CONTAINER_HWND.lock().unwrap() = Some(container.0 as usize);

        // 7. Rebuild tabs & populate list
        rebuild_tab_control(tab_ctrl);
        refresh_assignment_list(listview);
        sync_selection_controls(hwnd, listview);

        let _ = ShowWindow(hwnd, SW_SHOW);
        let _ = UpdateWindow(hwnd);
        hwnd
    }
}

fn rebuild_tab_control(tab_ctrl: HWND) {
    if tab_ctrl.0.is_null() {
        return;
    }
    unsafe {
        let _ = send_msg(tab_ctrl, TCM_DELETEALLITEMS, 0, 0);

        let reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        for (i, tab) in reg.tabs.iter().enumerate() {
            let mut tab_text: Vec<u16> = format!("{}\0", tab.name).encode_utf16().collect();
            let ti = TCITEMW {
                mask: TCIF_TEXT,
                dw_state: 0,
                dw_state_mask: 0,
                psz_text: tab_text.as_mut_ptr(),
                cch_text_max: tab_text.len() as i32,
                i_image: -1,
                l_param: i as isize,
            };
            send_msg(tab_ctrl, TCM_INSERTITEMW, i, &ti as *const _ as isize);
        }

        let cur = reg.current_tab_idx.min(reg.tabs.len().saturating_sub(1));
        send_msg(tab_ctrl, TCM_SETCURSEL, cur, 0);
    }
}

fn update_caption_for_current_tab(parent: HWND) {
    let reg = ASSIGNMENT_REGISTRY.lock().unwrap();
    let cur = reg.current_tab_idx;
    let desc = if let Some(tab) = reg.tabs.get(cur) {
        if !tab.is_offscreen {
            "Main Render Target".to_string()
        } else {
            tab.description.clone()
        }
    } else {
        "Main Render Target".to_string()
    };
    drop(reg);

    unsafe {
        if let Ok(lbl) = GetDlgItem(Some(parent), ID_CAPTION_LABEL as i32) {
            let w_desc: Vec<u16> = format!("{desc}\0").encode_utf16().collect();
            let _ = SetWindowTextW(lbl, PCWSTR(w_desc.as_ptr()));
        }
    }
}

fn set_status_text(text: &str) {
    crate::log_msg(&format!("[UI Status] {text}"));
}

/// Fully refreshes the host objects, tab control, and assignment list in the Effects Manager dialog.
pub fn refresh_effects_manager_dialog(hwnd: HWND) {
    if hwnd.0.is_null() || !unsafe { IsWindow(Some(hwnd)) }.as_bool() {
        return;
    }
    if let Ok(mut reg) = ASSIGNMENT_REGISTRY.lock() {
        reg.sync_host_objects();
    }
    LAST_DIALOG_FINGERPRINT.store(crate::mmd_host::get_host_objects_fingerprint(), Ordering::Relaxed);
    unsafe {
        if let Ok(tc) = GetDlgItem(Some(hwnd), ID_TAB_CONTROL as i32) {
            rebuild_tab_control(tc);
        }
        if let Ok(lv) = GetDlgItem(Some(hwnd), ID_LISTVIEW as i32) {
            refresh_assignment_list(lv);
            sync_selection_controls(hwnd, lv);
        }
        update_caption_for_current_tab(hwnd);
    }
}

/// Asynchronously notifies the Effects Manager dialog to refresh if visible when host models change.
pub fn notify_host_objects_changed() {
    let lock = MANAGER_HWND.lock().unwrap();
    if let Some(h) = *lock {
        let hwnd = HWND(h as *mut c_void);
        if unsafe { IsWindow(Some(hwnd)) }.as_bool() && unsafe { IsWindowVisible(hwnd) }.as_bool() {
            unsafe {
                let _ = PostMessageW(Some(hwnd), WM_REFRESH_HOST_OBJECTS, WPARAM(0), LPARAM(0));
            }
        }
    }
}

fn refresh_assignment_list(listview: HWND) {
    if listview.0.is_null() {
        return;
    }

    struct DisplayRow {
        col0: String,
        col1: String,
        shown: bool,
        is_default_row: bool,
        row_ref: UiRowRef,
    }

    let mut display_rows = Vec::new();
    {
        let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        reg.sync_host_objects();
        let cur_tab = reg.current_tab_idx;

        // 1. Default row
        let (def_col1, def_path) = if cur_tab == 0 {
            if reg.default_effect_path.is_empty() {
                ("(none)".to_string(), String::new())
            } else {
                let name = std::path::Path::new(&reg.default_effect_path)
                    .file_name()
                    .map(|s| s.to_string_lossy().to_string())
                    .unwrap_or_else(|| reg.default_effect_path.clone());
                (name, reg.default_effect_path.clone())
            }
        } else if let Some(tab) = reg.tabs.get(cur_tab) {
            if !tab.default_rules.is_empty() {
                (tab.default_rules.clone(), tab.default_effect_path.clone())
            } else {
                ("(none)".to_string(), String::new())
            }
        } else {
            ("(none)".to_string(), String::new())
        };

        display_rows.push(DisplayRow {
            col0: "(default)".to_string(),
            col1: def_col1,
            shown: true,
            is_default_row: true,
            row_ref: UiRowRef {
                object_id: None,
                subset: -1,
                filename: "(default)".to_string(),
                effect_path: def_path,
                shown: true,
            },
        });

        // 2. Models & subsets
        let host_objects = crate::mmd_host::enumerate_host_objects();
        for obj in &host_objects {
            let is_expanded = reg.is_subset_expanded(obj.id);
            let prefix = if is_expanded { "[-] " } else { "[+] " };
            let base_name = std::path::Path::new(&obj.filename)
                .file_name()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| obj.filename.clone());
            let title = format!("{prefix}{base_name} ({})", if obj.is_pmd { "Model" } else { "Accessory" });

            let (_, eff_path, shown) = reg.resolve_tab_effect(cur_tab, obj.id, -1, &obj.filename);
            let eff_name = if eff_path.is_empty() {
                "(none)".to_string()
            } else {
                std::path::Path::new(&eff_path)
                    .file_name()
                    .map(|s| s.to_string_lossy().to_string())
                    .unwrap_or_else(|| eff_path.clone())
            };

            display_rows.push(DisplayRow {
                col0: title,
                col1: eff_name.clone(),
                shown,
                is_default_row: false,
                row_ref: UiRowRef {
                    object_id: Some(obj.id),
                    subset: -1,
                    filename: obj.filename.clone(),
                    effect_path: eff_path.clone(),
                    shown,
                },
            });

            if is_expanded {
                for m in 0..obj.mat_count {
                    let is_last = m == obj.mat_count - 1;
                    let branch = if is_last { "  `-- " } else { "  |-- " };
                    let sub_title = format!("{branch}Material {m}");

                    let (_, sub_eff_path, sub_shown) = reg.resolve_tab_effect(cur_tab, obj.id, m, &obj.filename);
                    let sub_eff_name = if sub_eff_path.is_empty() {
                        if !eff_path.is_empty() {
                            format!("(inherits: {eff_name})")
                        } else {
                            "(none)".to_string()
                        }
                    } else {
                        std::path::Path::new(&sub_eff_path)
                            .file_name()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or_else(|| sub_eff_path.clone())
                    };

                    display_rows.push(DisplayRow {
                        col0: sub_title,
                        col1: sub_eff_name,
                        shown: sub_shown,
                        is_default_row: false,
                        row_ref: UiRowRef {
                            object_id: Some(obj.id),
                            subset: m,
                            filename: obj.filename.clone(),
                            effect_path: sub_eff_path,
                            shown: sub_shown,
                        },
                    });
                }
            }
        }
    } // reg lock dropped

    {
        let mut rows = UI_ROWS.lock().unwrap();
        rows.clear();
        for r in &display_rows {
            rows.push(r.row_ref.clone());
        }
    }

    IS_REFRESHING.store(true, Ordering::SeqCst);
    unsafe {
        let _ = send_msg(listview, WM_SETREDRAW, 0, 0);
        let _ = send_msg(listview, LVM_DELETEALLITEMS, 0, 0);

        for (cur_row, r) in display_rows.iter().enumerate() {
            insert_list_item(listview, cur_row as i32, &r.col0, &r.col1);

            // Configure checkbox state image:
            // Row 0 has NO checkbox (state: 0); normal rows have checked (0x2000) or unchecked (0x1000)
            let check_state = if r.is_default_row {
                0x0000
            } else if r.shown {
                0x2000
            } else {
                0x1000
            };

            let item_state = LVITEMW {
                mask: LVIF_STATE,
                i_item: cur_row as i32,
                i_sub_item: 0,
                state: check_state,
                state_mask: LVIS_STATEIMAGEMASK,
                psz_text: null_mut(),
                cch_text_max: 0,
                i_image: 0,
                l_param: cur_row as isize,
                i_indent: 0,
                i_group_id: 0,
                c_columns: 0,
                pu_columns: null_mut(),
                pi_col_fmt: null_mut(),
                i_group: 0,
            };
            let _ = send_msg(listview, LVM_SETITEMSTATE, cur_row, &item_state as *const _ as isize);
        }

        let _ = send_msg(listview, WM_SETREDRAW, 1, 0);
        let _ = InvalidateRect(Some(listview), None, true);
    }
    IS_REFRESHING.store(false, Ordering::SeqCst);
    crate::log_msg(&format!("[UI] refresh_assignment_list completed: {} items loaded", display_rows.len()));
    unsafe {
        if let Ok(parent) = GetParent(listview) {
            if !parent.0.is_null() {
                sync_selection_controls(parent, listview);
            }
        }
    }
}

unsafe fn insert_list_item(listview: HWND, row_idx: i32, col0_text: &str, col1_text: &str) {
    let mut w0: Vec<u16> = col0_text.encode_utf16().chain(std::iter::once(0)).collect();
    let item = LVITEMW {
        mask: LVIF_TEXT | LVIF_PARAM,
        i_item: row_idx,
        i_sub_item: 0,
        state: 0,
        state_mask: 0,
        psz_text: w0.as_mut_ptr(),
        cch_text_max: w0.len() as i32,
        i_image: 0,
        l_param: row_idx as isize,
        i_indent: 0,
        i_group_id: 0,
        c_columns: 0,
        pu_columns: null_mut(),
        pi_col_fmt: null_mut(),
        i_group: 0,
    };
    send_msg(listview, LVM_INSERTITEMW, 0, &item as *const _ as isize);

    let mut w1: Vec<u16> = col1_text.encode_utf16().chain(std::iter::once(0)).collect();
    let sub_item = LVITEMW {
        mask: LVIF_TEXT,
        i_item: row_idx,
        i_sub_item: 1,
        state: 0,
        state_mask: 0,
        psz_text: w1.as_mut_ptr(),
        cch_text_max: w1.len() as i32,
        i_image: 0,
        l_param: row_idx as isize,
        i_indent: 0,
        i_group_id: 0,
        c_columns: 0,
        pu_columns: null_mut(),
        pi_col_fmt: null_mut(),
        i_group: 0,
    };
    send_msg(listview, LVM_SETITEMW, 0, &sub_item as *const _ as isize);
}

fn on_row_selected(row_idx: usize) {
    *SELECTED_ROW.lock().unwrap() = Some(row_idx);

    let row = {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx >= rows.len() {
            return;
        }
        rows[row_idx].clone()
    };

    let effect_ptr = {
        let reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        let cur_tab = reg.current_tab_idx;
        match row.object_id {
            None => {
                if cur_tab == 0 {
                    reg.default_effect
                } else if let Some(tab) = reg.tabs.get(cur_tab) {
                    tab.default_effect
                } else {
                    reg.default_effect
                }
            }
            Some(obj_id) => {
                let (eff, _, _) = reg.resolve_tab_effect(cur_tab, obj_id, row.subset, &row.filename);
                eff
            }
        }
    };

    *ACTIVE_TARGET_EFFECT.lock().unwrap() = effect_ptr.map(|p| p as usize);

    if let Some(c) = *PARAM_CONTAINER_HWND.lock().unwrap() {
        let container = HWND(c as *mut c_void);
        rebuild_param_controls(container, effect_ptr);
    }
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        let parent = HWND(h as *mut c_void);
        if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
            sync_selection_controls(parent, lv);
        }
    }
}

fn handle_row_check_toggle(row_idx: usize, is_checked: bool) {
    if row_idx == 0 {
        return; // (default) row has no check state
    }
    let rows = UI_ROWS.lock().unwrap();
    if row_idx >= rows.len() {
        return;
    }
    let row = rows[row_idx].clone();
    drop(rows);

    let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
    let cur_tab = reg.current_tab_idx;
    reg.set_tab_item_shown(cur_tab, row.object_id, row.subset, is_checked);
    if crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire) {
        let _ = reg.save_hmm("hikari_mapping.hmm");
    }
    drop(reg);
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        let parent = HWND(h as *mut c_void);
        set_dirty_state(parent, true);
    }
}

fn toggle_selected_hide_show(listview: HWND) {
    let sel = unsafe { send_msg(listview, LVM_GETNEXTITEM, usize::MAX, LVNI_SELECTED as isize) };
    if sel < 1 {
        set_status_text("Select an object or subset row to toggle visibility.");
        return;
    }
    let row_idx = sel as usize;
    let cur_state = unsafe { send_msg(listview, LVM_GETITEMSTATE, row_idx, LVIS_STATEIMAGEMASK as isize) } as u32;
    let is_checked = (cur_state & 0x2000) != 0;
    let new_check = !is_checked;
    let new_state = if new_check { 0x2000 } else { 0x1000 };

    let item_state = LVITEMW {
        mask: LVIF_STATE,
        i_item: row_idx as i32,
        i_sub_item: 0,
        state: new_state,
        state_mask: LVIS_STATEIMAGEMASK,
        psz_text: null_mut(),
        cch_text_max: 0,
        i_image: 0,
        l_param: row_idx as isize,
        i_indent: 0,
        i_group_id: 0,
        c_columns: 0,
        pu_columns: null_mut(),
        pi_col_fmt: null_mut(),
        i_group: 0,
    };
    unsafe {
        let _ = send_msg(listview, LVM_SETITEMSTATE, row_idx, &item_state as *const _ as isize);
    }
    handle_row_check_toggle(row_idx, new_check);
    set_status_text(&format!("Visibility: {}", if new_check { "Shown" } else { "Hidden" }));
}

fn pick_and_assign_effect(row_idx: usize) {
    let row = {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx >= rows.len() {
            return;
        }
        rows[row_idx].clone()
    };

    let mut file_buf = [0u16; 512];
    let filter = w!("HLSL Effect Files (*.fx;*.fxsub;*.fxm)\0*.fx;*.fxsub;*.fxm\0All Files (*.*)\0*.*\0\0");
    let title = w!("Select DirectX 11 HLSL Effect File (.fx)");

    let mut ofn = OPENFILENAMEW::default();
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        ofn.hwndOwner = HWND(h as *mut c_void);
    }
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = PWSTR(file_buf.as_mut_ptr());
    ofn.nMaxFile = file_buf.len() as u32;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    let success = unsafe { GetOpenFileNameW(&mut ofn) }.as_bool();
    if !success {
        return;
    }

    let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
    let path = String::from_utf16_lossy(&file_buf[..end]);
    if !path.is_empty() {
        pick_and_assign_effect_path(row_idx, &path);
    }
}

fn pick_and_assign_effect_path(row_idx: usize, path: &str) {
    let row = {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx >= rows.len() {
            return;
        }
        rows[row_idx].clone()
    };

    let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
    let cur_tab = reg.current_tab_idx;
    let res = reg.assign_tab_effect(cur_tab, row.object_id, &row.filename, row.subset, path);
    if crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire) {
        let _ = reg.save_hmm("hikari_mapping.hmm");
    }
    drop(reg);

    match res {
        Ok(()) => {
            set_status_text(&format!("Effect assigned: {path}"));
            if let Some(h) = *MANAGER_HWND.lock().unwrap() {
                let parent = HWND(h as *mut c_void);
                if let Ok(tc) = unsafe { GetDlgItem(Some(parent), ID_TAB_CONTROL as i32) } {
                    rebuild_tab_control(tc);
                }
                if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
                    refresh_assignment_list(lv);
                }
                set_dirty_state(parent, true);
            }
            on_row_selected(row_idx);
        }
        Err(e) => {
            set_status_text(&format!("Effect compilation error: {e}"));
            if let Some(h) = *MANAGER_HWND.lock().unwrap() {
                let parent = HWND(h as *mut c_void);
                if let Ok(tc) = unsafe { GetDlgItem(Some(parent), ID_TAB_CONTROL as i32) } {
                    rebuild_tab_control(tc);
                }
                if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
                    refresh_assignment_list(lv);
                }
                set_dirty_state(parent, true);
            }
            on_row_selected(row_idx);
        }
    }
}

fn remove_assigned_effect(row_idx: usize) {
    *ACTIVE_TARGET_EFFECT.lock().unwrap() = None;
    ACTIVE_PARAM_CONTROLS.lock().unwrap().clear();

    let row = {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx >= rows.len() {
            return;
        }
        rows[row_idx].clone()
    };

    let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
    let cur_tab = reg.current_tab_idx;
    let _ = reg.assign_tab_effect(cur_tab, row.object_id, &row.filename, row.subset, "");
    if crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire) {
        let _ = reg.save_hmm("hikari_mapping.hmm");
    }
    drop(reg);

    set_status_text("Effect removed.");
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        let parent = HWND(h as *mut c_void);
        if let Ok(tc) = unsafe { GetDlgItem(Some(parent), ID_TAB_CONTROL as i32) } {
            rebuild_tab_control(tc);
        }
        if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
            refresh_assignment_list(lv);
        }
        set_dirty_state(parent, true);
    }
    on_row_selected(row_idx);
}

fn toggle_selected_subset_expand(row_idx: usize) {
    let row = {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx >= rows.len() {
            return;
        }
        rows[row_idx].clone()
    };

    if let Some(obj_id) = row.object_id {
        let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        reg.toggle_subset_expand(obj_id);
        drop(reg);

        if let Some(h) = *MANAGER_HWND.lock().unwrap() {
            let parent = HWND(h as *mut c_void);
            if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
                refresh_assignment_list(lv);
            }
            set_dirty_state(parent, true);
        }
    }
}

fn file_open_hmm(parent: HWND, listview: HWND) {
    let mut file_buf = [0u16; 512];
    let filter = w!("Hikari Mapping Files (*.hmm)\0*.hmm\0MME Mapping Files (*.emm)\0*.emm\0All Files (*.*)\0*.*\0\0");
    let title = w!("Open Hikari Effect Mapping (.hmm / .emm)");

    let mut ofn = OPENFILENAMEW::default();
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = PWSTR(file_buf.as_mut_ptr());
    ofn.nMaxFile = file_buf.len() as u32;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if unsafe { GetOpenFileNameW(&mut ofn) }.as_bool() {
        let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
        let path = String::from_utf16_lossy(&file_buf[..end]);
        if !path.is_empty() {
            let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
            match reg.load_hmm(&path) {
                Ok(()) => {
                    drop(reg);
                    if let Ok(tab_ctrl) = unsafe { GetDlgItem(Some(parent), ID_TAB_CONTROL as i32) } {
                        rebuild_tab_control(tab_ctrl);
                    }
                    update_caption_for_current_tab(parent);
                    refresh_assignment_list(listview);
                    sync_selection_controls(parent, listview);
                    set_dirty_state(parent, true);
                    set_status_text(&format!("Loaded mapping: {path}"));
                }
                Err(e) => {
                    set_status_text(&format!("Error loading mapping: {e}"));
                }
            }
        }
    }
}

fn file_save_hmm(parent: HWND) {
    let mut file_buf = [0u16; 512];
    let filter = w!("Hikari Mapping Files (*.hmm)\0*.hmm\0MME Mapping Files (*.emm)\0*.emm\0All Files (*.*)\0*.*\0\0");
    let title = w!("Save Hikari Effect Mapping (.hmm / .emm)");

    let mut ofn = OPENFILENAMEW::default();
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = PWSTR(file_buf.as_mut_ptr());
    ofn.nMaxFile = file_buf.len() as u32;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST;

    if unsafe { GetSaveFileNameW(&mut ofn) }.as_bool() {
        let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
        let mut path = String::from_utf16_lossy(&file_buf[..end]);
        if !path.ends_with(".hmm") && !path.ends_with(".emm") {
            path.push_str(".hmm");
        }
        let reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        let save_res = if path.ends_with(".emm") {
            reg.save_emm(&path)
        } else {
            reg.save_hmm(&path)
        };
        match save_res {
            Ok(()) => set_status_text(&format!("Saved mapping: {path}")),
            Err(e) => set_status_text(&format!("Error saving mapping: {e}")),
        }
    }
}

fn file_open_hmd(parent: HWND, listview: HWND) {
    let sel = *SELECTED_ROW.lock().unwrap();
    let rows = UI_ROWS.lock().unwrap();
    let obj_id = sel.and_then(|idx| rows.get(idx).and_then(|r| r.object_id));
    drop(rows);

    let Some(id) = obj_id else {
        set_status_text("Select a model row first to load .hmd / .emd.");
        return;
    };

    let mut file_buf = [0u16; 512];
    let filter = w!("Hikari Model Data (*.hmd)\0*.hmd\0MME Model Data (*.emd)\0*.emd\0All Files (*.*)\0*.*\0\0");
    let title = w!("Open Model Effect Mapping (.hmd / .emd)");

    let mut ofn = OPENFILENAMEW::default();
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = PWSTR(file_buf.as_mut_ptr());
    ofn.nMaxFile = file_buf.len() as u32;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if unsafe { GetOpenFileNameW(&mut ofn) }.as_bool() {
        let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
        let path = String::from_utf16_lossy(&file_buf[..end]);
        if !path.is_empty() {
            let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
            match reg.load_hmd(id, &path) {
                Ok(()) => {
                    drop(reg);
                    refresh_assignment_list(listview);
                    sync_selection_controls(parent, listview);
                    set_dirty_state(parent, true);
                    set_status_text(&format!("Loaded model mapping: {path}"));
                }
                Err(e) => {
                    set_status_text(&format!("Error loading model mapping: {e}"));
                }
            }
        }
    }
}

fn file_save_hmd(parent: HWND) {
    let sel = *SELECTED_ROW.lock().unwrap();
    let rows = UI_ROWS.lock().unwrap();
    let obj_id = sel.and_then(|idx| rows.get(idx).and_then(|r| r.object_id));
    drop(rows);

    let Some(id) = obj_id else {
        set_status_text("Select a model row first to save .hmd / .emd.");
        return;
    };

    let mut file_buf = [0u16; 512];
    let filter = w!("Hikari Model Data (*.hmd)\0*.hmd\0MME Model Data (*.emd)\0*.emd\0All Files (*.*)\0*.*\0\0");
    let title = w!("Save Model Effect Mapping (.hmd / .emd)");

    let mut ofn = OPENFILENAMEW::default();
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = PWSTR(file_buf.as_mut_ptr());
    ofn.nMaxFile = file_buf.len() as u32;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST;

    if unsafe { GetSaveFileNameW(&mut ofn) }.as_bool() {
        let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
        let mut path = String::from_utf16_lossy(&file_buf[..end]);
        if !path.ends_with(".hmd") && !path.ends_with(".emd") {
            path.push_str(".hmd");
        }
        let reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        let save_res = if path.ends_with(".emd") {
            reg.save_emd(id, &path)
        } else {
            reg.save_hmd(id, &path)
        };
        match save_res {
            Ok(()) => set_status_text(&format!("Saved model mapping: {path}")),
            Err(e) => set_status_text(&format!("Error saving model mapping: {e}")),
        }
    }
}

fn tool_disassemble_effect(parent: HWND) {
    let eff_addr = *ACTIVE_TARGET_EFFECT.lock().unwrap();
    let Some(addr) = eff_addr else {
        unsafe {
            MessageBoxW(
                Some(parent),
                w!("No effect is currently selected. Select an object with an assigned effect to disassemble its shaders."),
                w!("Disassemble Effect"),
                MB_OK | MB_ICONINFORMATION,
            );
        }
        return;
    };

    match unsafe { crate::effect::disassemble_hikari_effect(addr as *mut c_void) } {
        Ok(disasm_bytes) => {
            let disasm_text = String::from_utf8_lossy(&disasm_bytes);
            show_text_window(parent, "Hikari - Disassembled HLSL / DXBC Assembly", &disasm_text);
        }
        Err(e) => {
            let err_w: Vec<u16> = format!("Disassembly failed: {e}\0").encode_utf16().collect();
            unsafe {
                MessageBoxW(
                    Some(parent),
                    PCWSTR(err_w.as_ptr()),
                    w!("Disassembly Error"),
                    MB_OK | MB_ICONERROR,
                );
            }
        }
    }
}

fn tool_toggle_params_panel(parent: HWND) {
    let cur = SHOW_PARAMS_PANEL.load(Ordering::Acquire);
    let new_val = !cur;
    SHOW_PARAMS_PANEL.store(new_val, Ordering::Release);

    unsafe {
        let menu = GetMenu(parent);
        if !menu.0.is_null() {
            let chk_flag = (MF_BYCOMMAND.0 | if new_val { MF_CHECKED.0 } else { MF_UNCHECKED.0 }) as u32;
            let _ = CheckMenuItem(menu, ID_TOOL_TOGGLE_PARAMS, chk_flag);
        }
        if let Some(c) = *PARAM_CONTAINER_HWND.lock().unwrap() {
            let _ = ShowWindow(HWND(c as *mut c_void), if new_val { SW_SHOW } else { SW_HIDE });
        }
        let mut rect = RECT::default();
        let _ = GetWindowRect(parent, &mut rect);
        let new_w = if new_val { 920 } else { 590 };
        let h = rect.bottom - rect.top;
        let _ = SetWindowPos(parent, None, rect.left, rect.top, new_w, h, SWP_NOZORDER | SWP_NOMOVE);
    }
}

fn rebuild_param_controls(container: HWND, target_effect: Option<*mut HikariEffect>) {
    unsafe {
        ACTIVE_PARAM_CONTROLS.lock().unwrap().clear();

        let mut children = Vec::new();
        let mut child = GetWindow(container, GW_CHILD).unwrap_or_default();
        while !child.0.is_null() {
            children.push(child);
            child = GetWindow(child, GW_HWNDNEXT).unwrap_or_default();
        }
        for c in children {
            let _ = DestroyWindow(c);
        }

        let (font, bold) = get_or_create_fonts();

        let eff_ptr = match target_effect {
            Some(p) if !p.is_null() => p,
            _ => {
                let placeholder = make_child(
                    WINDOW_EX_STYLE::default(),
                    w!("STATIC"),
                    w!("No effect assigned to the selected object.\n\nClick 'Set Effect' to assign an HLSL effect file."),
                    WINDOW_STYLE::default(),
                    14, 20, 280, 80,
                    container,
                    None,
                );
                set_control_font(placeholder, font);
                let _ = ShowScrollBar(container, SB_VERT, false);
                return;
            }
        };

        let eff = &mut *eff_ptr;
        let mut current_y = 12;
        let mut ctrl_id_counter = PARAM_CONTROL_ID_BASE;
        let mut last_group = String::new();
        let mut param_controls = Vec::new();

        for (p_idx, param) in eff.parameters.iter().enumerate() {
            let ui_desc = match param.ui {
                Some(ref u) => u,
                None => continue,
            };

            // Render group header
            if !ui_desc.group.is_empty() && ui_desc.group != last_group {
                last_group = ui_desc.group.clone();
                current_y += 6;
                let w_group: Vec<u16> = format!("[ {} ]", last_group).encode_utf16().chain(std::iter::once(0)).collect();
                let grp_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_group.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 280, 18, container, None);
                set_control_font(grp_lbl, bold);
                current_y += 22;
            }

            let name_disp = if !ui_desc.label.is_empty() { &ui_desc.label } else { &param.name };
            let w_label: Vec<u16> = format!("{}:", name_disp).encode_utf16().chain(std::iter::once(0)).collect();
            let lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_label.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 280, 18, container, None);
            set_control_font(lbl, font);
            current_y += 20;

            if !ui_desc.help.is_empty() {
                let w_help: Vec<u16> = format!("  {}", ui_desc.help).encode_utf16().chain(std::iter::once(0)).collect();
                let help_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_help.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 280, 16, container, None);
                set_control_font(help_lbl, font);
                current_y += 18;
            }

            match ui_desc.widget {
                UIWidgetKind::Slider | UIWidgetKind::Drag => {
                    let min_v = ui_desc.min;
                    let max_v = if ui_desc.max > ui_desc.min { ui_desc.max } else { ui_desc.min + 1.0 };
                    let cur_v = eff.get_param_value_f32(p_idx).unwrap_or(min_v).clamp(min_v, max_v);

                    let trackbar_id = ctrl_id_counter;
                    ctrl_id_counter += 1;
                    let edit_id = ctrl_id_counter;
                    ctrl_id_counter += 1;

                    let trackbar = make_child(
                        WINDOW_EX_STYLE::default(),
                        TRACKBAR_CLASSW,
                        PCWSTR::null(),
                        WINDOW_STYLE(TBS_HORZ as u32 | TBS_AUTOTICKS as u32),
                        10,
                        current_y,
                        200,
                        28,
                        container,
                        Some(trackbar_id),
                    );

                    let val_str = format!("{:.3}", cur_v);
                    let w_val: Vec<u16> = val_str.encode_utf16().chain(std::iter::once(0)).collect();
                    let edit = make_child(
                        WS_EX_CLIENTEDGE,
                        w!("EDIT"),
                        PCWSTR(w_val.as_ptr()),
                        WINDOW_STYLE(ES_AUTOHSCROLL as u32),
                        216,
                        current_y + 2,
                        64,
                        22,
                        container,
                        Some(edit_id),
                    );
                    set_control_font(edit, font);

                    let _ = send_msg(trackbar, TBM_SETRANGE, 1, (1000 << 16) as isize);
                    let pos = (((cur_v - min_v) / (max_v - min_v)) * 1000.0) as isize;
                    let _ = send_msg(trackbar, TBM_SETPOS, 1, pos);

                    param_controls.push(ActiveParamControl {
                        param_idx: p_idx,
                        widget: ui_desc.widget,
                        trackbar_hwnd: trackbar,
                        edit_hwnd: edit,
                        checkbox_hwnd: HWND(null_mut()),
                        color_btn_hwnd: HWND(null_mut()),
                        color_label_hwnd: HWND(null_mut()),
                        combo_hwnd: HWND(null_mut()),
                        min_val: min_v,
                        max_val: max_v,
                        step: ui_desc.step,
                    });
                    current_y += 34;
                }
                UIWidgetKind::Checkbox => {
                    let is_checked = eff.get_param_value_bool(p_idx).unwrap_or(false);
                    let btn_id = ctrl_id_counter;
                    ctrl_id_counter += 1;

                    let chk = make_child(
                        WINDOW_EX_STYLE::default(),
                        w!("BUTTON"),
                        w!("Enable"),
                        WINDOW_STYLE(BS_AUTOCHECKBOX as u32),
                        10,
                        current_y,
                        180,
                        22,
                        container,
                        Some(btn_id),
                    );
                    set_control_font(chk, font);

                    let check_state = if is_checked { BST_CHECKED } else { BST_UNCHECKED };
                    let _ = send_msg(chk, BM_SETCHECK, check_state.0 as usize, 0);

                    param_controls.push(ActiveParamControl {
                        param_idx: p_idx,
                        widget: UIWidgetKind::Checkbox,
                        trackbar_hwnd: HWND(null_mut()),
                        edit_hwnd: HWND(null_mut()),
                        checkbox_hwnd: chk,
                        color_btn_hwnd: HWND(null_mut()),
                        color_label_hwnd: HWND(null_mut()),
                        combo_hwnd: HWND(null_mut()),
                        min_val: 0.0,
                        max_val: 1.0,
                        step: 1.0,
                    });
                    current_y += 28;
                }
                UIWidgetKind::Color => {
                    let vals = eff.get_param_value_float3(p_idx).unwrap_or([1.0, 1.0, 1.0]);
                    let r = vals[0].clamp(0.0, 1.0);
                    let g = vals[1].clamp(0.0, 1.0);
                    let b = vals[2].clamp(0.0, 1.0);

                    let btn_id = ctrl_id_counter;
                    ctrl_id_counter += 1;

                    let color_btn = make_child(
                        WINDOW_EX_STYLE::default(),
                        w!("BUTTON"),
                        w!("Pick Color..."),
                        WINDOW_STYLE(BS_PUSHBUTTON as u32),
                        10,
                        current_y,
                        120,
                        24,
                        container,
                        Some(btn_id),
                    );
                    set_control_font(color_btn, font);

                    let rgb_str = format!("RGB: {:.2}, {:.2}, {:.2}", r, g, b);
                    let w_rgb: Vec<u16> = rgb_str.encode_utf16().chain(std::iter::once(0)).collect();
                    let color_lbl = make_child(
                        WINDOW_EX_STYLE::default(),
                        w!("STATIC"),
                        PCWSTR(w_rgb.as_ptr()),
                        WINDOW_STYLE::default(),
                        138,
                        current_y + 3,
                        140,
                        20,
                        container,
                        None,
                    );
                    set_control_font(color_lbl, font);

                    param_controls.push(ActiveParamControl {
                        param_idx: p_idx,
                        widget: UIWidgetKind::Color,
                        trackbar_hwnd: HWND(null_mut()),
                        edit_hwnd: HWND(null_mut()),
                        checkbox_hwnd: HWND(null_mut()),
                        color_btn_hwnd: color_btn,
                        color_label_hwnd: color_lbl,
                        combo_hwnd: HWND(null_mut()),
                        min_val: 0.0,
                        max_val: 1.0,
                        step: 0.0,
                    });
                    current_y += 30;
                }
                UIWidgetKind::Combo => {
                    let combo_id = ctrl_id_counter;
                    ctrl_id_counter += 1;

                    let combo = make_child(
                        WINDOW_EX_STYLE::default(),
                        w!("COMBOBOX"),
                        PCWSTR::null(),
                        WINDOW_STYLE(CBS_DROPDOWNLIST as u32),
                        10,
                        current_y,
                        270,
                        160,
                        container,
                        Some(combo_id),
                    );
                    set_control_font(combo, font);

                    for item_str in &ui_desc.items {
                        let w_item: Vec<u16> = item_str.encode_utf16().chain(std::iter::once(0)).collect();
                        let _ = send_msg(combo, CB_ADDSTRING, 0, w_item.as_ptr() as isize);
                    }
                    let cur_idx = eff.get_param_value_f32(p_idx).unwrap_or(0.0) as usize;
                    let _ = send_msg(combo, CB_SETCURSEL, cur_idx, 0);

                    param_controls.push(ActiveParamControl {
                        param_idx: p_idx,
                        widget: UIWidgetKind::Combo,
                        trackbar_hwnd: HWND(null_mut()),
                        edit_hwnd: HWND(null_mut()),
                        checkbox_hwnd: HWND(null_mut()),
                        color_btn_hwnd: HWND(null_mut()),
                        color_label_hwnd: HWND(null_mut()),
                        combo_hwnd: combo,
                        min_val: 0.0,
                        max_val: ui_desc.items.len().saturating_sub(1) as f32,
                        step: 1.0,
                    });
                    current_y += 32;
                }
                UIWidgetKind::Input | UIWidgetKind::None => {}
            }
        }

        *ACTIVE_PARAM_CONTROLS.lock().unwrap() = param_controls;

        // Scrollbar configuration
        if current_y > 450 {
            let si = SCROLLINFO {
                cbSize: std::mem::size_of::<SCROLLINFO>() as u32,
                fMask: SIF_RANGE | SIF_PAGE,
                nMin: 0,
                nMax: current_y,
                nPage: 450,
                nPos: 0,
                nTrackPos: 0,
            };
            let _ = SetScrollInfo(container, SB_VERT, &si, true);
            let _ = ShowScrollBar(container, SB_VERT, true);
        } else {
            let _ = ShowScrollBar(container, SB_VERT, false);
        }
    }
}

/// Window procedure for the Effects Manager dialog ("Map Effect File").
unsafe extern "system" fn effects_manager_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        effects_manager_wndproc_inner(hwnd, msg, wparam, lparam)
    }));
    result.unwrap_or_else(|_| DefWindowProcW(hwnd, msg, wparam, lparam))
}

unsafe fn effects_manager_wndproc_inner(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_NOTIFY => {
            if lparam.0 == 0 {
                return LRESULT(0);
            }
            let nmhdr = &*(lparam.0 as *const NMHDR);
            if nmhdr.id_from == ID_TAB_CONTROL as usize {
                if nmhdr.code == TCN_SELCHANGE {
                    let cur = send_msg(nmhdr.hwnd_from, TCM_GETCURSEL, 0, 0) as usize;
                    ASSIGNMENT_REGISTRY.lock().unwrap().current_tab_idx = cur;
                    update_caption_for_current_tab(hwnd);
                    if let Ok(lv) = GetDlgItem(Some(hwnd), ID_LISTVIEW as i32) {
                        refresh_assignment_list(lv);
                    }
                    return LRESULT(0);
                }
            } else if nmhdr.id_from == ID_LISTVIEW as usize {
                if IS_REFRESHING.load(Ordering::Relaxed) {
                    return LRESULT(0);
                }
                if nmhdr.code == LVN_ITEMCHANGED {
                    let nmlv = &*(lparam.0 as *const NMLISTVIEW);
                    if (nmlv.u_changed & LVIF_STATE) != 0 {
                        // Check for selection change
                        if (nmlv.u_new_state & LVIS_SELECTED) != 0 && (nmlv.u_old_state & LVIS_SELECTED) == 0 {
                            if nmlv.i_item >= 0 {
                                on_row_selected(nmlv.i_item as usize);
                            }
                        }

                        // Check for checkbox state change
                        let old_check = nmlv.u_old_state & LVIS_STATEIMAGEMASK;
                        let new_check = nmlv.u_new_state & LVIS_STATEIMAGEMASK;
                        if old_check != 0 && new_check != 0 && old_check != new_check {
                            let is_checked = (new_check & 0x2000) != 0;
                            handle_row_check_toggle(nmlv.i_item as usize, is_checked);
                        }
                    }
                } else if nmhdr.code == NM_CLICK {
                    let sel = send_msg(nmhdr.hwnd_from, LVM_GETNEXTITEM, usize::MAX, LVNI_SELECTED as isize);
                    if sel >= 0 {
                        on_row_selected(sel as usize);
                    }
                } else if nmhdr.code == NM_DBLCLK {
                    let sel = send_msg(nmhdr.hwnd_from, LVM_GETNEXTITEM, usize::MAX, LVNI_SELECTED as isize);
                    if sel >= 0 {
                        pick_and_assign_effect(sel as usize);
                    }
                }
            }
        }
        WM_COMMAND => {
            let cmd_id = (wparam.0 & 0xFFFF) as u32;
            let notify_code = ((wparam.0 >> 16) & 0xFFFF) as u32;

            let listview = GetDlgItem(Some(hwnd), ID_LISTVIEW as i32).unwrap_or_default();
            let tab_ctrl = GetDlgItem(Some(hwnd), ID_TAB_CONTROL as i32).unwrap_or_default();

            match cmd_id {
                // Button / Menu Set Effect
                ID_SET_EFFECT_BTN | ID_EDIT_SET_EFFECT => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        pick_and_assign_effect(row_idx);
                    } else {
                        set_status_text("Please select an item in the list first.");
                    }
                    return LRESULT(0);
                }
                // Button / Menu Remove Effect
                ID_REMOVE_EFFECT_BTN | ID_EDIT_REMOVE_EFFECT => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        remove_assigned_effect(row_idx);
                    }
                    return LRESULT(0);
                }
                // Button / Menu Hide/Show
                ID_HIDE_SHOW_BTN | ID_EDIT_HIDE_SHOW => {
                    toggle_selected_hide_show(listview);
                    return LRESULT(0);
                }
                // Menu Reset with default
                ID_EDIT_RESET_DEFAULT => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        remove_assigned_effect(row_idx);
                    }
                    return LRESULT(0);
                }
                // Menu Subset-Extract
                ID_EDIT_SUBSET_EXTRACT => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        toggle_selected_subset_expand(row_idx);
                    }
                    return LRESULT(0);
                }
                // Menu File -> Open (.hmm / .emm)
                ID_FILE_OPEN => {
                    file_open_hmm(hwnd, listview);
                    return LRESULT(0);
                }
                // Menu File -> Save (.hmm / .emm)
                ID_FILE_SAVE => {
                    file_save_hmm(hwnd);
                    return LRESULT(0);
                }
                // Menu File -> Open by Model (.hmd / .emd)
                ID_FILE_OPEN_MODEL => {
                    file_open_hmd(hwnd, listview);
                    return LRESULT(0);
                }
                // Menu File -> Save by Model (.hmd / .emd)
                ID_FILE_SAVE_MODEL => {
                    file_save_hmd(hwnd);
                    return LRESULT(0);
                }
                // Menu File -> Close
                ID_FILE_CLOSE | ID_CANCEL_BTN => {
                    let _ = ShowWindow(hwnd, SW_HIDE);
                    return LRESULT(0);
                }
                // Menu Tool -> Disassemble Effect
                ID_TOOL_DISASSEMBLE => {
                    tool_disassemble_effect(hwnd);
                    return LRESULT(0);
                }
                // Menu Tool -> Skip Device Validation
                ID_TOOL_SKIP_VALIDATION => {
                    let cur = crate::effect::SKIP_DEVICE_VALIDATION.load(Ordering::Acquire);
                    let new_val = !cur;
                    crate::effect::SKIP_DEVICE_VALIDATION.store(new_val, Ordering::Release);
                    let menu = GetMenu(hwnd);
                    if !menu.0.is_null() {
                        let chk_flag = (MF_BYCOMMAND.0 | if new_val { MF_CHECKED.0 } else { MF_UNCHECKED.0 }) as u32;
                        let _ = CheckMenuItem(menu, ID_TOOL_SKIP_VALIDATION, chk_flag);
                    }
                    set_status_text(&format!("Skip Device Validation: {new_val}"));
                    return LRESULT(0);
                }
                // Menu Tool -> Show HLSL Parameters Panel
                ID_TOOL_TOGGLE_PARAMS => {
                    tool_toggle_params_panel(hwnd);
                    return LRESULT(0);
                }
                // Button OK
                ID_OK_BTN => {
                    if crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire) {
                        let _ = ASSIGNMENT_REGISTRY.lock().unwrap().save_hmm("hikari_mapping.hmm");
                    }
                    set_dirty_state(hwnd, false);
                    let _ = ShowWindow(hwnd, SW_HIDE);
                    return LRESULT(0);
                }
                // Button Apply
                ID_APPLY_BTN => {
                    if crate::effect::AUTO_SAVE_MAPPING.load(Ordering::Acquire) {
                        let _ = ASSIGNMENT_REGISTRY.lock().unwrap().save_hmm("hikari_mapping.hmm");
                    }
                    set_dirty_state(hwnd, false);
                    set_status_text("Mapping applied.");
                    return LRESULT(0);
                }
                _ => {}
            }

            // Parameter tweak controls dispatch
            if cmd_id >= PARAM_CONTROL_ID_BASE {
                let eff_addr = *ACTIVE_TARGET_EFFECT.lock().unwrap();
                if let Some(addr) = eff_addr {
                    let is_alive = addr != 0
                        && ASSIGNMENT_REGISTRY
                            .lock()
                            .map(|r| r.is_effect_alive(addr as *mut HikariEffect))
                            .unwrap_or(false);
                    if !is_alive {
                        *ACTIVE_TARGET_EFFECT.lock().unwrap() = None;
                        return LRESULT(0);
                    }
                    let eff = &mut *(addr as *mut HikariEffect);
                    let controls = ACTIVE_PARAM_CONTROLS.lock().unwrap().clone();
                    for ctrl in controls {
                        if ctrl.widget == UIWidgetKind::Checkbox {
                            let btn_hwnd = HWND(lparam.0 as *mut c_void);
                            if btn_hwnd == ctrl.checkbox_hwnd {
                                let is_checked = send_msg(btn_hwnd, BM_GETCHECK, 0, 0) == (BST_CHECKED.0 as isize);
                                let _ = eff.set_param_value_bool(ctrl.param_idx, is_checked);
                                break;
                            }
                        } else if ctrl.widget == UIWidgetKind::Combo {
                            let combo_hwnd = HWND(lparam.0 as *mut c_void);
                            if combo_hwnd == ctrl.combo_hwnd && notify_code == CBN_SELCHANGE {
                                let cur_sel = send_msg(combo_hwnd, CB_GETCURSEL, 0, 0);
                                if cur_sel >= 0 {
                                    let _ = eff.set_param_value_f32(ctrl.param_idx, cur_sel as f32);
                                }
                                break;
                            }
                        } else if ctrl.widget == UIWidgetKind::Color {
                            let btn_hwnd = HWND(lparam.0 as *mut c_void);
                            if btn_hwnd == ctrl.color_btn_hwnd {
                                let vals = eff.get_param_value_float3(ctrl.param_idx).unwrap_or([1.0, 1.0, 1.0]);
                                let init_r = (vals[0].clamp(0.0, 1.0) * 255.0) as u32;
                                let init_g = (vals[1].clamp(0.0, 1.0) * 255.0) as u32;
                                let init_b = (vals[2].clamp(0.0, 1.0) * 255.0) as u32;
                                let mut custom_colors = [COLORREF(0); 16];

                                let mut cc = CHOOSECOLORW::default();
                                cc.lStructSize = std::mem::size_of::<CHOOSECOLORW>() as u32;
                                cc.hwndOwner = hwnd;
                                cc.rgbResult = COLORREF(init_r | (init_g << 8) | (init_b << 16));
                                cc.lpCustColors = custom_colors.as_mut_ptr();
                                cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                                if ChooseColorW(&mut cc).as_bool() {
                                    let r = (cc.rgbResult.0 & 0xFF) as f32 / 255.0;
                                    let g = ((cc.rgbResult.0 >> 8) & 0xFF) as f32 / 255.0;
                                    let b = ((cc.rgbResult.0 >> 16) & 0xFF) as f32 / 255.0;
                                    eff.set_param_value_float3(ctrl.param_idx, [r, g, b]);

                                    let rgb_str = format!("RGB: {:.2}, {:.2}, {:.2}", r, g, b);
                                    let w_rgb: Vec<u16> = rgb_str.encode_utf16().chain(std::iter::once(0)).collect();
                                    let _ = SetWindowTextW(ctrl.color_label_hwnd, PCWSTR(w_rgb.as_ptr()));
                                }
                                break;
                            }
                        } else if notify_code == EN_KILLFOCUS {
                            let edit_hwnd = HWND(lparam.0 as *mut c_void);
                            if edit_hwnd == ctrl.edit_hwnd {
                                let mut text_buf = [0u16; 64];
                                let len = GetWindowTextW(edit_hwnd, &mut text_buf);
                                let text = String::from_utf16_lossy(&text_buf[..len as usize]);
                                if let Ok(v) = text.parse::<f32>() {
                                    let clamped = v.clamp(ctrl.min_val, ctrl.max_val);
                                    let _ = eff.set_param_value_f32(ctrl.param_idx, clamped);
                                    let pos = (((clamped - ctrl.min_val) / (ctrl.max_val - ctrl.min_val)) * 1000.0) as isize;
                                    let _ = send_msg(ctrl.trackbar_hwnd, TBM_SETPOS, 1, pos);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        WM_HSCROLL => {
            let trackbar = HWND(lparam.0 as *mut c_void);
            let pos = send_msg(trackbar, TBM_GETPOS, 0, 0);

            let eff_addr = *ACTIVE_TARGET_EFFECT.lock().unwrap();
            if let Some(addr) = eff_addr {
                let is_alive = addr != 0
                    && ASSIGNMENT_REGISTRY
                        .lock()
                        .map(|r| r.is_effect_alive(addr as *mut HikariEffect))
                        .unwrap_or(false);
                if !is_alive {
                    *ACTIVE_TARGET_EFFECT.lock().unwrap() = None;
                    return LRESULT(0);
                }
                let eff = &mut *(addr as *mut HikariEffect);
                let controls = ACTIVE_PARAM_CONTROLS.lock().unwrap().clone();
                for ctrl in controls {
                    if ctrl.trackbar_hwnd == trackbar {
                        let frac = (pos as f32) / 1000.0;
                        let val = ctrl.min_val + frac * (ctrl.max_val - ctrl.min_val);
                        let _ = eff.set_param_value_f32(ctrl.param_idx, val);

                        let val_str = format!("{:.3}", val);
                        let w_val: Vec<u16> = val_str.encode_utf16().chain(std::iter::once(0)).collect();
                        let _ = SetWindowTextW(ctrl.edit_hwnd, PCWSTR(w_val.as_ptr()));
                        break;
                    }
                }
            }
            return LRESULT(0);
        }
        WM_REFRESH_HOST_OBJECTS => {
            refresh_effects_manager_dialog(hwnd);
            return LRESULT(0);
        }
        WM_SHOWWINDOW => {
            if wparam.0 != 0 {
                refresh_effects_manager_dialog(hwnd);
            }
        }
        WM_ACTIVATE => {
            if (wparam.0 & 0xFFFF) != 0 {
                let current_fp = crate::mmd_host::get_host_objects_fingerprint();
                if current_fp != LAST_DIALOG_FINGERPRINT.swap(current_fp, Ordering::Relaxed) {
                    refresh_effects_manager_dialog(hwnd);
                }
            }
        }
        WM_CLOSE => {
            let _ = ShowWindow(hwnd, SW_HIDE);
            return LRESULT(0);
        }
        _ => {}
    }

    DefWindowProcW(hwnd, msg, wparam, lparam)
}

/// Window procedure for the scrollable parameter container.
unsafe extern "system" fn param_container_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        param_container_wndproc_inner(hwnd, msg, wparam, lparam)
    }));
    result.unwrap_or_else(|_| DefWindowProcW(hwnd, msg, wparam, lparam))
}

unsafe fn param_container_wndproc_inner(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_VSCROLL => {
            let mut si = SCROLLINFO {
                cbSize: std::mem::size_of::<SCROLLINFO>() as u32,
                fMask: SIF_ALL,
                ..Default::default()
            };
            let _ = GetScrollInfo(hwnd, SB_VERT, &mut si);
            let mut y = si.nPos;

            let action = SCROLLBAR_COMMAND((wparam.0 & 0xFFFF) as i32);
            match action {
                SB_TOP => y = si.nMin,
                SB_BOTTOM => y = si.nMax,
                SB_LINEUP => y -= 16,
                SB_LINEDOWN => y += 16,
                SB_PAGEUP => y -= si.nPage as i32,
                SB_PAGEDOWN => y += si.nPage as i32,
                SB_THUMBTRACK => y = si.nTrackPos,
                _ => {}
            }

            y = y.clamp(si.nMin, si.nMax - si.nPage as i32 + 1);
            if y != si.nPos {
                let _ = ScrollWindowEx(hwnd, 0, si.nPos - y, None, None, None, None, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
                si.nPos = y;
                si.fMask = SIF_POS;
                let _ = SetScrollInfo(hwnd, SB_VERT, &si, true);
            }
            return LRESULT(0);
        }
        WM_MOUSEWHEEL => {
            let delta = ((wparam.0 >> 16) & 0xFFFF) as i16;
            let lines = if delta > 0 { -3 } else { 3 };
            let cmd = if lines < 0 { SB_LINEUP.0 as usize } else { SB_LINEDOWN.0 as usize };
            let _ = send_msg(hwnd, WM_VSCROLL, cmd, 0);
            return LRESULT(0);
        }
        _ => {}
    }

    DefWindowProcW(hwnd, msg, wparam, lparam)
}

/// Modeless Log Window displaying compiler and effect diagnostic logs.
pub fn show_log_window(parent: HWND) {
    let mut lock = LOG_WINDOW_HWND.lock().unwrap();
    if let Some(h) = *lock {
        let hwnd = HWND(h as *mut c_void);
        if unsafe { IsWindow(Some(hwnd)) }.as_bool() {
            unsafe {
                let _ = ShowWindow(hwnd, SW_SHOW);
                let _ = SetForegroundWindow(hwnd);
            }
            refresh_log_window_content(hwnd);
            return;
        }
    }

    let class_name: Vec<u16> = "HikariLogWindow\0".encode_utf16().collect();
    unsafe {
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(log_window_wndproc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: HINSTANCE(null_mut()),
            hIcon: HICON(null_mut()),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: HBRUSH((COLOR_BTNFACE.0 + 1) as isize as *mut c_void),
            lpszMenuName: PCWSTR::null(),
            lpszClassName: PCWSTR(class_name.as_ptr()),
            hIconSm: HICON(null_mut()),
        };
        let _ = RegisterClassExW(&wc);

        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            PCWSTR(class_name.as_ptr()),
            w!("Hikari Effects Log"),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            640,
            420,
            Some(parent),
            None,
            None,
            None,
        ).unwrap_or_default();

        let (font, _) = get_or_create_fonts();
        let mono_font = get_or_create_mono_font();

        // 1001: Readonly multiline edit
        let edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            w!("EDIT"),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WINDOW_STYLE(ES_MULTILINE as u32 | ES_READONLY as u32 | ES_AUTOVSCROLL as u32 | ES_AUTOHSCROLL as u32),
            10, 10, 604, 320,
            Some(hwnd),
            Some(HMENU(1001 as usize as *mut c_void)),
            None,
            None,
        ).unwrap_or_default();
        set_control_font(edit, mono_font);

        let clear_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Clear"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 220, 342, 80, 26, hwnd, Some(3));
        set_control_font(clear_btn, font);

        let close_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Close"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 320, 342, 80, 26, hwnd, Some(2));
        set_control_font(close_btn, font);

        *lock = Some(hwnd.0 as usize);
        refresh_log_window_content(hwnd);
        let _ = ShowWindow(hwnd, SW_SHOW);
        let _ = UpdateWindow(hwnd);
    }
}

fn refresh_log_window_content(hwnd: HWND) {
    let logs = crate::get_recent_logs();
    let logs_w: Vec<u16> = format!("{logs}\0").encode_utf16().collect();
    unsafe {
        if let Ok(edit) = GetDlgItem(Some(hwnd), 1001) {
            let _ = SetWindowTextW(edit, PCWSTR(logs_w.as_ptr()));
            // Scroll to end
            let len = logs_w.len().saturating_sub(1);
            let _ = SendMessageW(edit, 0x00B1 /* EM_SETSEL */, Some(WPARAM(len)), Some(LPARAM(len as isize)));
            let _ = SendMessageW(edit, 0x00B7 /* EM_SCROLLCARET */, None, None);
        }
    }
}

unsafe extern "system" fn log_window_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_COMMAND => {
            let cmd_id = (wparam.0 & 0xFFFF) as u32;
            if cmd_id == 2 { // Close
                let _ = ShowWindow(hwnd, SW_HIDE);
                return LRESULT(0);
            } else if cmd_id == 3 { // Clear
                crate::clear_logs();
                refresh_log_window_content(hwnd);
                return LRESULT(0);
            }
        }
        WM_SIZE => {
            let mut rect = RECT::default();
            let _ = GetClientRect(hwnd, &mut rect);
            let w = rect.right - rect.left;
            let h = rect.bottom - rect.top;
            if let Ok(edit) = GetDlgItem(Some(hwnd), 1001) {
                let _ = SetWindowPos(edit, None, 10, 10, w - 20, (h - 54).max(50), SWP_NOZORDER);
            }
            if let Ok(clear_btn) = GetDlgItem(Some(hwnd), 3) {
                let _ = SetWindowPos(clear_btn, None, w / 2 - 85, h - 36, 80, 26, SWP_NOZORDER);
            }
            if let Ok(close_btn) = GetDlgItem(Some(hwnd), 2) {
                let _ = SetWindowPos(close_btn, None, w / 2 + 5, h - 36, 80, 26, SWP_NOZORDER);
            }
            return LRESULT(0);
        }
        WM_CLOSE => {
            let _ = ShowWindow(hwnd, SW_HIDE);
            return LRESULT(0);
        }
        _ => {}
    }
    DefWindowProcW(hwnd, msg, wparam, lparam)
}

fn show_text_window(parent: HWND, title: &str, text: &str) {
    let class_name: Vec<u16> = "HikariTextWindow\0".encode_utf16().collect();
    let title_w: Vec<u16> = format!("{title}\0").encode_utf16().collect();

    unsafe {
        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(text_window_wndproc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: HINSTANCE(null_mut()),
            hIcon: HICON(null_mut()),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: HBRUSH((COLOR_BTNFACE.0 + 1) as isize as *mut c_void),
            lpszMenuName: PCWSTR::null(),
            lpszClassName: PCWSTR(class_name.as_ptr()),
            hIconSm: HICON(null_mut()),
        };
        let _ = RegisterClassExW(&wc);

        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            PCWSTR(class_name.as_ptr()),
            PCWSTR(title_w.as_ptr()),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            720,
            500,
            Some(parent),
            None,
            None,
            None,
        ).unwrap_or_default();

        let (font, _) = get_or_create_fonts();
        let mono_font = get_or_create_mono_font();

        let edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            w!("EDIT"),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WINDOW_STYLE(ES_MULTILINE as u32 | ES_READONLY as u32 | ES_AUTOVSCROLL as u32 | ES_AUTOHSCROLL as u32),
            10, 10, 684, 400,
            Some(hwnd),
            Some(HMENU(1001 as usize as *mut c_void)),
            None,
            None,
        ).unwrap_or_default();
        set_control_font(edit, mono_font);

        let text_w: Vec<u16> = format!("{text}\0").encode_utf16().collect();
        let _ = SetWindowTextW(edit, PCWSTR(text_w.as_ptr()));

        let close_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Close"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 310, 422, 90, 26, hwnd, Some(2));
        set_control_font(close_btn, font);

        let _ = ShowWindow(hwnd, SW_SHOW);
        let _ = UpdateWindow(hwnd);
    }
}

unsafe extern "system" fn text_window_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    match msg {
        WM_COMMAND => {
            let cmd_id = (wparam.0 & 0xFFFF) as u32;
            if cmd_id == 2 {
                let _ = DestroyWindow(hwnd);
                return LRESULT(0);
            }
        }
        WM_SIZE => {
            let mut rect = RECT::default();
            let _ = GetClientRect(hwnd, &mut rect);
            let w = rect.right - rect.left;
            let h = rect.bottom - rect.top;
            if let Ok(edit) = GetDlgItem(Some(hwnd), 1001) {
                let _ = SetWindowPos(edit, None, 10, 10, w - 20, (h - 54).max(50), SWP_NOZORDER);
            }
            if let Ok(close_btn) = GetDlgItem(Some(hwnd), 2) {
                let _ = SetWindowPos(close_btn, None, w / 2 - 45, h - 36, 90, 26, SWP_NOZORDER);
            }
            return LRESULT(0);
        }
        WM_CLOSE => {
            let _ = DestroyWindow(hwnd);
            return LRESULT(0);
        }
        _ => {}
    }
    DefWindowProcW(hwnd, msg, wparam, lparam)
}
