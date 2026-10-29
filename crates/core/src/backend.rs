use std::sync::Mutex;
use windows::Win32::Foundation::HWND;

use crate::d3d11::D3D11Context;
use crate::vtable::{
    BOUND_IB, BOUND_TEXTURES, BOUND_VB, GLOBAL_D3D11,
};

pub static GLOBAL_HWND: Mutex<Option<usize>> = Mutex::new(None);
pub static ACTIVE_EFFECT: Mutex<Option<usize>> = Mutex::new(None);

pub fn on_device_created(hwnd: HWND) {
    crate::log_msg(&format!(
        "[backend] Window registered: hwnd={:?}",
        hwnd.0
    ));
    *GLOBAL_HWND.lock().unwrap() = Some(hwnd.0 as usize);

    // Initialize D3D11 Context
    let mut d3d11_lock = GLOBAL_D3D11.lock().unwrap();
    if d3d11_lock.is_none() {
        match D3D11Context::new(hwnd, 0, 0) {
            Ok(ctx) => {
                crate::log_msg("[backend] D3D11Context created successfully");
                *d3d11_lock = Some(ctx);
                crate::ui::init_ui(hwnd);
            }
            Err(e) => {
                crate::log_msg(&format!("[backend] Failed to create D3D11Context: {:?}", e));
            }
        }
    } else {
        crate::ui::init_ui(hwnd);
    }
}

pub fn on_device_reset_pre() {
    crate::vtable::release_d3d11_resources();
}

pub fn on_device_reset_post(width: u32, height: u32) {
    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ref mut ctx) = *guard {
            let _ = ctx.resize(width, height);
        }
    }
}

pub static CURRENT_WORLD_MATRIX: Mutex<[f32; 16]> = Mutex::new([
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
]);
pub static CURRENT_VIEW_MATRIX: Mutex<[f32; 16]> = Mutex::new([
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
]);
pub static CURRENT_PROJ_MATRIX: Mutex<[f32; 16]> = Mutex::new([
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
]);

pub fn on_set_transform(state: u32, matrix_ptr: *const f32) {
    if matrix_ptr.is_null() {
        return;
    }
    let mut mat = [0.0f32; 16];
    unsafe {
        std::ptr::copy_nonoverlapping(matrix_ptr, mat.as_mut_ptr(), 16);
    }
    match state {
        2 => *CURRENT_VIEW_MATRIX.lock().unwrap() = mat,
        3 => *CURRENT_PROJ_MATRIX.lock().unwrap() = mat,
        256..=511 => *CURRENT_WORLD_MATRIX.lock().unwrap() = mat,
        _ => {}
    }
}

fn mul_mat4(a: &[f32; 16], b: &[f32; 16]) -> [f32; 16] {
    let mut r = [0.0f32; 16];
    for row in 0..4 {
        for col in 0..4 {
            let mut sum = 0.0f32;
            for k in 0..4 {
                sum += a[row * 4 + k] * b[k * 4 + col];
            }
            r[row * 4 + col] = sum;
        }
    }
    r
}

pub fn update_effect_standard_matrices(eff: &mut crate::effect::HikariEffect) {
    let w = *CURRENT_WORLD_MATRIX.lock().unwrap();
    let v = *CURRENT_VIEW_MATRIX.lock().unwrap();
    let p = *CURRENT_PROJ_MATRIX.lock().unwrap();
    let wv = mul_mat4(&w, &v);
    let wvp = mul_mat4(&wv, &p);

    let o = eff.std_offsets;
    let cdata = &mut eff.constant_data;

    let write_mat = |cdata: &mut Vec<u8>, off_opt: Option<usize>, mat: &[f32; 16]| -> bool {
        if let Some(off) = off_opt {
            if off + 64 <= cdata.len() {
                let slice = unsafe { std::slice::from_raw_parts(mat.as_ptr() as *const u8, 64) };
                cdata[off..off + 64].copy_from_slice(slice);
                return true;
            }
        }
        false
    };

    let mut dirty = false;
    dirty |= write_mat(cdata, o.world, &w);
    dirty |= write_mat(cdata, o.view, &v);
    dirty |= write_mat(cdata, o.proj, &p);
    dirty |= write_mat(cdata, o.world_view, &wv);
    dirty |= write_mat(cdata, o.world_view_proj, &wvp);

    // Compute CameraPosition from View matrix
    let cam_x = -(v[12] * v[0] + v[13] * v[4] + v[14] * v[8]);
    let cam_y = -(v[12] * v[1] + v[13] * v[5] + v[14] * v[9]);
    let cam_z = -(v[12] * v[2] + v[13] * v[6] + v[14] * v[10]);
    let cam_pos = [cam_x, cam_y, cam_z, 1.0f32];

    let write_vec4 = |cdata: &mut Vec<u8>, off_opt: Option<usize>, vec: &[f32; 4]| -> bool {
        if let Some(off) = off_opt {
            if off + 16 <= cdata.len() {
                let slice = unsafe { std::slice::from_raw_parts(vec.as_ptr() as *const u8, 16) };
                cdata[off..off + 16].copy_from_slice(slice);
                return true;
            }
        }
        false
    };

    dirty |= write_vec4(cdata, o.camera_pos, &cam_pos);
    dirty |= write_vec4(cdata, o.light_dir, &[0.5, 1.0, -0.5, 0.0]);
    dirty |= write_vec4(cdata, o.mat_diffuse, &[1.0, 1.0, 1.0, 1.0]);
    dirty |= write_vec4(cdata, o.mat_ambient, &[0.5, 0.5, 0.5, 1.0]);
    dirty |= write_vec4(cdata, o.mat_specular, &[0.0, 0.0, 0.0, 1.0]);

    if dirty {
        eff.constant_dirty = true;
    }
}

pub static CURRENT_VIEWPORT: Mutex<Option<windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT>> = Mutex::new(None);

pub fn on_begin_scene() {
    // BeginScene hook
}

pub fn on_end_scene() {
    if let Ok(mut guard) = GLOBAL_D3D11.lock() {
        if let Some(ref mut ctx) = *guard {
            ctx.frame_counter = ctx.frame_counter.wrapping_add(1);
        }
    }
    crate::ui::ensure_ui_injected();
    static LAST_HOST_FINGERPRINT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let fp = crate::mmd_host::get_host_objects_fingerprint();
    if fp != LAST_HOST_FINGERPRINT.swap(fp, std::sync::atomic::Ordering::Relaxed) {
        if let Ok(mut reg) = crate::effect::ASSIGNMENT_REGISTRY.lock() {
            reg.sync_host_objects();
        }
        crate::ui::notify_host_objects_changed();
    }
    crate::effect::drain_deferred_effects();
}

pub fn on_clear(_flags: u32, _color: u32, _z: f32, _stencil: u32) {
    // Clear hook
}

pub fn on_set_viewport(x: u32, y: u32, w: u32, h: u32) {
    let vp = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
        TopLeftX: x as f32,
        TopLeftY: y as f32,
        Width: w as f32,
        Height: h as f32,
        MinDepth: 0.0,
        MaxDepth: 1.0,
    };
    *CURRENT_VIEWPORT.lock().unwrap() = Some(vp);
    if let Ok(guard) = GLOBAL_D3D11.lock() {
        if let Some(ref ctx) = *guard {
            unsafe {
                ctx.context.RSSetViewports(Some(&[vp]));
            }
        }
    }
}

pub fn on_set_texture(stage: u32, ptr: usize) {
    if (stage as usize) < 8 {
        let mut textures = BOUND_TEXTURES.lock().unwrap();
        textures[stage as usize] = if ptr != 0 { Some(ptr) } else { None };
    }
}

pub fn on_set_stream_source(stream: u32, ptr: usize, offset: u32, stride: u32) {
    if stream == 0 {
        let mut bound_vb = BOUND_VB.lock().unwrap();
        *bound_vb = if ptr != 0 {
            Some((ptr, stride, offset))
        } else {
            None
        };
    }
}

pub fn on_set_indices(ptr: usize) {
    let mut bound_ib = BOUND_IB.lock().unwrap();
    *bound_ib = if ptr != 0 { Some(ptr) } else { None };
}

pub fn should_render_dx11() -> bool {
    if !crate::effect::EFFECTS_ENABLED.load(std::sync::atomic::Ordering::Acquire) {
        return false;
    }
    crate::effect::HAS_ASSIGNED_EFFECTS.load(std::sync::atomic::Ordering::Relaxed)
        || crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap().is_some()
        || ACTIVE_EFFECT.lock().unwrap().is_some()
        || crate::effect::EFFECT_REGISTRY.lock().unwrap().iter().any(|e| e.enabled)
}
