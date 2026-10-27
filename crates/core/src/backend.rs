use std::ffi::c_void;
use std::sync::Mutex;
use windows::Win32::Foundation::HWND;

use crate::d3d11::D3D11Context;
use crate::vtable::{
    execute_dx11_composite, execute_dx11_draw_indexed, register_d3d9_device,
    BOUND_IB, BOUND_TEXTURES, BOUND_VB, GLOBAL_D3D11,
};

pub static GLOBAL_HWND: Mutex<Option<usize>> = Mutex::new(None);
pub static ACTIVE_EFFECT: Mutex<Option<usize>> = Mutex::new(None);

pub fn on_device_created(device: *mut c_void, hwnd: HWND) {
    crate::log_msg(&format!(
        "[backend] IDirect3DDevice9 registered: {:p}, hwnd={:?}",
        device, hwnd.0
    ));
    unsafe {
        register_d3d9_device(device);
    }
    *GLOBAL_HWND.lock().unwrap() = Some(hwnd.0 as usize);

    // Initialize D3D11 Context
    let mut d3d11_lock = GLOBAL_D3D11.lock().unwrap();
    if d3d11_lock.is_none() {
        match D3D11Context::new(hwnd, 0, 0) {
            Ok(ctx) => {
                crate::log_msg("[backend] D3D11Context created successfully");
                *d3d11_lock = Some(ctx);
            }
            Err(e) => {
                crate::log_msg(&format!("[backend] Failed to create D3D11Context: {:?}", e));
            }
        }
    }
}

pub fn on_begin_scene(_device: *mut c_void) {
    // BeginScene hook
}

pub fn on_end_scene(device: *mut c_void) {
    unsafe {
        execute_dx11_composite(device);
    }
}

pub fn on_clear(_flags: u32, _color: u32, _z: f32, _stencil: u32) {
    // Clear hook
}

pub fn on_set_viewport(x: u32, y: u32, w: u32, h: u32) {
    if let Ok(guard) = GLOBAL_D3D11.lock() {
        if let Some(ref ctx) = *guard {
            let vp = windows::Win32::Graphics::Direct3D11::D3D11_VIEWPORT {
                TopLeftX: x as f32,
                TopLeftY: y as f32,
                Width: w as f32,
                Height: h as f32,
                MinDepth: 0.0,
                MaxDepth: 1.0,
            };
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
    crate::effect::ACTIVE_EFFECT_PASS.lock().unwrap().is_some()
        || ACTIVE_EFFECT.lock().unwrap().is_some()
}

pub fn on_draw_indexed_primitive_dx11(
    this: *mut c_void,
    prim_type: u32,
    base_vertex_index: i32,
    min_vertex_index: u32,
    num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) -> bool {
    unsafe {
        execute_dx11_draw_indexed(
            this, prim_type, base_vertex_index, min_vertex_index, num_vertices, start_index, prim_count
        )
    }
}
