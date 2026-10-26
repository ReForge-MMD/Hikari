//! Direct memory structures and state access for MikuMikuDance (MMD x64).

use windows::Win32::System::LibraryLoader::GetModuleHandleW;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct Float3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CameraKeyFrameData {
    pub frame_no: i32,
    pub pre_index: i32,
    pub next_index: i32,
    pub length: f32,
    pub xyz: Float3,
    pub rxyz: Float3,
    pub hokan1_x: [u8; 6],
    pub hokan1_y: [u8; 6],
    pub hokan2_x: [u8; 6],
    pub hokan2_y: [u8; 6],
    pub is_perspective: i32,
    pub view_angle: i32,
    pub is_selected: i32,
    pub looking_model_index: i32,
    pub looking_bone_index: i32,
}

#[repr(C)]
pub struct BoneCurrentData {
    pub name_jp: [u8; 20],
    pub name_en: [u8; 20],
    pub unknown10: [i32; 5],
    pub unknown_mat: [[[f32; 4]; 4]; 4],
    pub init_x: f32,
    pub init_y: f32,
    pub init_z: f32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub unknown10_rotation_q: [f32; 4],
    pub unknown20_rotation_q: [f32; 4],
    pub x2: f32,
    pub y2: f32,
    pub z2: f32,
    pub unknown30_rotation_q: [f32; 4],
    pub unknown30: [i32; 56],
}

const _: () = assert!(std::mem::size_of::<BoneCurrentData>() == 624);

#[repr(C)]
pub struct MMDModelData {
    pub unknown10: [i32; 2224],
    pub name_jp: [u8; 50],
    pub name_en: [u8; 50],
    pub comment_jp: [u8; 256],
    pub comment_en: [u8; 292],
    pub file_path: [u16; 256],
    pub bone_current_data: *mut BoneCurrentData,
    pub unknown20: [i32; 10],
    pub keyframe_editor_toplevel_rows: u8,
    pub unknown30: [*mut std::ffi::c_void; 2],
    pub bone_keyframe: *mut std::ffi::c_void,
    pub morph_keyframe: *mut std::ffi::c_void,
    pub configuration_keyframe: *mut std::ffi::c_void,
    pub unknown40: [i32; 600],
    pub render_order: u8,
    pub morph_count: i32,
    pub bone_count: i32,
    pub ik_count: i32,
    pub unknown45: u8,
    pub is_visible: u8,
    pub selected_bone: i32,
    pub unknown50: [i32; 4],
    pub selected_morph_indices: [i32; 4],
}

#[repr(C)]
pub struct MMDMainData {
    pub unknown10: [i32; 2],
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub pre_mouse_x: i32,
    pub pre_mouse_y: i32,
    pub keys: [i32; 26],
    pub unknown20: [i32; 14],
    pub key_enter: i32,
    pub key_ctrl: i32,
    pub key_alt: i32,
    pub unknown30: i32,
    pub unknown_pointer: *mut std::ffi::c_void,
    pub unknown40: [i32; 155],
    pub rxyz: Float3,
    pub unknown49: [i32; 2],
    pub counter_f: f32,
    pub counter: i32,
    pub unknown50: [i32; 3],
    pub xyz: Float3,
    pub unknown60: [i32; 22],
    pub camera_key_frame: [CameraKeyFrameData; 10000],
    pub unknown_pointer20: [*mut std::ffi::c_void; 258],
    pub model_data: [*mut MMDModelData; 255],
    pub select_model: i32,
    pub select_bone_type: i32,
    pub unknown70: [i32; 4],
    pub unknown71: f32,
    pub mouse_over_move: i32,
    pub unknown80: [i32; 17],
    pub left_frame: i32,
    pub unknown90: i32,
    pub pre_left_frame: i32,
    pub now_frame: i32,
}

/// Safely inspects MMD's static state pointer.
pub fn get_mmd_main_data() -> Option<&'static mut MMDMainData> {
    unsafe {
        let base = GetModuleHandleW(None).unwrap_or_default().0 as usize;
        if base == 0 {
            return None;
        }

        let ptr_addr = (base + 0x1445F8) as *const *mut MMDMainData;
        if ptr_addr.is_null() {
            return None;
        }

        // Pointer check
        let data_ptr = *ptr_addr;
        if data_ptr.is_null() {
            return None;
        }

        Some(&mut *data_ptr)
    }
}
