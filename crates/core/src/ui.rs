//! Native Windows Effect Management GUI and MMD integration for Hikari.
//!
//! Zero-overhead pure Win32 UI (comctl32 / user32) providing MME-style effect
//! assignment per model and per subset/material, with top-right menu injection
//! (MFT_RIGHTJUSTIFY) and live HLSL UI parameter tuning.

use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

use windows::core::{w, PCWSTR, PWSTR};
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::*;
use windows::Win32::UI::Controls::Dialogs::*;
use windows::Win32::UI::Controls::*;
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::effect::{HikariEffect, UIWidgetKind, ASSIGNMENT_REGISTRY};

pub const ID_MENU_ROOT: u32 = 0x4800;
pub const ID_MENU_MANAGER: u32 = 0x4801;
pub const ID_MENU_RELOAD: u32 = 0x4802;
pub const ID_MENU_ABOUT: u32 = 0x4803;

const ID_LISTVIEW: u32 = 1001;
const ID_SET_EFFECT_BTN: u32 = 1002;
const ID_REMOVE_EFFECT_BTN: u32 = 1003;
const ID_EXTRACT_SUBSETS_BTN: u32 = 1004;
const ID_REFRESH_LIST_BTN: u32 = 1005;
const ID_RELOAD_BTN: u32 = 1006;
const ID_STATUS_LABEL: u32 = 1007;

const PARAM_CONTROL_ID_BASE: u32 = 2000;

// Trackbar message constants
const TBM_GETPOS: u32 = 0x0400;
const TBM_SETPOS: u32 = 0x0405;
const TBM_SETRANGE: u32 = 0x0406;

// ListView constants
const LVM_FIRST: u32 = 0x1000;
const LVM_DELETEALLITEMS: u32 = LVM_FIRST + 9;
const LVM_GETNEXTITEM: u32 = LVM_FIRST + 12;
const LVM_SETEXTENDEDLISTVIEWSTYLE: u32 = LVM_FIRST + 54;
const LVM_SETITEMW: u32 = LVM_FIRST + 76;
const LVM_INSERTITEMW: u32 = LVM_FIRST + 77;
const LVM_INSERTCOLUMNW: u32 = LVM_FIRST + 97;

const LVS_REPORT: u32 = 0x0001;
const LVS_SINGLESEL: u32 = 0x0004;
const LVS_SHOWSELALWAYS: u32 = 0x0008;
const LVS_EX_FULLROWSELECT: u32 = 0x00000020;
const LVS_EX_GRIDLINES: u32 = 0x00000001;

const LVCF_TEXT: u32 = 0x0004;
const LVCF_WIDTH: u32 = 0x0002;
const LVCF_SUBITEM: u32 = 0x0008;

const LVIF_TEXT: u32 = 0x0001;
const LVIF_PARAM: u32 = 0x0004;
const LVIF_STATE: u32 = 0x0008;

const LVIS_FOCUSED: u32 = 0x0001;
const LVIS_SELECTED: u32 = 0x0002;

const LVNI_SELECTED: u32 = 0x0002;

const NM_CLICK: u32 = 0u32.wrapping_sub(2);
const NM_DBLCLK: u32 = 0u32.wrapping_sub(3);
const LVN_ITEMCHANGED: u32 = 0u32.wrapping_sub(101);

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
static ORIGINAL_WNDPROC: Mutex<Option<usize>> = Mutex::new(None);
static MANAGER_HWND: Mutex<Option<usize>> = Mutex::new(None);
static PARAM_CONTAINER_HWND: Mutex<Option<usize>> = Mutex::new(None);
static UI_FONT: Mutex<Option<usize>> = Mutex::new(None);
static BOLD_FONT: Mutex<Option<usize>> = Mutex::new(None);

#[derive(Clone, Debug)]
pub struct UiRowRef {
    pub object_id: Option<usize>, // None for (default) row
    pub subset: i32,              // -1 for whole model, >=0 for material subset
    pub filename: String,
    pub effect_path: String,
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

/// Ensures the Hikari menu item is injected into MMD's top menu bar (right-justified).
pub fn ensure_ui_injected() {
    if MENU_INJECTED.load(Ordering::SeqCst) {
        return;
    }
    let hwnd_opt = *crate::backend::GLOBAL_HWND.lock().unwrap();
    if let Some(h) = hwnd_opt {
        init_ui(HWND(h as *mut c_void));
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
                crate::log_msg("[UI] Subclassed MMD window procedure for F10/F5 hotkeys");
            }
        }

        // Build popup submenu
        let submenu = CreatePopupMenu().unwrap_or_default();
        if submenu.0.is_null() {
            return;
        }

        let _ = AppendMenuW(submenu, MF_STRING, ID_MENU_MANAGER as usize, w!("Менеджер эффектов...\tF10"));
        let _ = AppendMenuW(submenu, MF_STRING, ID_MENU_RELOAD as usize, w!("Перезагрузить шейдер\tF5"));
        let _ = AppendMenuW(submenu, MF_SEPARATOR, 0, PCWSTR::null());
        let _ = AppendMenuW(submenu, MF_STRING, ID_MENU_ABOUT as usize, w!("О Hikari DX11..."));

        // Insert right-justified menu item (MFT_RIGHTJUSTIFY = 0x4000)
        let title_utf16: Vec<u16> = "Hikari (H)\0".encode_utf16().collect();
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
            crate::log_msg("[UI] Injected 'Hikari (H)' menu item to top-right corner of MMD menu bar (MFT_RIGHTJUSTIFY)");
        }
    }
}

/// Subclassed WndProc for MMD's root window to intercept Hikari shortcuts & menu commands.
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
                    ID_MENU_MANAGER => {
                        toggle_effects_manager(hwnd);
                        return Some(LRESULT(0));
                    }
                    ID_MENU_RELOAD => {
                        reload_active_effect();
                        return Some(LRESULT(0));
                    }
                    ID_MENU_ABOUT => {
                        MessageBoxW(
                            Some(hwnd),
                            w!("Hikari DirectX 11 Pipeline & Shader Runtime\nВерсия 0.1.0\n\nВысокопроизводительный SM5 рендерер и Win32 менеджер параметров для MikuMikuDance."),
                            w!("О Hikari DX11"),
                            MB_OK | MB_ICONINFORMATION,
                        );
                        return Some(LRESULT(0));
                    }
                    _ => {}
                }
            }
            WM_KEYDOWN => {
                let vk = (wparam.0 & 0xFF) as u32;
                if vk == 0x79 { // VK_F10
                    toggle_effects_manager(hwnd);
                    return Some(LRESULT(0));
                } else if vk == 0x74 { // VK_F5
                    reload_active_effect();
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

/// Shows or toggles the Effects Manager window.
pub fn toggle_effects_manager(parent: HWND) {
    let mut lock = MANAGER_HWND.lock().unwrap();
    if let Some(h) = *lock {
        let hwnd = HWND(h as *mut c_void);
        if unsafe { IsWindow(Some(hwnd)) }.as_bool() {
            if unsafe { IsWindowVisible(hwnd) }.as_bool() {
                unsafe { let _ = ShowWindow(hwnd, SW_HIDE); }
            } else {
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

fn reload_active_effect() {
    let sel = *SELECTED_ROW.lock().unwrap();
    if let Some(row_idx) = sel {
        let rows = UI_ROWS.lock().unwrap();
        if row_idx < rows.len() {
            let path = rows[row_idx].effect_path.clone();
            drop(rows);
            if !path.is_empty() {
                pick_and_assign_effect_path(row_idx, &path);
                set_status_text(&format!("Шейдер '{path}' перезагружен (F5)."));
                return;
            }
        }
    }
    set_status_text("Выберите строку с шейдером для перезагрузки.");
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

fn set_control_font(ctrl: HWND, font: HFONT) {
    if !ctrl.0.is_null() && !font.0.is_null() {
        unsafe {
            send_msg(ctrl, WM_SETFONT, font.0 as usize, 1);
        }
    }
}

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

        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            PCWSTR(class_name.as_ptr()),
            w!("Hikari - Назначение эффектов MMD (DirectX 11)"),
            WS_OVERLAPPEDWINDOW & !(WS_MAXIMIZEBOX | WS_THICKFRAME),
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            880,
            620,
            Some(parent),
            None,
            None,
            None,
        ).unwrap_or_default();

        let (font, bold) = get_or_create_fonts();

        // 1. Left panel: Models & Subsets ListView
        let list_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), w!("Объекты и саб-сеты MMD:"), WINDOW_STYLE::default(), 16, 12, 500, 18, hwnd, None);
        set_control_font(list_lbl, bold);

        let listview = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            w!("SysListView32"),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | WINDOW_STYLE(LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS),
            16, 34, 500, 436,
            Some(hwnd),
            Some(HMENU(ID_LISTVIEW as usize as *mut c_void)),
            None,
            None,
        ).unwrap_or_default();
        set_control_font(listview, font);

        // Extended style: full row select + grid lines
        let _ = send_msg(listview, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES) as isize);

        // Add columns
        let mut col_obj_text: Vec<u16> = "Объект / Саб-сет\0".encode_utf16().collect();
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

        let mut col_fx_text: Vec<u16> = "Файл эффекта (.fx)\0".encode_utf16().collect();
        col.cx = 210;
        col.psz_text = col_fx_text.as_mut_ptr();
        col.cch_text_max = col_fx_text.len() as i32;
        col.i_sub_item = 1;
        let _ = send_msg(listview, LVM_INSERTCOLUMNW, 1, &col as *const _ as isize);

        // Buttons below ListView
        let set_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Задать эффект (.fx)..."), WINDOW_STYLE(BS_PUSHBUTTON as u32), 16, 478, 160, 28, hwnd, Some(ID_SET_EFFECT_BTN));
        set_control_font(set_btn, bold);

        let rem_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Снять эффект"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 182, 478, 110, 28, hwnd, Some(ID_REMOVE_EFFECT_BTN));
        set_control_font(rem_btn, font);

        let extract_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Раскрыть саб-сеты (Subset-Extract)"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 298, 478, 218, 28, hwnd, Some(ID_EXTRACT_SUBSETS_BTN));
        set_control_font(extract_btn, font);

        let refresh_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Обновить список MMD"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 16, 512, 160, 26, hwnd, Some(ID_REFRESH_LIST_BTN));
        set_control_font(refresh_btn, font);

        let reload_btn = make_child(WINDOW_EX_STYLE::default(), w!("BUTTON"), w!("Перезагрузить шейдер (F5)"), WINDOW_STYLE(BS_PUSHBUTTON as u32), 182, 512, 180, 26, hwnd, Some(ID_RELOAD_BTN));
        set_control_font(reload_btn, font);

        // 2. Right panel: Parameter tweaks
        let param_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), w!("Параметры шейдера (HLSL):"), WINDOW_STYLE::default(), 532, 12, 320, 18, hwnd, None);
        set_control_font(param_lbl, bold);

        let container = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            PCWSTR(container_class_name.as_ptr()),
            PCWSTR::null(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            532, 34, 320, 504,
            Some(hwnd),
            None,
            None,
            None,
        ).unwrap_or_default();
        *PARAM_CONTAINER_HWND.lock().unwrap() = Some(container.0 as usize);

        // 3. Status bar
        let status = make_child(
            WINDOW_EX_STYLE::default(),
            w!("STATIC"),
            w!("Готов. Выберите строку и нажмите 'Задать эффект (.fx)...' или дважды кликните."),
            WINDOW_STYLE::default(),
            16, 550, 836, 20,
            hwnd,
            Some(ID_STATUS_LABEL),
        );
        set_control_font(status, font);

        // Populate list
        refresh_assignment_list(listview);

        let _ = ShowWindow(hwnd, SW_SHOW);
        let _ = UpdateWindow(hwnd);
        hwnd
    }
}

fn set_status_text(text: &str) {
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        let parent = HWND(h as *mut c_void);
        let status = unsafe { GetDlgItem(Some(parent), ID_STATUS_LABEL as i32) };
        if let Ok(ctrl) = status {
            if !ctrl.0.is_null() {
                let w_text: Vec<u16> = text.encode_utf16().chain(std::iter::once(0)).collect();
                unsafe {
                    let _ = SetWindowTextW(ctrl, PCWSTR(w_text.as_ptr()));
                }
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
        row_ref: UiRowRef,
    }

    let mut display_rows = Vec::new();
    {
        let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
        reg.sync_host_objects();

        // 1. Default row
        let def_eff = if reg.default_effect_path.is_empty() {
            "(нет / none)".to_string()
        } else {
            std::path::Path::new(&reg.default_effect_path)
                .file_name()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| reg.default_effect_path.clone())
        };

        display_rows.push(DisplayRow {
            col0: "(по умолчанию / default)".to_string(),
            col1: def_eff,
            row_ref: UiRowRef {
                object_id: None,
                subset: -1,
                filename: "(default)".to_string(),
                effect_path: reg.default_effect_path.clone(),
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
            let title = format!("{prefix}{base_name} ({})", if obj.is_pmd { "модель" } else { "аксессуар" });

            let model_assign = reg.models.get(&obj.id);
            let model_eff_path = model_assign.map(|m| m.effect_path.as_str()).unwrap_or("");
            let eff_name = if model_eff_path.is_empty() {
                "(нет)".to_string()
            } else {
                std::path::Path::new(model_eff_path)
                    .file_name()
                    .map(|s| s.to_string_lossy().to_string())
                    .unwrap_or_else(|| model_eff_path.to_string())
            };

            display_rows.push(DisplayRow {
                col0: title,
                col1: eff_name.clone(),
                row_ref: UiRowRef {
                    object_id: Some(obj.id),
                    subset: -1,
                    filename: obj.filename.clone(),
                    effect_path: model_eff_path.to_string(),
                },
            });

            if is_expanded {
                for m in 0..obj.mat_count {
                    let is_last = m == obj.mat_count - 1;
                    let branch = if is_last { "  `-- " } else { "  |-- " };
                    let sub_title = format!("{branch}Material {m}");

                    let sub_eff_path = model_assign
                        .and_then(|ma| ma.subsets.get(&m))
                        .map(|s| s.effect_path.as_str())
                        .unwrap_or("");

                    let sub_eff_name = if sub_eff_path.is_empty() {
                        if !model_eff_path.is_empty() {
                            format!("(наследует: {eff_name})")
                        } else {
                            "(нет)".to_string()
                        }
                    } else {
                        std::path::Path::new(sub_eff_path)
                            .file_name()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or_else(|| sub_eff_path.to_string())
                    };

                    display_rows.push(DisplayRow {
                        col0: sub_title,
                        col1: sub_eff_name,
                        row_ref: UiRowRef {
                            object_id: Some(obj.id),
                            subset: m,
                            filename: obj.filename.clone(),
                            effect_path: sub_eff_path.to_string(),
                        },
                    });
                }
            }
        }
    } // reg lock dropped!

    {
        let mut rows = UI_ROWS.lock().unwrap();
        rows.clear();
        for r in &display_rows {
            rows.push(r.row_ref.clone());
        }
    } // rows lock dropped!

    IS_REFRESHING.store(true, Ordering::SeqCst);
    unsafe {
        let _ = send_msg(listview, WM_SETREDRAW, 0, 0);
        let _ = send_msg(listview, LVM_DELETEALLITEMS, 0, 0);

        for (cur_row, r) in display_rows.iter().enumerate() {
            insert_list_item(listview, cur_row as i32, &r.col0, &r.col1);
        }

        let _ = send_msg(listview, WM_SETREDRAW, 1, 0);
        let _ = InvalidateRect(Some(listview), None, true);
    }
    IS_REFRESHING.store(false, Ordering::SeqCst);
    crate::log_msg(&format!("[UI] refresh_assignment_list completed: {} items loaded", display_rows.len()));
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
        match row.object_id {
            None => reg.default_effect,
            Some(obj_id) => {
                if let Some(m) = reg.models.get(&obj_id) {
                    if row.subset >= 0 {
                        m.subsets.get(&row.subset)
                            .and_then(|s| s.effect)
                            .or(m.effect)
                            .or(reg.default_effect)
                    } else {
                        m.effect.or(reg.default_effect)
                    }
                } else {
                    reg.default_effect
                }
            }
        }
    };

    *ACTIVE_TARGET_EFFECT.lock().unwrap() = effect_ptr.map(|p| p as usize);

    crate::log_msg(&format!(
        "[UI] on_row_selected: row #{row_idx} ('{}'), effect={:?}",
        row.filename,
        effect_ptr.map(|p| p as usize)
    ));

    if let Some(c) = *PARAM_CONTAINER_HWND.lock().unwrap() {
        let container = HWND(c as *mut c_void);
        rebuild_param_controls(container, effect_ptr);
    }
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
    let filter = w!("HLSL Effect Files (*.fx)\0*.fx\0All Files (*.*)\0*.*\0\0");
    let title = w!("Выберите эффект DirectX 11 (.fx)");

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
    let res = match row.object_id {
        None => reg.assign_default_effect(path),
        Some(obj_id) => {
            if row.subset >= 0 {
                reg.assign_subset_effect(obj_id, &row.filename, row.subset, path)
            } else {
                reg.assign_model_effect(obj_id, &row.filename, path)
            }
        }
    };

    drop(reg);

    match res {
        Ok(()) => {
            set_status_text(&format!("Эффект успешно назначен: {path}"));
            if let Some(h) = *MANAGER_HWND.lock().unwrap() {
                let parent = HWND(h as *mut c_void);
                if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
                    refresh_assignment_list(lv);
                }
            }
            on_row_selected(row_idx);
        }
        Err(e) => {
            set_status_text(&format!("Ошибка компиляции эффекта: {e}"));
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
    let _ = match row.object_id {
        None => reg.assign_default_effect(""),
        Some(obj_id) => {
            if row.subset >= 0 {
                reg.assign_subset_effect(obj_id, &row.filename, row.subset, "")
            } else {
                reg.assign_model_effect(obj_id, &row.filename, "")
            }
        }
    };
    drop(reg);

    let target_row = {
        let rows = UI_ROWS.lock().unwrap();
        if rows.is_empty() {
            None
        } else {
            Some(row_idx.min(rows.len() - 1))
        }
    };

    set_status_text("Эффект снят.");
    if let Some(h) = *MANAGER_HWND.lock().unwrap() {
        let parent = HWND(h as *mut c_void);
        if let Ok(lv) = unsafe { GetDlgItem(Some(parent), ID_LISTVIEW as i32) } {
            refresh_assignment_list(lv);
        }
    }
    if let Some(idx) = target_row {
        on_row_selected(idx);
    }
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
        }
    }
}

fn rebuild_param_controls(container: HWND, target_effect: Option<*mut HikariEffect>) {
    unsafe {
        // Destroy existing controls in container safely
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
                    w!("Шейдер не назначен для выбранного объекта.\n\nНажмите 'Задать эффект (.fx)...', чтобы наложить шейдер."),
                    WINDOW_STYLE::default(),
                    14, 20, 290, 80,
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
                let grp_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_group.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 290, 18, container, None);
                set_control_font(grp_lbl, bold);
                current_y += 22;
            }

            let name_disp = if !ui_desc.label.is_empty() { &ui_desc.label } else { &param.name };
            let w_label: Vec<u16> = format!("{}:", name_disp).encode_utf16().chain(std::iter::once(0)).collect();
            let lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_label.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 290, 18, container, None);
            set_control_font(lbl, font);
            current_y += 20;

            if !ui_desc.help.is_empty() {
                let w_help: Vec<u16> = format!("  {}", ui_desc.help).encode_utf16().chain(std::iter::once(0)).collect();
                let help_lbl = make_child(WINDOW_EX_STYLE::default(), w!("STATIC"), PCWSTR(w_help.as_ptr()), WINDOW_STYLE::default(), 10, current_y, 290, 16, container, None);
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
                        210,
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
                        226,
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
                        w!("Включить"),
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
                        w!("Выбрать цвет..."),
                        WINDOW_STYLE(BS_PUSHBUTTON as u32),
                        10,
                        current_y,
                        130,
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
                        148,
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
                        280,
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
        if current_y > 500 {
            let si = SCROLLINFO {
                cbSize: std::mem::size_of::<SCROLLINFO>() as u32,
                fMask: SIF_RANGE | SIF_PAGE,
                nMin: 0,
                nMax: current_y,
                nPage: 500,
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

/// Window procedure for the Effects Manager dialog.
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
            if nmhdr.id_from == ID_LISTVIEW as usize {
                if IS_REFRESHING.load(Ordering::Relaxed) {
                    return LRESULT(0);
                }
                if nmhdr.code == LVN_ITEMCHANGED {
                    let nmlv = &*(lparam.0 as *const NMLISTVIEW);
                    if (nmlv.u_changed & LVIF_STATE) != 0
                        && (nmlv.u_new_state & LVIS_SELECTED) != 0
                        && (nmlv.u_old_state & LVIS_SELECTED) == 0
                    {
                        if nmlv.i_item >= 0 {
                            on_row_selected(nmlv.i_item as usize);
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

            match cmd_id {
                ID_SET_EFFECT_BTN => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        pick_and_assign_effect(row_idx);
                    } else {
                        set_status_text("Сначала выберите объект в списке слева.");
                    }
                    return LRESULT(0);
                }
                ID_REMOVE_EFFECT_BTN => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        remove_assigned_effect(row_idx);
                    }
                    return LRESULT(0);
                }
                ID_EXTRACT_SUBSETS_BTN => {
                    let sel = *SELECTED_ROW.lock().unwrap();
                    if let Some(row_idx) = sel {
                        toggle_selected_subset_expand(row_idx);
                    }
                    return LRESULT(0);
                }
                ID_REFRESH_LIST_BTN => {
                    let listview = GetDlgItem(Some(hwnd), ID_LISTVIEW as i32).unwrap_or_default();
                    refresh_assignment_list(listview);
                    set_status_text("Список объектов MMD синхронизирован.");
                    return LRESULT(0);
                }
                ID_RELOAD_BTN => {
                    reload_active_effect();
                    return LRESULT(0);
                }
                _ => {}
            }

            // Check if this command belongs to a parameter control
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
